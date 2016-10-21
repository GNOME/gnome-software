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
 * @title: GsAppList
 * @include: gnome-software.h
 * @stability: Unstable
 * @short_description: An application list
 *
 * These functions provide a refcounted list of #GsApp objects.
 */

#include "config.h"

#include <glib.h>

#include "gs-app-private.h"
#include "gs-app-list-private.h"

struct _GsAppList
{
	GObject			 parent_instance;
	GPtrArray		*array;
	GHashTable		*hash_by_id;		/* app-id : app */
	GMutex			 mutex;
};

G_DEFINE_TYPE (GsAppList, gs_app_list, G_TYPE_OBJECT)

/**
 * gs_app_list_lookup:
 * @list: A #GsAppList
 * @unique_id: A unique_id
 *
 * Finds the first matching application in the list using the usual wildcard
 * rules allowed in unique_ids.
 *
 * Returns: (transfer none): a #GsApp, or %NULL if not found
 *
 * Since: 3.22
 **/
GsApp *
gs_app_list_lookup (GsAppList *list, const gchar *unique_id)
{
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&list->mutex);
	return g_hash_table_lookup (list->hash_by_id, unique_id);
}

static gboolean
gs_app_list_check_for_duplicate (GsAppList *list, GsApp *app)
{
	GsApp *app_old;
	const gchar *id;
	const gchar *id_old = NULL;

	/* does not exist */
	id = gs_app_get_unique_id (app);
	app_old = g_hash_table_lookup (list->hash_by_id, id);
	if (app_old == NULL) {
		g_debug ("adding %s as nothing matched hash", id);
		return TRUE;
	}

	/* existing app is a wildcard */
	id_old = gs_app_get_unique_id (app_old);
	if (gs_app_has_quirk (app_old, AS_APP_QUIRK_MATCH_ANY_PREFIX)) {
		g_debug ("adding %s as %s is a wildcard", id, id_old);
		return TRUE;
	}

	/* do a sanity check */
	if (!as_utils_unique_id_equal (id, id_old)) {
		g_debug ("unique-id non-equal %s as %s but hash matched!",
			 id, id_old);
		return TRUE;
	}

	/* already exists */
	g_debug ("not adding duplicate %s as %s already exists", id, id_old);
	return FALSE;
}

static void
gs_app_list_add_safe (GsAppList *list, GsApp *app)
{
	const gchar *id;
	guint i;

	/* if we're lazy-loading the ID then we can't filter for duplicates */
	id = gs_app_get_unique_id (app);
	if (id == NULL) {
		g_ptr_array_add (list->array, g_object_ref (app));
		return;
	}

	/* check for duplicate */
	if (!gs_app_list_check_for_duplicate (list, app))
		return;

	/* just use the ref */
	g_ptr_array_add (list->array, g_object_ref (app));
	g_hash_table_insert (list->hash_by_id, g_strdup (id), g_object_ref (app));
}

/**
 * gs_app_list_add:
 * @list: A #GsAppList
 * @app: A #GsApp
 *
 * If the application does not already exist in the list then it is added,
 * incrementing the reference count.
 * If the application already exists then a warning is printed to the console.
 *
 * Applications that have the application ID lazy-loaded will always be addded
 * to the list, and to clean these up the plugin loader will also call the
 * gs_app_list_filter_duplicates() method when all plugins have run.
 *
 * Since: 3.22
 **/
void
gs_app_list_add (GsAppList *list, GsApp *app)
{
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&list->mutex);
	g_return_if_fail (GS_IS_APP_LIST (list));
	g_return_if_fail (GS_IS_APP (app));
	gs_app_list_add_safe (list, app);
}

/**
 * gs_app_list_add_list:
 * @list: A #GsAppList
 * @donor: Another #GsAppList
 *
 * Adds all the applications in @donor to @list.
 *
 * Since: 3.24
 **/
void
gs_app_list_add_list (GsAppList *list, GsAppList *donor)
{
	guint i;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&list->mutex);

	g_return_if_fail (GS_IS_APP_LIST (list));
	g_return_if_fail (GS_IS_APP_LIST (donor));
	g_return_if_fail (list != donor);

	/* add each app */
	for (i = 0; i < donor->array->len; i++) {
		GsApp *app = gs_app_list_index (donor, i);
		gs_app_list_add_safe (list, app);
	}
}

/**
 * gs_app_list_index:
 * @list: A #GsAppList
 * @idx: An index into the list
 *
 * Gets an application at a specific position in the list.
 *
 * Returns: (transfer none): a #GsApp, or %NULL if invalid
 *
 * Since: 3.22
 **/
