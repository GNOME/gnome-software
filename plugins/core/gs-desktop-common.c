/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-desktop-common.h"

/* AudioVideo */
static const GsDesktopMap map_audiovisual[] = {
	{ "all",		NC_("Menu of Audio & Video", "All"),
					{ "AudioVideo",
					  NULL } },
	{ "featured",		NC_("Menu of Audio & Video", "Featured"),
					{ "AudioVideo::Featured",
					  NULL} },
	{ "creation-editing",	NC_("Menu of Audio & Video", "Audio Creation & Editing"),
					{ "AudioVideo::AudioVideoEditing",
					  "AudioVideo::Midi",
					  "AudioVideo::DiscBurning",
					  "AudioVideo::Sequencer",
					  NULL} },
	{ "music-players",	NC_("Menu of Audio & Video", "Music Players"),
					{ "AudioVideo::Music",
					  "AudioVideo::Player",
					  NULL} },
	{ NULL }
};

/* Development */
static const GsDesktopMap map_developertools[] = {
	{ "all",		NC_("Menu of Developer Tools", "All"),
					{ "Development",
					  NULL } },
	{ "featured",		NC_("Menu of Developer Tools", "Featured"),
					{ "Development::Featured",
					  NULL} },
	{ "debuggers",		NC_("Menu of Developer Tools", "Debuggers"),
					{ "Development::Debugger",
					  NULL} },
	{ "ide",		NC_("Menu of Developer Tools", "IDEs"),
					{ "Development::IDE",
					  "Development::GUIDesigner",
					  NULL} },
	{ NULL }
};

/* Education & Science */
static const GsDesktopMap map_education_science[] = {
	{ "all",		NC_("Menu of Education & Science", "All"),
					{ "Education",
					  "Science",
					  NULL } },
	{ "featured",		NC_("Menu of Education & Science", "Featured"),
					{ "Education::Featured",
					  "Science::Featured",
					  NULL} },
	{ "artificial-intelligence", NC_("Menu of Education & Science", "Artificial Intelligence"),
					{ "Science::ArtificialIntelligence",
					  NULL} },
	{ "astronomy",		NC_("Menu of Education & Science", "Astronomy"),
					{ "Education::Astronomy",
					  "Science::Astronomy",
					  NULL} },
	{ "chemistry",		NC_("Menu of Education & Science", "Chemistry"),
					{ "Education::Chemistry",
					  "Science::Chemistry",
					  NULL} },
	{ "languages",		NC_("Menu of Education & Science", "Languages"),
					{ "Education::Languages",
					  "Education::Literature",
					  NULL} },
	{ "math",		NC_("Menu of Education & Science", "Math"),
					{ "Education::Math",
					  "Education::NumericalAnalysis",
					  "Science::Math",
					  "Science::Physics",
					  "Science::NumericalAnalysis",
					  NULL} },
	{ "robotics",		NC_("Menu of Education & Science", "Robotics"),
					{ "Science::Robotics",
					  NULL} },

	{ NULL }
};

