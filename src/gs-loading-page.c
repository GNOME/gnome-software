/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-shell.h"
#include "gs-loading-page.h"

typedef struct {
	GsPage			 parent_instance;

	GsPluginLoader		*plugin_loader;
	GCancellable		*cancellable;
	GsShell			*shell;

	GtkWidget		*progressbar;
	GtkWidget		*label;
	guint			 progress_pulse_id;
} GsLoadingPagePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsLoadingPage, gs_loading_page, GS_TYPE_PAGE)

enum {
	SIGNAL_REFRESHED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

static gboolean
_pulse_cb (gpointer user_data)
{
	GsLoadingPage *self = GS_LOADING_PAGE (user_data);
	GsLoadingPagePrivate *priv = gs_loading_page_get_instance_private (self);
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (priv->progressbar));
	return TRUE;
}

static void
gs_loading_page_status_changed_cb (GsPluginLoader *plugin_loader,
                                   GsApp *app,
                                   GsPluginStatus status,
                                   GsLoadingPage *self)
{
	GsLoadingPagePrivate *priv = gs_loading_page_get_instance_private (self);
	const gchar *str = NULL;

	/* update label */
	if (status == GS_PLUGIN_STATUS_DOWNLOADING) {
		if (app != NULL)
			str = gs_app_get_summary_missing (app);
		if (str == NULL) {
			/* TRANSLATORS: initial start */
			str = _("Software catalog is being downloaded");
		}
	} else {
		/* TRANSLATORS: initial start */
		str = _("Software catalog is being loaded");
	}

	/* update label */
	gtk_label_set_label (GTK_LABEL (priv->label), str);

	/* update progresbar */
	if (app != NULL) {
		if (priv->progress_pulse_id != 0) {
			g_source_remove (priv->progress_pulse_id);
			priv->progress_pulse_id = 0;
		}
		if (gs_app_get_progress (app) == 0) {
			priv->progress_pulse_id = g_timeout_add (50, _pulse_cb, self);
		} else {
			gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->progressbar),
						       (gdouble) gs_app_get_progress (app) / 100.0f);
		}
	}
}

static void
gs_loading_page_refresh_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GsLoadingPage *self = GS_LOADING_PAGE (user_data);
	GsLoadingPagePrivate *priv = gs_loading_page_get_instance_private (self);
	g_autoptr(GError) error = NULL;

	/* no longer care */
	g_signal_handlers_disconnect_by_data (plugin_loader, self);

	/* not sure how to handle this */
	if (!gs_plugin_loader_job_action_finish (plugin_loader, res, &error)) {
		g_warning ("failed to load metadata: %s", error->message);
	}

	/* no more pulsing */
	if (priv->progress_pulse_id != 0) {
		g_source_remove (priv->progress_pulse_id);
		priv->progress_pulse_id = 0;
	}

	/* UI is good to go */
	g_signal_emit (self, signals[SIGNAL_REFRESHED], 0);
}

static void
gs_loading_page_load (GsLoadingPage *self)
{
	GsLoadingPagePrivate *priv = gs_loading_page_get_instance_private (self);
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* ensure that at least some metadata of any age is present, and also
	 * spin up the plugins enough as to prime caches */
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REFRESH,
					 "age", (guint64) G_MAXUINT,
					 NULL);
	gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
					priv->cancellable,
					gs_loading_page_refresh_cb,
					self);
	g_signal_connect (priv->plugin_loader, "status-changed",
			  G_CALLBACK (gs_loading_page_status_changed_cb),
			  self);
}

static void
gs_loading_page_switch_to (GsPage *page, gboolean scroll_up)
{
	GsLoadingPage *self = GS_LOADING_PAGE (page);
	GsLoadingPagePrivate *priv = gs_loading_page_get_instance_private (self);

	if (gs_shell_get_mode (priv->shell) != GS_SHELL_MODE_LOADING) {
		g_warning ("Called switch_to(loading) when in mode %s",
			   gs_shell_get_mode_string (priv->shell));
		return;
	}
	gs_loading_page_load (self);
}

static gboolean
gs_loading_page_setup (GsPage *page,
                       GsShell *shell,
                       GsPluginLoader *plugin_loader,
                       GtkBuilder *builder,
                       GCancellable *cancellable,
                       GError **error)
{
	GsLoadingPage *self = GS_LOADING_PAGE (page);
	GsLoadingPagePrivate *priv = gs_loading_page_get_instance_private (self);

	g_return_val_if_fail (GS_IS_LOADING_PAGE (self), TRUE);

	priv->shell = shell;
	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->cancellable = g_object_ref (cancellable);
	return TRUE;
}

static void
gs_loading_page_dispose (GObject *object)
{
	GsLoadingPage *self = GS_LOADING_PAGE (object);
	GsLoadingPagePrivate *priv = gs_loading_page_get_instance_private (self);

	if (priv->progress_pulse_id != 0) {
		g_source_remove (priv->progress_pulse_id);
		priv->progress_pulse_id = 0;
	}

	g_clear_object (&priv->plugin_loader);
	g_clear_object (&priv->cancellable);

	G_OBJECT_CLASS (gs_loading_page_parent_class)->dispose (object);
}

static void
gs_loading_page_class_init (GsLoadingPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_loading_page_dispose;
	page_class->switch_to = gs_loading_page_switch_to;
	page_class->setup = gs_loading_page_setup;

	signals [SIGNAL_REFRESHED] =
		g_signal_new ("refreshed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsLoadingPageClass, refreshed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-loading-page.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsLoadingPage, progressbar);
	gtk_widget_class_bind_template_child_private (widget_class, GsLoadingPage, label);
}

static void
gs_loading_page_init (GsLoadingPage *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}

GsLoadingPage *
gs_loading_page_new (void)
{
	GsLoadingPage *self;
	self = g_object_new (GS_TYPE_LOADING_PAGE, NULL);
	return GS_LOADING_PAGE (self);
}
