/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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

/* We are loading folders from a settings with type
 * a{sas}, which maps folder ids to list of app ids.
 *
 * For convenience, we unfold this variant into GsFolder
 * structs and two hash tables, one with folder ids
 * as keys, and one with app ids.
 */

typedef struct 
{
	gchar *id;
	gchar *name;
	gchar *translated;
	gboolean translate;
	GHashTable *apps;
	GHashTable *categories;
	GHashTable *excluded_apps;
} GsFolder;

struct _GsFolders
{
	GObject parent_instance;

	GSettings *settings;
	GHashTable *folders;
	GHashTable *apps;
	GHashTable *categories;
};

G_DEFINE_TYPE (GsFolders, gs_folders, G_TYPE_OBJECT)

#if 0
static void
dump_set (GHashTable *set)
{
	GHashTableIter iter;
	const gchar *key;

	g_hash_table_iter_init (&iter, set);
	while (g_hash_table_iter_next (&iter, (gpointer *)&key, NULL)) {
		g_print ("\t\t%s\n", key);
	}
}

static void
dump_map (GHashTable *map)
{
	GHashTableIter iter;
	const gchar *key;
	GsFolder *folder;

	g_hash_table_iter_init (&iter, map);
	while (g_hash_table_iter_next (&iter, (gpointer *)&key, (gpointer *)&folder)) {
		g_print ("\t%s -> %s\n", key, folder->id);
	}
}

static void
dump (GsFolders *folders)
{
	GHashTableIter iter;
	GsFolder *folder;

	g_hash_table_iter_init (&iter, folders->folders);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&folder)) {
		g_print ("folder %s\n", folder->id);
		g_print ("\tname %s\n", folder->name);
		g_print ("\ttranslate %d\n", folder->translate);
		if (g_hash_table_size (folder->apps) > 0) {
			g_print ("\tapps\n");
			dump_set (folder->apps);
		}
		if (g_hash_table_size (folder->categories) > 0) {
			g_print ("\tcategories\n");
			dump_set (folder->categories);
		}
		if (g_hash_table_size (folder->excluded_apps) > 0) {
			g_print ("\texcluded\n");
			dump_set (folder->excluded_apps);
		}
	}

	g_print ("app mapping\n");
	dump_map (folders->apps);
	g_print ("category mapping\n");
	dump_map (folders->categories);
}
#endif

static gchar *
lookup_folder_name (const gchar *id)
{
	gchar *name = NULL;
	g_autofree gchar *file = NULL;
	g_autoptr(GKeyFile) key_file = NULL;

	file = g_build_filename ("desktop-directories", id, NULL);
	key_file = g_key_file_new ();
	if (g_key_file_load_from_data_dirs (key_file, file, NULL, G_KEY_FILE_NONE, NULL)) {
       		name = g_key_file_get_locale_string (key_file, "Desktop Entry", "Name", NULL, NULL);
	}
	return name;
}

static GsFolder *
gs_folder_new (const gchar *id, const gchar *name, gboolean translate)
{
	GsFolder *folder;

	folder = g_new0 (GsFolder, 1);
	folder->id = g_strdup (id);
	folder->name = g_strdup (name);
	folder->translate = translate;
	if (translate) {
		folder->translated = lookup_folder_name (name);
	}
	folder->apps = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	folder->categories = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	folder->excluded_apps = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	return folder;
}

static void
gs_folder_free (GsFolder *folder)
{
	g_free (folder->id);
	g_free (folder->name);
	g_free (folder->translated);
	g_hash_table_destroy (folder->apps);
	g_hash_table_destroy (folder->categories);
	g_hash_table_destroy (folder->excluded_apps);
	g_free (folder);
}

