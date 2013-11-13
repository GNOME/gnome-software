/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#ifndef __GS_FOLDERS_H
#define __GS_FOLDERS_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_FOLDERS		(gs_folders_get_type ())
#define GS_FOLDERS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_FOLDERS, GsFolders))
#define GS_FOLDERS_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_FOLDERS, GsFoldersClass))
#define GS_IS_FOLDERS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_FOLDERS))
#define GS_IS_FOLDERS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_FOLDERS))
#define GS_FOLDERS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_FOLDERS, GsFoldersClass))

typedef struct GsFoldersPrivate GsFoldersPrivate;

typedef struct
{
	 GObject		 parent;
	 GsFoldersPrivate	*priv;
} GsFolders;

typedef struct
{
	GObjectClass		 parent_class;
} GsFoldersClass;

GType		  gs_folders_get_type		(void);

GsFolders	 *gs_folders_get		(void);

gchar		**gs_folders_get_folders	(GsFolders	*folders);
const gchar	**gs_folders_get_apps		(GsFolders	*folders,
						 const gchar    *id);
void		  gs_folders_add_folder		(GsFolders  	*folders,
						 const gchar	*id);
void		  gs_folders_remove_folder      (GsFolders      *folders,
						 const gchar    *id);
const gchar	 *gs_folders_get_folder_name	(GsFolders	*folders,
						 const gchar    *id);
void		  gs_folders_set_folder_name 	(GsFolders 	*folders,
						 const gchar	*id,
						 const gchar	*name);
const gchar	 *gs_folders_get_app_folder	(GsFolders	*folders,
						 const gchar	*app);
void		  gs_folders_set_app_folder	(GsFolders	*folders,
						 const gchar	*app,
						 const gchar	*id);
void		  gs_folders_save		(GsFolders 	*folders);
void		  gs_folders_revert		(GsFolders	*folders);

G_END_DECLS

#endif /* __GS_FOLDERS_H */

/* vim: set noexpandtab: */
