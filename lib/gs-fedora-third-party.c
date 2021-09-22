/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <glib.h>
#include <gio/gio.h>

#include "gs-fedora-third-party.h"

struct _GsFedoraThirdParty
{
	GObject		 parent_instance;
	GMutex		 lock;
	gchar		*executable;
	GHashTable	*repos; /* gchar *name ~> gchar *packaging format */
	gint64		 last_update;
};

G_DEFINE_TYPE (GsFedoraThirdParty, gs_fedora_third_party, G_TYPE_OBJECT)

static GObject *
gs_fedora_third_party_constructor (GType type,
				   guint n_construct_properties,
				   GObjectConstructParam *construct_properties)
{
	static GWeakRef singleton;
	GObject *result;

	result = g_weak_ref_get (&singleton);
	if (result == NULL) {
		result = G_OBJECT_CLASS (gs_fedora_third_party_parent_class)->constructor (type, n_construct_properties, construct_properties);

		if (result)
			g_weak_ref_set (&singleton, result);
	}

	return result;
}

static void
gs_fedora_third_party_finalize (GObject *object)
{
	GsFedoraThirdParty *self = GS_FEDORA_THIRD_PARTY (object);

	g_clear_pointer (&self->executable, g_free);
	g_clear_pointer (&self->repos, g_hash_table_unref);
	g_mutex_clear (&self->lock);

	/* Chain up to parent's method. */
	G_OBJECT_CLASS (gs_fedora_third_party_parent_class)->finalize (object);
}

static void
gs_fedora_third_party_class_init (GsFedoraThirdPartyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->constructor = gs_fedora_third_party_constructor;
	object_class->finalize = gs_fedora_third_party_finalize;
}

static void
gs_fedora_third_party_init (GsFedoraThirdParty *self)
{
	g_mutex_init (&self->lock);
}

GsFedoraThirdParty *
gs_fedora_third_party_new (void)
{
	return g_object_new (GS_TYPE_FEDORA_THIRD_PARTY, NULL);
}

static gboolean
gs_fedora_third_party_ensure_executable_locked (GsFedoraThirdParty *self,
						GError **error)
{
	if (self->executable == NULL)
		self->executable = g_find_program_in_path ("fedora-third-party");

	if (self->executable == NULL) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "File 'fedora-third-party' not found");
		return FALSE;
	}

	return TRUE;
}

gboolean
gs_fedora_third_party_is_available (GsFedoraThirdParty *self)
{
	gboolean res;

	g_return_val_if_fail (GS_IS_FEDORA_THIRD_PARTY (self), FALSE);

	g_mutex_lock (&self->lock);
	res = gs_fedora_third_party_ensure_executable_locked (self, NULL);
	g_mutex_unlock (&self->lock);

	return res;
}

void
gs_fedora_third_party_invalidate (GsFedoraThirdParty *self)
{
	g_return_if_fail (GS_IS_FEDORA_THIRD_PARTY (self));

	g_mutex_lock (&self->lock);
	g_clear_pointer (&self->executable, g_free);
	g_clear_pointer (&self->repos, g_hash_table_unref);
	self->last_update = 0;
	g_mutex_unlock (&self->lock);
}

typedef struct _AsyncData
{
	gboolean enable;
	gboolean config_only;
} AsyncData;

static AsyncData *
async_data_new (gboolean enable,
		gboolean config_only)
{
	AsyncData *async_data = g_slice_new0 (AsyncData);
	async_data->enable = enable;
	async_data->config_only = config_only;
	return async_data;
}

static void
async_data_free (gpointer ptr)
{
	AsyncData *async_data = ptr;
	if (async_data != NULL)
		g_slice_free (AsyncData, async_data);
}

static void
gs_fedora_third_party_query_thread (GTask *task,
				    gpointer source_object,
				    gpointer task_data,
				    GCancellable *cancellable)
{
	g_autoptr(GError) error = NULL;
	GsFedoraThirdPartyState state;
	if (gs_fedora_third_party_query_sync (GS_FEDORA_THIRD_PARTY (source_object), &state, cancellable, &error))
		g_task_return_int (task, state);
	else
		g_task_return_error (task, g_steal_pointer (&error));
}

void
gs_fedora_third_party_query (GsFedoraThirdParty *self,
			     GCancellable *cancellable,
			     GAsyncReadyCallback callback,
			     gpointer user_data)
{
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_FEDORA_THIRD_PARTY (self));

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_fedora_third_party_query);
	g_task_run_in_thread (task, gs_fedora_third_party_query_thread);
}

gboolean
gs_fedora_third_party_query_finish (GsFedoraThirdParty *self,
				    GAsyncResult *result,
				    GsFedoraThirdPartyState *out_state,
				    GError **error)
{
	GError *local_error = NULL;
	GsFedoraThirdPartyState state = GS_FEDORA_THIRD_PARTY_STATE_UNKNOWN;

	g_return_val_if_fail (GS_IS_FEDORA_THIRD_PARTY (self), FALSE);

	state = g_task_propagate_int (G_TASK (result), &local_error);
	if (local_error) {
		g_propagate_error (error, local_error);
		return FALSE;
	}

	if (out_state)
		*out_state = state;

	return TRUE;
}

