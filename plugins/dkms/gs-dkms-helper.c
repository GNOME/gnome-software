/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <locale.h>
#include <stdio.h>
#include <glib.h>
#include <gio/gio.h>

#include "gs-dkms-private.h"

static int
gs_dkms_helper_check_result (const gchar *val_stdout,
			     GsDkmsKeyKind key_kind,
			     gboolean with_print)
{
	g_autofree gchar *not_found_output = NULL;
	g_autofree gchar *not_enrolled_output = NULL;
	g_autofree gchar *pending_output = NULL;
	g_autofree gchar *enrolled_output = NULL;
	g_autofree gchar *dkms_key_filename = NULL;
	const gchar *key_filename;

	g_assert (val_stdout != NULL);

	if (key_kind == GS_DKMS_KEY_KIND_AKMODS)
		key_filename = GS_AKMODS_KEY_FILENAME;
	else if (key_kind == GS_DKMS_KEY_KIND_DKMS)
		key_filename = dkms_key_filename = gs_dkms_get_dkms_key_filename ();
	else
		g_assert_not_reached ();

	/* FIXME: use the return code instead of text parsing once the https://github.com/lcp/mokutil/issues/88 is addressed */
	not_found_output = g_strconcat (key_filename, " not found\n", NULL);
	not_enrolled_output = g_strconcat (key_filename, " is not enrolled\n", NULL);
	pending_output = g_strconcat (key_filename, " is already in the enrollment request\n", NULL);
	enrolled_output = g_strconcat (key_filename, " is already enrolled\n", NULL);

	if (g_ascii_strncasecmp (val_stdout, not_found_output, strlen (not_found_output)) == 0) {
		return GS_DKMS_STATE_NOT_FOUND;
	} else if (g_ascii_strncasecmp (val_stdout, not_enrolled_output, strlen (not_enrolled_output)) == 0) {
		return GS_DKMS_STATE_NOT_ENROLLED;
	} else if (g_ascii_strncasecmp (val_stdout, pending_output, strlen (pending_output)) == 0) {
		return GS_DKMS_STATE_PENDING;
	} else if (g_ascii_strncasecmp (val_stdout, enrolled_output, strlen (enrolled_output)) == 0) {
		return GS_DKMS_STATE_ENROLLED;
	} else if (with_print) {
		g_printerr ("Unexpected output '%s'\n", val_stdout);
	}

	return GS_DKMS_STATE_ERROR;
}

