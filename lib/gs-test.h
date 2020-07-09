/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include "gs-app.h"

G_BEGIN_DECLS

void	 gs_test_flush_main_context		(void);
gchar	*gs_test_get_filename			(const gchar	*testdatadir,
						 const gchar	*filename);
void	 gs_test_expose_icon_theme_paths	(void);

G_END_DECLS
