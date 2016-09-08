/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * SECTION:gs-event
 * @title: GsPluginEvent
 * @include: gnome-software.h
 * @stability: Unstable
 * @short_description: Infomation about a plugin event
 *
 * These functions provide a way for plugins to tell the UI layer about events
 * that may require displaying to the user. Plugins should not assume that a
 * specific event is actually shown to the user as it may be ignored
 * automatically.
 */

#include "config.h"

#include <glib.h>

#include "gs-plugin-private.h"
#include "gs-plugin-event.h"

struct _GsPluginEvent
{
	GObject			 parent_instance;
	GsApp			*app;
	GsApp			*origin;
	GsPluginAction		 action;
	GError			*error;
	GsPluginEventFlag	 flags;
	gchar			*unique_id;
};

G_DEFINE_TYPE (GsPluginEvent, gs_plugin_event, G_TYPE_OBJECT)

/**
 * gs_plugin_event_set_app:
 * @event: A #GsPluginEvent
 * @app: A #GsApp
 *
 * Set the application (or source, or whatever component) that caused the event
 * to be created.
 *
 * Since: 3.22
 **/
void
gs_plugin_event_set_app (GsPluginEvent *event, GsApp *app)
{
	g_return_if_fail (GS_IS_PLUGIN_EVENT (event));
	g_return_if_fail (GS_IS_APP (app));
	g_set_object (&event->app, app);
}

/**
 * gs_plugin_event_get_app:
 * @event: A #GsPluginEvent
 *
 * Gets an application that created the event.
 *
 * Returns: (transfer none): a #GsApp, or %NULL if unset
 *
 * Since: 3.22
 **/
GsApp *
gs_plugin_event_get_app (GsPluginEvent *event)
{
	g_return_val_if_fail (GS_IS_PLUGIN_EVENT (event), NULL);
	return event->app;
}

/**
 * gs_plugin_event_set_origin:
 * @event: A #GsPluginEvent
 * @origin: A #GsApp
 *
 * Set the origin that caused the event to be created.
 *
 * Since: 3.22
 **/
void
gs_plugin_event_set_origin (GsPluginEvent *event, GsApp *origin)
{
	g_return_if_fail (GS_IS_PLUGIN_EVENT (event));
	g_return_if_fail (GS_IS_APP (origin));
	g_set_object (&event->origin, origin);
}

/**
 * gs_plugin_event_get_origin:
 * @event: A #GsPluginEvent
 *
 * Gets an origin that created the event.
 *
 * Returns: (transfer none): a #GsApp, or %NULL if unset
 *
 * Since: 3.22
 **/
GsApp *
gs_plugin_event_get_origin (GsPluginEvent *event)
{
	g_return_val_if_fail (GS_IS_PLUGIN_EVENT (event), NULL);
	return event->origin;
}

/**
 * gs_plugin_event_set_action:
 * @event: A #GsPluginEvent
 * @action: A #GsPluginAction, e.g. %GS_PLUGIN_ACTION_UPDATE
 *
 * Set the action that caused the event to be created.
 *
 * Since: 3.22
 **/
void
gs_plugin_event_set_action (GsPluginEvent *event, GsPluginAction action)
{
	g_return_if_fail (GS_IS_PLUGIN_EVENT (event));
	event->action = action;
}

/**
 * gs_plugin_event_get_action:
 * @event: A #GsPluginEvent
 *
 * Gets an action that created the event.
 *
 * Returns: (transfer none): a #GsPluginAction, e.g. %GS_PLUGIN_ACTION_UPDATE
 *
 * Since: 3.22
 **/
GsPluginAction
gs_plugin_event_get_action (GsPluginEvent *event)
{
	g_return_val_if_fail (GS_IS_PLUGIN_EVENT (event), 0);
	return event->action;
}

/**
 * gs_plugin_event_get_unique_id:
 * @event: A #GsPluginEvent
 *
 * Gets the unique ID for the event. In most cases (if an app has been set)
 * this will just be the actual #GsApp unique-id. In the cases where only error
 * has been set a virtual (but plausible) ID will be generated.
 *
 * Returns: a string, or %NULL for invalid
 *
 * Since: 3.22
 **/
