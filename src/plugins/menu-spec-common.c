/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include "menu-spec-common.h"

static const MenuSpecData msdata[] = {
	/* TRANSLATORS: this is the menu spec main category for Audio */
	{ "Audio", 				"folder-music-symbolic", N_("Audio") },
	{ "Audio::AudioVideoEditing",		NULL, NC_("Menu subcategory of Audio", "Editing") },
	{ "Audio::Database",			NULL, NC_("Menu subcategory of Audio", "Databases") },
	{ "Audio::DiscBurning",			NULL, NC_("Menu subcategory of Audio", "Disc Burning") },
	{ "Audio::HamRadio",			NULL, NC_("Menu subcategory of Audio", "Ham Radio") },
	{ "Audio::Midi",			NULL, NC_("Menu subcategory of Audio", "MIDI") },
	{ "Audio::Mixer",			NULL, NC_("Menu subcategory of Audio", "Mixer") },
	{ "Audio::Music",			NULL, NC_("Menu subcategory of Audio", "Music") },
	{ "Audio::Player",			NULL, NC_("Menu subcategory of Audio", "Players") },
	{ "Audio::Recorder",			NULL, NC_("Menu subcategory of Audio", "Recorders") },
	{ "Audio::Sequencer",			NULL, NC_("Menu subcategory of Audio", "Sequencers") },
	{ "Audio::Tuner",			NULL, NC_("Menu subcategory of Audio", "Tuners") },
	/* TRANSLATORS: this is the menu spec main category for Development */
	{ "Development", 			"applications-engineering-symbolic", N_("Development Tools") },
	{ "Development::Building",		NULL, NC_("Menu subcategory of Development Tools", "Building") },
	{ "Development::Database",		NULL, NC_("Menu subcategory of Development Tools", "Databases") },
	{ "Development::Debugger",		NULL, NC_("Menu subcategory of Development Tools", "Debuggers") },
	{ "Development::GUIDesigner",		NULL, NC_("Menu subcategory of Development Tools", "GUI Designers") },
	{ "Development::IDE",			NULL, NC_("Menu subcategory of Development Tools", "IDE") },
	{ "Development::Profiling",		NULL, NC_("Menu subcategory of Development Tools", "Profiling") },
	{ "Development::ProjectManagement",	NULL, NC_("Menu subcategory of Development Tools", "Project Management") },
	{ "Development::RevisionControl",	NULL, NC_("Menu subcategory of Development Tools", "Revision Control") },
	{ "Development::Translation",		NULL, NC_("Menu subcategory of Development Tools", "Translation") },
	{ "Development::WebDevelopment",	NULL, NC_("Menu subcategory of Development Tools", "Web Development") },
	/* TRANSLATORS: this is the menu spec main category for Education */
	{ "Education", 				"system-help-symbolic", N_("Education") },
	{ "Education::Art",			NULL, NC_("Menu subcategory of Education", "Art") },
	{ "Education::ArtificialIntelligence",	NULL, NC_("Menu subcategory of Education", "Artificial Intelligence") },
	{ "Education::Astronomy",		NULL, NC_("Menu subcategory of Education", "Astronomy") },
	{ "Education::Biology",			NULL, NC_("Menu subcategory of Education", "Biology") },
	{ "Education::Chemistry",		NULL, NC_("Menu subcategory of Education", "Chemistry") },
	{ "Education::ComputerScience",		NULL, NC_("Menu subcategory of Education", "Computer Science") },
	{ "Education::Construction",		NULL, NC_("Menu subcategory of Education", "Construction") },
	{ "Education::DataVisualization",	NULL, NC_("Menu subcategory of Education", "Data Visualization") },
	{ "Education::Economy",			NULL, NC_("Menu subcategory of Education", "Economy") },
	{ "Education::Electricity",		NULL, NC_("Menu subcategory of Education", "Electricity") },
	{ "Education::Electronics",		NULL, NC_("Menu subcategory of Education", "Electronics") },
	{ "Education::Engineering",		NULL, NC_("Menu subcategory of Education", "Engineering") },
	{ "Education::Geography",		NULL, NC_("Menu subcategory of Education", "Geography") },
	{ "Education::Geology",			NULL, NC_("Menu subcategory of Education", "Geology") },
	{ "Education::Geoscience",		NULL, NC_("Menu subcategory of Education", "Geoscience") },
	{ "Education::History",			NULL, NC_("Menu subcategory of Education", "History") },
	{ "Education::Humanities",		NULL, NC_("Menu subcategory of Education", "Humanities") },
	{ "Education::ImageProcessing",		NULL, NC_("Menu subcategory of Education", "Image Processing") },
	{ "Education::Languages",		NULL, NC_("Menu subcategory of Education", "Languages") },
	{ "Education::Literature",		NULL, NC_("Menu subcategory of Education", "Literature") },
	{ "Education::Maps",			NULL, NC_("Menu subcategory of Education", "Maps") },
	{ "Education::Math",			NULL, NC_("Menu subcategory of Education", "Math") },
	{ "Education::MedicalSoftware",		NULL, NC_("Menu subcategory of Education", "Medical") },
	{ "Education::Music",			NULL, NC_("Menu subcategory of Education", "Music") },
	{ "Education::NumericalAnalysis",	NULL, NC_("Menu subcategory of Education", "Numerical Analysis") },
	{ "Education::ParallelComputing",	NULL, NC_("Menu subcategory of Education", "Parallel Computing") },
	{ "Education::Physics",			NULL, NC_("Menu subcategory of Education", "Physics") },
	{ "Education::Robotics",		NULL, NC_("Menu subcategory of Education", "Robotics") },
	{ "Education::Spirituality",		NULL, NC_("Menu subcategory of Education", "Spirituality") },
	{ "Education::Sports",			NULL, NC_("Menu subcategory of Education", "Sports") },
	/* TRANSLATORS: this is the menu spec main category for Games */
	{ "Game", 				"applications-games-symbolic", N_("Games") },
	{ "Game::ActionGame",			NULL, NC_("Menu subcategory of Games", "Action") },
	{ "Game::AdventureGame",		NULL, NC_("Menu subcategory of Games", "Adventure") },
	{ "Game::ArcadeGame",			NULL, NC_("Menu subcategory of Games", "Arcade") },
	{ "Game::BlocksGame",			NULL, NC_("Menu subcategory of Games", "Blocks") },
	{ "Game::BoardGame",			NULL, NC_("Menu subcategory of Games", "Board") },
	{ "Game::CardGame",			NULL, NC_("Menu subcategory of Games", "Card") },
	{ "Game::Emulator",			NULL, NC_("Menu subcategory of Games", "Emulators") },
	{ "Game::KidsGame",			NULL, NC_("Menu subcategory of Games", "Kids") },
	{ "Game::LogicGame",			NULL, NC_("Menu subcategory of Games", "Logic") },
	{ "Game::RolePlaying",			NULL, NC_("Menu subcategory of Games", "Role Playing") },
	{ "Game::Shooter",			NULL, NC_("Menu subcategory of Games", "Shooter") },
	{ "Game::Simulation",			NULL, NC_("Menu subcategory of Games", "Simulation") },
	{ "Game::SportsGame",			NULL, NC_("Menu subcategory of Games", "Sports") },
	{ "Game::StrategyGame",			NULL, NC_("Menu subcategory of Games", "Strategy") },
	/* TRANSLATORS: this is the menu spec main category for Graphics */
	{ "Graphics", 				"applications-graphics-symbolic", N_("Graphics") },
	{ "Graphics::2DGraphics",		NULL, NC_("Menu subcategory of Graphics", "2D Graphics") },
	{ "Graphics::3DGraphics",		NULL, NC_("Menu subcategory of Graphics", "3D Graphics") },
	{ "Graphics::OCR",			NULL, NC_("Menu subcategory of Graphics", "OCR") },
	{ "Graphics::Photography",		NULL, NC_("Menu subcategory of Graphics", "Photography") },
	{ "Graphics::Publishing",		NULL, NC_("Menu subcategory of Graphics", "Publishing") },
	{ "Graphics::RasterGraphics",		NULL, NC_("Menu subcategory of Graphics", "Raster Graphics") },
	{ "Graphics::Scanning",			NULL, NC_("Menu subcategory of Graphics", "Scanning") },
	{ "Graphics::VectorGraphics",		NULL, NC_("Menu subcategory of Graphics", "Vector Graphics") },
	{ "Graphics::Viewer",			NULL, NC_("Menu subcategory of Graphics", "Viewer") },
	/* TRANSLATORS: this is the menu spec main category for Network */
	{ "Network", 				"network-wireless-symbolic", N_("Internet") },
	{ "Network::Chat",			NULL, NC_("Menu subcategory of Internet", "Chat") },
	{ "Network::Dialup",			NULL, NC_("Menu subcategory of Internet", "Dialup") },
	{ "Network::Email",			NULL, NC_("Menu subcategory of Internet", "Email") },
	{ "Network::Feed",			NULL, NC_("Menu subcategory of Internet", "Feed") },
	{ "Network::FileTransfer",		NULL, NC_("Menu subcategory of Internet", "File Transfer") },
	{ "Network::HamRadio",			NULL, NC_("Menu subcategory of Internet", "Ham Radio") },
	{ "Network::InstantMessaging",		NULL, NC_("Menu subcategory of Internet", "Instant Messaging") },
	{ "Network::IRCClient",			NULL, NC_("Menu subcategory of Internet", "IRC Clients") },
	{ "Network::Monitor",			NULL, NC_("Menu subcategory of Internet", "Monitor") },
	{ "Network::News",			NULL, NC_("Menu subcategory of Internet", "News") },
	{ "Network::P2P",			NULL, NC_("Menu subcategory of Internet", "P2P") },
	{ "Network::RemoteAccess",		NULL, NC_("Menu subcategory of Internet", "Remote Access") },
	{ "Network::Telephony",			NULL, NC_("Menu subcategory of Internet", "Telephony") },
	{ "Network::VideoConference",		NULL, NC_("Menu subcategory of Internet", "Video Conference") },
	{ "Network::WebBrowser",		NULL, NC_("Menu subcategory of Internet", "Web Browser") },
	{ "Network::WebDevelopment",		NULL, NC_("Menu subcategory of Internet", "Web Development") },
	/* TRANSLATORS: this is the menu spec main category for Office */
	{ "Office", 				"text-editor-symbolic", N_("Office") },
	{ "Office::Calendar",			NULL, NC_("Menu subcategory of Office", "Calendar") },
	{ "Office::Chart",			NULL, NC_("Menu subcategory of Office", "Chart") },
	{ "Office::ContactManagement",		NULL, NC_("Menu subcategory of Office", "Contact Management") },
	{ "Office::Database",			NULL, NC_("Menu subcategory of Office", "Database") },
	{ "Office::Dictionary",			NULL, NC_("Menu subcategory of Office", "Dictionary") },
	{ "Office::Email",			NULL, NC_("Menu subcategory of Office", "Email") },
	{ "Office::Finance",			NULL, NC_("Menu subcategory of Office", "Finance") },
	{ "Office::FlowChart",			NULL, NC_("Menu subcategory of Office", "Flow Chart") },
	{ "Office::PDA",			NULL, NC_("Menu subcategory of Office", "PDA") },
	{ "Office::Photography",		NULL, NC_("Menu subcategory of Office", "Photography") },
	{ "Office::Presentation",		NULL, NC_("Menu subcategory of Office", "Presentation") },
	{ "Office::ProjectManagement",		NULL, NC_("Menu subcategory of Office", "Project Management") },
	{ "Office::Publishing",			NULL, NC_("Menu subcategory of Office", "Publishing") },
	{ "Office::Spreadsheet",		NULL, NC_("Menu subcategory of Office", "Spreadsheet") },
	{ "Office::Viewer",			NULL, NC_("Menu subcategory of Office", "Viewer") },
	{ "Office::WordProcessor",		NULL, NC_("Menu subcategory of Office", "Word Processor") },
	/* TRANSLATORS: this is the menu spec main category for Science */
	{ "Science", 				"applications-science-symbolic", N_("Science") },
	{ "Science::Art",			NULL, NC_("Menu subcategory of Science", "Art") },
	{ "Science::ArtificialIntelligence",	NULL, NC_("Menu subcategory of Science", "Artificial Intelligence") },
	{ "Science::Astronomy",			NULL, NC_("Menu subcategory of Science", "Astronomy") },
	{ "Science::Biology",			NULL, NC_("Menu subcategory of Science", "Biology") },
	{ "Science::Chemistry",			NULL, NC_("Menu subcategory of Science", "Chemistry") },
	{ "Science::ComputerScience",		NULL, NC_("Menu subcategory of Science", "Computer Science") },
	{ "Science::Construction",		NULL, NC_("Menu subcategory of Science", "Construction") },
	{ "Science::DataVisualization",		NULL, NC_("Menu subcategory of Science", "Data Visualization") },
	{ "Science::Economy",			NULL, NC_("Menu subcategory of Science", "Economy") },
	{ "Science::Electricity",		NULL, NC_("Menu subcategory of Science", "Electricity") },
	{ "Science::Electronics",		NULL, NC_("Menu subcategory of Science", "Electronics") },
	{ "Science::Engineering",		NULL, NC_("Menu subcategory of Science", "Engineering") },
	{ "Science::Geography",			NULL, NC_("Menu subcategory of Science", "Geography") },
	{ "Science::Geology",			NULL, NC_("Menu subcategory of Science", "Geology") },
	{ "Science::Geoscience",		NULL, NC_("Menu subcategory of Science", "Geoscience") },
	{ "Science::History",			NULL, NC_("Menu subcategory of Science", "History") },
	{ "Science::Humanities",		NULL, NC_("Menu subcategory of Science", "Humanities") },
	{ "Science::ImageProcessing",		NULL, NC_("Menu subcategory of Science", "Image Processing") },
	{ "Science::Languages",			NULL, NC_("Menu subcategory of Science", "Languages") },
	{ "Science::Literature",		NULL, NC_("Menu subcategory of Science", "Literature") },
	{ "Science::Maps",			NULL, NC_("Menu subcategory of Science", "Maps") },
	{ "Science::Math",			NULL, NC_("Menu subcategory of Science", "Math") },
	{ "Science::MedicalSoftware",		NULL, NC_("Menu subcategory of Science", "Medical") },
	{ "Science::NumericalAnalysis",		NULL, NC_("Menu subcategory of Science", "Numerical Analysis") },
	{ "Science::ParallelComputing",		NULL, NC_("Menu subcategory of Science", "Parallel Computing") },
	{ "Science::Physics",			NULL, NC_("Menu subcategory of Science", "Physics") },
	{ "Science::Robotics",			NULL, NC_("Menu subcategory of Science", "Robotics") },
	{ "Science::Spirituality",		NULL, NC_("Menu subcategory of Science", "Spirituality") },
	{ "Science::Sports",			NULL, NC_("Menu subcategory of Science", "Sports") },
	/* TRANSLATORS: this is the menu spec main category for System */
	{ "System", 				"applications-system-symbolic", N_("System") },
	{ "System::Emulator",			NULL, NC_("Menu subcategory of System", "Emulator") },
	{ "System::FileManager",		NULL, NC_("Menu subcategory of System", "File Manager") },
	{ "System::Filesystem",			NULL, NC_("Menu subcategory of System", "File System") },
	{ "System::FileTools",			NULL, NC_("Menu subcategory of System", "File Tools") },
	{ "System::Monitor",			NULL, NC_("Menu subcategory of System", "Monitor") },
	{ "System::Security",			NULL, NC_("Menu subcategory of System", "Security") },
	{ "System::TerminalEmulator",		NULL, NC_("Menu subcategory of System", "Terminal Emulator") },
	/* TRANSLATORS: this is the menu spec main category for Utility */
	{ "Utility", 				"applications-utilities-symbolic", N_("Utilities") },
	{ "Utility::Accessibility",		NULL, NC_("Menu subcategory of Utilities", "Accessibility") },
	{ "Utility::Archiving",			NULL, NC_("Menu subcategory of Utilities", "Archiving") },
	{ "Utility::Calculator",		NULL, NC_("Menu subcategory of Utilities", "Calculator") },
	{ "Utility::Clock",			NULL, NC_("Menu subcategory of Utilities", "Clock") },
	{ "Utility::Compression",		NULL, NC_("Menu subcategory of Utilities", "Compression") },
	{ "Utility::FileTools",			NULL, NC_("Menu subcategory of Utilities", "File Tools") },
	{ "Utility::Maps",			NULL, NC_("Menu subcategory of Utilities", "Maps") },
	{ "Utility::Spirituality",		NULL, NC_("Menu subcategory of Utilities", "Spirituality") },
	{ "Utility::TelephonyTools",		NULL, NC_("Menu subcategory of Utilities", "Telephony Tools") },
	{ "Utility::TextEditor",		NULL, NC_("Menu subcategory of Utilities", "Text Editor") },
	/* TRANSLATORS: this is the menu spec main category for Video */
	{ "Video", 				"folder-videos-symbolic", N_("Video") },
	{ "Video::AudioVideoEditing",		NULL, NC_("Menu subcategory of Video", "Editing") },
	{ "Video::Database",			NULL, NC_("Menu subcategory of Video", "Database") },
	{ "Video::DiscBurning",			NULL, NC_("Menu subcategory of Video", "Disc Burning") },
	{ "Video::Player",			NULL, NC_("Menu subcategory of Video", "Players") },
	{ "Video::Recorder",			NULL, NC_("Menu subcategory of Video", "Recorders") },
	{ "Video::TV",				NULL, NC_("Menu subcategory of Video", "TV") },
	/* TRANSLATORS: this is the main category for Add-ons */
	{ "Addons", 				"list-add-symbolic", N_("Add-ons") },
	{ "Addons::Fonts",			NULL, NC_("Menu subcategory of Add-ons", "Fonts") },
	{ "Addons::Codecs",			NULL, NC_("Menu subcategory of Add-ons", "Codecs") },
	{ "Addons::InputSources",		NULL, NC_("Menu subcategory of Add-ons", "Input Sources") },
	{ "Addons::LanguagePacks",		NULL, NC_("Menu subcategory of Add-ons", "Language Packs") },
	{ "Addons::ShellExtensions",		NULL, NC_("Menu subcategory of Add-ons", "Shell Extensions") },
	{ "Addons::Localization",		NULL, NC_("Menu subcategory of Add-ons", "Localization") },
	{ NULL,					NULL, NULL }
};

const MenuSpecData *
menu_spec_get_data (void)
{
	return msdata;
}
