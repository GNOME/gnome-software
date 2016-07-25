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

struct {
	const gchar *id;
	const gchar *css;
} myapps[] = {
	{ "ardour2.desktop",
		"border-color: #333333;\n"
		"text-shadow: 0 1px 1px rgba(0,0,0,0.5);\n"
		"color: #ffffff;\n"
		"outline-offset: 0;\n"
		"outline-color: alpha(#ffffff, 0.75);\n"
		"outline-style: dashed;\n"
		"outline-offset: 2px;\n"
		"background:"
		" url('@datadir@/gnome-software/featured-ardour.png')"
		" 30% 49% / 33% no-repeat,"
		" url('@datadir@/gnome-software/featured-ardour-bg.png')"
		" center center / 100% auto no-repeat,"
		" linear-gradient(to bottom, #373936, #60625e);" },
	{ "blender.desktop",
		"border-color: #783d03;\n"
		"text-shadow: 0 1px 1px rgba(0,0,0,0.5);\n"
		"color: #ffffff;\n"
		"outline-offset: 0;\n"
		"outline-color: alpha(#ffffff, 0.75);\n"
		"outline-style: dashed;\n"
		"outline-offset: 2px;\n"
		"background:"
		" url('@datadir@/gnome-software/featured-blender.png')"
		" 10% 40% / 50% auto no-repeat, -gtk-gradient (radial,"
		" center bottom, 0, center center, 1,"
		" from(#fcbf83), to(#c06105));" },
	{ "gnome-chess.desktop",
		"border-color: #2e3436;\n"
		"text-shadow: 0 1px 1px rgba(0,0,0,0.5);\n"
		"color: #ffffff;\n"
		"outline-offset: 0;\n"
		"outline-color: alpha(#ffffff, 0.75);\n"
		"outline-style: dashed;\n"
		"outline-offset: 2px;\n"
		"background:"
		" url('@datadir@/gnome-software/featured-chess.png')"
		" 10% center / 40% auto no-repeat,"
		" linear-gradient(to bottom, #555753, #888a85);" },
	{ "firefox.desktop",
		"border-color: #babdb6;\n"
		"text-shadow: 0 1px 1px rgba(255,255,255,0.7);\n"
		"color: #888a85;\n"
		"outline-offset: 0;\n"
		"outline-color: alpha(#888a85, 0.75);\n"
		"outline-style: dashed;\n"
		"outline-offset: 2px;\n"
		"background:"
		" url('@datadir@/gnome-software/featured-firefox.png')"
		" 10% center / 40% auto no-repeat,"
		" linear-gradient(to bottom, #d3d7cf, #eeeeec);" },
	{ "gimp.desktop",
		"border-color: #2a6c10;\n"
		"text-shadow: 0 1px 1px rgba(255,255,255,0.7);\n"
		"color: #333;\n"
		"outline-offset: 0;\n"
		"outline-color: alpha(#333, 0.75);\n"
		"outline-style: dashed;\n"
		"outline-offset: 2px;\n"
		"background:"
		" url('@datadir@/gnome-software/featured-gimp.png')"
		" left 50% / 50% auto no-repeat,"
		" linear-gradient(to bottom, #8ac674, #cbddc3);" },
	{ "inkscape.desktop",
		"border-color: #819a6b;\n"
		"text-shadow: none;\n"
		"color: #606060;\n"
		"outline-offset: 0;\n"
		"outline-color: alpha(#ffffff, 0.75);\n"
		"outline-style: dashed;\n"
		"outline-offset: 2px;\n"
		" background:"
		" url('@datadir@/gnome-software/featured-inkscape.svg')"
		" 20% / 60% auto no-repeat,"
		" linear-gradient(to bottom, #ffffff, #e2e2e2);" },
	{ "mypaint.desktop",
		"border-color: #4c52aa;\n"
		"color: #362d89;\n"
		"outline-offset: 0;\n"
		"outline-color: alpha(#362d89, 0.75);\n"
		"outline-style: dashed;\n"
		"outline-offset: 2px;\n"
		"background:"
		" url('@datadir@/gnome-software/featured-mypaint.png')"
		" left 67% / 50% auto no-repeat,"
		" linear-gradient(to bottom, #8fa5d9, #d8e0ef);" },
	{ "org.gnome.Polari.desktop",
		"border-color: #4e9a06;\n"
		"text-shadow: 0 2px #418e64;\n"
		"color: #a8c74f;\n"
		"outline-offset: 0;\n"
		"outline-color: alpha(#a8c74f, 0.75);\n"
		"outline-style: dashed;\n"
		"outline-offset: 2px;\n"
		"background:"
		" url('@datadir@/gnome-software/featured-polari.svg')"
		" 70% 80% / 120% auto no-repeat, #43a570;" },
	{ "org.gnome.Weather.Application.desktop",
		"border-color: #d8e0ef;\n"
		"text-shadow: 0 1px 1px rgba(0,0,0,0.5);\n"
		"color: #ffffff;\n"
		"outline-offset: 0;\n"
		"outline-color: alpha(#ffffff, 0.75);\n"
		"outline-style: dashed;\n"
		"outline-offset: 2px;\n"
		"background:"
		" url('@datadir@/gnome-software/featured-weather.png')"
		" left 80% / 50% auto no-repeat,"
		" url('@datadir@/gnome-software/featured-weather-bg.png'),"
		" linear-gradient(to bottom, #25486d, #6693ce);" },
	{ "transmission-gtk.desktop",
		"border-color: #a40000;\n"
		"text-shadow: 0 1px 1px rgba(0,0,0,0.5);\n"
		"color: #ffffff;\n"
		"outline-offset: 0;\n"
		"outline-color: alpha(#ffffff, 0.75);\n"
		"outline-style: dashed;\n"
		"outline-offset: 2px;\n"
		"background:"
		" url('@datadir@/gnome-software/featured-transmission.png')"
		" 10% 20% / 427px auto no-repeat, -gtk-gradient (radial,"
		" center bottom, 0, center center, 0.8,"
		" from(#ffc124), to(#b75200));" },
	{ "org.gnome.Builder.desktop",
		"border-color: #000000;\n"
		"text-shadow: 0 1px 1px rgba(0,0,0,0.5);\n"
		"color: #ffffff;\n"
		"outline-offset: 0;\n"
		"outline-color: alpha(#ffffff, 0.75);\n"
		"outline-style: dashed;\n"
		"outline-offset: 2px;\n"
		"background:"
		" url('@datadir@/gnome-software/featured-builder.png')"
		" left center / 100% auto no-repeat,"
		" url('@datadir@/gnome-software/featured-builder-bg.jpg')"
		" center / cover no-repeat;" },
	{ "org.gnome.Maps.desktop",
		"border-color: #ff0000;\n"
		"text-shadow: 0 1px 1px rgba(255,255,255,0.5);\n"
		"color: #000000;\n"
		"outline-offset: 0;\n"
		"outline-color: alpha(#000000, 0.75);\n"
		"outline-style: dashed;\n"
		"outline-offset: 2px;\n"
		"background:"
		" url('@datadir@/gnome-software/featured-maps.png')"
		" left -10px / 352px auto no-repeat,"
		" url('@datadir@/gnome-software/featured-maps-bg.png')"
		" bottom center / contain no-repeat;" },
	{ NULL, NULL }
};

