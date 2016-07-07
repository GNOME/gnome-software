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

#include <config.h>

#include <gnome-software.h>

struct GsPluginData {
	GHashTable	*urls;		/* origin : url */
	GFileMonitor	*monitor;
	gchar		*reposdir;
	gboolean	 valid;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	/* for debugging and the self tests */
	priv->reposdir = g_strdup (g_getenv ("GS_SELF_TEST_REPOS_DIR"));
	if (priv->reposdir == NULL)
		priv->reposdir = g_strdup ("/etc/yum.repos.d");

	/* plugin only makes sense if this exists at startup */
	if (!g_file_test (priv->reposdir, G_FILE_TEST_EXISTS)) {
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}

	/* we also watch this for changes */
	priv->urls = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	/* need application IDs */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit-refine");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_free (priv->reposdir);
	if (priv->urls != NULL)
		g_hash_table_unref (priv->urls);
	if (priv->monitor != NULL)
		g_object_unref (priv->monitor);
}

static gboolean
gs_plugin_repos_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GDir) dir = NULL;
	const gchar *fn;

	/* already valid */
	if (priv->valid)
		return TRUE;

	/* clear existing */
	g_hash_table_remove_all (priv->urls);

	/* search all files */
	dir = g_dir_open (priv->reposdir, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((fn = g_dir_read_name (dir)) != NULL) {
		g_autofree gchar *filename = NULL;
		g_auto(GStrv) groups = NULL;
		g_autoptr(GKeyFile) kf = g_key_file_new ();
		guint i;

		/* not a repo */
		if (!g_str_has_suffix (fn, ".repo"))
			continue;

		/* load file */
		filename = g_build_filename (priv->reposdir, fn, NULL);
		if (!g_key_file_load_from_file (kf, filename,
						G_KEY_FILE_NONE,
						error))
			return FALSE;

		/* we can have multiple repos in one file */
		groups = g_key_file_get_groups (kf, NULL);
		for (i = 0; groups[i] != NULL; i++) {
			g_autofree gchar *tmp = NULL;
			tmp = g_key_file_get_string (kf, groups[i], "baseurl", NULL);
			if (tmp != NULL) {
				g_hash_table_insert (priv->urls,
						     g_strdup (groups[i]),
						     g_strdup (tmp));
				continue;
			}
			tmp = g_key_file_get_string (kf, groups[i], "metalink", NULL);
			if (tmp != NULL) {
				g_hash_table_insert (priv->urls,
						     g_strdup (groups[i]),
						     g_strdup (tmp));
				continue;
			}
		}
	}

	/* success */
	priv->valid = TRUE;
	return TRUE;
}

static void
gs_plugin_repos_changed_cb (GFileMonitor *monitor,
			    GFile *file,
			    GFile *other_file,
			    GFileMonitorEvent event_type,
			    GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	priv->valid = FALSE;
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GFile) file = g_file_new_for_path (priv->reposdir);

	/* watch for changes */
	priv->monitor = g_file_monitor_directory (file, G_FILE_MONITOR_NONE, cancellable, error);
	if (priv->monitor == NULL)
		return FALSE;
	g_signal_connect (priv->monitor, "changed",
			  G_CALLBACK (gs_plugin_repos_changed_cb), plugin);

	/* unconditionally at startup */
	return gs_plugin_repos_setup (plugin, cancellable, error);
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *tmp;

	/* not required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME) == 0)
		return TRUE;
	if (gs_app_get_origin_hostname (app) != NULL)
		return TRUE;

	/* ensure valid */
	if (!gs_plugin_repos_setup (plugin, cancellable, error))
		return FALSE;

	/* find hostname */
	if (gs_app_get_origin (app) == NULL)
		return TRUE;
	tmp = g_hash_table_lookup (priv->urls, gs_app_get_origin (app));
	if (tmp != NULL)
		gs_app_set_origin_hostname (app, tmp);

	return TRUE;
}
