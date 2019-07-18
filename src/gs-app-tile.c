/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2019 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-app-tile.h"
#include "gs-common.h"

typedef struct {
	GsApp				*app;
	guint				 app_state_changed_idle_id;
} GsAppTilePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GsAppTile, gs_app_tile, GTK_TYPE_BUTTON)

GsApp *
gs_app_tile_get_app (GsAppTile *self)
{
	GsAppTilePrivate *priv = gs_app_tile_get_instance_private (self);
	g_return_val_if_fail (GS_IS_APP_TILE (self), NULL);
	return priv->app;
}

static gboolean
gs_app_tile_state_changed_idle_cb (gpointer user_data)
{
	GsAppTile *self = GS_APP_TILE (user_data);
	GsAppTileClass *klass = GS_APP_TILE_GET_CLASS (self);
	GsAppTilePrivate *priv = gs_app_tile_get_instance_private (self);
	priv->app_state_changed_idle_id = 0;
	klass->refresh (self);
	return G_SOURCE_REMOVE;
}

static void
gs_app_tile_state_changed_cb (GsApp *app, GParamSpec *pspec, GsAppTile *self)
{
	GsAppTilePrivate *priv = gs_app_tile_get_instance_private (self);
	g_clear_handle_id (&priv->app_state_changed_idle_id, g_source_remove);
	priv->app_state_changed_idle_id = g_idle_add (gs_app_tile_state_changed_idle_cb, self);
}

void
gs_app_tile_set_app (GsAppTile *self, GsApp *app)
{
	GsAppTileClass *klass = GS_APP_TILE_GET_CLASS (self);
	GsAppTilePrivate *priv = gs_app_tile_get_instance_private (self);

	g_return_if_fail (GS_IS_APP_TILE (self));
	g_return_if_fail (!app || GS_IS_APP (app));

	/* cancel pending refresh */
	g_clear_handle_id (&priv->app_state_changed_idle_id, g_source_remove);

	/* disconnect old app */
	if (priv->app != NULL)
		g_signal_handlers_disconnect_by_func (priv->app, gs_app_tile_state_changed_cb, self);
	g_set_object (&priv->app, app);

	/* optional refresh */
	if (klass->refresh != NULL && priv->app != NULL) {
		g_signal_connect (app, "notify::state",
				  G_CALLBACK (gs_app_tile_state_changed_cb), self);
		g_signal_connect (app, "notify::name",
				  G_CALLBACK (gs_app_tile_state_changed_cb), self);
		g_signal_connect (app, "notify::summary",
				  G_CALLBACK (gs_app_tile_state_changed_cb), self);
		g_signal_connect (app, "notify::key-colors",
				  G_CALLBACK (gs_app_tile_state_changed_cb), self);
		klass->refresh (self);
	}
}

static void
gs_app_tile_finalize (GObject *object)
{
	GsAppTile *self = GS_APP_TILE (object);
	GsAppTilePrivate *priv = gs_app_tile_get_instance_private (self);
	if (priv->app != NULL)
		g_signal_handlers_disconnect_by_func (priv->app, gs_app_tile_state_changed_cb, self);
	g_clear_handle_id (&priv->app_state_changed_idle_id, g_source_remove);
	g_clear_object (&priv->app);
}

void
gs_app_tile_class_init (GsAppTileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_app_tile_finalize;
}

void
gs_app_tile_init (GsAppTile *self)
{
	GsAppTilePrivate *priv = gs_app_tile_get_instance_private (self);
	priv->app_state_changed_idle_id = 0;
}

GtkWidget *
gs_app_tile_new (GsApp *app)
{
	GsAppTile *self = g_object_new (GS_TYPE_APP_TILE, NULL);
	gs_app_tile_set_app (self, app);
	return GTK_WIDGET (self);
}
