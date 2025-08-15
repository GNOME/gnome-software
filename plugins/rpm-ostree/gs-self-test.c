/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: (C) 2025 Red Hat <www.redhat.com>
 */

#include "config.h"

#include <locale.h>

#include "gnome-software-private.h"
#include "gs-rpm-ostree-utils.h"
#include "gs-test.h"

static GsApp * /* transfer full */
create_app (const gchar *id,
	    GsAppState state,
	    const gchar *name,
	    const gchar *version,
	    const gchar *update_version,
	    guint install_date, /* YYYYMMDD */
	    const gchar *update_details_markup)
{
	GsApp *app = gs_app_new (id);

	gs_app_set_state (app, state);
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, name);

	if (install_date != 0) {
		g_autoptr(GDateTime) dt = g_date_time_new_utc (install_date / 10000,
							       (install_date / 100) % 100,
							       install_date % 100,
							       0, 0, 0.0);
		gs_app_set_install_date (app, g_date_time_to_unix (dt));
	}
	if (version != NULL)
		gs_app_set_version (app, version);
	if (update_version != NULL)
		gs_app_set_update_version (app, update_version);
	if (update_details_markup != NULL)
		gs_app_set_update_details_markup (app, update_details_markup);

	return app;
}

/* expects NULL-terminated array of GsApp, which it uses as (transfer full) */
static void
verify_split_changelogs (const gchar *locale,
			 const gchar *input,
			 ...) G_GNUC_NULL_TERMINATED;

static void
verify_split_changelogs (const gchar *locale,
			 const gchar *input,
			 ...)
{
	g_autoptr(GPtrArray) expected_apps = g_ptr_array_new_with_free_func (g_object_unref);
	g_autoptr(GsApp) owner_app = gs_app_new (NULL);
	g_autofree gchar *changelogs = g_strdup (input);
	g_autofree gchar *last_locale = NULL;
	GsApp *app;
	GsAppList *related;
	va_list va;

	va_start (va, input);

	for (app = va_arg (va, GsApp *); app != NULL; app = va_arg (va, GsApp *)) {
		g_ptr_array_add (expected_apps, app);
	}

	va_end (va);

	if (locale != NULL) {
		last_locale = g_strdup (setlocale (LC_ALL, locale));
		g_assert_nonnull (last_locale);
	}

	gs_rpm_ostree_refine_app_from_changelogs (owner_app, g_steal_pointer (&changelogs));

	if (last_locale != NULL)
		setlocale (LC_ALL, last_locale);

	related = gs_app_get_related (owner_app);
	g_assert_nonnull (related);
	g_assert_cmpuint (gs_app_list_length (related), ==, expected_apps->len);

	for (guint j = 0; j < gs_app_list_length (related); j++) {
		gboolean found = FALSE;

		app = gs_app_list_index (related, j);

		for (guint i = 0; i < expected_apps->len; i++) {
			GsApp *expected_app = g_ptr_array_index (expected_apps, i);
			if (g_strcmp0 (gs_app_get_id (app), gs_app_get_id (expected_app)) == 0) {
				found = TRUE;
				g_assert_cmpstr (gs_app_get_name (app), ==, gs_app_get_name (expected_app));
				g_assert_cmpint (gs_app_get_state (app), ==, gs_app_get_state (expected_app));
				g_assert_cmpuint (gs_app_get_install_date (app), ==, gs_app_get_install_date (expected_app));
				g_assert_cmpstr (gs_app_get_version (app), ==, gs_app_get_version (expected_app));
				g_assert_cmpstr (gs_app_get_update_version (app), ==, gs_app_get_update_version (expected_app));
				g_assert_cmpstr (gs_app_get_update_details_markup (app), ==, gs_app_get_update_details_markup (expected_app));
				g_ptr_array_remove_index (expected_apps, i);
				break;
			}
		}

		g_assert_true (found);
	}

	/* all expected had been found */
	g_assert_cmpuint (0, ==, expected_apps->len);
}

