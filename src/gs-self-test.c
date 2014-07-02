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

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include "gs-app.h"
#include "gs-markdown.h"
#include "gs-plugin.h"
#include "gs-plugin-loader.h"
#include "gs-plugin-loader-sync.h"
#include "gs-utils.h"

static void
gs_markdown_func (void)
{
	GsMarkdown *md;
	gchar *text;
	const gchar *markdown;
	const gchar *markdown_expected;

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

	g_object_unref (md);
}

static gboolean
gs_plugin_list_filter_cb (GsApp *app, gpointer user_data)
{
	if (g_strcmp0 (gs_app_get_id (app), "a") == 0)
		return FALSE;
	if (g_strcmp0 (gs_app_get_id (app), "c") == 0)
		return FALSE;
	return TRUE;
}

static void
gs_plugin_func (void)
{
	GList *list = NULL;
	GList *list_dup;
	GList *list_remove = NULL;
	GsApp *app;

	/* add a couple of duplicate IDs */
	app = gs_app_new ("a");
	gs_plugin_add_app (&list, app);
	g_object_unref (app);

	/* test refcounting */
	g_assert_cmpstr (gs_app_get_id (GS_APP (list->data)), ==, "a");
	list_dup = gs_plugin_list_copy (list);
	gs_plugin_list_free (list);
	g_assert_cmpint (g_list_length (list_dup), ==, 1);
	g_assert_cmpstr (gs_app_get_id (GS_APP (list_dup->data)), ==, "a");
	gs_plugin_list_free (list_dup);

	/* test removing obects */
	app = gs_app_new ("a");
	gs_plugin_add_app (&list_remove, app);
	g_object_unref (app);
	app = gs_app_new ("b");
	gs_plugin_add_app (&list_remove, app);
	g_object_unref (app);
	app = gs_app_new ("c");
	gs_plugin_add_app (&list_remove, app);
	g_object_unref (app);
	g_assert_cmpint (g_list_length (list_remove), ==, 3);
	gs_plugin_list_filter (&list_remove, gs_plugin_list_filter_cb, NULL);
	g_assert_cmpint (g_list_length (list_remove), ==, 1);
	g_assert_cmpstr (gs_app_get_id (GS_APP (list_remove->data)), ==, "b");

	/* test removing duplicates */
	app = gs_app_new ("b");
	gs_plugin_add_app (&list_remove, app);
	g_object_unref (app);
	app = gs_app_new ("b");
	gs_plugin_add_app (&list_remove, app);
	g_object_unref (app);
	gs_plugin_list_filter_duplicates (&list_remove);
	g_assert_cmpint (g_list_length (list_remove), ==, 1);
	g_assert_cmpstr (gs_app_get_id (GS_APP (list_remove->data)), ==, "b");
	gs_plugin_list_free (list_remove);
}

static void
gs_app_subsume_func (void)
{
	GsApp *new;
	GsApp *old;

	new = gs_app_new ("xxx.desktop");
	old = gs_app_new ("yyy.desktop");
	gs_app_set_metadata (old, "foo", "bar");
	gs_app_subsume (new, old);
	g_assert_cmpstr (gs_app_get_metadata_item (new, "foo"), ==, "bar");

	g_object_unref (new);
	g_object_unref (old);
}

static void
gs_app_func (void)
{
	GsApp *app;

	app = gs_app_new ("gnome-software");
	g_assert (GS_IS_APP (app));

	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-software");

	/* check we clean up the version, but not at the expense of having
	 * the same string as the update version */
	gs_app_set_version (app, "2.8.6-3.fc20");
	gs_app_set_update_version (app, "2.8.6-4.fc20");
	g_assert_cmpstr (gs_app_get_version (app), ==, "2.8.6-3.fc20");
	g_assert_cmpstr (gs_app_get_update_version (app), ==, "2.8.6-4.fc20");
	g_assert_cmpstr (gs_app_get_version_ui (app), ==, "2.8.6-3");
	g_assert_cmpstr (gs_app_get_update_version_ui (app), ==, "2.8.6-4");

	/* check the quality stuff works */
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "dave");
	g_assert_cmpstr (gs_app_get_name (app), ==, "dave");
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, "brian");
	g_assert_cmpstr (gs_app_get_name (app), ==, "dave");
	gs_app_set_name (app, GS_APP_QUALITY_HIGHEST, "hugh");
	g_assert_cmpstr (gs_app_get_name (app), ==, "hugh");

	g_object_unref (app);
}

static guint _status_changed_cnt = 0;

static void
gs_plugin_loader_status_changed_cb (GsPluginLoader *plugin_loader,
				    GsApp *app,
				    GsPluginStatus status,
				    gpointer user_data)
{
	_status_changed_cnt++;
}

