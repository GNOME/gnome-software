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

#ifndef __GS_SHELL_LOADING_H
#define __GS_SHELL_LOADING_H

#include <glib-object.h>
#include <gtk/gtk.h>

#include "gs-page.h"
#include "gs-plugin-loader.h"

G_BEGIN_DECLS

#define GS_TYPE_SHELL_LOADING (gs_shell_loading_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsShellLoading, gs_shell_loading, GS, SHELL_LOADING, GsPage)

struct _GsShellLoadingClass
{
	GsPageClass		 parent_class;

	void	(*refreshed)	(GsShellLoading	*self);
};

GsShellLoading	*gs_shell_loading_new		(void);
void		 gs_shell_loading_setup		(GsShellLoading		*self,
						 GsShell		*shell,
						 GsPluginLoader		*plugin_loader,
						 GtkBuilder		*builder,
						 GCancellable		*cancellable);

G_END_DECLS

#endif /* __GS_SHELL_LOADING_H */

/* vim: set noexpandtab: */
