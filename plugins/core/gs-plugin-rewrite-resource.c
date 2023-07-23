/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <config.h>

#include <glib/gi18n.h>
#include <gnome-software.h>

#include "gs-plugin-rewrite-resource.h"

/*
 * SECTION:
 * Rewrites CSS metadata for apps to refer to locally downloaded resources.
 *
 * This plugin rewrites the CSS of apps to refer to locally cached resources,
 * rather than HTTP/HTTPS URIs for images (for example).
 *
 * FIXME: Eventually this should move into the refine plugin job, as it needs
 * to execute after all other refine jobs (in order to see all the URIs which
 * they produce).
 */

struct _GsPluginRewriteResource
{
	GsPlugin	parent;
};

G_DEFINE_TYPE (GsPluginRewriteResource, gs_plugin_rewrite_resource, GS_TYPE_PLUGIN)

static void
gs_plugin_rewrite_resource_init (GsPluginRewriteResource *self)
{
	/* let appstream add metadata first */
	gs_plugin_add_rule (GS_PLUGIN (self), GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

static void
gs_plugin_rewrite_resource_refine_async (GsPlugin            *plugin,
                                         GsAppList           *list,
                                         GsPluginRefineFlags  flags,
                                         GCancellable        *cancellable,
                                         GAsyncReadyCallback  callback,
                                         gpointer             user_data)
{
	GsPluginRewriteResource *self = GS_PLUGIN_REWRITE_RESOURCE (plugin);
	g_autoptr(GTask) task = NULL;

	task = gs_plugin_refine_data_new_task (plugin, list, flags, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_rewrite_resource_refine_async);

	gs_rewrite_resources_async (list, cancellable, rewrite_resources_cb, g_steal_pointer (&task));
}

static void
rewrite_resources_cb (GObject      *source_object,
                      GAsyncResult *result,
                      gpointer      user_data)
{
	g_autoptr(GError) local_error = NULL;

	if (!gs_rewrite_resources_finish (result, &local_error))
		g_task_return_error (task, g_steal_pointer (&local_error));
	else
		g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_rewrite_resource_refine_finish (GsPlugin      *plugin,
                                          GAsyncResult  *result,
                                          GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_rewrite_resource_class_init (GsPluginRewriteResourceClass *klass)
{
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	plugin_class->refine_async = gs_plugin_rewrite_resource_refine_async;
	plugin_class->refine_finish = gs_plugin_rewrite_resource_refine_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_REWRITE_RESOURCE;
}