static int
gs_dkms_helper_test (GsDkmsKeyKind key_kind,
		     gboolean with_print)
{
	const gchar *args[] = {
		"mokutil",
		"--test-key",
		NULL, /* key filename */
		NULL
	};
	g_autofree gchar *val_stdout = NULL;
	g_autofree gchar *val_stderr = NULL;
	g_autofree gchar *dkms_key_filename = NULL;
	g_autoptr(GSubprocess) subprocess = NULL;
	g_autoptr(GError) local_error = NULL;

	if (key_kind == GS_DKMS_KEY_KIND_AKMODS) {
		if (!g_file_test (GS_AKMODS_KEY_PATH, G_FILE_TEST_IS_DIR)) {
			if (with_print)
				g_printerr ("Akmods key directory not found.\n");
			return GS_DKMS_STATE_ERROR;
		}
		args[2] = GS_AKMODS_KEY_FILENAME;
	} else if (key_kind == GS_DKMS_KEY_KIND_DKMS) {
		g_autofree gchar *dkms_key_path = gs_dkms_get_dkms_key_path ();
		if (!g_file_test (dkms_key_path, G_FILE_TEST_IS_DIR)) {
			if (with_print)
				g_printerr ("DKMS key directory not found.\n");
			return GS_DKMS_STATE_ERROR;
		}
		dkms_key_filename = gs_dkms_get_dkms_key_filename ();
		args[2] = dkms_key_filename;
	} else {
		g_assert_not_reached ();
	}

	subprocess = g_subprocess_newv ((const gchar * const *) args,
					G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
					&local_error);
	if (subprocess == NULL) {
		if (with_print)
			g_printerr ("Failed to call 'mokutil --test-key': %s\n", local_error->message);
		return GS_DKMS_STATE_ERROR;
	}

	if (!g_subprocess_communicate_utf8 (subprocess, NULL, NULL, &val_stdout, &val_stderr, &local_error) ||
	    !g_subprocess_wait_check (subprocess, NULL, &local_error)) {
		if ((val_stdout == NULL || *val_stdout == '\0') && val_stderr != NULL && *val_stderr != '\0') {
			/* FIXME: use the return code instead of text parsing once the https://github.com/lcp/mokutil/issues/88 is addressed */
			g_autofree gchar *not_found_error = g_strconcat ("Failed to open ", args[2], "\n", NULL);
			if (g_ascii_strncasecmp (val_stderr, not_found_error, strlen (not_found_error)) == 0)
				return GS_DKMS_STATE_NOT_FOUND;
			if (with_print)
				g_printerr ("Failed to call 'mokutil --test-key': %s\n", val_stderr);
			return GS_DKMS_STATE_ERROR;
		} else if (val_stdout != NULL && g_error_matches (local_error, G_SPAWN_EXIT_ERROR, 1)) {
			/* it can mean: "pending to be enrolled" or "already enrolled" */
			return gs_dkms_helper_check_result (val_stdout, key_kind, with_print);
		} else {
			if (with_print) {
				g_printerr ("Failed to call 'mokutil --test-key': %s%s%s%s%s\n", local_error->message,
					    val_stdout != NULL && *val_stdout != '\0' ? "\nstdout: " : "",
					    val_stdout != NULL && *val_stdout != '\0' ? val_stdout : "",
					    val_stderr != NULL && *val_stderr != '\0' ? "\nstderr: " : "",
					    val_stderr != NULL && *val_stderr != '\0' ? val_stderr : "");
			}
			return GS_DKMS_STATE_ERROR;
		}
	} else {
		if (val_stderr != NULL && *val_stderr != '\0') {
			if (with_print)
				g_printerr ("Something failed while calling 'mokutil --test-key': %s\n", val_stderr);
			return GS_DKMS_STATE_ERROR;
		}
	}

	return gs_dkms_helper_check_result (val_stdout, key_kind, with_print);
}

static int
gs_dkms_helper_generate (GsDkmsKeyKind key_kind)
{
	const gchar *args[] = {
		"kmodgenca",
		"-a",
		NULL
	};
	g_autoptr(GSubprocess) subprocess = NULL;
	g_autoptr(GError) local_error = NULL;
	g_autofree gchar *val_stdout = NULL;
	g_autofree gchar *val_stderr = NULL;

	if (key_kind != GS_DKMS_KEY_KIND_AKMODS)
		return GS_DKMS_STATE_NOT_FOUND;

	subprocess = g_subprocess_newv ((const gchar * const *) args,
					G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
					&local_error);
	if (subprocess == NULL) {
		g_printerr ("Failed to call 'kmodgenca': %s\n", local_error->message);
		return GS_DKMS_STATE_ERROR;
	}

	if (!g_subprocess_communicate_utf8 (subprocess, NULL, NULL, &val_stdout, &val_stderr, &local_error) ||
	    !g_subprocess_wait_check (subprocess, NULL, &local_error)) {
		if ((val_stdout == NULL || *val_stdout == '\0') && val_stderr != NULL && *val_stderr != '\0') {
			g_printerr ("Failed to call 'kmodgenca': %s\n", val_stderr);
		} else {
			g_printerr ("Failed to call 'kmodgenca': %s%s%s%s%s\n", local_error->message,
				    val_stdout != NULL && *val_stdout != '\0' ? "\nstdout: " : "",
				    val_stdout != NULL && *val_stdout != '\0' ? val_stdout : "",
				    val_stderr != NULL && *val_stderr != '\0' ? "\nstderr: " : "",
				    val_stderr != NULL && *val_stderr != '\0' ? val_stderr : "");
		}
		return GS_DKMS_STATE_ERROR;
	}
	/* stderr contains keygen random data, thus do not treat it as "something failed" */

	return GS_DKMS_STATE_NOT_ENROLLED;
}

