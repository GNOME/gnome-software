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

#ifndef GS_AUTH_DIALOG_H
#define GS_AUTH_DIALOG_H

#include <gtk/gtk.h>

#include "gs-app.h"
#include "gs-plugin-loader.h"

G_BEGIN_DECLS

#define GS_TYPE_AUTH_DIALOG (gs_auth_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsAuthDialog, gs_auth_dialog, GS, AUTH_DIALOG, GtkDialog)

GtkWidget	*gs_auth_dialog_new		(GsPluginLoader	*plugin_loader,
						 GsApp		*app,
						 const gchar	*provider_id,
						 GError		**error);

G_END_DECLS

#endif /* GS_AUTH_DIALOG_H */

/* vim: set noexpandtab: */
