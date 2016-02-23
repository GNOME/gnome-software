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

#ifndef __GS_SHELL_EXTRAS_H
#define __GS_SHELL_EXTRAS_H

#include <glib-object.h>
#include <gtk/gtk.h>

#include "gs-page.h"
#include "gs-plugin-loader.h"
#include "gs-shell.h"

G_BEGIN_DECLS

#define GS_TYPE_SHELL_EXTRAS (gs_shell_extras_get_type ())

G_DECLARE_FINAL_TYPE (GsShellExtras, gs_shell_extras, GS, SHELL_EXTRAS, GsPage)

typedef enum {
	GS_SHELL_EXTRAS_MODE_UNKNOWN,
	GS_SHELL_EXTRAS_MODE_INSTALL_PACKAGE_FILES,
	GS_SHELL_EXTRAS_MODE_INSTALL_PROVIDE_FILES,
	GS_SHELL_EXTRAS_MODE_INSTALL_PACKAGE_NAMES,
	GS_SHELL_EXTRAS_MODE_INSTALL_MIME_TYPES,
	GS_SHELL_EXTRAS_MODE_INSTALL_FONTCONFIG_RESOURCES,
	GS_SHELL_EXTRAS_MODE_INSTALL_GSTREAMER_RESOURCES,
	GS_SHELL_EXTRAS_MODE_INSTALL_PLASMA_RESOURCES,
	GS_SHELL_EXTRAS_MODE_INSTALL_PRINTER_DRIVERS,
	GS_SHELL_EXTRAS_MODE_LAST
} GsShellExtrasMode;

const gchar		*gs_shell_extras_mode_to_string		(GsShellExtrasMode	  mode);
GsShellExtras		*gs_shell_extras_new			(void);
void			 gs_shell_extras_search			(GsShellExtras		 *self,
								 const gchar 		 *mode,
								 gchar			**resources);
void			 gs_shell_extras_reload			(GsShellExtras		 *self);
void			 gs_shell_extras_setup			(GsShellExtras		 *self,
								 GsShell		 *shell,
								 GsPluginLoader		 *plugin_loader,
								 GtkBuilder		 *builder,
								 GCancellable		 *cancellable);

G_END_DECLS

#endif /* __GS_SHELL_EXTRAS_H */

/* vim: set noexpandtab: */