static void
gs_rpm_ostree_split_changelogs_add (void)
{
	const gchar *input =
		"ostree diff commit from: rollback deployment (1234567890123456789012345678901234567890123456789012345678901234)\n"
		"ostree diff commit to:   booted deployment (0987654321098765432109876543210987654321098765432109876543210987)\n"
		"Added:\n"
		"  vim-common-2:9.1.1623-1.fc43.x86_64\n"
		"  vim-enhanced-2:9.1.1623-1.fc43.x86_64\n"
		"  vim-filesystem-2:9.1.1623-1.fc43.noarch\n"
		"  xxd-2:9.1.1623-1.fc43.x86_64\n";

	verify_split_changelogs (NULL, input,
		create_app ("vim-common-2:9.1.1623-1.fc43.x86_64",
			    GS_APP_STATE_AVAILABLE,
			    "vim-common-2:9.1.1623-1.fc43.x86_64",
			    NULL, NULL, 0, NULL),
		create_app ("vim-enhanced-2:9.1.1623-1.fc43.x86_64",
			    GS_APP_STATE_AVAILABLE,
			    "vim-enhanced-2:9.1.1623-1.fc43.x86_64",
			    NULL, NULL, 0, NULL),
		create_app ("vim-filesystem-2:9.1.1623-1.fc43.noarch",
			    GS_APP_STATE_AVAILABLE,
			    "vim-filesystem-2:9.1.1623-1.fc43.noarch",
			    NULL, NULL, 0, NULL),
		create_app ("xxd-2:9.1.1623-1.fc43.x86_64",
			    GS_APP_STATE_AVAILABLE,
			    "xxd-2:9.1.1623-1.fc43.x86_64",
			    NULL, NULL, 0, NULL),
		NULL);
}

static void
gs_rpm_ostree_split_changelogs_upgrade_en_us (void)
{
	const gchar *input =
		"ostree diff commit from: rollback deployment (1234567890123456789012345678901234567890123456789012345678901234)\n"
		"ostree diff commit to:   booted deployment (0987654321098765432109876543210987654321098765432109876543210987)\n"
		"Upgraded:\n"
		"  ModemManager 1.24.0-2.fc43.x86_64 -> 1.24.2-1.fc43.x86_64\n"
		"  ModemManager-glib 1.24.0-2.fc43.x86_64 -> 1.24.2-1.fc43.x86_64\n"
		"    * Tue Aug 12 2025 User1 Name1 <user1@no.where> - 1.24.2-1\n"
		"    - Update to 1.24.2\n"
		"\n"
		"    * Mon Aug 11 2025 User2 Name2 <user2@no.where> - 1.24.0-3\n"
		"    - Fix libmbim BR\n"
		"\n"
		"  gnome-software 49~beta-3.fc43.x86_64 -> 49~beta-4.fc43.x86_64\n"
		"  gnome-software-rpm-ostree 49~beta-3.fc43.x86_64 -> 49~beta-4.fc43.x86_64\n"
		"    * Thu Jul 31 2025 User3 Name3 <user3@no.where> - 49~beta-4\n"
		"    - Do some package fixes\n"
		"\n"
		"  libva-intel-media-driver 25.2.6-2.fc43.x86_64 -> 25.2.6-3.fc43.x86_64\n"
		"    * Wed Jul 30 2025 User4, Name4 <user4@no.where> - 25.2.6-3\n"
		"    - Turn cmrtlib ON\n"
		"\n"
		"  python3-boto3 1.40.8-1.fc43.noarch -> 1.40.9-1.fc43.noarch\n"
		"    * Wed Aug 13 2025 User5 Name5 <user5@no.where> - 1.40.9-1\n"
		"    - 1.40.9\n"
		"    - multiline log\n"
		"\n"
		"  python3-botocore 1.40.8-1.fc43.noarch -> 1.40.9-1.fc43.noarch\n"
		"    * Wed Aug 13 2025 User6 Name6 <user6@no.where> - 1.40.9-1\n"
		"    - 1.40.9\n";

	verify_split_changelogs ("en_US.utf8", input,
		create_app ("ModemManager",
			    GS_APP_STATE_UPDATABLE,
			    "ModemManager",
			    "1.24.0-2.fc43.x86_64",
			    "1.24.2-1.fc43.x86_64",
			    20250812,
			    "* Tue Aug 12 2025 User1 Name1 - 1.24.2-1\n"
			    "- Update to 1.24.2\n"
			    "\n"
			    "* Mon Aug 11 2025 User2 Name2 - 1.24.0-3\n"
			    "- Fix libmbim BR"),
		create_app ("gnome-software",
			    GS_APP_STATE_UPDATABLE,
			    "gnome-software",
			    "49~beta-3.fc43.x86_64",
			    "49~beta-4.fc43.x86_64",
			    20250731,
			    "* Thu Jul 31 2025 User3 Name3 - 49~beta-4\n"
			    "- Do some package fixes"),
		create_app ("libva-intel-media-driver",
			    GS_APP_STATE_UPDATABLE,
			    "libva-intel-media-driver",
			    "25.2.6-2.fc43.x86_64",
			    "25.2.6-3.fc43.x86_64",
			    20250730,
			    "* Wed Jul 30 2025 User4, Name4 - 25.2.6-3\n"
			    "- Turn cmrtlib ON"),
		create_app ("python3-boto3",
			    GS_APP_STATE_UPDATABLE,
			    "python3-boto3",
			    "1.40.8-1.fc43.noarch",
			    "1.40.9-1.fc43.noarch",
			    20250813,
			    "* Wed Aug 13 2025 User5 Name5 - 1.40.9-1\n"
			    "- 1.40.9\n"
			    "- multiline log"),
		create_app ("python3-botocore",
			    GS_APP_STATE_UPDATABLE,
			    "python3-botocore",
			    "1.40.8-1.fc43.noarch",
			    "1.40.9-1.fc43.noarch",
			    20250813,
			    "* Wed Aug 13 2025 User6 Name6 - 1.40.9-1\n"
			    "- 1.40.9"),
		NULL);
}

