/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2012-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib-object.h>

#include "gs-app.h"

G_BEGIN_DECLS

#define GS_TYPE_APP_LIST (gs_app_list_get_type ())

G_DECLARE_FINAL_TYPE (GsAppList, gs_app_list, GS, APP_LIST, GObject)

typedef gboolean (*GsAppListSortFunc)		(GsApp		*app1,
						 GsApp		*app2,
						 gpointer	 user_data);
typedef gboolean (*GsAppListFilterFunc)		(GsApp		*app,
						 gpointer	 user_data);

GsAppList	*gs_app_list_new		(void);
void		 gs_app_list_add		(GsAppList	*list,
						 GsApp		*app);
void		 gs_app_list_add_list		(GsAppList	*list,
						 GsAppList	*donor);
void		 gs_app_list_remove		(GsAppList	*list,
						 GsApp		*app);
GsApp		*gs_app_list_index		(GsAppList	*list,
						 guint		 idx);
GsApp		*gs_app_list_lookup		(GsAppList	*list,
						 const gchar	*unique_id);
guint		 gs_app_list_length		(GsAppList	*list);
void		 gs_app_list_sort		(GsAppList	*list,
						 GsAppListSortFunc func,
						 gpointer	 user_data);
void		 gs_app_list_filter		(GsAppList	*list,
						 GsAppListFilterFunc func,
						 gpointer	 user_data);

G_END_DECLS
