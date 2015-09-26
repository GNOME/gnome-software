/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2014 Richard Hughes <richard@hughsie.com>
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
#include <appstream-glib.h>

#include "gs-plugin-loader.h"
#include "gs-plugin.h"

#define GS_PLUGIN_LOADER_UPDATES_CHANGED_DELAY	3	/* s */

typedef struct
{
	GPtrArray		*plugins;
	gchar			*location;
	GsPluginStatus		 status_last;
	AsProfile		*profile;

	GMutex			 pending_apps_mutex;
	GPtrArray		*pending_apps;

	GMutex			 app_cache_mutex;
	GHashTable		*app_cache;
	GSettings		*settings;

	gchar			**compatible_projects;
	gint			 scale;

	guint			 updates_changed_id;
	gboolean		 online; 
} GsPluginLoaderPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsPluginLoader, gs_plugin_loader, G_TYPE_OBJECT)

enum {
	SIGNAL_STATUS_CHANGED,
	SIGNAL_PENDING_APPS_CHANGED,
	SIGNAL_UPDATES_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

/* async state */
typedef struct {
	const gchar			*function_name;
	GList				*list;
	GsPluginRefineFlags		 flags;
	gchar				*value;
	gchar				*filename;
	guint				 cache_age;
	GsCategory			*category;
	GsApp				*app;
	AsAppState			 state_success;
	AsAppState			 state_failure;
} GsPluginLoaderAsyncState;

static void
gs_plugin_loader_free_async_state (GsPluginLoaderAsyncState *state)
{
	if (state->category != NULL)
		g_object_unref (state->category);
	if (state->app != NULL)
		g_object_unref (state->app);

	g_free (state->filename);
	g_free (state->value);
	gs_plugin_list_free (state->list);
	g_slice_free (GsPluginLoaderAsyncState, state);
}

/**
 * gs_plugin_loader_error_quark:
 * Return value: Our personal error quark.
 **/
GQuark
gs_plugin_loader_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("gs_plugin_loader_error");
	return quark;
}

/**
 * gs_plugin_loader_app_sort_cb:
 **/
static gint
gs_plugin_loader_app_sort_cb (gconstpointer a, gconstpointer b)
{
	return g_strcmp0 (gs_app_get_name (GS_APP (a)),
			  gs_app_get_name (GS_APP (b)));
}

/**
 * gs_plugin_loader_dedupe:
 */
GsApp *
gs_plugin_loader_dedupe (GsPluginLoader *plugin_loader, GsApp *app)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsApp *new_app;

	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (GS_IS_APP (app), NULL);

	g_mutex_lock (&priv->app_cache_mutex);

	/* not yet set */
	if (gs_app_get_id (app) == NULL) {
		new_app = app;
		goto out;
	}

	/* already exists */
	new_app = g_hash_table_lookup (priv->app_cache, gs_app_get_id (app));
	if (new_app == app) {
		new_app = app;
		goto out;
	}

	/* insert new entry */
	if (new_app == NULL) {
		new_app = app;
		g_hash_table_insert (priv->app_cache,
				     g_strdup (gs_app_get_id (app)),
				     g_object_ref (app));
		goto out;
	}

	/* import all the useful properties */
	gs_app_subsume (new_app, app);

	/* this looks a little odd to unref the method parameter,
	 * but it allows us to do:
	 * app = gs_plugin_loader_dedupe (cache, app);
	 */
	g_object_unref (app);
	g_object_ref (new_app);
out:
	g_mutex_unlock (&priv->app_cache_mutex);
	return new_app;
}

/**
 * gs_plugin_loader_list_dedupe:
 **/
static void
gs_plugin_loader_list_dedupe (GsPluginLoader *plugin_loader, GList *list)
{
	GList *l;
	for (l = list; l != NULL; l = l->next)
		l->data = gs_plugin_loader_dedupe (plugin_loader, GS_APP (l->data));
}

/**
 * gs_plugin_loader_run_refine_plugin:
 **/
static gboolean
gs_plugin_loader_run_refine_plugin (GsPluginLoader *plugin_loader,
				    GsPlugin *plugin,
				    const gchar *function_name_parent,
				    GList **list,
				    GsPluginRefineFlags flags,
				    GCancellable *cancellable,
				    GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPluginRefineFunc plugin_func = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;
	const gchar *function_name = "gs_plugin_refine";
	gboolean exists;
	gboolean ret = TRUE;

	/* load the symbol */
	exists = g_module_symbol (plugin->module,
				  function_name,
				  (gpointer *) &plugin_func);
	if (!exists)
		goto out;

	/* profile the plugin runtime */
	if (function_name_parent == NULL) {
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  plugin->name,
					  function_name);
	} else {
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s;%s)",
					  plugin->name,
					  function_name_parent,
					  function_name);
	}
	ret = plugin_func (plugin, list, flags, cancellable, error);
	if (!ret) {
		/* check the plugin is well behaved and sets error
		 * if returning FALSE */
		if (error != NULL && *error == NULL) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "%s[%s] returned FALSE and set no error",
				     plugin->name, function_name);
		}
		goto out;
	}

	/* check the plugin is well behaved and returns FALSE
	 * if returning an error */
	if (error != NULL && *error != NULL) {
		ret = FALSE;
		g_warning ("%s set %s but did not return FALSE!",
			   plugin->name, (*error)->message);
		goto out;
	}
out:
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	return ret;
}

/**
 * gs_plugin_loader_run_refine:
 **/
static gboolean
gs_plugin_loader_run_refine (GsPluginLoader *plugin_loader,
			     const gchar *function_name_parent,
			     GList **list,
			     GsPluginRefineFlags flags,
			     GCancellable *cancellable,
			     GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GList *l;
	GPtrArray *addons;
	GPtrArray *related;
	GsApp *app;
	GsPlugin *plugin;
	gboolean ret = TRUE;
	guint i;
	g_autoptr(GsAppList) addons_list = NULL;
	g_autoptr(GsAppList) freeze_list = NULL;
	g_autoptr(GsAppList) related_list = NULL;

	/* freeze all apps */
	freeze_list = gs_plugin_list_copy (*list);
	for (l = freeze_list; l != NULL; l = l->next)
		g_object_freeze_notify (G_OBJECT (l->data));

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = gs_plugin_loader_run_refine_plugin (plugin_loader,
							  plugin,
							  function_name_parent,
							  list,
							  flags,
							  cancellable,
							  error);
		if (!ret)
			goto out;
	}

	/* refine addons one layer deep */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS) > 0) {
		flags &= ~GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS;
		for (l = *list; l != NULL; l = l->next) {
			app = GS_APP (l->data);
			addons = gs_app_get_addons (app);
			for (i = 0; i < addons->len; i++) {
				GsApp *addon = g_ptr_array_index (addons, i);
				g_debug ("refining app %s addon %s",
					 gs_app_get_id (app),
					 gs_app_get_id (addon));
				gs_plugin_add_app (&addons_list, addon);
			}
		}
		if (addons_list != NULL) {
			ret = gs_plugin_loader_run_refine (plugin_loader,
							   function_name_parent,
							   &addons_list,
							   flags,
							   cancellable,
							   error);
			if (!ret)
				goto out;
		}
	}

	/* also do related packages one layer deep */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED) > 0) {
		flags &= ~GS_PLUGIN_REFINE_FLAGS_REQUIRE_RELATED;
		for (l = *list; l != NULL; l = l->next) {
			app = GS_APP (l->data);
			related = gs_app_get_related (app);
			for (i = 0; i < related->len; i++) {
				app = g_ptr_array_index (related, i);
				g_debug ("refining related: %s[%s]",
					 gs_app_get_id (app),
					 gs_app_get_source_default (app));
				gs_plugin_add_app (&related_list, app);
			}
		}
		if (related_list != NULL) {
			ret = gs_plugin_loader_run_refine (plugin_loader,
							   function_name_parent,
							   &related_list,
							   flags,
							   cancellable,
							   error);
			if (!ret)
				goto out;
		}
	}

	/* dedupe applications we already know about */
	gs_plugin_loader_list_dedupe (plugin_loader, *list);
out:
	/* now emit all the changed signals */
	for (l = freeze_list; l != NULL; l = l->next)
		g_object_thaw_notify (G_OBJECT (l->data));

	return ret;
}

/**
 * gs_plugin_loader_run_results_plugin:
 **/
static gboolean
gs_plugin_loader_run_results_plugin (GsPluginLoader *plugin_loader,
				     GsPlugin *plugin,
				     const gchar *function_name,
				     GList **list,
				     GCancellable *cancellable,
				     GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPluginResultsFunc plugin_func = NULL;
	gboolean exists;
	gboolean ret = TRUE;
	g_autoptr(AsProfileTask) ptask = NULL;

	/* get symbol */
	exists = g_module_symbol (plugin->module,
				  function_name,
				  (gpointer *) &plugin_func);
	if (!exists)
		goto out;

	/* run function */
	ptask = as_profile_start (priv->profile,
				  "GsPlugin::%s(%s)",
				  plugin->name, function_name);
	g_assert (error == NULL || *error == NULL);
	ret = plugin_func (plugin, list, cancellable, error);
	if (!ret)
		goto out;
out:
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	return ret;
}

/**
 * gs_plugin_loader_run_results:
 **/
static GList *
gs_plugin_loader_run_results (GsPluginLoader *plugin_loader,
			      const gchar *function_name,
			      GsPluginRefineFlags flags,
			      GCancellable *cancellable,
			      GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	gboolean ret = TRUE;
	GList *list = NULL;
	GsPlugin *plugin;
	guint i;
	g_autoptr(AsProfileTask) ptask = NULL;

	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (function_name != NULL, NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);

	/* profile */
	ptask = as_profile_start (priv->profile, "GsPlugin::*(%s)", function_name);

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_cancellable_set_error_if_cancelled (cancellable, error);
		if (ret) {
			ret = FALSE;
			goto out;
		}
		ret = gs_plugin_loader_run_results_plugin (plugin_loader,
							   plugin,
							   function_name,
							   &list,
							   cancellable,
							   error);
		if (!ret)
			goto out;
	}

	/* dedupe applications we already know about */
	gs_plugin_loader_list_dedupe (plugin_loader, list);

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   &list,
					   flags,
					   cancellable,
					   error);
	if (!ret)
		goto out;

	/* filter package list */
	gs_plugin_list_filter_duplicates (&list);

	/* no results */
	if (list == NULL) {
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
			     "no results to show");
		goto out;
	}
