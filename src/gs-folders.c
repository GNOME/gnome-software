/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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
#include <gio/gio.h>

#include "gs-folders.h"

#define APP_FOLDER_SCHEMA       "org.gnome.desktop.app-folders"
#define APP_FOLDER_CHILD_SCHEMA "org.gnome.desktop.app-folders.folder"

static void	gs_folders_finalize	(GObject	*object);

#define GS_FOLDERS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_FOLDERS, GsFoldersPrivate))

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

struct GsFoldersPrivate
{
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

	g_hash_table_iter_init (&iter, folders->priv->folders);
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
	dump_map (folders->priv->apps);
	g_print ("category mapping\n");
	dump_map (folders->priv->categories);
}
#endif

static gchar *
lookup_folder_name (const gchar *id)
{
	gchar *name = NULL;
	GKeyFile *key_file;
	gchar *file;

	file = g_build_filename ("desktop-directories", id, NULL);
	key_file = g_key_file_new ();
	if (g_key_file_load_from_data_dirs (key_file, file, NULL, G_KEY_FILE_NONE, NULL)) {
       		name = g_key_file_get_locale_string (key_file, "Desktop Entry", "Name", NULL, NULL);
	}

	g_free (file);
	g_key_file_unref (key_file);

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
gs_folders_class_init (GsFoldersClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_folders_finalize;
	g_type_class_add_private (klass, sizeof (GsFoldersPrivate));
}

static void
load (GsFolders *folders)
{
	GsFolder *folder;
	gchar **ids;
	gchar **apps;
	gchar **excluded_apps;
	gchar **categories;
	guint i, j;
	gchar *name;
        gchar *path;
        gchar *child_path;
        GSettings *settings;
	gboolean translate;
	GHashTableIter iter;
	gchar *app;
	gchar *category;

	folders->priv->folders = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify)gs_folder_free);
	folders->priv->apps = g_hash_table_new (g_str_hash, g_str_equal);
	folders->priv->categories = g_hash_table_new (g_str_hash, g_str_equal);

	ids = g_settings_get_strv (folders->priv->settings, "folder-children");
        g_object_get (folders->priv->settings, "path", &path, NULL);
	for (i = 0; ids[i]; i++) {
                child_path = g_strconcat (path, "folders/", ids[i], "/", NULL);
                settings = g_settings_new_with_path (APP_FOLDER_CHILD_SCHEMA, child_path);
                name = g_settings_get_string (settings, "name");
		translate = g_settings_get_boolean (settings, "translate");
		folder = gs_folder_new (ids[i], name, translate);

		excluded_apps = g_settings_get_strv (settings, "excluded-apps");
		for (j = 0; excluded_apps[j]; j++) {
			g_hash_table_add (folder->excluded_apps, excluded_apps[j]);
		}

		apps = g_settings_get_strv (settings, "apps");
		for (j = 0; apps[j]; j++) {
			if (!g_hash_table_contains (folder->excluded_apps, apps[j]))
				g_hash_table_add (folder->apps, apps[j]);
		}

		categories = g_settings_get_strv (settings, "categories");
		for (j = 0; categories[j]; j++) {
			g_hash_table_add (folder->categories, categories[j]);
		}

		g_hash_table_insert (folders->priv->folders, (gpointer)folder->id, folder);
		g_hash_table_iter_init (&iter, folder->apps);
		while (g_hash_table_iter_next (&iter, (gpointer*)&app, NULL)) {
			g_hash_table_insert (folders->priv->apps, app, folder);
		}

		g_hash_table_iter_init (&iter, folder->categories);
		while (g_hash_table_iter_next (&iter, (gpointer*)&category, NULL)) {
			g_hash_table_insert (folders->priv->categories, category, folder);
		}

		g_free (apps);
		g_free (excluded_apps);
		g_free (categories);
		g_free (name);
		g_object_unref (settings);
                g_free (child_path);
	}
	g_strfreev (ids);
}

static void
save (GsFolders *folders)
{
	GHashTableIter iter;
	GsFolder *folder;
	gpointer apps;
	gchar *path;
        gchar *child_path;
	GSettings *settings;
	gpointer keys;

        g_object_get (folders->priv->settings, "path", &path, NULL);
	g_hash_table_iter_init (&iter, folders->priv->folders);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&folder)) {
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

		g_object_unref (settings);
		g_free (child_path);
	}
	g_free (path);

	apps = g_hash_table_get_keys_as_array (folders->priv->folders, NULL);
	g_settings_set_strv (folders->priv->settings, "folder-children",
                             (const gchar * const *)apps);
	g_free (apps);
}

