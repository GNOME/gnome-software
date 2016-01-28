/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Kalev Lember <klember@redhat.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include "gs-upgrade-banner.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <stdlib.h>

typedef struct
{
	GsApp		*app;

	GtkWidget	*button_upgrades_download;
	GtkWidget	*button_upgrades_install;
	GtkWidget	*button_upgrades_learn_more;
	GtkWidget	*label_upgrades_summary;
	GtkWidget	*label_upgrades_title;
	GtkWidget	*progressbar;
} GsUpgradeBannerPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsUpgradeBanner, gs_upgrade_banner, GTK_TYPE_BIN)

enum {
	SIGNAL_DOWNLOAD_BUTTON_CLICKED,
	SIGNAL_INSTALL_BUTTON_CLICKED,
	SIGNAL_LEARN_MORE_BUTTON_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

static void
gs_upgrade_banner_refresh (GsUpgradeBanner *self)
{
	GsUpgradeBannerPrivate *priv = gs_upgrade_banner_get_instance_private (self);

	if (priv->app == NULL)
		return;

	/* Refresh the title. Normally a distro upgrade state goes from
	 *
	 * AVAILABLE (available to download) to
	 * INSTALLING (downloading packages for later installation) to
	 * UPDATABLE (packages are downloaded and upgrade is ready to go)
	 */
	switch (gs_app_get_state (priv->app)) {
	case AS_APP_STATE_AVAILABLE:
	{
		g_autofree gchar *str = NULL;

		/* TRANSLATORS: This is the text displayed when a distro
		 * upgrade is available. First %s is the distro name and the
		 * 2nd %s is the version, e.g. "Fedora 23 Now Available" */
		str = g_strdup_printf (_("%s %s Now Available"),
		                       gs_app_get_name (priv->app),
		                       gs_app_get_version (priv->app));
		gtk_label_set_text (GTK_LABEL (priv->label_upgrades_title), str);
		break;
	}
	case AS_APP_STATE_INSTALLING:
	{
		g_autofree gchar *str = NULL;

		/* TRANSLATORS: This is the text displayed while downloading a
		 * distro upgrade. First %s is the distro name and the 2nd %s
		 * is the version, e.g. "Downloading Fedora 23" */
		str = g_strdup_printf (_("Downloading %s %s"),
		                       gs_app_get_name (priv->app),
		                       gs_app_get_version (priv->app));
		gtk_label_set_text (GTK_LABEL (priv->label_upgrades_title), str);
		break;
	}
	case AS_APP_STATE_UPDATABLE:
	{
		g_autofree gchar *str = NULL;

		/* TRANSLATORS: This is the text displayed when a distro
		 * upgrade has been downloaded and is ready to be installed.
		 * First %s is the distro name and the 2nd %s is the version,
		 * e.g. "Fedora 23 Ready to be Installed" */
		str = g_strdup_printf (_("%s %s Ready to be Installed"),
		                       gs_app_get_name (priv->app),
		                       gs_app_get_version (priv->app));
		gtk_label_set_text (GTK_LABEL (priv->label_upgrades_title), str);
		break;
	}
	default:
		g_critical ("Unexpected app state");
		break;
	}

	/* Refresh the summary if we got anything better than the default blurb */
	if (gs_app_get_summary (priv->app) != NULL)
		gtk_label_set_text (GTK_LABEL (priv->label_upgrades_summary),
		                    gs_app_get_summary (priv->app));

	/* Show the right buttons for the current state */
	switch (gs_app_get_state (priv->app)) {
	case AS_APP_STATE_AVAILABLE:
		gtk_widget_show (priv->button_upgrades_learn_more);
		gtk_widget_show (priv->button_upgrades_download);
		gtk_widget_hide (priv->button_upgrades_install);
		break;
	case AS_APP_STATE_INSTALLING:
		gtk_widget_show (priv->button_upgrades_learn_more);
		gtk_widget_hide (priv->button_upgrades_download);
		gtk_widget_hide (priv->button_upgrades_install);
		break;
	case AS_APP_STATE_UPDATABLE:
		gtk_widget_show (priv->button_upgrades_learn_more);
		gtk_widget_hide (priv->button_upgrades_download);
		gtk_widget_show (priv->button_upgrades_install);
		break;
	default:
		g_critical ("Unexpected app state");
		break;
	}

	/* do a fill bar for the current progress */
	switch (gs_app_get_state (priv->app)) {
	case AS_APP_STATE_INSTALLING:
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->progressbar),
		                               gs_app_get_progress (priv->app));
		gtk_widget_show (priv->progressbar);
		break;
	default:
		gtk_widget_hide (priv->progressbar);
		break;
	}
}

