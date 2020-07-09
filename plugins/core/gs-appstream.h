/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gnome-software.h>
#include <xmlb.h>

G_BEGIN_DECLS

GsApp		*gs_appstream_create_app		(GsPlugin	*plugin,
							 XbSilo		*silo,
							 XbNode		*component,
							 GError		**error);
gboolean	 gs_appstream_refine_app		(GsPlugin	*plugin,
							 GsApp		*app,
							 XbSilo		*silo,
							 XbNode		*component,
							 GsPluginRefineFlags flags,
							 GError		**error);
gboolean	 gs_appstream_search			(GsPlugin	*plugin,
							 XbSilo		*silo,
							 const gchar * const *values,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_appstream_add_categories		(GsPlugin	*plugin,
							 XbSilo		*silo,
							 GPtrArray	*list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_appstream_add_category_apps		(GsPlugin	*plugin,
							 XbSilo		*silo,
							 GsCategory	*category,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_appstream_add_popular		(GsPlugin	*plugin,
							 XbSilo		*silo,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_appstream_add_featured		(GsPlugin	*plugin,
							 XbSilo		*silo,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_appstream_add_alternates		(GsPlugin	*plugin,
							 XbSilo		*silo,
							 GsApp		*app,
							 GsAppList	*list,
							 GCancellable	*cancellable,
							 GError		**error);
gboolean	 gs_appstream_add_recent		(GsPlugin	*plugin,
							 XbSilo		*silo,
							 GsAppList	*list,
							 guint64	 age,
							 GCancellable	*cancellable,
							 GError		**error);
void		 gs_appstream_component_add_extra_info	(GsPlugin	*plugin,
							 XbBuilderNode	*component);
void		 gs_appstream_component_add_keyword	(XbBuilderNode	*component,
							 const gchar	*str);
void		 gs_appstream_component_add_category	(XbBuilderNode	*component,
							 const gchar	*str);
void		 gs_appstream_component_add_icon	(XbBuilderNode	*component,
							 const gchar	*str);
void		 gs_appstream_component_add_provide	(XbBuilderNode	*component,
							 const gchar	*str);

G_END_DECLS
