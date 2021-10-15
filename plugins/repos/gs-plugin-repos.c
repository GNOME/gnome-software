/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <gnome-software.h>

#include "gs-plugin-repos.h"

/*
 * SECTION:
 * Plugin to set URLs and origin hostnames on repos and apps using data from
 * `/etc/yum.repos.d`
 *
 * This plugin is only useful on distributions which use `/etc/yum.repos.d`.
 *
 * It enumerates `/etc/yum.repos.d` in a worker thread and updates its internal
 * hash tables and state from that worker thread (while holding a lock).
 *
 * Other tasks on the plugin access the data synchronously, not using a worker
 * thread. Data accesses should be fast.
 */

struct _GsPluginRepos {
	GsPlugin	 parent;

	GHashTable	*fns;		/* origin : filename */
	GHashTable	*urls;		/* origin : url */
	GFileMonitor	*monitor;
	GMutex		 mutex;
	gchar		*reposdir;
	gboolean	 valid;		/* (atomic) */
};

G_DEFINE_TYPE (GsPluginRepos, gs_plugin_repos, GS_TYPE_PLUGIN)

static void
gs_plugin_repos_init (GsPluginRepos *self)
{
	GsPlugin *plugin = GS_PLUGIN (self);

	g_mutex_init (&self->mutex);

	/* for debugging and the self tests */
	self->reposdir = g_strdup (g_getenv ("GS_SELF_TEST_REPOS_DIR"));
	if (self->reposdir == NULL)
		self->reposdir = g_strdup ("/etc/yum.repos.d");

	/* plugin only makes sense if this exists at startup */
	if (!g_file_test (self->reposdir, G_FILE_TEST_EXISTS)) {
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}

	/* we also watch this for changes */
	self->fns = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
	self->urls = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);

	/* need application IDs */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "rpm-ostree");
}

static void
gs_plugin_repos_dispose (GObject *object)
{
	GsPluginRepos *self = GS_PLUGIN_REPOS (object);

	g_clear_pointer (&self->reposdir, g_free);
	g_clear_pointer (&self->fns, g_hash_table_unref);
	g_clear_pointer (&self->urls, g_hash_table_unref);
	g_clear_object (&self->monitor);

	G_OBJECT_CLASS (gs_plugin_repos_parent_class)->dispose (object);
}

static void
gs_plugin_repos_finalize (GObject *object)
{
	GsPluginRepos *self = GS_PLUGIN_REPOS (object);

	g_mutex_clear (&self->mutex);

	G_OBJECT_CLASS (gs_plugin_repos_parent_class)->finalize (object);
}

/* Run in a worker thread; mutex must be held */
static gboolean
gs_plugin_repos_ensure_valid_locked (GsPluginRepos  *self,
                                     GCancellable   *cancellable,
                                     GError        **error)
{
	g_autoptr(GDir) dir = NULL;
	const gchar *fn;

	/* already valid */
	if (g_atomic_int_get (&self->valid))
		return TRUE;

	/* clear existing */
	g_hash_table_remove_all (self->fns);
	g_hash_table_remove_all (self->urls);

	/* search all files */
	dir = g_dir_open (self->reposdir, 0, error);
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
		filename = g_build_filename (self->reposdir, fn, NULL);
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

			g_hash_table_insert (self->fns,
			                     g_strdup (groups[i]),
			                     g_strdup (filename));

			tmp = g_key_file_get_string (kf, groups[i], "baseurl", NULL);
			if (tmp != NULL) {
				g_hash_table_insert (self->urls,
						     g_strdup (groups[i]),
						     g_strdup (tmp));
				continue;
			}

			tmp = g_key_file_get_string (kf, groups[i], "metalink", NULL);
			if (tmp != NULL) {
				g_hash_table_insert (self->urls,
						     g_strdup (groups[i]),
						     g_strdup (tmp));
				continue;
			}
		}
	}

	/* success */
	g_atomic_int_set (&self->valid, TRUE);

	return TRUE;
}

/* Run in the main thread. */
static void
gs_plugin_repos_changed_cb (GFileMonitor      *monitor,
                            GFile             *file,
                            GFile             *other_file,
                            GFileMonitorEvent  event_type,
                            gpointer           user_data)
{
	GsPluginRepos *self = GS_PLUGIN_REPOS (user_data);

	g_atomic_int_set (&self->valid, FALSE);
}

