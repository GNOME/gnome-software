/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <stdlib.h>

#include "gs-upgrade-banner.h"
#include "gs-common.h"

typedef struct
{
	GsApp		*app;

	GtkWidget	*box_upgrades_info;
	GtkWidget	*box_upgrades_download;
	GtkWidget	*box_upgrades_downloading;
	GtkWidget	*box_upgrades_install;
	GtkWidget	*button_upgrades_download;
	GtkWidget	*button_upgrades_install;
	GtkWidget	*button_upgrades_cancel;
	GtkWidget	*label_upgrades_summary;
	GtkWidget	*label_upgrades_title;
	GtkWidget	*label_download_info;
	GtkWidget	*label_upgrades_downloading_spacer;
	GtkWidget	*label_upgrades_downloading;
	GtkWidget	*progressbar;
	guint		 progress_pulse_id;
	GtkCssProvider	*banner_provider;  /* (owned) (nullable) */
} GsUpgradeBannerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsUpgradeBanner, gs_upgrade_banner, GTK_TYPE_BIN)

enum {
	SIGNAL_DOWNLOAD_CLICKED,
	SIGNAL_INSTALL_CLICKED,
	SIGNAL_CANCEL_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

static gboolean
_pulse_cb (gpointer user_data)
{
	GsUpgradeBanner *self = GS_UPGRADE_BANNER (user_data);
	GsUpgradeBannerPrivate *priv = gs_upgrade_banner_get_instance_private (self);

	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (priv->progressbar));

	return G_SOURCE_CONTINUE;
}

static void
stop_progress_pulsing (GsUpgradeBanner *self)
{
	GsUpgradeBannerPrivate *priv = gs_upgrade_banner_get_instance_private (self);

	if (priv->progress_pulse_id != 0) {
		g_source_remove (priv->progress_pulse_id);
		priv->progress_pulse_id = 0;
	}
}

static void
gs_upgrade_banner_refresh (GsUpgradeBanner *self)
{
	GsUpgradeBannerPrivate *priv = gs_upgrade_banner_get_instance_private (self);
	const gchar *uri, *summary;
	g_autofree gchar *str = NULL;
	guint percentage;

	if (priv->app == NULL)
		return;

	/* TRANSLATORS: This is the text displayed when a distro
	 * upgrade is available. The first %s is the distro name
	 * and the 2nd %s is the version, e.g. "Fedora 35 Available" */
	str = g_strdup_printf (_("%s %s Available"), gs_app_get_name (priv->app), gs_app_get_version (priv->app));
	gtk_label_set_markup (GTK_LABEL (priv->label_upgrades_title), str);

	/* Normally a distro upgrade state goes from
	 *
	 * AVAILABLE (available to download) to
	 * INSTALLING (downloading packages for later installation) to
	 * UPDATABLE (packages are downloaded and upgrade is ready to go)
	 */
	switch (gs_app_get_state (priv->app)) {
	case GS_APP_STATE_AVAILABLE:
	case GS_APP_STATE_QUEUED_FOR_INSTALL:
		gtk_widget_show (priv->box_upgrades_download);
		gtk_widget_hide (priv->box_upgrades_downloading);
		gtk_widget_hide (priv->box_upgrades_install);
		break;
	case GS_APP_STATE_INSTALLING:
		gtk_widget_hide (priv->box_upgrades_download);
		gtk_widget_show (priv->box_upgrades_downloading);
		gtk_widget_hide (priv->box_upgrades_install);
		break;
	case GS_APP_STATE_UPDATABLE:
		gtk_widget_hide (priv->box_upgrades_download);
		gtk_widget_hide (priv->box_upgrades_downloading);
		gtk_widget_show (priv->box_upgrades_install);
		break;
	default:
		g_critical ("Unexpected app state ‘%s’ of app ‘%s’",
			    gs_app_state_to_string (gs_app_get_state (priv->app)),
			    gs_app_get_unique_id (priv->app));
		break;
	}

	/* Hide the upgrade box until the app state is known. */
	gtk_widget_set_visible (GTK_WIDGET (self),
				(gs_app_get_state (priv->app) != GS_APP_STATE_UNKNOWN));

	/* Refresh the summary if we got anything better than the default blurb */
	summary = gs_app_get_summary (priv->app);
	if (summary != NULL)
		gtk_label_set_text (GTK_LABEL (priv->label_upgrades_summary), summary);

	uri = gs_app_get_url (priv->app, AS_URL_KIND_HOMEPAGE);
	if (uri != NULL) {
		g_autofree gchar *link = NULL;
		link = g_markup_printf_escaped ("<a href=\"%s\">%s</a>", uri, _("Learn about the new version"));
		gtk_label_set_markup (GTK_LABEL (priv->label_download_info), link);
	} else if (gs_app_get_size_download (priv->app) != GS_APP_SIZE_UNKNOWABLE &&
		   gs_app_get_size_download (priv->app) != 0) {
		g_autofree gchar *tmp;
		g_clear_pointer (&str, g_free);
		tmp = g_format_size (gs_app_get_size_download (priv->app));
		/* Translators: the '%s' is replaced with the download size, forming text like "2 GB download" */
		str = g_strdup_printf ("%s download", tmp);
		gtk_label_set_text (GTK_LABEL (priv->label_download_info), str);
	}

	/* do a fill bar for the current progress */
	switch (gs_app_get_state (priv->app)) {
	case GS_APP_STATE_INSTALLING:
		percentage = gs_app_get_progress (priv->app);
		if (percentage == GS_APP_PROGRESS_UNKNOWN) {
			if (priv->progress_pulse_id == 0)
				priv->progress_pulse_id = g_timeout_add (50, _pulse_cb, self);

			gtk_label_set_text (GTK_LABEL (priv->label_upgrades_downloading), _("Downloading…"));
			break;
		} else if (percentage <= 100) {
			stop_progress_pulsing (self);
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->progressbar),
						       (gdouble) percentage / 100.f);
			g_clear_pointer (&str, g_free);
			if (gs_app_get_size_download (priv->app) != GS_APP_SIZE_UNKNOWABLE &&
			    gs_app_get_size_download (priv->app) != 0) {
				g_autofree gchar *tmp = NULL;
				g_autofree gchar *downloaded_tmp = NULL;
				guint64 downloaded;

				downloaded = gs_app_get_size_download (priv->app) * percentage / 100.0;
				downloaded_tmp = g_format_size (downloaded);
				tmp = g_format_size (gs_app_get_size_download (priv->app));
				/* Translators: the first '%s' is replaced with the downloaded size, the second '%s'
				   with the total download size, forming text like "135 MB of 2 GB downloaded" */
				str = g_strdup_printf (_("%s of %s downloaded"), downloaded_tmp, tmp);
			} else {
				/* Translators: the '%u' is replaced with the actual percentage being already
				   downloaded, forming text like "13% downloaded" */
				str = g_strdup_printf (_("%u%% downloaded"), percentage);
			}
			gtk_label_set_text (GTK_LABEL (priv->label_upgrades_downloading), str);
			break;
		}
		break;
	default:
		stop_progress_pulsing (self);
		break;
	}
}