out:
	if (!ret) {
		gs_plugin_list_free (list);
		list = NULL;
	}
	return list;
}

/**
 * gs_plugin_loader_get_app_str:
 **/
static const gchar *
gs_plugin_loader_get_app_str (GsApp *app)
{
	const gchar *id;

	/* first try the actual id */
	id = gs_app_get_id (app);
	if (id != NULL)
		return id;

	/* then try the source */
	id = gs_app_get_source_default (app);
	if (id != NULL)
		return id;

	/* lastly try the source id */
	id = gs_app_get_source_id_default (app);
	if (id != NULL)
		return id;

	/* urmmm */
	return "<invalid>";
}

/**
 * gs_plugin_loader_app_is_valid:
 **/
static gboolean
gs_plugin_loader_app_is_valid (GsApp *app, gpointer user_data)
{
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) user_data;

	/* don't show unknown state */
	if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN) {
		g_debug ("app invalid as state unknown %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unconverted unavailables */
	if (gs_app_get_kind (app) == GS_APP_KIND_UNKNOWN &&
		gs_app_get_state (app) == AS_APP_STATE_UNAVAILABLE) {
		g_debug ("app invalid as unconverted unavailable %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show sources */
	if (gs_app_get_kind (app) == GS_APP_KIND_SOURCE) {
		g_debug ("app invalid as source %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unknown kind */
	if (gs_app_get_kind (app) == GS_APP_KIND_UNKNOWN) {
		g_debug ("app invalid as kind unknown %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unconverted packages in the application view */
	if (((state->flags & GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES) == 0) &&
	    (gs_app_get_kind (app) == GS_APP_KIND_PACKAGE ||
	     gs_app_get_kind (app) == GS_APP_KIND_CORE)) {
//		g_debug ("app invalid as only a %s: %s",
//			 gs_app_kind_to_string (gs_app_get_kind (app)),
//			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show apps that do not have the required details */
	if (gs_app_get_name (app) == NULL) {
		g_debug ("app invalid as no name %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}
	if (gs_app_get_summary (app) == NULL) {
		g_debug ("app invalid as no summary %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}
	if (gs_app_get_kind (app) == GS_APP_KIND_NORMAL &&
	    gs_app_get_pixbuf (app) == NULL) {
		g_debug ("app invalid as no pixbuf %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}
	return TRUE;
}

/**
 * gs_plugin_loader_filter_qt_for_gtk:
 **/
static gboolean
gs_plugin_loader_filter_qt_for_gtk (GsApp *app, gpointer user_data)
{
	/* hide the QT versions in preference to the GTK ones */
	if (g_strcmp0 (gs_app_get_id (app), "transmission-qt.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "nntpgrab_qt.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "gimagereader-qt4.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "gimagereader-qt5.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "nntpgrab_server_qt.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "hotot-qt.desktop") == 0) {
		g_debug ("removing QT version of %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* hide the KDE version in preference to the GTK one */
	if (g_strcmp0 (gs_app_get_id (app), "qalculate_kde.desktop") == 0) {
		g_debug ("removing KDE version of %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* hide the KDE version in preference to the Qt one */
	if (g_strcmp0 (gs_app_get_id (app), "kid3.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "kchmviewer.desktop") == 0) {
		g_debug ("removing KDE version of %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}
	return TRUE;
}

/**
 * gs_plugin_loader_app_is_non_system:
 **/
static gboolean
gs_plugin_loader_app_is_non_system (GsApp *app, gpointer user_data)
{
	return gs_app_get_kind (app) != GS_APP_KIND_SYSTEM;
}

/**
 * gs_plugin_loader_app_is_non_installed:
 **/
static gboolean
gs_plugin_loader_app_is_non_installed (GsApp *app, gpointer user_data)
{
	if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED)
		return FALSE;
	if (gs_app_get_state (app) == AS_APP_STATE_UPDATABLE)
		return FALSE;
	return TRUE;
}

/**
 * gs_plugin_loader_get_app_is_compatible:
 */
static gboolean
gs_plugin_loader_get_app_is_compatible (GsApp *app, gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *tmp;
	guint i;

	/* search for any compatible projects */
	tmp = gs_app_get_project_group (app);
	if (tmp == NULL)
		return TRUE;
	for (i = 0; priv->compatible_projects[i] != NULL; i++) {
		if (g_strcmp0 (tmp,  priv->compatible_projects[i]) == 0)
			return TRUE;
	}
	g_debug ("removing incompatible %s from project group %s",
		 gs_app_get_id (app), gs_app_get_project_group (app));
	return FALSE;
}

/**
 * gs_plugin_loader_run_action_plugin:
 **/
static gboolean
gs_plugin_loader_run_action_plugin (GsPluginLoader *plugin_loader,
				    GsPlugin *plugin,
				    GsApp *app,
				    const gchar *function_name,
				    GCancellable *cancellable,
				    GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GError *error_local = NULL;
	GsPluginActionFunc plugin_func = NULL;
	gboolean exists;
	gboolean ret = TRUE;
	g_autoptr(AsProfileTask) ptask = NULL;

	exists = g_module_symbol (plugin->module,
				  function_name,
				  (gpointer *) &plugin_func);
	if (!exists)
		goto out;
	ptask = as_profile_start (priv->profile,
				  "GsPlugin::%s(%s)",
				  plugin->name,
				  function_name);
	ret = plugin_func (plugin, app, cancellable, &error_local);
	if (!ret) {
		if (g_error_matches (error_local,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED)) {
			ret = TRUE;
			g_debug ("not supported for plugin %s: %s",
				 plugin->name,
				 error_local->message);
			g_clear_error (&error_local);
		} else {
			g_propagate_error (error, error_local);
			goto out;
		}
	}
out:
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	return ret;
}

/**
 * gs_plugin_loader_run_action:
 **/
static gboolean
gs_plugin_loader_run_action (GsPluginLoader *plugin_loader,
			     GsApp *app,
			     const gchar *function_name,
			     GCancellable *cancellable,
			     GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	gboolean ret;
	gboolean anything_ran = FALSE;
	GsPlugin *plugin;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		if (g_cancellable_set_error_if_cancelled (cancellable, error))
			return FALSE;
		ret = gs_plugin_loader_run_action_plugin (plugin_loader,
							  plugin,
							  app,
							  function_name,
							  cancellable,
							  error);
		if (!ret)
			return FALSE;
		anything_ran = TRUE;
	}

	/* nothing ran */
	if (!anything_ran) {
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no plugin could handle %s",
			     function_name);
		return FALSE;
	}
	return TRUE;
}

/******************************************************************************/

/**
 * gs_plugin_loader_merge_into_os_update:
 **/
static gboolean
gs_plugin_loader_merge_into_os_update (GsApp *app)
{
	if (gs_app_get_kind (app) == GS_APP_KIND_PACKAGE)
		return TRUE;
	if (gs_app_get_kind (app) == GS_APP_KIND_CORE)
		return TRUE;
	if (gs_app_get_kind (app) == GS_APP_KIND_SOURCE)
		return TRUE;
	return FALSE;
}

/**
 * gs_plugin_loader_add_os_update_item:
 **/
static GList *
gs_plugin_loader_add_os_update_item (GList *list)
{
	gboolean has_os_update = FALSE;
	GList *l;
	GsApp *app_os;
	GsApp *app_tmp;
	g_autoptr(GError) error = NULL;
	g_autoptr(GdkPixbuf) pixbuf = NULL;

	/* do we have any packages left that are not apps? */
	for (l = list; l != NULL; l = l->next) {
		app_tmp = GS_APP (l->data);
		if (gs_plugin_loader_merge_into_os_update (app_tmp)) {
			has_os_update = TRUE;
			break;
		}
	}
	if (!has_os_update)
		return list;

	/* create new meta object */
	app_os = gs_app_new ("os-update.virtual");
	gs_app_set_kind (app_os, GS_APP_KIND_OS_UPDATE);
	gs_app_set_state (app_os, AS_APP_STATE_UPDATABLE);
	gs_app_set_name (app_os,
			 GS_APP_QUALITY_NORMAL,
			 /* TRANSLATORS: this is a group of updates that are not
			  * packages and are not shown in the main list */
			 _("OS Updates"));
	gs_app_set_summary (app_os,
			    GS_APP_QUALITY_NORMAL,
			    /* TRANSLATORS: this is a longer description of the
			     * "OS Updates" string */
			    _("Includes performance, stability and security improvements."));
	gs_app_set_description (app_os,
				GS_APP_QUALITY_NORMAL,
				gs_app_get_summary (app_os));
	for (l = list; l != NULL; l = l->next) {
		app_tmp = GS_APP (l->data);
		if (!gs_plugin_loader_merge_into_os_update (app_tmp))
			continue;
		gs_app_add_related (app_os, app_tmp);
	}

	return g_list_prepend (list, app_os);
}

/**
 * gs_plugin_loader_get_updates_thread_cb:
 **/
static void
gs_plugin_loader_get_updates_thread_cb (GTask *task,
					gpointer object,
					gpointer task_data,
					GCancellable *cancellable)
{
	const gchar *method_name = "gs_plugin_add_updates";
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GError *error = NULL;

	/* do things that would block */
	if ((state->flags & GS_PLUGIN_REFINE_FLAGS_USE_HISTORY) > 0)
		method_name = "gs_plugin_add_updates_historical";

	state->list = gs_plugin_loader_run_results (plugin_loader,
						    method_name,
						    state->flags,
						    cancellable,
						    &error);
	if (error != NULL) {
		g_task_return_error (task, error);
		return;
	}

	/* filter package list */
	gs_plugin_list_filter_duplicates (&state->list);

	/* dedupe applications we already know about */
	gs_plugin_loader_list_dedupe (plugin_loader, state->list);

	/* coalesce all packages down into one os-update */
	state->list = gs_plugin_loader_add_os_update_item (state->list);

	/* remove any packages that are not proper applications or
	 * OS updates */
	gs_plugin_list_filter (&state->list, gs_plugin_loader_app_is_valid, state);
	if (state->list == NULL) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "no updates to show after invalid");
		return;
	}

	/* success */
	g_task_return_pointer (task, gs_plugin_list_copy (state->list), (GDestroyNotify) gs_plugin_list_free);
}

/**
 * gs_plugin_loader_get_updates_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_updates()
 * function. The plugins can either return #GsApp objects of kind
 * %GS_APP_KIND_NORMAL for bonafide applications, or #GsApp's of kind
 * %GS_APP_KIND_PACKAGE for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %GS_APP_KIND_PACKAGE will have been promoted to a kind of %GS_APP_KIND_NORMAL,
 * or if they are core applications, promoted again to a kind of %GS_APP_KIND_SYSTEM.
 *
 * Any #GsApp's of kind %GS_APP_KIND_PACKAGE that remain after refining are
 * added to a new virtual #GsApp of kind %GS_APP_KIND_OS_UPDATE and all the
 * %GS_APP_KIND_PACKAGE objects are moved to related packages of this object.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %GS_APP_KIND_NORMAL, %GS_APP_KIND_SYSTEM or %GS_APP_KIND_OS_UPDATE.
 *
 * The #GsApps may be in state %AS_APP_STATE_INSTALLED or %AS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_get_updates_async (GsPluginLoader *plugin_loader,
				    GsPluginRefineFlags flags,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_get_updates_thread_cb);
}

/**
 * gs_plugin_loader_get_updates_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_get_updates_finish (GsPluginLoader *plugin_loader,
				       GAsyncResult *res,
				       GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

/**
 * gs_plugin_loader_get_sources_thread_cb:
 **/
static void
gs_plugin_loader_get_sources_thread_cb (GTask *task,
					gpointer object,
					gpointer task_data,
					GCancellable *cancellable)
{
	GsPluginLoaderAsyncState *state = task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GError *error = NULL;

	state->list = gs_plugin_loader_run_results (plugin_loader,
						    "gs_plugin_add_sources",
						    state->flags,
						    cancellable,
						    &error);
	if (error != NULL) {
		g_task_return_error (task, error);
		return;
	}

	/* filter package list */
	gs_plugin_list_filter_duplicates (&state->list);

	/* dedupe applications we already know about */
	gs_plugin_loader_list_dedupe (plugin_loader, state->list);

	/* none left? */
	if (state->list == NULL) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "no sources to show");
		return;
	}

	/* success */
	g_task_return_pointer (task, gs_plugin_list_copy (state->list), (GDestroyNotify) gs_plugin_list_free);
}

/**
 * gs_plugin_loader_get_sources_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_sources()
 * function. The plugins return #GsApp objects of kind %GS_APP_KIND_SOURCE..
 *
 * The *applications* installed from each source can be obtained using
 * gs_app_get_related() if this information is available.
 **/
void
gs_plugin_loader_get_sources_async (GsPluginLoader *plugin_loader,
				    GsPluginRefineFlags flags,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_get_sources_thread_cb);
}

/**
 * gs_plugin_loader_get_sources_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_get_sources_finish (GsPluginLoader *plugin_loader,
				       GAsyncResult *res,
				       GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

/**
 * gs_plugin_loader_get_installed_thread_cb:
 **/
static void
gs_plugin_loader_get_installed_thread_cb (GTask *task,
					  gpointer object,
					  gpointer task_data,
					  GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GError *error = NULL;

	/* do things that would block */
	state->list = gs_plugin_loader_run_results (plugin_loader,
						    "gs_plugin_add_installed",
						    state->flags,
						    cancellable,
						    &error);
	if (error != NULL) {
		g_task_return_error (task, error);
		return;
	}

	/* filter package list */
	gs_plugin_list_filter (&state->list, gs_plugin_loader_app_is_valid, state);
	if (state->list == NULL) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "no installed applications to show after invalid");
		return;
	}

	/* success */
	g_task_return_pointer (task, gs_plugin_list_copy (state->list), (GDestroyNotify) gs_plugin_list_free);
}

/**
 * gs_plugin_loader_get_installed_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_installed()
 * function. The plugins can either return #GsApp objects of kind
 * %GS_APP_KIND_NORMAL for bonafide applications, or #GsApp's of kind
 * %GS_APP_KIND_PACKAGE for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %GS_APP_KIND_PACKAGE will have been promoted to a kind of %GS_APP_KIND_NORMAL,
 * or if they are core applications, promoted again to a kind of %GS_APP_KIND_SYSTEM.
 *
 * Any #GsApp's of kind %GS_APP_KIND_PACKAGE or %GS_APP_KIND_SYSTEM that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %GS_APP_KIND_NORMAL.
 *
 * The #GsApps will all initially be in state %AS_APP_STATE_INSTALLED.
 **/
void
gs_plugin_loader_get_installed_async (GsPluginLoader *plugin_loader,
				      GsPluginRefineFlags flags,
				      GCancellable *cancellable,
				      GAsyncReadyCallback callback,
				      gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_get_installed_thread_cb);
}

/**
 * gs_plugin_loader_get_installed_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_get_installed_finish (GsPluginLoader *plugin_loader,
				       GAsyncResult *res,
				       GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

/**
 * gs_plugin_loader_get_popular_thread_cb:
 **/
static void
gs_plugin_loader_get_popular_thread_cb (GTask *task,
					gpointer object,
					gpointer task_data,
					GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GError *error = NULL;

	/* do things that would block */
	state->list = gs_plugin_loader_run_results (plugin_loader,
						    "gs_plugin_add_popular",
						    state->flags,
						    cancellable,
						    &error);
	if (error != NULL) {
		g_task_return_error (task, error);
		return;
	}

	/* filter package list */
	gs_plugin_list_filter (&state->list, gs_plugin_loader_app_is_valid, state);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_filter_qt_for_gtk, NULL);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
	if (state->list == NULL) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "no popular apps to show");
		return;
	}

	/* success */
	g_task_return_pointer (task, gs_plugin_list_copy (state->list), (GDestroyNotify) gs_plugin_list_free);
}

/**
 * gs_plugin_loader_get_popular_async:
 **/
void
gs_plugin_loader_get_popular_async (GsPluginLoader *plugin_loader,
				    GsPluginRefineFlags flags,
				    GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_get_popular_thread_cb);
}

/**
 * gs_plugin_loader_get_popular_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_get_popular_finish (GsPluginLoader *plugin_loader,
				     GAsyncResult *res,
				     GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

/**
 * gs_plugin_loader_featured_debug:
 **/
static gboolean
gs_plugin_loader_featured_debug (GsApp *app, gpointer user_data)
{
	if (g_strcmp0 (gs_app_get_id (app),
	    g_getenv ("GNOME_SOFTWARE_FEATURED")) == 0)
		return TRUE;
	return FALSE;
}

/**
 * gs_plugin_loader_get_featured_thread_cb:
 **/
static void
gs_plugin_loader_get_featured_thread_cb (GTask *task,
					 gpointer object,
					 gpointer task_data,
					 GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GError *error = NULL;

	/* do things that would block */
	state->list = gs_plugin_loader_run_results (plugin_loader,
						    "gs_plugin_add_featured",
						    state->flags,
						    cancellable,
						    &error);
	if (error != NULL) {
		g_task_return_error (task, error);
		return;
	}

	/* filter package list */
	if (g_getenv ("GNOME_SOFTWARE_FEATURED") != NULL) {
		gs_plugin_list_filter (&state->list, gs_plugin_loader_featured_debug, NULL);
	} else {
		gs_plugin_list_filter (&state->list, gs_plugin_loader_app_is_valid, state);
		gs_plugin_list_filter (&state->list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
	}
	if (state->list == NULL) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "no featured apps to show");
		return;
	}

	/* success */
	g_task_return_pointer (task, gs_plugin_list_copy (state->list), (GDestroyNotify) gs_plugin_list_free);
}

/**
 * gs_plugin_loader_get_featured_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_featured()
 * function. The plugins can either return #GsApp objects of kind
 * %GS_APP_KIND_NORMAL for bonafide applications, or #GsApp's of kind
 * %GS_APP_KIND_PACKAGE for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %GS_APP_KIND_PACKAGE will have been promoted to a kind of %GS_APP_KIND_NORMAL,
 * or if they are core applications, promoted again to a kind of %GS_APP_KIND_SYSTEM.
 *
 * Any #GsApp's of kind %GS_APP_KIND_PACKAGE that remain after refining are
 * automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %GS_APP_KIND_NORMAL or %GS_APP_KIND_SYSTEM.
 *
 * The #GsApps may be in state %AS_APP_STATE_INSTALLED or %AS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_get_featured_async (GsPluginLoader *plugin_loader,
				     GsPluginRefineFlags flags,
				     GCancellable *cancellable,
				     GAsyncReadyCallback callback,
				     gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_get_featured_thread_cb);
}

/**
 * gs_plugin_loader_get_featured_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_get_featured_finish (GsPluginLoader *plugin_loader,
				      GAsyncResult *res,
				      GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

/**
 * gs_plugin_loader_convert_unavailable_app:
 **/
static gboolean
gs_plugin_loader_convert_unavailable_app (GsApp *app, const gchar *search)
{
	GPtrArray *keywords;
	const gchar *keyword;
	guint i;
	g_autoptr(GString) tmp = NULL;

	/* is the search string one of the codec keywords */
	keywords = gs_app_get_keywords (app);
	for (i = 0; i < keywords->len; i++) {
		keyword = g_ptr_array_index (keywords, i);
		if (g_ascii_strcasecmp (search, keyword) == 0) {
			search = keyword;
			break;
		}
	}

	tmp = g_string_new ("");
	/* TRANSLATORS: this is when we know about an application or
	 * addon, but it can't be listed for some reason */
	g_string_append_printf (tmp, _("No addon codecs are available "
				"for the %s format."), search);
	g_string_append (tmp, "\n");
	g_string_append_printf (tmp, _("Information about %s, as well as options "
				"for how to get a codec that can play this format "
				"can be found on the website."), search);
	gs_app_set_summary_missing (app, tmp->str);
	gs_app_set_kind (app, GS_APP_KIND_MISSING);
	gs_app_set_size (app, GS_APP_SIZE_MISSING);
	return TRUE;
}

/**
 * gs_plugin_loader_convert_unavailable:
 **/
static void
gs_plugin_loader_convert_unavailable (GList *list, const gchar *search)
{
	GList *l;
	GsApp *app;

	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_kind (app) != GS_APP_KIND_UNKNOWN &&
		    gs_app_get_kind (app) != GS_APP_KIND_MISSING)
			continue;
		if (gs_app_get_state (app) != AS_APP_STATE_UNAVAILABLE)
			continue;
		if (gs_app_get_id_kind (app) != AS_ID_KIND_CODEC)
			continue;
		if (gs_app_get_url (app, AS_URL_KIND_MISSING) == NULL)
			continue;

		/* only convert the first unavailable codec */
		if (gs_plugin_loader_convert_unavailable_app (app, search))
			break;
	}
}

/**
 * gs_plugin_loader_search_thread_cb:
 **/
static void
gs_plugin_loader_search_thread_cb (GTask *task,
				   gpointer object,
				   gpointer task_data,
				   GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_add_search";
	gboolean ret = TRUE;
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginSearchFunc plugin_func = NULL;
	guint i;
	g_auto(GStrv) values = NULL;

	/* run each plugin */
	values = as_utils_search_tokenize (state->value);
	if (values == NULL) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "no valid search terms");
		return;
	}
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  plugin->name,
					  function_name);
		ret = plugin_func (plugin, values, &state->list, cancellable, &error);
		if (!ret) {
			g_task_return_error (task, error);
			return;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* dedupe applications we already know about */
	gs_plugin_loader_list_dedupe (plugin_loader, state->list);

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   &state->list,
					   state->flags,
					   cancellable,
					   &error);
	if (!ret) {
		g_task_return_error (task, error);
		return;
	}

	/* convert any unavailables */
	gs_plugin_loader_convert_unavailable (state->list, state->value);

	/* filter package list */
	gs_plugin_list_filter_duplicates (&state->list);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_app_is_valid, state);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_filter_qt_for_gtk, NULL);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
	if (state->list == NULL) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "no search results to show");
		return;
	}
	if (g_list_length (state->list) > 500) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "Too many search results returned");
		return;
	}

	/* success */
	g_task_return_pointer (task, gs_plugin_list_copy (state->list), (GDestroyNotify) gs_plugin_list_free);
}