static void
clear (GsFolders *folders)
{
	g_hash_table_unref (folders->priv->apps);
	g_hash_table_unref (folders->priv->categories);
	g_hash_table_unref (folders->priv->folders);

	folders->priv->apps = NULL;
	folders->priv->categories = NULL;
	folders->priv->folders = NULL;
}

static void
gs_folders_init (GsFolders *folders)
{
	folders->priv = GS_FOLDERS_GET_PRIVATE (folders);

	folders->priv->settings = g_settings_new (APP_FOLDER_SCHEMA);
	load (folders);
}

static void
gs_folders_finalize (GObject *object)
{
	GsFolders *folders = GS_FOLDERS (object);

	clear (folders);
	g_object_unref (folders->priv->settings);

	G_OBJECT_CLASS (gs_folders_parent_class)->finalize (object);
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
	return (gchar**) g_hash_table_get_keys_as_array (folders->priv->folders, NULL);
}

gchar **
gs_folders_get_nonempty_folders (GsFolders *folders)
{
	GHashTable *tmp;
	GHashTableIter iter;
	GsFolder *folder;
	gchar **keys;

	tmp = g_hash_table_new (g_str_hash, g_str_equal);

	g_hash_table_iter_init (&iter, folders->priv->apps);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&folder)) {
		g_hash_table_add (tmp, folder->id);
	}

	g_hash_table_iter_init (&iter, folders->priv->categories);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&folder)) {
		g_hash_table_add (tmp, folder->id);
	}

	keys = (gchar **) g_hash_table_get_keys_as_array (tmp, NULL);
	g_hash_table_destroy (tmp);

	return keys;
}

static void
canonicalize_key (gchar *key)
{
  gchar *p;

  for (p = key; *p != 0; p++)
    {
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
	gchar *key;

	key = g_strdup (id);
	canonicalize_key (key);	
	folder = g_hash_table_lookup (folders->priv->folders, key);
	if (!folder) {
		folder = gs_folder_new (key, id, FALSE);
		g_hash_table_insert (folders->priv->folders, folder->id, folder);
	}
	g_free (key);

	return folder->id;
}

void
gs_folders_remove_folder (GsFolders *folders, const gchar *id)
{
	GsFolder *folder = NULL;
	GHashTableIter iter;

	if (id == NULL)
		return;

	g_hash_table_iter_init (&iter, folders->priv->apps);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer*)folder)) {
		if (folder && g_strcmp0 (id, folder->id) == 0) {
			g_hash_table_iter_remove (&iter);
		}
	}

	g_hash_table_iter_init (&iter, folders->priv->categories);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer*)folder)) {
		if (folder && g_strcmp0 (id, folder->id) == 0) {
			g_hash_table_iter_remove (&iter);
		}
	}

	if (folder != NULL)
		g_hash_table_remove (folders->priv->folders, folder->id);
}

const gchar *
gs_folders_get_folder_name (GsFolders *folders, const gchar *id)
{
	GsFolder *folder;

	folder = g_hash_table_lookup (folders->priv->folders, id);

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

	folder = g_hash_table_lookup (folders->priv->folders, id);
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
	guint i;

	folder = g_hash_table_lookup (folders->priv->apps, app);
	if (!folder && categories) {
		for (i = 0; i < categories->len; i++) {
			folder = g_hash_table_lookup (folders->priv->categories, g_ptr_array_index (categories, i));
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

	folder = get_app_folder (folders, app, categories);

	return folder ? folder->id : NULL;
}

void
gs_folders_set_app_folder (GsFolders *folders, const gchar *app, GPtrArray *categories, const gchar *id)
{
	GsFolder *folder;

	folder = get_app_folder (folders, app, categories);

	if (folder) {
		g_hash_table_remove (folders->priv->apps, app);
		g_hash_table_remove (folder->apps, app);
	}

	if (id) {
		gchar *app_id;

		app_id = g_strdup (app);
		folder = g_hash_table_lookup (folders->priv->folders, id);
		g_hash_table_add (folder->apps, app_id);
		g_hash_table_remove (folder->excluded_apps, app);
		g_hash_table_insert (folders->priv->apps, app_id, folder);
	} else {
		guint i;
		gchar *category;

		for (i = 0; i < categories->len; i++) {
			category = g_ptr_array_index (categories, i);
			folder = g_hash_table_lookup (folders->priv->categories, category);
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

/* vim: set noexpandtab: */
