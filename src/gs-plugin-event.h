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

#ifndef __GS_PLUGIN_EVENT
#define __GS_PLUGIN_EVENT

#include <glib-object.h>

#include "gs-app.h"
#include "gs-plugin-private.h"

G_BEGIN_DECLS

#define GS_TYPE_PLUGIN_EVENT (gs_plugin_event_get_type ())

G_DECLARE_FINAL_TYPE (GsPluginEvent, gs_plugin_event, GS, PLUGIN_EVENT, GObject)

/**
 * GsPluginEventFlag:
 * @GS_PLUGIN_EVENT_FLAG_NONE:		No special flags set
 * @GS_PLUGIN_EVENT_FLAG_INVALID:	Event is no longer valid, e.g. was dismissed
 * @GS_PLUGIN_EVENT_FLAG_VISIBLE:	Event is is visible on the screen
 * @GS_PLUGIN_EVENT_FLAG_WARNING:	Event should be shown with more urgency
 *
 * Any flags an event can have.
 **/
typedef enum {
	GS_PLUGIN_EVENT_FLAG_NONE	= 0,		/* Since: 3.22 */
	GS_PLUGIN_EVENT_FLAG_INVALID	= 1 << 0,	/* Since: 3.22 */
	GS_PLUGIN_EVENT_FLAG_VISIBLE	= 1 << 1,	/* Since: 3.22 */
	GS_PLUGIN_EVENT_FLAG_WARNING	= 1 << 2,	/* Since: 3.22 */
	/*< private >*/
	GS_PLUGIN_EVENT_FLAG_LAST
} GsPluginEventFlag;

GsPluginEvent		*gs_plugin_event_new		(void);

const gchar		*gs_plugin_event_get_unique_id	(GsPluginEvent		*event);

void			 gs_plugin_event_set_app	(GsPluginEvent		*event,
							 GsApp			*app);
GsApp			*gs_plugin_event_get_app	(GsPluginEvent		*event);
void			 gs_plugin_event_set_origin	(GsPluginEvent		*event,
							 GsApp			*origin);
GsApp			*gs_plugin_event_get_origin	(GsPluginEvent		*event);
void			 gs_plugin_event_set_action	(GsPluginEvent		*event,
							 GsPluginAction		 action);
GsPluginAction		 gs_plugin_event_get_action	(GsPluginEvent		*event);

void			 gs_plugin_event_set_error	(GsPluginEvent		*event,
							 const GError		*error);
const GError		*gs_plugin_event_get_error	(GsPluginEvent		*event);

void			 gs_plugin_event_add_flag	(GsPluginEvent		*event,
							 GsPluginEventFlag	 flag);
void			 gs_plugin_event_remove_flag	(GsPluginEvent		*event,
							 GsPluginEventFlag	 flag);
gboolean		 gs_plugin_event_has_flag	(GsPluginEvent		*event,
							 GsPluginEventFlag	 flag);

G_END_DECLS

#endif /* __GS_PLUGIN_EVENT */

/* vim: set noexpandtab: */