/**
 * gs_plugin_loader_search_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_search()
 * function. The plugins can either return #GsApp objects of kind
 * %GS_APP_KIND_NORMAL for bonafide applications, or #GsApp's of kind
 * %GS_APP_KIND_PACKAGE for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %GS_APP_KIND_PACKAGE will have been promoted to a kind of %GS_APP_KIND_NORMAL,
 * or if they are core applications, promoted again to a kind of %GS_APP_KIND_SYSTEM.
 *
 * Any #GsApp's of kind %GS_APP_KIND_PACKAGE or %GS_APP_KIND_SYSTEM that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %GS_APP_KIND_NORMAL.
 *
 * The #GsApps may be in state %AS_APP_STATE_INSTALLED or %AS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_search_async (GsPluginLoader *plugin_loader,
			       const gchar *value,
			       GsPluginRefineFlags flags,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->value = g_strdup (value);

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_search_thread_cb);
}

/**
 * gs_plugin_loader_search_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_search_finish (GsPluginLoader *plugin_loader,
				GAsyncResult *res,
				GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

/**
 * gs_plugin_loader_search_files_thread_cb:
 **/
static void
gs_plugin_loader_search_files_thread_cb (GTask *task,
                                         gpointer object,
                                         gpointer task_data,
                                         GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_add_search_files";
	gboolean ret = TRUE;
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginSearchFunc plugin_func = NULL;
	guint i;
	g_auto(GStrv) values = NULL;

	values = g_new0 (gchar *, 2);
	values[0] = g_strdup (state->value);

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  plugin->name,
					  function_name);
		ret = plugin_func (plugin, values, &state->list, cancellable, &error);
		if (!ret) {
			g_task_return_error (task, error);
			return;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* dedupe applications we already know about */
	gs_plugin_loader_list_dedupe (plugin_loader, state->list);

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   &state->list,
					   state->flags,
					   cancellable,
					   &error);
	if (!ret) {
		g_task_return_error (task, error);
		return;
	}

	/* convert any unavailables */
	gs_plugin_loader_convert_unavailable (state->list, state->value);

	/* filter package list */
	gs_plugin_list_filter_duplicates (&state->list);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_app_is_valid, state);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_app_is_non_installed, NULL);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_filter_qt_for_gtk, NULL);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
	if (state->list == NULL) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "no search results to show");
		return;
	}
	if (g_list_length (state->list) > 500) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "Too many search results returned");
		return;
	}

	/* success */
	g_task_return_pointer (task, gs_plugin_list_copy (state->list), (GDestroyNotify) gs_plugin_list_free);
}

