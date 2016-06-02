/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2016 Richard Hughes <richard@hughsie.com>
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
#include <glib/gi18n.h>

/*
 * SECTION:
 * Adds categories from a hardcoded list based on the the desktop menu
 * specification.
 */

void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* need categories */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_BEFORE, "appstream");

	/* the old name for these plugins */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "menu-spec-categories");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "menu-spec-refine");
}

typedef struct {
	const gchar	*id;
	const gchar	*name;
	const gchar	*fdo_cats[16];
} GsCategoryMap;

typedef struct {
	const gchar	*id;
	GsCategoryMap	*mapping;
	const gchar	*name;
	const gchar	*icon;
	const gchar	*key_colors;
	gboolean	 important;
} GsCategoryData;

/* AudioVideo */
static const GsCategoryMap map_audiovisual[] = {
//	{ "all",		NC_("Menu of AudioVideo", "All"),
//					{ NULL } },
	{ "featured",		NC_("Menu of AudioVideo", "Featured"),
					{ "AudioVideo::featured",
					  NULL} },
	{ "creation-editing",	NC_("Menu of AudioVideo", "Audio Creation & Editing"),
					{ "AudioVideo::AudioVideoEditing",
					  "AudioVideo::Midi",
					  "AudioVideo::DiscBurning",
					  "AudioVideo::Sequencer",
					  NULL} },
	{ "music-players",	NC_("Menu of AudioVideo", "Music Players"),
					{ "AudioVideo::Music",
					  "AudioVideo::Player",
					  NULL} },
	{ NULL }
};

/* Development */
static const GsCategoryMap map_developertools[] = {
	{ "featured",		NC_("Menu of Development", "Featured"),
					{ "Development::featured",
					  NULL} },
	{ "debuggers",		NC_("Menu of Development", "Debuggers"),
					{ "Development:Debugger",
					  NULL} },
	{ "ide",		NC_("Menu of Development", "IDEs"),
					{ "Development::IDE",
					  "Development::GUIDesigner",
					  NULL} },
	{ NULL }
};

/* Education */
static const GsCategoryMap map_education[] = {
	{ "featured",		NC_("Menu of Education", "Featured"),
					{ "Education::featured",
					  NULL} },
	{ "astronomy",		NC_("Menu of Education", "Astronomy"),
					{ "Education::Astronomy",
					  NULL} },
	{ "chemistry",		NC_("Menu of Education", "Chemistry"),
					{ "Education::Chemistry",
					  NULL} },
	{ "languages",		NC_("Menu of Education", "Languages"),
					{ "Education::Languages",
					  "Education::Literature",
					  NULL} },
	{ "math",		NC_("Menu of Education", "Math"),
					{ "Education::Math",
					  "Education::NumericalAnalysis",
					  NULL} },
	{ NULL }
};

/* Games */
static const GsCategoryMap map_games[] = {
	{ "featured",		NC_("Menu of Games", "Featured"),
					{ "Game::featured",
					  NULL} },
	{ "action",		NC_("Menu of Games", "Action"),
					{ "Game::ActionGame",
					  NULL} },
	{ "adventure",		NC_("Menu of Games", "Adventure"),
					{ "Game::AdventureGame",
					  NULL} },
	{ "arcade",		NC_("Menu of Games", "Arcade"),
					{ "Game::ArcadeGame",
					  NULL} },
	{ "blocks",		NC_("Menu of Games", "Blocks"),
					{ "Game::BlocksGame",
					  NULL} },
	{ "board",		NC_("Menu of Games", "Board"),
					{ "Game::BoardGame",
					  NULL} },
	{ "card",		NC_("Menu of Games", "Card"),
					{ "Game::CardGame",
					  NULL} },
	{ "emulator",		NC_("Menu of Games", "Emulators"),
					{ "Game::Emulator",
					  NULL} },
	{ "kids",		NC_("Menu of Games", "Kids"),
					{ "Game::KidsGame",
					  NULL} },
	{ "logic",		NC_("Menu of Games", "Logic"),
					{ "Game::LogicGame",
					  NULL} },
	{ "role-playing",	NC_("Menu of Games", "Role Playing"),
					{ "Game::RolePlaying",
					  NULL} },
	{ "sports",		NC_("Menu of Games", "Sports"),
					{ "Game::SportsGame",
					  "Game::Simulation",
					  NULL} },
	{ "strategy",		NC_("Menu of Games", "Strategy"),
					{ "Game::StrategyGame",
					  NULL} },
	{ NULL }
};

/* Graphics */
static const GsCategoryMap map_graphics[] = {
	{ "featured",		NC_("Menu of Graphics", "Featured"),
					{ "Graphics::featured",
					  NULL} },
	{ "3d",			NC_("Menu of Graphics", "3D Graphics"),
					{ "Graphics::3DGraphics",
					  NULL} },
	{ "photography",	NC_("Menu of Graphics", "Photography"),
					{ "Graphics::Photography",
					  NULL} },
	{ "scanning",		NC_("Menu of Graphics", "Scanning"),
					{ "Graphics::Scanning",
					  NULL} },
	{ "vector",		NC_("Menu of Graphics", "Vector Graphics"),
					{ "Graphics::VectorGraphics",
					  NULL} },
	{ "viewers",		NC_("Menu of Graphics", "Viewers"),
					{ "Graphics::Viewer",
					  NULL} },
	{ NULL }
};

