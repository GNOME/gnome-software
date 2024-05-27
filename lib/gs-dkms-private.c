/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <gio/gio.h>

#include "gs-dkms-private.h"

static void
gs_dkms_execute_communicated_cb (GObject *source_object,
				 GAsyncResult *result,
				 gpointer user_data)
{
	g_autoptr(GTask) task = user_data;
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GError) result_error = NULL;
	g_autofree gchar *val_stdout = NULL;
	g_autofree gchar *val_stderr = NULL;
	GSubprocess *subprocess = G_SUBPROCESS (source_object);
	GCancellable *cancellable = g_task_get_cancellable (task);
	gboolean communicate_succeeded;

	communicate_succeeded = g_subprocess_communicate_utf8_finish (subprocess, result, &val_stdout, &val_stderr, &local_error);
	if (!communicate_succeeded ||
	    /* it's safe to call it here, because the g_subprocess_communicate_utf8_async()
	       already waited for the process to finish, thus this does not block on I/O */
	    !g_subprocess_wait_check (subprocess, cancellable, &local_error)) {
		if (communicate_succeeded && (val_stdout == NULL || *val_stdout == '\0') && val_stderr != NULL && *val_stderr != '\0') {
			g_set_error_literal (&result_error, local_error->domain, local_error->code, val_stderr);
		} else if (communicate_succeeded) {
			g_set_error (&result_error, local_error->domain, local_error->code,
				     "%s%s%s%s%s",
				     local_error->message,
				     val_stdout != NULL && *val_stdout != '\0' ? "\nstdout: " : "",
				     val_stdout != NULL && *val_stdout != '\0' ? val_stdout : "",
				     val_stderr != NULL && *val_stderr != '\0' ? "\nstderr: " : "",
				     val_stderr != NULL && *val_stderr != '\0' ? val_stderr : "");
		} else {
			g_propagate_error (&result_error, g_steal_pointer (&local_error));
		}
	} else {
		if (g_cancellable_set_error_if_cancelled (cancellable, &local_error)) {
			g_propagate_error (&result_error, g_steal_pointer (&local_error));
		} else if (val_stderr != NULL && *val_stderr != '\0') {
			g_set_error_literal (&result_error, G_IO_ERROR, G_IO_ERROR_FAILED, val_stderr);
		} else {
			g_task_return_int (task, GS_DKMS_STATE_ENROLLED);
			return;
		}
	}

	if (result_error != NULL && result_error->domain == G_SPAWN_EXIT_ERROR) {
		switch (result_error->code) {
		case GS_DKMS_STATE_ENROLLED:
		case GS_DKMS_STATE_NOT_FOUND:
		case GS_DKMS_STATE_NOT_ENROLLED:
		case GS_DKMS_STATE_PENDING:
			g_task_return_int (task, result_error->code);
			return;
		case GS_DKMS_STATE_ERROR:
		default:
			break;
		}
	}

	if (result_error != NULL)
		g_task_return_error (task, g_steal_pointer (&result_error));
	else
		g_task_return_int (task, GS_DKMS_STATE_ERROR);
}

static void
gs_dkms_execute_async (const gchar * const *args,
		       const gchar *stdin_str,
		       GCancellable *cancellable,
		       GAsyncReadyCallback callback,
		       gpointer user_data)
{
	g_autoptr(GSubprocess) subprocess = NULL;
	g_autoptr(GTask) task = NULL;
	g_autoptr(GError) local_error = NULL;

	task = g_task_new (NULL, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_dkms_execute_async);

	subprocess = g_subprocess_newv (args, (stdin_str == NULL ? 0 : G_SUBPROCESS_FLAGS_STDIN_PIPE) |
					G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
					&local_error);
	if (local_error != NULL) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_subprocess_communicate_utf8_async (subprocess, stdin_str, cancellable,
					     gs_dkms_execute_communicated_cb, g_steal_pointer (&task));
}

static GsDkmsState
gs_dkms_execute_finish (GAsyncResult *result,
			GError **error)
{
	return (GsDkmsState) g_task_propagate_int (G_TASK (result), error);
}

static GsDkmsState last_akmods_key_state = GS_DKMS_STATE_ERROR;
static gint64 last_akmods_key_state_time = 0;
static GsDkmsState last_dkms_key_state = GS_DKMS_STATE_ERROR;
static gint64 last_dkms_key_state_time = 0;

static void
gs_dkms_finish_get_key_state_internal (GTask *task,
				       GAsyncResult *result,
				       GsDkmsState *out_key_state,
				       gint64 *out_key_state_time)
{
	g_autoptr(GError) local_error = NULL;

	*out_key_state = gs_dkms_execute_finish (result, &local_error);
	*out_key_state_time = g_get_real_time ();

	if (local_error != NULL)
		g_task_return_error (task, g_steal_pointer (&local_error));
	else
		g_task_return_int (task, *out_key_state);
}

