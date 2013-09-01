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

struct GsPluginPrivate {
	GHashTable		*cache;
	gboolean		 loaded;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "hardcoded-descriptions";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->loaded = FALSE;
	plugin->priv->cache = g_hash_table_new_full (g_str_hash,
						     g_str_equal,
						     NULL,
						     NULL);
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
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_hash_table_unref (plugin->priv->cache);
}

/**
 * gs_plugin_hardcoded_descriptions_add:
 */
static gboolean
gs_plugin_hardcoded_descriptions_add (GsPlugin *plugin, GError **error)
{
	guint i;
	static struct {
		const gchar *id;
		const gchar *desc;
	} descriptions[] = {
		{ "inkscape",		"An Open Source vector graphics editor, with capabilities similar to Illustrator, CorelDraw, or Xara X, using the W3C standard Scalable Vector Graphics (SVG) file format. Inkscape supports many advanced SVG features (markers, clones, alpha blending, etc.) and great care is taken in designing a streamlined interface. It is very easy to edit nodes, perform complex path operations, trace bitmaps and much more. We also aim to maintain a thriving user and developer community by using open, community-oriented development."},
		{ "gnome-boxes",	"A simple GNOME 3 application to access remote or virtual systems. While virt-manager does a very good job as a virtual machine management software, its very much tailored for system administration and virtual machines. Boxes on the other hand is targeted towards typical desktop end-user who either just want a very safe and easy way to try out new operating systems or new (potentially unstable) versions of her/his favorite operating system(s), or need to connect to a remote machine (home-office connection being a typical use-case). For this reason, Boxes will not provide many of the advanced options to tweak virtual machines provided by virt-manager. Instead Boxes will focus on getting things working out of the box with very little input from user."},
		{ "gedit",		"gedit is the official text editor of the GNOME desktop environment.\nWhile aiming at simplicity and ease of use, gedit is a powerful general purpose text editor.\ngedit features also a flexible plugin system which can be used to dynamically add new advanced features to gedit itself."},
		{ "cheese",		"Cheese uses your webcam to take photos and videos, applies fancy special effects and lets you share the fun with others. Under the hood, Cheese uses GStreamer to apply fancy effects to photos and videos. With Cheese it is easy to take photos of you, your friends, pets or whatever you want and share them with others."},
		{ "transmission-gtk",	"Transmission is an open source, volunteer-based project. Unlike some BitTorrent clients, Transmission doesn't play games with its users to make money.\nTransmission is designed for easy, powerful use. We've set the defaults to Just Work and it only takes a few clicks to configure advanced features like watch directories, bad peer blocklists, and the web interface.\nTransmission also has the lowest memory footprint of any major BitTorrent client."},
		{ "firefox",		"Bringing together all kinds of awesomeness to make browsing better for you.\nGet to your favorite sites quickly – even if you don’t remember the URLs. Type your term into the location bar (aka the Awesome Bar) and the autocomplete function will include possible matches from your browsing history, bookmarked sites and open tabs."},
		{ "abiword",		"AbiWord is a free word processing program similar to Microsoft® Word. It is suitable for a wide variety of word processing tasks.\nAbiWord allows you to collaborate with multiple people on one document at the same time. It is tightly integrated with the AbiCollab.net web service, which lets you store documents online, allows easy document sharing with your friends, and performs format conversions on the fly."},
		{ "file-roller",	"File Roller is an archive manager for the GNOME environment. With File Roller you can:\n* Create and modify archives.\n* View the content of an archive.\n* View and modify a file contained in the archive.\n* Extract files from the archive.\n* Save the archive in a different format."},
		{ "gnome-abrt",		"Collection of software tools designed for collecting, analyzing and reporting of software issues."},
		{ "darktable",		"darktable is an open source photography workflow application and RAW developer. A virtual lighttable and darkroom for photographers. It manages your digital negatives in a database, lets you view them through a zoomable lighttable and enables you to develop raw images and enhance them."},
		{ "devhelp",		"Devhelp is an API documentation browser for GTK+ and GNOME. It works natively with gtk-doc (the API reference framework developed for GTK+ and used throughout GNOME for API documentation). If you use gtk-doc with your project, you can use Devhelp to browse the documentation."},
		{ "evolution",		"Evolution provides integrated mail, addressbook and calendaring functionality to users of the GNOME desktop."},
		{ "gimp",		"GIMP is an acronym for GNU Image Manipulation Program. It is a freely distributed program for such tasks as photo retouching, image composition and image authoring.\nIt has many capabilities. It can be used as a simple paint program, an expert quality photo retouching program, an online batch processing system, a mass production image renderer, an image format converter, etc.\nGIMP is expandable and extensible. It is designed to be augmented with plug-ins and extensions to do just about anything. The advanced scripting interface allows everything from the simplest task to the most complex image manipulation procedures to be easily scripted."},
		{ "geany",		"Geany is a small and lightweight Integrated Development Environment. It was developed to provide a small and fast IDE, which has only a few dependencies from other packages. Another goal was to be as independent as possible from a special Desktop Environment like KDE or GNOME - Geany only requires the GTK2 runtime libraries."},
		{ "gtg",		"Getting Things GNOME! (GTG) is a personal tasks and TODO-list items organizer for the GNOME desktop environment inspired by the Getting Things Done (GTD) methodology. GTG is designed with flexibility, adaptability, and ease of use in mind so it can be used as more than just GTD software.\nGTG is intended to help you track everything you need to do and need to know, from small tasks to large projects."},
		{ "gnote",		"Gnote is a port of Tomboy to C++.\nIt is the same note taking application, including most of the add-ins (more are to come). Synchronization support is being worked on."},
		{ "gnumeric",		"The Gnumeric spreadsheet is part of the GNOME desktop environment: a project to create a free, user friendly desktop environment.\nThe goal of Gnumeric is to be the best possible spreadsheet. We are not attempting to clone existing applications. However, Gnumeric can read files saved with other spreadsheets and we offer a customizable feel that attempts to minimize the costs of transition."},
		{ "gramps",		"Gramps is a free software project and community. We strive to produce a genealogy program that is both intuitive for hobbyists and feature-complete for professional genealogists. It is a community project, created, developed and governed by genealogists."},
		{ "orca",		"Orca is a free, open source, flexible, and extensible screen reader that provides access to the graphical desktop via user-customizable combinations of speech and/or braille.\nOrca works with applications and toolkits that support the assistive technology service provider interface (AT-SPI), which is the primary assistive technology infrastructure for the Solaris and Linux operating environments. Applications and toolkits supporting the AT-SPI include the GNOME GTK+ toolkit, the Java platform's Swing toolkit, SWT, OpenOffice/LibreOffice, Mozilla, and WebKitGtk. AT-SPI support for the KDE Qt toolkit is currently being pursued."},
		{ "rhythmbox",		"Rhythmbox is an integrated music management application, originally inspired by Apple's iTunes. It is free software, designed to work well under the GNOME Desktop, and based on the powerful GStreamer media framework.."},
		{ NULL,		NULL}
	};

	/* add each one to a hash table */
	for (i = 0; descriptions[i].id != NULL; i++) {
		g_hash_table_insert (plugin->priv->cache,
				     (gpointer) descriptions[i].id,
				     (gpointer) descriptions[i].desc);
	}

	plugin->priv->loaded = TRUE;
	return TRUE;
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList *list,
		  GCancellable *cancellable,
		  GError **error)
{
	const gchar *id;
	const gchar *value;
	gboolean ret = TRUE;
	GList *l;
	GsApp *app;

	/* already loaded */
	if (!plugin->priv->loaded) {
		ret = gs_plugin_hardcoded_descriptions_add (plugin, error);
		if (!ret)
			goto out;
	}

	/* add any missing descriptions data */
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_description (app) != NULL)
			continue;
		id = gs_app_get_id (app);
		if (id == NULL)
			continue;
		value = g_hash_table_lookup (plugin->priv->cache, id);
		if (value != NULL)
			gs_app_set_description (app, value);
	}
out:
	return ret;
}