/* Office */
static const GsCategoryMap map_productivity[] = {
	{ "featured",		NC_("Menu of Office", "Featured"),
					{ "Office::featured",
					  NULL} },
	{ "calendar",		NC_("Menu of Office", "Calendar"),
					{ "Office::Calendar",
					  "Office::ProjectManagement",
					  NULL} },
	{ "database",		NC_("Menu of Office", "Database"),
					{ "Office::Database",
					  NULL} },
	{ "finance",		NC_("Menu of Office", "Finance"),
					{ "Office::Finance",
					  "Office::Spreadsheet",
					  NULL} },
	{ "word-processor",	NC_("Menu of Office", "Word Processor"),
					{ "Office::WordProcessor",
					  "Office::Dictionary",
					  NULL} },
	{ NULL }
};

/* Addons */
static const GsCategoryMap map_addons[] = {
	{ "fonts",		NC_("Menu of Addons", "Fonts"),
					{ "Addons::Fonts",
					  NULL} },
	{ "codecs",		NC_("Menu of Addons", "Codecs"),
					{ "Addons::Codecs",
					  NULL} },
	{ "input-sources",	NC_("Menu of Addons", "Input Sources"),
					{ "Addons::InputSources",
					  NULL} },
	{ "language-packs",	NC_("Menu of Addons", "Language Packs"),
					{ "Addons::LanguagePacks",
					  NULL} },
	{ "shell-extensions",	NC_("Menu of Addons", "Shell Extensions"),
					{ "Addons::ShellExtensions",
					  NULL} },
	{ "localization",	NC_("Menu of Addons", "Localization"),
					{ "Addons::Localization",
					  NULL} },
	{ NULL }
};

/* Science */
static const GsCategoryMap map_science[] = {
	{ "featured",		NC_("Menu of Science", "Featured"),
					{ "Science::featured",
					  NULL} },
	{ "artificial-intelligence", NC_("Menu of Science", "Artificial Intelligence"),
					{ "Science::ArtificialIntelligence",
					  NULL} },
	{ "astronomy",		NC_("Menu of Science", "Astronomy"),
					{ "Science::Astronomy",
					  NULL} },
	{ "chemistry",		NC_("Menu of Science", "Chemistry"),
					{ "Science::Chemistry",
					  NULL} },
	{ "math",		NC_("Menu of Science", "Math"),
					{ "Science::Math",
					  "Science::Physics",
					  "Science::NumericalAnalysis",
					  NULL} },
	{ "robotics",		NC_("Menu of Science", "Robotics"),
					{ "Science::Robotics",
					  NULL} },
	{ NULL }
};

/* Communication */
static const GsCategoryMap map_communication[] = {
	{ "featured",		NC_("Menu of Communication", "Featured"),
					{ "Network::featured",
					  NULL} },
	{ "chat",		NC_("Menu of Communication", "Chat"),
					{ "Network::Chat",
					  "Network::IRCClient",
					  "Network::Telephony",
					  "Network::VideoConference",
					  "Network::Email",
					  NULL} },
	{ "news",		NC_("Menu of Communication", "News"),
					{ "Network::Feed",
					  "Network::News",
					  NULL} },
	{ "web-browsers",	NC_("Menu of Communication", "Web Browsers"),
					{ "Network::WebBrowser",
					  NULL} },
	{ NULL }
};

/* Utility */
static const GsCategoryMap map_utilities[] = {
	{ "featured",		NC_("Menu of Utility", "Featured"),
					{ "Utility::featured",
					  NULL} },
	{ "text-editors",	NC_("Menu of Utility", "Text Editors"),
					{ "Utility::TextEditor",
					  NULL} },
	{ NULL }
};

/* main categories */
static const GsCategoryData msdata[] = {
	/* TRANSLATORS: this is the menu spec main category for Audio & Video */
	{ "audio-video",	map_audiovisual,	N_("Audio & Video"),
				"folder-music-symbolic", "#215d9c", TRUE },
	/* TRANSLATORS: this is the menu spec main category for Development */
	{ "developer-tools",	map_developertools,	N_("Developer Tools"),
				"applications-engineering-symbolic", "#297bcc" },
	/* TRANSLATORS: this is the menu spec main category for Education */
	{ "education",		map_education,		N_("Education"),
				"system-help-symbolic", "#29cc5d" },
	/* TRANSLATORS: this is the menu spec main category for Game */
	{ "games",		map_games,		N_("Games"),
				"applications-games-symbolic", "#c4a000", TRUE },
	/* TRANSLATORS: this is the menu spec main category for Graphics */
	{ "graphics",		map_graphics,		N_("Graphics & Photography"),
				"applications-graphics-symbolic", "#75507b", TRUE },
	/* TRANSLATORS: this is the menu spec main category for Office */
	{ "productivity",	map_productivity,	N_("Productivity"),
				"text-editor-symbolic", "#cc0000", TRUE },
	/* TRANSLATORS: this is the menu spec main category for Add-ons */
	{ "addons",		map_addons,		N_("Add-ons"),
				"application-x-addon-symbolic", "#4e9a06", TRUE },
	/* TRANSLATORS: this is the menu spec main category for Science */
	{ "science",		map_science,		N_("Science"),
				"applications-science-symbolic", "#9c29ca" },
	/* TRANSLATORS: this is the menu spec main category for Communication */
	{ "communication",	map_communication,	N_("Communication & News"),
				"user-available-symbolic", "#729fcf", TRUE },
	/* TRANSLATORS: this is the menu spec main category for Utilities */
	{ "utilities",		map_utilities,		N_("Utilities"),
				"applications-utilities-symbolic", "#2944cc" },
	{ NULL }
};