static void
load (GsFolders *folders)
{
	GsFolder *folder;
	guint i, j;
	gboolean translate;
	GHashTableIter iter;
	gchar *app;
	gchar *category;
	g_autofree gchar *path = NULL;
	g_auto(GStrv) ids = NULL;

	folders->folders = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify)gs_folder_free);
	folders->apps = g_hash_table_new (g_str_hash, g_str_equal);
	folders->categories = g_hash_table_new (g_str_hash, g_str_equal);

	ids = g_settings_get_strv (folders->settings, "folder-children");
	g_object_get (folders->settings, "path", &path, NULL);
	for (i = 0; ids[i]; i++) {
		g_auto(GStrv) apps = NULL;
		g_auto(GStrv) categories = NULL;
		g_autofree gchar *child_path = NULL;
		g_auto(GStrv) excluded_apps = NULL;
		g_autofree gchar *name = NULL;
		g_autoptr(GSettings) settings = NULL;

		child_path = g_strconcat (path, "folders/", ids[i], "/", NULL);
		settings = g_settings_new_with_path (APP_FOLDER_CHILD_SCHEMA, child_path);
		if (settings == NULL) {
			g_warning ("ignoring folder child %s as invalid", ids[i]);
			continue;
		}
		name = g_settings_get_string (settings, "name");
		translate = g_settings_get_boolean (settings, "translate");
		folder = gs_folder_new (ids[i], name, translate);

		excluded_apps = g_settings_get_strv (settings, "excluded-apps");
		for (j = 0; excluded_apps[j]; j++) {
			g_hash_table_add (folder->excluded_apps, g_strdup (excluded_apps[j]));
		}

		apps = g_settings_get_strv (settings, "apps");
		for (j = 0; apps[j]; j++) {
			if (!g_hash_table_contains (folder->excluded_apps, apps[j]))
				g_hash_table_add (folder->apps, g_strdup (apps[j]));
		}

		categories = g_settings_get_strv (settings, "categories");
		for (j = 0; categories[j]; j++) {
			g_hash_table_add (folder->categories, g_strdup (categories[j]));
		}

		g_hash_table_insert (folders->folders, (gpointer)folder->id, folder);
		g_hash_table_iter_init (&iter, folder->apps);
		while (g_hash_table_iter_next (&iter, (gpointer*)&app, NULL)) {
			g_hash_table_insert (folders->apps, app, folder);
		}

		g_hash_table_iter_init (&iter, folder->categories);
		while (g_hash_table_iter_next (&iter, (gpointer*)&category, NULL)) {
			g_hash_table_insert (folders->categories, category, folder);
		}
	}
}

static void
save (GsFolders *folders)
{
	GHashTableIter iter;
	GsFolder *folder;
	gpointer keys;
	g_autofree gchar *path = NULL;
	g_autofree gpointer apps = NULL;

	g_object_get (folders->settings, "path", &path, NULL);
	g_hash_table_iter_init (&iter, folders->folders);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&folder)) {
		g_autofree gchar *child_path = NULL;
		g_autoptr(GSettings) settings = NULL;

		child_path = g_strconcat (path, "folders/", folder->id, "/", NULL);
		settings = g_settings_new_with_path (APP_FOLDER_CHILD_SCHEMA, child_path);
		g_settings_set_string (settings, "name", folder->name);
		g_settings_set_boolean (settings, "translate", folder->translate);
		keys = g_hash_table_get_keys_as_array (folder->apps, NULL);
		g_settings_set_strv (settings, "apps", (const gchar * const *)keys);
		g_free (keys);

		keys = g_hash_table_get_keys_as_array (folder->excluded_apps, NULL);
		g_settings_set_strv (settings, "excluded-apps", (const gchar * const *)keys);
		g_free (keys);

		keys = g_hash_table_get_keys_as_array (folder->categories, NULL);
		g_settings_set_strv (settings, "categories", (const gchar * const *)keys);
		g_free (keys);
	}

	apps = gs_folders_get_nonempty_folders (folders);
	g_settings_set_strv (folders->settings, "folder-children",
			     (const gchar * const *)apps);
}

static void
clear (GsFolders *folders)
{
	g_hash_table_unref (folders->apps);
	g_hash_table_unref (folders->categories);
	g_hash_table_unref (folders->folders);

	folders->apps = NULL;
	folders->categories = NULL;
	folders->folders = NULL;
}

static void
gs_folders_dispose (GObject *object)
{
	GsFolders *folders = GS_FOLDERS (object);

	g_clear_object (&folders->settings);

	G_OBJECT_CLASS (gs_folders_parent_class)->dispose (object);
}

static void
gs_folders_finalize (GObject *object)
{
	GsFolders *folders = GS_FOLDERS (object);

	clear (folders);

	G_OBJECT_CLASS (gs_folders_parent_class)->finalize (object);
}

