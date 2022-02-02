/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021, 2022 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-download-utils
 * @short_description: Download and HTTP utilities
 *
 * A set of utilities for downloading things and doing HTTP requests.
 *
 * Since: 42
 */

#include "config.h"

#include <glib.h>
#include <libsoup/soup.h>

#include "gs-download-utils.h"
#include "gs-utils.h"

/**
 * gs_build_soup_session:
 *
 * Build a new #SoupSession configured with the gnome-software user agent.
 *
 * A new #SoupSession should be used for each independent download context, such
 * as in different plugins. Each #SoupSession caches HTTP connections and
 * authentication information, and these likely needn’t be shared between
 * plugins. Using separate sessions reduces thread contention.
 *
 * Returns: (transfer full): a new #SoupSession
 * Since: 42
 */
SoupSession *
gs_build_soup_session (void)
{
	return soup_session_new_with_options ("user-agent", gs_user_agent (),
					      "timeout", 10,
					      NULL);
}
