/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_APP_UPDATE_DETAILS_DIALOG (gs_app_update_details_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsAppUpdateDetailsDialog, gs_app_update_details_dialog, GS, APP_UPDATE_DETAILS_DIALOG, AdwDialog)

GtkWidget	*gs_app_update_details_dialog_new	(GsPluginLoader		*plugin_loader,
							 GsApp			*app);

G_END_DECLS
