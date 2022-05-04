/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2022 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-app-query
 * @short_description: Immutable representation of a query for apps
 *
 * #GsAppQuery is an object to represent a query for apps.
 *
 * It will typically be used with #GsPluginJobListApps, which searches for
 * matching apps, but it may have multiple consumers. #GsAppQuery only
 * represents the query and does not provide an implementation for executing
 * that query.
 *
 * It is immutable after construction, and hence threadsafe. It may be extended
 * in future by adding more query properties. The existing query properties are
 * conjunctive: results should only be returned which match *all* properties
 * which are set, not _any_ properties which are set.
 *
 * The set of apps returned for the query can be controlled with the
 * #GsAppQuery:refine-flags,
 * #GsAppQuery:max-results and
 * #GsAppQuery:dedupe-flags properties. If `refine-flags` is
 * set, all results must be refined using the given set of refine flags (see
 * #GsPluginJobRefine). `max-results` and `dedupe-flags` are used to limit the
 * set of results.
 *
 * Results must always be processed in this order:
 *  - Filtering using #GsAppQuery:filter-func (and any other custom filter
 *    functions the query executor provides).
 *  - Deduplication using #GsAppQuery:dedupe-flags.
 *  - Sorting using #GsAppQuery:sort-func.
 *  - Truncating result list length to #GsAppQuery:max-results.
 *
 * Since: 43
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>

#include "gs-app.h"
#include "gs-app-list.h"
#include "gs-app-query.h"
#include "gs-enums.h"
#include "gs-plugin-types.h"
#include "gs-utils.h"

struct _GsAppQuery
{
	GObject parent;

	GsPluginRefineFlags refine_flags;
	guint max_results;
	GsAppListFilterFlags dedupe_flags;

	GsAppListSortFunc sort_func;
	gpointer sort_user_data;
	GDestroyNotify sort_user_data_notify;

	GsAppListFilterFunc filter_func;
	gpointer filter_user_data;
	GDestroyNotify filter_user_data_notify;

	/* This is guaranteed to either be %NULL, or a non-empty array */
	gchar **provides_files;  /* (owned) (nullable) (array zero-terminated=1) */
	GDateTime *released_since;  /* (owned) (nullable) */
	GsAppQueryTristate is_curated;
	GsCategory *category;  /* (nullable) (owned) */
};

G_DEFINE_TYPE (GsAppQuery, gs_app_query, G_TYPE_OBJECT)

typedef enum {
	PROP_REFINE_FLAGS = 1,
	PROP_MAX_RESULTS,
	PROP_DEDUPE_FLAGS,
	PROP_SORT_FUNC,
	PROP_SORT_USER_DATA,
	PROP_SORT_USER_DATA_NOTIFY,
	PROP_FILTER_FUNC,
	PROP_FILTER_USER_DATA,
	PROP_FILTER_USER_DATA_NOTIFY,
	PROP_PROVIDES_FILES,
	PROP_RELEASED_SINCE,
	PROP_IS_CURATED,
	PROP_CATEGORY,
} GsAppQueryProperty;

static GParamSpec *props[PROP_CATEGORY + 1] = { NULL, };

