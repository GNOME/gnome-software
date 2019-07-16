/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2017-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
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
#include "gs-app-collation.h"

struct _GsAppList
{
	GObject			 parent_instance;
	GPtrArray		*array;
	GMutex			 mutex;
	guint			 size_peak;
	GsAppListFlags		 flags;
	AsAppState		 state;
	guint			 progress;
};

G_DEFINE_TYPE (GsAppList, gs_app_list, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_STATE,
	PROP_PROGRESS,
	PROP_LAST
};

/**
 * gs_app_list_get_state:
 * @list: A #GsAppList
 *
 * Gets the state of the list.
 *
 * This method will only return a valid result if gs_app_list_add_flag() has
 * been called with %GS_APP_LIST_FLAG_WATCH_APPS.
 *
 * Returns: the #AsAppState, e.g. %AS_APP_STATE_INSTALLED
 *
 * Since: 3.30
 **/
AsAppState
gs_app_list_get_state (GsAppList *list)
{
	g_return_val_if_fail (GS_IS_APP_LIST (list), AS_APP_STATE_UNKNOWN);
	return list->state;
}

/**
 * gs_app_list_get_progress:
 * @list: A #GsAppList
 *
 * Gets the average percentage completion of all apps in the list.
 *
 * This method will only return a valid result if gs_app_list_add_flag() has
 * been called with %GS_APP_LIST_FLAG_WATCH_APPS.
 *
 * Returns: the percentage completion, or 0 for unknown
 *
 * Since: 3.30
 **/
guint
gs_app_list_get_progress (GsAppList *list)
{
	g_return_val_if_fail (GS_IS_APP_LIST (list), 0);
	return list->progress;
}

static void
gs_app_list_add_watched_for_app (GsAppList *list, GPtrArray *apps, GsApp *app)
{
	if (list->flags & GS_APP_LIST_FLAG_WATCH_APPS)
		g_ptr_array_add (apps, app);
	if (list->flags & GS_APP_LIST_FLAG_WATCH_APPS_ADDONS) {
		GsAppList *list2 = gs_app_get_addons (app);
		for (guint i = 0; i < gs_app_list_length (list2); i++) {
			GsApp *app2 = gs_app_list_index (list2, i);
			g_ptr_array_add (apps, app2);
		}
	}
	if (list->flags & GS_APP_LIST_FLAG_WATCH_APPS_RELATED) {
		GsAppList *list2 = gs_app_get_related (app);
		for (guint i = 0; i < gs_app_list_length (list2); i++) {
			GsApp *app2 = gs_app_list_index (list2, i);
			g_ptr_array_add (apps, app2);
		}
	}
}

static GPtrArray *
gs_app_list_get_watched_for_app (GsAppList *list, GsApp *app)
{
	GPtrArray *apps = g_ptr_array_new ();
	gs_app_list_add_watched_for_app (list, apps, app);
	return apps;
}

static GPtrArray *
gs_app_list_get_watched (GsAppList *list)
{
	GPtrArray *apps = g_ptr_array_new ();
	for (guint i = 0; i < list->array->len; i++) {
		GsApp *app_tmp = g_ptr_array_index (list->array, i);
		gs_app_list_add_watched_for_app (list, apps, app_tmp);
	}
	return apps;
}

static void
gs_app_list_invalidate_progress (GsAppList *self)
{
	guint progress = 0;
	g_autoptr(GPtrArray) apps = gs_app_list_get_watched (self);

	/* find the average percentage complete of the list */
	if (apps->len > 0) {
		guint64 pc_cnt = 0;
		for (guint i = 0; i < apps->len; i++) {
			GsApp *app_tmp = g_ptr_array_index (apps, i);
			pc_cnt += gs_app_get_progress (app_tmp);
		}
		progress = pc_cnt / apps->len;
	}
	if (self->progress != progress) {
		self->progress = progress;
		g_object_notify (G_OBJECT (self), "progress");
	}
}

static void
gs_app_list_invalidate_state (GsAppList *self)
{
	AsAppState state = AS_APP_STATE_UNKNOWN;
	g_autoptr(GPtrArray) apps = gs_app_list_get_watched (self);

	/* find any action state of the list */
	for (guint i = 0; i < apps->len; i++) {
		GsApp *app_tmp = g_ptr_array_index (apps, i);
		AsAppState state_tmp = gs_app_get_state (app_tmp);
		if (state_tmp == AS_APP_STATE_INSTALLING ||
		    state_tmp == AS_APP_STATE_REMOVING) {
			state = state_tmp;
			break;
		}
	}
	if (self->state != state) {
		self->state = state;
		g_object_notify (G_OBJECT (self), "state");
	}
}

