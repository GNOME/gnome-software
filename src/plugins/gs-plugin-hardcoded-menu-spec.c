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

//#define SHOW_EMPTY_SUB_CATS

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
	return 0.0f;
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

	/* Audio */
	category = gs_category_new (NULL, "Audio", _("Audio"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"AudioVideoEditing",
								_("Editing")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Database",
								_("Databases")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"DiscBurning",
								_("Disc Burning")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"HamRadio",
								_("Ham Radio")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Midi",
								_("MIDI")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Mixer",
								_("Mixer")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Music",
								_("Music")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Player",
								_("Players")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Recorder",
								_("Recorders")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Sequencer",
								_("Sequencers")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Tuner",
								_("Tuners")));
	*list = g_list_prepend (*list, category);

	/* Development */
	category = gs_category_new (NULL, "Development", _("Development Tools"));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Building",
								_("Building")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"Database",
								_("Databases")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Debugger",
								_("Debuggers")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"GUIDesigner",
								_("GUI Designers")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"IDE",
								_("IDE")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Profiling",
								_("Profiling")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ProjectManagement",
								_("Project Management")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"RevisionControl",
								_("Revision Control")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Translation",
								_("Translation")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"WebDevelopment",
								_("Web Development")));
	*list = g_list_prepend (*list, category);

	/* Education */
	category = gs_category_new (NULL, "Education", _("Education"));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Art",
								_("Art")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"ArtificialIntelligence",
								_("Artificial Intelligence")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Astronomy",
								_("Astronomy")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Biology",
								_("Biology")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Chemistry",
								_("Chemistry")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ComputerScience",
								_("Computer Science")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Construction",
								_("Construction")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"DataVisualization",
								_("Data Visualization")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Economy",
								_("Economy")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Electricity",
								_("Electricity")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Electronics",
								_("Electronics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Engineering",
								_("Engineering")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Geography",
								_("Geography")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Geology",
								_("Geology")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"Geoscience",
								_("Geoscience")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"History",
								_("History")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Humanities",
								_("Humanities")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"ImageProcessing",
								_("Image Processing")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Languages",
								_("Languages")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Literature",
								_("Literature")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Maps",
								_("Maps")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"Math",
								_("Math")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"MedicalSoftware",
								_("Medical")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Music",
								_("Music")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"NumericalAnalysis",
								_("Numerical Analysis")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ParallelComputing",
								_("Parallel Computing")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Physics",
								_("Physics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Robotics",
								_("Robotics")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Spirituality",
								_("Spirituality")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Sports",
								_("Sports")));