static void
gs_rpm_ostree_split_changelogs_upgrade_cs_cz (void)
{
	const gchar *input =
		"ostree diff commit from: rollback deployment (1234567890123456789012345678901234567890123456789012345678901234)\n"
		"ostree diff commit to:   booted deployment (0987654321098765432109876543210987654321098765432109876543210987)\n"
		"Upgraded:\n"
		"  jxl-pixbuf-loader 1:0.11.1-3.fc43.x86_64 -> 1:0.11.1-4.fc43.x86_64\n"
		"  libjxl 1:0.11.1-3.fc43.x86_64 -> 1:0.11.1-4.fc43.x86_64\n"
		"    * čt čec 31 2025 User1 'nick' Name1 <user1@no.where> - 1:0.11.1-4\n"
		"    - enable tests\n"
		"\n"
		"  python3-boto3 1.40.7-1.fc43.noarch -> 1.40.8-1.fc43.noarch\n"
		"    * út srp 12 2025 User2 Name2 <user2@no.where> - 1.40.8-1\n"
		"    - 1.40.8\n"
		"    - multiline log\n"
		"\n"
		"  python3-botocore 1.40.7-1.fc43.noarch -> 1.40.8-1.fc43.noarch\n"
		"    * út srp 12 2025 User3 Name3 <user3@no.where> - 1.40.8-1\n"
		"    - 1.40.8\n"
		"\n"
		"  xdg-desktop-portal-gnome 49~alpha-2.fc43.x86_64 -> 49~beta-1.fc43.x86_64\n"
		"    * st srp 13 2025 Name4 <user4@no.where> - 49~beta-1\n"
		"    - Update to 49.beta\n";

	verify_split_changelogs ("cs_CZ.utf8", input,
		create_app ("jxl-pixbuf-loader",
			    GS_APP_STATE_UPDATABLE,
			    "jxl-pixbuf-loader",
			    "1:0.11.1-3.fc43.x86_64",
			    "1:0.11.1-4.fc43.x86_64",
			    20250731,
			    "* čt čec 31 2025 User1 &apos;nick&apos; Name1 - 1:0.11.1-4\n"
			    "- enable tests"),
		create_app ("python3-boto3",
			    GS_APP_STATE_UPDATABLE,
			    "python3-boto3",
			    "1.40.7-1.fc43.noarch",
			    "1.40.8-1.fc43.noarch",
			    20250812,
			    "* út srp 12 2025 User2 Name2 - 1.40.8-1\n"
			    "- 1.40.8\n"
			    "- multiline log"),
		create_app ("python3-botocore",
			    GS_APP_STATE_UPDATABLE,
			    "python3-botocore",
			    "1.40.7-1.fc43.noarch",
			    "1.40.8-1.fc43.noarch",
			    20250812,
			    "* út srp 12 2025 User3 Name3 - 1.40.8-1\n"
			    "- 1.40.8"),
		create_app ("xdg-desktop-portal-gnome",
			    GS_APP_STATE_UPDATABLE,
			    "xdg-desktop-portal-gnome",
			    "49~alpha-2.fc43.x86_64",
			    "49~beta-1.fc43.x86_64",
			    20250813,
			    "* st srp 13 2025 Name4 - 49~beta-1\n"
			    "- Update to 49.beta"),
		NULL);
}

