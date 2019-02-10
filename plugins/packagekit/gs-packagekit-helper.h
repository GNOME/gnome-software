/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib-object.h>
#include <gnome-software.h>
#include <packagekit-glib2/packagekit.h>

G_BEGIN_DECLS

#define GS_TYPE_PACKAGEKIT_HELPER (gs_packagekit_helper_get_type ())

G_DECLARE_FINAL_TYPE (GsPackagekitHelper, gs_packagekit_helper, GS, PACKAGEKIT_HELPER, GObject)

GsPackagekitHelper *gs_packagekit_helper_new		(GsPlugin		*plugin);
GsPlugin	*gs_packagekit_helper_get_plugin	(GsPackagekitHelper	*self);
void		 gs_packagekit_helper_add_app		(GsPackagekitHelper	*self,
							 GsApp			*app);
GsApp		*gs_packagekit_helper_get_app_by_id	(GsPackagekitHelper	*progress,
							 const gchar		*package_id);
void		 gs_packagekit_helper_cb		(PkProgress		*progress,
							 PkProgressType		 type,
							 gpointer		 user_data);


G_END_DECLS