/* Run in a worker thread. */
static void
setup_thread_cb (GTask        *task,
                 gpointer      source_object,
                 gpointer      task_data,
                 GCancellable *cancellable)
{
	GsPluginRepos *self = GS_PLUGIN_REPOS (source_object);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&self->mutex);
	g_autoptr(GError) local_error = NULL;

	if (!gs_plugin_repos_ensure_valid_locked (self, cancellable, &local_error))
		g_task_return_error (task, g_steal_pointer (&local_error));
	else
		g_task_return_boolean (task, TRUE);
}

static void
gs_plugin_repos_setup_async (GsPlugin            *plugin,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
	GsPluginRepos *self = GS_PLUGIN_REPOS (plugin);
	g_autoptr(GFile) file = g_file_new_for_path (self->reposdir);
	g_autoptr(GTask) task = NULL;
	g_autoptr(GError) local_error = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_repos_setup_async);

	/* watch for changes in the main thread */
	self->monitor = g_file_monitor_directory (file, G_FILE_MONITOR_NONE, cancellable, &local_error);
	if (self->monitor == NULL) {
		gs_utils_error_convert_gio (&local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_signal_connect (self->monitor, "changed",
			  G_CALLBACK (gs_plugin_repos_changed_cb), self);

	/* Set up the repos at startup. */
	g_task_run_in_thread (task, setup_thread_cb);
}

static gboolean
gs_plugin_repos_setup_finish (GsPlugin      *plugin,
                              GAsyncResult  *result,
                              GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static gboolean
refine_app_locked (GsPluginRepos        *self,
		   GsApp                *app,
		   GsPluginRefineFlags   flags,
		   GCancellable         *cancellable,
		   GError              **error)
{
	const gchar *tmp;

	/* not required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME) == 0)
		return TRUE;
	if (gs_app_get_origin_hostname (app) != NULL)
		return TRUE;

	/* make sure we don't end up refining flatpak repos */
	if (gs_app_get_bundle_kind (app) != AS_BUNDLE_KIND_PACKAGE)
		return TRUE;

	/* ensure valid */
	if (!gs_plugin_repos_ensure_valid_locked (self, cancellable, error))
		return FALSE;

	/* find hostname */
	switch (gs_app_get_kind (app)) {
	case AS_COMPONENT_KIND_REPOSITORY:
		if (gs_app_get_id (app) == NULL)
			return TRUE;
		tmp = g_hash_table_lookup (self->urls, gs_app_get_id (app));
		if (tmp != NULL)
			gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, tmp);
		break;
	default:
		if (gs_app_get_origin (app) == NULL)
			return TRUE;
		tmp = g_hash_table_lookup (self->urls, gs_app_get_origin (app));
		if (tmp != NULL)
			gs_app_set_origin_hostname (app, tmp);
		else {
			GHashTableIter iter;
			gpointer key, value;
			const gchar *origin;

			origin = gs_app_get_origin (app);

			/* Some repos, such as rpmfusion, can have set the name with a distribution
			   number in the appstream file, thus check those specifically */
			g_hash_table_iter_init (&iter, self->urls);
			while (g_hash_table_iter_next (&iter, &key, &value)) {
				if (g_str_has_prefix (origin, key)) {
					const gchar *rest = origin + strlen (key);
					while (*rest == '-' || (*rest >= '0' && *rest <= '9'))
						rest++;
					if (!*rest) {
						gs_app_set_origin_hostname (app, value);
						break;
					}
				}
			}
		}
		break;
	}

	/* find filename */
	switch (gs_app_get_kind (app)) {
	case AS_COMPONENT_KIND_REPOSITORY:
		if (gs_app_get_id (app) == NULL)
			return TRUE;
		tmp = g_hash_table_lookup (self->fns, gs_app_get_id (app));
		if (tmp != NULL)
			gs_app_set_metadata (app, "repos::repo-filename", tmp);
		break;
	default:
		break;
	}

	return TRUE;
}

gboolean
gs_plugin_refine (GsPlugin             *plugin,
		  GsAppList            *list,
		  GsPluginRefineFlags   flags,
		  GCancellable         *cancellable,
		  GError              **error)
{
	GsPluginRepos *self = GS_PLUGIN_REPOS (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&self->mutex);

	/* nothing to do here */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN_HOSTNAME) == 0)
		return TRUE;

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (!refine_app_locked (self, app, flags, cancellable, error))
			return FALSE;
	}

	return TRUE;
}

static void
gs_plugin_repos_class_init (GsPluginReposClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_repos_dispose;
	object_class->finalize = gs_plugin_repos_finalize;

	plugin_class->setup_async = gs_plugin_repos_setup_async;
	plugin_class->setup_finish = gs_plugin_repos_setup_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_REPOS;
}