gboolean
gs_plugin_add_categories (GsPlugin *plugin,
			  GPtrArray *list,
			  GCancellable *cancellable,
			  GError **error)
{
	gchar msgctxt[100];
	guint i, j, k;

	for (i = 0; msdata[i].id != NULL; i++) {
		GdkRGBA key_color;
		GsCategory *category;

		/* add parent category */
		category = gs_category_new (msdata[i].id);
		gs_category_set_icon (category, msdata[i].icon);
		gs_category_set_name (category, gettext (msdata[i].name));
		gs_category_set_important (category, msdata[i].important);
		if (gdk_rgba_parse (&key_color, msdata[i].key_colors))
			gs_category_add_key_color (category, &key_color);
		g_ptr_array_add (list, category);
		g_snprintf (msgctxt, sizeof(msgctxt),
			    "Menu subcategory of %s", msdata[i].name);

		/* add subcategories */
		for (j = 0; msdata[i].mapping[j].id != NULL; j++) {
			const GsCategoryMap *map = &msdata[i].mapping[j];
			g_autoptr(GsCategory) sub = gs_category_new (map->id);
			for (k = 0; map->fdo_cats[k] != NULL; k++)
				gs_category_add_desktop_group (sub, map->fdo_cats[k]);
			gs_category_set_name (sub, g_dpgettext2 (GETTEXT_PACKAGE,
								 msgctxt,
								 map->name));
			gs_category_add_child (category, sub);
		}
	}
	return TRUE;
}

/* most of this time this won't be required, unless the user creates a
 * GsCategory manually and uses it to get results, for instance in the
 * overview page or `gnome-software-cmd get-category-apps games/featured` */
gboolean
gs_plugin_add_category_apps (GsPlugin *plugin,
			     GsCategory *category,
			     GsAppList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	GPtrArray *desktop_groups;
	GsCategory *parent;
	guint i, j, k;

	/* already set */
	desktop_groups = gs_category_get_desktop_groups (category);
	if (desktop_groups->len > 0)
		return TRUE;

	/* not valid */
	parent = gs_category_get_parent (category);
	if (parent == NULL)
		return TRUE;

	/* find desktop_groups for a parent::child category */
	for (i = 0; msdata[i].id != NULL; i++) {
		if (g_strcmp0 (gs_category_get_id (parent), msdata[i].id) != 0)
			continue;
		for (j = 0; msdata[i].mapping[j].id != NULL; j++) {
			const GsCategoryMap *map = &msdata[i].mapping[j];
			if (g_strcmp0 (gs_category_get_id (category), map->id) != 0)
				continue;
			for (k = 0; map->fdo_cats[k] != NULL; k++)
				gs_category_add_desktop_group (category, map->fdo_cats[k]);
		}
	}
	return TRUE;
}

static gboolean
_gs_app_has_desktop_group (GsApp *app, const gchar *desktop_group)
{
	guint i;
	g_auto(GStrv) split = g_strsplit (desktop_group, "::", -1);
	for (i = 0; split[i] != NULL; i++) {
		if (!gs_app_has_category (app, split[i]))
			return FALSE;
	}
	return TRUE;
}

/* adds the menu-path for applications */
gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	gboolean found = FALSE;
	guint i, j, k;
	const gchar *strv[] = { "", NULL, NULL };

	/* nothing to do here */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_MENU_PATH) == 0)
		return TRUE;
	if (gs_app_get_menu_path (app) != NULL)
		return TRUE;

	/* find a top level category the app has */
	for (i = 0; !found && msdata[i].id != NULL; i++) {
		GsCategoryData *data = &msdata[i];
		for (j = 0; !found && data->mapping[j].id != NULL; j++) {
			GsCategoryMap *map = &data->mapping[j];
			if (g_strcmp0 (map->id, "featured") == 0)
				continue;
			for (k = 0; !found && map->fdo_cats[k] != NULL; k++) {
				const gchar *tmp = msdata[i].mapping[j].fdo_cats[k];
				if (_gs_app_has_desktop_group (app, tmp)) {
					strv[0] = msdata[i].name;
					strv[1] = msdata[i].mapping[j].name;
					found = TRUE;
					break;
				}
			}
		}
	}

	/* always set something to avoid keep searching for this */
	gs_app_set_menu_path (app, (gchar **) strv);
	return TRUE;
}
