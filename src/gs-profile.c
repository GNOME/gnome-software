/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include <glib/gi18n.h>

#include "gs-profile.h"

static void	gs_profile_finalize	(GObject	*object);

#define GS_PROFILE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_PROFILE, GsProfilePrivate))

struct GsProfilePrivate
{
	GPtrArray	*current;
	GPtrArray	*archived;
	GMutex		 mutex;
};

typedef struct {
	gchar		*id;
	gint64		 time_start;
	gint64		 time_stop;
} GsProfileItem;

G_DEFINE_TYPE (GsProfile, gs_profile, G_TYPE_OBJECT)

static gpointer gs_profile_object = NULL;

/**
 * gs_profile_item_free:
 **/
static void
gs_profile_item_free (GsProfileItem *item)
{
	g_free (item->id);
	g_free (item);
}

/**
 * gs_profile_item_find:
 **/
static GsProfileItem *
gs_profile_item_find (GPtrArray *array, const gchar *id)
{
	GsProfileItem *tmp;
	guint i;

	g_return_val_if_fail (id != NULL, NULL);

	for (i = 0; i < array->len; i++) {
		tmp = g_ptr_array_index (array, i);
		if (g_strcmp0 (tmp->id, id) == 0)
			return tmp;
	}
	return NULL;
}

/**
 * gs_profile_start:
 **/
void
gs_profile_start (GsProfile *profile, const gchar *id)
{
	GsProfileItem *item;

	g_return_if_fail (GS_IS_PROFILE (profile));
	g_return_if_fail (id != NULL);

	/* lock */
	g_mutex_lock (&profile->priv->mutex);

	/* already started */
	item = gs_profile_item_find (profile->priv->current, id);
	if (item != NULL) {
		g_warning ("Already a started task for %s", id);
		goto out;
	}

	/* add new item */
	item = g_new0 (GsProfileItem, 1);
	item->id = g_strdup (id);
	item->time_start = g_get_real_time ();
	g_ptr_array_add (profile->priv->current, item);
	g_debug ("run %s", id);
out:
	/* unlock */
	g_mutex_unlock (&profile->priv->mutex);
}

/**
 * gs_profile_start_full:
 **/
void
gs_profile_start_full (GsProfile *profile, const gchar *id)
{
	gchar *id_thr;
	id_thr = g_strdup_printf ("%p~%s", g_thread_self (), id);
	gs_profile_start (profile, id_thr);
	g_free (id_thr);
}

/**
 * gs_profile_stop:
 **/
void
gs_profile_stop (GsProfile *profile, const gchar *id)
{
	GsProfileItem *item;
	gdouble elapsed_ms;

	g_return_if_fail (GS_IS_PROFILE (profile));
	g_return_if_fail (id != NULL);

	/* lock */
	g_mutex_lock (&profile->priv->mutex);

	/* already started */
	item = gs_profile_item_find (profile->priv->current, id);
	if (item == NULL) {
		g_warning ("Not already a started task for %s", id);
		goto out;
	}

	/* debug */
	elapsed_ms = (item->time_stop - item->time_start) / 1000;
	if (elapsed_ms > 5)
		g_debug ("%s took %.0fms", id, elapsed_ms);

	/* update */
	item->time_stop = g_get_real_time ();

	/* move to archive */
	g_ptr_array_remove (profile->priv->current, item);
	g_ptr_array_add (profile->priv->archived, item);
out:
	/* unlock */
	g_mutex_unlock (&profile->priv->mutex);
}

/**
 * gs_profile_stop_full:
 **/
void
gs_profile_stop_full (GsProfile *profile, const gchar *id)
{
	gchar *id_thr;
	id_thr = g_strdup_printf ("%p~%s", g_thread_self (), id);
	gs_profile_stop (profile, id_thr);
	g_free (id_thr);
}

/**
 * gs_profile_sort_cb:
 **/
static gint
gs_profile_sort_cb (gconstpointer a, gconstpointer b)
{
	GsProfileItem *item_a = *((GsProfileItem **) a);
	GsProfileItem *item_b = *((GsProfileItem **) b);
	if (item_a->time_start < item_b->time_start)
		return -1;
	if (item_a->time_start > item_b->time_start)
		return 1;
	return 0;
}

