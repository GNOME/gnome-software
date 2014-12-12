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
	return "hardcoded-categories";
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"menu-spec-categories",	/* Featured subcat added to existing categories*/
		NULL };
	return deps;
}

typedef struct {
  const gchar *category;
  const gchar *app;
} Featured;

static Featured featured[] = {
	{ "Audio", "audacity.desktop" },
	{ "Audio", "ardour2.desktop" },
	{ "Audio", "gnome-banshee.desktop" },
	{ "Audio", "rosegarden.desktop" },
	{ "Audio", "sound-juicer.desktop" },
	{ "Audio", "rhythmbox.desktop" },
	{ "Audio", "brasero.desktop" },
	{ "Game", "doom.desktop" }, // id ?
	{ "Game", "openarena.desktop" },
	{ "Game", "xonotic.desktop" },
	{ "Game", "tremulous.desktop" },
	{ "Game", "btanks.desktop" },
	{ "Game", "frozen-bubble.desktop" },
	{ "Game", "quadrapassel.desktop" },
	{ "Game", "sol.desktop" },
	{ "Game", "neverball.desktop" },
	{ "Game", "gnome-mines.desktop" },
	{ "Game", "wesnoth.desktop" },
	{ "Game", "supertuxkart.desktop" }, // id ?
	{ "Game", "redeclipse.desktop" },
	{ "Game", "gnome-chess.desktop" },
	{ "Office", "evolution.desktop" },
	{ "Office", "geary.desktop" },
	{ "Office", "gnucash.desktop" },
	{ "Office", "abiword.desktop" },
	{ "Office", "libreoffice-calc.desktop" },
	{ "Office", "libreoffice-writer.desktop" },
	{ "Office", "libreoffice-impress.desktop" },
	{ "Office", "gnumeric.desktop" },
	{ "Office", "gramps.desktop" },
	{ "Office", "lyx.desktop" },
	{ "System", "gparted.desktop" },
	{ "System", "org.gnome.Boxes.desktop" },
	{ "System", "virt-manager.desktop" },
	{ "System", "gnome-disks.desktop" },
	{ "Development", "devassistant.desktop" },
	{ "Development", "glade.desktop" },
	{ "Development", "anjuta.desktop" },
	{ "Development", "d-feet.desktop" },
	{ "Development", "eclipse.desktop" },
	{ "Development", "gitg.desktop" },
	{ "Development", "monodevelop.desktop" },
	{ "Development", "org.gnome.gedit.desktop" },
	{ "Development", "devhelp.desktop" },
	{ "Graphics", "gimp.desktop" },
	{ "Graphics", "mypaint.desktop" },
	{ "Graphics", "blender.desktop" },
	{ "Graphics", "darktable.desktop" },
	{ "Graphics", "inkscape.desktop" },
	{ "Graphics", "libreoffice-draw.desktop" },
	{ "Graphics", "shotwell.desktop" },
	{ "Graphics", "scribus.desktop" },
	{ "Graphics", "simple-scan.desktop" },
	{ "Graphics", "org.gnome.font-viewer.desktop" },
	{ "Science", "stellarium.desktop" },
	{ "Science", "octave.desktop" },
	{ "Science", "saoimage.desktop" },
	{ "Utility", "org.gnome.Documents.desktop" },
	{ "Utility", "bijiben.desktop" },
	{ "Utility", "org.gnome.Photos.desktop" },
	{ "Utility", "workrave.desktop" },
	{ "Utility", "org.gnome.clocks.desktop" },
	{ "Education", "celestia.desktop" },
	{ "Network", "geary.desktop" },
	{ "Network", "mozilla-thunderbird.desktop" },
	{ "Network", "firefox.desktop" },
	{ "Network", "transmission-gtk.desktop" },
	{ "Network", "xchat.desktop" },
	{ "Network", "org.gnome.Polari.desktop" }, // id ?
	{ "Network", "vinagre.desktop" },
	{ "Network", "epiphany.desktop" },
	{ "Network", "pidgin.desktop" },
	{ "Network", "chromium.desktop" }, // id ?
	{ "Video", "pitivi.desktop" },
	{ "Video", "vlc.desktop" }, // id ?
	{ "Video", "org.gnome.Totem.desktop" },
	{ "Video", "openshot.desktop" }, // ?
	{ "Video", "org.gnome.Cheese.desktop" },
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
					g_object_unref (cat);
					break;
				}
			}
		}
		if (cat != NULL)
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

	if (g_strcmp0 (gs_category_get_id (category), "featured") != 0)
		return TRUE;

	parent = gs_category_get_parent (category);
	id = gs_category_get_id (parent);

	for (i = 0; i < G_N_ELEMENTS (featured); i++) {
		if (g_strcmp0 (id, featured[i].category) == 0) {
			_cleanup_object_unref_ GsApp *app = NULL;
			app = gs_app_new (featured[i].app);
			gs_plugin_add_app (list, app);
		}
	}

	return TRUE;
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList **list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	const gchar *id;
	GList *l;
	GsApp *app;
	guint i;

	for (i = 0; i < G_N_ELEMENTS (featured); i++) {
		for (l = *list; l != NULL; l = l->next) {
			app = GS_APP (l->data);
			id = gs_app_get_id (app);
			if (g_strcmp0 (id, featured[i].app) != 0)
				continue;
			gs_app_add_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED);
		}
	}

	return TRUE;

}

/* vim: set noexpandtab: */
