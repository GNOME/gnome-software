/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Joaquim Rocha <jrocha@endlessm.com>
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gnome-software.h>
#include <flatpak.h>

G_BEGIN_DECLS

#define GS_TYPE_FLATPAK (gs_flatpak_get_type ())

G_DECLARE_FINAL_TYPE (GsFlatpak, gs_flatpak, GS, FLATPAK, GObject)

typedef enum {
	GS_FLATPAK_FLAG_NONE			= 0,
	GS_FLATPAK_FLAG_IS_TEMPORARY		= 1 << 0,
	/*< private >*/
	GS_FLATPAK_FLAG_LAST
} GsFlatpakFlags;

GsFlatpak	*gs_flatpak_new			(GsPlugin		*plugin,
						 FlatpakInstallation	*installation,
						 GsFlatpakFlags		 flags);
FlatpakInstallation *gs_flatpak_get_installation (GsFlatpak		*self);

GsApp	*gs_flatpak_ref_to_app (GsFlatpak *self, const gchar *ref, GCancellable *cancellable, GError **error);

AsAppScope	gs_flatpak_get_scope		(GsFlatpak		*self);
const gchar	*gs_flatpak_get_id		(GsFlatpak		*self);
gboolean	gs_flatpak_setup		(GsFlatpak		*self,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_add_installed	(GsFlatpak		*self,
						 GsAppList		*list,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_add_sources		(GsFlatpak		*self,
						 GsAppList		*list,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_add_updates		(GsFlatpak		*self,
						 GsAppList		*list,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_refresh		(GsFlatpak		*self,
						 guint			 cache_age,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_refine_app		(GsFlatpak		*self,
						 GsApp			*app,
						 GsPluginRefineFlags	flags,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_refine_app_state	(GsFlatpak		*self,
						 GsApp			*app,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_refine_wildcard	(GsFlatpak		*self,
						 GsApp			*app,
						 GsAppList		*list,
						 GsPluginRefineFlags	 flags,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_launch		(GsFlatpak		*self,
						 GsApp			*app,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_app_remove_source	(GsFlatpak		*self,
						 GsApp			*app,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_app_install_source	(GsFlatpak		*self,
						 GsApp			*app,
						 GCancellable		*cancellable,
						 GError			**error);
GsApp		*gs_flatpak_file_to_app_ref	(GsFlatpak		*self,
						 GFile			*file,
						 GCancellable		*cancellable,
						 GError			**error);
GsApp		*gs_flatpak_file_to_app_bundle	(GsFlatpak		*self,
						 GFile			*file,
						 GCancellable		*cancellable,
						 GError			**error);
GsApp		*gs_flatpak_find_source_by_url	(GsFlatpak		*self,
						 const gchar		*name,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_search		(GsFlatpak		*self,
						 gchar			**values,
						 GsAppList		*list,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_add_categories	(GsFlatpak		*self,
						 GPtrArray		*list,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_add_category_apps	(GsFlatpak		*self,
						 GsCategory		*category,
						 GsAppList		*list,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_add_popular		(GsFlatpak		*self,
						 GsAppList		*list,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_add_featured		(GsFlatpak		*self,
						 GsAppList		*list,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_add_alternates	(GsFlatpak		*self,
						 GsApp			*app,
						 GsAppList		*list,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_add_recent		(GsFlatpak		*self,
						 GsAppList		*list,
						 guint64		 age,
						 GCancellable		*cancellable,
						 GError			**error);
GsApp *		gs_flatpak_create_app_from_repo_dir (GsFlatpak		*self,
						     GFile		*file,
						     GCancellable	*cancellable,
						     GError		**error);
gboolean	gs_flatpak_app_get_copyable	(GsFlatpak		*self,
						 GsApp			*app,
						 gboolean		*copyable,
						 GCancellable		*cancellable,
						 GError			**error);
gboolean	gs_flatpak_app_copy		(GsFlatpak		*self,
						 GsApp			*app,
						 GFile			*copy_dest,
						 GCancellable		*cancellable,
						 GError			**error);

G_END_DECLS
