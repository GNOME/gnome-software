/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

#include <glib/gi18n.h>

#include <gs-plugin.h>
#include <gs-category.h>

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "hardcoded-popular";
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return -100.0f;
}

/**
 * gs_plugin_add_popular:
 */
gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsApp *app;
	const gchar *apps[] = {
		"transmission-gtk",
		"inkscape",
		"scribus",
		"simple-scan",
		"tomboy",
		"gtg",
		"stellarium",
		"gnome-maps",
		"calibre",
		"hotot-gtk",
		"musique",
		"sol", /* aisleriot */
		"shutter",
		"gnucash",
		"iagno",
		"mozilla-thunderbird",
		"geary",
		"pdfshuffler"
	};
	gint primes[] = {
		 2,  3,  5,  7, 11, 13, 17, 19, 23, 29,
		31, 37, 41, 43, 47, 53, 59, 61, 67, 71,
		73, 79, 83
	};
	GDateTime *date;
	gboolean hit[G_N_ELEMENTS (apps)];
	const gint n = G_N_ELEMENTS (apps);
	gint d, i, k;

	if (g_getenv ("GNOME_SOFTWARE_POPULAR")) {
		gchar **popular;

		popular = g_strsplit (g_getenv ("GNOME_SOFTWARE_POPULAR"), ",", 0);
		for (i = 0; popular[i]; i++) {
			app = gs_app_new (popular[i]);
			gs_plugin_add_app (list, app);
			g_object_unref (app);
		}

		g_strfreev (popular);
		return TRUE;
	}

	date = g_date_time_new_now_utc ();
	d = (((gint)g_date_time_get_day_of_year (date)) % (G_N_ELEMENTS (primes) * 3)) / 3;
	g_date_time_unref (date);

	for (i = 0; i < n; i++) hit[i] = 0;

	i = d % n;
	for (k = 0; k < n; k++) {
		i = (i + primes[d]) % n;
		while (hit[i]) i = (i + 1) % n;
		hit[i] = 1;

		app = gs_app_new (apps[i]);
		gs_plugin_add_app (list, app);
	}
	return TRUE;
}

typedef struct {
  const gchar *category;
  const gchar *app;
} Featured;

static Featured featured[] = {
	{ "Audio", "audacity" },
	{ "Audio", "ardour2" },
	{ "Audio", "gnome-banshee" },
	{ "Audio", "rosegarden" },
	{ "Audio", "sound-juicer" },
	{ "Audio", "rhythmbox" },
	{ "Audio", "brasero" },
	{ "Game", "doom" }, // id ?
	{ "Game", "openarena" },
 	{ "Game", "xonotic" },
	{ "Game", "tremulous" },
	{ "Game", "btanks" },
	{ "Game", "frozen-bubble" },
	{ "Game", "quadrapassel" },
	{ "Game", "sol" },
	{ "Game", "neverball" },
	{ "Game", "gnomine" },
	{ "Game", "wesnoth" },
	{ "Game", "supertuxkart" }, // id ?
	{ "Game", "redeclipse" },
	{ "Office", "evolution" },
	{ "Office", "geary" },
	{ "Office", "gnucash" },
	{ "Office", "abiword" },
	{ "Office", "libreoffice-calc" },
	{ "Office", "libreoffice-writer" },
	{ "Office", "libreoffice-impress" },
	{ "Office", "gnumeric" },
	{ "Office", "gramps" },
	{ "Office", "lyx" },
	{ "System", "gparted" },
	{ "System", "gnome-boxes" },
	{ "System", "virt-manager" },
	{ "System", "gnome-disks" },
	{ "Development", "glade" },
	{ "Development", "anjuta" },
	{ "Development", "d-feet" },
	{ "Development", "eclipse" },
	{ "Development", "gitg" },
	{ "Development", "monodevelop" },
	{ "Development", "gedit" },
	{ "Development", "devhelp" },
	{ "Graphics", "gimp" },
	{ "Graphics", "mypaint" },
	{ "Graphics", "blender" },
	{ "Graphics", "darktable" },
	{ "Graphics", "inkscape" },
	{ "Graphics", "libreoffice-draw" },
	{ "Graphics", "shotwell" },
	{ "Graphics", "scribus" },
	{ "Graphics", "simple-scan" },
	{ "Graphics", "gnome-font-viewer" },
	{ "Science", "stellarium" },
	{ "Science", "octave" },
	{ "Science", "saoimage" },
	{ "Utility", "gnome-documents" },
	{ "Utility", "bijiben" },
	{ "Utility", "gnome-photos" },
	{ "Utility", "workrave" },
	{ "Utility", "gnome-clocks" },
	{ "Education", "celestia" },
	{ "Network", "geary" },
	{ "Network", "mozilla-thunderbird" },
	{ "Network", "firefox" },
	{ "Network", "transmission-gtk" },
	{ "Network", "xchat" },
	{ "Network", "polari" }, // id ?
	{ "Network", "vinagre" },
	{ "Network", "epiphany" },
	{ "Network", "pidgin" },
	{ "Network", "chromium" }, // id ?
	{ "Video", "pitivi" },
	{ "Video", "vlc" }, // id ?
	{ "Video", "totem" },
	{ "Video", "openshot" }, // ?
	{ "Video", "cheese" },
};

gboolean
gs_plugin_add_categories (GsPlugin *plugin,
                          GList **list,
                          GCancellable *cancellable,
                          GError **error)
{
	GList *l;
	GsCategory *parent;
	GsCategory *cat;
	const gchar *id;
	const gchar *last_id;
	guint i;

	cat = NULL;
	last_id = NULL;
	for (i = 0; i < G_N_ELEMENTS (featured); i++) {
		if (g_strcmp0 (last_id, featured[i].category) != 0) {
			last_id = featured[i].category;
			cat = NULL;
			for (l = *list; l; l = l->next) {
				parent = l->data;
				id = gs_category_get_id (parent);
				if (g_strcmp0 (last_id, id) == 0) {
					cat = gs_category_new (parent, "featured", _("Featured"));
					gs_category_add_subcategory (parent, cat);
					break;
				}
			}
		}
		if (cat)
			gs_category_increment_size (cat);
	}

	return TRUE;
}

gboolean
gs_plugin_add_category_apps (GsPlugin *plugin,
			     GsCategory *category,
			     GList **list,
			     GCancellable *cancellable,
			     GError **error)
{
	GsCategory *parent;
	const gchar *id;
	guint i;
	GsApp *app;

	if (g_strcmp0 (gs_category_get_id (category), "featured") != 0)
		return TRUE;

	parent = gs_category_get_parent (category);
	id = gs_category_get_id (parent);

	for (i = 0; i < G_N_ELEMENTS (featured); i++) {
		if (g_strcmp0 (id, featured[i].category) == 0) {
			app = gs_app_new (featured[i].app);
			gs_plugin_add_app (list, app);
		}
	}

	return TRUE;
}

/* vim: set noexpandtab: */
