/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
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
 * SECTION:gs-app-list
 * @short_description: An application list
 *
 * These functions provide a refcounted list of #GsApp objects.
 */

#include "config.h"

#include <glib.h>

#include "gs-app-list.h"

/**
 * gs_app_list_add:
 * @list: A pointer to a #GsAppList
 * @app: A #GsApp
 *
 * Adds an application to the list, adding a reference.
 **/
void
gs_app_list_add (GsAppList **list, GsApp *app)
{
	g_return_if_fail (list != NULL);
	g_return_if_fail (GS_IS_APP (app));
	*list = g_list_prepend (*list, g_object_ref (app));
}

/**
 * gs_app_list_free:
 * @list: A #GsAppList
 *
 * Frees the application list.
 **/
void
gs_app_list_free (GsAppList *list)
{
	g_list_free_full (list, (GDestroyNotify) g_object_unref);
}

/**
 * gs_app_list_filter:
 * @list: A pointer to a #GsAppList
 * @func: A #GsAppListFilterFunc
 * @user_data: the user pointer to pass to @func
 *
 * If func() returns TRUE for the GsApp, then the app is kept.
 **/
void
gs_app_list_filter (GsAppList **list, GsAppListFilterFunc func, gpointer user_data)
{
	GsAppList *l;
	GsAppList *new = NULL;
	GsApp *app;

	g_return_if_fail (list != NULL);
	g_return_if_fail (func != NULL);

	/* see if any of the apps need filtering */
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (func (app, user_data))
			gs_app_list_add (&new, app);
	}

	/* replace the list */
	gs_app_list_free (*list);
	*list = new;
}

/**
 * gs_app_list_randomize_cb:
 */
static gint
gs_app_list_randomize_cb (gconstpointer a, gconstpointer b, gpointer user_data)
{
	const gchar *k1;
	const gchar *k2;
	g_autofree gchar *key = NULL;

	key = g_strdup_printf ("Plugin::sort-key[%p]", user_data);
	k1 = gs_app_get_metadata_item ((GsApp *) a, key);
	k2 = gs_app_get_metadata_item ((GsApp *) b, key);
	return g_strcmp0 (k1, k2);
}

/**
 * gs_app_list_randomize:
 * @list: A pointer to a #GsAppList
 *
 * Randomize the order of the list, but don't change the order until
 * the next day.
 **/
void
gs_app_list_randomize (GsAppList **list)
{
	GsAppList *l;
	GRand *rand;
	GsApp *app;
	gchar sort_key[] = { '\0', '\0', '\0', '\0' };
	g_autoptr(GDateTime) date = NULL;
	g_autofree gchar *key = NULL;

	g_return_if_fail (list != NULL);

	key = g_strdup_printf ("Plugin::sort-key[%p]", list);
	rand = g_rand_new ();
	date = g_date_time_new_now_utc ();
	g_rand_set_seed (rand, g_date_time_get_day_of_year (date));
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		sort_key[0] = g_rand_int_range (rand, (gint32) 'A', (gint32) 'Z');
		sort_key[1] = g_rand_int_range (rand, (gint32) 'A', (gint32) 'Z');
		sort_key[2] = g_rand_int_range (rand, (gint32) 'A', (gint32) 'Z');
		gs_app_set_metadata (app, key, sort_key);
	}
	*list = g_list_sort_with_data (*list, gs_app_list_randomize_cb, list);
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		gs_app_set_metadata (app, key, NULL);
	}
	g_rand_free (rand);
}

/**
 * gs_app_list_filter_duplicates:
 * @list: A pointer to a #GsAppList
 *
 * Filter any duplicate applications from the list.
 **/
void
gs_app_list_filter_duplicates (GsAppList **list)
{
	GsAppList *l;
	GsAppList *new = NULL;
	GsApp *app;
	GsApp *found;
	const gchar *id;
	g_autoptr(GHashTable) hash = NULL;

	g_return_if_fail (list != NULL);

	/* create a new list with just the unique items */
	hash = g_hash_table_new (g_str_hash, g_str_equal);
	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		id = gs_app_get_id (app);
		if (id == NULL) {
			gs_app_list_add (&new, app);
			continue;
		}
		found = g_hash_table_lookup (hash, id);
		if (found == NULL) {
			gs_app_list_add (&new, app);
			g_hash_table_insert (hash, (gpointer) id,
					     GUINT_TO_POINTER (1));
			continue;
		}
		g_debug ("ignoring duplicate %s", id);
	}

	/* replace the list */
	gs_app_list_free (*list);
	*list = new;
}

/**
 * gs_app_list_copy:
 * @list: A #GsAppList
 *
 * Returns a deep copy of the application list.
 *
 * Returns: A newly allocated #GsAppList
 **/
GsAppList *
gs_app_list_copy (GsAppList *list)
{
	return g_list_copy_deep (list, (GCopyFunc) g_object_ref, NULL);
}

/* vim: set noexpandtab: */
