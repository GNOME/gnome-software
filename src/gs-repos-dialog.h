/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
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

#ifndef GS_REPOS_DIALOG_H
#define GS_REPOS_DIALOG_H

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_REPOS_DIALOG (gs_repos_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsReposDialog, gs_repos_dialog, GS, REPOS_DIALOG, GtkDialog)

GtkWidget	*gs_repos_dialog_new		(GtkWindow	*parent,
						 GsPluginLoader	*plugin_loader);

G_END_DECLS

#endif /* GS_REPOS_DIALOG_H */

/* vim: set noexpandtab: */
