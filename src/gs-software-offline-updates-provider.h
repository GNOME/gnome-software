/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * SPDX-FileCopyrightText: (C) 2026 Red Hat <www.redhat.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include "gnome-software-private.h"

#define GS_TYPE_SOFTWARE_OFFLINE_UPDATES_PROVIDER gs_software_offline_updates_provider_get_type()

G_DECLARE_FINAL_TYPE (GsSoftwareOfflineUpdatesProvider, gs_software_offline_updates_provider, GS, SOFTWARE_OFFLINE_UPDATES_PROVIDER, GObject)

GsSoftwareOfflineUpdatesProvider *
			 gs_software_offline_updates_provider_new	(void);
void			 gs_software_offline_updates_provider_setup	(GsSoftwareOfflineUpdatesProvider *self,
									 GsPluginLoader	                  *loader);
gboolean		 gs_software_offline_updates_provider_register	(GsSoftwareOfflineUpdatesProvider *self,
									 GDBusConnection                  *connection,
									 GError                          **error);
void			 gs_software_offline_updates_provider_unregister(GsSoftwareOfflineUpdatesProvider *self);
