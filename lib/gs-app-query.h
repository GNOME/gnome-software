/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2022 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include "gs-app-list.h"
#include "gs-plugin-types.h"

G_BEGIN_DECLS

#define GS_TYPE_APP_QUERY (gs_app_query_get_type ())

G_DECLARE_FINAL_TYPE (GsAppQuery, gs_app_query, GS, APP_QUERY, GObject)

GsAppQuery	*gs_app_query_new	(const gchar *first_property_name,
					 ...) G_GNUC_NULL_TERMINATED;

GsPluginRefineFlags	 gs_app_query_get_refine_flags	(GsAppQuery *self);
guint			 gs_app_query_get_max_results	(GsAppQuery *self);
GsAppListFilterFlags	 gs_app_query_get_dedupe_flags	(GsAppQuery *self);
GsAppListSortFunc	 gs_app_query_get_sort_func	(GsAppQuery *self,
							 gpointer   *user_data_out);
GsAppListFilterFunc	 gs_app_query_get_filter_func	(GsAppQuery *self,
							 gpointer   *user_data_out);

const gchar * const	*gs_app_query_get_provides_files (GsAppQuery *self);
GDateTime		*gs_app_query_get_released_since (GsAppQuery *self);

G_END_DECLS
