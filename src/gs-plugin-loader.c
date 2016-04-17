/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2016 Richard Hughes <richard@hughsie.com>
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

#include <locale.h>
#include <glib/gi18n.h>
#include <appstream-glib.h>

#include "gs-plugin-loader.h"
#include "gs-plugin.h"
#include "gs-utils.h"

#define GS_PLUGIN_LOADER_UPDATES_CHANGED_DELAY	3	/* s */

typedef struct
{
	GPtrArray		*plugins;
	gchar			*location;
	gchar			*locale;
	GsPluginStatus		 status_last;
	AsProfile		*profile;
	SoupSession		*soup_session;

	GMutex			 pending_apps_mutex;
	GPtrArray		*pending_apps;

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
	GsReview			*review;
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
	if (state->review != NULL)
		g_object_unref (state->review);

	g_free (state->filename);
	g_free (state->value);
	gs_plugin_list_free (state->list);
	g_slice_free (GsPluginLoaderAsyncState, state);
}

/**
 * gs_plugin_loader_error_quark:
 * Return value: Our personal error quark.
 **/
G_DEFINE_QUARK (gs-plugin-loader-error-quark, gs_plugin_loader_error)

/**
 * gs_plugin_loader_app_sort_cb:
 **/
static gint
gs_plugin_loader_app_sort_cb (gconstpointer a, gconstpointer b)
{
	return g_strcmp0 (gs_app_get_name (GS_APP ((gpointer) a)),
			  gs_app_get_name (GS_APP ((gpointer) b)));
}

/**
 * gs_plugin_loader_run_adopt:
 **/
static void
gs_plugin_loader_run_adopt (GsPluginLoader *plugin_loader, GList *list)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GList *l;
	guint i;

	/* go through each plugin in priority order */
	for (i = 0; i < priv->plugins->len; i++) {
		GsPluginAdoptAppFunc adopt_app_func = NULL;
		GsPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		g_module_symbol (plugin->module, "gs_plugin_adopt_app",
				 (gpointer *) &adopt_app_func);
		if (adopt_app_func == NULL)
			continue;
		for (l = list; l != NULL; l = l->next) {
			GsApp *app = GS_APP (l->data);
			if (gs_app_get_management_plugin (app) != NULL)
				continue;
			g_rw_lock_reader_lock (&plugin->rwlock);
			adopt_app_func (plugin, app);
			g_rw_lock_reader_unlock (&plugin->rwlock);
			if (gs_app_get_management_plugin (app) != NULL) {
				g_debug ("%s adopted %s", plugin->name,
					 gs_app_get_id (app));
			}
		}
	}
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
	const gchar *function_name_app = "gs_plugin_refine_app";
	const gchar *function_name = "gs_plugin_refine";
	gboolean ret = TRUE;
	guint i;
	g_autoptr(GsAppList) addons_list = NULL;
	g_autoptr(GsAppList) freeze_list = NULL;
	g_autoptr(GsAppList) related_list = NULL;

	/* freeze all apps */
	freeze_list = gs_plugin_list_copy (*list);
	for (l = freeze_list; l != NULL; l = l->next)
		g_object_freeze_notify (G_OBJECT (l->data));

	/* try to adopt each application with a plugin */
	gs_plugin_loader_run_adopt (plugin_loader, *list);

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		GsPluginRefineAppFunc plugin_app_func = NULL;
		GsPluginRefineFunc plugin_func = NULL;
		g_autoptr(AsProfileTask) ptask = NULL;

		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;

		/* load the possible symbols */
		g_module_symbol (plugin->module, function_name,
				 (gpointer *) &plugin_func);
		g_module_symbol (plugin->module, function_name_app,
				 (gpointer *) &plugin_app_func);
		if (plugin_func == NULL && plugin_app_func == NULL)
			continue;

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

		/* run the batched plugin symbol then the per-app plugin */
		if (plugin_func != NULL) {
			g_autoptr(GError) error_local = NULL;
			g_rw_lock_reader_lock (&plugin->rwlock);
			ret = plugin_func (plugin, list, flags,
					   cancellable, &error_local);
			g_rw_lock_reader_unlock (&plugin->rwlock);
			if (!ret) {
				g_warning ("failed to call %s on %s: %s",
					   function_name, plugin->name,
					   error_local->message);
				continue;
			}
		}
		if (plugin_app_func != NULL) {
			for (l = *list; l != NULL; l = l->next) {
				g_autoptr(GError) error_local = NULL;
				app = GS_APP (l->data);
				g_rw_lock_reader_lock (&plugin->rwlock);
				ret = plugin_app_func (plugin, app, flags,
						       cancellable, &error_local);
				g_rw_lock_reader_unlock (&plugin->rwlock);
				if (!ret) {
					g_warning ("failed to call %s on %s: %s",
						   function_name_app, plugin->name,
						   error_local->message);
					continue;
				}
			}
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* refine addons one layer deep */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS) > 0) {
		flags &= ~GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS;
		flags &= ~GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS;
		flags &= ~GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS;
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

	/* show a warning if nothing adopted this */
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_management_plugin (app) != NULL)
			continue;
		g_warning ("nothing adopted %s", gs_app_get_id (app));
		g_print ("%s", gs_app_to_string (app));
	}
