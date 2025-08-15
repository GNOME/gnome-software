/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: (C) 2025 Red Hat <www.redhat.com>
 */

#pragma once

#include <gnome-software.h>

void		gs_rpm_ostree_refine_app_from_changelogs(GsApp *owner_app,
							 gchar *in_changelogs);
