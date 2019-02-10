/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef GS_REMOVAL_DIALOG_H
#define GS_REMOVAL_DIALOG_H

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_REMOVAL_DIALOG (gs_removal_dialog_get_type ())

G_DECLARE_FINAL_TYPE (GsRemovalDialog, gs_removal_dialog, GS, REMOVAL_DIALOG, GtkMessageDialog)

GtkWidget	*gs_removal_dialog_new				(void);
void		 gs_removal_dialog_show_upgrade_removals	(GsRemovalDialog	 *self,
								 GsApp			 *upgrade);

G_END_DECLS

#endif /* GS_REMOVAL_DIALOG_H */

/* vim: set noexpandtab: */