/**
 * gs_plugin_loader_search_files_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_search_files()
 * function. The plugins can either return #GsApp objects of kind
 * %GS_APP_KIND_NORMAL for bonafide applications, or #GsApp's of kind
 * %GS_APP_KIND_PACKAGE for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %GS_APP_KIND_PACKAGE will have been promoted to a kind of %GS_APP_KIND_NORMAL,
 * or if they are core applications, promoted again to a kind of %GS_APP_KIND_SYSTEM.
 *
 * Any #GsApp's of kind %GS_APP_KIND_PACKAGE or %GS_APP_KIND_SYSTEM that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %GS_APP_KIND_NORMAL.
 *
 * The #GsApps may be in state %AS_APP_STATE_INSTALLED or %AS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_search_files_async (GsPluginLoader *plugin_loader,
                                     const gchar *value,
                                     GsPluginRefineFlags flags,
                                     GCancellable *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->value = g_strdup (value);

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_search_files_thread_cb);
}

/**
 * gs_plugin_loader_search_files_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_search_files_finish (GsPluginLoader *plugin_loader,
                                      GAsyncResult *res,
                                      GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

/**
 * gs_plugin_loader_search_what_provides_thread_cb:
 **/
static void
gs_plugin_loader_search_what_provides_thread_cb (GTask *task,
                                                 gpointer object,
                                                 gpointer task_data,
                                                 GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_add_search_what_provides";
	gboolean ret = TRUE;
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginSearchFunc plugin_func = NULL;
	guint i;
	g_auto(GStrv) values = NULL;

	values = g_new0 (gchar *, 2);
	values[0] = g_strdup (state->value);

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  plugin->name,
					  function_name);
		ret = plugin_func (plugin, values, &state->list, cancellable, &error);
		if (!ret) {
			g_task_return_error (task, error);
			return;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* dedupe applications we already know about */
	gs_plugin_loader_list_dedupe (plugin_loader, state->list);

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   &state->list,
					   state->flags,
					   cancellable,
					   &error);
	if (!ret) {
		g_task_return_error (task, error);
		return;
	}

	/* convert any unavailables */
	gs_plugin_loader_convert_unavailable (state->list, state->value);

	/* filter package list */
	gs_plugin_list_filter_duplicates (&state->list);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_app_is_valid, state);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_app_is_non_installed, NULL);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_filter_qt_for_gtk, NULL);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
	if (state->list == NULL) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "no search results to show");
		return;
	}
	if (g_list_length (state->list) > 500) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "Too many search results returned");
		return;
	}

	/* success */
	g_task_return_pointer (task, gs_plugin_list_copy (state->list), (GDestroyNotify) gs_plugin_list_free);
}

/**
 * gs_plugin_loader_search_what_provides_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_search_what_provides()
 * function. The plugins can either return #GsApp objects of kind
 * %GS_APP_KIND_NORMAL for bonafide applications, or #GsApp's of kind
 * %GS_APP_KIND_PACKAGE for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %GS_APP_KIND_PACKAGE will have been promoted to a kind of %GS_APP_KIND_NORMAL,
 * or if they are core applications, promoted again to a kind of %GS_APP_KIND_SYSTEM.
 *
 * Any #GsApp's of kind %GS_APP_KIND_PACKAGE or %GS_APP_KIND_SYSTEM that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %GS_APP_KIND_NORMAL.
 *
 * The #GsApps may be in state %AS_APP_STATE_INSTALLED or %AS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_search_what_provides_async (GsPluginLoader *plugin_loader,
                                             const gchar *value,
                                             GsPluginRefineFlags flags,
                                             GCancellable *cancellable,
                                             GAsyncReadyCallback callback,
                                             gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->value = g_strdup (value);

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_search_what_provides_thread_cb);
}

/**
 * gs_plugin_loader_search_what_provides_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_search_what_provides_finish (GsPluginLoader *plugin_loader,
                                              GAsyncResult *res,
                                              GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

/**
 * gs_plugin_loader_category_sort_cb:
 **/
static gint
gs_plugin_loader_category_sort_cb (gconstpointer a, gconstpointer b)
{
	return g_strcmp0 (gs_category_get_name (GS_CATEGORY (a)),
			  gs_category_get_name (GS_CATEGORY (b)));
}

/**
 * gs_plugin_loader_get_categories_thread_cb:
 **/
