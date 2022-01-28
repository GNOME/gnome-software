/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-plugin-event
 * @title: GsPluginEvent
 * @include: gnome-software.h
 * @stability: Unstable
 * @short_description: Information about a plugin event
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
#include "gs-utils.h"

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

typedef enum {
	PROP_APP = 1,
	PROP_ORIGIN,
	PROP_ACTION,
	PROP_ERROR,
} GsPluginEventProperty;

static GParamSpec *props[PROP_ERROR + 1] = { NULL, };

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
	if (event->origin != NULL &&
	    gs_app_get_unique_id (event->origin) != NULL) {
		return gs_app_get_unique_id (event->origin);
	}
	if (event->app != NULL &&
	    gs_app_get_unique_id (event->app) != NULL) {
		return gs_app_get_unique_id (event->app);
	}

	/* generate from error */
	if (event->error != NULL) {
		if (event->unique_id == NULL) {
			g_autofree gchar *id = NULL;
			id = g_strdup_printf ("%s.error",
					      gs_plugin_error_to_string (event->error->code));
			event->unique_id = gs_utils_build_unique_id (AS_COMPONENT_SCOPE_UNKNOWN,
								     AS_BUNDLE_KIND_UNKNOWN,
								     NULL,
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
	if (event->error) {
		/* Just in case the caller left there any D-Bus remote error notes */
		g_dbus_error_strip_remote_error (event->error);
	}
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
gs_plugin_event_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	GsPluginEvent *self = GS_PLUGIN_EVENT (object);

	switch ((GsPluginEventProperty) prop_id) {
	case PROP_APP:
		g_value_set_object (value, self->app);
		break;
	case PROP_ORIGIN:
		g_value_set_object (value, self->origin);
		break;
	case PROP_ACTION:
		g_value_set_enum (value, self->action);
		break;
	case PROP_ERROR:
		g_value_set_boxed (value, self->error);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_event_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	GsPluginEvent *self = GS_PLUGIN_EVENT (object);

	switch ((GsPluginEventProperty) prop_id) {
	case PROP_APP:
		/* Construct only. */
		g_assert (self->app == NULL);
		self->app = g_value_dup_object (value);
		g_object_notify_by_pspec (object, props[prop_id]);
		break;
	case PROP_ORIGIN:
		/* Construct only. */
		g_assert (self->origin == NULL);
		self->origin = g_value_dup_object (value);
		g_object_notify_by_pspec (object, props[prop_id]);
		break;
	case PROP_ACTION:
		/* Construct only. */
		g_assert (self->action == GS_PLUGIN_ACTION_UNKNOWN);
		self->action = g_value_get_enum (value);
		g_object_notify_by_pspec (object, props[prop_id]);
		break;
	case PROP_ERROR:
		/* Construct only. */
		g_assert (self->error == NULL);
		gs_plugin_event_set_error (self, g_value_get_boxed (value));
		g_object_notify_by_pspec (object, props[prop_id]);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_event_dispose (GObject *object)
{
	GsPluginEvent *event = GS_PLUGIN_EVENT (object);

	g_clear_object (&event->app);
	g_clear_object (&event->origin);

	G_OBJECT_CLASS (gs_plugin_event_parent_class)->dispose (object);
}

static void
gs_plugin_event_finalize (GObject *object)
{
	GsPluginEvent *event = GS_PLUGIN_EVENT (object);

	g_clear_error (&event->error);
	g_free (event->unique_id);

	G_OBJECT_CLASS (gs_plugin_event_parent_class)->finalize (object);
}

static void
gs_plugin_event_class_init (GsPluginEventClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gs_plugin_event_get_property;
	object_class->set_property = gs_plugin_event_set_property;
	object_class->dispose = gs_plugin_event_dispose;
	object_class->finalize = gs_plugin_event_finalize;

	/**
	 * GsPluginEvent:app: (nullable)
	 *
	 * The application (or source, or whatever component) that caused the
	 * event to be created.
	 *
	 * Since: 42
	 */
	props[PROP_APP] =
		g_param_spec_object ("app", "App",
				     "The application (or source, or whatever component) that caused the event to be created.",
				     GS_TYPE_APP,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsPluginEvent:origin: (nullable)
	 *
	 * The origin that caused the event to be created.
	 *
	 * Since: 42
	 */
	props[PROP_ORIGIN] =
		g_param_spec_object ("origin", "Origin",
				     "The origin that caused the event to be created.",
				     GS_TYPE_APP,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsPluginEvent:action:
	 *
	 * The action that caused the event to be created.
	 *
	 * Since: 42
	 */
	props[PROP_ACTION] =
		g_param_spec_enum ("action", "Action",
				   "The action that caused the event to be created.",
				   GS_TYPE_PLUGIN_ACTION, GS_PLUGIN_ACTION_UNKNOWN,
				   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				   G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsPluginEvent:error: (nullable)
	 *
	 * The error the event is reporting.
	 *
	 * Since: 42
	 */
	props[PROP_ERROR] =
		g_param_spec_boxed ("error", "Error",
				    "The error the event is reporting.",
				    G_TYPE_ERROR,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				    G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
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
