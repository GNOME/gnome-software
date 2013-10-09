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
#include <glib/gi18n.h>

typedef struct {
	const gchar *path;
	const gchar *text;
} GsPluginMenuSpecData;

static GsPluginMenuSpecData msdata[] = {
	/* TRANSLATORS: this is the menu spec main category for Audio */
	{ "Audio", 			N_("Audio") },
	{ "???::AudioVideoEditing",	NC_("Menu subcategory of Audio", "Editing") },
	{ "???::Database",		NC_("Menu subcategory of Audio", "Databases") },
	{ "???::DiscBurning",		NC_("Menu subcategory of Audio", "Disc Burning") },
	{ "???::HamRadio",		NC_("Menu subcategory of Audio", "Ham Radio") },
	{ "???::Midi",			NC_("Menu subcategory of Audio", "MIDI") },
	{ "???::Mixer",			NC_("Menu subcategory of Audio", "Mixer") },
	{ "???::Music",			NC_("Menu subcategory of Audio", "Music") },
	{ "???::Player",		NC_("Menu subcategory of Audio", "Players") },
	{ "???::Recorder",		NC_("Menu subcategory of Audio", "Recorders") },
	{ "???::Sequencer",		NC_("Menu subcategory of Audio", "Sequencers") },
	{ "???::Tuner",			NC_("Menu subcategory of Audio", "Tuners") },
	/* TRANSLATORS: this is the menu spec main category for Development */
	{ "Development", 		N_("Development Tools") },
	{ "???::Building",		NC_("Menu subcategory of Development", "Building") },
	{ "???::Database",		NC_("Menu subcategory of Development", "Databases") },
	{ "???::Debugger",		NC_("Menu subcategory of Development", "Debuggers") },
	{ "???::GUIDesigner",		NC_("Menu subcategory of Development", "GUI Designers") },
	{ "???::IDE",			NC_("Menu subcategory of Development", "IDE") },
	{ "???::Profiling",		NC_("Menu subcategory of Development", "Profiling") },
	{ "???::ProjectManagement",	NC_("Menu subcategory of Development", "Project Management") },
	{ "???::RevisionControl",	NC_("Menu subcategory of Development", "Revision Control") },
	{ "???::Translation",		NC_("Menu subcategory of Development", "Translation") },
	{ "???::WebDevelopment",	NC_("Menu subcategory of Development", "Web Development") },
	/* TRANSLATORS: this is the menu spec main category for Education */
	{ "Education", 			N_("Education") },
	{ "???::Art",			NC_("Menu subcategory of Education", "Art") },
	{ "???::ArtificialIntelligence", NC_("Menu subcategory of Education", "Artificial Intelligence") },
	{ "???::Astronomy",		NC_("Menu subcategory of Education", "Astronomy") },
	{ "???::Biology",		NC_("Menu subcategory of Education", "Biology") },
	{ "???::Chemistry",		NC_("Menu subcategory of Education", "Chemistry") },
	{ "???::ComputerScience",	NC_("Menu subcategory of Education", "Computer Science") },
	{ "???::Construction",		NC_("Menu subcategory of Education", "Construction") },
	{ "???::DataVisualization",	NC_("Menu subcategory of Education", "Data Visualization") },
	{ "???::Economy",		NC_("Menu subcategory of Education", "Economy") },
	{ "???::Electricity",		NC_("Menu subcategory of Education", "Electricity") },
	{ "???::Electronics",		NC_("Menu subcategory of Education", "Electronics") },
	{ "???::Engineering",		NC_("Menu subcategory of Education", "Engineering") },
	{ "???::Geography",		NC_("Menu subcategory of Education", "Geography") },
	{ "???::Geology",		NC_("Menu subcategory of Education", "Geology") },
	{ "???::Geoscience",		NC_("Menu subcategory of Education", "Geoscience") },
	{ "???::History",		NC_("Menu subcategory of Education", "History") },
	{ "???::Humanities",		NC_("Menu subcategory of Education", "Humanities") },
	{ "???::ImageProcessing",	NC_("Menu subcategory of Education", "Image Processing") },
	{ "???::Languages",		NC_("Menu subcategory of Education", "Languages") },
	{ "???::Literature",		NC_("Menu subcategory of Education", "Literature") },
	{ "???::Maps",			NC_("Menu subcategory of Education", "Maps") },
	{ "???::Math",			NC_("Menu subcategory of Education", "Math") },
	{ "???::MedicalSoftware",	NC_("Menu subcategory of Education", "Medical") },
	{ "???::Music",			NC_("Menu subcategory of Education", "Music") },
	{ "???::NumericalAnalysis",	NC_("Menu subcategory of Education", "Numerical Analysis") },
	{ "???::ParallelComputing",	NC_("Menu subcategory of Education", "Parallel Computing") },
	{ "???::Physics",		NC_("Menu subcategory of Education", "Physics") },
	{ "???::Robotics",		NC_("Menu subcategory of Education", "Robotics") },
	{ "???::Spirituality",		NC_("Menu subcategory of Education", "Spirituality") },
	{ "???::Sports",		NC_("Menu subcategory of Education", "Sports") },
	/* TRANSLATORS: this is the menu spec main category for Games */
	{ "Game", 			N_("Games") },
	{ "???::ActionGame",		NC_("Menu subcategory of Games", "Action") },
	{ "???::AdventureGame",		NC_("Menu subcategory of Games", "Adventure") },
	{ "???::ArcadeGame",		NC_("Menu subcategory of Games", "Arcade") },
	{ "???::BlocksGame",		NC_("Menu subcategory of Games", "Blocks") },
	{ "???::BoardGame",		NC_("Menu subcategory of Games", "Board") },
	{ "???::CardGame",		NC_("Menu subcategory of Games", "Card") },
	{ "???::Emulator",		NC_("Menu subcategory of Games", "Emulators") },
	{ "???::KidsGame",		NC_("Menu subcategory of Games", "Kids") },
	{ "???::LogicGame",		NC_("Menu subcategory of Games", "Logic") },
	{ "???::RolePlaying",		NC_("Menu subcategory of Games", "Role Playing") },
	{ "???::Shooter",		NC_("Menu subcategory of Games", "Shooter") },
	{ "???::Simulation",		NC_("Menu subcategory of Games", "Simulation") },
	{ "???::SportsGame",		NC_("Menu subcategory of Games", "Sports") },
	{ "???::StrategyGame",		NC_("Menu subcategory of Games", "Strategy") },
	/* TRANSLATORS: this is the menu spec main category for Graphics */
	{ "Graphics", 			N_("Graphics") },
	{ "???::2DGraphics",		NC_("Menu subcategory of Graphics", "2D Graphics") },
	{ "???::3DGraphics",		NC_("Menu subcategory of Graphics", "3D Graphics") },
	{ "???::OCR",			NC_("Menu subcategory of Graphics", "OCR") },
	{ "???::Photography",		NC_("Menu subcategory of Graphics", "Photography") },
	{ "???::Publishing",		NC_("Menu subcategory of Graphics", "Publishing") },
	{ "???::RasterGraphics",	NC_("Menu subcategory of Graphics", "Raster Graphics") },
	{ "???::Scanning",		NC_("Menu subcategory of Graphics", "Scanning") },
	{ "???::VectorGraphics",	NC_("Menu subcategory of Graphics", "Vector Graphics") },
	{ "???::Viewer",		NC_("Menu subcategory of Graphics", "Viewer") },
	/* TRANSLATORS: this is the menu spec main category for Network */
	{ "Network", 			N_("Internet") },
	{ "???::Chat",			NC_("Menu subcategory of Network", "Chat") },
	{ "???::Dialup",		NC_("Menu subcategory of Network", "Dialup") },
	{ "???::Email",			NC_("Menu subcategory of Network", "Email") },
	{ "???::Feed",			NC_("Menu subcategory of Network", "Feed") },
	{ "???::FileTransfer",		NC_("Menu subcategory of Network", "File Transfer") },
	{ "???::HamRadio",		NC_("Menu subcategory of Network", "Ham Radio") },
	{ "???::InstantMessaging",	NC_("Menu subcategory of Network", "Instant Messaging") },
	{ "???::IRCClient",		NC_("Menu subcategory of Network", "IRC Clients") },
	{ "???::Monitor",		NC_("Menu subcategory of Network", "Monitor") },
	{ "???::News",			NC_("Menu subcategory of Network", "News") },
	{ "???::P2P",			NC_("Menu subcategory of Network", "P2P") },
	{ "???::RemoteAccess",		NC_("Menu subcategory of Network", "Remote Access") },
	{ "???::Telephony",		NC_("Menu subcategory of Network", "Telephony") },
	{ "???::VideoConference",	NC_("Menu subcategory of Network", "Video Conference") },
	{ "???::WebBrowser",		NC_("Menu subcategory of Network", "Web Browser") },
	{ "???::WebDevelopment",	NC_("Menu subcategory of Network", "Web Development") },
	/* TRANSLATORS: this is the menu spec main category for Office */
	{ "Office", 			N_("Office") },
	{ "???::Calendar",		NC_("Menu subcategory of Office", "Calendar") },
	{ "???::Chart",			NC_("Menu subcategory of Office", "Chart") },
	{ "???::ContactManagement",	NC_("Menu subcategory of Office", "Contact Management") },
	{ "???::Database",		NC_("Menu subcategory of Office", "Database") },
	{ "???::Dictionary",		NC_("Menu subcategory of Office", "Dictionary") },
	{ "???::Email",			NC_("Menu subcategory of Office", "Email") },
	{ "???::Finance",		NC_("Menu subcategory of Office", "Finance") },
	{ "???::FlowChart",		NC_("Menu subcategory of Office", "Flow Chart") },
	{ "???::PDA",			NC_("Menu subcategory of Office", "PDA") },
	{ "???::Photography",		NC_("Menu subcategory of Office", "Photography") },
	{ "???::Presentation",		NC_("Menu subcategory of Office", "Presentation") },
	{ "???::ProjectManagement",	NC_("Menu subcategory of Office", "Project Management") },
	{ "???::Publishing",		NC_("Menu subcategory of Office", "Publishing") },
	{ "???::Spreadsheet",		NC_("Menu subcategory of Office", "Spreadsheet") },
	{ "???::Viewer",		NC_("Menu subcategory of Office", "Viewer") },
	{ "???::WordProcessor",		NC_("Menu subcategory of Office", "Word Processor") },
	/* TRANSLATORS: this is the menu spec main category for Science */
	{ "Science", 			N_("Science") },
	{ "???::Art",			NC_("Menu subcategory of Science", "Art") },
	{ "???::ArtificialIntelligence",NC_("Menu subcategory of Science", "Artificial Intelligence") },
	{ "???::Astronomy",		NC_("Menu subcategory of Science", "Astronomy") },
	{ "???::Biology",		NC_("Menu subcategory of Science", "Biology") },
	{ "???::Chemistry",		NC_("Menu subcategory of Science", "Chemistry") },
	{ "???::ComputerScience",	NC_("Menu subcategory of Science", "Computer Science") },
	{ "???::Construction",		NC_("Menu subcategory of Science", "Construction") },
	{ "???::DataVisualization",	NC_("Menu subcategory of Science", "Data Visualization") },
	{ "???::Economy",		NC_("Menu subcategory of Science", "Economy") },
	{ "???::Electricity",		NC_("Menu subcategory of Science", "Electricity") },
	{ "???::Electronics",		NC_("Menu subcategory of Science", "Electronics") },
	{ "???::Engineering",		NC_("Menu subcategory of Science", "Engineering") },
	{ "???::Geography",		NC_("Menu subcategory of Science", "Geography") },
	{ "???::Geology",		NC_("Menu subcategory of Science", "Geology") },
	{ "???::Geoscience",		NC_("Menu subcategory of Science", "Geoscience") },
	{ "???::History",		NC_("Menu subcategory of Science", "History") },
	{ "???::Humanities",		NC_("Menu subcategory of Science", "Humanities") },
	{ "???::ImageProcessing",	NC_("Menu subcategory of Science", "Image Processing") },
	{ "???::Languages",		NC_("Menu subcategory of Science", "Languages") },
	{ "???::Literature",		NC_("Menu subcategory of Science", "Literature") },
	{ "???::Maps",			NC_("Menu subcategory of Science", "Maps") },
	{ "???::Math",			NC_("Menu subcategory of Science", "Math") },
	{ "???::MedicalSoftware",	NC_("Menu subcategory of Science", "Medical") },
	{ "???::NumericalAnalysis",	NC_("Menu subcategory of Science", "Numerical Analysis") },
	{ "???::ParallelComputing",	NC_("Menu subcategory of Science", "Parallel Computing") },
	{ "???::Physics",		NC_("Menu subcategory of Science", "Physics") },
	{ "???::Robotics",		NC_("Menu subcategory of Science", "Robotics") },
	{ "???::Spirituality",		NC_("Menu subcategory of Science", "Spirituality") },
	{ "???::Sports",		NC_("Menu subcategory of Science", "Sports") },
	/* TRANSLATORS: this is the menu spec main category for System */
	{ "System", 			N_("System") },
	{ "???::Emulator",		NC_("Menu subcategory of System", "Emulator") },
	{ "???::FileManager",		NC_("Menu subcategory of System", "File Manager") },
	{ "???::Filesystem",		NC_("Menu subcategory of System", "File System") },
	{ "???::FileTools",		NC_("Menu subcategory of System", "File Tools") },
	{ "???::Monitor",		NC_("Menu subcategory of System", "Monitor") },
	{ "???::Security",		NC_("Menu subcategory of System", "Security") },
	{ "???::TerminalEmulator",	NC_("Menu subcategory of System", "Terminal Emulator") },
	/* TRANSLATORS: this is the menu spec main category for Utility */
	{ "Utility", 			N_("Utilities") },
	{ "???::Accessibility",		NC_("Menu subcategory of Utility", "Accessibility") },
	{ "???::Archiving",		NC_("Menu subcategory of Utility", "Archiving") },
	{ "???::Calculator",		NC_("Menu subcategory of Utility", "Calculator") },
	{ "???::Clock",			NC_("Menu subcategory of Utility", "Clock") },
	{ "???::Compression",		NC_("Menu subcategory of Utility", "Compression") },
	{ "???::FileTools",		NC_("Menu subcategory of Utility", "File Tools") },
	{ "???::Maps",			NC_("Menu subcategory of Utility", "Maps") },
	{ "???::Spirituality",		NC_("Menu subcategory of Utility", "Spirituality") },
	{ "???::TelephonyTools",	NC_("Menu subcategory of Utility", "Telephony Tools") },
	{ "???::TextEditor",		NC_("Menu subcategory of Utility", "Text Editor") },
	/* TRANSLATORS: this is the menu spec main category for Video */
	{ "Video", 			N_("Video") },
	{ "???::AudioVideoEditing",	NC_("Menu subcategory of Video", "Editing") },
	{ "???::Database",		NC_("Menu subcategory of Video", "Database") },
	{ "???::DiscBurning",		NC_("Menu subcategory of Video", "Disc Burning") },
	{ "???::Player",		NC_("Menu subcategory of Video", "Players") },
	{ "???::Recorder",		NC_("Menu subcategory of Video", "Recorders") },
	{ "???::TV",			NC_("Menu subcategory of Video", "TV") },
	/* TRANSLATORS: this is the main category for Add-ons */
	{ "Addons", 			N_("Add-ons") },
	{ "???::Fonts",			NC_("Menu subcategory of Addons", "Fonts") },
	{ "???::Codecs",		NC_("Menu subcategory of Addons", "Codecs") },
	{ "???::InputSources",		NC_("Menu subcategory of Addons", "Input Sources") },
	{ "???::LanguagePacks",		NC_("Menu subcategory of Addons", "Language Packs") },
	{ NULL,				NULL }
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "hardcoded-menu-spec";
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return -101.0f;
}

/**
 * gs_plugin_add_categories:
 */
gboolean
gs_plugin_add_categories (GsPlugin *plugin,
			  GList **list,
			  GCancellable *cancellable,
			  GError **error)
{
	GsCategory *category = NULL;
	GsCategory *sub;
	gchar *tmp;
	guint i;

	for (i = 0; msdata[i].path != NULL; i++) {
		tmp = g_strstr_len (msdata[i].path, -1, "::");
		if (tmp == NULL) {
			category = gs_category_new (NULL,
						    msdata[i].path,
						    gettext(msdata[i].text));
			*list = g_list_prepend (*list, category);
		} else {
			sub = gs_category_new (category,
					       tmp + 2,
					       gettext(msdata[i].text));
			gs_category_add_subcategory (category, sub);
			g_object_unref (sub);
		}
	}

	return TRUE;
}
