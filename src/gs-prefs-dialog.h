/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef GS_PREFS_DIALOG_H
#define GS_PREFS_DIALOG_H

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_PREFS_DIALOG (gs_prefs_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsPrefsDialog, gs_prefs_dialog, GS, PREFS_DIALOG, GtkDialog)

GtkWidget	*gs_prefs_dialog_new		(GtkWindow	*parent,
						 GsPluginLoader	*plugin_loader);

G_END_DECLS

#endif /* GS_PREFS_DIALOG_H */

/* vim: set noexpandtab: */
