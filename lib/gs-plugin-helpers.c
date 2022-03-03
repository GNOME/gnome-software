/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-plugin-helpers
 * @short_description: Helpers for storing call closures for #GsPlugin vfuncs
 *
 * The helpers in this file each create a context structure to store the
 * arguments passed to a standard #GsPlugin vfunc.
 *
 * These are intended to be used by plugin implementations to easily create
 * #GTasks for handling #GsPlugin vfunc calls, without all having to write the
 * same code to create a structure to wrap the vfunc arguments.
 *
 * Since: 42
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>

#include "gs-plugin-helpers.h"

/**
 * gs_plugin_refine_data_new:
 * @list: list of #GsApps to refine
 * @flags: refine flags
 *
 * Context data for a call to #GsPluginClass.refine_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 42
 */
GsPluginRefineData *
gs_plugin_refine_data_new (GsAppList           *list,
                           GsPluginRefineFlags  flags)
{
	g_autoptr(GsPluginRefineData) data = g_new0 (GsPluginRefineData, 1);
	data->list = g_object_ref (list);
	data->flags = flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_refine_data_new_task:
 * @source_object: task source object
 * @list: list of #GsApps to refine
 * @flags: refine flags
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: function to call once asynchronous operation is finished
 * @user_data: data to pass to @callback
 *
 * Create a #GTask for a refine operation with the given arguments. The task
 * data will be set to a #GsPluginRefineData containing the given context.
 *
 * This is essentially a combination of gs_plugin_refine_data_new(),
 * g_task_new() and g_task_set_task_data().
 *
 * Returns: (transfer full): new #GTask with the given context data
 * Since: 42
 */
GTask *
gs_plugin_refine_data_new_task (gpointer             source_object,
                                GsAppList           *list,
                                GsPluginRefineFlags  flags,
                                GCancellable        *cancellable,
                                GAsyncReadyCallback  callback,
                                gpointer             user_data)
{
	g_autoptr(GTask) task = g_task_new (source_object, cancellable, callback, user_data);
	g_task_set_task_data (task, gs_plugin_refine_data_new (list, flags), (GDestroyNotify) gs_plugin_refine_data_free);
	return g_steal_pointer (&task);
}

/**
 * gs_plugin_refine_data_free:
 * @data: (transfer full): a #GsPluginRefineData
 *
 * Free the given @data.
 *
 * Since: 42
 */
void
gs_plugin_refine_data_free (GsPluginRefineData *data)
{
	g_clear_object (&data->list);
	g_free (data);
}

/**
 * gs_plugin_refresh_metadata_data_new:
 * @cache_age_secs: maximum allowed age of the cache in order for it to remain valid, in seconds
 * @flags: refresh metadata flags
 *
 * Context data for a call to #GsPluginClass.refresh_metadata_async.
 *
 * Returns: (transfer full): context data structure
 * Since: 42
 */
GsPluginRefreshMetadataData *
gs_plugin_refresh_metadata_data_new (guint64                      cache_age_secs,
                                     GsPluginRefreshMetadataFlags flags)
{
	g_autoptr(GsPluginRefreshMetadataData) data = g_new0 (GsPluginRefreshMetadataData, 1);
	data->cache_age_secs = cache_age_secs;
	data->flags = flags;

	return g_steal_pointer (&data);
}

/**
 * gs_plugin_refresh_metadata_data_free:
 * @data: (transfer full): a #GsPluginRefreshMetadataData
 *
 * Free the given @data.
 *
 * Since: 42
 */
void
gs_plugin_refresh_metadata_data_free (GsPluginRefreshMetadataData *data)
{
	g_free (data);
}