gboolean
gs_plugin_add_featured (GsPlugin *plugin,
			GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	guint i;

	/* we've already got enough featured apps */
	if (gs_app_list_length (list) >= 5)
		return TRUE;

	/* just add all */
	g_debug ("using hardcoded as only %u apps", gs_app_list_length (list));
	for (i = 0; myapps[i].id != NULL; i++) {
		g_autoptr(GsApp) app = NULL;

		/* look in the cache */
		app = gs_plugin_cache_lookup (plugin, myapps[i].id);
		if (app != NULL) {
			gs_app_list_add (list, app);
			continue;
		}

		/* create new */
		app = gs_app_new (myapps[i].id);
		gs_app_add_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX);
		gs_app_set_metadata (app, "GnomeSoftware::Creator",
				     gs_plugin_get_name (plugin));
		gs_app_set_metadata (app, "GnomeSoftware::FeatureTile-css",
				     myapps[i].css);
		gs_app_list_add (list, app);

		/* save in the cache */
		gs_plugin_cache_add (plugin, myapps[i].id, app);
	}
	return TRUE;
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	const gchar *key = "GnomeSoftware::FeatureTile-css";
	guint i;
	for (i = 0; myapps[i].id != NULL; i++) {
		if (g_strcmp0 (gs_app_get_id_no_prefix (app),
			       myapps[i].id) != 0)
			continue;
		if (gs_app_get_metadata_item (app, key) != NULL)
			continue;
		gs_app_set_metadata (app, key, myapps[i].css);
	}
	return TRUE;
}