const gchar *
gs_plugin_event_get_unique_id (GsPluginEvent *event)
{
	/* just proxy */
	if (event->origin != NULL)
		return gs_app_get_unique_id (event->origin);
	if (event->app != NULL)
		return gs_app_get_unique_id (event->app);

	/* generate from error */
	if (event->error != NULL) {
		if (event->unique_id == NULL) {
			g_autofree gchar *id = NULL;
			id = g_strdup_printf ("%s.error",
					      gs_plugin_error_to_string (event->error->code));
			event->unique_id = as_utils_unique_id_build (AS_APP_SCOPE_UNKNOWN,
								     AS_BUNDLE_KIND_UNKNOWN,
								     NULL,
								     AS_APP_KIND_UNKNOWN,
								     id,
								     NULL);
		}
		return event->unique_id;
	}

	/* failed */
	return NULL;
}

/**
 * gs_plugin_event_get_kind:
 * @event: A #GsPluginEvent
 * @flag: A #GsPluginEventFlag, e.g. %GS_PLUGIN_EVENT_FLAG_INVALID
 *
 * Adds a flag to the event.
 *
 * Since: 3.22
 **/
void
gs_plugin_event_add_flag (GsPluginEvent *event, GsPluginEventFlag flag)
{
	g_return_if_fail (GS_IS_PLUGIN_EVENT (event));
	event->flags |= flag;
}

/**
 * gs_plugin_event_set_kind:
 * @event: A #GsPluginEvent
 * @flag: A #GsPluginEventFlag, e.g. %GS_PLUGIN_EVENT_FLAG_INVALID
 *
 * Removes a flag from the event.
 *
 * Since: 3.22
 **/
void
gs_plugin_event_remove_flag (GsPluginEvent *event, GsPluginEventFlag flag)
{
	g_return_if_fail (GS_IS_PLUGIN_EVENT (event));
	event->flags &= ~flag;
}

/**
 * gs_plugin_event_has_flag:
 * @event: A #GsPluginEvent
 * @flag: A #GsPluginEventFlag, e.g. %GS_PLUGIN_EVENT_FLAG_INVALID
 *
 * Finds out if the event has a specific flag.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 3.22
 **/
gboolean
gs_plugin_event_has_flag (GsPluginEvent *event, GsPluginEventFlag flag)
{
	g_return_val_if_fail (GS_IS_PLUGIN_EVENT (event), FALSE);
	return ((event->flags & flag) > 0);
}

/**
 * gs_plugin_event_set_error:
 * @event: A #GsPluginEvent
 * @error: A #GError
 *
 * Sets the event error.
 *
 * Since: 3.22
 **/
void
gs_plugin_event_set_error (GsPluginEvent *event, const GError *error)
{
	g_clear_error (&event->error);
	event->error = g_error_copy (error);
}

/**
 * gs_plugin_event_get_error:
 * @event: A #GsPluginEvent
 *
 * Gets the event error.
 *
 * Returns: a #GError, or %NULL for unset
 *
 * Since: 3.22
 **/
const GError *
gs_plugin_event_get_error (GsPluginEvent *event)
{
	return event->error;
}

static void
gs_plugin_event_finalize (GObject *object)
{
	GsPluginEvent *event = GS_PLUGIN_EVENT (object);
	if (event->error != NULL)
		g_error_free (event->error);
	if (event->app != NULL)
		g_object_unref (event->app);
	if (event->origin != NULL)
		g_object_unref (event->origin);
	g_free (event->unique_id);
	G_OBJECT_CLASS (gs_plugin_event_parent_class)->finalize (object);
}

static void
gs_plugin_event_class_init (GsPluginEventClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_plugin_event_finalize;
}

static void
gs_plugin_event_init (GsPluginEvent *event)
{
}

/**
 * gs_plugin_event_new:
 *
 * Creates a new event.
 *
 * Returns: A newly allocated #GsPluginEvent
 *
 * Since: 3.22
 **/
GsPluginEvent *
gs_plugin_event_new (void)
{
	GsPluginEvent *event;
	event = g_object_new (GS_TYPE_PLUGIN_EVENT, NULL);
	return GS_PLUGIN_EVENT (event);
}

/* vim: set noexpandtab: */
