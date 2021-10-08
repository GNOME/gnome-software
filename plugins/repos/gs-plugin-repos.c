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

struct _GsPluginRepos {
	GsPlugin	 parent;

	GHashTable	*fns;		/* origin : filename */
	GHashTable	*urls;		/* origin : url */
	GFileMonitor	*monitor;
	GMutex		 mutex;
	gchar		*reposdir;
	gboolean	 valid;
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

/* mutex must be held */
static gboolean
gs_plugin_repos_setup (GsPluginRepos  *self,
                       GCancellable   *cancellable,
                       GError        **error)
{
	g_autoptr(GDir) dir = NULL;
	const gchar *fn;

	/* already valid */
	if (self->valid)
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
	self->valid = TRUE;
	return TRUE;
}

static void
gs_plugin_repos_changed_cb (GFileMonitor      *monitor,
                            GFile             *file,
                            GFile             *other_file,
                            GFileMonitorEvent  event_type,
                            gpointer           user_data)
{
	GsPluginRepos *self = GS_PLUGIN_REPOS (user_data);

	self->valid = FALSE;
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginRepos *self = GS_PLUGIN_REPOS (plugin);
	g_autoptr(GFile) file = g_file_new_for_path (self->reposdir);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&self->mutex);

	/* watch for changes */
	self->monitor = g_file_monitor_directory (file, G_FILE_MONITOR_NONE, cancellable, error);
	if (self->monitor == NULL) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}
	g_signal_connect (self->monitor, "changed",
			  G_CALLBACK (gs_plugin_repos_changed_cb), self);

	/* unconditionally at startup */
	return gs_plugin_repos_setup (self, cancellable, error);
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
	if (!gs_plugin_repos_setup (self, cancellable, error))
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

	object_class->dispose = gs_plugin_repos_dispose;
	object_class->finalize = gs_plugin_repos_finalize;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_REPOS;
}
