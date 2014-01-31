/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

#include <string.h>

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>

#include <gs-plugin.h>

struct GsPluginPrivate {
	PkDesktop	*desktop;
	gsize		 loaded;
	GMutex		 plugin_mutex;
	GHashTable	*cache;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "desktopdb";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->desktop = pk_desktop_new ();
	plugin->priv->cache = g_hash_table_new_full (g_str_hash,
						     g_str_equal,
						     g_free,
						     g_free);
	g_mutex_init (&plugin->priv->plugin_mutex);
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_object_unref (plugin->priv->desktop);
	g_hash_table_unref (plugin->priv->cache);
	g_mutex_clear (&plugin->priv->plugin_mutex);
}

/**
 * gs_plugin_desktopdb_set_metadata:
 */
static void
gs_plugin_desktopdb_set_metadata (GsPlugin *plugin,
				  GsApp *app,
				  const gchar *pkg_name)
{
	gchar *desktop_file;
	gchar *id = NULL;
	GError *error = NULL;
	GPtrArray *files = NULL;

	/* is in cache */
	g_mutex_lock (&plugin->priv->plugin_mutex);
	desktop_file = g_hash_table_lookup (plugin->priv->cache, pkg_name);
	desktop_file = g_strdup (desktop_file);
	g_mutex_unlock (&plugin->priv->plugin_mutex);

	if (desktop_file == NULL) {
		/* try to get the list of desktop files for this package */
		files = pk_desktop_get_shown_for_package (plugin->priv->desktop,
							  pkg_name,
							  &error);
		if (files == NULL) {
			g_warning ("failed to get files for %s: %s",
				   pkg_name, error->message);
			g_error_free (error);
			goto out;
		}
		if (files->len == 0) {
			g_debug ("no desktopdb entry for %s", pkg_name);
			goto out;
		}

		/* add just the first desktop file */
		desktop_file = g_strdup (g_ptr_array_index (files, 0));

		/* add to the cache */
		g_mutex_lock (&plugin->priv->plugin_mutex);
		g_hash_table_insert (plugin->priv->cache,
				     g_strdup (pkg_name),
				     g_strdup (desktop_file));
		g_mutex_unlock (&plugin->priv->plugin_mutex);

	}

	/* also set the ID if it's missing */
	if (gs_app_get_id (app) == NULL) {
		id = g_path_get_basename (desktop_file);
		gs_app_set_id (app, id);
	}

	gs_app_set_metadata (app,
			     "DataDir::desktop-filename",
			     desktop_file);
	g_free (desktop_file);

out:
	g_free (id);
	if (files != NULL)
		g_ptr_array_unref (files);
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList *list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	GList *l;
	GPtrArray *sources;
	GsApp *app;
	const gchar *pkgname;
	gboolean ret = TRUE;
	guint i;

	/* not loaded yet */
	if (g_once_init_enter (&plugin->priv->loaded)) {
		ret = pk_desktop_open_database (plugin->priv->desktop, error);
		g_once_init_leave (&plugin->priv->loaded, TRUE);

		if (!ret)
			goto out;
	}

	/* can we convert a package to an application */
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_metadata_item (app, "DataDir::desktop-filename") != NULL)
			continue;
		sources = gs_app_get_sources (app);
		for (i = 0; i < sources->len; i++) {
			pkgname = g_ptr_array_index (sources, i);
			gs_plugin_desktopdb_set_metadata (plugin, app, pkgname);
		}
	}
out:
	return ret;
}
