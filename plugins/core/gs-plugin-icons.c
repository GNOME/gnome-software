/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <config.h>

#include <libsoup/soup.h>
#include <string.h>

#include <gnome-software.h>

#include "gs-plugin-icons.h"

/*
 * SECTION:
 * Loads remote icons and converts them into local cached ones.
 *
 * It is provided so that each plugin handling icons does not
 * have to handle the download and caching functionality.
 *
 * It runs entirely in the main thread and requires no locking. Downloading the
 * remote icons is done in a worker thread owned by #GsIconDownloader.
 *
 * FIXME: This plugin will eventually go away. Currently it only exists as the
 * plugin ordering code is a convenient way of ensuring that loading the remote
 * icons happens after all other plugins have refined icons.
 */

struct _GsPluginIcons
{
	GsPlugin	parent;

	GsIconDownloader	*icon_downloader; /* (owned) */
	SoupSession	*soup_session;  /* (owned) */
};

G_DEFINE_TYPE (GsPluginIcons, gs_plugin_icons, GS_TYPE_PLUGIN)

static void
gs_plugin_icons_init (GsPluginIcons *self)
{
	/* needs remote icons downloaded */
	gs_plugin_add_rule (GS_PLUGIN (self), GS_PLUGIN_RULE_RUN_AFTER, "appstream");
	gs_plugin_add_rule (GS_PLUGIN (self), GS_PLUGIN_RULE_RUN_AFTER, "epiphany");
}

static void
gs_plugin_icons_dispose (GObject *object)
{
	GsPluginIcons *self = GS_PLUGIN_ICONS (object);

	g_clear_object (&self->icon_downloader);
	g_clear_object (&self->soup_session);

	G_OBJECT_CLASS (gs_plugin_icons_parent_class)->dispose (object);
}

static void
gs_plugin_icons_setup_async (GsPlugin            *plugin,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
	GsPluginIcons *self = GS_PLUGIN_ICONS (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_icons_setup_async);

	self->soup_session = gs_build_soup_session ();

	/* Currently a 160px icon is needed for #GsFeatureTile, at most.
	   Scaling is applied inside the downloader. */
	self->icon_downloader = gs_icon_downloader_new (self->soup_session, 160);
	g_object_bind_property (plugin, "scale",
				self->icon_downloader, "scale",
				G_BINDING_SYNC_CREATE);

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_icons_setup_finish (GsPlugin      *plugin,
                              GAsyncResult  *result,
                              GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void shutdown_cb (GObject      *source_object,
                         GAsyncResult *result,
                         gpointer      user_data);

static void
gs_plugin_icons_shutdown_async (GsPlugin            *plugin,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
	GsPluginIcons *self = GS_PLUGIN_ICONS (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_icons_shutdown_async);

	/* Stop the icon downloader. */
	gs_icon_downloader_shutdown_async (self->icon_downloader, cancellable,
					   shutdown_cb, g_steal_pointer (&task));
}

static void
shutdown_cb (GObject      *source_object,
             GAsyncResult *result,
             gpointer      user_data)
{
	g_autoptr(GsIconDownloader) icon_downloader = NULL;
	g_autoptr(GTask) task = G_TASK (user_data);
	GsPluginIcons *self = g_task_get_source_object (task);
	g_autoptr(GError) local_error = NULL;

	g_clear_object (&self->soup_session);
	icon_downloader = g_steal_pointer (&self->icon_downloader);
	if (!gs_icon_downloader_shutdown_finish (icon_downloader, result, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_icons_shutdown_finish (GsPlugin      *plugin,
                                 GAsyncResult  *result,
                                 GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_icons_refine_async (GsPlugin                   *plugin,
                              GsAppList                  *list,
                              GsPluginRefineFlags         job_flags,
                              GsPluginRefineRequireFlags  require_flags,
                              GsPluginEventCallback       event_callback,
                              void                       *event_user_data,
                              GCancellable               *cancellable,
                              GAsyncReadyCallback         callback,
                              gpointer                    user_data)
{
	GsPluginIcons *self = GS_PLUGIN_ICONS (plugin);
	g_autoptr(GTask) task = NULL;
	gboolean interactive = (job_flags & GS_PLUGIN_REFINE_FLAGS_INTERACTIVE) != 0;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_icons_refine_async);

	/* nothing to do here */
	if ((require_flags & GS_PLUGIN_REFINE_REQUIRE_FLAGS_ICON) == 0) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);

		gs_icon_downloader_queue_app (self->icon_downloader, app, interactive);
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_icons_refine_finish (GsPlugin      *plugin,
                               GAsyncResult  *result,
                               GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_icons_class_init (GsPluginIconsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_icons_dispose;

	plugin_class->setup_async = gs_plugin_icons_setup_async;
	plugin_class->setup_finish = gs_plugin_icons_setup_finish;
	plugin_class->shutdown_async = gs_plugin_icons_shutdown_async;
	plugin_class->shutdown_finish = gs_plugin_icons_shutdown_finish;
	plugin_class->refine_async = gs_plugin_icons_refine_async;
	plugin_class->refine_finish = gs_plugin_icons_refine_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_ICONS;
}
