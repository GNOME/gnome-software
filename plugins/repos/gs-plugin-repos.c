/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <gnome-software.h>

struct GsPluginData {
	GHashTable	*fns;		/* origin : filename */
	GHashTable	*urls;		/* origin : url */
	GFileMonitor	*monitor;
	GMutex		 mutex;
	gchar		*reposdir;
	gboolean	 valid;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	g_mutex_init (&priv->mutex);

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
	priv->fns = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	priv->urls = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	/* need application IDs */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit-refine");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "rpm-ostree");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_free (priv->reposdir);
	if (priv->fns != NULL)
		g_hash_table_unref (priv->fns);
	if (priv->urls != NULL)
		g_hash_table_unref (priv->urls);
	if (priv->monitor != NULL)
		g_object_unref (priv->monitor);
	g_mutex_clear (&priv->mutex);
}

/* mutex must be held */
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
	g_hash_table_remove_all (priv->fns);
	g_hash_table_remove_all (priv->urls);

	/* search all files */
	dir = g_dir_open (priv->reposdir, 0, error);
	if (dir == NULL) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}
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
						error)) {
			gs_utils_error_convert_gio (error);
			return FALSE;
		}

		/* we can have multiple repos in one file */
		groups = g_key_file_get_groups (kf, NULL);
		for (i = 0; groups[i] != NULL; i++) {
			g_autofree gchar *tmp = NULL;

			g_hash_table_insert (priv->fns,
			                     g_strdup (groups[i]),
			                     g_strdup (filename));

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
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);

	/* watch for changes */
	priv->monitor = g_file_monitor_directory (file, G_FILE_MONITOR_NONE, cancellable, error);
	if (priv->monitor == NULL) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}
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
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);

	/* not required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME) == 0)
		return TRUE;
	if (gs_app_get_origin_hostname (app) != NULL)
		return TRUE;

	/* ensure valid */
	if (!gs_plugin_repos_setup (plugin, cancellable, error))
		return FALSE;

	/* find hostname */
	switch (gs_app_get_kind (app)) {
	case AS_APP_KIND_SOURCE:
		if (gs_app_get_id (app) == NULL)
			return TRUE;
		tmp = g_hash_table_lookup (priv->urls, gs_app_get_id (app));
		if (tmp != NULL)
			gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, tmp);
		break;
	default:
		if (gs_app_get_origin (app) == NULL)
			return TRUE;
		tmp = g_hash_table_lookup (priv->urls, gs_app_get_origin (app));
		if (tmp != NULL)
			gs_app_set_origin_hostname (app, tmp);
		break;
	}

	/* find filename */
	switch (gs_app_get_kind (app)) {
	case AS_APP_KIND_SOURCE:
		if (gs_app_get_id (app) == NULL)
			return TRUE;
		tmp = g_hash_table_lookup (priv->fns, gs_app_get_id (app));
		if (tmp != NULL)
			gs_app_set_metadata (app, "repos::repo-filename", tmp);
		break;
	default:
		break;
	}

	return TRUE;
}
