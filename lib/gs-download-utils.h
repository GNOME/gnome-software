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
#include <libsoup/soup.h>

G_BEGIN_DECLS

SoupSession *gs_build_soup_session (void);

/**
 * GsDownloadProgressCallback:
 * @bytes_downloaded: number of bytes downloaded so far
 * @total_download_size: the total size of the download, in bytes
 * @user_data: data passed to the calling function
 *
 * A progress callback to indicate how far a download has progressed.
 *
 * @total_download_size may be zero (for example, at the start of the download),
 * so implementations of this callback must be careful to avoid division by zero
 * errors.
 *
 * @total_download_size is guaranteed to always be greater than or equal to
 * @bytes_downloaded.
 *
 * Since: 42
 */
typedef void (*GsDownloadProgressCallback) (gsize    bytes_downloaded,
                                            gsize    total_download_size,
                                            gpointer user_data);

void		gs_download_stream_async	(SoupSession                *soup_session,
						 const gchar                *uri,
						 GOutputStream              *output_stream,
						 const gchar                *last_etag,
						 int                         io_priority,
						 GsDownloadProgressCallback  progress_callback,
						 gpointer                    progress_user_data,
						 GCancellable               *cancellable,
						 GAsyncReadyCallback         callback,
						 gpointer                    user_data);
gboolean	gs_download_stream_finish	(SoupSession   *soup_session,
						 GAsyncResult  *result,
						 gchar        **new_etag_out,
						 GError       **error);

G_END_DECLS
