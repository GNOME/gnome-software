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

#include <config.h>

#include <gnome-software.h>

void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* let appstream add applications first */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	guint i;
	const gchar *apps[] = {
		"org.gnome.Builder.desktop",
		"org.gnome.Calculator.desktop",
		"org.gnome.clocks.desktop",
		"org.gnome.Dictionary.desktop",
		"org.gnome.Documents.desktop",
		"org.gnome.Evince.desktop",
		"org.gnome.gedit.desktop",
		"org.gnome.Maps.desktop",
		"org.gnome.Weather.desktop",
		NULL };

	/* we've already got enough popular apps */
	if (gs_app_list_length (list) >= 5)
		return TRUE;

	/* just add all */
	g_debug ("using hardcoded as only %i apps", gs_app_list_length (list));
	for (i = 0; apps[i] != NULL; i++) {
		g_autoptr(GsApp) app = NULL;
		app = gs_app_new (apps[i]);
		gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
		gs_app_list_add (list, app);
	}
	return TRUE;
}
