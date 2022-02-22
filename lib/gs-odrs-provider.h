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
#include <libsoup/soup.h>

#include <gs-plugin.h>

G_BEGIN_DECLS

/**
 * GsOdrsProviderError:
 * @GS_ODRS_PROVIDER_ERROR_DOWNLOADING: Error while downloading ODRS data.
 * @GS_ODRS_PROVIDER_ERROR_PARSING_DATA: Problem parsing downloaded ODRS data.
 * @GS_ODRS_PROVIDER_ERROR_NO_NETWORK: Offline or network unavailable.
 * @GS_ODRS_PROVIDER_ERROR_SERVER_ERROR: Server rejected ODRS submission or returned an error.
 *
 * Error codes for #GsOdrsProvider.
 *
 * Since: 42
 */
typedef enum {
	GS_ODRS_PROVIDER_ERROR_DOWNLOADING,
	GS_ODRS_PROVIDER_ERROR_PARSING_DATA,
	GS_ODRS_PROVIDER_ERROR_NO_NETWORK,
	GS_ODRS_PROVIDER_ERROR_SERVER_ERROR,
} GsOdrsProviderError;

#define GS_ODRS_PROVIDER_ERROR gs_odrs_provider_error_quark ()
GQuark		 gs_odrs_provider_error_quark		(void);

#define GS_TYPE_ODRS_PROVIDER (gs_odrs_provider_get_type ())

G_DECLARE_FINAL_TYPE (GsOdrsProvider, gs_odrs_provider, GS, ODRS_PROVIDER, GObject)

GsOdrsProvider	*gs_odrs_provider_new			(const gchar		 *review_server,
							 const gchar		 *user_hash,
							 const gchar		 *distro,
							 guint64		  max_cache_age_secs,
							 guint			  n_results_max,
							 SoupSession		 *session);

gboolean	 gs_odrs_provider_refresh		(GsOdrsProvider		 *self,
							 GsPlugin		 *plugin,
							 guint64		  cache_age_secs,
							 GCancellable		 *cancellable,
							 GError			**error);

gboolean	 gs_odrs_provider_refine		(GsOdrsProvider		 *self,
							 GsAppList		 *list,
							 GsPluginRefineFlags	  flags,
							 GCancellable		 *cancellable,
							 GError			**error);

gboolean	 gs_odrs_provider_submit_review		(GsOdrsProvider		 *self,
							 GsApp			 *app,
							 AsReview		 *review,
							 GCancellable		 *cancellable,
							 GError			**error);
gboolean	 gs_odrs_provider_report_review		(GsOdrsProvider		 *self,
							 GsApp			 *app,
							 AsReview		 *review,
							 GCancellable		 *cancellable,
							 GError			**error);
gboolean	 gs_odrs_provider_upvote_review		(GsOdrsProvider		 *self,
							 GsApp			 *app,
							 AsReview		 *review,
							 GCancellable		 *cancellable,
							 GError			**error);
gboolean	 gs_odrs_provider_downvote_review	(GsOdrsProvider		 *self,
							 GsApp			 *app,
							 AsReview		 *review,
							 GCancellable		 *cancellable,
							 GError			**error);
gboolean	 gs_odrs_provider_dismiss_review	(GsOdrsProvider		 *self,
							 GsApp			 *app,
							 AsReview		 *review,
							 GCancellable		 *cancellable,
							 GError			**error);
gboolean	 gs_odrs_provider_remove_review		(GsOdrsProvider		 *self,
							 GsApp			 *app,
							 AsReview		 *review,
							 GCancellable		 *cancellable,
							 GError			**error);

gboolean	 gs_odrs_provider_add_unvoted_reviews	(GsOdrsProvider		 *self,
							 GsAppList		 *list,
							 GCancellable		 *cancellable,
							 GError			**error);

G_END_DECLS