static void
gs_dkms_got_akmods_key_state_cb (GObject *source_object,
				 GAsyncResult *result,
				 gpointer user_data)
{
	g_autoptr(GTask) task = user_data;

	/* accesses the global variables in the main thread */
	g_assert (g_main_context_is_owner (g_main_context_default ()));

	gs_dkms_finish_get_key_state_internal (task, result, &last_akmods_key_state, &last_akmods_key_state_time);
}

static void
gs_dkms_got_dkms_key_state_cb (GObject *source_object,
			       GAsyncResult *result,
			       gpointer user_data)
{
	g_autoptr(GTask) task = user_data;

	/* accesses the global variables in the main thread */
	g_assert (g_main_context_is_owner (g_main_context_default ()));

	gs_dkms_finish_get_key_state_internal (task, result, &last_dkms_key_state, &last_dkms_key_state_time);
}

/*
 * gs_dkms_get_key_state_async:
 * @key_kind: which key to check
 * @cancellable: a #GCancellable or %NULL
 * @callback: (not nullable): a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: (closure callback) (scope async): data to pass to @callback
 *
 * Asynchronously checks what state the @key_kind key currently is.
 * Use gs_dkms_get_key_state_finish() withing the @callback
 * to complete the call.
 *
 * Since: 47
 **/
void
gs_dkms_get_key_state_async (GsDkmsKeyKind key_kind,
			     GCancellable *cancellable,
			     GAsyncReadyCallback callback,
			     gpointer user_data)
{
	const gchar *args[] = {
		"pkexec",
		LIBEXECDIR "/gnome-software-dkms-helper",
		NULL, /* one of --test arguments */
		NULL
	};
	g_autoptr(GTask) task = NULL;
	GsDkmsState *last_key_state = NULL;
	gint64 *last_key_state_time = NULL;

	task = g_task_new (NULL, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_dkms_get_key_state_async);

	if (key_kind == GS_DKMS_KEY_KIND_AKMODS) {
		if (!g_file_test (GS_AKMODS_KEY_PATH, G_FILE_TEST_IS_DIR)) {
			g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, "Akmods key directory not found.");
			return;
		}
		args[2] = "--test-akmods";
		last_key_state = &last_akmods_key_state;
		last_key_state_time = &last_akmods_key_state_time;
	} else if (key_kind == GS_DKMS_KEY_KIND_DKMS) {
		g_autofree gchar *dkms_key_path = gs_dkms_get_dkms_key_path ();
		if (!g_file_test (dkms_key_path, G_FILE_TEST_IS_DIR)) {
			g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, "DKMS key directory not found.");
			return;
		}
		args[2] = "--test-dkms";
		last_key_state = &last_dkms_key_state;
		last_key_state_time = &last_dkms_key_state_time;
	} else {
		g_assert_not_reached ();
	}

	/* accesses the global variables in the main thread */
	g_assert (g_main_context_is_owner (g_main_context_default ()));

	/* consider state discovered within the last 5 seconds still valid */
	if (g_get_real_time () > (*last_key_state_time) + (G_USEC_PER_SEC * 5)) {
		if (key_kind == GS_DKMS_KEY_KIND_AKMODS)
			gs_dkms_execute_async ((const gchar * const *) args, NULL, cancellable, gs_dkms_got_akmods_key_state_cb, g_steal_pointer (&task));
		else
			gs_dkms_execute_async ((const gchar * const *) args, NULL, cancellable, gs_dkms_got_dkms_key_state_cb, g_steal_pointer (&task));
	} else {
		g_task_return_int (task, (*last_key_state));
	}
}

/*
 * gs_dkms_get_key_state_finish:
 * @result: an async result
 * @error: a #GError or %NULL
 *
 * Finishes operation started by gs_dkms_get_key_state_async().
 *
 * Returns: one of #GsDkmsState
 *
 * Since: 47
 */
GsDkmsState
gs_dkms_get_key_state_finish (GAsyncResult *result,
			      GError **error)
{
	g_return_val_if_fail (g_task_is_valid (G_TASK (result), NULL), GS_DKMS_STATE_ERROR);
	g_return_val_if_fail (g_async_result_is_tagged (result, gs_dkms_get_key_state_async), GS_DKMS_STATE_ERROR);

	return (GsDkmsState) g_task_propagate_int (G_TASK (result), error);
}

static void
gs_dkms_enrolled_key_cb (GObject *source_object,
			 GAsyncResult *result,
			 gpointer user_data)
{
	g_autoptr(GTask) task = user_data;
	g_autoptr(GError) local_error = NULL;
	GsDkmsState key_state;

	key_state = gs_dkms_execute_finish (result, &local_error);

	if (local_error != NULL)
		g_task_return_error (task, g_steal_pointer (&local_error));
	else
		g_task_return_int (task, key_state);
}

