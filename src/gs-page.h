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

#ifndef GS_PAGE_H
#define GS_PAGE_H

#include <glib-object.h>
#include <gtk/gtk.h>

#include "gs-shell.h"
#include "gs-plugin-loader.h"

G_BEGIN_DECLS

#define GS_TYPE_PAGE		(gs_page_get_type ())
#define GS_PAGE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_PAGE, GsPage))
#define GS_PAGE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_PAGE, GsPageClass))
#define GS_IS_PAGE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_PAGE))
#define GS_IS_PAGE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_PAGE))
#define GS_PAGE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_PAGE, GsPageClass))

typedef struct _GsPage		GsPage;
typedef struct _GsPageClass	GsPageClass;
typedef struct _GsPagePrivate	GsPagePrivate;

struct _GsPageClass
{
	GtkBinClass	 parent_class;

	void		(*app_installed)	(GsPage		 *page,
						 GsApp		 *app);
	void		(*app_removed)		(GsPage		 *page,
						 GsApp		 *app);
};

struct _GsPage
{
	 GtkBin		 parent;
};

GType		 gs_page_get_type			(void);
GsPage		*gs_page_new				(void);
void		 gs_page_install_app			(GsPage		*page,
							 GsApp		*app);
void		 gs_page_remove_app			(GsPage		*page,
							 GsApp		*app);
void		 gs_page_setup				(GsPage		*page,
							 GsShell	*shell,
							 GsPluginLoader	*plugin_loader,
							 GCancellable	*cancellable);

G_END_DECLS

#endif /* GS_PAGE_H */

/* vim: set noexpandtab: */
