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
	return "hardcoded-featured";
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
 * gs_plugin_add_featured:
 */
gboolean
gs_plugin_add_featured (GsPlugin *plugin,
		        GList **list,
		        GCancellable *cancellable,
		        GError **error)
{
	gboolean ret = TRUE;
	gchar *path;
	GdkPixbuf *pixbuf;
	GsApp *app;
	guint i;
        GDateTime *date;
	const gchar *apps[] = {
                "gimp",
                "org.gnome.Weather.Application",
                "gnome-sudoku",
		NULL
        };

        /* In lieu of a random number generator, just
         * rotate the featured apps, giving each app
         * 3 days apiece.
         */
        date = g_date_time_new_now_utc ();
        i = g_date_time_get_day_of_year (date);
        g_date_time_unref (date);
        i = (i % (G_N_ELEMENTS (apps) * 3)) / 3;

        app = gs_app_new (apps[i]);
	gs_plugin_add_app (list, app);
	path = g_strdup_printf ("%s/gnome-software/featured-%s.png",
				DATADIR, apps[i]);
	pixbuf = gdk_pixbuf_new_from_file_at_scale (path, -1, -1, TRUE, error);
	g_free (path);
	if (pixbuf == NULL) {
		ret = FALSE;
		goto out;
	}
	gs_app_set_featured_pixbuf (app, pixbuf);
	g_object_unref (pixbuf);
out:
	return ret;
}