/*
 * gs_dkms_enroll_async:
 * @key_kind: which key to enroll
 * @password: (not nullable): an import password
 * @cancellable: a #GCancellable or %NULL
 * @callback: (not nullable): a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: (closure callback) (scope async): data to pass to @callback
 *
 * Asynchronously enrolls the @key_kind key. It can create one, if no such exists yet.
 * The import @password is to be used in MOK on reboot. Use gs_dkms_enroll_finish()
 * withing the @callback to complete the call.
 *
 * Since: 47
 **/
void
gs_dkms_enroll_async (GsDkmsKeyKind key_kind,
		      const gchar *password,
		      GCancellable *cancellable,
		      GAsyncReadyCallback callback,
		      gpointer user_data)
{
	const gchar *args[] = {
		"pkexec",
		LIBEXECDIR "/gnome-software-dkms-helper",
		NULL, /* one of --enroll arguments */
		NULL
	};
	g_autoptr(GTask) task = NULL;

	g_assert (password != NULL);

	task = g_task_new (NULL, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_dkms_enroll_async);

	if (key_kind == GS_DKMS_KEY_KIND_AKMODS)
		args[2] = "--enroll-akmods";
	else if (key_kind == GS_DKMS_KEY_KIND_DKMS)
		args[2] = "--enroll-dkms";
	else
		g_assert_not_reached ();

	gs_dkms_execute_async ((const gchar * const *) args, password, cancellable, gs_dkms_enrolled_key_cb, g_steal_pointer (&task));
}

/*
 * gs_dkms_enroll_finish:
 * @result: an async result
 * @error: a #GError or %NULL
 *
 * Finishes operation started by gs_dkms_enroll_async().
 *
 * Returns: one of #GsDkmsState
 *
 * Since: 47
 */
GsDkmsState
gs_dkms_enroll_finish (GAsyncResult *result,
		       GError **error)
{
	g_return_val_if_fail (g_task_is_valid (G_TASK (result), NULL), GS_DKMS_STATE_ERROR);
	g_return_val_if_fail (g_async_result_is_tagged (result, gs_dkms_enroll_async), GS_DKMS_STATE_ERROR);

	return (GsDkmsState) g_task_propagate_int (G_TASK (result), error);
}

static GsSecurebootState secureboot_state = GS_SECUREBOOT_STATE_UNKNOWN;

