/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include "gnome-software-private.h"

#include "../gs-css.h"
#include "gs-test.h"

static void
test_css_parsing (void)
{
	const gchar *tmp;
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsCss) css = gs_css_new ();

	/* no IDs */
	ret = gs_css_parse (css, "border: 0;", &error);
	g_assert_no_error (error);
	g_assert (ret);
	tmp = gs_css_get_markup_for_id (css, "tile");
	g_assert_cmpstr (tmp, ==, "border: 0;");

	/* with IDs */
	ret = gs_css_parse (css, "#tile2{\nborder: 0;}\n#name {color: white;\n}", &error);
	g_assert_no_error (error);
	g_assert (ret);
	tmp = gs_css_get_markup_for_id (css, "NotGoingToExist");
	g_assert_cmpstr (tmp, ==, NULL);
	tmp = gs_css_get_markup_for_id (css, "tile2");
	g_assert_cmpstr (tmp, ==, "border: 0;");
	tmp = gs_css_get_markup_for_id (css, "name");
	g_assert_cmpstr (tmp, ==, "color: white;");
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/css/parsing", test_css_parsing);

	return g_test_run ();
}