static int
gs_dkms_helper_import (GsDkmsKeyKind key_kind)
{
	const gchar *args[] = {
		"mokutil",
		"--import",
		NULL, /* key filename */
		NULL
	};

	g_autofree gchar *val_stdout = NULL;
	g_autofree gchar *val_stderr = NULL;
	g_autofree gchar *dkms_key_filename = NULL;
	g_autoptr(GString) password = g_string_new (NULL);
	g_autoptr(GSubprocess) subprocess = NULL;
	g_autoptr(GError) local_error = NULL;
	int chr;

	if (key_kind == GS_DKMS_KEY_KIND_AKMODS)
		args[2] = GS_AKMODS_KEY_FILENAME;
	else if (key_kind == GS_DKMS_KEY_KIND_DKMS)
		args[2] = dkms_key_filename = gs_dkms_get_dkms_key_filename ();
	else
		g_assert_not_reached ();

	/* the password comes on stdin */
	while ((chr = getchar ()) != EOF) {
		g_string_append_c (password, chr);
	}

	if (password->len == 0) {
		g_printerr ("Password cannot be empty.\n");
		return GS_DKMS_STATE_ERROR;
	}

	subprocess = g_subprocess_newv ((const gchar * const *) args,
					G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
					&local_error);
	if (subprocess == NULL) {
		g_printerr ("Failed to call 'mokutil --import': %s\n", local_error->message);
		return GS_DKMS_STATE_ERROR;
	}

	/* password is entered twice, each ended by a new line, thus construct the stdin that way */
	g_string_append_c (password, '\n');
	g_string_append (password, password->str);

	if (!g_subprocess_communicate_utf8 (subprocess, password->str, NULL, &val_stdout, &val_stderr, &local_error) ||
	    !g_subprocess_wait_check (subprocess, NULL, &local_error)) {
		if ((val_stdout == NULL || *val_stdout == '\0') && val_stderr != NULL && *val_stderr != '\0') {
			g_printerr ("Failed to call 'mokutil --import': %s\n", val_stderr);
		} else {
			g_printerr ("Failed to call 'mokutil --import': %s%s%s%s%s\n", local_error->message,
				    val_stdout != NULL && *val_stdout != '\0' ? "\nstdout: " : "",
				    val_stdout != NULL && *val_stdout != '\0' ? val_stdout : "",
				    val_stderr != NULL && *val_stderr != '\0' ? "\nstderr: " : "",
				    val_stderr != NULL && *val_stderr != '\0' ? val_stderr : "");
		}
		return GS_DKMS_STATE_ERROR;
	} else {
		if (val_stderr != NULL && *val_stderr != '\0') {
			g_printerr ("Something failed while calling 'mokutil --import': %s\n", val_stderr);
			return GS_DKMS_STATE_ERROR;
		}
	}

	return GS_DKMS_STATE_PENDING;
}

static int
gs_dkms_helper_enroll (GsDkmsKeyKind key_kind)
{
	GsDkmsState state = gs_dkms_helper_test (key_kind, FALSE);
	if (state == GS_DKMS_STATE_ERROR)
		return gs_dkms_helper_test (key_kind, TRUE);
	if (state == GS_DKMS_STATE_ENROLLED || state == GS_DKMS_STATE_PENDING)
		return state;
	if (state == GS_DKMS_STATE_NOT_FOUND)
		state = gs_dkms_helper_generate (key_kind);
	if (state == GS_DKMS_STATE_NOT_ENROLLED)
		state = gs_dkms_helper_import (key_kind);
	return state;
}

int
main (int argc,
      const char *argv[])
{
	setlocale (LC_ALL, "");

	if (argc != 2 ||
	    g_strcmp0 (argv[1], "--help") == 0) {
		g_printerr ("Requires one argument, --test-akmods, --test-dkms, --enroll-akmods or --enroll-dkms\n");
		return GS_DKMS_STATE_ERROR;
	}

	if (g_strcmp0 (argv[1], "--test-akmods") == 0)
		return gs_dkms_helper_test (GS_DKMS_KEY_KIND_AKMODS, TRUE);

	if (g_strcmp0 (argv[1], "--test-dkms") == 0)
		return gs_dkms_helper_test (GS_DKMS_KEY_KIND_DKMS, TRUE);

	if (g_strcmp0 (argv[1], "--enroll-akmods") == 0)
		return gs_dkms_helper_enroll (GS_DKMS_KEY_KIND_AKMODS);

	if (g_strcmp0 (argv[1], "--enroll-dkms") == 0)
		return gs_dkms_helper_enroll (GS_DKMS_KEY_KIND_DKMS);

	g_printerr ("Unknown argument '%s'\n", argv[1]);
	return GS_DKMS_STATE_ERROR;
}
