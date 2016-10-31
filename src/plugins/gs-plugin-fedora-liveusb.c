/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include <gnome-software.h>

void
gs_plugin_initialize (GsPlugin *plugin)
{
	if (!gs_plugin_check_distro_id (plugin, "fedora"))
		gs_plugin_set_enabled (plugin, FALSE);
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	g_autofree gchar *data = NULL;
	if (!g_file_get_contents ("/proc/cmdline", &data, NULL, error)) {
		g_prefix_error (error, "failed to get kernel command line: ");
		return FALSE;
	}
	g_debug ("kernel command line: %s", data);
	if (g_strstr_len (data, -1, "root=live") != NULL)
		gs_plugin_set_allow_updates (plugin, FALSE);
	return TRUE;
}
