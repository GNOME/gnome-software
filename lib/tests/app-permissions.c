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

	permissions = gs_app_permissions_new ();
	gs_app_permissions_add_bus_policy (permissions, G_BUS_TYPE_SESSION,
					   "org.freedesktop.Flatpak",
					   GS_BUS_POLICY_PERMISSION_TALK);
	g_assert_false (gs_app_permissions_is_empty (permissions));
}

static void
assert_bus_policy_matches (const GsBusPolicy     *policy,
                           GBusType               expected_bus_type,
                           const char            *expected_bus_name,
                           GsBusPolicyPermission  expected_permission)
{
	g_assert_cmpint (policy->bus_type, ==, expected_bus_type);
	g_assert_cmpstr (policy->bus_name, ==, expected_bus_name);
	g_assert_cmpint (policy->permission, ==, expected_permission);
}

static void
test_diff (void)
{
	g_autoptr(GsAppPermissions) old = NULL, new = NULL, diff = NULL;
	const GPtrArray *array;
	const GsBusPolicy * const *bus_policies;
	size_t n_bus_policies;

	g_test_summary ("Test that diffing two sets of permissions works");

	/* Create a couple of apps with some permissions which change a bit from old to new. */
	old = gs_app_permissions_new ();
	gs_app_permissions_set_flags (old,
				      GS_APP_PERMISSIONS_FLAGS_NETWORK |
				      GS_APP_PERMISSIONS_FLAGS_HOME_FULL |
				      GS_APP_PERMISSIONS_FLAGS_X11);
	gs_app_permissions_add_filesystem_read (old, "/etc/cups.conf");
	gs_app_permissions_add_filesystem_read (old, "/var/spool/cron/");
	gs_app_permissions_add_filesystem_full (old, "/tmp/");
	gs_app_permissions_add_filesystem_full (old, "/home/");
	gs_app_permissions_add_bus_policy (old, G_BUS_TYPE_SESSION,
					   "org.freedesktop.Flatpak", GS_BUS_POLICY_PERMISSION_TALK);
	gs_app_permissions_add_bus_policy (old, G_BUS_TYPE_SYSTEM,
					   "org.freedesktop.UDisks2", GS_BUS_POLICY_PERMISSION_SEE);
	gs_app_permissions_add_bus_policy (old, G_BUS_TYPE_SESSION,
					   "org.gnome.Shell", GS_BUS_POLICY_PERMISSION_OWN);
	gs_app_permissions_add_bus_policy (old, G_BUS_TYPE_SYSTEM,
					   "org.systemd.login1", GS_BUS_POLICY_PERMISSION_SEE);
	gs_app_permissions_seal (old);

	new = gs_app_permissions_new ();
	gs_app_permissions_set_flags (new,
				      GS_APP_PERMISSIONS_FLAGS_NETWORK |
				      GS_APP_PERMISSIONS_FLAGS_X11 |
				      GS_APP_PERMISSIONS_FLAGS_SCREEN);
	gs_app_permissions_add_filesystem_read (new, "/var/log/");
	gs_app_permissions_add_filesystem_read (new, "/etc/cups.conf");
	gs_app_permissions_add_bus_policy (new, G_BUS_TYPE_SESSION,
					   "org.freedesktop.Flatpak", GS_BUS_POLICY_PERMISSION_TALK);
	gs_app_permissions_add_bus_policy (new, G_BUS_TYPE_SYSTEM,
					   "org.freedesktop.UDisks2", GS_BUS_POLICY_PERMISSION_TALK);
	gs_app_permissions_add_bus_policy (new, G_BUS_TYPE_SESSION,
					   "org.gnome.Shell", GS_BUS_POLICY_PERMISSION_TALK);
	gs_app_permissions_add_bus_policy (new, G_BUS_TYPE_SESSION,
					   "org.gnome.Nautilus", GS_BUS_POLICY_PERMISSION_TALK);
	gs_app_permissions_seal (new);

	/* Try a diff from old to new. */
	diff = gs_app_permissions_diff (old, new);
	g_assert_true (gs_app_permissions_is_sealed (diff));
	g_assert_false (gs_app_permissions_is_empty (diff));
	g_assert_cmpint (gs_app_permissions_get_flags (diff), ==,
			 GS_APP_PERMISSIONS_FLAGS_SCREEN);

	array = gs_app_permissions_get_filesystem_read (diff);
	g_assert_nonnull (array);
	g_assert_cmpuint (array->len, ==, 1);
	g_assert_cmpstr (array->pdata[0], ==, "/var/log/");

	array = gs_app_permissions_get_filesystem_full (diff);
	g_assert_null (array);

	bus_policies = gs_app_permissions_get_bus_policies (diff, &n_bus_policies);
	g_assert_cmpuint (n_bus_policies, ==, 2);
	g_assert_nonnull (bus_policies);
	assert_bus_policy_matches (bus_policies[0], G_BUS_TYPE_SYSTEM,
				   "org.freedesktop.UDisks2", GS_BUS_POLICY_PERMISSION_TALK);
	assert_bus_policy_matches (bus_policies[1], G_BUS_TYPE_SESSION,
				   "org.gnome.Nautilus", GS_BUS_POLICY_PERMISSION_TALK);

	g_clear_object (&diff);

	/* Diffing the other way round should give a different result. */
	diff = gs_app_permissions_diff (new, old);
	g_assert_true (gs_app_permissions_is_sealed (diff));
	g_assert_false (gs_app_permissions_is_empty (diff));
	g_assert_cmpint (gs_app_permissions_get_flags (diff), ==,
			 GS_APP_PERMISSIONS_FLAGS_HOME_FULL);

	array = gs_app_permissions_get_filesystem_read (diff);
	g_assert_nonnull (array);
	g_assert_cmpuint (array->len, ==, 1);
	g_assert_cmpstr (array->pdata[0], ==, "/var/spool/cron/");

	array = gs_app_permissions_get_filesystem_full (diff);
	g_assert_nonnull (array);
	g_assert_cmpuint (array->len, ==, 2);
	g_assert_cmpstr (array->pdata[0], ==, "/home/");
	g_assert_cmpstr (array->pdata[1], ==, "/tmp/");

	bus_policies = gs_app_permissions_get_bus_policies (diff, &n_bus_policies);
	g_assert_cmpuint (n_bus_policies, ==, 2);
	g_assert_nonnull (bus_policies);
	assert_bus_policy_matches (bus_policies[0], G_BUS_TYPE_SYSTEM,
				   "org.systemd.login1", GS_BUS_POLICY_PERMISSION_SEE);
	assert_bus_policy_matches (bus_policies[1], G_BUS_TYPE_SESSION,
				   "org.gnome.Shell", GS_BUS_POLICY_PERMISSION_OWN);

	g_clear_object (&diff);

	/* Diffing against itself should always give an empty result. */
	diff = gs_app_permissions_diff (old, old);
	g_assert_true (gs_app_permissions_is_sealed (diff));
	g_assert_true (gs_app_permissions_is_empty (diff));
}

int
main (int    argc,
      char **argv)
{
	g_test_init (&argc, &argv, NULL);

	g_test_add_func ("/app-permissions/is-empty", test_is_empty);
	g_test_add_func ("/app-permissions/diff", test_diff);

	return g_test_run ();
}
