/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <gio/gio.h>

#include "gs-folders.h"

#define APP_FOLDER_SCHEMA       "org.gnome.desktop.app-folders"
#define APP_FOLDER_CHILD_SCHEMA "org.gnome.desktop.app-folders.folder"

/* Ensure we have the default folders for Utilities and YaST.
 * We can't do this as default values, since the schemas have
 * no fixed path.
 *
 * The app lists come from gnome-menus: layout/gnome-applications.menu
 */
void
gs_folders_convert (void)
{
	g_autoptr(GSettings) settings = NULL;
	g_auto(GStrv) ids = NULL;

	settings = g_settings_new (APP_FOLDER_SCHEMA);
	ids = g_settings_get_strv (settings, "folder-children");
	if (g_strv_length (ids) == 0) {
		const gchar * const children[] = {
			"Utilities",
			"YaST",
			NULL
		};
		const gchar * const utilities_categories[] = {
			"X-GNOME-Utilities",
			NULL
		};
		const gchar * const utilities_apps[] = {
			"gnome-abrt.desktop",
			"gnome-system-log.desktop",
			"nm-connection-editor.desktop",
			"org.gnome.baobab.desktop",
			"org.gnome.DejaDup.desktop",
			"org.gnome.Dictionary.desktop",
			"org.gnome.DiskUtility.desktop",
			"org.gnome.eog.desktop",
			"org.gnome.Evince.desktop",
			"org.gnome.FileRoller.desktop",
			"org.gnome.fonts.desktop",
			"org.gnome.seahorse.Application.desktop",
			"org.gnome.tweaks.desktop",
			"org.gnome.Usage.desktop",
			"vinagre.desktop",
			NULL
		};
		const gchar * const yast_categories[] = {
			"X-SuSE-YaST",
			NULL
		};

		gchar *path;
		gchar *child_path;
		GSettings *child;

		g_settings_set_strv (settings, "folder-children", children);
		g_object_get (settings, "path", &path, NULL);

		child_path = g_strconcat (path, "folders/Utilities/", NULL);
		child = g_settings_new_with_path (APP_FOLDER_CHILD_SCHEMA, child_path);
		g_settings_set_string (child, "name", "X-GNOME-Utilities.directory");
		g_settings_set_boolean (child, "translate", TRUE);
		g_settings_set_strv (child, "categories", utilities_categories);
		g_settings_set_strv (child, "apps", utilities_apps);

		g_object_unref (child);
		g_free (child_path);

		child_path = g_strconcat (path, "folders/YaST/", NULL);
		child = g_settings_new_with_path (APP_FOLDER_CHILD_SCHEMA, child_path);
		g_settings_set_string (child, "name", "suse-yast.directory");
		g_settings_set_boolean (child, "translate", TRUE);
		g_settings_set_strv (child, "categories", yast_categories);

		g_object_unref (child);
		g_free (child_path);
		
	}
}
