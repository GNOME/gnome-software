/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include <glib/gi18n.h>

#include "gs-app.h"
#include "gs-shell.h"
#include "gs-shell-loading.h"

typedef struct {
	GsPage			 parent_instance;

	GsPluginLoader		*plugin_loader;
	GCancellable		*cancellable;
	GsShell			*shell;

	GtkWidget		*progressbar;
	GtkWidget		*label;
} GsShellLoadingPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsShellLoading, gs_shell_loading, GS_TYPE_PAGE)

enum {
	SIGNAL_REFRESHED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

/**
 * gs_shell_loading_status_changed_cb:
 **/
static void
gs_shell_loading_status_changed_cb (GsPluginLoader *plugin_loader,
				    GsApp *app,
				    GsPluginStatus status,
				    GsShellLoading *self)
{
	GsShellLoadingPrivate *priv = gs_shell_loading_get_instance_private (self);

	/* update label */
	switch (status) {
	case GS_PLUGIN_STATUS_DOWNLOADING:
		gtk_label_set_label (GTK_LABEL (priv->label),
				     /* TRANSLATORS: initial start */
				     _("Software catalog is being downloaded"));
		break;
	default:
		gtk_label_set_label (GTK_LABEL (priv->label),
				     /* TRANSLATORS: initial start */
				     _("Software catalog is being loaded"));
		break;
	}

	/* update progresbar */
	if (app != NULL) {
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->progressbar),
					       (gdouble) gs_app_get_progress (app) / 100.0f);
	}
}

/**
 * gs_shell_loading_refresh_cb:
 **/
static void
gs_shell_loading_refresh_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source_object);
	GsShellLoading *self = GS_SHELL_LOADING (user_data);
	g_autoptr(GError) error = NULL;

	/* no longer care */
	g_signal_handlers_disconnect_by_data (plugin_loader, self);

	/* not sure how to handle this */
	if (!gs_plugin_loader_refresh_finish (plugin_loader, res, &error)) {
		g_warning ("failed to load metadata: %s", error->message);
		return;
	}

	/* UI is good to go */
	g_signal_emit (self, signals[SIGNAL_REFRESHED], 0);
}

/**
 * gs_shell_loading_load:
 */
static void
gs_shell_loading_load (GsShellLoading *self)
{
	GsShellLoadingPrivate *priv = gs_shell_loading_get_instance_private (self);

	/* ensure that at least some metadata of any age is present, and also
	 * spin up the plugins enough as to prime caches */
	gs_plugin_loader_refresh_async (priv->plugin_loader, -1,
					GS_PLUGIN_REFRESH_FLAGS_METADATA,
					priv->cancellable,
					gs_shell_loading_refresh_cb,
					self);
	g_signal_connect (priv->plugin_loader, "status-changed",
			  G_CALLBACK (gs_shell_loading_status_changed_cb),
			  self);
}

/**
 * gs_shell_loading_switch_to:
 **/
static void
gs_shell_loading_switch_to (GsPage *page, gboolean scroll_up)
{
	GsShellLoading *self = GS_SHELL_LOADING (page);
	GsShellLoadingPrivate *priv = gs_shell_loading_get_instance_private (self);

	if (gs_shell_get_mode (priv->shell) != GS_SHELL_MODE_LOADING) {
		g_warning ("Called switch_to(loading) when in mode %s",
			   gs_shell_get_mode_string (priv->shell));
		return;
	}
	gs_shell_loading_load (self);
}

/**
 * gs_shell_loading_setup:
 */
void
gs_shell_loading_setup (GsShellLoading *self,
			GsShell *shell,
			GsPluginLoader *plugin_loader,
			GtkBuilder *builder,
			GCancellable *cancellable)
{
	GsShellLoadingPrivate *priv = gs_shell_loading_get_instance_private (self);

	g_return_if_fail (GS_IS_SHELL_LOADING (self));

	priv->shell = shell;
	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->cancellable = g_object_ref (cancellable);

	/* chain up */
	gs_page_setup (GS_PAGE (self), shell, plugin_loader, cancellable);
}

/**
 * gs_shell_loading_dispose:
 **/
static void
gs_shell_loading_dispose (GObject *object)
{
	GsShellLoading *self = GS_SHELL_LOADING (object);
	GsShellLoadingPrivate *priv = gs_shell_loading_get_instance_private (self);

	g_clear_object (&priv->plugin_loader);
	g_clear_object (&priv->cancellable);

	G_OBJECT_CLASS (gs_shell_loading_parent_class)->dispose (object);
}

/**
 * gs_shell_loading_class_init:
 **/
static void
gs_shell_loading_class_init (GsShellLoadingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPageClass *page_class = GS_PAGE_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_shell_loading_dispose;
	page_class->switch_to = gs_shell_loading_switch_to;

	signals [SIGNAL_REFRESHED] =
		g_signal_new ("refreshed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsShellLoadingClass, refreshed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-shell-loading.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsShellLoading, progressbar);
	gtk_widget_class_bind_template_child_private (widget_class, GsShellLoading, label);
}

/**
 * gs_shell_loading_init:
 **/
static void
gs_shell_loading_init (GsShellLoading *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}

/**
 * gs_shell_loading_new:
 **/
GsShellLoading *
gs_shell_loading_new (void)
{
	GsShellLoading *self;
	self = g_object_new (GS_TYPE_SHELL_LOADING, NULL);
	return GS_SHELL_LOADING (self);
}

/* vim: set noexpandtab: */