/* Games */
static const GsDesktopMap map_games[] = {
	{ "all",		NC_("Menu of Games", "All"),
					{ "Game",
					  NULL } },
	{ "featured",		NC_("Menu of Games", "Featured"),
					{ "Game::Featured",
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
static const GsDesktopMap map_graphics[] = {
	{ "all",		NC_("Menu of Graphics & Photography", "All"),
					{ "Graphics",
					  NULL } },
	{ "featured",		NC_("Menu of Graphics & Photography", "Featured"),
					{ "Graphics::Featured",
					  NULL} },
	{ "3d",			NC_("Menu of Graphics & Photography", "3D Graphics"),
					{ "Graphics::3DGraphics",
					  NULL} },
	{ "photography",	NC_("Menu of Graphics & Photography", "Photography"),
					{ "Graphics::Photography",
					  NULL} },
	{ "scanning",		NC_("Menu of Graphics & Photography", "Scanning"),
					{ "Graphics::Scanning",
					  NULL} },
	{ "vector",		NC_("Menu of Graphics & Photography", "Vector Graphics"),
					{ "Graphics::VectorGraphics",
					  NULL} },
	{ "viewers",		NC_("Menu of Graphics & Photography", "Viewers"),
					{ "Graphics::Viewer",
					  NULL} },
	{ NULL }
};

/* Office */
static const GsDesktopMap map_productivity[] = {
	{ "all",		NC_("Menu of Productivity", "All"),
					{ "Office",
					  NULL } },
	{ "featured",		NC_("Menu of Productivity", "Featured"),
					{ "Office::Featured",
					  NULL} },
	{ "calendar",		NC_("Menu of Productivity", "Calendar"),
					{ "Office::Calendar",
					  "Office::ProjectManagement",
					  NULL} },
	{ "database",		NC_("Menu of Productivity", "Database"),
					{ "Office::Database",
					  NULL} },
	{ "finance",		NC_("Menu of Productivity", "Finance"),
					{ "Office::Finance",
					  "Office::Spreadsheet",
					  NULL} },
	{ "word-processor",	NC_("Menu of Productivity", "Word Processor"),
					{ "Office::WordProcessor",
					  "Office::Dictionary",
					  NULL} },
	{ NULL }
};

/* Addons */
static const GsDesktopMap map_addons[] = {
	{ "fonts",		NC_("Menu of Add-ons", "Fonts"),
					{ "Addon::Font",
					  NULL} },
	{ "codecs",		NC_("Menu of Add-ons", "Codecs"),
					{ "Addon::Codec",
					  NULL} },
	{ "input-sources",	NC_("Menu of Add-ons", "Input Sources"),
					{ "Addon::InputSource",
					  NULL} },
	{ "language-packs",	NC_("Menu of Add-ons", "Language Packs"),
					{ "Addon::LanguagePack",
					  NULL} },
	{ "localization",	NC_("Menu of Add-ons", "Localization"),
					{ "Addon::Localization",
					  NULL} },
	{ "drivers",		NC_("Menu of Add-ons", "Hardware Drivers"),
					{ "Addon::Driver",
					  NULL} },
	{ NULL }
};

/* Communication */
static const GsDesktopMap map_communication[] = {
	{ "all",		NC_("Menu of Communication & News", "All"),
					{ "Network",
					  NULL } },
	{ "featured",		NC_("Menu of Communication & News", "Featured"),
					{ "Network::Featured",
					  NULL} },
	{ "chat",		NC_("Menu of Communication & News", "Chat"),
					{ "Network::Chat",
					  "Network::IRCClient",
					  "Network::Telephony",
					  "Network::VideoConference",
					  "Network::Email",
					  NULL} },
	{ "news",		NC_("Menu of Communication & News", "News"),
					{ "Network::Feed",
					  "Network::News",
					  NULL} },
	{ "web-browsers",	NC_("Menu of Communication & News", "Web Browsers"),
					{ "Network::WebBrowser",
					  NULL} },
	{ NULL }
};

/* Utility */
static const GsDesktopMap map_utilities[] = {
	{ "all",		NC_("Menu of Utilities", "All"),
					{ "Utility",
					  NULL } },
	{ "featured",		NC_("Menu of Utilities", "Featured"),
					{ "Utility::Featured",
					  NULL} },
	{ "text-editors",	NC_("Menu of Utilities", "Text Editors"),
					{ "Utility::TextEditor",
					  NULL} },
	{ NULL }
};

/* Reference */
static const GsDesktopMap map_reference[] = {
	{ "all",		NC_("Menu of Reference", "All"),
					{ "Reference",
					  NULL } },
	{ "featured",		NC_("Menu of Reference", "Featured"),
					{ "Reference::Featured",
					  NULL} },
	{ "art",		NC_("Menu of Art", "Art"),
					{ "Reference::Art",
					  NULL} },
	{ "biography",		NC_("Menu of Reference", "Biography"),
					{ "Reference::Biography",
					  NULL} },
	{ "comics",		NC_("Menu of Reference", "Comics"),
					{ "Reference::Comics",
					  NULL} },
	{ "fiction",		NC_("Menu of Reference", "Fiction"),
					{ "Reference::Fiction",
					  NULL} },
	{ "health",		NC_("Menu of Reference", "Health"),
					{ "Reference::Health",
					  NULL} },
	{ "history",		NC_("Menu of Reference", "History"),
					{ "Reference::History",
					  NULL} },
	{ "lifestyle",		NC_("Menu of Reference", "Lifestyle"),
					{ "Reference::Lifestyle",
					  NULL} },
	{ "politics",		NC_("Menu of Reference", "Politics"),
					{ "Reference::Politics",
					  NULL} },
	{ "sports",		NC_("Menu of Reference", "Sports"),
					{ "Reference::Sports",
					  NULL} },
	{ NULL }
};

/* main categories */
/* Please keep category name and subcategory context synchronized!!! */
static const GsDesktopData msdata[] = {
	/* TRANSLATORS: this is the menu spec main category for Audio & Video */
	{ "audio-video",	map_audiovisual,	N_("Audio & Video"),
				"folder-music-symbolic", 100 },
	/* TRANSLATORS: this is the menu spec main category for Development */
	{ "developer-tools",	map_developertools,	N_("Developer Tools"),
				"applications-engineering-symbolic", 40 },
	/* TRANSLATORS: this is the menu spec main category for Education & Science */
	{ "education-science",		map_education_science,	N_("Education & Science"),
				"system-help-symbolic", 30 },
	/* TRANSLATORS: this is the menu spec main category for Game */
	{ "games",		map_games,		N_("Games"),
				"applications-games-symbolic", 70 },
	/* TRANSLATORS: this is the menu spec main category for Graphics */
	{ "graphics",		map_graphics,		N_("Graphics & Photography"),
				"applications-graphics-symbolic", 60 },
	/* TRANSLATORS: this is the menu spec main category for Office */
	{ "productivity",	map_productivity,	N_("Productivity"),
				"text-editor-symbolic", 80 },
	/* TRANSLATORS: this is the menu spec main category for Add-ons */
	{ "addons",		map_addons,		N_("Add-ons"),
				"application-x-addon-symbolic", 50 },
	/* TRANSLATORS: this is the menu spec main category for Communication */
	{ "communication",	map_communication,	N_("Communication & News"),
				"user-available-symbolic", 90 },
	/* TRANSLATORS: this is the menu spec main category for Reference */
	{ "reference",		map_reference,		N_("Reference"),
				"view-dual-symbolic", 0 },
	/* TRANSLATORS: this is the menu spec main category for Utilities */
	{ "utilities",		map_utilities,		N_("Utilities"),
				"applications-utilities-symbolic", 10 },
	{ NULL }
};

const GsDesktopData *
gs_desktop_get_data (void)
{
	return msdata;
}
