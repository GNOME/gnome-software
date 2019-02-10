/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef __GS_FOLDERS_H
#define __GS_FOLDERS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_FOLDERS (gs_folders_get_type ())

G_DECLARE_FINAL_TYPE (GsFolders, gs_folders, GS, FOLDERS, GObject)

GsFolders	 *gs_folders_get		(void);

gchar		**gs_folders_get_folders	(GsFolders	*folders);
gchar		**gs_folders_get_nonempty_folders (GsFolders	*folders);
const gchar	 *gs_folders_add_folder		(GsFolders  	*folders,
						 const gchar	*id);
void		  gs_folders_remove_folder      (GsFolders      *folders,
						 const gchar    *id);
const gchar	 *gs_folders_get_folder_name	(GsFolders	*folders,
						 const gchar    *id);
void		  gs_folders_set_folder_name 	(GsFolders 	*folders,
						 const gchar	*id,
						 const gchar	*name);
const gchar	 *gs_folders_get_app_folder	(GsFolders	*folders,
						 const gchar	*app,
						 GPtrArray      *categories);
void		  gs_folders_set_app_folder	(GsFolders	*folders,
						 const gchar	*app,
						 GPtrArray      *categories,
						 const gchar	*id);
void		  gs_folders_save		(GsFolders 	*folders);
void		  gs_folders_revert		(GsFolders	*folders);

void              gs_folders_convert            (void);

G_END_DECLS

#endif /* __GS_FOLDERS_H */
