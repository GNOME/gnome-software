/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright 2025 GNOME Foundation, Inc.
 *
 * Author: Philip Withnall <pwithnall@gnome.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <glib.h>
#include <gnome-software.h>

static void
test_is_empty (void)
{
	g_autoptr(GsAppPermissions) permissions = NULL;

	g_test_summary ("Test that checking a set of permissions is empty works");

	permissions = gs_app_permissions_new ();
	g_assert_true (gs_app_permissions_is_empty (permissions));

	gs_app_permissions_add_flag (permissions, GS_APP_PERMISSIONS_FLAGS_X11);
	g_assert_false (gs_app_permissions_is_empty (permissions));
	gs_app_permissions_set_flags (permissions, GS_APP_PERMISSIONS_FLAGS_NONE);
	g_assert_true (gs_app_permissions_is_empty (permissions));

	gs_app_permissions_add_filesystem_read (permissions, "/etc");
	g_assert_false (gs_app_permissions_is_empty (permissions));

	g_clear_object (&permissions);

	permissions = gs_app_permissions_new ();
	gs_app_permissions_add_filesystem_full (permissions, "/usr");
	g_assert_false (gs_app_permissions_is_empty (permissions));

	g_clear_object (&permissions);
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/app-permissions/is-empty", test_is_empty);

	return g_test_run ();
}
