/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <adwaita.h>
#include "gs-app.h"

G_BEGIN_DECLS

#define GS_TYPE_DKMS_DIALOG (gs_dkms_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsDkmsDialog, gs_dkms_dialog, GS, DKMS_DIALOG, AdwDialog)

void		gs_dkms_dialog_run	(GtkWidget *parent,
					 GsApp *app);

G_END_DECLS
