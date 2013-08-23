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

#ifndef __GS_SHELL_UPDATES_H
#define __GS_SHELL_UPDATES_H

#include <glib-object.h>
#include <gtk/gtk.h>

#include "gs-plugin-loader.h"

G_BEGIN_DECLS

#define GS_TYPE_SHELL_UPDATES		(gs_shell_updates_get_type ())
#define GS_SHELL_UPDATES(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GS_TYPE_SHELL_UPDATES, GsShellUpdates))
#define GS_SHELL_UPDATES_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GS_TYPE_SHELL_UPDATES, GsShellUpdatesClass))
#define GS_IS_SHELL_UPDATES(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), GS_TYPE_SHELL_UPDATES))
#define GS_IS_SHELL_UPDATES_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GS_TYPE_SHELL_UPDATES))
#define GS_SHELL_UPDATES_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GS_TYPE_SHELL_UPDATES, GsShellUpdatesClass))

typedef struct GsShellUpdatesPrivate GsShellUpdatesPrivate;

typedef struct
{
	 GObject		 parent;
	 GsShellUpdatesPrivate	*priv;
} GsShellUpdates;

typedef struct
{
	GObjectClass		 parent_class;
} GsShellUpdatesClass;

GType		 gs_shell_updates_get_type	(void);

GsShellUpdates	*gs_shell_updates_new		(void);
void		 gs_shell_updates_invalidate	(GsShellUpdates		*shell_updates);
void		 gs_shell_updates_refresh	(GsShellUpdates		*shell_updates);
void 		 gs_shell_updates_setup		(GsShellUpdates		*shell_updates,
						 GsPluginLoader		*plugin_loader,
						 GtkBuilder		*builder,
						 GCancellable		*cancellable);

#endif /* __GS_SHELL_UPDATES_H */
