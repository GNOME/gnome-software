/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Canonical Ltd.
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

#include "config.h"

#include "gs-permission-switch.h"

struct _GsPermissionSwitch
{
	GtkSwitch	 parent_instance;

	GsPermission	*permission;
};

G_DEFINE_TYPE (GsPermissionSwitch, gs_permission_switch, GTK_TYPE_SWITCH)

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

GsPermission *
gs_permission_switch_get_permission (GsPermissionSwitch *sw)
{
	g_return_val_if_fail (GS_IS_PERMISSION_SWITCH (sw), NULL);
	return sw->permission;
}

static void
active_changed_cb (GsPermissionSwitch *sw)
{
	GsPermissionValue *value;

	value = g_ptr_array_index (gs_permission_get_values (sw->permission), 0);
	g_signal_emit (sw, signals[SIGNAL_CHANGED], 0,
		       gtk_switch_get_active (GTK_SWITCH (sw)) ? value : NULL);
}

static void
gs_permission_switch_dispose (GObject *object)
{
	GsPermissionSwitch *sw = GS_PERMISSION_SWITCH (object);

	g_clear_object (&sw->permission);

	G_OBJECT_CLASS (gs_permission_switch_parent_class)->dispose (object);
}

static void
gs_permission_switch_class_init (GsPermissionSwitchClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = gs_permission_switch_dispose;

	signals [SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, GS_TYPE_PERMISSION_VALUE);
}

static void
gs_permission_switch_init (GsPermissionSwitch *sw)
{
}

GsPermissionSwitch *
gs_permission_switch_new (GsPermission *permission)
{
	GsPermissionSwitch *sw;

	sw = g_object_new (GS_TYPE_PERMISSION_SWITCH, NULL);
	sw->permission = g_object_ref (permission);
	gtk_switch_set_active (GTK_SWITCH (sw), gs_permission_get_value (permission) != NULL);
	g_signal_connect (sw, "notify::active", G_CALLBACK (active_changed_cb), NULL);

	return sw;
}