static void
gs_app_list_progress_notify_cb (GsApp *app, GParamSpec *pspec, GsAppList *self)
{
	gs_app_list_invalidate_progress (self);
}

static void
gs_app_list_state_notify_cb (GsApp *app, GParamSpec *pspec, GsAppList *self)
{
	gs_app_list_invalidate_state (self);
}

static void
gs_app_list_maybe_watch_app (GsAppList *list, GsApp *app)
{
	g_autoptr(GPtrArray) apps = gs_app_list_get_watched_for_app (list, app);
	for (guint i = 0; i < apps->len; i++) {
		GsApp *app_tmp = g_ptr_array_index (apps, i);
		g_signal_connect_object (app_tmp, "notify::progress",
					 G_CALLBACK (gs_app_list_progress_notify_cb),
					 list, 0);
		g_signal_connect_object (app_tmp, "notify::state",
					 G_CALLBACK (gs_app_list_state_notify_cb),
					 list, 0);
	}
}

static void
gs_app_list_maybe_unwatch_app (GsAppList *list, GsApp *app)
{
	g_autoptr(GPtrArray) apps = gs_app_list_get_watched_for_app (list, app);
	for (guint i = 0; i < apps->len; i++) {
		GsApp *app_tmp = g_ptr_array_index (apps, i);
		g_signal_handlers_disconnect_by_data (app_tmp, list);
	}
}

/**
 * gs_app_list_get_size_peak:
 * @list: A #GsAppList
 *
 * Returns the largest size the list has ever been.
 *
 * Returns: integer
 *
 * Since: 3.24
 **/
guint
gs_app_list_get_size_peak (GsAppList *list)
{
	return list->size_peak;
}

static GsApp *
gs_app_list_lookup_safe (GsAppList *list, const gchar *unique_id)
{
	for (guint i = 0; i < list->array->len; i++) {
		GsApp *app = g_ptr_array_index (list->array, i);
		if (as_utils_unique_id_equal (gs_app_get_unique_id (app), unique_id))
			return app;
	}
	return NULL;
}

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
	return gs_app_list_lookup_safe (list, unique_id);
}

/**
 * gs_app_list_has_flag:
 * @list: A #GsAppList
 * @flag: A flag to test, e.g. %GS_APP_LIST_FLAG_IS_TRUNCATED
 *
 * Gets if a specific flag is set.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 3.24
 **/
gboolean
gs_app_list_has_flag (GsAppList *list, GsAppListFlags flag)
{
	return (list->flags & flag) > 0;
}

/**
 * gs_app_list_add_flag:
 * @list: A #GsAppList
 * @flag: A flag to test, e.g. %GS_APP_LIST_FLAG_IS_TRUNCATED
 *
 * Gets if a specific flag is set.
 *
 * Returns: %TRUE if the flag is set
 *
 * Since: 3.30
 **/
void
gs_app_list_add_flag (GsAppList *list, GsAppListFlags flag)
{
	if (list->flags & flag)
		return;
	list->flags |= flag;

	/* turn this on for existing apps */
	for (guint i = 0; i < list->array->len; i++) {
		GsApp *app = g_ptr_array_index (list->array, i);
		gs_app_list_maybe_watch_app (list, app);
	}
}

static gboolean
gs_app_list_check_for_duplicate (GsAppList *list, GsApp *app)
{
	GsApp *app_old;
	const gchar *id;

	/* adding a wildcard */
	if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD)) {
		for (guint i = 0; i < list->array->len; i++) {
			GsApp *app_tmp = g_ptr_array_index (list->array, i);
			if (!gs_app_has_quirk (app_tmp, GS_APP_QUIRK_IS_WILDCARD))
				continue;
			/* not adding exactly the same wildcard */
			if (g_strcmp0 (gs_app_get_unique_id (app_tmp),
				       gs_app_get_unique_id (app)) == 0)
				return FALSE;
		}
		return TRUE;
	}

	/* does not exist */
	id = gs_app_get_unique_id (app);
	if (id == NULL) {
		for (guint i = 0; i < list->array->len; i++) {
			GsApp *app_tmp = g_ptr_array_index (list->array, i);
			if (app_tmp == app)
				return FALSE;
		}
		/* not much else we can do... */
		return TRUE;
	}

	/* existing app is a wildcard */
	app_old = gs_app_list_lookup_safe (list, id);
	if (app_old == NULL)
		return TRUE;
	if (gs_app_has_quirk (app_old, GS_APP_QUIRK_IS_WILDCARD))
		return TRUE;

	/* already exists */
	return FALSE;
}