static gboolean
app_state_changed_idle (gpointer user_data)
{
	GsUpgradeBanner *self = GS_UPGRADE_BANNER (user_data);

	gs_upgrade_banner_refresh (self);

	g_object_unref (self);
	return G_SOURCE_REMOVE;
}

static void
app_state_changed (GsApp *app, GParamSpec *pspec, GsUpgradeBanner *self)
{
	g_idle_add (app_state_changed_idle, g_object_ref (self));
}

static void
download_button_cb (GtkWidget *widget, GsUpgradeBanner *self)
{
	g_signal_emit (self, signals[SIGNAL_DOWNLOAD_BUTTON_CLICKED], 0);
}

static void
install_button_cb (GtkWidget *widget, GsUpgradeBanner *self)
{
	g_signal_emit (self, signals[SIGNAL_INSTALL_BUTTON_CLICKED], 0);
}

static void
learn_more_button_cb (GtkWidget *widget, GsUpgradeBanner *self)
{
	g_signal_emit (self, signals[SIGNAL_LEARN_MORE_BUTTON_CLICKED], 0);
}

static void
gs_upgrade_banner_set_app (GsUpgradeBanner *self, GsApp *app)
{
	GsUpgradeBannerPrivate *priv = gs_upgrade_banner_get_instance_private (self);

	g_return_if_fail (GS_IS_UPGRADE_BANNER (self));
	g_return_if_fail (GS_IS_APP (app) || app == NULL);

	if (priv->app)
		g_signal_handlers_disconnect_by_func (priv->app, app_state_changed, self);

	g_set_object (&priv->app, app);
	if (!app)
		return;

	g_signal_connect (priv->app, "notify::state",
			  G_CALLBACK (app_state_changed), self);

	gs_upgrade_banner_refresh (self);
}

static void
gs_upgrade_banner_destroy (GtkWidget *widget)
{
	GsUpgradeBanner *self = GS_UPGRADE_BANNER (widget);
	GsUpgradeBannerPrivate *priv = gs_upgrade_banner_get_instance_private (self);

	if (priv->app)
		g_signal_handlers_disconnect_by_func (priv->app, app_state_changed, self);

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
	g_signal_connect (priv->button_upgrades_learn_more, "clicked",
	                  G_CALLBACK (learn_more_button_cb),
	                  self);
}

static void
gs_upgrade_banner_class_init (GsUpgradeBannerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->destroy = gs_upgrade_banner_destroy;

	signals [SIGNAL_DOWNLOAD_BUTTON_CLICKED] =
		g_signal_new ("download-button-clicked",
		              G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (GsUpgradeBannerClass, download_button_clicked),
		              NULL, NULL, g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	signals [SIGNAL_INSTALL_BUTTON_CLICKED] =
		g_signal_new ("install-button-clicked",
		              G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (GsUpgradeBannerClass, install_button_clicked),
		              NULL, NULL, g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	signals [SIGNAL_LEARN_MORE_BUTTON_CLICKED] =
		g_signal_new ("learn-more-button-clicked",
		              G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
		              G_STRUCT_OFFSET (GsUpgradeBannerClass, learn_more_button_clicked),
		              NULL, NULL, g_cclosure_marshal_VOID__VOID,
		              G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-upgrade-banner.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, button_upgrades_download);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, button_upgrades_install);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, button_upgrades_learn_more);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, label_upgrades_summary);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, label_upgrades_title);
	gtk_widget_class_bind_template_child_private (widget_class, GsUpgradeBanner, progressbar);
}

GtkWidget *
gs_upgrade_banner_new (GsApp *app)
{
	GsUpgradeBanner *self;

	self = g_object_new (GS_TYPE_UPGRADE_BANNER, NULL);
	gs_upgrade_banner_set_app (self, app);

	return GTK_WIDGET (self);
}

/* vim: set noexpandtab: */