static void
gs_plugin_loader_get_categories_thread_cb (GTask *task,
					   gpointer object,
					   gpointer task_data,
					   GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_add_categories";
	gboolean ret = TRUE;
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginResultsFunc plugin_func = NULL;
	GList *l;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  plugin->name,
					  function_name);
		ret = plugin_func (plugin, &state->list, cancellable, &error);
		if (!ret) {
			g_task_return_error (task, error);
			return;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* sort by name */
	state->list = g_list_sort (state->list, gs_plugin_loader_category_sort_cb);
	for (l = state->list; l != NULL; l = l->next)
		gs_category_sort_subcategories (GS_CATEGORY (l->data));

	/* success */
	if (state->list == NULL) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "no categories to show");
		return;
	}

	/* success */
	g_task_return_pointer (task, gs_plugin_list_copy (state->list), (GDestroyNotify) gs_plugin_list_free);
}

/**
 * gs_plugin_loader_get_categories_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_categories()
 * function. The plugins return #GsCategory objects.
 **/
void
gs_plugin_loader_get_categories_async (GsPluginLoader *plugin_loader,
				       GsPluginRefineFlags flags,
				       GCancellable *cancellable,
				       GAsyncReadyCallback callback,
				       gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_get_categories_thread_cb);
}

/**
 * gs_plugin_loader_get_categories_finish:
 *
 * Return value: (element-type GsCategory) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_get_categories_finish (GsPluginLoader *plugin_loader,
					GAsyncResult *res,
					GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

/**
 * gs_plugin_loader_get_category_apps_thread_cb:
 **/
static void
gs_plugin_loader_get_category_apps_thread_cb (GTask *task,
					      gpointer object,
					      gpointer task_data,
					      GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_add_category_apps";
	gboolean ret = TRUE;
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginCategoryFunc plugin_func = NULL;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  plugin->name,
					  function_name);
		ret = plugin_func (plugin, state->category, &state->list, cancellable, &error);
		if (!ret) {
			g_task_return_error (task, error);
			return;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* dedupe applications we already know about */
	gs_plugin_loader_list_dedupe (plugin_loader, state->list);

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   &state->list,
					   state->flags,
					   cancellable,
					   &error);
	if (!ret) {
		g_task_return_error (task, error);
		return;
	}

	/* filter package list */
	gs_plugin_list_filter_duplicates (&state->list);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_app_is_non_system, NULL);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_app_is_valid, state);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_filter_qt_for_gtk, NULL);
	gs_plugin_list_filter (&state->list, gs_plugin_loader_get_app_is_compatible, plugin_loader);
	if (state->list == NULL) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "no get_category_apps results to show");
		return;
	}

	/* sort, just in case the UI doesn't do this */
	state->list = g_list_sort (state->list, gs_plugin_loader_app_sort_cb);

	/* success */
	g_task_return_pointer (task, gs_plugin_list_copy (state->list), (GDestroyNotify) gs_plugin_list_free);
}

/**
 * gs_plugin_loader_get_category_apps_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_category_apps()
 * function. The plugins can either return #GsApp objects of kind
 * %GS_APP_KIND_NORMAL for bonafide applications, or #GsApp's of kind
 * %GS_APP_KIND_PACKAGE for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %GS_APP_KIND_PACKAGE will have been promoted to a kind of %GS_APP_KIND_NORMAL,
 * or if they are core applications, promoted again to a kind of %GS_APP_KIND_SYSTEM.
 *
 * Any #GsApp's of kind %GS_APP_KIND_PACKAGE or %GS_APP_KIND_SYSTEM that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %GS_APP_KIND_NORMAL.
 *
 * The #GsApps may be in state %AS_APP_STATE_INSTALLED or %AS_APP_STATE_AVAILABLE
 * and the UI may want to filter the two classes of applications differently.
 **/
void
gs_plugin_loader_get_category_apps_async (GsPluginLoader *plugin_loader,
					  GsCategory *category,
					  GsPluginRefineFlags flags,
					  GCancellable *cancellable,
					  GAsyncReadyCallback callback,
					  gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->category = g_object_ref (category);

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_get_category_apps_thread_cb);
}

/**
 * gs_plugin_loader_get_category_apps_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_get_category_apps_finish (GsPluginLoader *plugin_loader,
					   GAsyncResult *res,
					   GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

/**
 * gs_plugin_loader_app_refine_thread_cb:
 **/
static void
gs_plugin_loader_app_refine_thread_cb (GTask *task,
				       gpointer object,
				       gpointer task_data,
				       GCancellable *cancellable)
{
	GError *error = NULL;
	GList *list = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	gboolean ret;

	gs_plugin_add_app (&list, state->app);
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   NULL,
					   &list,
					   state->flags,
					   cancellable,
					   &error);
	if (!ret) {
		g_task_return_error (task, error);
		goto out;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
out:
	gs_plugin_list_free (list);
}

/**
 * gs_plugin_loader_app_refine_async:
 *
 * This method calls all plugins that implement the gs_plugin_refine()
 * function.
 **/
void
gs_plugin_loader_app_refine_async (GsPluginLoader *plugin_loader,
				   GsApp *app,
				   GsPluginRefineFlags flags,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->app = g_object_ref (app);
	state->flags = flags;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_app_refine_thread_cb);
}

/**
 * gs_plugin_loader_app_refine_finish:
 *
 * Return value: success
 **/
gboolean
gs_plugin_loader_app_refine_finish (GsPluginLoader *plugin_loader,
				    GAsyncResult *res,
				    GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), FALSE);
	g_return_val_if_fail (G_IS_TASK (res), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return g_task_propagate_boolean (G_TASK (res), error);
}

/******************************************************************************/

static gboolean
emit_pending_apps_idle (gpointer loader)
{
	g_signal_emit (loader, signals[SIGNAL_PENDING_APPS_CHANGED], 0);
	g_object_unref (loader);

	return G_SOURCE_REMOVE;
}

/**
 * gs_plugin_loader_app_action_thread_cb:
 **/
static void
gs_plugin_loader_app_action_thread_cb (GTask *task,
				       gpointer object,
				       gpointer task_data,
				       GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GPtrArray *addons;
	gboolean ret;
	guint i;
	g_autoptr(GsAppList) list = NULL;

	/* add to list */
	g_mutex_lock (&priv->pending_apps_mutex);
	g_ptr_array_add (priv->pending_apps, g_object_ref (state->app));
	g_mutex_unlock (&priv->pending_apps_mutex);
	g_idle_add (emit_pending_apps_idle, g_object_ref (plugin_loader));

	/* perform action */
	ret = gs_plugin_loader_run_action (plugin_loader,
					   state->app,
					   state->function_name,
					   cancellable,
					   &error);
	if (ret) {
		if (state->state_success != AS_APP_STATE_UNKNOWN) {
			gs_app_set_state (state->app, state->state_success);
			addons = gs_app_get_addons (state->app);
			for (i = 0; i < addons->len; i++) {
				GsApp *addon = g_ptr_array_index (addons, i);
				if (gs_app_get_to_be_installed (addon)) {
					gs_app_set_state (addon, state->state_success);
					gs_app_set_to_be_installed (addon, FALSE);
				}
			}
		}

		/* refine again to make sure we pick up new source id */
		gs_plugin_add_app (&list, state->app);
		ret = gs_plugin_loader_run_refine (plugin_loader,
						   state->function_name,
						   &list,
						   GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN,
						   cancellable,
						   &error);
		if (ret) {
			g_task_return_boolean (task, TRUE);
		} else {
			g_task_return_error (task, error);
		}
	} else {
		if (state->state_failure != AS_APP_STATE_UNKNOWN) {
			gs_app_set_state (state->app, state->state_failure);
			addons = gs_app_get_addons (state->app);
			for (i = 0; i < addons->len; i++) {
				GsApp *addon = g_ptr_array_index (addons, i);
				if (gs_app_get_to_be_installed (addon)) {
					gs_app_set_state (addon, state->state_failure);
					gs_app_set_to_be_installed (addon, FALSE);
				}
			}
		}
		g_task_return_error (task, error);
	}

	/* remove from list */
	g_mutex_lock (&priv->pending_apps_mutex);
	g_ptr_array_remove (priv->pending_apps, state->app);
	g_mutex_unlock (&priv->pending_apps_mutex);
	g_idle_add (emit_pending_apps_idle, g_object_ref (plugin_loader));
}

static gboolean
load_install_queue (GsPluginLoader *plugin_loader, GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GList *list = NULL;
	gboolean ret = TRUE;
	guint i;
	g_autofree gchar *contents = NULL;
	g_autofree gchar *file = NULL;
	g_auto(GStrv) names = NULL;

	/* load from file */
	file = g_build_filename (g_get_user_data_dir (),
				 "gnome-software",
				 "install-queue",
				 NULL);
	if (!g_file_test (file, G_FILE_TEST_EXISTS))
		goto out;
	g_debug ("loading install queue from %s", file);
	ret = g_file_get_contents (file, &contents, NULL, error);
	if (!ret)
		goto out;

	/* add each app-id */
	names = g_strsplit (contents, "\n", 0);
	for (i = 0; names[i]; i++) {
		g_autoptr(GsApp) app = NULL;
		if (strlen (names[i]) == 0)
			continue;
		app = gs_app_new (names[i]);
		gs_app_set_state (app, AS_APP_STATE_QUEUED_FOR_INSTALL);

		g_mutex_lock (&priv->app_cache_mutex);
		g_hash_table_insert (priv->app_cache,
				     g_strdup (gs_app_get_id (app)),
				     g_object_ref (app));
		g_mutex_unlock (&priv->app_cache_mutex);

		g_mutex_lock (&priv->pending_apps_mutex);
		g_ptr_array_add (priv->pending_apps,
				 g_object_ref (app));
		g_mutex_unlock (&priv->pending_apps_mutex);

		g_debug ("adding pending app %s", gs_app_get_id (app));
		gs_plugin_add_app (&list, app);
	}

	/* refine */
	if (list != NULL) {
		ret = gs_plugin_loader_run_refine (plugin_loader,
						   NULL,
						   &list,
						   GS_PLUGIN_REFINE_FLAGS_DEFAULT,
						   NULL, //FIXME?
						   error);
		if (!ret)
			goto out;
	}
out:
	gs_plugin_list_free (list);
	return ret;
}