static void
gs_folders_class_init (GsFoldersClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = gs_folders_dispose;
	object_class->finalize = gs_folders_finalize;
}

static void
gs_folders_init (GsFolders *folders)
{
	folders->settings = g_settings_new (APP_FOLDER_SCHEMA);
	load (folders);
}

static GsFolders *
gs_folders_new (void)
{
	return GS_FOLDERS (g_object_new (GS_TYPE_FOLDERS, NULL));
}

static GsFolders *singleton;

GsFolders *
gs_folders_get (void)
{
	if (!singleton)
		singleton = gs_folders_new ();

	return g_object_ref (singleton);	
}

gchar **
gs_folders_get_folders (GsFolders *folders)
{
	return (gchar**) g_hash_table_get_keys_as_array (folders->folders, NULL);
}

gchar **
gs_folders_get_nonempty_folders (GsFolders *folders)
{
	GHashTableIter iter;
	GsFolder *folder;
	g_autoptr(GHashTable) tmp = NULL;

	tmp = g_hash_table_new (g_str_hash, g_str_equal);

	g_hash_table_iter_init (&iter, folders->apps);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&folder)) {
		g_hash_table_add (tmp, folder->id);
	}

	g_hash_table_iter_init (&iter, folders->categories);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&folder)) {
		g_hash_table_add (tmp, folder->id);
	}

	return (gchar **) g_hash_table_get_keys_as_array (tmp, NULL);
}

static void
canonicalize_key (gchar *key)
{
	gchar *p;

	for (p = key; *p != 0; p++) {
		gchar c = *p;

		if (c != '-' &&
		    (c < '0' || c > '9') &&
		    (c < 'A' || c > 'Z') &&
		    (c < 'a' || c > 'z'))
		*p = '-';
	}
}

const gchar *
gs_folders_add_folder (GsFolders *folders, const gchar *id)
{
	GsFolder *folder;
	g_autofree gchar *key = NULL;

	key = g_strdup (id);
	canonicalize_key (key);	
	folder = g_hash_table_lookup (folders->folders, key);
	if (!folder) {
		folder = gs_folder_new (key, id, FALSE);
		g_hash_table_insert (folders->folders, folder->id, folder);
	}

	return folder->id;
}

void
gs_folders_remove_folder (GsFolders *folders, const gchar *id)
{
	GsFolder *folder = NULL;
	GHashTableIter iter;

	if (id == NULL)
		return;

	g_hash_table_iter_init (&iter, folders->apps);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer*)&folder)) {
		if (folder && g_strcmp0 (id, folder->id) == 0) {
			g_hash_table_iter_remove (&iter);
		}
	}

	g_hash_table_iter_init (&iter, folders->categories);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer*)&folder)) {
		if (folder && g_strcmp0 (id, folder->id) == 0) {
			g_hash_table_iter_remove (&iter);
		}
	}

	if (folder != NULL)
		g_hash_table_remove (folders->folders, folder->id);
}

const gchar *
gs_folders_get_folder_name (GsFolders *folders, const gchar *id)
{
	GsFolder *folder;

	folder = g_hash_table_lookup (folders->folders, id);

	if (folder) {
		if (folder->translated)
			return folder->translated;

		return folder->name;
	}

	return NULL;
}

void
gs_folders_set_folder_name (GsFolders *folders, const gchar *id, const gchar *name)
{
	GsFolder *folder;

	folder = g_hash_table_lookup (folders->folders, id);
	if (folder) {
		g_free (folder->name);
		g_free (folder->translated);
		folder->name = g_strdup (name);
		folder->translate = FALSE;
	}
}

static GsFolder *
get_app_folder (GsFolders *folders, const gchar *app, GPtrArray *categories)
{
	GsFolder *folder;
	const gchar *category;
	guint i;

	folder = g_hash_table_lookup (folders->apps, app);
	if (!folder && categories) {
		for (i = 0; i < categories->len; i++) {
			category = g_ptr_array_index (categories, i);
			if (category == NULL)
				continue;

			folder = g_hash_table_lookup (folders->categories, category);
			if (folder) {
				break;
			}
		}
	}
	if (folder) {
		if (g_hash_table_contains (folder->excluded_apps, app)) {
			folder = NULL;
		}
	}

	return folder;
}

