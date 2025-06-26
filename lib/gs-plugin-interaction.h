/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2025 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <appstream.h>
#include <glib.h>

#include <gs-plugin.h>
#include <gs-plugin-event.h>

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_INTERACTION (gs_plugin_interaction_get_type ())

G_DECLARE_INTERFACE (GsPluginInteraction, gs_plugin_interaction, GS, PLUGIN_INTERACTION, GObject)

struct _GsPluginInteractionInterface {
	GTypeInterface g_iface;

	void	(* event)		(GsPluginInteraction *self,
					 GsPlugin            *plugin,
					 GsPluginEvent       *event);
	void	(* progress)		(GsPluginInteraction *iface,
					 GsPlugin            *plugin,
					 guint                progress);
	void	( *app_needs_user)	(GsPluginInteraction *iface,
					 GsPlugin            *plugin,
					 GsApp               *app,
					 AsScreenshot        *action_screenshot);
	/* padding for future expansion */
	gpointer reserved[12];
};

void		gs_plugin_interaction_event	(GsPluginInteraction *self,
						 GsPlugin            *plugin,
						 GsPluginEvent       *event);
void		gs_plugin_interaction_progress	(GsPluginInteraction *self,
						 GsPlugin            *plugin,
						 guint                progress);
void		gs_plugin_interaction_app_needs_user
						(GsPluginInteraction *self,
						 GsPlugin            *plugin,
						 GsApp               *app,
						 AsScreenshot        *action_screenshot);

G_END_DECLS
