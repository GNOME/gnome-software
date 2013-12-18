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

#include "config.h"

#include <glib/gi18n.h>
#include <packagekit-glib2/packagekit.h>

#include "gs-offline-updates.h"
#include "gs-utils.h"

static void
child_exit_cb (GPid pid, gint status, gpointer user_data)
{
	g_spawn_close_pid (pid);
}


static gboolean
gs_spawn_pkexec (const gchar *command, const gchar *parameter, GError **error)
{
	GPid pid;
	const gchar *argv[4];
	gboolean ret;

	argv[0] = "pkexec";
	argv[1] = command;
	argv[2] = parameter;
	argv[3] = NULL;
	g_debug ("calling %s %s %s",
		 argv[0], argv[1], argv[2] != NULL ? argv[2] : "");
	ret = g_spawn_async (NULL, (gchar**)argv, NULL,
			     G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH,
			     NULL, NULL, &pid, error);
	if (!ret)
		return FALSE;
	g_child_watch_add (pid, child_exit_cb, NULL);
	return TRUE;
}

void
gs_offline_updates_clear_status (void)
{
	gboolean ret;
	GError *error = NULL;

	ret = gs_spawn_pkexec (LIBEXECDIR "/pk-clear-offline-update", NULL, &error);
	if (!ret) {
		g_warning ("Failure clearing offline update message: %s",
			   error->message);
		g_error_free (error);
	}
}

void
gs_offline_updates_trigger (void)
{
	gboolean ret;
	GError *error = NULL;
	GDateTime *now;
	GSettings *settings;

	ret = gs_spawn_pkexec (LIBEXECDIR "/pk-trigger-offline-update", NULL, &error);
	if (!ret) {
		g_warning ("Failure triggering offline update: %s",
			   error->message);
		g_error_free (error);
	}

	now = g_date_time_new_now_local ();
	settings = g_settings_new ("org.gnome.software");
	g_settings_set (settings, "install-timestamp", "x",
			g_date_time_to_unix (now));
	g_date_time_unref (now);
	g_object_unref (settings);
}

void
gs_offline_updates_cancel (void)
{
	gboolean ret;
	GError *error = NULL;

	ret = gs_spawn_pkexec (LIBEXECDIR "/pk-trigger-offline-update",
			       "--cancel", &error);
	if (!ret) {
		g_warning ("Failure cancelling offline update: %s",
			   error->message);
		g_error_free (error);
	}
}

#define PK_OFFLINE_UPDATE_RESULTS_GROUP		"PackageKit Offline Update Results"
#define PK_OFFLINE_UPDATE_RESULTS_FILENAME	"/var/lib/PackageKit/offline-update-competed"

gboolean
gs_offline_updates_get_status (gboolean  *success,
			       guint     *num_packages,
			       gchar    **error_code,
			       gchar    **error_details)
{
	GKeyFile *key_file = NULL;
	gchar *packages = NULL;
	gchar *code = NULL;
	gchar *details = NULL;
	gboolean result = FALSE;
	gboolean ret;
	GError *error = NULL;
	gint i;

	g_debug ("get offline update status");

	*success = FALSE;
	*num_packages = 0;
	if (error_code)
		*error_code = 0;
	if (error_details)
		*error_details = NULL;

	if (!g_file_test (PK_OFFLINE_UPDATE_RESULTS_FILENAME, G_FILE_TEST_EXISTS))
		goto out;

	key_file = g_key_file_new ();
	ret = g_key_file_load_from_file (key_file,
					 PK_OFFLINE_UPDATE_RESULTS_FILENAME,
					 G_KEY_FILE_NONE,
					 &error);
	if (!ret) {
		g_warning ("failed to open %s: %s",
			   PK_OFFLINE_UPDATE_RESULTS_FILENAME,
			   error->message);
		g_error_free (error);
		goto out;
	}

	*success = g_key_file_get_boolean (key_file,
					   PK_OFFLINE_UPDATE_RESULTS_GROUP,
					   "Success",
					   NULL);

	if (*success) {
		packages = g_key_file_get_string (key_file,
						  PK_OFFLINE_UPDATE_RESULTS_GROUP,
						  "Packages",
						  NULL);

		if (packages == NULL) {
			g_warning ("No 'Packages' in %s",
				   PK_OFFLINE_UPDATE_RESULTS_FILENAME);
			goto out;
		}

		for (i = 0; packages[i] != '\0'; i++) {
			if (packages[i] == ',')
				(*num_packages)++;
		}

	} else {

		code = g_key_file_get_string (key_file,
					      PK_OFFLINE_UPDATE_RESULTS_GROUP,
					      "ErrorCode",
					      NULL);
		details = g_key_file_get_string (key_file,
						 PK_OFFLINE_UPDATE_RESULTS_GROUP,
						 "ErrorDetails",
						 NULL);
	}

	result = TRUE;

out:
	g_debug ("success %d, packages %s, error %s %s",
		 *success, packages, code, details);
	if (error_code)
		*error_code = code;
	else
		g_free (code);
	if (error_details)
		*error_details = details;
	else
		g_free (details);
	g_free (packages);
	if (key_file != NULL)
		g_key_file_free (key_file);

	return result;
}

