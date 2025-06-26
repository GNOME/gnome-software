/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2025 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * SECTION:gs-plugin-interaction
 * @short_description: interface handling interaction with the user
 *
 * The #GsPluginInteraction interface is used by th eplugins inside the job
 * calls to interact with the user. It's up to each job which operations
 * are allowed.
 *
 * Since: 49
 */

#include "config.h"

#include <appstream.h>
#include <glib.h>

#include "gs-plugin.h"
#include "gs-plugin-event.h"
#include "gs-plugin-interaction.h"

G_DEFINE_INTERFACE (GsPluginInteraction, gs_plugin_interaction, G_TYPE_OBJECT)

static void
gs_plugin_interaction_default_event (GsPluginInteraction *self,
				     GsPlugin            *plugin,
				     GsPluginEvent       *event)
{
	g_warning ("Plugin '%s' called '%s', but '%s' does not implement it",
		G_OBJECT_TYPE_NAME (plugin),
		G_STRFUNC,
		G_OBJECT_TYPE_NAME (self));
}

static void
gs_plugin_interaction_default_progress (GsPluginInteraction *self,
					GsPlugin            *plugin,
					guint                progress)
{
	g_warning ("Plugin '%s' called '%s', but '%s' does not implement it",
		G_OBJECT_TYPE_NAME (plugin),
		G_STRFUNC,
		G_OBJECT_TYPE_NAME (self));
}

static void
gs_plugin_interaction_default_app_needs_user (GsPluginInteraction *self,
					      GsPlugin            *plugin,
					      GsApp               *app,
					      AsScreenshot        *action_screenshot)
{
	g_warning ("Plugin '%s' called '%s', but '%s' does not implement it",
		G_OBJECT_TYPE_NAME (plugin),
		G_STRFUNC,
		G_OBJECT_TYPE_NAME (self));
}

static void
gs_plugin_interaction_default_init (GsPluginInteractionInterface *iface)
{
	iface->event = gs_plugin_interaction_default_event;
	iface->progress = gs_plugin_interaction_default_progress;
	iface->app_needs_user = gs_plugin_interaction_default_app_needs_user;
}

/**
 * gs_plugin_interaction_event:
 * @self: (nullable): a #GsPluginInteraction, or %NULL
 * @plugin: a #GsPlugin
 * @event: a #GsPluginEvent
 *
 * Calls GsPluginInteractionInterface::event() method, or does nothing when @iface is %NULL
 *
 * Since: 49
 **/
void
gs_plugin_interaction_event (GsPluginInteraction *self,
			     GsPlugin            *plugin,
			     GsPluginEvent       *event)
{
	GsPluginInteractionInterface *iface;

	if (self == NULL)
		return;

	g_return_if_fail (GS_IS_PLUGIN_INTERACTION (self));
	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_return_if_fail (GS_IS_PLUGIN_EVENT (event));

	iface = GS_PLUGIN_INTERACTION_GET_IFACE (self);
	g_return_if_fail (iface != NULL);

	iface->event (self, plugin, event);
}

/**
 * gs_plugin_interaction_progress:
 * @self: (nullable): a #GsPluginInteraction, or %NULL
 * @plugin: a #GsPlugin
 * @progress: a progress, in percentage
 *
 * Calls GsPluginInteractionInterface::progress() method, or does nothing when @iface is %NULL
 *
 * Since: 49
 **/
void
gs_plugin_interaction_progress (GsPluginInteraction *self,
				GsPlugin            *plugin,
				guint                progress)
{
	GsPluginInteractionInterface *iface;

	if (self == NULL)
		return;

	g_return_if_fail (GS_IS_PLUGIN_INTERACTION (self));
	g_return_if_fail (GS_IS_PLUGIN (plugin));

	iface = GS_PLUGIN_INTERACTION_GET_IFACE (self);
	g_return_if_fail (iface != NULL);

	iface->progress (self, plugin, progress);
}

/**
 * gs_plugin_interaction_app_needs_user:
 * @self: (nullable): a #GsPluginInteraction, or %NULL
 * @plugin: a #GsPlugin
 * @app: an app
 * @action_screenshot: (nullable): optional #AsScreenshot, or %NULL
 *
 * Calls GsPluginInteractionInterface::progress() method, or does nothing when @iface is %NULL
 *
 * Since: 49
 **/
void
gs_plugin_interaction_app_needs_user (GsPluginInteraction *self,
				      GsPlugin            *plugin,
				      GsApp               *app,
				      AsScreenshot        *action_screenshot)
{
	GsPluginInteractionInterface *iface;

	if (self == NULL)
		return;

	g_return_if_fail (GS_IS_PLUGIN_INTERACTION (self));
	g_return_if_fail (GS_IS_PLUGIN (plugin));
	g_return_if_fail (GS_IS_APP (app));
	if (action_screenshot)
		g_return_if_fail (AS_IS_SCREENSHOT (action_screenshot));

	iface = GS_PLUGIN_INTERACTION_GET_IFACE (self);
	g_return_if_fail (iface != NULL);

	iface->app_needs_user (self, plugin, app, action_screenshot);
}
