/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
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

#include <glib.h>
#include <string.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "gs-markdown.h"
#include "gs-moduleset.h"

static void
moduleset_func (void)
{
	gboolean ret;
	gchar **data;
	GError *error = NULL;
	g_autoptr(GsModuleset) ms = NULL;

	/* not avaiable in make distcheck */
	if (!g_file_test ("./moduleset-test.xml", G_FILE_TEST_EXISTS))
		return;

	ms = gs_moduleset_new ();
	ret = gs_moduleset_parse_filename (ms, "./moduleset-test.xml", &error);
	g_assert_no_error (error);
	g_assert (ret);

	data = gs_moduleset_get_modules (ms,
					 GS_MODULESET_MODULE_KIND_PACKAGE,
					 "gnome3",
					 NULL);
	g_assert (data != NULL);
	g_assert_cmpint (g_strv_length (data), ==, 1);
	g_assert_cmpstr (data[0], ==, "kernel");
	g_assert_cmpstr (data[1], ==, NULL);

	data = gs_moduleset_get_modules (ms,
					 GS_MODULESET_MODULE_KIND_APPLICATION,
					 "gnome3",
					 NULL);
	g_assert (data != NULL);
	g_assert_cmpint (g_strv_length (data), ==, 1);
	g_assert_cmpstr (data[0], ==, "gnome-shell.desktop");
	g_assert_cmpstr (data[1], ==, NULL);
}

