/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Canonical Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-channel.h"

struct _GsChannel
{
	GObject	 parent_instance;

	gchar	*name;
	gchar	*version;
};

G_DEFINE_TYPE (GsChannel, gs_channel, G_TYPE_OBJECT)

/**
 * gs_channel_get_name:
 * @channel: a #GsChannel
 *
 * Get the channel name.
 *
 * Returns: a channel name.
 *
 * Since: 3.34
 **/
const gchar *
gs_channel_get_name (GsChannel *channel)
{
	g_return_val_if_fail (GS_IS_CHANNEL (channel), NULL);
	return channel->name;
}

/**
 * gs_channel_get_version:
 * @channel: a #GsChannel
 *
 * Get the channel version.
 *
 * Returns: a channel version.
 *
 * Since: 3.34
 **/
const gchar *
gs_channel_get_version (GsChannel *channel)
{
	g_return_val_if_fail (GS_IS_CHANNEL (channel), NULL);
	return channel->version;
}

static void
gs_channel_finalize (GObject *object)
{
	GsChannel *channel = GS_CHANNEL (object);

	g_free (channel->name);
	g_free (channel->version);

	G_OBJECT_CLASS (gs_channel_parent_class)->finalize (object);
}

static void
gs_channel_class_init (GsChannelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_channel_finalize;
}

static void
gs_channel_init (GsChannel *channel)
{
}

/**
 * gs_channel_new:
 * @name: the name of the channel.
 * @version: the version this channel is providing.
 *
 * Creates a new channel object.
 *
 * Return value: a new #GsChannel object.
 *
 * Since: 3.34
 **/
GsChannel *
gs_channel_new (const gchar *name, const gchar *version)
{
	GsChannel *channel;
	channel = g_object_new (GS_TYPE_CHANNEL, NULL);
	channel->name = g_strdup (name);
	channel->version = g_strdup (version);
	return GS_CHANNEL (channel);
}