typedef enum {
	GS_APP_LIST_ADD_FLAG_NONE		= 0,
	GS_APP_LIST_ADD_FLAG_CHECK_FOR_DUPE	= 1 << 0,
	GS_APP_LIST_ADD_FLAG_LAST
} GsAppListAddFlag;

static void
gs_app_list_add_safe (GsAppList *list, GsApp *app, GsAppListAddFlag flag)
{
	const gchar *id;

	/* check for duplicate */
	if ((flag & GS_APP_LIST_ADD_FLAG_CHECK_FOR_DUPE) > 0 &&
	    !gs_app_list_check_for_duplicate (list, app))
		return;

	/* if we're lazy-loading the ID then we can't use the ID hash */
	id = gs_app_get_unique_id (app);
	if (id == NULL) {
		gs_app_list_maybe_watch_app (list, app);
		g_ptr_array_add (list->array, g_object_ref (app));
		return;
	}

	/* just use the ref */
	gs_app_list_maybe_watch_app (list, app);
	g_ptr_array_add (list->array, g_object_ref (app));

	/* update the historical max */
	if (list->array->len > list->size_peak)
		list->size_peak = list->array->len;
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
 * Applications that have the application ID lazy-loaded will always be added
 * to the list, and to clean these up the plugin loader will also call the
 * gs_app_list_filter_duplicates() method when all plugins have run.
 *
 * Since: 3.22
 **/
void
gs_app_list_add (GsAppList *list, GsApp *app)
{
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP_LIST (list));
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&list->mutex);
	gs_app_list_add_safe (list, app, GS_APP_LIST_ADD_FLAG_CHECK_FOR_DUPE);

	/* recalculate global state */
	gs_app_list_invalidate_state (list);
	gs_app_list_invalidate_progress (list);
}

/**
 * gs_app_list_remove:
 * @list: A #GsAppList
 * @app: A #GsApp
 *
 * Removes an application from the list. If the application does not exist the
 * request is ignored.
 *
 * Since: 3.22
 **/
void
gs_app_list_remove (GsAppList *list, GsApp *app)
{
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_APP_LIST (list));
	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&list->mutex);
	g_ptr_array_remove (list->array, app);
	gs_app_list_maybe_unwatch_app (list, app);

	/* recalculate global state */
	gs_app_list_invalidate_state (list);
	gs_app_list_invalidate_progress (list);
}

/**
 * gs_app_list_add_list:
 * @list: A #GsAppList
 * @donor: Another #GsAppList
 *
 * Adds all the applications in @donor to @list.
 *
 * Since: 3.22
 **/
