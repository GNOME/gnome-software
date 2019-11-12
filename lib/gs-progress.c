/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-progress.h"
#include <glib.h>

struct _GsProgress
{
	GObject		 parent_instance;
	GMutex		 mutex;
	guint64		 size_downloaded;
	guint64		 size_total;
	gchar		*message;
	guint		 percentage;
};

G_DEFINE_TYPE (GsProgress, gs_progress, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_SIZE_DOWNLOADED,
	PROP_SIZE_TOTAL,
	PROP_MESSAGE,
	PROP_PERCENTAGE,
	PROP_LAST
};

/**
 * gs_progress_get_size_downloaded:
 * @self: A #GsProgress
 *
 * Gets the bytes downloaded.
 *
 * Since: 3.36
 **/
guint64
gs_progress_get_size_downloaded (GsProgress *self)
{
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_val_if_fail (GS_IS_PROGRESS (self), 0);

	locker = g_mutex_locker_new (&self->mutex);
	return self->size_downloaded;
}

/**
 * gs_progress_set_size_downloaded:
 * @self: A #GsApp
 * @size_downloaded: the bytes downloaded
 *
 * Sets the size in bytes that have been downloaded.
 *
 * Since: 3.36
 **/
void
gs_progress_set_size_downloaded (GsProgress *self, guint64 size_downloaded)
{
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_PROGRESS (self));
	g_print ("gs_progress_set_size_downloaded: %lu\n", size_downloaded);

	locker = g_mutex_locker_new (&self->mutex);
	if (self->size_downloaded != size_downloaded) {
		self->size_downloaded = size_downloaded;
		g_object_notify (G_OBJECT (self), "size-downloaded");
	}
}

/**
 * gs_progress_get_size_total:
 * @self: A #GsProgress
 *
 * Gets the total size of the download in bytes.
 *
 * Since: 3.36
 **/
guint64
gs_progress_get_size_total (GsProgress *self)
{
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_val_if_fail (GS_IS_PROGRESS (self), 0);

	locker = g_mutex_locker_new (&self->mutex);
	return self->size_total;
}

/**
 * gs_progress_set_size_total:
 * @self: A #GsApp
 * @size_total: the total download size
 *
 * Sets the size in bytes that need to be downloaded.
 *
 * Since: 3.36
 **/
void
gs_progress_set_size_total (GsProgress *self, guint64 size_total)
{
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_PROGRESS (self));
	g_print ("gs_progress_set_size_total: %lu\n", size_total);

	locker = g_mutex_locker_new (&self->mutex);
	if (self->size_total != size_total) {
		self->size_total = size_total;
		g_object_notify (G_OBJECT (self), "size-total");
	}
}

/**
 * gs_progress_get_message:
 * @self: A #GsProgress
 *
 * Gets the progress message.
 *
 * Since: 3.36
 **/
gchar *
gs_progress_get_message (GsProgress *self)
{
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_val_if_fail (GS_IS_PROGRESS (self), NULL);

	locker = g_mutex_locker_new (&self->mutex);
	return g_strdup (self->message);
}

/**
 * gs_progress_set_message:
 * @self: A #GsApp
 * @message: progress message
 *
 * Sets a custom progress message to show in the UI.
 *
 * Since: 3.36
 **/
void
gs_progress_set_message (GsProgress *self, const gchar *message)
{
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_PROGRESS (self));
	g_print ("gs_progress_set_message: %s\n", message);

	locker = g_mutex_locker_new (&self->mutex);
	if (g_strcmp0 (self->message, message) != 0) {
		g_free (self->message);
		self->message = g_strdup (message);
		g_object_notify (G_OBJECT (self), "message");
	}
}

/**
 * gs_progress_get_percentage:
 * @self: A #GsProgress
 *
 * Gets the percentage completed.
 *
 * Since: 3.36
 **/
guint
gs_progress_get_percentage (GsProgress *self)
{
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_val_if_fail (GS_IS_PROGRESS (self), 0);

	locker = g_mutex_locker_new (&self->mutex);
	return self->percentage;
}

/**
 * gs_progress_set_percentage:
 * @self: A #GsApp
 * @percentage: the percentage completed
 *
 * Sets the percentage that has been completed.
 *
 * Since: 3.36
 **/
void
gs_progress_set_percentage (GsProgress *self, guint percentage)
{
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_PROGRESS (self));
	g_print ("gs_progress_set_percentage: %u\n", percentage);

	percentage = MIN (percentage, 100);

	locker = g_mutex_locker_new (&self->mutex);
	if (self->percentage != percentage) {
		self->percentage = percentage;
		g_object_notify (G_OBJECT (self), "percentage");
	}
}

static void
gs_progress_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsProgress *self = GS_PROGRESS (object);
	switch (prop_id) {
	case PROP_SIZE_DOWNLOADED:
		g_value_set_uint64 (value, gs_progress_get_size_downloaded (self));
		break;
	case PROP_SIZE_TOTAL:
		g_value_set_uint64 (value, gs_progress_get_size_total (self));
		break;
	case PROP_MESSAGE:
		g_value_take_string (value, gs_progress_get_message (self));
		break;
	case PROP_PERCENTAGE:
		g_value_set_uint (value, gs_progress_get_percentage (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_progress_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_progress_finalize (GObject *object)
{
	GsProgress *self = GS_PROGRESS (object);

	g_free (self->message);
	g_mutex_clear (&self->mutex);

	G_OBJECT_CLASS (gs_progress_parent_class)->finalize (object);
}

static void
gs_progress_class_init (GsProgressClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gs_progress_get_property;
	object_class->set_property = gs_progress_set_property;
	object_class->finalize = gs_progress_finalize;

	pspec = g_param_spec_uint64 ("size-downloaded", NULL, NULL, 0, G_MAXUINT64, 0,
	                             G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_SIZE_DOWNLOADED, pspec);

	pspec = g_param_spec_uint64 ("size-total", NULL, NULL, 0, G_MAXUINT64, 0,
	                             G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_SIZE_DOWNLOADED, pspec);

	pspec = g_param_spec_string ("message", NULL, NULL, NULL,
	                             G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_MESSAGE, pspec);

	pspec = g_param_spec_uint ("percentage", NULL, NULL, 0, 100, 0,
	                           G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_PERCENTAGE, pspec);
}

static void
gs_progress_init (GsProgress *self)
{
	g_mutex_init (&self->mutex);
}

/**
 * gs_progress_new:
 *
 * Creates a new progress object.
 *
 * Returns: A newly allocated #GsProgress
 *
 * Since: 3.36
 **/
GsProgress *
gs_progress_new (void)
{
	GsProgress *self;
	self = g_object_new (GS_TYPE_PROGRESS, NULL);
	return GS_PROGRESS (self);
}