gboolean
gs_fedora_third_party_query_sync (GsFedoraThirdParty *self,
				  GsFedoraThirdPartyState *out_state,
				  GCancellable *cancellable,
				  GError **error)
{
	const gchar *args[] = {
		"", /* executable */
		"query",
		"--quiet",
		NULL
	};
	gboolean success = FALSE;

	g_return_val_if_fail (GS_IS_FEDORA_THIRD_PARTY (self), FALSE);

	g_mutex_lock (&self->lock);
	if (gs_fedora_third_party_ensure_executable_locked (self, error)) {
		gint exit_status = -1;
		args[0] = self->executable;
		success = g_spawn_sync (NULL, (gchar **) args, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &exit_status, error);
		if (success) {
			GsFedoraThirdPartyState state = GS_FEDORA_THIRD_PARTY_STATE_UNKNOWN;
			/* See https://pagure.io/fedora-third-party/blob/main/f/doc/fedora-third-party.1.md */
			switch (WEXITSTATUS (exit_status)) {
			case 0:
				state = GS_FEDORA_THIRD_PARTY_STATE_ENABLED;
				break;
			case 1:
				state = GS_FEDORA_THIRD_PARTY_STATE_DISABLED;
				break;
			case 2:
				state = GS_FEDORA_THIRD_PARTY_STATE_ASK;
				break;
			default:
				break;
			}
			if (out_state)
				*out_state = state;
		}
	}
	g_mutex_unlock (&self->lock);

	return success;
}

static void
gs_fedora_third_party_switch_thread (GTask *task,
				     gpointer source_object,
				     gpointer task_data,
				     GCancellable *cancellable)
{
	g_autoptr(GError) error = NULL;
	AsyncData *async_data = task_data;
	if (gs_fedora_third_party_switch_sync (GS_FEDORA_THIRD_PARTY (source_object), async_data->enable, async_data->config_only, cancellable, &error))
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, g_steal_pointer (&error));
}

void
gs_fedora_third_party_switch (GsFedoraThirdParty *self,
			      gboolean enable,
			      gboolean config_only,
			      GCancellable *cancellable,
			      GAsyncReadyCallback callback,
			      gpointer user_data)
{
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_FEDORA_THIRD_PARTY (self));

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_fedora_third_party_switch);
	g_task_set_task_data (task, async_data_new (enable, config_only), async_data_free);
	g_task_run_in_thread (task, gs_fedora_third_party_switch_thread);
}

gboolean
gs_fedora_third_party_switch_finish (GsFedoraThirdParty *self,
				     GAsyncResult *result,
				     GError **error)
{
	g_return_val_if_fail (GS_IS_FEDORA_THIRD_PARTY (self), FALSE);
	return g_task_propagate_boolean (G_TASK (result), error);
}

gboolean
gs_fedora_third_party_switch_sync (GsFedoraThirdParty *self,
				   gboolean enable,
				   gboolean config_only,
				   GCancellable *cancellable,
				   GError **error)
{
	const gchar *args[] = {
		"pkexec",
		"", /* executable */
		"", /* command */
		"", /* config-only */
		NULL
	};
	gboolean success = FALSE;

	g_return_val_if_fail (GS_IS_FEDORA_THIRD_PARTY (self), FALSE);

	g_mutex_lock (&self->lock);
	if (gs_fedora_third_party_ensure_executable_locked (self, error)) {
		args[1] = self->executable;
		args[2] = enable ? "enable" : "disable";
		args[3] = config_only ? "--config-only" : NULL;
		success = g_spawn_sync (NULL, (gchar **) args, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, error);
	}
	g_mutex_unlock (&self->lock);

	return success;
}

static void
gs_fedora_third_party_opt_out_thread (GTask *task,
				      gpointer source_object,
				      gpointer task_data,
				      GCancellable *cancellable)
{
	g_autoptr(GError) error = NULL;
	if (gs_fedora_third_party_opt_out_sync (GS_FEDORA_THIRD_PARTY (source_object), cancellable, &error))
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, g_steal_pointer (&error));
}

void
gs_fedora_third_party_opt_out (GsFedoraThirdParty *self,
			       GCancellable *cancellable,
			       GAsyncReadyCallback callback,
			       gpointer user_data)
{
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_FEDORA_THIRD_PARTY (self));

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_fedora_third_party_opt_out);
	g_task_run_in_thread (task, gs_fedora_third_party_opt_out_thread);
}

gboolean
gs_fedora_third_party_opt_out_finish (GsFedoraThirdParty *self,
				      GAsyncResult *result,
				      GError **error)
{
	g_return_val_if_fail (GS_IS_FEDORA_THIRD_PARTY (self), FALSE);
	return g_task_propagate_boolean (G_TASK (result), error);
}

