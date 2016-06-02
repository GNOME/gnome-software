/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <glib/gi18n.h>

#include "gs-desktop-common.h"

/* AudioVideo */
static const GsDesktopMap map_audiovisual[] = {
	{ "all",		NC_("Menu of AudioVideo", "All"),
					{ "AudioVideo",
					  NULL } },
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
static const GsDesktopMap map_developertools[] = {
	{ "all",		NC_("Menu of Development", "All"),
					{ "Development",
					  NULL } },
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
static const GsDesktopMap map_education[] = {
	{ "all",		NC_("Menu of Education", "All"),
					{ "Education",
					  NULL } },
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
static const GsDesktopMap map_games[] = {
	{ "all",		NC_("Menu of Games", "All"),
					{ "Game",
					  NULL } },
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
static const GsDesktopMap map_graphics[] = {
	{ "all",		NC_("Menu of Graphics", "All"),
					{ "Graphics",
					  NULL } },
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
static const GsDesktopMap map_productivity[] = {
	{ "all",		NC_("Menu of Office", "All"),
					{ "Office",
					  NULL } },
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
static const GsDesktopMap map_addons[] = {
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
static const GsDesktopMap map_science[] = {
	{ "all",		NC_("Menu of Science", "All"),
					{ "Science",
					  NULL } },
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
static const GsDesktopMap map_communication[] = {
	{ "all",		NC_("Menu of Communication", "All"),
					{ "Network",
					  NULL } },
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
static const GsDesktopMap map_utilities[] = {
	{ "all",		NC_("Menu of Utility", "All"),
					{ "Utility",
					  NULL } },
	{ "featured",		NC_("Menu of Utility", "Featured"),
					{ "Utility::featured",
					  NULL} },
	{ "text-editors",	NC_("Menu of Utility", "Text Editors"),
					{ "Utility::TextEditor",
					  NULL} },
	{ NULL }
};

/* main categories */
static const GsDesktopData msdata[] = {
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

const GsDesktopData *
gs_desktop_get_data (void)
{
	return msdata;
}
