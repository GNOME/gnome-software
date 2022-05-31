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
#include "gs-category.h"
#include "gs-plugin-types.h"

G_BEGIN_DECLS

/**
 * GsAppQueryTristate:
 * @GS_APP_QUERY_TRISTATE_UNSET: Value is unset.
 * @GS_APP_QUERY_TRISTATE_FALSE: False. Equal in value to %FALSE.
 * @GS_APP_QUERY_TRISTATE_TRUE: True. Equal in value to %TRUE.
 *
 * A type for storing a boolean value which can also have an ‘unknown’ or
 * ‘unset’ state.
 *
 * Within #GsAppQuery this is used for boolean query properties which are unset
 * by default so that they don’t affect the query.
 *
 * Since: 43
 */
typedef enum
{
	GS_APP_QUERY_TRISTATE_UNSET = -1,
	GS_APP_QUERY_TRISTATE_FALSE = 0,
	GS_APP_QUERY_TRISTATE_TRUE = 1,
} GsAppQueryTristate;

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

guint			 gs_app_query_get_n_properties_set (GsAppQuery *self);

const gchar * const	*gs_app_query_get_provides_files (GsAppQuery *self);
GDateTime		*gs_app_query_get_released_since (GsAppQuery *self);
GsAppQueryTristate	 gs_app_query_get_is_curated	 (GsAppQuery *self);
GsAppQueryTristate	 gs_app_query_get_is_featured	 (GsAppQuery *self);
GsCategory		*gs_app_query_get_category	 (GsAppQuery *self);
GsAppQueryTristate	 gs_app_query_get_is_installed	 (GsAppQuery *self);
const gchar * const	*gs_app_query_get_deployment_featured
							 (GsAppQuery *self);
const gchar * const	*gs_app_query_get_developers	 (GsAppQuery *self);

G_END_DECLS
