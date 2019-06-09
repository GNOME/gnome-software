/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Canonical Ltd.
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_CHANNEL (gs_channel_get_type ())

G_DECLARE_FINAL_TYPE (GsChannel, gs_channel, GS, CHANNEL, GObject)

GsChannel	*gs_channel_new		(const gchar	*name,
					 const gchar	*version);

const gchar	*gs_channel_get_name	(GsChannel	*channel);

const gchar	*gs_channel_get_version	(GsChannel	*channel);

G_END_DECLS
