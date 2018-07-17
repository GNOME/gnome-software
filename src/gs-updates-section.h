/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Kalev Lember <klember@redhat.com>
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

#ifndef __GS_UPDATES_SECTION_H
#define __GS_UPDATES_SECTION_H

#include <gtk/gtk.h>

#include "gs-app-list.h"
#include "gs-plugin-loader.h"
#include "gs-page.h"

G_BEGIN_DECLS

#define GS_TYPE_UPDATES_SECTION (gs_updates_section_get_type ())

G_DECLARE_FINAL_TYPE (GsUpdatesSection, gs_updates_section, GS, UPDATES_SECTION, GtkListBox)

typedef enum {
	GS_UPDATES_SECTION_KIND_OFFLINE_FIRMWARE,
	GS_UPDATES_SECTION_KIND_OFFLINE,
	GS_UPDATES_SECTION_KIND_ONLINE,
	GS_UPDATES_SECTION_KIND_ONLINE_FIRMWARE,
	GS_UPDATES_SECTION_KIND_LAST
} GsUpdatesSectionKind;

GtkListBox	*gs_updates_section_new			(GsUpdatesSectionKind	 kind,
							 GsPluginLoader		*plugin_loader,
							 GsPage			*page);
GsAppList	*gs_updates_section_get_list		(GsUpdatesSection	*self);
void		 gs_updates_section_add_app		(GsUpdatesSection	*self,
							 GsApp			*app);
void		 gs_updates_section_remove_all		(GsUpdatesSection	*self);
void		 gs_updates_section_set_size_groups	(GsUpdatesSection	*self,
							 GtkSizeGroup		*image,
							 GtkSizeGroup		*name,
							 GtkSizeGroup		*desc,
							 GtkSizeGroup		*button,
							 GtkSizeGroup		*header);

G_END_DECLS

#endif /* __GS_UPDATES_SECTION_H */

/* vim: set noexpandtab: */
