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

#include <gnome-software.h>
#include <glib/gi18n.h>

#include "menu-spec-common.h"

/*
 * SECTION:
 * Adds categories from a hardcoded list based on the the desktop menu
 * specification.
 */

gboolean
gs_plugin_add_categories (GsPlugin *plugin,
			  GPtrArray *list,
			  GCancellable *cancellable,
			  GError **error)
{
	GsCategory *category = NULL;
	const MenuSpecData *msdata;
	gchar *tmp;
	gchar msgctxt[100];
	guint i;

	msdata = menu_spec_get_data ();
	for (i = 0; msdata[i].path != NULL; i++) {
		tmp = g_strstr_len (msdata[i].path, -1, "::");
		if (tmp == NULL) {
			category = gs_category_new (msdata[i].path);
			gs_category_set_icon (category, msdata[i].icon);
			gs_category_set_name (category, gettext (msdata[i].text));
			g_ptr_array_add (list, category);
			g_snprintf (msgctxt, 100, "Menu subcategory of %s", msdata[i].text);
		} else {
			g_autoptr(GsCategory) sub = gs_category_new (tmp + 2);
			gs_category_set_name (sub, g_dpgettext2 (GETTEXT_PACKAGE,
								 msgctxt,
								 msdata[i].text));
			gs_category_add_child (category, sub);
		}
	}

	return TRUE;
}