#endif
	*list = g_list_prepend (*list, category);

	/* Games */
	category = gs_category_new (NULL, "Game", _("Games"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ActionGame",
								_("Action")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"AdventureGame",
								_("Adventure")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ArcadeGame",
								_("Arcade")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"BlocksGame",
								_("Blocks")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"BoardGame",
								_("Board")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"CardGame",
								_("Card")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Emulator",
								_("Emulators")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"KidsGame",
								_("Kids")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"LogicGame",
								_("Logic")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"RolePlaying",
								_("Role Playing")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Shooter",
								_("Shooter")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"Simulation",
								_("Simulation")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"SportsGame",
								_("Sports")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"StrategyGame",
								_("Strategy")));
	*list = g_list_prepend (*list, category);

	/* Graphics */
	category = gs_category_new (NULL, "Graphics", _("Graphics"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"2DGraphics",
								_("2D Graphics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"3DGraphics",
								_("3D Graphics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"OCR",
								_("OCR")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Photography",
								_("Photography")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Publishing",
								_("Publishing")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"RasterGraphics",
								_("Raster Graphics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Scanning",
								_("Scanning")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"VectorGraphics",
								_("Vector Graphics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Viewer",
								_("Viewer")));
	*list = g_list_prepend (*list, category);

	/* Network */
	category = gs_category_new (NULL, "Network", _("Internet"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Chat",
								_("Chat")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Dialup",
								_("Dialup")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"Email",
								_("Email")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Feed",
								_("Feed")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"FileTransfer",
								_("File Transfer")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"HamRadio",
								_("Ham Radio")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"InstantMessaging",
								_("Instant Messaging")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"IRCClient",
								_("IRC Clients")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Monitor",
								_("Monitor")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"News",
								_("News")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"P2P",
								_("P2P")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"RemoteAccess",
								_("Remote Access")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Telephony",
								_("Telephony")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"VideoConference",
								_("Video Conference")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"WebBrowser",
								_("Web Browser")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"WebDevelopment",
								_("Web Development")));
#endif
	*list = g_list_prepend (*list, category);

	/* Office */
	category = gs_category_new (NULL, "Office", _("Office"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Calendar",
								_("Calendar")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Chart",
								_("Chart")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"ContactManagement",
								_("Contact Management")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Database",
								_("Database")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Dictionary",
								_("Dictionary")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Email",
								_("Email")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Finance",
								_("Finance")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"FlowChart",
								_("Flow Chart")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"PDA",
								_("PDA")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Photography",
								_("Photography")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Presentation",
								_("Presentation")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"ProjectManagement",
								_("Project Management")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Publishing",
								_("Publishing")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Spreadsheet",
								_("Spreadsheet")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Viewer",
								_("Viewer")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"WordProcessor",
								_("Word Processor")));
	*list = g_list_prepend (*list, category);

	/* Science */
	category = gs_category_new (NULL, "Science", _("Science"));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Art",
								_("Art")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"ArtificialIntelligence",
								_("Artificial Intelligence")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Astronomy",
								_("Astronomy")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Biology",
								_("Biology")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Chemistry",
								_("Chemistry")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ComputerScience",
								_("Computer Science")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Construction",
								_("Construction")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"DataVisualization",
								_("DataVisualization")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Economy",
								_("Economy")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Electricity",
								_("Electricity")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Electronics",
								_("Electronics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Engineering",
								_("Engineering")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Geography",
								_("Geography")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Geology",
								_("Geology")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"Geoscience",
								_("Geoscience")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"History",
								_("History")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Humanities",
								_("Humanities")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"ImageProcessing",
								_("Image Processing")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Languages",
								_("Languages")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Literature",
								_("Literature")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"Maps",
								_("Maps")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Math",
								_("Math")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"MedicalSoftware",
								_("Medical Software")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"NumericalAnalysis",
								_("Numerical Analysis")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"ParallelComputing",
								_("Parallel Computing")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Physics",
								_("Physics")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Robotics",
								_("Robotics")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Spirituality",
								_("Spirituality")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Sports",
								_("Sports")));
#endif
	*list = g_list_prepend (*list, category);

	/* Settings */
	category = gs_category_new (NULL, "Settings", _("Settings"));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Accessibility",
								_("Accessibility")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"DesktopSettings",
								_("Desktop Settings")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"HardwareSettings",
								_("Hardware Settings")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"PackageManager",
								_("Package Manager")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Printing",
								_("Printing")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"Security",
								_("Security")));
	*list = g_list_prepend (*list, category);

	/* System */
	category = gs_category_new (NULL, "System", _("System"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Emulator",
								_("Emulator")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"FileManager",
								_("File Manager")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Filesystem",
								_("Filesystem")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"FileTools",
								_("File Tools")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Monitor",
								_("Monitor")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Security",
								_("Security")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"TerminalEmulator",
								_("Terminal Emulator")));
	*list = g_list_prepend (*list, category);

	/* Utility */
	category = gs_category_new (NULL, "Utility", _("Utilities"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Accessibility",
								_("Accessibility")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Archiving",
								_("Archiving")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Calculator",
								_("Calculator")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Clock",
								_("Clock")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Compression",
								_("Compression")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"FileTools",
								_("File Tools")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Maps",
								_("Maps")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"Spirituality",
								_("Spirituality")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"TelephonyTools",
								_("Telephony Tools")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"TextEditor",
								_("Text Editor")));
	*list = g_list_prepend (*list, category);

	/* Video */
	category = gs_category_new (NULL, "Video", _("Video"));
	gs_category_add_subcategory (category, gs_category_new (category,
								"AudioVideoEditing",
								_("Editing")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Database",
								_("Database")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"DiscBurning",
								_("Disc Burning")));
#endif
	gs_category_add_subcategory (category, gs_category_new (category,
								"Player",
								_("Players")));
#ifdef SHOW_EMPTY_SUB_CATS
	gs_category_add_subcategory (category, gs_category_new (category,
								"Recorder",
								_("Recorders")));
	gs_category_add_subcategory (category, gs_category_new (category,
								"TV",
								_("TV")));
#endif
	*list = g_list_prepend (*list, category);

	return TRUE;
}
