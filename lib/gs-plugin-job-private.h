/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib-object.h>

#include "gs-plugin-job.h"

G_BEGIN_DECLS

gchar			*gs_plugin_job_to_string		(GsPluginJob	*self);
void			 gs_plugin_job_cancel			(GsPluginJob	*self);

G_END_DECLS
