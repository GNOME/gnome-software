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
	GsCategory *category;

	/* TRANSLATORS: this is the menu spec main category for Audio */
	category = gs_category_new (NULL, "Audio", _("Audio"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"AudioVideoEditing",
								C_("Menu subcategory of Audio", "Editing")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Database",
								C_("Menu subcategory of Audio", "Databases")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"DiscBurning",
								C_("Menu subcategory of Audio", "Disc Burning")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"HamRadio",
								C_("Menu subcategory of Audio", "Ham Radio")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Midi",
								C_("Menu subcategory of Audio", "MIDI")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Mixer",
								C_("Menu subcategory of Audio", "Mixer")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Music",
								C_("Menu subcategory of Audio", "Music")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Player",
								C_("Menu subcategory of Audio", "Players")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Recorder",
								C_("Menu subcategory of Audio", "Recorders")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Sequencer",
								C_("Menu subcategory of Audio", "Sequencers")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Tuner",
								C_("Menu subcategory of Audio", "Tuners")));
	*list = g_list_prepend (*list, category);

	/* TRANSLATORS: this is the menu spec main category for Development */
	category = gs_category_new (NULL, "Development", _("Development Tools"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Building",
								C_("Menu subcategory of Development", "Building")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Database",
								C_("Menu subcategory of Development", "Databases")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Debugger",
								C_("Menu subcategory of Development", "Debuggers")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"GUIDesigner",
								C_("Menu subcategory of Development", "GUI Designers")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"IDE",
								C_("Menu subcategory of Development", "IDE")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Profiling",
								C_("Menu subcategory of Development", "Profiling")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ProjectManagement",
								C_("Menu subcategory of Development", "Project Management")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"RevisionControl",
								C_("Menu subcategory of Development", "Revision Control")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Translation",
								C_("Menu subcategory of Development", "Translation")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"WebDevelopment",
								C_("Menu subcategory of Development", "Web Development")));
	*list = g_list_prepend (*list, category);

	/* TRANSLATORS: this is the menu spec main category for Education */
	category = gs_category_new (NULL, "Education", _("Education"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Art",
								C_("Menu subcategory of Education", "Art")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ArtificialIntelligence",
								C_("Menu subcategory of Education", "Artificial Intelligence")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Astronomy",
								C_("Menu subcategory of Education", "Astronomy")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Biology",
								C_("Menu subcategory of Education", "Biology")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Chemistry",
								C_("Menu subcategory of Education", "Chemistry")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ComputerScience",
								C_("Menu subcategory of Education", "Computer Science")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Construction",
								C_("Menu subcategory of Education", "Construction")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"DataVisualization",
								C_("Menu subcategory of Education", "Data Visualization")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Economy",
								C_("Menu subcategory of Education", "Economy")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Electricity",
								C_("Menu subcategory of Education", "Electricity")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Electronics",
								C_("Menu subcategory of Education", "Electronics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Engineering",
								C_("Menu subcategory of Education", "Engineering")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Geography",
								C_("Menu subcategory of Education", "Geography")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Geology",
								C_("Menu subcategory of Education", "Geology")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Geoscience",
								C_("Menu subcategory of Education", "Geoscience")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"History",
								C_("Menu subcategory of Education", "History")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Humanities",
								C_("Menu subcategory of Education", "Humanities")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ImageProcessing",
								C_("Menu subcategory of Education", "Image Processing")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Languages",
								C_("Menu subcategory of Education", "Languages")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Literature",
								C_("Menu subcategory of Education", "Literature")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Maps",
								C_("Menu subcategory of Education", "Maps")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Math",
								C_("Menu subcategory of Education", "Math")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"MedicalSoftware",
								C_("Menu subcategory of Education", "Medical")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Music",
								C_("Menu subcategory of Education", "Music")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"NumericalAnalysis",
								C_("Menu subcategory of Education", "Numerical Analysis")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ParallelComputing",
								C_("Menu subcategory of Education", "Parallel Computing")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Physics",
								C_("Menu subcategory of Education", "Physics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Robotics",
								C_("Menu subcategory of Education", "Robotics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Spirituality",
								C_("Menu subcategory of Education", "Spirituality")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Sports",
								C_("Menu subcategory of Education", "Sports")));
	*list = g_list_prepend (*list, category);

	/* TRANSLATORS: this is the menu spec main category for Games */
	category = gs_category_new (NULL, "Game", _("Games"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ActionGame",
								C_("Menu subcategory of Games", "Action")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"AdventureGame",
								C_("Menu subcategory of Games", "Adventure")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ArcadeGame",
								C_("Menu subcategory of Games", "Arcade")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"BlocksGame",
								C_("Menu subcategory of Games", "Blocks")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"BoardGame",
								C_("Menu subcategory of Games", "Board")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"CardGame",
								C_("Menu subcategory of Games", "Card")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Emulator",
								C_("Menu subcategory of Games", "Emulators")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"KidsGame",
								C_("Menu subcategory of Games", "Kids")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"LogicGame",
								C_("Menu subcategory of Games", "Logic")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"RolePlaying",
								C_("Menu subcategory of Games", "Role Playing")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Shooter",
								C_("Menu subcategory of Games", "Shooter")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Simulation",
								C_("Menu subcategory of Games", "Simulation")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"SportsGame",
								C_("Menu subcategory of Games", "Sports")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"StrategyGame",
								C_("Menu subcategory of Games", "Strategy")));
	*list = g_list_prepend (*list, category);

	/* TRANSLATORS: this is the menu spec main category for Graphics */
	category = gs_category_new (NULL, "Graphics", _("Graphics"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"2DGraphics",
								C_("Menu subcategory of Graphics", "2D Graphics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"3DGraphics",
								C_("Menu subcategory of Graphics", "3D Graphics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"OCR",
								C_("Menu subcategory of Graphics", "OCR")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Photography",
								C_("Menu subcategory of Graphics", "Photography")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Publishing",
								C_("Menu subcategory of Graphics", "Publishing")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"RasterGraphics",
								C_("Menu subcategory of Graphics", "Raster Graphics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Scanning",
								C_("Menu subcategory of Graphics", "Scanning")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"VectorGraphics",
								C_("Menu subcategory of Graphics", "Vector Graphics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Viewer",
								C_("Menu subcategory of Graphics", "Viewer")));
	*list = g_list_prepend (*list, category);

	/* TRANSLATORS: this is the menu spec main category for Network */
	category = gs_category_new (NULL, "Network", _("Internet"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Chat",
								C_("Menu subcategory of Network", "Chat")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Dialup",
								C_("Menu subcategory of Network", "Dialup")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Email",
								C_("Menu subcategory of Network", "Email")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Feed",
								C_("Menu subcategory of Network", "Feed")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"FileTransfer",
								C_("Menu subcategory of Network", "File Transfer")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"HamRadio",
								C_("Menu subcategory of Network", "Ham Radio")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"InstantMessaging",
								C_("Menu subcategory of Network", "Instant Messaging")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"IRCClient",
								C_("Menu subcategory of Network", "IRC Clients")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Monitor",
								C_("Menu subcategory of Network", "Monitor")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"News",
								C_("Menu subcategory of Network", "News")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"P2P",
								C_("Menu subcategory of Network", "P2P")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"RemoteAccess",
								C_("Menu subcategory of Network", "Remote Access")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Telephony",
								C_("Menu subcategory of Network", "Telephony")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"VideoConference",
								C_("Menu subcategory of Network", "Video Conference")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"WebBrowser",
								C_("Menu subcategory of Network", "Web Browser")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"WebDevelopment",
								C_("Menu subcategory of Network", "Web Development")));
	*list = g_list_prepend (*list, category);

	/* TRANSLATORS: this is the menu spec main category for Office */
	category = gs_category_new (NULL, "Office", _("Office"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Calendar",
								C_("Menu subcategory of Office", "Calendar")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Chart",
								C_("Menu subcategory of Office", "Chart")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ContactManagement",
								C_("Menu subcategory of Office", "Contact Management")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Database",
								C_("Menu subcategory of Office", "Database")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Dictionary",
								C_("Menu subcategory of Office", "Dictionary")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Email",
								C_("Menu subcategory of Office", "Email")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Finance",
								C_("Menu subcategory of Office", "Finance")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"FlowChart",
								C_("Menu subcategory of Office", "Flow Chart")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"PDA",
								C_("Menu subcategory of Office", "PDA")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Photography",
								C_("Menu subcategory of Office", "Photography")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Presentation",
								C_("Menu subcategory of Office", "Presentation")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ProjectManagement",
								C_("Menu subcategory of Office", "Project Management")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Publishing",
								C_("Menu subcategory of Office", "Publishing")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Spreadsheet",
								C_("Menu subcategory of Office", "Spreadsheet")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Viewer",
								C_("Menu subcategory of Office", "Viewer")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"WordProcessor",
								C_("Menu subcategory of Office", "Word Processor")));
	*list = g_list_prepend (*list, category);

	/* TRANSLATORS: this is the menu spec main category for Science */
	category = gs_category_new (NULL, "Science", _("Science"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Art",
								C_("Menu subcategory of Science", "Art")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ArtificialIntelligence",
								C_("Menu subcategory of Science", "Artificial Intelligence")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Astronomy",
								C_("Menu subcategory of Science", "Astronomy")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Biology",
								C_("Menu subcategory of Science", "Biology")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Chemistry",
								C_("Menu subcategory of Science", "Chemistry")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ComputerScience",
								C_("Menu subcategory of Science", "Computer Science")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Construction",
								C_("Menu subcategory of Science", "Construction")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"DataVisualization",
								C_("Menu subcategory of Science", "Data Visualization")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Economy",
								C_("Menu subcategory of Science", "Economy")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Electricity",
								C_("Menu subcategory of Science", "Electricity")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Electronics",
								C_("Menu subcategory of Science", "Electronics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Engineering",
								C_("Menu subcategory of Science", "Engineering")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Geography",
								C_("Menu subcategory of Science", "Geography")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Geology",
								C_("Menu subcategory of Science", "Geology")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Geoscience",
								C_("Menu subcategory of Science", "Geoscience")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"History",
								C_("Menu subcategory of Science", "History")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Humanities",
								C_("Menu subcategory of Science", "Humanities")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ImageProcessing",
								C_("Menu subcategory of Science", "Image Processing")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Languages",
								C_("Menu subcategory of Science", "Languages")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Literature",
								C_("Menu subcategory of Science", "Literature")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Maps",
								C_("Menu subcategory of Science", "Maps")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Math",
								C_("Menu subcategory of Science", "Math")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"MedicalSoftware",
								C_("Menu subcategory of Science", "Medical")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"NumericalAnalysis",
								C_("Menu subcategory of Science", "Numerical Analysis")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ParallelComputing",
								C_("Menu subcategory of Science", "Parallel Computing")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Physics",
								C_("Menu subcategory of Science", "Physics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Robotics",
								C_("Menu subcategory of Science", "Robotics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Spirituality",
								C_("Menu subcategory of Science", "Spirituality")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Sports",
								C_("Menu subcategory of Science", "Sports")));
	*list = g_list_prepend (*list, category);

	/* TRANSLATORS: this is the menu spec main category for System */
	category = gs_category_new (NULL, "System", _("System"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Emulator",
								C_("Menu subcategory of System", "Emulator")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"FileManager",
								C_("Menu subcategory of System", "File Manager")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Filesystem",
								C_("Menu subcategory of System", "File System")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"FileTools",
								C_("Menu subcategory of System", "File Tools")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Monitor",
								C_("Menu subcategory of System", "Monitor")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Security",
								C_("Menu subcategory of System", "Security")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"TerminalEmulator",
								C_("Menu subcategory of System", "Terminal Emulator")));
	*list = g_list_prepend (*list, category);

	/* TRANSLATORS: this is the menu spec main category for Utility */
	category = gs_category_new (NULL, "Utility", _("Utilities"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Accessibility",
								C_("Menu subcategory of Utility", "Accessibility")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Archiving",
								C_("Menu subcategory of Utility", "Archiving")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Calculator",
								C_("Menu subcategory of Utility", "Calculator")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Clock",
								C_("Menu subcategory of Utility", "Clock")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Compression",
								C_("Menu subcategory of Utility", "Compression")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"FileTools",
								C_("Menu subcategory of Utility", "File Tools")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Maps",
								C_("Menu subcategory of Utility", "Maps")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Spirituality",
								C_("Menu subcategory of Utility", "Spirituality")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"TelephonyTools",
								C_("Menu subcategory of Utility", "Telephony Tools")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"TextEditor",
								C_("Menu subcategory of Utility", "Text Editor")));
	*list = g_list_prepend (*list, category);

	/* TRANSLATORS: this is the menu spec main category for Video */
	category = gs_category_new (NULL, "Video", _("Video"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"AudioVideoEditing",
								C_("Menu subcategory of Video", "Editing")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Database",
								C_("Menu subcategory of Video", "Database")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"DiscBurning",
								C_("Menu subcategory of Video", "Disc Burning")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Player",
								C_("Menu subcategory of Video", "Players")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Recorder",
								C_("Menu subcategory of Video", "Recorders")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"TV",
								C_("Menu subcategory of Video", "TV")));
	*list = g_list_prepend (*list, category);

	/* TRANSLATORS: this is the main category for Add-ons */
	category = gs_category_new (NULL, "Addons", _("Add-ons"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Fonts",
								C_("Menu subcategory of Addons", "Fonts")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Codecs",
								C_("Menu subcategory of Addons", "Codecs")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"InputSources",
								C_("Menu subcategory of Addons", "Input Sources")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"LanguagePacks",
								C_("Menu subcategory of Addons", "Language Packs")));

	*list = g_list_prepend (*list, category);

	return TRUE;
}
