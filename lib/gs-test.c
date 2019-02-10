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
