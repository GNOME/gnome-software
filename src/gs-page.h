/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2016 Kalev Lember <klember@redhat.com>
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

#ifndef GS_PAGE_H
#define GS_PAGE_H

#include "gs-shell.h"

G_BEGIN_DECLS

#define GS_TYPE_PAGE (gs_page_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsPage, gs_page, GS, PAGE, GtkBin)

struct _GsPageClass
{
	GtkBinClass	 parent_class;

	void		(*app_installed)	(GsPage		 *page,
						 GsApp		 *app);
	void		(*app_removed)		(GsPage		 *page,
						 GsApp		 *app);
	void		(*switch_to)		(GsPage		 *page,
						 gboolean	  scroll_up);
	void		(*switch_from)		(GsPage		 *page);
	void		(*reload)		(GsPage		 *page);
	gboolean	(*setup)		(GsPage		 *page,
						 GsShell	*shell,
						 GsPluginLoader	*plugin_loader,
						 GtkBuilder	*builder,
						 GCancellable	*cancellable,
						 GError		**error);
};

typedef void (*GsPageAuthCallback) (GsPage *page, gboolean authorized, gpointer user_data);

GsPage		*gs_page_new				(void);
GsShell		*gs_page_get_shell			(GsPage		*page);
GtkWidget	*gs_page_get_header_start_widget	(GsPage		*page);
void		 gs_page_set_header_start_widget	(GsPage		*page,
							 GtkWidget	*widget);
GtkWidget	*gs_page_get_header_end_widget		(GsPage		*page);
void		 gs_page_set_header_end_widget		(GsPage		*page,
							 GtkWidget	*widget);
void		 gs_page_authenticate			(GsPage			*page,
							 GsApp			*app,
							 const gchar		*provider_id,
							 GCancellable		*cancellable,
							 GsPageAuthCallback	 callback,
							 gpointer		 user_data);
void		 gs_page_install_app			(GsPage			*page,
							 GsApp			*app,
							 GsShellInteraction	interaction,
							 GCancellable		*cancellable);
void		 gs_page_remove_app			(GsPage		*page,
							 GsApp		*app,
							 GCancellable	*cancellable);
void		 gs_page_update_app			(GsPage		*page,
							 GsApp		*app,
							 GCancellable	*cancellable);
void		 gs_page_launch_app			(GsPage		*page,
							 GsApp		*app,
							 GCancellable	*cancellable);
void		 gs_page_shortcut_add			(GsPage		*page,
							 GsApp		*app,
							 GCancellable	*cancellable);
void		 gs_page_shortcut_remove		(GsPage		*page,
							 GsApp		*app,
							 GCancellable	*cancellable);
void		 gs_page_switch_to			(GsPage		*page,
							 gboolean	 scroll_up);
void		 gs_page_switch_from			(GsPage		*page);
void		 gs_page_reload				(GsPage		*page);
gboolean	 gs_page_setup				(GsPage		*page,
							 GsShell	*shell,
							 GsPluginLoader	*plugin_loader,
							 GtkBuilder	*builder,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_page_is_active			(GsPage		*page);

G_END_DECLS

#endif /* GS_PAGE_H */

/* vim: set noexpandtab: */
