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

        group = NULL;

#ifdef DEBUG
        if (g_getenv ("GNOME_SOFTWARE_FEATURED")) {
                const gchar *featured;
                featured = g_getenv ("GNOME_SOFTWARE_FEATURED");
                for (i = 0; apps[i]; i++) {
                        if (g_strcmp0 (apps[i], featured) == 0) {
                                group = featured;
                                break;
                        }
                }
        }
#endif

        if (!group) {
                /* In lieu of a random number generator, just
                 * rotate the featured apps, giving each app
                 * 3 days apiece.
                 */
                date = g_date_time_new_now_utc ();
                i = g_date_time_get_day_of_year (date);
                g_date_time_unref (date);
                i = (i % (n_apps * 3)) / 3;
                group = apps[i];
        }

        app = gs_app_new (group);
        s = g_key_file_get_string (kf, group, "background", NULL);
        if (s) {
                gs_app_set_metadata (app, "Featured::background", s);
                g_free (s);
        }
        s = g_key_file_get_string (kf, group, "stroke", NULL);
        if (s) {
                gs_app_set_metadata (app, "Featured::stroke-color", s);
                g_free (s);
        }
        s = g_key_file_get_string (kf, group, "text", NULL);
        if (s) {
                gs_app_set_metadata (app, "Featured::text-color", s);
                g_free (s);
        }
	gs_plugin_add_app (list, app);

out:
        g_strfreev (apps);

	return ret;
}
