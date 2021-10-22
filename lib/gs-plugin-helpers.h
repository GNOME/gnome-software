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

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

#include <gnome-software.h>

G_BEGIN_DECLS

typedef struct {
	GsAppList *list;  /* (owned) (not nullable) */
	GsPluginRefineFlags flags;
} GsPluginRefineData;

GsPluginRefineData *gs_plugin_refine_data_new (GsAppList           *list,
                                               GsPluginRefineFlags  flags);
GTask *gs_plugin_refine_data_new_task (gpointer             source_object,
                                       GsAppList           *list,
                                       GsPluginRefineFlags  flags,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data);
void gs_plugin_refine_data_free (GsPluginRefineData *data);
G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsPluginRefineData, gs_plugin_refine_data_free)

G_END_DECLS
