/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_MODULESET_H
#define __GS_MODULESET_H

#include <glib-object.h>

typedef struct _GsModuleset		GsModuleset;
typedef struct _GsModulesetClass	GsModulesetClass;

#define GS_TYPE_MODULESET	(gs_moduleset_get_type ())
#define GS_MODULESET(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_MODULESET, GsModuleset))
#define GS_IS_MODULESET(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_MODULESET))

struct _GsModuleset {
	GObject		 parent_instance;
};

struct _GsModulesetClass {
	GObjectClass	 parent_class;
};

typedef enum {
	GS_MODULESET_MODULE_KIND_UNKNOWN,
	GS_MODULESET_MODULE_KIND_APPLICATION,
	GS_MODULESET_MODULE_KIND_PACKAGE,
	GS_MODULESET_MODULE_KIND_LAST
} GsModulesetModuleKind;

GType		 gs_moduleset_get_type			(void);
GsModuleset	*gs_moduleset_new			(void);

gchar		**gs_moduleset_get_modules		(GsModuleset		*moduleset,
							 GsModulesetModuleKind	 module_kind,
							 const gchar		*name);
gboolean	 gs_moduleset_parse_filename		(GsModuleset		*moduleset,
							 const gchar		*filename,
							 GError			**error);
gboolean	 gs_moduleset_parse_path		(GsModuleset		*moduleset,
							 const gchar		*path,
							 GError			**error);

G_END_DECLS

#endif /* __GS_MODULESET_H */