gboolean
gs_fedora_third_party_opt_out_sync (GsFedoraThirdParty *self,
				    GCancellable *cancellable,
				    GError **error)
{
	/* fedora-third-party-opt-out is a single-purpose script that changes
	 * the third-party status from unset => disabled. It exists to allow
	 * a different pkexec configuration for opting-out and thus avoid
	 * admin users needing to authenticate to opt-out.
	 */
	const gchar *args[] = {
		"pkexec",
		"/usr/lib/fedora-third-party/fedora-third-party-opt-out",
		NULL
	};
	gboolean success = FALSE;

	g_return_val_if_fail (GS_IS_FEDORA_THIRD_PARTY (self), FALSE);

	g_mutex_lock (&self->lock);
	if (gs_fedora_third_party_ensure_executable_locked (self, error)) {
		success = g_spawn_sync (NULL, (gchar **) args, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, error);
	}
	g_mutex_unlock (&self->lock);

	return success;
}

static void
gs_fedora_third_party_list_thread (GTask *task,
				   gpointer source_object,
				   gpointer task_data,
				   GCancellable *cancellable)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GHashTable) repos = NULL;
	if (gs_fedora_third_party_list_sync (GS_FEDORA_THIRD_PARTY (source_object), &repos, cancellable, &error))
		g_task_return_pointer (task, g_steal_pointer (&repos), (GDestroyNotify) g_hash_table_unref);
	else
		g_task_return_error (task, g_steal_pointer (&error));
}

void
gs_fedora_third_party_list (GsFedoraThirdParty *self,
			    GCancellable *cancellable,
			    GAsyncReadyCallback callback,
			    gpointer user_data)
{
	g_autoptr(GTask) task = NULL;

	g_return_if_fail (GS_IS_FEDORA_THIRD_PARTY (self));

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_fedora_third_party_list);
	g_task_run_in_thread (task, gs_fedora_third_party_list_thread);
}

gboolean
gs_fedora_third_party_list_finish (GsFedoraThirdParty *self,
				   GAsyncResult *result,
				   GHashTable **out_repos, /* gchar *name ~> gchar *management_plugin */
				   GError **error)
{
	g_autoptr(GHashTable) repos = NULL;
	g_return_val_if_fail (GS_IS_FEDORA_THIRD_PARTY (self), FALSE);
	repos = g_task_propagate_pointer (G_TASK (result), error);
	if (repos == NULL)
		return FALSE;
	if (out_repos)
		*out_repos = g_steal_pointer (&repos);
	return TRUE;
}

gboolean
gs_fedora_third_party_list_sync (GsFedoraThirdParty *self,
				 GHashTable **out_repos, /* gchar *name ~> gchar *management_plugin */
				 GCancellable *cancellable,
				 GError **error)
{
	const gchar *args[] = {
		"", /* executable */
		"list",
		"--csv",
		"--columns=type,name",
		NULL
	};
	gboolean success = FALSE;

	g_return_val_if_fail (GS_IS_FEDORA_THIRD_PARTY (self), FALSE);

	g_mutex_lock (&self->lock);
	/* Auto-recheck only twice a day */
	if (self->repos == NULL || (g_get_real_time () / G_USEC_PER_SEC) - self->last_update > 12 * 60 * 60) {
		g_clear_pointer (&self->repos, g_hash_table_unref);
		if (gs_fedora_third_party_ensure_executable_locked (self, error)) {
			g_autofree gchar *stdoutput = NULL;
			args[0] = self->executable;
			if (g_spawn_sync (NULL, (gchar **) args, NULL, G_SPAWN_DEFAULT, NULL, NULL, &stdoutput, NULL, NULL, error)) {
				GHashTable *repos = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
				g_auto(GStrv) lines = NULL;

				lines = g_strsplit (stdoutput != NULL ? stdoutput : "", "\n", -1);

				for (gsize ii = 0; lines != NULL && lines[ii]; ii++) {
					g_auto(GStrv) tokens = g_strsplit (lines[ii], ",", 2);
					if (tokens != NULL && tokens[0] != NULL && tokens[1] != NULL) {
						const gchar *repo_type = tokens[0];
						/* The 'dnf' means 'packagekit' here */
						if (g_str_equal (repo_type, "dnf"))
							repo_type = "packagekit";
						/* Hash them by name, which cannot clash between types */
						g_hash_table_insert (repos, g_strdup (tokens[1]), g_strdup (repo_type));
					}
				}

				self->repos = repos;
			}
		}
		self->last_update = g_get_real_time () / G_USEC_PER_SEC;
	}
	success = self->repos != NULL && g_hash_table_size (self->repos) != 0;
	if (success && out_repos)
		*out_repos = g_hash_table_ref (self->repos);
	g_mutex_unlock (&self->lock);

	return success;
}

gboolean
gs_fedora_third_party_util_is_third_party_repo (GHashTable *third_party_repos,
						const gchar *origin,
						const gchar *management_plugin)
{
	const gchar *expected_management_plugin;

	if (third_party_repos == NULL || origin == NULL)
		return FALSE;

	expected_management_plugin = g_hash_table_lookup (third_party_repos, origin);
	if (expected_management_plugin == NULL)
		return FALSE;

	return g_strcmp0 (management_plugin, expected_management_plugin) == 0;
}