static void
gs_app_query_get_property (GObject    *object,
                           guint       prop_id,
                           GValue     *value,
                           GParamSpec *pspec)
{
	GsAppQuery *self = GS_APP_QUERY (object);

	switch ((GsAppQueryProperty) prop_id) {
	case PROP_REFINE_FLAGS:
		g_value_set_flags (value, self->refine_flags);
		break;
	case PROP_MAX_RESULTS:
		g_value_set_uint (value, self->max_results);
		break;
	case PROP_DEDUPE_FLAGS:
		g_value_set_flags (value, self->dedupe_flags);
		break;
	case PROP_SORT_FUNC:
		g_value_set_pointer (value, self->sort_func);
		break;
	case PROP_SORT_USER_DATA:
		g_value_set_pointer (value, self->sort_user_data);
		break;
	case PROP_SORT_USER_DATA_NOTIFY:
		g_value_set_pointer (value, self->sort_user_data_notify);
		break;
	case PROP_FILTER_FUNC:
		g_value_set_pointer (value, self->filter_func);
		break;
	case PROP_FILTER_USER_DATA:
		g_value_set_pointer (value, self->filter_user_data);
		break;
	case PROP_FILTER_USER_DATA_NOTIFY:
		g_value_set_pointer (value, self->filter_user_data_notify);
		break;
	case PROP_PROVIDES_FILES:
		g_value_set_boxed (value, self->provides_files);
		break;
	case PROP_RELEASED_SINCE:
		g_value_set_boxed (value, self->released_since);
		break;
	case PROP_IS_CURATED:
		g_value_set_enum (value, self->is_curated);
		break;
	case PROP_CATEGORY:
		g_value_set_object (value, self->category);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_query_set_property (GObject      *object,
                           guint         prop_id,
                           const GValue *value,
                           GParamSpec   *pspec)
{
	GsAppQuery *self = GS_APP_QUERY (object);

	switch ((GsAppQueryProperty) prop_id) {
	case PROP_REFINE_FLAGS:
		/* Construct only. */
		g_assert (self->refine_flags == 0);
		self->refine_flags = g_value_get_flags (value);
		break;
	case PROP_MAX_RESULTS:
		/* Construct only. */
		g_assert (self->max_results == 0);
		self->max_results = g_value_get_uint (value);
		break;
	case PROP_DEDUPE_FLAGS:
		/* Construct only. */
		g_assert (self->dedupe_flags == 0);
		self->dedupe_flags = g_value_get_flags (value);
		break;
	case PROP_SORT_FUNC:
		/* Construct only. */
		g_assert (self->sort_func == NULL);
		self->sort_func = g_value_get_pointer (value);
		break;
	case PROP_SORT_USER_DATA:
		/* Construct only. */
		g_assert (self->sort_user_data == NULL);
		self->sort_user_data = g_value_get_pointer (value);
		break;
	case PROP_SORT_USER_DATA_NOTIFY:
		/* Construct only. */
		g_assert (self->sort_user_data_notify == NULL);
		self->sort_user_data_notify = g_value_get_pointer (value);
		break;
	case PROP_FILTER_FUNC:
		/* Construct only. */
		g_assert (self->filter_func == NULL);
		self->filter_func = g_value_get_pointer (value);
		break;
	case PROP_FILTER_USER_DATA:
		/* Construct only. */
		g_assert (self->filter_user_data == NULL);
		self->filter_user_data = g_value_get_pointer (value);
		break;
	case PROP_FILTER_USER_DATA_NOTIFY:
		/* Construct only. */
		g_assert (self->filter_user_data_notify == NULL);
		self->filter_user_data_notify = g_value_get_pointer (value);
		break;
	case PROP_PROVIDES_FILES:
		/* Construct only. */
		g_assert (self->provides_files == NULL);
		self->provides_files = g_value_dup_boxed (value);

		/* Squash empty arrays to %NULL. */
		if (self->provides_files != NULL && self->provides_files[0] == NULL)
			g_clear_pointer (&self->provides_files, g_strfreev);

		break;
	case PROP_RELEASED_SINCE:
		/* Construct only. */
		g_assert (self->released_since == NULL);
		self->released_since = g_value_dup_boxed (value);
		break;
	case PROP_IS_CURATED:
		/* Construct only. */
		g_assert (self->is_curated == GS_APP_QUERY_TRISTATE_UNSET);
		self->is_curated = g_value_get_enum (value);
		break;
	case PROP_CATEGORY:
		/* Construct only. */
		g_assert (self->category == NULL);
		self->category = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_query_dispose (GObject *object)
{
	GsAppQuery *self = GS_APP_QUERY (object);

	if (self->sort_user_data_notify != NULL && self->sort_user_data != NULL) {
		self->sort_user_data_notify (g_steal_pointer (&self->sort_user_data));
		self->sort_user_data_notify = NULL;
	}

	if (self->filter_user_data_notify != NULL && self->filter_user_data != NULL) {
		self->filter_user_data_notify (g_steal_pointer (&self->filter_user_data));
		self->filter_user_data_notify = NULL;
	}

	g_clear_object (&self->category);

	G_OBJECT_CLASS (gs_app_query_parent_class)->dispose (object);
}

static void
gs_app_query_finalize (GObject *object)
{
	GsAppQuery *self = GS_APP_QUERY (object);

	g_clear_pointer (&self->provides_files, g_strfreev);
	g_clear_pointer (&self->released_since, g_date_time_unref);

	G_OBJECT_CLASS (gs_app_query_parent_class)->finalize (object);
}

static void
gs_app_query_class_init (GsAppQueryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gs_app_query_get_property;
	object_class->set_property = gs_app_query_set_property;
	object_class->dispose = gs_app_query_dispose;
	object_class->finalize = gs_app_query_finalize;

	/**
	 * GsAppQuery:refine-flags:
	 *
	 * Flags to specify how the returned apps must be refined, if at all.
	 *
	 * Since: 43
	 */
	props[PROP_REFINE_FLAGS] =
		g_param_spec_flags ("refine-flags", "Refine Flags",
				    "Flags to specify how the returned apps must be refined, if at all.",
				    GS_TYPE_PLUGIN_REFINE_FLAGS, GS_PLUGIN_REFINE_FLAGS_NONE,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				    G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsAppQuery:max-results:
	 *
	 * Maximum number of results to return, or 0 for no limit.
	 *
	 * Since: 43
	 */
	props[PROP_MAX_RESULTS] =
		g_param_spec_uint ("max-results", "Max Results",
				   "Maximum number of results to return, or 0 for no limit.",
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				   G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsAppQuery:dedupe-flags:
	 *
	 * Flags to specify how to deduplicate the returned apps, if at all.
	 *
	 * Since: 43
	 */
	props[PROP_DEDUPE_FLAGS] =
		g_param_spec_flags ("dedupe-flags", "Dedupe Flags",
				    "Flags to specify how to deduplicate the returned apps, if at all.",
				    GS_TYPE_APP_LIST_FILTER_FLAGS, GS_APP_LIST_FILTER_FLAG_NONE,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				    G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsAppQuery:sort-func: (nullable)
	 *
	 * A sort function to sort the returned apps.
	 *
	 * This must be of type #GsAppListSortFunc.
	 *
	 * Since: 43
	 */
	props[PROP_SORT_FUNC] =
		g_param_spec_pointer ("sort-func", "Sort Function",
				      "A sort function to sort the returned apps.",
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				      G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsAppQuery:sort-user-data: (nullable)
	 *
	 * User data to pass to #GsAppQuery:sort-func.
	 *
	 * Since: 43
	 */
	props[PROP_SORT_USER_DATA] =
		g_param_spec_pointer ("sort-user-data", "Sort User Data",
				      "User data to pass to #GsAppQuery:sort-func.",
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				      G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsAppQuery:sort-user-data-notify: (nullable)
	 *
	 * A function to free #GsAppQuery:sort-user-data once it is no longer
	 * needed.
	 *
	 * This must be of type #GDestroyNotify.
	 *
	 * This will be called exactly once between being set and when the
	 * #GsAppQuery is finalized.
	 *
	 * Since: 43
	 */
	props[PROP_SORT_USER_DATA_NOTIFY] =
		g_param_spec_pointer ("sort-user-data-notify", "Sort User Data Notify",
				      "A function to free #GsAppQuery:sort-user-data once it is no longer needed.",
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				      G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsAppQuery:filter-func: (nullable)
	 *
	 * A filter function to filter the returned apps.
	 *
	 * This must be of type #GsAppListFilterFunc.
	 *
	 * Since: 43
	 */
	props[PROP_FILTER_FUNC] =
		g_param_spec_pointer ("filter-func", "Filter Function",
				      "A filter function to filter the returned apps.",
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				      G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsAppQuery:filter-user-data: (nullable)
	 *
	 * User data to pass to #GsAppQuery:filter-func.
	 *
	 * Since: 43
	 */
	props[PROP_FILTER_USER_DATA] =
		g_param_spec_pointer ("filter-user-data", "Filter User Data",
				      "User data to pass to #GsAppQuery:filter-func.",
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				      G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsAppQuery:filter-user-data-notify: (nullable)
	 *
	 * A function to free #GsAppQuery:filter-user-data once it is no longer
	 * needed.
	 *
	 * This must be of type #GDestroyNotify.
	 *
	 * This will be called exactly once between being set and when the
	 * #GsAppQuery is finalized.
	 *
	 * Since: 43
	 */
	props[PROP_FILTER_USER_DATA_NOTIFY] =
		g_param_spec_pointer ("filter-user-data-notify", "Filter User Data Notify",
				      "A function to free #GsAppQuery:filter-user-data once it is no longer needed.",
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				      G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsAppQuery:provides-files: (nullable)
	 *
	 * A list of file paths which the apps must provide.
	 *
	 * Used to search for apps which provide specific files on the local
	 * file system.
	 *
	 * This may be %NULL to not filter on file paths. An empty array is
	 * considered equivalent to %NULL.
	 *
	 * Since: 43
	 */
	props[PROP_PROVIDES_FILES] =
		g_param_spec_boxed ("provides-files", "Provides Files",
				    "A list of file paths which the apps must provide.",
				    G_TYPE_STRV,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				    G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsAppQuery:released-since: (nullable)
	 *
	 * A date/time which apps must have been released since (exclusive).
	 *
	 * Used to search for apps which have been updated recently.
	 *
	 * This may be %NULL to not filter on release date.
	 *
	 * Since: 43
	 */
	props[PROP_RELEASED_SINCE] =
		g_param_spec_boxed ("released-since", "Released Since",
				    "A date/time which apps must have been released since (exclusive).",
				    G_TYPE_DATE_TIME,
				    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				    G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsAppQuery:is-curated:
	 *
	 * Whether apps must be curated (%GS_APP_QUERY_TRISTATE_TRUE), or not
	 * curated (%GS_APP_QUERY_TRISTATE_FALSE).
	 *
	 * If this is %GS_APP_QUERY_TRISTATE_UNSET, apps are not filtered by
	 * their curation state.
	 *
	 * ‘Curated’ apps have been reviewed and picked by an editor to be
	 * promoted to users in some way. They should be high quality and
	 * feature complete.
	 *
	 * Since: 43
	 */
	props[PROP_IS_CURATED] =
		g_param_spec_enum ("is-curated", "Is Curated",
				   "Whether apps must be curated, or not curated.",
				   GS_TYPE_APP_QUERY_TRISTATE,
				   GS_APP_QUERY_TRISTATE_UNSET,
				   G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				   G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsAppQuery:category: (nullable)
	 *
	 * A category which apps must be in.
	 *
	 * If this is %NULL, apps are not filtered by category.
	 *
	 * Since: 43
	 */
	props[PROP_CATEGORY] =
		g_param_spec_object ("category", "Category",
				     "A category which apps must be in.",
				     GS_TYPE_CATEGORY,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
				     G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (props), props);
}

static void
gs_app_query_init (GsAppQuery *self)
{
	self->is_curated = GS_APP_QUERY_TRISTATE_UNSET;
}

/**
 * gs_app_query_new:
 * @first_property_name: name of the first #GObject property
 * @...: value for the first property, followed by additional property/value
 *   pairs, then a terminating %NULL
 *
 * Create a new #GsAppQuery containing the given query properties.
 *
 * Returns: (transfer full): a new #GsAppQuery
 * Since: 43
 */
GsAppQuery *
gs_app_query_new (const gchar *first_property_name,
		  ...)
{
	va_list args;
	g_autoptr(GsAppQuery) query = NULL;

	va_start (args, first_property_name);
	query = GS_APP_QUERY (g_object_new_valist (GS_TYPE_APP_QUERY, first_property_name, args));
	va_end (args);

	return g_steal_pointer (&query);
}

/**
 * gs_app_query_get_refine_flags:
 * @self: a #GsAppQuery
 *
 * Get the value of #GsAppQuery:refine-flags.
 *
 * Returns: the refine flags for the query
 * Since: 43
 */
GsPluginRefineFlags
gs_app_query_get_refine_flags (GsAppQuery *self)
{
	g_return_val_if_fail (GS_IS_APP_QUERY (self), GS_PLUGIN_REFINE_FLAGS_NONE);

	return self->refine_flags;
}

/**
 * gs_app_query_get_max_results:
 * @self: a #GsAppQuery
 *
 * Get the value of #GsAppQuery:max-results.
 *
 * Returns: the maximum number of results to return for the query, or `0` to
 *   indicate no limit
 * Since: 43
 */
guint
gs_app_query_get_max_results (GsAppQuery *self)
{
	g_return_val_if_fail (GS_IS_APP_QUERY (self), 0);

	return self->max_results;
}

/**
 * gs_app_query_get_dedupe_flags:
 * @self: a #GsAppQuery
 *
 * Get the value of #GsAppQuery:dedupe-flags.
 *
 * Returns: the dedupe flags for the query
 * Since: 43
 */
GsAppListFilterFlags
gs_app_query_get_dedupe_flags (GsAppQuery *self)
{
	g_return_val_if_fail (GS_IS_APP_QUERY (self), GS_APP_LIST_FILTER_FLAG_NONE);

	return self->dedupe_flags;
}

/**
 * gs_app_query_get_sort_func:
 * @self: a #GsAppQuery
 * @user_data_out: (out) (transfer none) (optional) (nullable): return location
 *   for the #GsAppQuery:sort-user-data, or %NULL to ignore
 *
 * Get the value of #GsAppQuery:sort-func.
 *
 * Returns: (nullable): the sort function for the query
 * Since: 43
 */
GsAppListSortFunc
gs_app_query_get_sort_func (GsAppQuery *self,
                            gpointer   *user_data_out)
{
	g_return_val_if_fail (GS_IS_APP_QUERY (self), NULL);

	if (user_data_out != NULL)
		*user_data_out = self->sort_user_data;

	return self->sort_func;
}

/**
 * gs_app_query_get_filter_func:
 * @self: a #GsAppQuery
 * @user_data_out: (out) (transfer none) (optional) (nullable): return location
 *   for the #GsAppQuery:filter-user-data, or %NULL to ignore
 *
 * Get the value of #GsAppQuery:filter-func.
 *
 * Returns: (nullable): the filter function for the query
 * Since: 43
 */
GsAppListFilterFunc
gs_app_query_get_filter_func (GsAppQuery *self,
                              gpointer   *user_data_out)
{
	g_return_val_if_fail (GS_IS_APP_QUERY (self), NULL);

	if (user_data_out != NULL)
		*user_data_out = self->filter_user_data;

	return self->filter_func;
}

/**
 * gs_app_query_get_n_properties_set:
 * @self: a #GsAppQuery
 *
 * Get the number of query properties which have been set.
 *
 * These are the properties which determine the query results, rather than ones
 * which control refining the results (#GsAppQuery:refine-flags,
 * #GsAppQuery:max-results, #GsAppQuery:dedupe-flags, #GsAppQuery:sort-func and
 * its user data, #GsAppQuery:filter-func and its user data).
 *
 * Returns: number of properties set so they will affect query results
 * Since: 43
 */
guint
gs_app_query_get_n_properties_set (GsAppQuery *self)
{
	guint n = 0;

	g_return_val_if_fail (GS_IS_APP_QUERY (self), 0);

	if (self->provides_files != NULL)
		n++;
	if (self->released_since != NULL)
		n++;
	if (self->is_curated != GS_APP_QUERY_TRISTATE_UNSET)
		n++;
	if (self->category != NULL)
		n++;

	return n;
}

/**
 * gs_app_query_get_provides_files:
 * @self: a #GsAppQuery
 *
 * Get the value of #GsAppQuery:provides-files.
 *
 * Returns: (nullable): a list of file paths which the apps must provide,
 *   or %NULL to not filter on file paths
 * Since: 43
 */
const gchar * const *
gs_app_query_get_provides_files (GsAppQuery *self)
{
	g_return_val_if_fail (GS_IS_APP_QUERY (self), NULL);

	/* Always return %NULL or a non-empty array */
	g_assert (self->provides_files == NULL || self->provides_files[0] != NULL);

	return (const gchar * const *) self->provides_files;
}

/**
 * gs_app_query_get_released_since:
 * @self: a #GsAppQuery
 *
 * Get the value of #GsAppQuery:released-since.
 *
 * Returns: (nullable): a date/time which apps must have been released since,
 *   or %NULL to not filter on release date
 * Since: 43
 */
GDateTime *
gs_app_query_get_released_since (GsAppQuery *self)
{
	g_return_val_if_fail (GS_IS_APP_QUERY (self), NULL);

	return self->released_since;
}

/**
 * gs_app_query_get_is_curated:
 * @self: a #GsAppQuery
 *
 * Get the value of #GsAppQuery:is-curated.
 *
 * Returns: %GS_APP_QUERY_TRISTATE_TRUE if apps must be curated,
 *   %GS_APP_QUERY_TRISTATE_FALSE if they must be not curated, or
 *   %GS_APP_QUERY_TRISTATE_UNSET if it doesn’t matter
 * Since: 43
 */
GsAppQueryTristate
gs_app_query_get_is_curated (GsAppQuery *self)
{
	g_return_val_if_fail (GS_IS_APP_QUERY (self), GS_APP_QUERY_TRISTATE_FALSE);

	return self->is_curated;
}

/**
 * gs_app_query_get_category:
 * @self: a #GsAppQuery
 *
 * Get the value of #GsAppQuery:category.
 *
 * Returns: (nullable) (transfer none): a category which apps must be part of,
 *   or %NULL to not filter on category
 * Since: 43
 */
GsCategory *
gs_app_query_get_category (GsAppQuery *self)
{
	g_return_val_if_fail (GS_IS_APP_QUERY (self), NULL);

	return self->category;
}