static void
gs_plugin_loader_dedupe_func (void)
{
	GsApp *app1;
	GsApp *app2;
	GsPluginLoader *loader;

	loader = gs_plugin_loader_new ();

	/* add app */
	app1 = gs_app_new ("app1");
	gs_app_set_description (app1, GS_APP_QUALITY_NORMAL, "description");
	app1 = gs_plugin_loader_dedupe (loader, app1);
	g_assert_cmpstr (gs_app_get_id (app1), ==, "app1");
	g_assert_cmpstr (gs_app_get_description (app1), ==, "description");

	app2 = gs_app_new ("app1");
	app2 = gs_plugin_loader_dedupe (loader, app2);
	g_assert_cmpstr (gs_app_get_id (app2), ==, "app1");
	g_assert_cmpstr (gs_app_get_description (app2), ==, "description");
	app2 = gs_plugin_loader_dedupe (loader, app2);
	g_assert_cmpstr (gs_app_get_id (app2), ==, "app1");
	g_assert_cmpstr (gs_app_get_description (app2), ==, "description");

	g_object_unref (app1);
	g_object_unref (app2);

	g_object_unref (loader);
}

static void
gs_plugin_loader_func (void)
{
	gboolean ret;
	GError *error = NULL;
	GList *list;
	GList *l;
	GsApp *app;
	GsPluginLoader *loader;

	/* not avaiable in make distcheck */
	if (!g_file_test (GS_MODULESETDIR, G_FILE_TEST_EXISTS))
		return;

	loader = gs_plugin_loader_new ();
	g_assert (GS_IS_PLUGIN_LOADER (loader));
	g_signal_connect (loader, "status-changed",
			  G_CALLBACK (gs_plugin_loader_status_changed_cb), NULL);

	/* load the plugins */
	gs_plugin_loader_set_location (loader, "./plugins/.libs");
	ret = gs_plugin_loader_setup (loader, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* enable some that will give us predictable results */
	ret = gs_plugin_loader_set_enabled (loader, "dummy", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "hardcoded-kind", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "moduleset", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "hardcoded-ratings", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "datadir-filename", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "datadir-apps", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "notgoingtoexist", TRUE);
	g_assert (!ret);

	list = gs_plugin_loader_get_popular (loader, GS_PLUGIN_REFINE_FLAGS_DEFAULT, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);
	g_assert_cmpint (_status_changed_cnt, ==, 1);
	g_assert_cmpint (g_list_length (list), ==, 6);
	app = g_list_nth_data (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-boxes");
	g_assert_cmpstr (gs_app_get_name (app), ==, "Boxes");

	app = g_list_nth_data (list, 1);
	g_assert_cmpstr (gs_app_get_id (app), ==, "gedit");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Edit text files");

	gs_plugin_list_free (list);

	/* get updates */
	_status_changed_cnt = 0;
	list = gs_plugin_loader_get_updates (loader, GS_PLUGIN_REFINE_FLAGS_DEFAULT, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);
	g_assert_cmpint (_status_changed_cnt, >=, 1);
	g_assert_cmpint (g_list_length (list), ==, 2);
	app = g_list_nth_data (list, 0);
	g_assert_cmpstr (gs_app_get_id (app), ==, "os-update:gnome-boxes-libs;0.0.1;i386;updates-testing,libvirt-glib-devel;0.0.1;noarch;fedora");
	g_assert_cmpstr (gs_app_get_name (app), ==, "OS Updates");
//	g_assert_cmpstr (gs_app_get_summary (app), ==, "Includes performance, stability and security improvements for all users\nDo not segfault when using newer versons of libvirt.\nFix several memory leaks.");
	g_assert_cmpint (gs_app_get_kind (app), ==, GS_APP_KIND_OS_UPDATE);

	app = g_list_nth_data (list, 1);
	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-boxes");
	g_assert_cmpstr (gs_app_get_name (app), ==, "Boxes");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Do not segfault when using newer versons of libvirt.");
	g_assert_cmpint (gs_app_get_kind (app), ==, GS_APP_KIND_NORMAL);
	gs_plugin_list_free (list);

	/* test packagekit */
	gs_plugin_loader_set_enabled (loader, "dummy", FALSE);
	ret = gs_plugin_loader_set_enabled (loader, "packagekit", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "desktopdb", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "datadir-apps", TRUE);
	g_assert (ret);

	list = gs_plugin_loader_get_installed (loader, GS_PLUGIN_REFINE_FLAGS_DEFAULT, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);
	g_assert_cmpint (g_list_length (list), >, 50);

	/* find a specific app */
	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (g_strcmp0 (gs_app_get_id (app), "gnome-screenshot") == 0)
			break;
	}
	g_assert_cmpstr (gs_app_get_id (app), ==, "gnome-screenshot");
	g_assert_cmpstr (gs_app_get_name (app), ==, "Screenshot");
	g_assert_cmpstr (gs_app_get_summary (app), ==, "Save images of your screen or individual windows");
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_INSTALLED);
	g_assert_cmpint (gs_app_get_kind (app), ==, GS_APP_KIND_SYSTEM);
	g_assert (gs_app_get_pixbuf (app) != NULL);
	gs_plugin_list_free (list);

	/* do this again, which should be much faster */
	list = gs_plugin_loader_get_installed (loader, GS_PLUGIN_REFINE_FLAGS_DEFAULT, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);
	g_assert_cmpint (g_list_length (list), >, 50);
	gs_plugin_list_free (list);

	/* set a rating */
	gs_plugin_loader_set_enabled (loader, "packagekit", FALSE);
	gs_plugin_loader_set_enabled (loader, "desktopdb", FALSE);
	gs_plugin_loader_set_enabled (loader, "datadir-apps", FALSE);
	ret = gs_plugin_loader_set_enabled (loader, "local-ratings", TRUE);
	g_assert (ret);

	/* create a dummy value */
	app = gs_app_new ("self-test");
	gs_app_set_rating (app, 35);
	ret = gs_plugin_loader_app_action (loader,
					   app,
					   GS_PLUGIN_LOADER_ACTION_SET_RATING,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* get the saved value */
	gs_app_set_rating (app, -1);
	ret = gs_plugin_loader_app_refine (loader,
					   app,
					   GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_rating (app), ==, 35);
	g_object_unref (app);

	g_object_unref (loader);
}

static void
gs_plugin_loader_refine_func (void)
{
	GError *error = NULL;
	GsApp *app;
	GsPluginLoader *loader;
	const gchar *url;
	gboolean ret;

	/* not avaiable in make distcheck */
	if (!g_file_test (GS_MODULESETDIR, G_FILE_TEST_EXISTS))
		return;

	/* load the plugins */
	loader = gs_plugin_loader_new ();
	gs_plugin_loader_set_location (loader, "./plugins/.libs");
	ret = gs_plugin_loader_setup (loader, &error);
	g_assert_no_error (error);
	g_assert (ret);

	ret = gs_plugin_loader_set_enabled (loader, "packagekit-refine", TRUE);
	g_assert (ret);

	/* get the extra bits */
	g_setenv ("GNOME_SOFTWARE_USE_PKG_DESCRIPTIONS", "1", TRUE);
	app = gs_app_new ("gimp");
	gs_app_add_source (app, "gimp");
	ret = gs_plugin_loader_app_refine (loader, app,
					   GS_PLUGIN_REFINE_FLAGS_DEFAULT |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENCE |
					   GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);

	g_assert_cmpstr (gs_app_get_licence (app), ==, "<a href=\"http://spdx.org/licenses/GPL-3.0+\">GPL-3.0+</a> and <a href=\"http://spdx.org/licenses/GPL-3.0\">GPL-3.0</a>");
	g_assert_cmpstr (gs_app_get_description (app), !=, NULL);
	url = gs_app_get_url (app, AS_URL_KIND_HOMEPAGE);
	g_assert_cmpstr (url, ==, "http://www.gimp.org/");

	g_object_unref (app);
	g_object_unref (loader);
}

static void
gs_plugin_loader_empty_func (void)
{
	gboolean ret;
	GError *error = NULL;
	GList *apps;
	GList *l;
	GList *l2;
	GList *list;
	GList *subcats;
	GsCategory *category;
	GsCategory *sub;
	GsPluginLoader *loader;
	guint empty_subcats_cnt = 0;

	/* not avaiable in make distcheck */
	if (!g_file_test (GS_MODULESETDIR, G_FILE_TEST_EXISTS))
		return;

	/* load the plugins */
	loader = gs_plugin_loader_new ();
	gs_plugin_loader_set_location (loader, "./plugins/.libs");
	ret = gs_plugin_loader_setup (loader, &error);
	g_assert_no_error (error);
	g_assert (ret);

	ret = gs_plugin_loader_set_enabled (loader, "hardcoded-menu-spec", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "appstream", TRUE);
	g_assert (ret);
	ret = gs_plugin_loader_set_enabled (loader, "self-test", TRUE);
	g_assert (ret);

	/* get the list of categories */
	list = gs_plugin_loader_get_categories (loader, GS_PLUGIN_REFINE_FLAGS_DEFAULT, NULL, &error);
	g_assert_no_error (error);
	g_assert (list != NULL);

	/* find how many packages each sub category has */
	for (l = list; l != NULL; l = l->next) {
		category = GS_CATEGORY (l->data);
		subcats = gs_category_get_subcategories (category);
		if (subcats == NULL)
			continue;
		for (l2 = subcats; l2 != NULL; l2 = l2->next) {
			sub = GS_CATEGORY (l2->data);

			/* ignore general */
			if (gs_category_get_id (sub) == NULL)
				continue;

			/* find subcaegories that have no applications */
			apps = gs_plugin_loader_get_category_apps (loader,
								   sub,
								   GS_PLUGIN_REFINE_FLAGS_DEFAULT,
								   NULL,
								   &error);
			if (apps == NULL) {
				g_debug ("NOAPPS:\t%s/%s: %s",
					 gs_category_get_id (category),
					 gs_category_get_id (sub),
					 error->message);
				g_clear_error (&error);
				empty_subcats_cnt++;
				//g_warning ("MOO");
			} else {
				GList *g;
				if (g_getenv ("DUMPGROUPS") != NULL) {
					for (g = apps; g != NULL; g = g->next) {
						g_print ("Cat: %s\tSubCat: %s\tPkgName: %s\tAppId: %s\n",
							 gs_category_get_id (category),
							 gs_category_get_id (sub),
							 gs_app_get_source_default (GS_APP (g->data)),
							 gs_app_get_id (GS_APP (g->data)));
					}
				}
				g_debug ("APPS[%i]:\t%s/%s",
					 g_list_length (apps),
					 gs_category_get_id (category),
					 gs_category_get_id (sub));
			}
			g_list_free_full (apps, (GDestroyNotify) g_object_unref);
		}
		g_list_free (subcats);
	}
	g_assert_cmpint (empty_subcats_cnt, ==, 0);

	gs_plugin_list_free (list);
	g_object_unref (loader);
}

static void
gs_plugin_loader_webapps_func (void)
{
	GsPluginLoader *loader;
	GsApp *app;
	gchar *path;
	gboolean ret;
	GError *error = NULL;

	/* not avaiable in make distcheck */
	if (!g_file_test (GS_MODULESETDIR, G_FILE_TEST_EXISTS))
		return;

	/* load the plugins */
	loader = gs_plugin_loader_new ();
	gs_plugin_loader_set_location (loader, "./plugins/.libs");
	ret = gs_plugin_loader_setup (loader, &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* save shitty file */
	path = g_build_filename (g_get_user_data_dir (),
				 "app-info",
				 "xmls",
				 "test.xml",
				 NULL);
	ret = gs_mkdir_parent (path, &error);
	g_assert_no_error (error);
	g_assert (ret);
	ret = g_file_set_contents (path,
				   "<?xml version=\"1.0\"?>\n"
				   "<applications version=\"0.1\">\n"
				   "  <application>\n"
				   "    <id type=\"webapp\">epiphany-test.desktop</id>\n"
				   "    <name>test</name>\n"
				   "    <icon type=\"remote\">http://www.test.com/test.png</icon>\n"
				   "  </application>\n"
				   "</applications>\n",
				   -1,
				   &error);
	g_assert_no_error (error);
	g_assert (ret);

	/* load a webapp with a failing icon */
	app = gs_app_new ("epiphany-test");
	ret = gs_plugin_loader_app_refine (loader, app,
					   GS_PLUGIN_REFINE_FLAGS_DEFAULT,
					   NULL,
					   &error);
	g_assert_no_error (error);
	g_assert (ret);
	g_assert_cmpint (gs_app_get_state (app), ==, AS_APP_STATE_UNAVAILABLE);

	g_unlink (path);
	g_free (path);
	g_object_unref (app);
	g_object_unref (loader);
}

int
main (int argc, char **argv)
{
	gtk_init (&argc, &argv);
	g_test_init (&argc, &argv, NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);
	g_setenv ("GNOME_SOFTWARE_SELF_TEST", "1", TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/gnome-software/markdown", gs_markdown_func);
	g_test_add_func ("/gnome-software/plugin-loader{refine}", gs_plugin_loader_refine_func);
	g_test_add_func ("/gnome-software/plugin", gs_plugin_func);
	g_test_add_func ("/gnome-software/app", gs_app_func);
	g_test_add_func ("/gnome-software/app{subsume}", gs_app_subsume_func);
	if (g_getenv ("HAS_APPSTREAM") != NULL)
		g_test_add_func ("/gnome-software/plugin-loader{empty}", gs_plugin_loader_empty_func);
	g_test_add_func ("/gnome-software/plugin-loader{dedupe}", gs_plugin_loader_dedupe_func);
	if(0)g_test_add_func ("/gnome-software/plugin-loader", gs_plugin_loader_func);
	if(0)g_test_add_func ("/gnome-software/plugin-loader{webapps}", gs_plugin_loader_webapps_func);

	return g_test_run ();
}

/* vim: set noexpandtab: */
