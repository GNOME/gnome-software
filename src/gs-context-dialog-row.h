/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

/**
 * GsContextDialogRowImportance:
 * @GS_CONTEXT_DIALOG_ROW_IMPORTANCE_NEUTRAL: neutral or unknown importance
 * @GS_CONTEXT_DIALOG_ROW_IMPORTANCE_UNIMPORTANT: unimportant
 * @GS_CONTEXT_DIALOG_ROW_IMPORTANCE_WARNING: a bit important
 * @GS_CONTEXT_DIALOG_ROW_IMPORTANCE_IMPORTANT: definitely important
 *
 * The importance of the information in a #GsContextDialogRow. The values
 * increase from less important to more important.
 *
 * Since: 41
 */
typedef enum
{
	/* The code in this file relies on the fact that these enum values
	 * numerically increase as they get more important. */
	GS_CONTEXT_DIALOG_ROW_IMPORTANCE_NEUTRAL,
	GS_CONTEXT_DIALOG_ROW_IMPORTANCE_UNIMPORTANT,
	GS_CONTEXT_DIALOG_ROW_IMPORTANCE_WARNING,
	GS_CONTEXT_DIALOG_ROW_IMPORTANCE_IMPORTANT,
} GsContextDialogRowImportance;

#define GS_TYPE_CONTEXT_DIALOG_ROW (gs_context_dialog_row_get_type ())

G_DECLARE_FINAL_TYPE (GsContextDialogRow, gs_context_dialog_row, GS, CONTEXT_DIALOG_ROW, GtkListBoxRow)

GtkListBoxRow	*gs_context_dialog_row_new	(const gchar			*icon_name,
						 GsContextDialogRowImportance	 importance,
						 const gchar			*title,
						 const gchar			*description);

const gchar			*gs_context_dialog_row_get_icon_name	(GsContextDialogRow	*self);
GsContextDialogRowImportance	 gs_context_dialog_row_get_importance	(GsContextDialogRow	*self);
const gchar			*gs_context_dialog_row_get_title	(GsContextDialogRow	*self);
const gchar			*gs_context_dialog_row_get_description	(GsContextDialogRow	*self);

G_END_DECLS