const gchar *
gs_folders_get_app_folder (GsFolders *folders, const gchar *app, GPtrArray *categories)
{
	GsFolder *folder;

	if (app == NULL)
		return NULL;

	folder = get_app_folder (folders, app, categories);

	return folder ? folder->id : NULL;
}

void
gs_folders_set_app_folder (GsFolders *folders, const gchar *app, GPtrArray *categories, const gchar *id)
{
	GsFolder *folder;

	folder = get_app_folder (folders, app, categories);

	if (folder) {
		g_hash_table_remove (folders->apps, app);
		g_hash_table_remove (folder->apps, app);
	}

	if (id) {
		gchar *app_id;

		app_id = g_strdup (app);
		folder = g_hash_table_lookup (folders->folders, id);
		g_hash_table_add (folder->apps, app_id);
		g_hash_table_remove (folder->excluded_apps, app);
		g_hash_table_insert (folders->apps, app_id, folder);
	} else {
		guint i;
		gchar *category;

		for (i = 0; i < categories->len; i++) {
			category = g_ptr_array_index (categories, i);
			folder = g_hash_table_lookup (folders->categories, category);
			if (folder) {
				g_hash_table_add (folder->excluded_apps, g_strdup (app));
			}
		}
	}
}

void
gs_folders_save (GsFolders *folders)
{
	save (folders);
}

void
gs_folders_revert (GsFolders *folders)
{
	clear (folders);
	load (folders);
}

/* Ensure we have the default folders for Utilities and Sundry.
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
			"Sundry",
			"YaST",
			NULL
		};
		const gchar * const utilities_categories[] = {
			"X-GNOME-Utilities",
			NULL
		};
		const gchar * const utilities_apps[] = {
			"eog.desktop",
			"gnome-system-log.desktop",
			"gnome-system-monitor.desktop",
			"gucharmap.desktop",
			"org.gnome.Calculator.desktop",
			"org.gnome.DejaDup.desktop",
			"org.gnome.Dictionary.desktop",
			"org.gnome.DiskUtility.desktop",
			"org.gnome.Evince.desktop",
			"org.gnome.FileRoller.desktop",
			"org.gnome.font-viewer.desktop",
			"org.gnome.Screenshot.desktop",
			"org.gnome.Terminal.desktop",
			"org.gnome.tweaks.desktop",
			"seahorse.desktop",
			"vinagre.desktop",
			"yelp.desktop",
			NULL
		};
		const gchar * const sundry_categories[] = {
			"X-GNOME-Sundry",
			NULL
		};
		const gchar * const sundry_apps[] = {
			"alacarte.desktop",
			"authconfig.desktop",
			"ca.desrt.dconf-editor.desktop",
			"fedora-release-notes.desktop",
			"firewall-config.desktop",
			"flash-player-properties.desktop",
			"gconf-editor.desktop",
			"gnome-abrt.desktop",
			"ibus-setup-anthy.desktop",
			"ibus-setup.desktop",
			"ibus-setup-hangul.desktop",
			"ibus-setup-libbopomofo.desktop",
			"ibus-setup-libpinyin.desktop",
			"ibus-setup-m17n.desktop",
			"ibus-setup-typing-booster.desktop",
			"im-chooser.desktop",
			"itweb-settings.desktop",
			"jhbuild.desktop",
			"javaws.desktop",
			"java-1.7.0-openjdk-jconsole.desktop",
			"java-1.7.0-openjdk-policytool.desktop",
			"log4j-chainsaw.desktop",
			"log4j-logfactor5.desktop",
			"nm-connection-editor.desktop",
			"org.gnome.PowerStats.desktop",
			"setroubleshoot.desktop",
			"system-config-date.desktop",
			"system-config-firewall.desktop",
			"system-config-keyboard.desktop",
			"system-config-language.desktop",
			"system-config-printer.desktop",
			"system-config-users.desktop",
			"vino-preferences.desktop",
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

		child_path = g_strconcat (path, "folders/Sundry/", NULL);
		child = g_settings_new_with_path (APP_FOLDER_CHILD_SCHEMA, child_path);
		g_settings_set_string (child, "name", "X-GNOME-Sundry.directory");
		g_settings_set_boolean (child, "translate", TRUE);
		g_settings_set_strv (child, "categories", sundry_categories);
		g_settings_set_strv (child, "apps", sundry_apps);

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
