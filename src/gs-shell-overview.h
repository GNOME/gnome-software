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

#ifndef __GS_SHELL_OVERVIEW_H
#define __GS_SHELL_OVERVIEW_H

#include <glib-object.h>
#include <gtk/gtk.h>

#include "gs-app.h"
#include "gs-page.h"
#include "gs-shell.h"
#include "gs-plugin-loader.h"

G_BEGIN_DECLS

#define GS_TYPE_SHELL_OVERVIEW (gs_shell_overview_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsShellOverview, gs_shell_overview, GS, SHELL_OVERVIEW, GsPage)

struct _GsShellOverviewClass
{
	GsPageClass		 parent_class;

	void	(*refreshed)	(GsShellOverview *shell);
};

GsShellOverview	*gs_shell_overview_new		(void);
void		 gs_shell_overview_invalidate	(GsShellOverview	*shell_overview);
void		 gs_shell_overview_switch_to	(GsShellOverview	*shell_overview,
						 gboolean		scroll_up);
void		 gs_shell_overview_reload	(GsShellOverview	*shell_overview);
void		 gs_shell_overview_setup	(GsShellOverview	*shell_overview,
						 GsShell		*shell,
						 GsPluginLoader		*plugin_loader,
						 GtkBuilder		*builder,
						 GCancellable		*cancellable);
void		 gs_shell_overview_set_category	(GsShellOverview	*shell_overview,
						 const gchar		*category);

G_END_DECLS

#endif /* __GS_SHELL_OVERVIEW_H */

/* vim: set noexpandtab: */
