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

#include <config.h>

#include <gs-plugin.h>

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "datadir-apps";
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 1.0f;
}

/**
 * gs_plugin_datadir_apps_extract_desktop_data:
 */
static gboolean
gs_plugin_datadir_apps_extract_desktop_data (GsPlugin *plugin,
					     GsApp *app,
					     const gchar *desktop_file,
					     GError **error)
{
	const gchar *basename_tmp = NULL;
	gboolean ret;
	gchar *basename = NULL;
	gchar *comment = NULL;
	gchar *name = NULL;
	gchar *icon = NULL;
	GKeyFile *key_file;
	GdkPixbuf *pixbuf = NULL;

	/* load desktop file */
	key_file = g_key_file_new ();
	ret = g_key_file_load_from_file (key_file,
					 desktop_file,
					 G_KEY_FILE_NONE,
					 error);
	if (!ret)
		goto out;

	/* get desktop name */
	name = g_key_file_get_string (key_file,
				      G_KEY_FILE_DESKTOP_GROUP,
				      G_KEY_FILE_DESKTOP_KEY_NAME,
				      NULL);
	if (name != NULL && name[0] != '\0')
		gs_app_set_name (app, name);

	/* get desktop summary */
	comment = g_key_file_get_string (key_file,
					 G_KEY_FILE_DESKTOP_GROUP,
					 G_KEY_FILE_DESKTOP_KEY_COMMENT,
					 NULL);
	if (comment != NULL && comment[0] != '\0')
		gs_app_set_summary (app, comment);

	/* get desktop icon */
	icon = g_key_file_get_string (key_file,
				      G_KEY_FILE_DESKTOP_GROUP,
				      G_KEY_FILE_DESKTOP_KEY_ICON,
				      NULL);
	if (icon == NULL)
		icon = g_strdup (GTK_STOCK_MISSING_IMAGE);

	/* set pixbuf */
	if (icon[0] == '/') {
		pixbuf = gdk_pixbuf_new_from_file_at_size (icon,
							   plugin->pixbuf_size,
							   plugin->pixbuf_size,
							   NULL);
	} else {
		pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
						   icon,
						   plugin->pixbuf_size,
						   GTK_ICON_LOOKUP_USE_BUILTIN |
						   GTK_ICON_LOOKUP_FORCE_SIZE,
						   NULL);
	}
	if (pixbuf != NULL)
		gs_app_set_pixbuf (app, pixbuf);

	/* set new id */
	basename = g_path_get_basename (desktop_file);
	g_strdelimit (basename, ".", '\0');
	basename_tmp = basename;
	if (g_str_has_prefix (basename_tmp, "fedora-"))
		basename_tmp += 7;
	g_debug ("setting new id for %s to %s",
		 gs_app_get_id (app), basename_tmp);
	gs_app_set_id (app, basename_tmp);

	/* mark as an application */
	gs_app_set_kind (app, GS_APP_KIND_NORMAL);
out:
	g_key_file_unref (key_file);
	if (pixbuf != NULL)
		g_object_unref (pixbuf);
	g_free (basename);
	g_free (icon);
	g_free (name);
	g_free (comment);
	return ret;
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin, GList *list, GError **error)
{
	const gchar *tmp;
	gboolean ret = TRUE;
	GList *l;
	GsApp *app;

	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_name (app) != NULL)
			continue;
		tmp = gs_app_get_metadata_item (app, "datadir-desktop-filename");
		if (tmp == NULL)
			continue;
		ret = gs_plugin_datadir_apps_extract_desktop_data (plugin,
								   app,
								   tmp,
								   error);
		if (!ret)
			goto out;
	}
out:
	return ret;
}
