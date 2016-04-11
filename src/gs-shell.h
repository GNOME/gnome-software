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

#ifndef __GS_SHELL_H
#define __GS_SHELL_H

#include <glib-object.h>
#include <gtk/gtk.h>

#include "gs-plugin-loader.h"
#include "gs-category.h"
#include "gs-app.h"

G_BEGIN_DECLS

#define GS_TYPE_SHELL (gs_shell_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsShell, gs_shell, GS, SHELL, GObject)

struct _GsShellClass
{
	GObjectClass			 parent_class;

	void (* loaded)		 (GsShell *shell);
};

typedef enum {
	GS_SHELL_MODE_UNKNOWN,
	GS_SHELL_MODE_OVERVIEW,
	GS_SHELL_MODE_INSTALLED,
	GS_SHELL_MODE_SEARCH,
	GS_SHELL_MODE_UPDATES,
	GS_SHELL_MODE_DETAILS,
	GS_SHELL_MODE_CATEGORY,
	GS_SHELL_MODE_EXTRAS,
	GS_SHELL_MODE_MODERATE,
	GS_SHELL_MODE_LOADING,
	GS_SHELL_MODE_LAST
} GsShellMode;

GsShell		*gs_shell_new			(void);
void		 gs_shell_activate		(GsShell	*shell);
void		 gs_shell_refresh		(GsShell	*shell,
						 GCancellable	*cancellable);
void		 gs_shell_change_mode		(GsShell	*shell,
						 GsShellMode	 mode,
						 GsApp		*app,
						 gpointer	 data,
						 gboolean	 scroll_up);
void		 gs_shell_set_mode		(GsShell	*shell,
						 GsShellMode	 mode);
void		 gs_shell_modal_dialog_present	(GsShell	*shell,
						 GtkDialog	*dialog);
GsShellMode	 gs_shell_get_mode		(GsShell	*shell);
const gchar	*gs_shell_get_mode_string	(GsShell	*shell);
void		 gs_shell_show_installed_updates(GsShell	*shell);
void		 gs_shell_show_sources		(GsShell	*shell);
void		 gs_shell_show_app		(GsShell	*shell,
						 GsApp		*app);
void		 gs_shell_show_category		(GsShell	*shell,
						 GsCategory	*category);
void		 gs_shell_show_search		(GsShell	*shell,
						 const gchar	*search);
void		 gs_shell_show_filename		(GsShell	*shell,
						 const gchar	*filename);
void		 gs_shell_show_search_result	(GsShell	*shell,
						 const gchar	*id,
						 const gchar    *search);
void		 gs_shell_show_details		(GsShell	*shell,
						 const gchar	*id);
void		 gs_shell_show_extras_search	(GsShell	*shell,
						 const gchar	*mode,
						 gchar		**resources);
void		 gs_shell_setup			(GsShell	*shell,
						 GsPluginLoader	*plugin_loader,
						 GCancellable	*cancellable);
void		 gs_shell_invalidate		(GsShell	*shell);
gboolean	 gs_shell_is_active		(GsShell	*shell);
GtkWindow	*gs_shell_get_window		(GsShell	*shell);

G_END_DECLS

#endif /* __GS_SHELL_H */

/* vim: set noexpandtab: */