static void
save_install_queue (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GPtrArray *pending_apps;
	gboolean ret;
	gint i;
	g_autoptr(GError) error = NULL;
	g_autoptr(GString) s = NULL;
	g_autofree gchar *file = NULL;

	s = g_string_new ("");
	pending_apps = priv->pending_apps;
	g_mutex_lock (&priv->pending_apps_mutex);
	for (i = pending_apps->len - 1; i >= 0; i--) {
		GsApp *app;
		app = g_ptr_array_index (pending_apps, i);
		if (gs_app_get_state (app) == AS_APP_STATE_QUEUED_FOR_INSTALL) {
			g_string_append (s, gs_app_get_id (app));
			g_string_append_c (s, '\n');
		}
	}
	g_mutex_unlock (&priv->pending_apps_mutex);

	/* save file */
	file = g_build_filename (g_get_user_data_dir (),
				 "gnome-software",
				 "install-queue",
				 NULL);
	g_debug ("saving install queue to %s", file);
	ret = g_file_set_contents (file, s->str, s->len, &error);
	if (!ret)
		g_warning ("failed to save install queue: %s", error->message);
}

static void
add_app_to_install_queue (GsPluginLoader *plugin_loader, GsApp *app)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GPtrArray *addons;
	guint i;
	guint id;

	/* queue the app itself */
	g_mutex_lock (&priv->pending_apps_mutex);
	g_ptr_array_add (priv->pending_apps, g_object_ref (app));
	g_mutex_unlock (&priv->pending_apps_mutex);

	gs_app_set_state (app, AS_APP_STATE_QUEUED_FOR_INSTALL);
	id = g_idle_add (emit_pending_apps_idle, g_object_ref (plugin_loader));
	g_source_set_name_by_id (id, "[gnome-software] emit_pending_apps_idle");
	save_install_queue (plugin_loader);

	/* recursively queue any addons */
	addons = gs_app_get_addons (app);
	for (i = 0; i < addons->len; i++) {
		GsApp *addon = g_ptr_array_index (addons, i);
		if (gs_app_get_to_be_installed (addon))
			add_app_to_install_queue (plugin_loader, addon);
	}
}

static gboolean
remove_app_from_install_queue (GsPluginLoader *plugin_loader, GsApp *app)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GPtrArray *addons;
	gboolean ret;
	guint i;
	guint id;

	g_mutex_lock (&priv->pending_apps_mutex);
	ret = g_ptr_array_remove (priv->pending_apps, app);
	g_mutex_unlock (&priv->pending_apps_mutex);

	if (ret) {
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		id = g_idle_add (emit_pending_apps_idle, g_object_ref (plugin_loader));
		g_source_set_name_by_id (id, "[gnome-software] emit_pending_apps_idle");
		save_install_queue (plugin_loader);

		/* recursively remove any queued addons */
		addons = gs_app_get_addons (app);
		for (i = 0; i < addons->len; i++) {
			GsApp *addon = g_ptr_array_index (addons, i);
			remove_app_from_install_queue (plugin_loader, addon);
		}
	}

	return ret;
}

/**
 * gs_plugin_loader_app_action_async:
 *
 * This method calls all plugins that implement the gs_plugin_action()
 * function.
 **/
void
gs_plugin_loader_app_action_async (GsPluginLoader *plugin_loader,
				   GsApp *app,
				   GsPluginLoaderAction action,
				   GCancellable *cancellable,
				   GAsyncReadyCallback callback,
				   gpointer user_data)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	if (action == GS_PLUGIN_LOADER_ACTION_REMOVE) {
		if (remove_app_from_install_queue (plugin_loader, app)) {
			task = g_task_new (plugin_loader, cancellable, callback, user_data);
			g_task_return_boolean (task, TRUE);
			return;
		}
	}

	if (action == GS_PLUGIN_LOADER_ACTION_INSTALL &&
	    !priv->online) {
		add_app_to_install_queue (plugin_loader, app);
		task = g_task_new (plugin_loader, cancellable, callback, user_data);
		g_task_return_boolean (task, TRUE);
		return;
	}

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->app = g_object_ref (app);

	switch (action) {
	case GS_PLUGIN_LOADER_ACTION_INSTALL:
		state->function_name = "gs_plugin_app_install";
		state->state_success = AS_APP_STATE_INSTALLED;
		state->state_failure = AS_APP_STATE_AVAILABLE;
		break;
	case GS_PLUGIN_LOADER_ACTION_REMOVE:
		state->function_name = "gs_plugin_app_remove";
		state->state_success = AS_APP_STATE_AVAILABLE;
		state->state_failure = AS_APP_STATE_INSTALLED;
		break;
	case GS_PLUGIN_LOADER_ACTION_SET_RATING:
		state->function_name = "gs_plugin_app_set_rating";
		state->state_success = AS_APP_STATE_UNKNOWN;
		state->state_failure = AS_APP_STATE_UNKNOWN;
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_app_action_thread_cb);
}

/**
 * gs_plugin_loader_app_action_finish:
 *
 * Return value: success
 **/
gboolean
gs_plugin_loader_app_action_finish (GsPluginLoader *plugin_loader,
				    GAsyncResult *res,
				    GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), FALSE);
	g_return_val_if_fail (G_IS_TASK (res), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return g_task_propagate_boolean (G_TASK (res), error);
}

/******************************************************************************/


/**
 * gs_plugin_loader_get_state_for_app:
 **/
AsAppState
gs_plugin_loader_get_state_for_app (GsPluginLoader *plugin_loader, GsApp *app)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	AsAppState state = AS_APP_STATE_UNKNOWN;
	GsApp *tmp;
	guint i;

	g_mutex_lock (&priv->pending_apps_mutex);
	for (i = 0; i < priv->pending_apps->len; i++) {
		tmp = g_ptr_array_index (priv->pending_apps, i);
		if (g_strcmp0 (gs_app_get_id (tmp), gs_app_get_id (app)) == 0) {
			state = gs_app_get_state (tmp);
			break;
		}
	}
	g_mutex_unlock (&priv->pending_apps_mutex);
	return state;
}

/**
 * gs_plugin_loader_get_pending:
 **/
GPtrArray *
gs_plugin_loader_get_pending (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GPtrArray *array;
	guint i;

	array = g_ptr_array_new_with_free_func ((GFreeFunc) g_object_unref);

	g_mutex_lock (&priv->pending_apps_mutex);
	for (i = 0; i < priv->pending_apps->len; i++) {
		GsApp *app = g_ptr_array_index (priv->pending_apps, i);
		g_ptr_array_add (array, g_object_ref (app));
	}
	g_mutex_unlock (&priv->pending_apps_mutex);

	return array;
}

/**
 * gs_plugin_loader_run:
 **/
static void
gs_plugin_loader_run (GsPluginLoader *plugin_loader, const gchar *function_name)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	gboolean ret;
	GsPluginFunc plugin_func = NULL;
	GsPlugin *plugin;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  plugin->name,
					  function_name);
		plugin_func (plugin);
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}
}

/**
 * gs_plugin_loader_set_enabled:
 */
gboolean
gs_plugin_loader_set_enabled (GsPluginLoader *plugin_loader,
			      const gchar *plugin_name,
			      gboolean enabled)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	gboolean ret = FALSE;
	GsPlugin *plugin;
	guint i;

	for (i = 0; i < priv->plugins->len; i++) {
		plugin = g_ptr_array_index (priv->plugins, i);
		if (g_strcmp0 (plugin->name, plugin_name) == 0) {
			plugin->enabled = enabled;
			ret = TRUE;
			break;
		}
	}
	return ret;
}

/**
 * gs_plugin_loader_status_update_cb:
 */
static void
gs_plugin_loader_status_update_cb (GsPlugin *plugin,
				   GsApp *app,
				   GsPluginStatus status,
				   gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);

	/* same as last time */
	if (app == NULL && status == priv->status_last)
		return;

	/* new, or an app, so emit */
	g_debug ("emitting %s(%s)",
		 gs_plugin_status_to_string (status),
		 app != NULL ? gs_app_get_id (app) : "<general>");
	priv->status_last = status;
	g_signal_emit (plugin_loader,
		       signals[SIGNAL_STATUS_CHANGED],
		       0, app, status);
}

/**
 * gs_plugin_loader_updates_changed_delay_cb:
 */
static gboolean
gs_plugin_loader_updates_changed_delay_cb (gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GList *apps;
	GList *l;
	GsApp *app;

	/* no longer know the state of these */
	g_mutex_lock (&priv->app_cache_mutex);
	apps = g_hash_table_get_values (priv->app_cache);
	for (l = apps; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
	}
	g_list_free (apps);

	/* not valid anymore */
	g_hash_table_remove_all (priv->app_cache);
	g_mutex_unlock (&priv->app_cache_mutex);

	/* notify shells */
	g_debug ("updates-changed");
	g_signal_emit (plugin_loader, signals[SIGNAL_UPDATES_CHANGED], 0);
	priv->updates_changed_id = 0;
	return FALSE;
}

/**
 * gs_plugin_loader_updates_changed_cb:
 */
static void
gs_plugin_loader_updates_changed_cb (GsPlugin *plugin, gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (user_data);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	if (priv->updates_changed_id != 0)
		return;
	priv->updates_changed_id =
		g_timeout_add_seconds (GS_PLUGIN_LOADER_UPDATES_CHANGED_DELAY,
				       gs_plugin_loader_updates_changed_delay_cb,
				       plugin_loader);
}

/**
 * gs_plugin_loader_open_plugin:
 */
