/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_AUTH_DIALOG (gs_auth_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsAuthDialog, gs_auth_dialog, GS, AUTH_DIALOG, GtkDialog)

GtkWidget	*gs_auth_dialog_new	(GsPluginLoader	*plugin_loader,
					 GsApp		*app,
					 const gchar	*auth_id,
					 GError		**error);

G_END_DECLS
