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

#include "gs-cleanup.h"
#include "gs-offline-updates.h"
#include "gs-utils.h"

static void
gs_offline_updates_label_allocate_cb (GtkWidget *label,
				      GdkRectangle *allocation, gpointer data)
{
	GtkScrolledWindow *sw = GTK_SCROLLED_WINDOW (data);
	gtk_scrolled_window_set_min_content_height (
			sw, MIN (allocation->height, 300));
}

void
gs_offline_updates_show_error (void)
{
	const gchar *title;
	gboolean show_geeky = FALSE;
	GtkWidget *dialog, *message_area, *sw, *label;
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ PkError *pk_error = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;
	_cleanup_string_free_ GString *msg = NULL;

	results = pk_offline_get_results (NULL);
	if (results == NULL)
		return;
	pk_error = pk_results_get_error_code (results);
	if (pk_error == NULL)
		return;

	/* can this happen in reality? */
	if (pk_results_get_exit_code (results) == PK_EXIT_ENUM_SUCCESS)
		return;

	/* TRANSLATORS: this is when the offline update failed */
	title = _("Failed To Update");
	msg = g_string_new ("");
	switch (pk_error_get_code (pk_error)) {
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
					pk_error_get_details (pk_error));
	}
	dialog = gtk_message_dialog_new (NULL,
					 0,
					 GTK_MESSAGE_INFO,
					 GTK_BUTTONS_CLOSE,
					 "%s", title);

	message_area = gtk_message_dialog_get_message_area (GTK_MESSAGE_DIALOG (dialog));
	g_assert (GTK_IS_BOX (message_area));
	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
					GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_visible (sw, TRUE);

	label = gtk_label_new (msg->str);
	gtk_widget_set_visible (label, TRUE);
	g_signal_connect (label, "size-allocate",
			G_CALLBACK (gs_offline_updates_label_allocate_cb), sw);
	gtk_container_add (GTK_CONTAINER (sw), label);
	gtk_container_add (GTK_CONTAINER (message_area), sw);

	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy),
				  dialog);
	gtk_widget_show (dialog);

	if (!pk_offline_clear_results (NULL, &error)) {
		g_warning ("Failure clearing offline update message: %s",
			   error->message);
	}
}

/* vim: set noexpandtab: */
