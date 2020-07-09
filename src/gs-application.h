/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

#include "gnome-software-private.h"

#define GS_APPLICATION_TYPE (gs_application_get_type ())

G_DECLARE_FINAL_TYPE (GsApplication, gs_application, GS, APPLICATION, GtkApplication)

GsApplication	*gs_application_new		(void);
GsPluginLoader	*gs_application_get_plugin_loader	(GsApplication *application);
gboolean	 gs_application_has_active_window	(GsApplication *application);