static void
gs_rpm_ostree_split_changelogs_downgrade (void)
{
	const gchar *input =
		"ostree diff commit from: rollback deployment (1234567890123456789012345678901234567890123456789012345678901234)\n"
		"ostree diff commit to:   booted deployment (0987654321098765432109876543210987654321098765432109876543210987)\n"
		"Downgraded:\n"
		"  jxl-pixbuf-loader 1:0.11.1-4.fc43.x86_64 -> 1:0.11.1-3.fc43.x86_64\n"
		"  libjxl 1:0.11.1-4.fc43.x86_64 -> 1:0.11.1-3.fc43.x86_64\n"
		"  xdg-desktop-portal-gnome 49~beta-1.fc43.x86_64 -> 49~alpha-2.fc43.x86_64\n";

	verify_split_changelogs (NULL, input,
		create_app ("jxl-pixbuf-loader",
			    GS_APP_STATE_UPDATABLE,
			    "jxl-pixbuf-loader",
			    "1:0.11.1-4.fc43.x86_64",
			    "1:0.11.1-3.fc43.x86_64",
			    0, NULL),
		create_app ("libjxl",
			    GS_APP_STATE_UPDATABLE,
			    "libjxl",
			    "1:0.11.1-4.fc43.x86_64",
			    "1:0.11.1-3.fc43.x86_64",
			    0, NULL),
		create_app ("xdg-desktop-portal-gnome",
			    GS_APP_STATE_UPDATABLE,
			    "xdg-desktop-portal-gnome",
			    "49~beta-1.fc43.x86_64",
			    "49~alpha-2.fc43.x86_64",
			    0, NULL),
		NULL);
}

static void
gs_rpm_ostree_split_changelogs_remove (void)
{
	const gchar *input =
		"ostree diff commit from: rollback deployment (1234567890123456789012345678901234567890123456789012345678901234)\n"
		"ostree diff commit to:   booted deployment (0987654321098765432109876543210987654321098765432109876543210987)\n"
		"Removed:\n"
		"  vim-common-2:9.1.1623-1.fc43.x86_64\n"
		"  xxd-2:9.1.1623-1.fc43.x86_64\n";

	verify_split_changelogs (NULL, input,
		create_app ("vim-common-2:9.1.1623-1.fc43.x86_64",
			    GS_APP_STATE_UNAVAILABLE,
			    "vim-common-2:9.1.1623-1.fc43.x86_64",
			    NULL, NULL, 0, NULL),
		create_app ("xxd-2:9.1.1623-1.fc43.x86_64",
			    GS_APP_STATE_UNAVAILABLE,
			    "xxd-2:9.1.1623-1.fc43.x86_64",
			    NULL, NULL, 0, NULL),
		NULL);
}

