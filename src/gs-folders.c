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
	GPtrArray *apps;
} GsFolder;

struct GsFoldersPrivate
{
	GSettings *settings;
	GHashTable *folders;
	GHashTable *apps;
};

G_DEFINE_TYPE (GsFolders, gs_folders, G_TYPE_OBJECT)

static GsFolder *
gs_folder_new (const gchar *id, const gchar *name)
{
	GsFolder *folder;

	folder = g_new0 (GsFolder, 1);
	folder->id = g_strdup (id);
	folder->name = g_strdup (name ? name : id);
	folder->apps = g_ptr_array_new_with_free_func (g_free);

	return folder;
}

static void
gs_folder_free (GsFolder *folder)
{
	g_free (folder->id);
	g_free (folder->name);
	g_ptr_array_free (folder->apps, TRUE);
	g_free (folder);
}

static void
gs_folders_class_init (GsFoldersClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_folders_finalize;
	g_type_class_add_private (klass, sizeof (GsFoldersPrivate));
}

static const gchar *
lookup_folder_name (const gchar *id)
{
	/* FIXME - we should consult directory files here.
  	 * The one hardcoded entry here is just to test
  	 * id-name separation.
  	 */
	if (g_strcmp0 (id, "other") == 0)
		return "Sundry";

	return id;
}

static void
load (GsFolders *folders)
{
	GVariant *v;
	GVariantIter iter;
	const gchar *id;
	GVariantIter *apps;
	GsFolder *folder;
	gchar *app;
	guint i;

	folders->priv->folders = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify)gs_folder_free);
	folders->priv->apps = g_hash_table_new (g_str_hash, g_str_equal);

	v = g_settings_get_value (folders->priv->settings, "app-folders");
	g_variant_iter_init (&iter, v);
	while (g_variant_iter_next (&iter, "{&sas}", &id, &apps)) {

		folder = gs_folder_new (id, lookup_folder_name (id));
		while (g_variant_iter_next (apps, "s", &app)) {
			g_ptr_array_add (folder->apps, app);
		}
		
		g_hash_table_insert (folders->priv->folders, (gpointer)folder->id, folder);
		for (i = 0; i < folder->apps->len; i++) {
			g_hash_table_insert (folders->priv->apps, g_ptr_array_index (folder->apps, i), folder);
		}

		g_variant_iter_free (apps);
	}
	g_variant_unref (v);
}

static void
save (GsFolders *folders)
{
	GHashTableIter iter;
	GVariantBuilder builder;
	GsFolder *folder;
	
	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a{sas}"));
	g_hash_table_iter_init (&iter, folders->priv->folders);
	while (g_hash_table_iter_next (&iter, NULL, (gpointer *)&folder)) {
		g_ptr_array_add (folder->apps, NULL);
		g_variant_builder_add (&builder, "{s^as}",
				       folder->id, folder->apps->pdata);
		g_ptr_array_remove (folder->apps, NULL);
	}

	g_settings_set_value (folders->priv->settings, "app-folders", g_variant_builder_end (&builder));
	
}

static void
clear (GsFolders *folders)
{
	g_hash_table_unref (folders->priv->apps);
	g_hash_table_unref (folders->priv->folders);

	folders->priv->apps = NULL;
	folders->priv->folders = NULL;
}

static void
gs_folders_init (GsFolders *folders)
{
	folders->priv = GS_FOLDERS_GET_PRIVATE (folders);

	folders->priv->settings = g_settings_new ("org.gnome.software");
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

const gchar **
gs_folders_get_apps (GsFolders *folders, const gchar *id)
{
	GsFolder *folder;

	folder = g_hash_table_lookup (folders->priv->folders, id);

	return folder ? (const gchar**)folder->apps->pdata : NULL;
}

void
gs_folders_add_folder (GsFolders *folders, const gchar *id)
{
	GsFolder *folder;

	folder = g_hash_table_lookup (folders->priv->folders, id);
	if (!folder) {
		folder = gs_folder_new (id, id);
		g_hash_table_insert (folders->priv->folders, folder->id, folder);
	}
}

void
gs_folders_remove_folder (GsFolders *folders, const gchar *id)
{
	GsFolder *folder;
	guint i;

	if (id == NULL)
		return;

	folder = g_hash_table_lookup (folders->priv->folders, id);
	if (folder) {
		for (i = 0; i < folder->apps->len; i++) {
			g_hash_table_remove (folders->priv->apps, g_ptr_array_index (folder->apps, i));
		}
		g_hash_table_remove (folders->priv->folders, folder->id);
	}
}

const gchar *
gs_folders_get_folder_name (GsFolders *folders, const gchar *id)
{
	GsFolder *folder;

	folder = g_hash_table_lookup (folders->priv->folders, id);

	return folder ? folder->name : NULL;
}

void
gs_folders_set_folder_name (GsFolders *folders, const gchar *id, const gchar *name)
{
	GsFolder *folder;

	folder = g_hash_table_lookup (folders->priv->folders, id);
	if (folder) {
		g_free (folder->name);
		folder->name = g_strdup (name);
	}
}

const gchar *
gs_folders_get_app_folder (GsFolders *folders, const gchar *app)
{
	GsFolder *folder;

	folder = g_hash_table_lookup (folders->priv->apps, app);

	return folder ? folder->id : NULL;
}

void
gs_folders_set_app_folder (GsFolders *folders, const gchar *app, const gchar *id)
{
	GsFolder *folder;
	GsFolder *old_folder = NULL;
	gchar *app_id = NULL;

	g_hash_table_lookup_extended (folders->priv->apps, app, (gpointer *)&app_id, (gpointer *)&old_folder);

	if (old_folder) {
		g_hash_table_remove (folders->priv->apps, app_id);
		g_ptr_array_remove (old_folder->apps, app_id);
	}

	if (id) {
		app_id = g_strdup (app);
		folder = g_hash_table_lookup (folders->priv->folders, id);
		g_ptr_array_add (folder->apps, app_id);
		g_hash_table_insert (folders->priv->apps, app_id, folder);
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