static GsPlugin *
gs_plugin_loader_open_plugin (GsPluginLoader *plugin_loader,
			      const gchar *filename)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	gboolean ret;
	GModule *module;
	GsPluginGetNameFunc plugin_name = NULL;
	GsPluginGetDepsFunc plugin_deps = NULL;
	GsPlugin *plugin = NULL;

	module = g_module_open (filename, 0);
	if (module == NULL) {
		g_warning ("failed to open plugin %s: %s",
			   filename, g_module_error ());
		return NULL;
	}

	/* get description */
	ret = g_module_symbol (module,
			       "gs_plugin_get_name",
			       (gpointer *) &plugin_name);
	if (!ret) {
		g_warning ("Plugin %s requires name", filename);
		g_module_close (module);
		return NULL;
	}

	/* get plugins this plugin depends on */
	(void) g_module_symbol (module,
	                        "gs_plugin_get_deps",
	                        (gpointer *) &plugin_deps);

	/* print what we know */
	plugin = g_slice_new0 (GsPlugin);
	plugin->enabled = TRUE;
	plugin->module = module;
	plugin->pixbuf_size = 64;
	plugin->priority = 0.f;
	plugin->deps = plugin_deps != NULL ? plugin_deps (plugin) : NULL;
	plugin->name = g_strdup (plugin_name ());
	plugin->status_update_fn = gs_plugin_loader_status_update_cb;
	plugin->status_update_user_data = plugin_loader;
	plugin->updates_changed_fn = gs_plugin_loader_updates_changed_cb;
	plugin->updates_changed_user_data = plugin_loader;
	plugin->profile = g_object_ref (priv->profile);
	plugin->scale = gs_plugin_loader_get_scale (plugin_loader);
	g_debug ("opened plugin %s: %s", filename, plugin->name);

	/* add to array */
	g_ptr_array_add (priv->plugins, plugin);
	return plugin;
}

/**
 * gs_plugin_loader_set_scale:
 */
void
gs_plugin_loader_set_scale (GsPluginLoader *plugin_loader, gint scale)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPlugin *plugin;
	guint i;

	/* save globally, and update each plugin */
	priv->scale = scale;
	for (i = 0; i < priv->plugins->len; i++) {
		plugin = g_ptr_array_index (priv->plugins, i);
		plugin->scale = scale;
	}
}

/**
 * gs_plugin_loader_get_scale:
 */
gint
gs_plugin_loader_get_scale (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	return priv->scale;
}

/**
 * gs_plugin_loader_set_location:
 */
void
gs_plugin_loader_set_location (GsPluginLoader *plugin_loader, const gchar *location)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	g_autofree gchar *filename = NULL;

	g_free (priv->location);

	/* something non-default specified */
	if (location != NULL) {
		priv->location = g_strdup (location);
		return;
	}

	/* use the default, but this requires a 'make install' */
	filename = g_strdup_printf ("gs-plugins-%s", GS_PLUGIN_API_VERSION);
	priv->location = g_build_filename (LIBDIR, filename, NULL);
}

/**
 * gs_plugin_loader_plugin_sort_fn:
 */
static gint
gs_plugin_loader_plugin_sort_fn (gconstpointer a, gconstpointer b)
{
	GsPlugin **pa = (GsPlugin **) a;
	GsPlugin **pb = (GsPlugin **) b;
	if ((*pa)->priority < (*pb)->priority)
		return -1;
	if ((*pa)->priority > (*pb)->priority)
		return 1;
	return 0;
}

/**
 * gs_plugin_loader_find_plugin:
 */
static GsPlugin *
gs_plugin_loader_find_plugin (GsPluginLoader *plugin_loader,
			      const gchar *plugin_name)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPlugin *plugin;
	guint i;

	for (i = 0; i < priv->plugins->len; i++) {
		plugin = g_ptr_array_index (priv->plugins, i);
		if (g_strcmp0 (plugin->name, plugin_name) == 0)
			return plugin;
	}
	return NULL;
}

/**
 * gs_plugin_loader_setup:
 */
gboolean
gs_plugin_loader_setup (GsPluginLoader *plugin_loader, GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *filename_tmp;
	const gdouble dep_increment = 1.f;
	gboolean changes;
	GsPlugin *dep;
	GsPlugin *plugin;
	guint dep_loop_check = 0;
	guint i;
	guint j;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;

	g_return_val_if_fail (priv->location != NULL, FALSE);

	/* search in the plugin directory for plugins */
	ptask = as_profile_start_literal (priv->profile, "GsPlugin::setup");
	dir = g_dir_open (priv->location, 0, error);
	if (dir == NULL)
		return FALSE;

	/* try to open each plugin */
	g_debug ("searching for plugins in %s", priv->location);
	do {
		g_autofree gchar *filename_plugin = NULL;
		filename_tmp = g_dir_read_name (dir);
		if (filename_tmp == NULL)
			break;
		if (!g_str_has_suffix (filename_tmp, ".so"))
			continue;
		filename_plugin = g_build_filename (priv->location,
						    filename_tmp,
						    NULL);
		gs_plugin_loader_open_plugin (plugin_loader, filename_plugin);
	} while (TRUE);

	/* order by deps */
	do {
		changes = FALSE;
		for (i = 0; i < priv->plugins->len; i++) {
			plugin = g_ptr_array_index (priv->plugins, i);
			if (plugin->deps == NULL)
				continue;
			for (j = 0; plugin->deps[j] != NULL && !changes; j++) {
				dep = gs_plugin_loader_find_plugin (plugin_loader,
								    plugin->deps[j]);
				if (dep == NULL) {
					g_warning ("cannot find plugin '%s'",
						   plugin->deps[j]);
					continue;
				}
				if (!dep->enabled)
					continue;
				if (plugin->priority <= dep->priority) {
					g_debug ("%s [%.1f] requires %s [%.1f] "
						 "so promoting to [%.1f]",
						 plugin->name, plugin->priority,
						 dep->name, dep->priority,
						 dep->priority + dep_increment);
					plugin->priority = dep->priority + dep_increment;
					changes = TRUE;
				}
			}
		}

		/* check we're not stuck */
		if (dep_loop_check++ > 100) {
			g_set_error (error,
				     GS_PLUGIN_LOADER_ERROR,
				     GS_PLUGIN_LOADER_ERROR_FAILED,
				     "got stuck in dep loop");
			return FALSE;
		}
	} while (changes);

	/* sort by priority */
	g_ptr_array_sort (priv->plugins,
			  gs_plugin_loader_plugin_sort_fn);

	/* run the plugins */
	gs_plugin_loader_run (plugin_loader, "gs_plugin_initialize");

	/* now we can load the install-queue */
	if (!load_install_queue (plugin_loader, error))
		return FALSE;
	return TRUE;
}

/**
 * gs_plugin_loader_dump_state:
 **/
void
gs_plugin_loader_dump_state (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPlugin *plugin;
	guint i;

	/* print what the priorities are */
	for (i = 0; i < priv->plugins->len; i++) {
		plugin = g_ptr_array_index (priv->plugins, i);
		g_debug ("[%s]\t%.1f\t->\t%s",
			 plugin->enabled ? "enabled" : "disabled",
			 plugin->priority,
			 plugin->name);
	}
}

/**
 * gs_plugin_loader_plugin_free:
 **/
static void
gs_plugin_loader_plugin_free (GsPlugin *plugin)
{
	g_free (plugin->priv);
	g_free (plugin->name);
	g_object_unref (plugin->profile);
	g_module_close (plugin->module);
	g_slice_free (GsPlugin, plugin);
}

/**
 * gs_plugin_loader_dispose:
 * @object: The object to dispose
 **/
static void
gs_plugin_loader_dispose (GObject *object)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);

	if (priv->plugins != NULL) {
		gs_plugin_loader_run (plugin_loader, "gs_plugin_destroy");
		g_clear_pointer (&priv->plugins, g_ptr_array_unref);
	}
	if (priv->updates_changed_id != 0) {
		g_source_remove (priv->updates_changed_id);
		priv->updates_changed_id = 0;
	}
	g_clear_object (&priv->profile);
	g_clear_object (&priv->settings);
	g_clear_pointer (&priv->app_cache, g_hash_table_unref);
	g_clear_pointer (&priv->pending_apps, g_ptr_array_unref);

	G_OBJECT_CLASS (gs_plugin_loader_parent_class)->dispose (object);
}

/**
 * gs_plugin_loader_finalize:
 * @object: The object to finalize
 **/
static void
gs_plugin_loader_finalize (GObject *object)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);

	g_strfreev (priv->compatible_projects);
	g_free (priv->location);

	g_mutex_clear (&priv->pending_apps_mutex);
	g_mutex_clear (&priv->app_cache_mutex);

	G_OBJECT_CLASS (gs_plugin_loader_parent_class)->finalize (object);
}

/**
 * gs_plugin_loader_class_init:
 * @klass: The GsPluginLoaderClass
 **/
static void
gs_plugin_loader_class_init (GsPluginLoaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = gs_plugin_loader_dispose;
	object_class->finalize = gs_plugin_loader_finalize;

	signals [SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginLoaderClass, status_changed),
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);
	signals [SIGNAL_PENDING_APPS_CHANGED] =
		g_signal_new ("pending-apps-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginLoaderClass, pending_apps_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [SIGNAL_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsPluginLoaderClass, updates_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * gs_plugin_loader_init:
 **/
static void
gs_plugin_loader_init (GsPluginLoader *plugin_loader)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *tmp;
	gchar **projects;
	guint i;

	priv->scale = 1;
	priv->plugins = g_ptr_array_new_with_free_func ((GDestroyNotify) gs_plugin_loader_plugin_free);
	priv->status_last = GS_PLUGIN_STATUS_LAST;
	priv->pending_apps = g_ptr_array_new_with_free_func ((GFreeFunc) g_object_unref);
	priv->profile = as_profile_new ();
	priv->settings = g_settings_new ("org.gnome.software");
	priv->app_cache = g_hash_table_new_full (g_str_hash,
								g_str_equal,
								g_free,
								(GFreeFunc) g_object_unref);

	g_mutex_init (&priv->pending_apps_mutex);
	g_mutex_init (&priv->app_cache_mutex);

	/* by default we only show project-less apps or compatible projects */
	tmp = g_getenv ("GNOME_SOFTWARE_COMPATIBLE_PROJECTS");
	if (tmp == NULL) {
		projects = g_settings_get_strv (priv->settings,
						"compatible-projects");
	} else {
		projects = g_strsplit (tmp, ",", -1);
	}
	for (i = 0; projects[i] != NULL; i++)
		g_debug ("compatible-project: %s", projects[i]);
	priv->compatible_projects = projects;
}