/**
 * gs_profile_dump:
 **/
void
gs_profile_dump (GsProfile *profile)
{
	GsProfileItem *item;
	gint64 time_start = G_MAXINT64;
	gint64 time_stop = 0;
	gint64 time_ms;
	guint console_width = 86;
	guint i;
	guint j;
	gdouble scale;
	guint bar_offset;
	guint bar_length;

	g_return_if_fail (GS_IS_PROFILE (profile));

	/* nothing to show */
	if (profile->priv->archived->len == 0)
		return;

	/* get the start and end times */
	for (i = 0; i < profile->priv->archived->len; i++) {
		item = g_ptr_array_index (profile->priv->archived, i);
		if (item->time_start < time_start)
			time_start = item->time_start;
		if (item->time_stop > time_stop)
			time_stop = item->time_stop;
	}
	scale = (gdouble) console_width / (gdouble) ((time_stop - time_start) / 1000);

	/* sort the list */
	g_ptr_array_sort (profile->priv->archived, gs_profile_sort_cb);

	/* dump a list of what happened when */
	for (i = 0; i < profile->priv->archived->len; i++) {
		item = g_ptr_array_index (profile->priv->archived, i);
		time_ms = (item->time_stop - item->time_start) / 1000;
		if (time_ms < 5)
			continue;

		/* print a timechart of what we've done */
		bar_offset = scale * (item->time_start - time_start) / 1000;
		for (j = 0; j < bar_offset; j++)
			g_print (" ");
		bar_length = scale * time_ms;
		if (bar_length == 0)
			bar_length = 1;
		for (j = 0; j < bar_length; j++)
			g_print ("#");
		for (j = bar_offset + bar_length; j < console_width + 1; j++)
			g_print (" ");
		g_print ("@%04lims ", (item->time_stop - time_start) / 1000);
		g_print ("%s %lims\n", item->id, time_ms);
	}

	/* not all complete */
	if (profile->priv->current->len > 0) {
		for (i = 0; i < profile->priv->current->len; i++) {
			item = g_ptr_array_index (profile->priv->current, i);
			item->time_stop = g_get_real_time ();
			for (j = 0; j < console_width; j++)
				g_print ("$");
			time_ms = (item->time_stop - item->time_start) / 1000;
			g_print (" @????ms %s %lims\n", item->id, time_ms);
		}
	}
}

/**
 * gs_profile_class_init:
 **/
static void
gs_profile_class_init (GsProfileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_profile_finalize;
	g_type_class_add_private (klass, sizeof (GsProfilePrivate));
}

/**
 * gs_profile_init:
 **/
static void
gs_profile_init (GsProfile *profile)
{
	profile->priv = GS_PROFILE_GET_PRIVATE (profile);
	profile->priv->current = g_ptr_array_new ();
	profile->priv->archived = g_ptr_array_new_with_free_func ((GDestroyNotify) gs_profile_item_free);
	g_mutex_init (&profile->priv->mutex);
}

/**
 * gs_profile_finalize:
 **/
static void
gs_profile_finalize (GObject *object)
{
	GsProfile *profile = GS_PROFILE (object);
	GsProfilePrivate *priv = profile->priv;

	g_ptr_array_foreach (priv->current, (GFunc) gs_profile_item_free, NULL);
	g_ptr_array_unref (priv->current);
	g_ptr_array_unref (priv->archived);

	G_OBJECT_CLASS (gs_profile_parent_class)->finalize (object);
}

/**
 * gs_profile_new:
 **/
GsProfile *
gs_profile_new (void)
{
	if (gs_profile_object != NULL) {
		g_object_ref (gs_profile_object);
	} else {
		gs_profile_object = g_object_new (GS_TYPE_PROFILE, NULL);
		g_object_add_weak_pointer (gs_profile_object, &gs_profile_object);
	}
	return GS_PROFILE (gs_profile_object);
}

/* vim: set noexpandtab: */
