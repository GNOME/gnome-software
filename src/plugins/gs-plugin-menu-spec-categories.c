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

#include <gs-plugin.h>
#include <glib/gi18n.h>

#include "menu-spec-common.h"

/*
 * SECTION:
 * Adds categories from a hardcoded list based on the the desktop menu
 * specification.
 */

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "menu-spec-categories";
}

/**
 * gs_plugin_add_categories:
 */
gboolean
gs_plugin_add_categories (GsPlugin *plugin,
			  GList **list,
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
			category = gs_category_new (NULL,
						    msdata[i].path,
						    gettext(msdata[i].text));
			*list = g_list_prepend (*list, category);
			g_snprintf(msgctxt, 100, "Menu subcategory of %s", msdata[i].text);
		} else {
			g_autoptr(GsCategory) sub = NULL;
			sub = gs_category_new (category,
					       tmp + 2,
					       g_dpgettext2(GETTEXT_PACKAGE, msgctxt, msdata[i].text));
			gs_category_add_subcategory (category, sub);
		}
	}

	return TRUE;
}
