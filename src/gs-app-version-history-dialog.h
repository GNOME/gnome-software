/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Matthew Leeds <mwleeds@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

#include "gnome-software-private.h"
#include "gs-info-window.h"

G_BEGIN_DECLS

#define GS_TYPE_APP_VERSION_HISTORY_DIALOG (gs_app_version_history_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsAppVersionHistoryDialog, gs_app_version_history_dialog, GS, APP_VERSION_HISTORY_DIALOG, GsInfoWindow)

GtkWidget	*gs_app_version_history_dialog_new	(GtkWindow	*parent,
							 GsApp		*app);

G_END_DECLS
