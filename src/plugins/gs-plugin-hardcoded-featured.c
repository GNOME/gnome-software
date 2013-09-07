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
        gchar **apps;
        gsize n_apps;
        GError *local_error = NULL;
        GKeyFile *kf;
        gchar *s;
        const gchar *group;

        apps = NULL;

        path = g_build_filename (DATADIR, "gnome-software", "featured.ini", NULL);
        kf = g_key_file_new ();
        if (!g_key_file_load_from_file (kf, path, 0, &local_error)) {
                g_warning ("Failed to read %s: %s", path, local_error->message);
                ret = FALSE;
                goto out;
        }
        g_free (path);

        apps = g_key_file_get_groups (kf, &n_apps)
;

        /* In lieu of a random number generator, just
         * rotate the featured apps, giving each app
         * 3 days apiece.
         */
        date = g_date_time_new_now_utc ();
        i = g_date_time_get_seconds (date);
        g_date_time_unref (date);
        i = (i % (n_apps * 3)) / 3;
        group = apps[i];

        s = g_key_file_get_string (kf, group, "image", NULL);
	path = g_build_filename (DATADIR, "gnome-software", s, NULL);
        g_free (s);
	pixbuf = gdk_pixbuf_new_from_file_at_scale (path, -1, -1, TRUE, &local_error);
	if (pixbuf == NULL) {
                g_warning ("Failed to load %s: %s", path, local_error->message);
                g_propagate_error (error, local_error);
                g_key_file_unref (kf);
                g_free (path);
		ret = FALSE;
		goto out;
	}

        app = gs_app_new (apps[i]);
	gs_app_set_featured_pixbuf (app, pixbuf);
        gs_app_set_metadata (app, "featured-image-path", path);
        s = g_key_file_get_locale_string (kf, group, "title", NULL, NULL);
        if (s) {
                gs_app_set_metadata (app, "featured-title", s);
                g_free (s);
        }
        s = g_key_file_get_locale_string (kf, group, "subtitle", NULL, NULL);
        if (s) {
                gs_app_set_metadata (app, "featured-subtitle", s);
                g_free (s);
        }
        s = g_key_file_get_string (kf, group, "gradient1", NULL);
        if (s) {
                gs_app_set_metadata (app, "featured-gradient1-color", s);
                g_free (s);
        }
        s = g_key_file_get_string (kf, group, "gradient2", NULL);
        if (s) {
                gs_app_set_metadata (app, "featured-gradient2-color", s);
                g_free (s);
        }
        s = g_key_file_get_string (kf, group, "stroke", NULL);
        if (s) {
                gs_app_set_metadata (app, "featured-stroke-color", s);
                g_free (s);
        }
        s = g_key_file_get_string (kf, group, "text", NULL);
        if (s) {
                gs_app_set_metadata (app, "featured-text-color", s);
                g_free (s);
        }

       	g_object_unref (pixbuf);
	gs_plugin_add_app (list, app);
	g_free (path);

out:
        g_strfreev (apps);

	return ret;
}
