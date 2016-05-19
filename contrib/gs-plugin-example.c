/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include <gnome-software.h>

/*
 * Compile and install with:
 *
gcc -shared -o libgs_plugin_example.so gs-plugin-example.c -fPIC \
 `pkg-config --libs --cflags gnome-software` \
 -DI_KNOW_THE_GNOME_SOFTWARE_API_IS_SUBJECT_TO_CHANGE &&
 sudo cp libgs_plugin_example.so `pkg-config gnome-software --variable=plugindir`
 */

const gchar **
gs_plugin_order_before (GsPlugin *plugin)
{
	static const gchar *deps[] = { "appstream", NULL };
	return deps;
}

gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      gchar **values,
		      GsAppList *list,
		      GCancellable *cancellable,
		      GError **error)
{
	guint i;
	for (i = 0; values[i] != NULL; i++) {
		if (g_strcmp0 (values[i], "fotoshop") == 0) {
			g_autoptr(GsApp) app = gs_app_new ("gimp.desktop");
			gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
			gs_app_list_add (list, app);
		}
	}
	return TRUE;
}
