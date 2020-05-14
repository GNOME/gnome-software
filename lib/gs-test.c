/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <stdlib.h>

#include "gs-test.h"

gchar *
gs_test_get_filename (const gchar *testdatadir, const gchar *filename)
{
	gchar *tmp;
	char full_tmp[PATH_MAX];
	g_autofree gchar *path = NULL;
	path = g_build_filename (testdatadir, filename, NULL);
	g_debug ("looking in %s", path);
	tmp = realpath (path, full_tmp);
	if (tmp == NULL)
		return NULL;
	return g_strdup (full_tmp);
}

void
gs_test_flush_main_context (void)
{
	guint cnt = 0;
	while (g_main_context_iteration (NULL, FALSE)) {
		if (cnt == 0)
			g_debug ("clearing pending events...");
		cnt++;
	}
	if (cnt > 0)
		g_debug ("cleared %u events", cnt);
}

/**
 * gs_test_expose_icon_theme_paths:
 *
 * Calculate and set the `GS_SELF_TEST_ICON_THEME_PATH` environment variable
 * to include the current system icon theme paths. This is designed to be called
 * before calling `g_test_init()` with `G_TEST_OPTION_ISOLATE_DIRS`, which will
 * clear the system icon theme paths.
 *
 * As this function calls `g_setenv()`, it must not be called after threads have
 * been spawned.
 *
 * Calling this function is an explicit acknowledgement that the code under test
 * should be accessing the icon theme.
 *
 * Since: 3.38
 */
void
gs_test_expose_icon_theme_paths (void)
{
	const gchar * const *data_dirs;
	g_autoptr(GString) data_dirs_str = NULL;
	g_autofree gchar *data_dirs_joined = NULL;

	data_dirs = g_get_system_data_dirs ();
	data_dirs_str = g_string_new ("");
	for (gsize i = 0; data_dirs[i] != NULL; i++)
		g_string_append_printf (data_dirs_str, "%s%s/icons",
					(data_dirs_str->len > 0) ? ":" : "",
					data_dirs[i]);
	data_dirs_joined = g_string_free (g_steal_pointer (&data_dirs_str), FALSE);
	g_setenv ("GS_SELF_TEST_ICON_THEME_PATH", data_dirs_joined, TRUE);
}
