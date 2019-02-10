/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_APP_FOLDER_DIALOG (gs_app_folder_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsAppFolderDialog, gs_app_folder_dialog, GS, APP_FOLDER_DIALOG, GtkDialog)

GtkWidget	*gs_app_folder_dialog_new	(GtkWindow	*parent,
						 GList		*apps);

G_END_DECLS