static gboolean
app_refresh_idle (gpointer user_data)
{
	GsUpgradeBanner *self = GS_UPGRADE_BANNER (user_data);

	gs_upgrade_banner_refresh (self);

	g_object_unref (self);
	return G_SOURCE_REMOVE;
}

static void
app_state_changed (GsApp *app, GParamSpec *pspec, GsUpgradeBanner *self)
{
	g_idle_add (app_refresh_idle, g_object_ref (self));
}

static void
app_progress_changed (GsApp *app, GParamSpec *pspec, GsUpgradeBanner *self)
{
	g_idle_add (app_refresh_idle, g_object_ref (self));
}

static void
download_button_cb (GtkWidget *widget, GsUpgradeBanner *self)
{
	g_signal_emit (self, signals[SIGNAL_DOWNLOAD_CLICKED], 0);
}

static void
install_button_cb (GtkWidget *widget, GsUpgradeBanner *self)
{
	g_signal_emit (self, signals[SIGNAL_INSTALL_CLICKED], 0);
}

static void
cancel_button_cb (GtkWidget *widget, GsUpgradeBanner *self)
{
	g_signal_emit (self, signals[SIGNAL_CANCEL_CLICKED], 0);
}

static gboolean
cancel_button_size_allocate_cb (GtkWidget *widget,
				GdkRectangle *allocation,
				gpointer user_data)
{
	GsUpgradeBanner *self = user_data;
	GsUpgradeBannerPrivate *priv = gs_upgrade_banner_get_instance_private (self);
	gint size = allocation->width;

	/* Make sure it's a square button, thus looks like a circle */
	if (allocation->width != allocation->height) {
		size = MAX (allocation->width, allocation->height);
		gtk_widget_set_size_request (widget, size, size);
	}

	/* Also let the spacer be of the same size, to have the progress bar centered */
	size += gtk_widget_get_margin_start (widget) + gtk_widget_get_margin_end (widget);
	gtk_widget_set_size_request (priv->label_upgrades_downloading_spacer, size, -1);

	return FALSE;
}

static gboolean
box_upgrades_download_size_allocate_cb (GtkWidget *widget,
					GdkRectangle *allocation,
					gpointer user_data)
{
	GsUpgradeBanner *self = user_data;
	GsUpgradeBannerPrivate *priv = gs_upgrade_banner_get_instance_private (self);
	gint size = allocation->height;

	/* Make them all the same height, ruled by the first box shown */
	gtk_widget_set_size_request (priv->box_upgrades_downloading, -1, size);
	gtk_widget_set_size_request (priv->box_upgrades_install, -1, size);

	return FALSE;
}