GsApp *
gs_app_list_index (GsAppList *list, guint idx)
{
	return GS_APP (g_ptr_array_index (list->array, idx));
}

/**
 * gs_app_list_length:
 * @list: A #GsAppList
 *
 * Gets the length of the application list.
 *
 * Returns: Integer
 *
 * Since: 3.22
 **/
guint
gs_app_list_length (GsAppList *list)
{
	g_return_val_if_fail (GS_IS_APP_LIST (list), 0);
	return list->array->len;
}

static void
gs_app_list_remove_all_safe (GsAppList *list)
{
	g_ptr_array_set_size (list->array, 0);
	g_hash_table_remove_all (list->hash_by_id);
}

/**
 * gs_app_list_remove_all:
 * @list: A #GsAppList
 *
 * Removes all applications from the list.
 *
 * Since: 3.22
 **/
void
gs_app_list_remove_all (GsAppList *list)
{
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&list->mutex);
	g_return_if_fail (GS_IS_APP_LIST (list));
	gs_app_list_remove_all_safe (list);
}

/**
 * gs_app_list_filter:
 * @list: A #GsAppList
 * @func: A #GsAppListFilterFunc
 * @user_data: the user pointer to pass to @func
 *
 * If func() returns TRUE for the GsApp, then the app is kept.
 *
 * Since: 3.22
 **/
void
gs_app_list_filter (GsAppList *list, GsAppListFilterFunc func, gpointer user_data)
{
	guint i;
	GsApp *app;
	g_autoptr(GsAppList) old = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&list->mutex);

	g_return_if_fail (GS_IS_APP_LIST (list));
	g_return_if_fail (func != NULL);

	/* deep copy to a temp list and clear the current one */
	old = gs_app_list_copy (list);
	gs_app_list_remove_all_safe (list);

	/* see if any of the apps need filtering */
	for (i = 0; i < old->array->len; i++) {
		app = gs_app_list_index (old, i);
		if (func (app, user_data))
			gs_app_list_add_safe (list, app);
	}
}

typedef struct {
	GsAppListSortFunc	 func;
	gpointer		 user_data;
} GsAppListSortHelper;

static gint
gs_app_list_sort_cb (gconstpointer a, gconstpointer b, gpointer user_data)
{
	GsApp *app1 = GS_APP (*(GsApp **) a);
	GsApp *app2 = GS_APP (*(GsApp **) b);
	GsAppListSortHelper *helper = (GsAppListSortHelper *) user_data;
	return helper->func (app1, app2, user_data);
}

/**
 * gs_app_list_sort:
 * @list: A #GsAppList
 * @func: A #GCompareFunc
 *
 * Sorts the application list.
 *
 * Since: 3.22
 **/
void
gs_app_list_sort (GsAppList *list, GsAppListSortFunc func, gpointer user_data)
{
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&list->mutex);
	GsAppListSortHelper helper;
	g_return_if_fail (GS_IS_APP_LIST (list));
	helper.func = func;
	helper.user_data = user_data;
	g_ptr_array_sort_with_data (list->array, gs_app_list_sort_cb, &helper);
}

static gint
gs_app_list_randomize_cb (gconstpointer a, gconstpointer b, gpointer user_data)
{
	GsApp *app1 = GS_APP (*(GsApp **) a);
	GsApp *app2 = GS_APP (*(GsApp **) b);
	const gchar *k1;
	const gchar *k2;
	g_autofree gchar *key = NULL;

	key = g_strdup_printf ("Plugin::sort-key[%p]", user_data);
	k1 = gs_app_get_metadata_item (app1, key);
	k2 = gs_app_get_metadata_item (app2, key);
	return g_strcmp0 (k1, k2);
}

/**
 * gs_app_list_randomize:
 * @list: A #GsAppList
 *
 * Randomize the order of the list, but don't change the order until
 * the next day.
 *
 * Since: 3.22
 **/