static void
gs_markdown_func (void)
{
	gchar *text;
	const gchar *markdown;
	const gchar *markdown_expected;
	g_autoptr(GsMarkdown) md = NULL;

	/* get GsMarkdown object */
	md = gs_markdown_new (GS_MARKDOWN_OUTPUT_PANGO);
	g_assert (md);

	markdown = "OEMs\n"
		   "====\n"
		   " - Bullett\n";
	markdown_expected =
		   "<big>OEMs</big>\n"
		   "• Bullett";
	/* markdown (type2 header) */
	text = gs_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	/* markdown (autocode) */
	markdown = "this is http://www.hughsie.com/with_spaces_in_url inline link\n";
	markdown_expected = "this is <tt>http://www.hughsie.com/with_spaces_in_url</tt> inline link";
	gs_markdown_set_autocode (md, TRUE);
	text = gs_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	/* markdown some invalid header */
	markdown = "*** This software is currently in alpha state ***\n";
	markdown_expected = "<b><i> This software is currently in alpha state </b></i>";
	text = gs_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	/* markdown (complex1) */
	markdown = " - This is a *very*\n"
		   "   short paragraph\n"
		   "   that is not usual.\n"
		   " - Another";
	markdown_expected =
		   "• This is a <i>very</i> short paragraph that is not usual.\n"
		   "• Another";
	text = gs_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	/* markdown (complex1) */
	markdown = "*  This is a *very*\n"
		   "   short paragraph\n"
		   "   that is not usual.\n"
		   "*  This is the second\n"
		   "   bullett point.\n"
		   "*  And the third.\n"
		   " \n"
		   "* * *\n"
		   " \n"
		   "Paragraph one\n"
		   "isn't __very__ long at all.\n"
		   "\n"
		   "Paragraph two\n"
		   "isn't much better.";
	markdown_expected =
		   "• This is a <i>very</i> short paragraph that is not usual.\n"
		   "• This is the second bullett point.\n"
		   "• And the third.\n"
		   "⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯\n"
		   "Paragraph one isn&apos;t <b>very</b> long at all.\n"
		   "Paragraph two isn&apos;t much better.";
	text = gs_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	markdown = "This is a spec file description or\n"
		   "an **update** description in bohdi.\n"
		   "\n"
		   "* * *\n"
		   "# Big title #\n"
		   "\n"
		   "The *following* things 'were' fixed:\n"
		   "- Fix `dave`\n"
		   "* Fubar update because of \"security\"\n";
	markdown_expected =
		   "This is a spec file description or an <b>update</b> description in bohdi.\n"
		   "⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯\n"
		   "<big>Big title</big>\n"
		   "The <i>following</i> things 'were' fixed:\n"
		   "• Fix <tt>dave</tt>\n"
		   "• Fubar update because of \"security\"";
	/* markdown (complex2) */
	text = gs_markdown_parse (md, markdown);
	if (g_strcmp0 (text, markdown_expected) == 0)
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	/* markdown (list with spaces) */
	markdown = "* list seporated with spaces -\n"
		   "  first item\n"
		   "\n"
		   "* second item\n"
		   "\n"
		   "* third item\n";
	markdown_expected =
		   "• list seporated with spaces - first item\n"
		   "• second item\n"
		   "• third item";
	text = gs_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	gs_markdown_set_max_lines (md, 1);

	/* markdown (one line limit) */
	markdown = "* list seporated with spaces -\n"
		   "  first item\n"
		   "* second item\n";
	markdown_expected =
		   "• list seporated with spaces - first item";
	text = gs_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	gs_markdown_set_max_lines (md, 1);

	/* markdown (escaping) */
	markdown = "* list & <spaces>";
	markdown_expected =
		   "• list &amp; &lt;spaces&gt;";
	text = gs_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	/* markdown (URLs) */
	markdown = "this is the http://www.hughsie.com/ coolest site";
	markdown_expected =
		   "this is the "
		   "<a href=\"http://www.hughsie.com/\">http://www.hughsie.com/</a>"
		   " coolest site";
	text = gs_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);

	/* markdown (free text) */
	gs_markdown_set_escape (md, FALSE);
	text = gs_markdown_parse (md, "This isn't a present");
	g_assert_cmpstr (text, ==, "This isn't a present");
	g_free (text);

	/* markdown (autotext underscore) */
	text = gs_markdown_parse (md, "This isn't CONFIG_UEVENT_HELPER_PATH present");
	g_assert_cmpstr (text, ==, "This isn't <tt>CONFIG_UEVENT_HELPER_PATH</tt> present");
	g_free (text);

	/* markdown (end of bullett) */
	markdown = "*Thu Mar 12 12:00:00 2009* Dan Walsh <dwalsh@redhat.com> - 2.0.79-1\n"
		   "- Update to upstream \n"
		   " * Netlink socket handoff patch from Adam Jackson.\n"
		   " * AVC caching of compute_create results by Eric Paris.\n"
		   "\n"
		   "*Tue Mar 10 12:00:00 2009* Dan Walsh <dwalsh@redhat.com> - 2.0.78-5\n"
		   "- Add patch from ajax to accellerate X SELinux \n"
		   "- Update eparis patch\n";
	markdown_expected =
		   "<i>Thu Mar 12 12:00:00 2009</i> Dan Walsh <tt>&lt;dwalsh@redhat.com&gt;</tt> - 2.0.79-1\n"
		   "• Update to upstream\n"
		   "• Netlink socket handoff patch from Adam Jackson.\n"
		   "• AVC caching of compute_create results by Eric Paris.\n"
		   "<i>Tue Mar 10 12:00:00 2009</i> Dan Walsh <tt>&lt;dwalsh@redhat.com&gt;</tt> - 2.0.78-5\n"
		   "• Add patch from ajax to accellerate X SELinux\n"
		   "• Update eparis patch";
	gs_markdown_set_escape (md, TRUE);
	gs_markdown_set_max_lines (md, 1024);
	text = gs_markdown_parse (md, markdown);
	g_assert_cmpstr (text, ==, markdown_expected);
	g_free (text);
}

int
main (int argc, char **argv)
{
	gtk_init (&argc, &argv);
	g_test_init (&argc, &argv, NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/gnome-software/moduleset", moduleset_func);
	g_test_add_func ("/gnome-software/markdown", gs_markdown_func);

	return g_test_run ();
}

/* vim: set noexpandtab: */
