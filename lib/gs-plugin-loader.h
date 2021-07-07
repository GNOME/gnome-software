/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2007-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2020 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib-object.h>

#include "gs-app.h"
#include "gs-category.h"
#include "gs-category-manager.h"
#include "gs-plugin-event.h"
#include "gs-plugin-private.h"
#include "gs-plugin-job.h"

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_LOADER		(gs_plugin_loader_get_type ())
G_DECLARE_FINAL_TYPE (GsPluginLoader, gs_plugin_loader, GS, PLUGIN_LOADER, GObject)

GsPluginLoader	*gs_plugin_loader_new			(void);
void		 gs_plugin_loader_job_process_async	(GsPluginLoader	*plugin_loader,
							 GsPluginJob	*plugin_job,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GsAppList	*gs_plugin_loader_job_process_finish	(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
gboolean	 gs_plugin_loader_job_action_finish	(GsPluginLoader	*plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
void		 gs_plugin_loader_job_get_categories_async (GsPluginLoader *plugin_loader,
							 GsPluginJob	*plugin_job,
							 GCancellable	*cancellable,
							 GAsyncReadyCallback callback,
							 gpointer	 user_data);
GPtrArray	*gs_plugin_loader_job_get_categories_finish (GsPluginLoader *plugin_loader,
							 GAsyncResult	*res,
							 GError		**error);
gboolean	 gs_plugin_loader_setup			(GsPluginLoader	*plugin_loader,
							 gchar		**allowlist,
							 gchar		**blocklist,
							 GCancellable	*cancellable,
							 GError		**error);
void		 gs_plugin_loader_dump_state		(GsPluginLoader	*plugin_loader);
gboolean	 gs_plugin_loader_get_enabled		(GsPluginLoader	*plugin_loader,
							 const gchar	*plugin_name);
void		 gs_plugin_loader_add_location		(GsPluginLoader	*plugin_loader,
							 const gchar	*location);
guint		 gs_plugin_loader_get_scale		(GsPluginLoader	*plugin_loader);
void		 gs_plugin_loader_set_scale		(GsPluginLoader	*plugin_loader,
							 guint		 scale);
GsAppList	*gs_plugin_loader_get_pending		(GsPluginLoader	*plugin_loader);
gboolean	 gs_plugin_loader_get_allow_updates	(GsPluginLoader	*plugin_loader);
gboolean	 gs_plugin_loader_get_network_available	(GsPluginLoader *plugin_loader);
gboolean	 gs_plugin_loader_get_network_metered	(GsPluginLoader *plugin_loader);
gboolean	 gs_plugin_loader_get_plugin_supported	(GsPluginLoader	*plugin_loader,
							 const gchar	*function_name);

void		 gs_plugin_loader_add_event		(GsPluginLoader *plugin_loader,
							 GsPluginEvent	*event);
GPtrArray	*gs_plugin_loader_get_events		(GsPluginLoader	*plugin_loader);
GsPluginEvent	*gs_plugin_loader_get_event_default	(GsPluginLoader	*plugin_loader);
void		 gs_plugin_loader_remove_events		(GsPluginLoader	*plugin_loader);

GsApp		*gs_plugin_loader_app_create		(GsPluginLoader	*plugin_loader,
							 const gchar	*unique_id);
GsApp		*gs_plugin_loader_get_system_app	(GsPluginLoader	*plugin_loader);

/* only useful from the self tests */
void		 gs_plugin_loader_setup_again		(GsPluginLoader	*plugin_loader);
void		 gs_plugin_loader_clear_caches		(GsPluginLoader	*plugin_loader);
GsPlugin	*gs_plugin_loader_find_plugin		(GsPluginLoader	*plugin_loader,
							 const gchar	*plugin_name);
void            gs_plugin_loader_set_max_parallel_ops  (GsPluginLoader *plugin_loader,
                                                        guint           max_ops);

const gchar	*gs_plugin_loader_get_locale		(GsPluginLoader *plugin_loader);

GsCategoryManager *gs_plugin_loader_get_category_manager (GsPluginLoader *plugin_loader);
void		 gs_plugin_loader_claim_error		(GsPluginLoader *plugin_loader,
							 GsPlugin *plugin,
							 GsPluginAction action,
							 GsApp *app,
							 gboolean interactive,
							 const GError *error);
void		 gs_plugin_loader_claim_job_error	(GsPluginLoader *plugin_loader,
							 GsPlugin *plugin,
							 GsPluginJob *job,
							 const GError *error);

G_END_DECLS