void
gs_upgrade_banner_set_app (GsUpgradeBanner *self, GsApp *app)
{
	GsUpgradeBannerPrivate *priv = gs_upgrade_banner_get_instance_private (self);
	const gchar *css;
	g_autofree gchar *modified_css = NULL;

	g_return_if_fail (GS_IS_UPGRADE_BANNER (self));
	g_return_if_fail (GS_IS_APP (app) || app == NULL);

	if (priv->app) {
		g_signal_handlers_disconnect_by_func (priv->app, app_state_changed, self);
		g_signal_handlers_disconnect_by_func (priv->app, app_progress_changed, self);
	}

	g_set_object (&priv->app, app);
	if (!app)
		return;

	g_signal_connect (priv->app, "notify::state",
			  G_CALLBACK (app_state_changed), self);
	g_signal_connect (priv->app, "notify::progress",
	                  G_CALLBACK (app_progress_changed), self);

	/* perhaps set custom css */
	css = gs_app_get_metadata_item (app, "GnomeSoftware::UpgradeBanner-css");
	modified_css = gs_utils_set_key_colors_in_css (css, app);
	gs_utils_widget_set_css (priv->box_upgrades_info, &priv->banner_provider, "upgrade-banner-custom", modified_css);

	gs_upgrade_banner_refresh (self);
}

GsApp *
gs_upgrade_banner_get_app (GsUpgradeBanner *self)
{
	GsUpgradeBannerPrivate *priv = gs_upgrade_banner_get_instance_private (self);

	g_return_val_if_fail (GS_IS_UPGRADE_BANNER (self), NULL);

	return priv->app;
}

static void
gs_upgrade_banner_dispose (GObject *object)
{
	GsUpgradeBanner *self = GS_UPGRADE_BANNER (object);
	GsUpgradeBannerPrivate *priv = gs_upgrade_banner_get_instance_private (self);

	g_clear_object (&priv->banner_provider);

	G_OBJECT_CLASS (gs_upgrade_banner_parent_class)->dispose (object);
}

static void
gs_upgrade_banner_destroy (GtkWidget *widget)
{
	GsUpgradeBanner *self = GS_UPGRADE_BANNER (widget);
	GsUpgradeBannerPrivate *priv = gs_upgrade_banner_get_instance_private (self);

	stop_progress_pulsing (self);

	if (priv->app) {
		g_signal_handlers_disconnect_by_func (priv->app, app_state_changed, self);
		g_signal_handlers_disconnect_by_func (priv->app, app_progress_changed, self);
	}

	g_clear_object (&priv->app);

	GTK_WIDGET_CLASS (gs_upgrade_banner_parent_class)->destroy (widget);
}

static void
gs_upgrade_banner_init (GsUpgradeBanner *self)
{
	GsUpgradeBannerPrivate *priv = gs_upgrade_banner_get_instance_private (self);

	gtk_widget_init_template (GTK_WIDGET (self));

	g_signal_connect (priv->button_upgrades_download, "clicked",
	                  G_CALLBACK (download_button_cb),
	                  self);
	g_signal_connect (priv->button_upgrades_install, "clicked",
	                  G_CALLBACK (install_button_cb),
	                  self);
	g_signal_connect (priv->button_upgrades_cancel, "clicked",
	                  G_CALLBACK (cancel_button_cb),
	                  self);
	g_signal_connect (priv->button_upgrades_cancel, "size-allocate",
			  G_CALLBACK (cancel_button_size_allocate_cb), self);
	g_signal_connect (priv->box_upgrades_download, "size-allocate",
			  G_CALLBACK (box_upgrades_download_size_allocate_cb), self);
}

static void
gs_upgrade_banner_class_init (GsUpgradeBannerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_upgrade_banner_dispose;
	widget_class->destroy = gs_upgrade_banner_destroy;

	signals [SIGNAL_DOWNLOAD_CLICKED] =
		g_signal_new ("download-clicked",
		              G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (GsUpgradeBannerClass, download_clicked),
		              NULL, NULL, g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	signals [SIGNAL_INSTALL_CLICKED] =
		g_signal_new ("install-clicked",
		              G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (GsUpgradeBannerClass, install_clicked),
		              NULL, NULL, g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	signals [SIGNAL_CANCEL_CLICKED] =
		g_signal_new ("cancel-clicked",
		              G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (GsUpgradeBannerClass, cancel_clicked),
		              NULL, NULL, g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-upgrade-banner.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, box_upgrades_info);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, box_upgrades_download);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, box_upgrades_downloading);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, box_upgrades_install);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, button_upgrades_download);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, button_upgrades_install);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, button_upgrades_cancel);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, label_upgrades_summary);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, label_upgrades_title);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, label_download_info);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, label_upgrades_downloading);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, label_upgrades_downloading_spacer);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, progressbar);
}

GtkWidget *
gs_upgrade_banner_new (void)
{
	GsUpgradeBanner *self;
	self = g_object_new (GS_TYPE_UPGRADE_BANNER,
			     "vexpand", FALSE,
			     NULL);
	return GTK_WIDGET (self);
}
