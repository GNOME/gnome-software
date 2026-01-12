/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2026 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <appstream.h>
#include <glib-object.h>
#include <xmlb.h>

G_BEGIN_DECLS

#define GS_TYPE_SILO_WRAPPER (gs_silo_wrapper_get_type ())

G_DECLARE_FINAL_TYPE (GsSiloWrapper, gs_silo_wrapper, GS, SILO_WRAPPER, GObject)

/**
 * GsSiloWrapperBuildFunc:
 * @silo_wrapper: a #GsSiloWrapper which requested a build
 * @interactive: whether it's called within a user initiated job
 * @user_data: user data passed to gs_silo_wrapper_new()
 * @cancellable: a #GCancellable, or %NULL
 * @error: a location to store a #GError to, or %NULL
 *
 * Called when the @wrapper requires to build the #XbSilo.
 *
 * Returns: (transfer full): a new #XbSilo
 *
 * Since: 50
 **/
typedef XbSilo * (*GsSiloWrapperBuildFunc)	(GsSiloWrapper *silo_wrapper,
						 gboolean interactive,
						 gpointer user_data,
						 GCancellable *cancellable,
						 GError **error);

GsSiloWrapper *	gs_silo_wrapper_new		(GsSiloWrapperBuildFunc build_func,
						 gpointer user_data,
						 GDestroyNotify free_user_data);
void		gs_silo_wrapper_add_file_monitor(GsSiloWrapper *self,
						 GFileMonitor *file_monitor);
gboolean	gs_silo_wrapper_acquire		(GsSiloWrapper *self,
						 gboolean interactive,
						 GCancellable *cancellable,
						 GError **error);
void		gs_silo_wrapper_release		(GsSiloWrapper *self);
void		gs_silo_wrapper_invalidate	(GsSiloWrapper *self);
XbSilo *	gs_silo_wrapper_get_silo	(GsSiloWrapper *self);
AsComponentScope
		gs_silo_wrapper_get_scope	(GsSiloWrapper *self);
const gchar *	gs_silo_wrapper_get_filename	(GsSiloWrapper *self);
GHashTable *	gs_silo_wrapper_get_installed_by_desktopid
						(GsSiloWrapper *self);

/**
 * GsSiloHandle:
 *
 * Handle for an acquired [class@Gs.SiloWrapper]. It can be used
 * with `g_autoptr()` to automatically release the wrapper.
 *
 * It is a `typedef` of [class@Gs.SiloWrapper], so can safely be passed to
 * silo wrapper method calls.
 *
 * See [method@Gs.SiloWrapper.acquire] for details.
 *
 * Since: 50
 */
typedef GsSiloWrapper GsSiloHandle;

static inline void
gs_silo_handle_release (GsSiloHandle *handle)
{
	if (handle != NULL)
		gs_silo_wrapper_release (handle);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsSiloHandle, gs_silo_handle_release)

G_END_DECLS
