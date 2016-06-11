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

#define GS_TYPE_SHELL_DETAILS (gs_shell_details_get_type ())

G_DECLARE_FINAL_TYPE (GsShellDetails, gs_shell_details, GS, SHELL_DETAILS, GsPage)

GsShellDetails	*gs_shell_details_new		(void);
void		 gs_shell_details_invalidate	(GsShellDetails		*self);
void		 gs_shell_details_set_app	(GsShellDetails		*self,
						 GsApp			*app);
void		 gs_shell_details_set_filename	(GsShellDetails		*self,
						 const gchar		*filename);
GsApp		*gs_shell_details_get_app       (GsShellDetails		*self);
void		 gs_shell_details_setup		(GsShellDetails		*self,
						 GsShell		*shell,
						 GsPluginLoader		*plugin_loader,
						 GtkBuilder		*builder,
						 GCancellable		*cancellable);

G_END_DECLS

#endif /* __GS_SHELL_DETAILS_H */

/* vim: set noexpandtab: */