out:
	/* now emit all the changed signals */
	for (l = freeze_list; l != NULL; l = l->next)
		g_object_thaw_notify (G_OBJECT (l->data));

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
	GList *list = NULL;
	GsPluginResultsFunc plugin_func = NULL;
	GsPlugin *plugin;
	gboolean exists;
	gboolean ret = TRUE;
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
		g_autoptr(GError) error_local = NULL;
		g_autoptr(AsProfileTask) ptask2 = NULL;

		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_cancellable_set_error_if_cancelled (cancellable, error);
		if (ret) {
			ret = FALSE;
			goto out;
		}

		/* get symbol */
		exists = g_module_symbol (plugin->module,
					  function_name,
					  (gpointer *) &plugin_func);
		if (!exists)
			continue;

		/* run function */
		ptask2 = as_profile_start (priv->profile,
					   "GsPlugin::%s(%s)",
					   plugin->name, function_name);
		g_rw_lock_reader_lock (&plugin->rwlock);
		ret = plugin_func (plugin, &list, cancellable, &error_local);
		g_rw_lock_reader_unlock (&plugin->rwlock);
		if (!ret) {
			g_warning ("failed to call %s on %s: %s",
				   function_name, plugin->name,
				   error_local->message);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

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
 * gs_plugin_loader_app_is_valid_installed:
 **/
static gboolean
gs_plugin_loader_app_is_valid_installed (GsApp *app, gpointer user_data)
{
	switch (gs_app_get_kind (app)) {
	case AS_APP_KIND_CODEC:
	case AS_APP_KIND_FONT:
		g_debug ("app invalid as %s: %s",
			 as_app_kind_to_string (gs_app_get_kind (app)),
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
		break;
	default:
		break;
	}
	return TRUE;
}

/**
 * gs_plugin_loader_app_is_valid:
 **/
static gboolean
gs_plugin_loader_app_is_valid (GsApp *app, gpointer user_data)
{
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) user_data;

	/* never show addons */
	if (gs_app_get_kind (app) == AS_APP_KIND_ADDON) {
		g_debug ("app invalid as addon %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unknown state */
	if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN) {
		g_debug ("app invalid as state unknown %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unconverted unavailables */
	if (gs_app_get_kind (app) == AS_APP_KIND_UNKNOWN &&
		gs_app_get_state (app) == AS_APP_STATE_UNAVAILABLE) {
		g_debug ("app invalid as unconverted unavailable %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show blacklisted apps */
	if (gs_app_has_category (app, "Blacklisted")) {
		g_debug ("app invalid as blacklisted %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show sources */
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE) {
		g_debug ("app invalid as source %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unknown kind */
	if (gs_app_get_kind (app) == AS_APP_KIND_UNKNOWN) {
		g_debug ("app invalid as kind unknown %s",
			 gs_plugin_loader_get_app_str (app));
		return FALSE;
	}

	/* don't show unconverted packages in the application view */
	if (((state->flags & GS_PLUGIN_REFINE_FLAGS_ALLOW_PACKAGES) == 0) &&
	    (gs_app_get_kind (app) == AS_APP_KIND_GENERIC)) {
//		g_debug ("app invalid as only a %s: %s",
//			 as_app_kind_to_string (gs_app_get_kind (app)),
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
	if (gs_app_get_kind (app) == AS_APP_KIND_DESKTOP &&
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
 * gs_plugin_loader_app_is_non_compulsory:
 **/
static gboolean
gs_plugin_loader_app_is_non_compulsory (GsApp *app, gpointer user_data)
{
	return !gs_app_has_quirk (app, AS_APP_QUIRK_COMPULSORY);
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
	if (gs_app_get_state (app) == AS_APP_STATE_UPDATABLE_LIVE)
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
 * gs_plugin_loader_run_action:
 **/
static gboolean
gs_plugin_loader_run_action (GsPluginLoader *plugin_loader,
			     GsApp *app,
			     const gchar *function_name,
			     GCancellable *cancellable,
			     GError **error)
{
	GsPluginActionFunc plugin_func = NULL;
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPlugin *plugin;
	gboolean anything_ran = FALSE;
	gboolean exists;
	gboolean ret;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		g_autoptr(GError) error_local = NULL;

		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		if (g_cancellable_set_error_if_cancelled (cancellable, error))
			return FALSE;
		exists = g_module_symbol (plugin->module,
					  function_name,
					  (gpointer *) &plugin_func);
		if (!exists)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  plugin->name,
					  function_name);
		g_rw_lock_reader_lock (&plugin->rwlock);
		ret = plugin_func (plugin, app, cancellable, &error_local);
		g_rw_lock_reader_unlock (&plugin->rwlock);
		if (!ret) {
			g_warning ("failed to call %s on %s: %s",
				   function_name, plugin->name,
				   error_local->message);
			continue;
		}
		anything_ran = TRUE;
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
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
	if (gs_app_get_kind (app) == AS_APP_KIND_GENERIC)
		return TRUE;
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE)
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
	gs_app_set_kind (app_os, AS_APP_KIND_OS_UPDATE);
	gs_app_set_state (app_os, AS_APP_STATE_UPDATABLE_LIVE);
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
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications, set as compulsory.
 *
 * Any #GsApp's of kind %AS_APP_KIND_GENERIC that remain after refining are
 * added to a new virtual #GsApp of kind %AS_APP_KIND_OS_UPDATE and all the
 * %AS_APP_KIND_GENERIC objects are moved to related packages of this object.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %AS_APP_KIND_DESKTOP or %AS_APP_KIND_OS_UPDATE.
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
 * gs_plugin_loader_get_distro_upgrades_thread_cb:
 **/
static void
gs_plugin_loader_get_distro_upgrades_thread_cb (GTask *task,
						gpointer object,
						gpointer task_data,
						GCancellable *cancellable)
{
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GError *error = NULL;

	state->list = gs_plugin_loader_run_results (plugin_loader,
						    "gs_plugin_add_distro_upgrades",
						    state->flags,
						    cancellable,
						    &error);
	if (error != NULL) {
		g_task_return_error (task, error);
		return;
	}

	/* filter package list */
	gs_plugin_list_filter_duplicates (&state->list);

	/* success */
	g_task_return_pointer (task, gs_plugin_list_copy (state->list), (GDestroyNotify) gs_plugin_list_free);
}

/**
 * gs_plugin_loader_get_distro_upgrades_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_distro_upgrades()
 * function. The plugins can returns #GsApp objects of kind %AS_APP_KIND_OS_UPGRADE.
 **/
void
gs_plugin_loader_get_distro_upgrades_async (GsPluginLoader *plugin_loader,
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
	g_task_run_in_thread (task, gs_plugin_loader_get_distro_upgrades_thread_cb);
}

/**
 * gs_plugin_loader_get_distro_upgrades_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_get_distro_upgrades_finish (GsPluginLoader *plugin_loader,
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
 * gs_plugin_loader_get_unvoted_reviews_thread_cb:
 **/
static void
gs_plugin_loader_get_unvoted_reviews_thread_cb (GTask *task,
						gpointer object,
						gpointer task_data,
						GCancellable *cancellable)
{
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GError *error = NULL;

	state->list = gs_plugin_loader_run_results (plugin_loader,
						    "gs_plugin_add_unvoted_reviews",
						    state->flags,
						    cancellable,
						    &error);
	if (error != NULL) {
		g_task_return_error (task, error);
		return;
	}

	/* filter package list */
	gs_plugin_list_filter_duplicates (&state->list);

	/* success */
	g_task_return_pointer (task, gs_plugin_list_copy (state->list), (GDestroyNotify) gs_plugin_list_free);
}

/**
 * gs_plugin_loader_get_unvoted_reviews_async:
 *
 * This method calls all plugins that implement the gs_plugin_add_unvoted_reviews()
 * function.
 **/
void
gs_plugin_loader_get_unvoted_reviews_async (GsPluginLoader *plugin_loader,
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
	g_task_run_in_thread (task, gs_plugin_loader_get_unvoted_reviews_thread_cb);
}

/**
 * gs_plugin_loader_get_unvoted_reviews_finish:
 *
 * Return value: (element-type GsApp) (transfer full): A list of applications
 **/
GList *
gs_plugin_loader_get_unvoted_reviews_finish (GsPluginLoader *plugin_loader,
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
 * function. The plugins return #GsApp objects of kind %AS_APP_KIND_SOURCE..
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
	gs_plugin_list_filter (&state->list, gs_plugin_loader_app_is_valid_installed, state);
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
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications, set as compulsory.
 *
 * Any #GsApp's of kind %AS_APP_KIND_GENERIC or %AS_APP_KIND_UNKNOWN that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %AS_APP_KIND_DESKTOP.
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
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GError *error = NULL;
	g_auto(GStrv) apps = NULL;

	/* debugging only */
	if (g_getenv ("GNOME_SOFTWARE_POPULAR"))
		apps = g_strsplit (g_getenv ("GNOME_SOFTWARE_POPULAR"), ",", 0);

	/* are we using a corporate build */
	if (apps == NULL)
		apps = g_settings_get_strv (priv->settings, "popular-overrides");
	if (apps != NULL && g_strv_length (apps) > 0) {
		guint i;
		for (i = 0; apps[i] != NULL; i++) {
			g_autoptr(GsApp) app = gs_app_new (apps[i]);
			gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
			gs_plugin_add_app (&state->list, app);
		}
	} else {
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
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications, set as compulsory.
 *
 * Any #GsApp's of kind %AS_APP_KIND_GENERIC that remain after refining are
 * automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %AS_APP_KIND_DESKTOP.
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
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_set_state (app, AS_APP_STATE_UNAVAILABLE);
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
		if (gs_app_get_kind (app) != AS_APP_KIND_GENERIC)
			continue;
		if (gs_app_get_state (app) != AS_APP_STATE_UNAVAILABLE)
			continue;
		if (gs_app_get_kind (app) != AS_APP_KIND_CODEC)
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
		g_autoptr(GError) error_local = NULL;
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
		g_rw_lock_reader_lock (&plugin->rwlock);
		ret = plugin_func (plugin, values, &state->list,
				   cancellable, &error_local);
		g_rw_lock_reader_unlock (&plugin->rwlock);
		if (!ret) {
			g_warning ("failed to call %s on %s: %s",
				   function_name, plugin->name,
				   error_local->message);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

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
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications, set as compulsory.
 *
 * Any #GsApp's of kind %AS_APP_KIND_GENERIC or %AS_APP_KIND_UNKNOWN that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %AS_APP_KIND_DESKTOP.
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
		g_autoptr(GError) error_local = NULL;
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
		g_rw_lock_reader_lock (&plugin->rwlock);
		ret = plugin_func (plugin, values, &state->list,
				   cancellable, &error_local);
		g_rw_lock_reader_unlock (&plugin->rwlock);
		if (!ret) {
			g_warning ("failed to call %s on %s: %s",
				   function_name, plugin->name,
				   error_local->message);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

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
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications, set as compulsory.
 *
 * Any #GsApp's of kind %AS_APP_KIND_GENERIC or %AS_APP_KIND_UNKNOWN that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %AS_APP_KIND_DESKTOP.
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
		g_autoptr(GError) error_local = NULL;
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
		g_rw_lock_reader_lock (&plugin->rwlock);
		ret = plugin_func (plugin, values, &state->list,
				   cancellable, &error_local);
		g_rw_lock_reader_unlock (&plugin->rwlock);
		if (!ret) {
			g_warning ("failed to call %s on %s: %s",
				   function_name, plugin->name,
				   error_local->message);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

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
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications, set as compulsory.
 *
 * Any #GsApp's of kind %AS_APP_KIND_GENERIC or %AS_APP_KIND_UNKNOWN that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %AS_APP_KIND_DESKTOP.
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
	return g_strcmp0 (gs_category_get_name (GS_CATEGORY ((gpointer) a)),
			  gs_category_get_name (GS_CATEGORY ((gpointer) b)));
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
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginResultsFunc plugin_func = NULL;
	GList *l;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		g_autoptr(GError) error_local = NULL;
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
		g_rw_lock_reader_lock (&plugin->rwlock);
		ret = plugin_func (plugin, &state->list,
				   cancellable, &error_local);
		g_rw_lock_reader_unlock (&plugin->rwlock);
		if (!ret) {
			g_warning ("failed to call %s on %s: %s",
				   function_name, plugin->name,
				   error_local->message);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* ensure they all have an 'All' category */
	for (l = state->list; l != NULL; l = l->next) {
		GsCategory *parent = GS_CATEGORY (l->data);
		if (g_strcmp0 (gs_category_get_id (parent), "Addons") == 0)
			continue;
		if (gs_category_find_child (parent, "all") == NULL) {
			g_autoptr(GsCategory) child = NULL;
			child = gs_category_new (parent, "all", NULL);
			gs_category_add_subcategory (parent, child);
			/* this is probably valid... */
			gs_category_set_size (child, gs_category_get_size (parent));
		}
		if (gs_category_find_child (parent, "featured") == NULL) {
			g_autoptr(GsCategory) child = NULL;
			child = gs_category_new (parent, "featured", NULL);
			gs_category_add_subcategory (parent, child);
			/* this is probably valid... */
			gs_category_set_size (child, 5);
		}
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
		g_autoptr(GError) error_local = NULL;
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
		g_rw_lock_reader_lock (&plugin->rwlock);
		ret = plugin_func (plugin, state->category, &state->list,
				   cancellable, &error_local);
		g_rw_lock_reader_unlock (&plugin->rwlock);
		if (!ret) {
			g_warning ("failed to call %s on %s: %s",
				   function_name, plugin->name,
				   error_local->message);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

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
	gs_plugin_list_filter (&state->list, gs_plugin_loader_app_is_non_compulsory, NULL);
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
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
 * or if they are core applications, set as compulsory.
 *
 * Any #GsApp's of kind %AS_APP_KIND_GENERIC or %AS_APP_KIND_UNKNOWN that remain
 * after refining are automatically removed.
 *
 * This means all of the #GsApp's returning from this function are of kind
 * %AS_APP_KIND_DESKTOP.
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

/**
 * gs_plugin_loader_review_action_thread_cb:
 **/
static void
gs_plugin_loader_review_action_thread_cb (GTask *task,
					  gpointer object,
					  gpointer task_data,
					  GCancellable *cancellable)
{
	GError *error = NULL;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	GsPlugin *plugin;
	GsPluginReviewFunc plugin_func = NULL;
	gboolean anything_ran = FALSE;
	gboolean exists;
	gboolean ret;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		g_autoptr(GError) error_local = NULL;

		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		if (g_cancellable_set_error_if_cancelled (cancellable, &error))
			g_task_return_error (task, error);

		exists = g_module_symbol (plugin->module,
					  state->function_name,
					  (gpointer *) &plugin_func);
		if (!exists)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  plugin->name,
					  state->function_name);
		g_rw_lock_reader_lock (&plugin->rwlock);
		ret = plugin_func (plugin, state->app, state->review,
				   cancellable, &error_local);
		g_rw_lock_reader_unlock (&plugin->rwlock);
		if (!ret) {
			g_warning ("failed to call %s on %s: %s",
				   state->function_name, plugin->name,
				   error_local->message);
			continue;
		}
		anything_ran = TRUE;
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* nothing ran */
	if (!anything_ran) {
		g_set_error (&error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no plugin could handle %s",
			     state->function_name);
		g_task_return_error (task, error);
	}

	/* add this to the app */
	if (g_strcmp0 (state->function_name, "gs_plugin_review_submit") == 0)
		gs_app_add_review (state->app, state->review);

	/* remove this from the app */
	if (g_strcmp0 (state->function_name, "gs_plugin_review_remove") == 0)
		gs_app_remove_review (state->app, state->review);

	g_task_return_boolean (task, TRUE);
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

	/* handle with a fake list */
	if (action == GS_PLUGIN_LOADER_ACTION_UPDATE) {
		g_autoptr(GsAppList) list = NULL;
		gs_plugin_add_app (&list, app);
		gs_plugin_loader_update_async (plugin_loader, list,
					       cancellable, callback,
					       user_data);
		return;
	}

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
	case GS_PLUGIN_LOADER_ACTION_UPGRADE_DOWNLOAD:
		state->function_name = "gs_plugin_app_upgrade_download";
		state->state_success = AS_APP_STATE_UPDATABLE;
		state->state_failure = AS_APP_STATE_AVAILABLE;
		break;
	case GS_PLUGIN_LOADER_ACTION_UPGRADE_TRIGGER:
		state->function_name = "gs_plugin_app_upgrade_trigger";
		state->state_success = AS_APP_STATE_UNKNOWN;
		state->state_failure = AS_APP_STATE_UNKNOWN;
		break;
	case GS_PLUGIN_LOADER_ACTION_LAUNCH:
		state->function_name = "gs_plugin_launch";
		state->state_success = AS_APP_STATE_UNKNOWN;
		state->state_failure = AS_APP_STATE_UNKNOWN;
		break;
	case GS_PLUGIN_LOADER_ACTION_UPDATE_CANCEL:
		state->function_name = "gs_plugin_update_cancel";
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
 * gs_plugin_loader_review_action_async:
 **/
void
gs_plugin_loader_review_action_async (GsPluginLoader *plugin_loader,
				      GsApp *app,
				      GsReview *review,
				      GsReviewAction action,
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
	state->review = g_object_ref (review);

	switch (action) {
	case GS_REVIEW_ACTION_SUBMIT:
		state->function_name = "gs_plugin_review_submit";
		break;
	case GS_REVIEW_ACTION_UPVOTE:
		state->function_name = "gs_plugin_review_upvote";
		break;
	case GS_REVIEW_ACTION_DOWNVOTE:
		state->function_name = "gs_plugin_review_downvote";
		break;
	case GS_REVIEW_ACTION_REPORT:
		state->function_name = "gs_plugin_review_report";
		break;
	case GS_REVIEW_ACTION_REMOVE:
		state->function_name = "gs_plugin_review_remove";
		break;
	case GS_REVIEW_ACTION_DISMISS:
		state->function_name = "gs_plugin_review_dismiss";
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	/* run in a thread */
	task = g_task_new (plugin_loader, cancellable, callback, user_data);
	g_task_set_task_data (task, state, (GDestroyNotify) gs_plugin_loader_free_async_state);
	g_task_set_return_on_cancel (task, TRUE);
	g_task_run_in_thread (task, gs_plugin_loader_review_action_thread_cb);
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
		g_rw_lock_reader_lock (&plugin->rwlock);
		plugin_func (plugin);
		g_rw_lock_reader_unlock (&plugin->rwlock);
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}
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
 * gs_plugin_loader_get_enabled:
 */
gboolean
gs_plugin_loader_get_enabled (GsPluginLoader *plugin_loader,
			      const gchar *plugin_name)
{
	GsPlugin *plugin;
	plugin = gs_plugin_loader_find_plugin (plugin_loader, plugin_name);
	if (plugin == NULL)
		return FALSE;
	return plugin->enabled;
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

	/* notify shells */
	g_debug ("updates-changed");
	g_signal_emit (plugin_loader, signals[SIGNAL_UPDATES_CHANGED], 0);
	priv->updates_changed_id = 0;

	g_object_unref (plugin_loader);
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
				       g_object_ref (plugin_loader));
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
	GsPluginGetDepsFunc order_after = NULL;
	GsPluginGetDepsFunc order_before = NULL;
	GsPluginGetDepsFunc plugin_conflicts = NULL;
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
	g_module_symbol (module,
			 "gs_plugin_order_after",
			 (gpointer *) &order_after);
	g_module_symbol (module,
			 "gs_plugin_order_before",
			 (gpointer *) &order_before);
	g_module_symbol (module,
			 "gs_plugin_get_conflicts",
			 (gpointer *) &plugin_conflicts);

	/* print what we know */
	plugin = g_slice_new0 (GsPlugin);
	plugin->enabled = TRUE;
	plugin->module = module;
	plugin->pixbuf_size = 64;
	plugin->priority = 0.f;
	plugin->order_after = order_after != NULL ? order_after (plugin) : NULL;
	plugin->order_before = order_before != NULL ? order_before (plugin) : NULL;
	plugin->conflicts = plugin_conflicts != NULL ? plugin_conflicts (plugin) : NULL;
	plugin->name = g_strdup (plugin_name ());
	plugin->locale = priv->locale;
	plugin->status_update_fn = gs_plugin_loader_status_update_cb;
	plugin->status_update_user_data = plugin_loader;
	plugin->updates_changed_fn = gs_plugin_loader_updates_changed_cb;
	plugin->updates_changed_user_data = plugin_loader;
	plugin->profile = g_object_ref (priv->profile);
	plugin->soup_session = g_object_ref (priv->soup_session);
	plugin->scale = gs_plugin_loader_get_scale (plugin_loader);
	g_debug ("opened plugin %s: %s", filename, plugin->name);

	/* rwlock */
	g_rw_lock_init (&plugin->rwlock);

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
 * gs_plugin_loader_setup:
 */
gboolean
gs_plugin_loader_setup (GsPluginLoader *plugin_loader,
			gchar **whitelist,
			GError **error)
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

	/* optional whitelist */
	if (whitelist != NULL) {
		for (i = 0; i < priv->plugins->len; i++) {
			plugin = g_ptr_array_index (priv->plugins, i);
			if (!plugin->enabled)
				continue;
			plugin->enabled = g_strv_contains ((const gchar * const *) whitelist,
							   plugin->name);
		}
	}

	/* order by deps */
	do {
		changes = FALSE;
		for (i = 0; i < priv->plugins->len; i++) {
			plugin = g_ptr_array_index (priv->plugins, i);
			if (plugin->order_after == NULL)
				continue;
			for (j = 0; plugin->order_after[j] != NULL && !changes; j++) {
				dep = gs_plugin_loader_find_plugin (plugin_loader,
								    plugin->order_after[j]);
				if (dep == NULL) {
					g_debug ("cannot find plugin '%s'",
						 plugin->order_after[j]);
					continue;
				}
				if (!dep->enabled)
					continue;
				if (plugin->priority <= dep->priority) {
					g_debug ("%s [%.1f] to be ordered after %s [%.1f] "
						 "so promoting to [%.1f]",
						 plugin->name, plugin->priority,
						 dep->name, dep->priority,
						 dep->priority + dep_increment);
					plugin->priority = dep->priority + dep_increment;
					changes = TRUE;
				}
			}
		}
		for (i = 0; i < priv->plugins->len; i++) {
			plugin = g_ptr_array_index (priv->plugins, i);
			if (plugin->order_before == NULL)
				continue;
			for (j = 0; plugin->order_before[j] != NULL && !changes; j++) {
				dep = gs_plugin_loader_find_plugin (plugin_loader,
								    plugin->order_before[j]);
				if (dep == NULL) {
					g_debug ("cannot find plugin '%s'",
						 plugin->order_before[j]);
					continue;
				}
				if (!dep->enabled)
					continue;
				if (plugin->priority <= dep->priority) {
					g_debug ("%s [%.1f] to be ordered before %s [%.1f] "
						 "so promoting to [%.1f]",
						 plugin->name, plugin->priority,
						 dep->name, dep->priority,
						 dep->priority + dep_increment);
					dep->priority = plugin->priority + dep_increment;
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

	/* check for conflicts */
	for (i = 0; i < priv->plugins->len; i++) {
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		if (plugin->conflicts == NULL)
			continue;
		for (j = 0; plugin->conflicts[j] != NULL && !changes; j++) {
			dep = gs_plugin_loader_find_plugin (plugin_loader,
							    plugin->conflicts[j]);
			if (dep == NULL)
				continue;
			if (!dep->enabled)
				continue;
			g_debug ("disabling %s as conflicts with %s",
				 dep->name, plugin->name);
			dep->enabled = FALSE;
		}
	}

	/* run setup */
	for (i = 0; i < priv->plugins->len; i++) {
		GsPluginSetupFunc plugin_func = NULL;
		const gchar *function_name = "gs_plugin_setup";
		gboolean ret;
		g_autoptr(AsProfileTask) ptask2 = NULL;
		g_autoptr(GError) error_local = NULL;

		/* run setup() if it exists */
		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &plugin_func);
		if (!ret)
			continue;
		ptask2 = as_profile_start (priv->profile,
					   "GsPlugin::%s(%s)",
					   plugin->name,
					   function_name);
		g_rw_lock_writer_lock (&plugin->rwlock);
		ret = plugin_func (plugin, NULL, &error_local);
		g_rw_lock_writer_unlock (&plugin->rwlock);
		if (!ret) {
			g_debug ("disabling %s as setup failed: %s",
				 plugin->name, error_local->message);
		}
	}

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
			 plugin->enabled ? "enabled" : "disabld",
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
	g_rw_lock_clear (&plugin->rwlock);
	g_object_unref (plugin->profile);
	g_object_unref (plugin->soup_session);
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
	g_clear_object (&priv->soup_session);
	g_clear_object (&priv->profile);
	g_clear_object (&priv->settings);
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
	g_free (priv->locale);

	g_mutex_clear (&priv->pending_apps_mutex);

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
	gchar *match;
	gchar **projects;
	guint i;

	priv->scale = 1;
	priv->plugins = g_ptr_array_new_with_free_func ((GDestroyNotify) gs_plugin_loader_plugin_free);
	priv->status_last = GS_PLUGIN_STATUS_LAST;
	priv->pending_apps = g_ptr_array_new_with_free_func ((GFreeFunc) g_object_unref);
	priv->profile = as_profile_new ();
	priv->settings = g_settings_new ("org.gnome.software");

	/* share a soup session (also disable the double-compression) */
	priv->soup_session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, gs_user_agent (),
							    NULL);
	soup_session_remove_feature_by_type (priv->soup_session,
					     SOUP_TYPE_CONTENT_DECODER);

	/* get the locale without the various UTF-8 suffixes */
	priv->locale = g_strdup (setlocale (LC_MESSAGES, NULL));
	match = g_strstr_len (priv->locale, -1, ".UTF-8");
	if (match != NULL)
		*match = '\0';
	match = g_strstr_len (priv->locale, -1, ".utf8");
	if (match != NULL)
		*match = '\0';

	g_mutex_init (&priv->pending_apps_mutex);

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
	GsPlugin *plugin;
	GsPluginRefreshFunc plugin_func = NULL;
	const gchar *function_name = "gs_plugin_refresh";
	gboolean anything_ran = FALSE;
	gboolean ret;
	guint i;
	gboolean exists;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(GError) error_local = NULL;
		g_autoptr(AsProfileTask) ptask = NULL;

		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		if (g_cancellable_set_error_if_cancelled (cancellable, error))
			return FALSE;

		exists = g_module_symbol (plugin->module,
					  function_name,
					  (gpointer *) &plugin_func);
		if (!exists)
			continue;
		ptask = as_profile_start (priv->profile,
					  "GsPlugin::%s(%s)",
					  plugin->name,
					  function_name);
		g_rw_lock_writer_lock (&plugin->rwlock);
		ret = plugin_func (plugin, cache_age, flags, cancellable, &error_local);
		g_rw_lock_writer_unlock (&plugin->rwlock);
		if (!ret) {
			g_warning ("failed to call %s on %s: %s",
				   function_name, plugin->name,
				   error_local->message);
			continue;
		}
		anything_ran = TRUE;
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* nothing ran */
	if (!anything_ran) {
		g_set_error (error,
			     GS_PLUGIN_LOADER_ERROR,
			     GS_PLUGIN_LOADER_ERROR_FAILED,
			     "no plugin could handle refresh");
		return FALSE;
	}
	return TRUE;
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
		g_autoptr(GError) error_local = NULL;
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
		g_rw_lock_reader_lock (&plugin->rwlock);
		ret = plugin_func (plugin, &state->list, state->filename,
				   cancellable, &error_local);
		g_rw_lock_reader_unlock (&plugin->rwlock);
		if (!ret) {
			g_warning ("failed to call %s on %s: %s",
				   function_name, plugin->name,
				   error_local->message);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

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
 * %AS_APP_KIND_DESKTOP for bonafide applications, or #GsApp's of kind
 * %AS_APP_KIND_GENERIC for packages that may or may not be applications.
 *
 * Once the list of updates is refined, some of the #GsApp's of kind
 * %AS_APP_KIND_GENERIC will have been promoted to a kind of %AS_APP_KIND_DESKTOP,
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
 * gs_plugin_loader_update_thread_cb:
 **/
static void
gs_plugin_loader_update_thread_cb (GTask *task,
				   gpointer object,
				   gpointer task_data,
				   GCancellable *cancellable)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (object);
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	const gchar *function_name = "gs_plugin_update";
	gboolean ret = TRUE;
	GsPluginLoaderAsyncState *state = (GsPluginLoaderAsyncState *) task_data;
	GsPlugin *plugin;
	GsPluginUpdateFunc plugin_func = NULL;
	GsPluginActionFunc plugin_app_func = NULL;
	guint i;

	/* run each plugin */
	for (i = 0; i < priv->plugins->len; i++) {
		g_autoptr(AsProfileTask) ptask = NULL;
		g_autoptr(GError) error_local = NULL;
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
		g_rw_lock_reader_lock (&plugin->rwlock);
		ret = plugin_func (plugin, state->list, cancellable, &error_local);
		g_rw_lock_reader_unlock (&plugin->rwlock);
		if (!ret) {
			g_warning ("failed to call %s on %s: %s",
				   function_name, plugin->name,
				   error_local->message);
			continue;
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	/* run each plugin, per-app version */
	function_name = "gs_plugin_update_app";
	for (i = 0; i < priv->plugins->len; i++) {
		GList *l;

		plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_task_return_error_if_cancelled (task);
		if (ret)
			return;
		ret = g_module_symbol (plugin->module,
		                       function_name,
		                       (gpointer *) &plugin_app_func);
		if (!ret)
			continue;

		/* for each app */
		for (l = state->list; l != NULL; l = l->next) {
			GsApp *app = GS_APP (l->data);
			g_autoptr(AsProfileTask) ptask = NULL;
			g_autoptr(GError) error_local = NULL;

			ptask = as_profile_start (priv->profile,
						  "GsPlugin::%s(%s){%s}",
						  plugin->name,
						  function_name,
						  gs_app_get_id (app));
			g_rw_lock_reader_lock (&plugin->rwlock);
			ret = plugin_app_func (plugin, app,
					       cancellable,
					       &error_local);
			g_rw_lock_reader_unlock (&plugin->rwlock);
			if (!ret) {
				g_warning ("failed to call %s on %s: %s",
					   function_name, plugin->name,
					   error_local->message);
				continue;
			}
		}
		gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_FINISHED);
	}

	g_task_return_boolean (task, TRUE);
}

/**
 * gs_plugin_loader_update_async:
 *
 * This method calls all plugins that implement the gs_plugin_update()
 * or gs_plugin_update_app() functions.
 **/
void
gs_plugin_loader_update_async (GsPluginLoader *plugin_loader,
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
	g_task_run_in_thread (task, gs_plugin_loader_update_thread_cb);
}

/**
 * gs_plugin_loader_update_finish:
 **/
gboolean
gs_plugin_loader_update_finish (GsPluginLoader *plugin_loader,
				GAsyncResult *res,
				GError **error)
{
	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), FALSE);
	g_return_val_if_fail (G_IS_TASK (res), FALSE);
	g_return_val_if_fail (g_task_is_valid (res, plugin_loader), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	return g_task_propagate_boolean (G_TASK (res), error);
}

/**
 * gs_plugin_loader_get_plugin_supported:
 *
 * This function returns TRUE if the symbol is found in any enabled plugin.
 */
gboolean
gs_plugin_loader_get_plugin_supported (GsPluginLoader *plugin_loader,
				       const gchar *function_name)
{
	GsPluginLoaderPrivate *priv = gs_plugin_loader_get_instance_private (plugin_loader);
	gboolean ret;
	gpointer dummy;
	guint i;

	for (i = 0; i < priv->plugins->len; i++) {
		GsPlugin *plugin = g_ptr_array_index (priv->plugins, i);
		if (!plugin->enabled)
			continue;
		ret = g_module_symbol (plugin->module,
				       function_name,
				       (gpointer *) &dummy);
		if (ret)
			return TRUE;
	}
	return FALSE;
}

/******************************************************************************/

/* vim: set noexpandtab: */