/**
 * gs_plugin_loader_new:
 *
 * Return value: a new GsPluginLoader object.
 **/
GsPluginLoader *
gs_plugin_loader_new (void)
{
	GsPluginLoader *plugin_loader;
	plugin_loader = g_object_new (GS_TYPE_PLUGIN_LOADER, NULL);
	return GS_PLUGIN_LOADER (plugin_loader);
}

/**
 * gs_plugin_loader_app_installed_cb:
 **/
static void
gs_plugin_loader_app_installed_cb (GObject *source,
				   GAsyncResult *res,
				   gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app = GS_APP (user_data);

	ret = gs_plugin_loader_app_action_finish (plugin_loader,
						  res,
						  &error);
	if (!ret) {
		remove_app_from_install_queue (plugin_loader, app);
		g_warning ("failed to install %s: %s",
			   gs_app_get_id (app), error->message);
	}
}

/**
 * gs_plugin_loader_set_network_status:
 **/
void
gs_plugin_loader_set_network_status (GsPluginLoader *plugin_loader,
				     gboolean online)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GList *l;
	GsApp *app;
	guint i;
	g_autoptr(GsAppList) queue = NULL;

	if (priv->online == online)
		return;

	g_debug ("*** Network status change: %s", online ? "online" : "offline");

	priv->online = online;

	if (!online)
		return;

	g_mutex_lock (&priv->pending_apps_mutex);
	for (i = 0; i < priv->pending_apps->len; i++) {
		app = g_ptr_array_index (priv->pending_apps, i);
		if (gs_app_get_state (app) == AS_APP_STATE_QUEUED_FOR_INSTALL)
			gs_plugin_add_app (&queue, app);
	}
	g_mutex_unlock (&priv->pending_apps_mutex);
	for (l = queue; l; l = l->next) {
		app = l->data;
		gs_plugin_loader_app_action_async (plugin_loader,
						   app,
						   GS_PLUGIN_LOADER_ACTION_INSTALL,
						   NULL,
						   gs_plugin_loader_app_installed_cb,
						   g_object_ref (app));
	}
}

/******************************************************************************/

/**
 * gs_plugin_loader_run_refresh_plugin:
 **/
static gboolean
gs_plugin_loader_run_refresh_plugin (GsPluginLoader *plugin_loader,
				     GsPlugin *plugin,
				     guint cache_age,
				     GsPluginRefreshFlags flags,
				     GCancellable *cancellable,
				     GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_refresh";
	gboolean exists;
	gboolean ret = TRUE;
	GError *error_local = NULL;
	GsPluginRefreshFunc plugin_func = NULL;
	g_autoptr(AsProfileTask) ptask = NULL;

	exists = g_module_symbol (plugin->module,
				  function_name,
				  (gpointer *) &plugin_func);
	if (!exists)
		goto out;
	ptask = as_profile_start (priv->profile,
				  "GsPlugin::%s(%s)",
				  plugin->name,
				  function_name);
	ret = plugin_func (plugin, cache_age, flags, cancellable, &error_local);
	if (!ret) {
		if (g_error_matches (error_local,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED)) {
			ret = TRUE;
			g_debug ("not supported for plugin %s: %s",
				 plugin->name,
				 error_local->message);
			g_clear_error (&error_local);
		} else {
			g_propagate_error (error, error_local);
			goto out;
		}
	}
out:
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	return ret;
}

/**
 * gs_plugin_loader_run_refresh:
 **/
static gboolean
gs_plugin_loader_run_refresh (GsPluginLoader *plugin_loader,
			      guint cache_age,
			      GsPluginRefreshFlags flags,
			      GCancellable *cancellable,
			      GError **error)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	gboolean anything_ran = FALSE;
	gboolean ret;
	GsPlugin *plugin;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		if (g_cancellable_set_error_if_cancelled (cancellable, error))
			return FALSE;
		ret = gs_plugin_loader_run_refresh_plugin (plugin_loader,
							   plugin,
							   cache_age,
							   flags,
							   cancellable,
							   error);
		if (!ret)
			return FALSE;
		anything_ran = TRUE;
	}

	/* nothing ran */
	if (!anything_ran) {
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no plugin could handle refresh");
		return FALSE;
	}
	return ret;
}

/**
 * gs_plugin_loader_refresh_thread_cb:
 **/
static void
gs_plugin_loader_refresh_thread_cb (GTask *task,
				    gpointer object,
				    gpointer task_data,
				    GCancellable *cancellable)
{
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	gboolean ret;

	ret = gs_plugin_loader_run_refresh (plugin_loader,
					    state->cache_age,
					    state->flags,
					    cancellable,
					    &error);
	if (!ret) {
		g_task_return_error (task, error);
		return;
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

/**
 * gs_plugin_loader_refresh_async:
 *
 * This method calls all plugins that implement the gs_plugin_refresh()
 * function.
 **/
void
gs_plugin_loader_refresh_async (GsPluginLoader *plugin_loader,
				guint cache_age,
				GsPluginRefreshFlags flags,
				GCancellable *cancellable,
				GAsyncReadyCallback callback,
				gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->cache_age = cache_age;

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_refresh_thread_cb);
}

/**
 * gs_plugin_loader_refresh_finish:
 *
 * Return value: success
 **/
gboolean
gs_plugin_loader_refresh_finish (GsPluginLoader *plugin_loader,
				 GAsyncResult *res,
				 GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), FALSE);
	g_return_val_if_fail (G_IS_TASK (res), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return g_task_propagate_boolean (G_TASK (res), error);
}

/******************************************************************************/

/**
 * gs_plugin_loader_filename_to_app_thread_cb:
 **/
static void
gs_plugin_loader_filename_to_app_thread_cb (GTask *task,
					    gpointer object,
					    gpointer task_data,
					    GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_filename_to_app";
	gboolean ret = TRUE;
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginFilenameToAppFunc plugin_func = NULL;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  plugin->name,
					  function_name);
		ret = plugin_func (plugin, &state->list, state->filename, cancellable, &error);
		if (!ret) {
			g_task_return_error (task, error);
			return;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* dedupe applications we already know about */
	gs_plugin_loader_list_dedupe (plugin_loader, state->list);

	/* run refine() on each one */
	ret = gs_plugin_loader_run_refine (plugin_loader,
					   function_name,
					   &state->list,
					   state->flags,
					   cancellable,
					   &error);
	if (!ret) {
		g_task_return_error (task, error);
		return;
	}

	/* filter package list */
	gs_plugin_list_filter_duplicates (&state->list);
	if (state->list == NULL) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "no filename_to_app results to show");
		return;
	}

	/* success */
	if (g_list_length (state->list) != 1) {
		g_task_return_new_error (task,
					 GS_PLUGIN_LOADER_ERROR,
					 GS_PLUGIN_LOADER_ERROR_NO_RESULTS,
					 "no application was created for %s",
					 state->filename);
		return;
	}
	g_task_return_pointer (task, g_object_ref (state->list->data), (GDestroyNotify) g_object_unref);
}

/**
 * gs_plugin_loader_filename_to_app_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_filename_to_app()
 * function. The plugins can either return #GsApp objects of kind
 * %GS_APP_KIND_NORMAL for bonafide applications, or #GsApp's of kind
 * %GS_APP_KIND_PACKAGE for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %GS_APP_KIND_PACKAGE will have been promoted to a kind of %GS_APP_KIND_NORMAL,
 * or if they are core applications.
 **/
void
gs_plugin_loader_filename_to_app_async (GsPluginLoader *plugin_loader,
					const gchar *filename,
					GsPluginRefineFlags flags,
					GCancellable *cancellable,
					GAsyncReadyCallback callback,
					gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->flags = flags;
	state->filename = g_strdup (filename);

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_filename_to_app_thread_cb);
}

/**
 * gs_plugin_loader_filename_to_app_finish:
 *
 * Return value: (element-type GsApp) (transfer full): An application, or %NULL
 **/
GsApp *
gs_plugin_loader_filename_to_app_finish (GsPluginLoader *plugin_loader,
					 GAsyncResult *res,
					 GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_pointer (G_TASK (res), error);
}

/******************************************************************************/

/**
 * gs_plugin_loader_offline_update_thread_cb:
 **/
static void
gs_plugin_loader_offline_update_thread_cb (GTask *task,
                                           gpointer object,
                                           gpointer task_data,
                                           GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_offline_update";
	gboolean ret = TRUE;
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginOfflineUpdateFunc plugin_func = NULL;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (plugin->module,
		                       function_name,
		                       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  plugin->name,
					  function_name);
		ret = plugin_func (plugin, state->list, cancellable, &error);
		if (!ret) {
			g_task_return_error (task, error);
			return;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	g_task_return_boolean (task, TRUE);
}

/**
 * gs_plugin_loader_offline_update_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_offline_update()
 * function.
 **/
void
gs_plugin_loader_offline_update_async (GsPluginLoader *plugin_loader,
                                       GList *apps,
                                       GCancellable *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
	GsPluginLoaderAsyncState *state;
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	/* save state */
	state = g_slice_new0 (GsPluginLoaderAsyncState);
	state->list = g_list_copy_deep (apps, (GCopyFunc) g_object_ref, NULL);

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_offline_update_thread_cb);
}

/**
 * gs_plugin_loader_offline_update_finish:
 **/
gboolean
gs_plugin_loader_offline_update_finish (GsPluginLoader *plugin_loader,
                                        GAsyncResult *res,
                                        GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);
	g_return_val_if_fail (G_IS_TASK (res), NULL);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	return g_task_propagate_boolean (G_TASK (res), error);
}

/******************************************************************************/

/* vim: set noexpandtab: */