void
gs_app_list_add_list (GsAppList *list, GsAppList *donor)
{
	guint i;
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_APP_LIST (list));
	g_return_if_fail (GS_IS_APP_LIST (donor));
	g_return_if_fail (list != donor);

	locker = g_mutex_locker_new (&list->mutex);

	/* add each app */
	for (i = 0; i < donor->array->len; i++) {
		GsApp *app = gs_app_list_index (donor, i);
		gs_app_list_add_safe (list, app, GS_APP_LIST_ADD_FLAG_CHECK_FOR_DUPE);
	}

	/* recalculate global state */
	gs_app_list_invalidate_state (list);
	gs_app_list_invalidate_progress (list);
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
	for (guint i = 0; i < list->array->len; i++) {
		GsApp *app = g_ptr_array_index (list->array, i);
		gs_app_list_maybe_unwatch_app (list, app);
	}
	g_ptr_array_set_size (list->array, 0);
	gs_app_list_invalidate_state (list);
	gs_app_list_invalidate_progress (list);
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
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP_LIST (list));
	locker = g_mutex_locker_new (&list->mutex);
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
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_APP_LIST (list));
	g_return_if_fail (func != NULL);

	locker = g_mutex_locker_new (&list->mutex);

	/* deep copy to a temp list and clear the current one */
	old = gs_app_list_copy (list);
	gs_app_list_remove_all_safe (list);

	/* see if any of the apps need filtering */
	for (i = 0; i < old->array->len; i++) {
		app = gs_app_list_index (old, i);
		if (func (app, user_data))
			gs_app_list_add_safe (list, app, GS_APP_LIST_ADD_FLAG_NONE);
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
 * @user_data: user data to pass to @func
 *
 * Sorts the application list.
 *
 * Since: 3.22
 **/
void
gs_app_list_sort (GsAppList *list, GsAppListSortFunc func, gpointer user_data)
{
	g_autoptr(GMutexLocker) locker = NULL;
	GsAppListSortHelper helper;
	g_return_if_fail (GS_IS_APP_LIST (list));
	locker = g_mutex_locker_new (&list->mutex);
	helper.func = func;
	helper.user_data = user_data;
	g_ptr_array_sort_with_data (list->array, gs_app_list_sort_cb, &helper);
}

/**
 * gs_app_list_truncate:
 * @list: A #GsAppList
 * @length: the new length
 *
 * Truncates the application list. It is an error if @length is larger than the
 * size of the list.
 *
 * Since: 3.24
 **/
void
gs_app_list_truncate (GsAppList *list, guint length)
{
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_APP_LIST (list));
	g_return_if_fail (length <= list->array->len);

	/* mark this list as unworthy */
	list->flags |= GS_APP_LIST_FLAG_IS_TRUNCATED;

	/* everything */
	if (length == 0) {
		gs_app_list_remove_all (list);
		return;
	}

	/* remove the apps in the positions larger than the length */
	locker = g_mutex_locker_new (&list->mutex);
	g_ptr_array_set_size (list->array, length);
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
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_APP_LIST (list));

	locker = g_mutex_locker_new (&list->mutex);

	/* mark this list as random */
	list->flags |= GS_APP_LIST_FLAG_IS_RANDOMIZED;

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

static gboolean
gs_app_list_filter_app_is_better (GsApp *app, GsApp *found, GsAppListFilterFlags flags)
{
	/* optional 1st layer sort */
	if ((flags & GS_APP_LIST_FILTER_FLAG_PREFER_INSTALLED) > 0) {
		if (gs_app_is_installed (app) && !gs_app_is_installed (found))
			return TRUE;
		if (!gs_app_is_installed (app) && gs_app_is_installed (found))
			return FALSE;
	}

	/* 2nd layer, priority and bundle kind */
	if (gs_app_compare_priority (app, found) < 0)
		return TRUE;

	/* assume is worse */
	return FALSE;
}

static GPtrArray *
gs_app_list_filter_app_get_keys (GsApp *app, GsAppListFilterFlags flags)
{
	GPtrArray *keys = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GString) key = NULL;

	/* just use the unique ID */
	if (flags == GS_APP_LIST_FILTER_FLAG_NONE) {
		if (gs_app_get_unique_id (app) != NULL)
			g_ptr_array_add (keys, g_strdup (gs_app_get_unique_id (app)));
		return keys;
	}

	/* use the ID and any provides */
	if (flags & GS_APP_LIST_FILTER_FLAG_KEY_ID_PROVIDES) {
		GPtrArray *provides = gs_app_get_provides (app);
		g_ptr_array_add (keys, g_strdup (gs_app_get_id (app)));
		for (guint i = 0; i < provides->len; i++) {
			AsProvide *prov = g_ptr_array_index (provides, i);
			if (as_provide_get_kind (prov) != AS_PROVIDE_KIND_ID)
				continue;
			g_ptr_array_add (keys, g_strdup (as_provide_get_value (prov)));
		}
		return keys;
	}

	/* specific compound type */
	key = g_string_new (NULL);
	if (flags & GS_APP_LIST_FILTER_FLAG_KEY_ID) {
		const gchar *tmp = gs_app_get_id (app);
		if (tmp != NULL)
			g_string_append (key, gs_app_get_id (app));
	}
	if (flags & GS_APP_LIST_FILTER_FLAG_KEY_SOURCE) {
		const gchar *tmp = gs_app_get_source_default (app);
		if (tmp != NULL)
			g_string_append_printf (key, ":%s", tmp);
	}
	if (flags & GS_APP_LIST_FILTER_FLAG_KEY_VERSION) {
		const gchar *tmp = gs_app_get_version (app);
		if (tmp != NULL)
			g_string_append_printf (key, ":%s", tmp);
	}
	if (key->len == 0)
		return keys;
	g_ptr_array_add (keys, g_string_free (g_steal_pointer (&key), FALSE));
	return keys;
}

