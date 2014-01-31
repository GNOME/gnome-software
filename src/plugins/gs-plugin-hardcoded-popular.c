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
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* check that we are running on Fedora */
	if (gs_plugin_check_distro_id (plugin, "fedora")) {
		gs_plugin_set_enabled (plugin, FALSE);
		g_debug ("disabling '%s' as we're Fedora and have tagger", plugin->name);
		return;
	}
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
		"transmission-gtk.desktop",
		"inkscape.desktop",
		"scribus.desktop",
		"simple-scan.desktop",
		"tomboy.desktop",
		"gtg.desktop",
		"stellarium.desktop",
		"gnome-maps.desktop",
		"calibre.desktop",
		"hotot-gtk.desktop",
		"musique.desktop",
		"sol.desktop", /* aisleriot */
		"shutter.desktop",
		"gnucash.desktop",
		"iagno.desktop",
		"mozilla-thunderbird.desktop",
		"geary.desktop",
		"pdfshuffler.desktop"
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

/* vim: set noexpandtab: */