static void
gs_rpm_ostree_split_changelogs_mix (void)
{
	const gchar *input =
		"ostree diff commit from: rollback deployment (1234567890123456789012345678901234567890123456789012345678901234)\n"
		"ostree diff commit to:   booted deployment (0987654321098765432109876543210987654321098765432109876543210987)\n"
		"Added:\n"
		"  vim-common2-2:9.1.1623-1.fc43.x86_64\n"
		"  vim-enhanced2-2:9.1.1623-1.fc43.x86_64\n"
		"  xxd2-2:9.1.1623-1.fc43.x86_64\n"
		"\n"
		"Upgraded:\n"
		"  ModemManager 1.24.0-2.fc43.x86_64 -> 1.24.2-1.fc43.x86_64\n"
		"  ModemManager-glib 1.24.0-2.fc43.x86_64 -> 1.24.2-1.fc43.x86_64\n"
		"    * Tue Aug 12 2025 User1 Name1 <user1@no.where> - 1.24.2-1\n"
		"    - Update to 1.24.2\n"
		"\n"
		"    * Mon Aug 11 2025 User2 Name2 <user2@no.where> - 1.24.0-3\n"
		"    - Fix libmbim BR\n"
		"\n"
		"  libva-intel-media-driver 25.2.6-2.fc43.x86_64 -> 25.2.6-3.fc43.x86_64\n"
		"    * Wed Jul 30 2025 User4, Name4 <user4@no.where> - 25.2.6-3\n"
		"    - Turn cmrtlib ON\n"
		"\n"
		"Downgraded:\n"
		"  jxl-pixbuf-loader 1:0.11.1-4.fc43.x86_64 -> 1:0.11.1-3.fc43.x86_64\n"
		"\n"
		"Removed:\n"
		"  vim-common-2:9.1.1623-1.fc43.x86_64\n"
		"  xxd-2:9.1.1623-1.fc43.x86_64\n";

	verify_split_changelogs ("en_US.utf8", input,
		create_app ("vim-common2-2:9.1.1623-1.fc43.x86_64",
			    GS_APP_STATE_AVAILABLE,
			    "vim-common2-2:9.1.1623-1.fc43.x86_64",
			    NULL, NULL, 0, NULL),
		create_app ("vim-enhanced2-2:9.1.1623-1.fc43.x86_64",
			    GS_APP_STATE_AVAILABLE,
			    "vim-enhanced2-2:9.1.1623-1.fc43.x86_64",
			    NULL, NULL, 0, NULL),
		create_app ("xxd2-2:9.1.1623-1.fc43.x86_64",
			    GS_APP_STATE_AVAILABLE,
			    "xxd2-2:9.1.1623-1.fc43.x86_64",
			    NULL, NULL, 0, NULL),
		create_app ("ModemManager",
			    GS_APP_STATE_UPDATABLE,
			    "ModemManager",
			    "1.24.0-2.fc43.x86_64",
			    "1.24.2-1.fc43.x86_64",
			    20250812,
			    "* Tue Aug 12 2025 User1 Name1 - 1.24.2-1\n"
			    "- Update to 1.24.2\n"
			    "\n"
			    "* Mon Aug 11 2025 User2 Name2 - 1.24.0-3\n"
			    "- Fix libmbim BR"),
		create_app ("libva-intel-media-driver",
			    GS_APP_STATE_UPDATABLE,
			    "libva-intel-media-driver",
			    "25.2.6-2.fc43.x86_64",
			    "25.2.6-3.fc43.x86_64",
			    20250730,
			    "* Wed Jul 30 2025 User4, Name4 - 25.2.6-3\n"
			    "- Turn cmrtlib ON"),
		create_app ("jxl-pixbuf-loader",
			    GS_APP_STATE_UPDATABLE,
			    "jxl-pixbuf-loader",
			    "1:0.11.1-4.fc43.x86_64",
			    "1:0.11.1-3.fc43.x86_64",
			    0, NULL),
		create_app ("vim-common-2:9.1.1623-1.fc43.x86_64",
			    GS_APP_STATE_UNAVAILABLE,
			    "vim-common-2:9.1.1623-1.fc43.x86_64",
			    NULL, NULL, 0, NULL),
		create_app ("xxd-2:9.1.1623-1.fc43.x86_64",
			    GS_APP_STATE_UNAVAILABLE,
			    "xxd-2:9.1.1623-1.fc43.x86_64",
			    NULL, NULL, 0, NULL),
		NULL);
}

int
main (int argc, char **argv)
{
	gs_test_init (&argc, &argv);

	g_test_add_func ("/rpm-ostree/split-changelogs/add", gs_rpm_ostree_split_changelogs_add);
	g_test_add_func ("/rpm-ostree/split-changelogs/upgrade-en_US", gs_rpm_ostree_split_changelogs_upgrade_en_us);
	g_test_add_func ("/rpm-ostree/split-changelogs/upgrade-cs_CZ", gs_rpm_ostree_split_changelogs_upgrade_cs_cz);
	g_test_add_func ("/rpm-ostree/split-changelogs/downgrade", gs_rpm_ostree_split_changelogs_downgrade);
	g_test_add_func ("/rpm-ostree/split-changelogs/remove", gs_rpm_ostree_split_changelogs_remove);
	g_test_add_func ("/rpm-ostree/split-changelogs/mix", gs_rpm_ostree_split_changelogs_mix);

	return g_test_run ();
}