void
gs_offline_updates_show_error (void)
{
	const gchar *title;
	gboolean show_geeky = FALSE;
	GString *msg;
	GtkWidget *dialog;
	gboolean success;
	guint num_packages;
	gchar *error_code;
	gchar *error_details;
	PkErrorEnum error_enum = PK_ERROR_ENUM_UNKNOWN;

	if (!gs_offline_updates_get_status (&success, &num_packages, &error_code, &error_details))
		return;

	if (error_code != NULL)
		error_enum = pk_error_enum_from_string (error_code);

	/* TRANSLATORS: this is when the offline update failed */
	title = _("Failed To Update");
	msg = g_string_new ("");
	switch (error_enum) {
	case PK_ERROR_ENUM_UNFINISHED_TRANSACTION:
		/* TRANSLATORS: the transaction could not be completed
 		 * as a previous transaction was unfinished */
		g_string_append (msg, _("A previous update was unfinished."));
		show_geeky = TRUE;
		break;
	case PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED:
	case PK_ERROR_ENUM_NO_CACHE:
	case PK_ERROR_ENUM_NO_NETWORK:
	case PK_ERROR_ENUM_NO_MORE_MIRRORS_TO_TRY:
	case PK_ERROR_ENUM_CANNOT_FETCH_SOURCES:
		/* TRANSLATORS: the package manager needed to download
		 * something with no network available */
		g_string_append (msg, _("Network access was required but not available."));
		break;
	case PK_ERROR_ENUM_BAD_GPG_SIGNATURE:
	case PK_ERROR_ENUM_CANNOT_UPDATE_REPO_UNSIGNED:
	case PK_ERROR_ENUM_GPG_FAILURE:
	case PK_ERROR_ENUM_MISSING_GPG_SIGNATURE:
	case PK_ERROR_ENUM_PACKAGE_CORRUPT:
		/* TRANSLATORS: if the package is not signed correctly
		 *  */
		g_string_append (msg, _("An update was not signed in the correct way."));
		show_geeky = TRUE;
		break;
	case PK_ERROR_ENUM_DEP_RESOLUTION_FAILED:
	case PK_ERROR_ENUM_FILE_CONFLICTS:
	case PK_ERROR_ENUM_INCOMPATIBLE_ARCHITECTURE:
	case PK_ERROR_ENUM_PACKAGE_CONFLICTS:
		/* TRANSLATORS: the transaction failed in a way the user
		 * probably cannot comprehend. Package management systems
		 * really are teh suck.*/
		g_string_append (msg, _("The update could not be completed."));
		show_geeky = TRUE;
		break;
	case PK_ERROR_ENUM_TRANSACTION_CANCELLED:
		/* TRANSLATORS: the user aborted the update manually */
		g_string_append (msg, _("The update was cancelled."));
		break;
	case PK_ERROR_ENUM_NO_PACKAGES_TO_UPDATE:
	case PK_ERROR_ENUM_UPDATE_NOT_FOUND:
		/* TRANSLATORS: the user must have updated manually after
		 * the updates were prepared */
		g_string_append (msg, _("An offline update was requested but no packages required updating."));
		break;
	case PK_ERROR_ENUM_NO_SPACE_ON_DEVICE:
		/* TRANSLATORS: we ran out of disk space */
		g_string_append (msg, _("No space was left on the drive."));
		break;
	case PK_ERROR_ENUM_PACKAGE_FAILED_TO_BUILD:
	case PK_ERROR_ENUM_PACKAGE_FAILED_TO_INSTALL:
	case PK_ERROR_ENUM_PACKAGE_FAILED_TO_REMOVE:
		/* TRANSLATORS: the update process failed in a general
		 * way, usually this message will come from source distros
		 * like gentoo */
		g_string_append (msg, _("An update failed to install correctly."));
		show_geeky = TRUE;
		break;
	default:
		/* TRANSLATORS: We didn't handle the error type */
		g_string_append (msg, _("The offline update failed in an unexpected way."));
		show_geeky = TRUE;
		break;
	}
	if (show_geeky) {
		g_string_append_printf (msg, "\n%s\n\n%s",
					/* TRANSLATORS: these are geeky messages from the
					 * package manager no mortal is supposed to understand,
					 * but google might know what they mean */
					_("Detailed errors from the package manager follow:"),
					error_details);
	}
	dialog = gtk_message_dialog_new (NULL,
					 0,
					 GTK_MESSAGE_INFO,
					 GTK_BUTTONS_CLOSE,
					 "%s", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", msg->str);
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy),
				  dialog);
	gtk_widget_show (dialog);

	gs_offline_updates_clear_status ();
	g_string_free (msg, TRUE);

	g_free (error_code);
	g_free (error_details);
}

/* vim: set noexpandtab: */
