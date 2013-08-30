/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
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
                "aisleriot",
                "shutter",
                "gnucash",
                "iagno",
                "thunderbird",
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