/**
 * gs_app_list_filter_duplicates:
 * @list: A #GsAppList
 * @flags: a #GsAppListFilterFlags, e.g. GS_APP_LIST_FILTER_KEY_ID
 *
 * Filter any duplicate applications from the list.
 *
 * Since: 3.22
 **/
void
gs_app_list_filter_duplicates (GsAppList *list, GsAppListFilterFlags flags)
{
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GHashTable) kept_apps = NULL;
	g_autoptr(GsAppList) old = NULL;
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_APP_LIST (list));

	locker = g_mutex_locker_new (&list->mutex);

	/* a hash table to hold apps with unique app ids */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	/* a hash table containing apps we want to keep */
	kept_apps = g_hash_table_new (g_direct_hash, g_direct_equal);

	for (guint i = 0; i < list->array->len; i++) {
		GsApp *app = gs_app_list_index (list, i);
		GsApp *found = NULL;
		g_autoptr(GPtrArray) keys = NULL;

		/* get all the keys used to identify this app */
		keys = gs_app_list_filter_app_get_keys (app, flags);
		for (guint j = 0; j < keys->len; j++) {
			const gchar *key = g_ptr_array_index (keys, j);
			found = g_hash_table_lookup (hash, key);
			if (found != NULL)
				break;
		}

		/* new app */
		if (found == NULL) {
			for (guint j = 0; j < keys->len; j++) {
				const gchar *key = g_ptr_array_index (keys, j);
				g_hash_table_insert (hash, g_strdup (key), app);
			}
			g_hash_table_add (kept_apps, app);
			continue;
		}

		/* better? */
		if (flags != GS_APP_LIST_FILTER_FLAG_NONE) {
			if (gs_app_list_filter_app_is_better (app, found, flags)) {
				for (guint j = 0; j < keys->len; j++) {
					const gchar *key = g_ptr_array_index (keys, j);
					g_hash_table_insert (hash, g_strdup (key), app);
				}
				g_hash_table_remove (kept_apps, found);
				g_hash_table_add (kept_apps, app);
				continue;
			}
			continue;
		}
		continue;
	}

	/* deep copy to a temp list and clear the current one */
	old = gs_app_list_copy (list);
	gs_app_list_remove_all_safe (list);

	/* add back the apps we want to keep */
	for (guint i = 0; i < old->array->len; i++) {
		GsApp *app = gs_app_list_index (old, i);
		if (g_hash_table_contains (kept_apps, app))
			gs_app_list_add_safe (list, app, GS_APP_LIST_ADD_FLAG_NONE);
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
		gs_app_list_add_safe (new, app, GS_APP_LIST_ADD_FLAG_NONE);
	}
	return new;
}

static void
gs_app_list_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsAppList *self = GS_APP_LIST (object);
	switch (prop_id) {
	case PROP_STATE:
		g_value_set_uint (value, self->state);
		break;
	case PROP_PROGRESS:
		g_value_set_uint (value, self->progress);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_list_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_list_finalize (GObject *object)
{
	GsAppList *list = GS_APP_LIST (object);
	g_ptr_array_unref (list->array);
	g_mutex_clear (&list->mutex);
	G_OBJECT_CLASS (gs_app_list_parent_class)->finalize (object);
}

static void
gs_app_list_class_init (GsAppListClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = gs_app_list_get_property;
	object_class->set_property = gs_app_list_set_property;
	object_class->finalize = gs_app_list_finalize;

	/**
	 * GsAppList:state:
	 */
	pspec = g_param_spec_uint ("state", NULL, NULL,
				   AS_APP_STATE_UNKNOWN,
				   AS_APP_STATE_LAST,
				   AS_APP_STATE_UNKNOWN,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_STATE, pspec);

	/**
	 * GsAppList:progress:
	 */
	pspec = g_param_spec_uint ("progress", NULL, NULL, 0, 100, 0,
				   G_PARAM_READABLE);
	g_object_class_install_property (object_class, PROP_PROGRESS, pspec);
}

static void
gs_app_list_init (GsAppList *list)
{
	g_mutex_init (&list->mutex);
	list->array = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
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