void
gs_app_list_randomize (GsAppList *list)
{
	guint i;
	GRand *rand;
	GsApp *app;
	gchar sort_key[] = { '\0', '\0', '\0', '\0' };
	g_autoptr(GDateTime) date = NULL;
	g_autofree gchar *key = NULL;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&list->mutex);

	g_return_if_fail (GS_IS_APP_LIST (list));

	key = g_strdup_printf ("Plugin::sort-key[%p]", list);
	rand = g_rand_new ();
	date = g_date_time_new_now_utc ();
	g_rand_set_seed (rand, (guint32) g_date_time_get_day_of_year (date));
	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		sort_key[0] = (gchar) g_rand_int_range (rand, (gint32) 'A', (gint32) 'Z');
		sort_key[1] = (gchar) g_rand_int_range (rand, (gint32) 'A', (gint32) 'Z');
		sort_key[2] = (gchar) g_rand_int_range (rand, (gint32) 'A', (gint32) 'Z');
		gs_app_set_metadata (app, key, sort_key);
	}
	g_ptr_array_sort_with_data (list->array, gs_app_list_randomize_cb, list);
	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		gs_app_set_metadata (app, key, NULL);
	}
	g_rand_free (rand);
}

/**
 * gs_app_list_filter_duplicates:
 * @list: A #GsAppList
 *
 * Filter any duplicate applications from the list.
 *
 * Since: 3.22
 **/
void
gs_app_list_filter_duplicates (GsAppList *list, GsAppListFilterFlags flags)
{
	guint i;
	GsApp *app;
	GsApp *found;
	const gchar *id;
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GList) values = NULL;
	GList *l;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&list->mutex);

	g_return_if_fail (GS_IS_APP_LIST (list));

	/* create a new list with just the unique items */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal,
				      g_free, (GDestroyNotify) g_object_unref);
	for (i = 0; i < list->array->len; i++) {
		app = gs_app_list_index (list, i);
		id = gs_app_get_unique_id (app);
		if (flags & GS_APP_LIST_FILTER_FLAG_PRIORITY)
			id = gs_app_get_id (app);
		if (id == NULL) {
			g_autofree gchar *str = gs_app_to_string (app);
			g_debug ("ignoring as no application id for: %s", str);
			continue;
		}
		found = g_hash_table_lookup (hash, id);
		if (found == NULL) {
			g_debug ("found new %s", id);
			g_hash_table_insert (hash,
					     g_strdup (id),
					     g_object_ref (app));
			continue;
		}

		/* better? */
		if (flags & GS_APP_LIST_FILTER_FLAG_PRIORITY) {
			if (gs_app_get_priority (app) >
			    gs_app_get_priority (found)) {
				g_debug ("using better %s (priority %u > %u)",
					 id,
					 gs_app_get_priority (app),
					 gs_app_get_priority (found));
				g_hash_table_insert (hash,
						     g_strdup (id),
						     g_object_ref (app));
				continue;
			}
			g_debug ("ignoring worse duplicate %s (priority %u > %u)",
				 id,
				 gs_app_get_priority (app),
				 gs_app_get_priority (found));
			continue;
		}
		g_debug ("ignoring duplicate %s", id);
		continue;
	}

	/* add back the best results to the existing list */
	gs_app_list_remove_all_safe (list);
	values = g_hash_table_get_values (hash);
	for (l = values; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		gs_app_list_add_safe (list, app);
	}
}

/**
 * gs_app_list_copy:
 * @list: A #GsAppList
 *
 * Returns a deep copy of the application list.
 *
 * Returns: A newly allocated #GsAppList
 *
 * Since: 3.22
 **/
GsAppList *
gs_app_list_copy (GsAppList *list)
{
	GsAppList *new;
	guint i;

	g_return_val_if_fail (GS_IS_APP_LIST (list), NULL);

	new = gs_app_list_new ();
	for (i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		gs_app_list_add_safe (new, app);
	}
	return new;
}

static void
gs_app_list_finalize (GObject *object)
{
	GsAppList *list = GS_APP_LIST (object);
	g_ptr_array_unref (list->array);
	g_hash_table_unref (list->hash_by_id);
	g_mutex_clear (&list->mutex);
	G_OBJECT_CLASS (gs_app_list_parent_class)->finalize (object);
}

static void
gs_app_list_class_init (GsAppListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_app_list_finalize;
}

static void
gs_app_list_init (GsAppList *list)
{
	g_mutex_init (&list->mutex);
	list->array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	list->hash_by_id = g_hash_table_new_full ((GHashFunc) as_utils_unique_id_hash,
						  (GEqualFunc) as_utils_unique_id_equal,
						  g_free,
						  (GDestroyNotify) g_object_unref);
}

/**
 * gs_app_list_new:
 *
 * Creates a new list.
 *
 * Returns: A newly allocated #GsAppList
 *
 * Since: 3.22
 **/
GsAppList *
gs_app_list_new (void)
{
	GsAppList *list;
	list = g_object_new (GS_TYPE_APP_LIST, NULL);
	return GS_APP_LIST (list);
}

/* vim: set noexpandtab: */
