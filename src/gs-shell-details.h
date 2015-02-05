/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_SHELL_DETAILS_H
#define __GS_SHELL_DETAILS_H

#include <glib-object.h>
#include <gtk/gtk.h>

#include "gs-app.h"
#include "gs-shell.h"
#include "gs-page.h"
#include "gs-plugin-loader.h"

G_BEGIN_DECLS

#define GS_TYPE_SHELL_DETAILS		(gs_shell_details_get_type ())
#define GS_SHELL_DETAILS(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_SHELL_DETAILS, GsShellDetails))
#define GS_SHELL_DETAILS_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_SHELL_DETAILS, GsShellDetailsClass))
#define GS_IS_SHELL_DETAILS(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_SHELL_DETAILS))
#define GS_IS_SHELL_DETAILS_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_SHELL_DETAILS))
#define GS_SHELL_DETAILS_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_SHELL_DETAILS, GsShellDetailsClass))

typedef struct GsShellDetailsPrivate GsShellDetailsPrivate;

typedef struct
{
	 GsPage				 parent;
	 GsShellDetailsPrivate		*priv;
} GsShellDetails;

typedef struct
{
	GsPageClass			 parent_class;
} GsShellDetailsClass;

GType		 gs_shell_details_get_type	(void);

GsShellDetails	*gs_shell_details_new		(void);
void		 gs_shell_details_invalidate	(GsShellDetails		*shell_details);
void		 gs_shell_details_set_app	(GsShellDetails		*shell_details,
						 GsApp			*app);
void		 gs_shell_details_set_filename	(GsShellDetails		*shell_details,
						 const gchar		*filename);
GsApp		*gs_shell_details_get_app       (GsShellDetails		*shell_details);
void		 gs_shell_details_switch_to	(GsShellDetails		*shell_details);
void		 gs_shell_details_reload	(GsShellDetails		*shell_details);
void		 gs_shell_details_setup		(GsShellDetails		*shell_details,
						 GsShell		*shell,
						 GsPluginLoader		*plugin_loader,
						 GtkBuilder		*builder,
						 GCancellable		*cancellable);

#endif /* __GS_SHELL_DETAILS_H */

/* vim: set noexpandtab: */