static void
gs_dkms_get_secureboot_state_ready_cb (GObject *source_object,
				       GAsyncResult *result,
				       gpointer user_data)
{
	GSubprocess *subprocess = G_SUBPROCESS (source_object);
	g_autoptr(GTask) task = user_data;
	g_autoptr(GError) local_error = NULL;
	g_autofree gchar *standard_output = NULL;
	g_autofree gchar *standard_error = NULL;

	if (!g_subprocess_communicate_utf8_finish (subprocess, result, &standard_output, &standard_error, &local_error)) {
		g_debug ("dkms: Failed to enum Secure Boot state: %s", local_error->message);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	if (standard_output == NULL) {
		g_debug ("dkms: No standard output from 'mokutil'");
		g_task_return_int (task, secureboot_state);
		return;
	}

	/* FIXME: use the return code instead of text parsing once the https://github.com/lcp/mokutil/issues/88 is addressed */
	#define ENABLED_OUTPUT "SecureBoot enabled\n"
	#define DISABLED_OUTPUT "SecureBoot disabled\n"
	#define NOT_SUPPORTED_OUTPUT "EFI variables are not supported on this system\n"

	if (g_ascii_strncasecmp (standard_output, ENABLED_OUTPUT, strlen (ENABLED_OUTPUT)) == 0)
		secureboot_state = GS_SECUREBOOT_STATE_ENABLED;
	else if (g_ascii_strncasecmp (standard_output, DISABLED_OUTPUT, strlen (DISABLED_OUTPUT)) == 0)
		secureboot_state = GS_SECUREBOOT_STATE_DISABLED;
	else if (*standard_output == '\0' && standard_error != NULL &&
		 g_ascii_strncasecmp (standard_error, NOT_SUPPORTED_OUTPUT, strlen (NOT_SUPPORTED_OUTPUT)) == 0)
		secureboot_state = GS_SECUREBOOT_STATE_NOT_SUPPORTED;
	else
		g_debug ("dkms: Unexpected response from 'mokutil': '%s'; stderr:'%s'", standard_output, standard_error);

	#undef ENABLED_OUTPUT
	#undef DISABLED_OUTPUT
	#undef NOT_SUPPORTED_OUTPUT

	g_task_return_int (task, secureboot_state);
}

/*
 * gs_dkms_get_secureboot_state_async:
 * @cancellable: a #GCancellable or %NULL
 * @callback: (not nullable): a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: (closure callback) (scope async): data to pass to @callback
 *
 * Asynchronously enumerates Secure Boot state of the system.
 * Use gs_dkms_get_secureboot_state_finish() withing the @callback
 * to complete the call.
 *
 * Since: 47
 **/
void
gs_dkms_get_secureboot_state_async (GCancellable *cancellable,
				    GAsyncReadyCallback callback,
				    gpointer user_data)
{
	g_autoptr(GSubprocess) subprocess = NULL;
	g_autoptr(GTask) task = NULL;
	g_autoptr(GError) local_error = NULL;
	const gchar *args[] = { "mokutil", "--sb-state", NULL };

	task = g_task_new (NULL, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_dkms_get_secureboot_state_async);

	if (secureboot_state != GS_SECUREBOOT_STATE_UNKNOWN) {
		g_task_return_int (task, secureboot_state);
		return;
	}

	subprocess = g_subprocess_newv ((const gchar * const *) args,
					G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
					&local_error);
	if (subprocess == NULL) {
		g_debug ("dkms: Failed to create process to enum Secure Boot state: %s", local_error->message);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_task_set_task_data (task, g_object_ref (subprocess), g_object_unref);

	g_subprocess_communicate_utf8_async (subprocess, NULL, cancellable, gs_dkms_get_secureboot_state_ready_cb, g_steal_pointer (&task));
}

/*
 * gs_dkms_get_secureboot_state_finish:
 * @result: an async result
 * @error: a #GError or %NULL
 *
 * Finishes operation started by gs_dkms_get_secureboot_state_async().
 * It saves the value for later use by gs_dkms_get_last_secureboot_state().
 *
 * It can happen the return value is %GS_SECUREBOOT_STATE_UNKNOWN, for
 * example when the mokutil is not installed or calling it failed.
 *
 * Returns: one of #GsSecurebootState
 *
 * Since: 47
 */
GsSecurebootState
gs_dkms_get_secureboot_state_finish (GAsyncResult *result,
				     GError **error)
{
	g_return_val_if_fail (g_task_is_valid (G_TASK (result), NULL), secureboot_state);
	g_return_val_if_fail (g_async_result_is_tagged (result, gs_dkms_get_secureboot_state_async), secureboot_state);

	return (GsSecurebootState) g_task_propagate_int (G_TASK (result), error);
}

/*
 * gs_dkms_get_last_secureboot_state:
 *
 * Returns last recognized state from gs_dkms_get_secureboot_state_sync().
 *
 * Returns: previously enumerated secure boot state
 *
 * Since: 47
 **/
GsSecurebootState
gs_dkms_get_last_secureboot_state (void)
{
	return secureboot_state;
}

/*
 * gs_dkms_get_dkms_key_path:
 *
 * Returns key path for the DKMS, as read from the configuration,
 * or using the default. Free the returned pointer with g_free(),
 * when no longer needed.
 *
 * Returns: (transfer full) (type filename): a path where DKMS key is stored
 *
 * Since: 47
 **/
gchar *
gs_dkms_get_dkms_key_path (void)
{
	g_autofree gchar *filename = gs_dkms_get_dkms_key_filename ();
	return g_path_get_dirname (filename);
}

/*
 * gs_dkms_get_dkms_key_filename:
 *
 * Returns key file name (with path) for the DKMS, as read from the configuration,
 * or using the default. Free the returned pointer with g_free(),
 * when no longer needed.
 *
 * Returns: (transfer full) (type filename): a file name of the DKMS key
 *
 * Since: 47
 **/
gchar *
gs_dkms_get_dkms_key_filename (void)
{
	g_autofree gchar *contents = NULL;

	if (g_file_get_contents ("/etc/dkms/framework.conf", &contents, NULL, NULL)) {
		/* the configuration file is almost ini-like, add a fake section
		   at the top to be able to use GKeyFile routines */
		g_autoptr(GString) fake_ini = g_string_new ("[keys]\n");
		g_autoptr(GKeyFile) keyfile = g_key_file_new ();
		g_autoptr(GError) local_error = NULL;

		g_string_append (fake_ini, contents);

		if (g_key_file_load_from_data (keyfile, fake_ini->str, -1, G_KEY_FILE_NONE, &local_error)) {
			g_autofree gchar *filename = g_key_file_get_string (keyfile, "keys", "mok_certificate", NULL);
			if (filename != NULL && *filename != '\0')
				return g_steal_pointer (&filename);
		} else {
			g_debug ("dkms: Failed to read '/etc/dkms/framework.conf': %s", local_error->message);
		}
	}

	/* this is the default key to be used */
	return g_strdup ("/var/lib/dkms/mok.pub");
}
