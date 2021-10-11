/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2014-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <packagekit-glib2/packagekit.h>
#include <gnome-software.h>

#include "gs-metered.h"
#include "gs-packagekit-helper.h"
#include "packagekit-common.h"

#include "gs-plugin-packagekit-refresh.h"

/*
 * SECTION:
 * Do a PackageKit UpdatePackages(ONLY_DOWNLOAD) method on refresh and
 * also convert any package files to applications the best we can.
 */

struct _GsPluginPackagekitRefresh {
	GsPlugin		 parent;

	PkTask			*task;
	GMutex			 task_mutex;
};

G_DEFINE_TYPE (GsPluginPackagekitRefresh, gs_plugin_packagekit_refresh, GS_TYPE_PLUGIN)

static void
gs_plugin_packagekit_refresh_init (GsPluginPackagekitRefresh *self)
{
	GsPlugin *plugin = GS_PLUGIN (self);

	g_mutex_init (&self->task_mutex);
	self->task = pk_task_new ();
	pk_task_set_only_download (self->task, TRUE);
	pk_client_set_background (PK_CLIENT (self->task), TRUE);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));

	/* we can return better results than dpkg directly */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "dpkg");
}

static void
gs_plugin_packagekit_refresh_dispose (GObject *object)
{
	GsPluginPackagekitRefresh *self = GS_PLUGIN_PACKAGEKIT_REFRESH (object);

	g_clear_object (&self->task);

	G_OBJECT_CLASS (gs_plugin_packagekit_refresh_parent_class)->dispose (object);
}

static void
gs_plugin_packagekit_refresh_finalize (GObject *object)
{
	GsPluginPackagekitRefresh *self = GS_PLUGIN_PACKAGEKIT_REFRESH (object);

	g_mutex_clear (&self->task_mutex);

	G_OBJECT_CLASS (gs_plugin_packagekit_refresh_parent_class)->finalize (object);
}

static gboolean
_download_only (GsPluginPackagekitRefresh  *self,
                GsAppList                  *list,
                GCancellable               *cancellable,
                GError                    **error)
{
	GsPlugin *plugin = GS_PLUGIN (self);
	g_auto(GStrv) package_ids = NULL;
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(PkPackageSack) sack = NULL;
	g_autoptr(PkResults) results2 = NULL;
	g_autoptr(PkResults) results = NULL;

	/* get the list of packages to update */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);

	g_mutex_lock (&self->task_mutex);
	/* never refresh the metadata here as this can surprise the frontend if
	 * we end up downloading a different set of packages than what was
	 * shown to the user */
	pk_client_set_cache_age (PK_CLIENT (self->task), G_MAXUINT);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results = pk_client_get_updates (PK_CLIENT (self->task),
					 pk_bitfield_value (PK_FILTER_ENUM_NONE),
					 cancellable,
					 gs_packagekit_helper_cb, helper,
					 error);
	g_mutex_unlock (&self->task_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		return FALSE;
	}

	/* download all the packages */
	sack = pk_results_get_package_sack (results);
	if (pk_package_sack_get_size (sack) == 0)
		return TRUE;
	package_ids = pk_package_sack_get_ids (sack);
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		gs_packagekit_helper_add_app (helper, app);
	}
	g_mutex_lock (&self->task_mutex);
	/* never refresh the metadata here as this can surprise the frontend if
	 * we end up downloading a different set of packages than what was
	 * shown to the user */
	pk_client_set_cache_age (PK_CLIENT (self->task), G_MAXUINT);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	results2 = pk_task_update_packages_sync (self->task,
						 package_ids,
						 cancellable,
						 gs_packagekit_helper_cb, helper,
						 error);
	g_mutex_unlock (&self->task_mutex);
	if (results2 == NULL) {
		gs_plugin_packagekit_error_convert (error);
		return FALSE;
	}
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		/* To indicate the app is already downloaded */
		gs_app_set_size_download (app, 0);
	}
	return TRUE;
}

gboolean
gs_plugin_download (GsPlugin *plugin,
                    GsAppList *list,
                    GCancellable *cancellable,
                    GError **error)
{
	GsPluginPackagekitRefresh *self = GS_PLUGIN_PACKAGEKIT_REFRESH (plugin);
	g_autoptr(GsAppList) list_tmp = gs_app_list_new ();
	g_autoptr(GError) error_local = NULL;
	gboolean retval;
	gpointer schedule_entry_handle = NULL;

	/* add any packages */
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		GsAppList *related = gs_app_get_related (app);

		/* add this app */
		if (!gs_app_has_quirk (app, GS_APP_QUIRK_IS_PROXY))
			if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") == 0) {
				gs_app_list_add (list_tmp, app);
			continue;
		}

		/* add each related app */
		for (guint j = 0; j < gs_app_list_length (related); j++) {
			GsApp *app_tmp = gs_app_list_index (related, j);
			if (g_strcmp0 (gs_app_get_management_plugin (app_tmp), "packagekit") == 0)
				gs_app_list_add (list_tmp, app_tmp);
		}
	}

	if (gs_app_list_length (list_tmp) == 0)
		return TRUE;

	if (!gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE)) {
		if (!gs_metered_block_app_list_on_download_scheduler (list_tmp, &schedule_entry_handle, cancellable, &error_local)) {
			g_warning ("Failed to block on download scheduler: %s",
				   error_local->message);
			g_clear_error (&error_local);
		}
	}

	retval = _download_only (self, list_tmp, cancellable, error);

	if (!gs_metered_remove_from_download_scheduler (schedule_entry_handle, NULL, &error_local))
		g_warning ("Failed to remove schedule entry: %s", error_local->message);

	return retval;
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GCancellable *cancellable,
		   GError **error)
{
	GsPluginPackagekitRefresh *self = GS_PLUGIN_PACKAGEKIT_REFRESH (plugin);
	g_autoptr(GsPackagekitHelper) helper = gs_packagekit_helper_new (plugin);
	g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));
	g_autoptr(PkResults) results = NULL;

	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);
	gs_packagekit_helper_set_progress_app (helper, app_dl);

	g_mutex_lock (&self->task_mutex);
	/* cache age of 1 is user-initiated */
	pk_client_set_background (PK_CLIENT (self->task), cache_age > 1);
	pk_client_set_interactive (PK_CLIENT (self->task), gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE));
	pk_client_set_cache_age (PK_CLIENT (self->task), cache_age);
	/* refresh the metadata */
	results = pk_client_refresh_cache (PK_CLIENT (self->task),
	                                   FALSE /* force */,
	                                   cancellable,
	                                   gs_packagekit_helper_cb, helper,
	                                   error);
	g_mutex_unlock (&self->task_mutex);
	if (!gs_plugin_packagekit_results_valid (results, error)) {
		return FALSE;
	}

	return TRUE;
}

static void
gs_plugin_packagekit_refresh_class_init (GsPluginPackagekitRefreshClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = gs_plugin_packagekit_refresh_dispose;
	object_class->finalize = gs_plugin_packagekit_refresh_finalize;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_PACKAGEKIT_REFRESH;
}
