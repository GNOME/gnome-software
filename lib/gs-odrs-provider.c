/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016-2018 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/*
 * SECTION:gs-odrs-provider
 * @short_description: Provides review data from the Open Desktop Ratings Service.
 *
 * To test this plugin locally you will probably want to build and run the
 * `odrs-web` container, following the instructions in the
 * [`odrs-web` repository](https://gitlab.gnome.org/Infrastructure/odrs-web/-/blob/master/README.md),
 * and then get gnome-software to use your local review server by running:
 * ```
 * gsettings set org.gnome.software review-server 'http://127.0.0.1:5000/1.0/reviews/api'
 * ```
 *
 * When you are done with development, run the following command to use the real
 * ODRS server again:
 * ```
 * gsettings reset org.gnome.software review-server
 * ```
 *
 * Since: 41
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gnome-software.h>
#include <json-glib/json-glib.h>
#include <libsoup/soup.h>
#include <locale.h>
#include <math.h>
#include <string.h>

#if !GLIB_CHECK_VERSION(2, 62, 0)
typedef struct
{
  guint8 *data;
  guint   len;
  guint   alloc;
  guint   elt_size;
  guint   zero_terminated : 1;
  guint   clear : 1;
  gatomicrefcount ref_count;
  GDestroyNotify clear_func;
} GRealArray;

static gboolean
g_array_binary_search (GArray        *array,
                       gconstpointer  target,
                       GCompareFunc   compare_func,
                       guint         *out_match_index)
{
  gboolean result = FALSE;
  GRealArray *_array = (GRealArray *) array;
  guint left, middle, right;
  gint val;

  g_return_val_if_fail (_array != NULL, FALSE);
  g_return_val_if_fail (compare_func != NULL, FALSE);

  if (G_LIKELY(_array->len))
    {
      left = 0;
      right = _array->len - 1;

      while (left <= right)
        {
          middle = left + (right - left) / 2;

          val = compare_func (_array->data + (_array->elt_size * middle), target);
          if (val == 0)
            {
              result = TRUE;
              break;
            }
          else if (val < 0)
            left = middle + 1;
          else if (/* val > 0 && */ middle > 0)
            right = middle - 1;
          else
            break;  /* element not found */
        }
    }

  if (result && out_match_index != NULL)
    *out_match_index = middle;

  return result;
}
#endif  /* glib < 2.62.0 */

/* Element in self->ratings, all allocated in one big block and sorted
 * alphabetically to reduce the number of allocations and fragmentation. */
typedef struct {
	gchar *app_id;  /* (owned) */
	guint32 n_star_ratings[6];
} GsOdrsRating;

static int
rating_compare (const GsOdrsRating *a, const GsOdrsRating *b)
{
	return g_strcmp0 (a->app_id, b->app_id);
}

static void
rating_clear (GsOdrsRating *rating)
{
	g_free (rating->app_id);
}

struct _GsOdrsProvider
{
	GObject		 parent_instance;

	gchar		*distro;  /* (not nullable) (owned) */
	gchar		*user_hash;  /* (not nullable) (owned) */
	gchar		*review_server;  /* (not nullable) (owned) */
	GArray		*ratings;  /* (element-type GsOdrsRating) (mutex ratings_mutex) (owned) (nullable) */
	GMutex		 ratings_mutex;
	GsApp		*cached_origin;
	guint64		 max_cache_age_secs;
	guint		 n_results_max;
};

G_DEFINE_TYPE (GsOdrsProvider, gs_odrs_provider, G_TYPE_OBJECT)

typedef enum {
	PROP_REVIEW_SERVER = 1,
	PROP_USER_HASH,
	PROP_DISTRO,
	PROP_MAX_CACHE_AGE_SECS,
	PROP_N_RESULTS_MAX,
} GsOdrsProviderProperty;

static GParamSpec *obj_props[PROP_N_RESULTS_MAX + 1] = { NULL, };

static gboolean
gs_odrs_provider_load_ratings_for_app (JsonObject   *json_app,
                                       const gchar  *app_id,
                                       GsOdrsRating *rating_out)
{
	guint i;
	const gchar *names[] = { "star0", "star1", "star2", "star3",
				 "star4", "star5", NULL };

	for (i = 0; names[i] != NULL; i++) {
		if (!json_object_has_member (json_app, names[i]))
			return FALSE;
		rating_out->n_star_ratings[i] = (guint64) json_object_get_int_member (json_app, names[i]);
	}

	rating_out->app_id = g_strdup (app_id);

	return TRUE;
}

static gboolean
gs_odrs_provider_load_ratings (GsOdrsProvider  *self,
                               const gchar     *filename,
                               GError         **error)
{
	JsonNode *json_root;
	JsonObject *json_item;
	g_autoptr(JsonParser) json_parser = NULL;
	const gchar *app_id;
	JsonNode *json_app_node;
	JsonObjectIter iter;
	g_autoptr(GArray) new_ratings = NULL;
	g_autoptr(GMutexLocker) locker = NULL;

	/* parse the data and find the success */
	json_parser = json_parser_new_immutable ();
#if JSON_CHECK_VERSION(1, 6, 0)
	if (!json_parser_load_from_mapped_file (json_parser, filename, error)) {
#else
	if (!json_parser_load_from_file (json_parser, filename, error)) {
#endif
		gs_utils_error_convert_json_glib (error);
		return FALSE;
	}
	json_root = json_parser_get_root (json_parser);
	if (json_root == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "no ratings root");
		return FALSE;
	}
	if (json_node_get_node_type (json_root) != JSON_NODE_OBJECT) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "no ratings array");
		return FALSE;
	}

	json_item = json_node_get_object (json_root);

	new_ratings = g_array_sized_new (FALSE,  /* don’t zero-terminate */
					 FALSE,  /* don’t clear */
					 sizeof (GsOdrsRating),
					 json_object_get_size (json_item));
	g_array_set_clear_func (new_ratings, (GDestroyNotify) rating_clear);

	/* parse each app */
	json_object_iter_init (&iter, json_item);
	while (json_object_iter_next (&iter, &app_id, &json_app_node)) {
		GsOdrsRating rating;
		JsonObject *json_app;

		if (!JSON_NODE_HOLDS_OBJECT (json_app_node))
			continue;
		json_app = json_node_get_object (json_app_node);

		if (gs_odrs_provider_load_ratings_for_app (json_app, app_id, &rating))
			g_array_append_val (new_ratings, rating);
	}

	/* Allow for binary searches later. */
	g_array_sort (new_ratings, (GCompareFunc) rating_compare);

	/* Update the shared state */
	locker = g_mutex_locker_new (&self->ratings_mutex);
	g_clear_pointer (&self->ratings, g_array_unref);
	self->ratings = g_steal_pointer (&new_ratings);

	return TRUE;
}

static AsReview *
gs_odrs_provider_parse_review_object (JsonObject *item)
{
	AsReview *rev = as_review_new ();

	/* date */
	if (json_object_has_member (item, "date_created")) {
		gint64 timestamp;
		g_autoptr(GDateTime) dt = NULL;
		timestamp = json_object_get_int_member (item, "date_created");
		dt = g_date_time_new_from_unix_utc (timestamp);
		as_review_set_date (rev, dt);
	}

	/* assemble review */
	if (json_object_has_member (item, "rating"))
		as_review_set_rating (rev, (gint) json_object_get_int_member (item, "rating"));
	if (json_object_has_member (item, "score")) {
		as_review_set_priority (rev, (gint) json_object_get_int_member (item, "score"));
	} else if (json_object_has_member (item, "karma_up") &&
		   json_object_has_member (item, "karma_down")) {
		gdouble ku = (gdouble) json_object_get_int_member (item, "karma_up");
		gdouble kd = (gdouble) json_object_get_int_member (item, "karma_down");
		gdouble wilson = 0.f;

		/* from http://www.evanmiller.org/how-not-to-sort-by-average-rating.html */
		if (ku > 0 || kd > 0) {
			wilson = ((ku + 1.9208) / (ku + kd) -
				  1.96 * sqrt ((ku * kd) / (ku + kd) + 0.9604) /
				  (ku + kd)) / (1 + 3.8416 / (ku + kd));
			wilson *= 100.f;
		}
		as_review_set_priority (rev, (gint) wilson);
	}
	if (json_object_has_member (item, "user_hash"))
		as_review_set_reviewer_id (rev, json_object_get_string_member (item, "user_hash"));
	if (json_object_has_member (item, "user_display"))
		as_review_set_reviewer_name (rev, json_object_get_string_member (item, "user_display"));
	if (json_object_has_member (item, "summary"))
		as_review_set_summary (rev, json_object_get_string_member (item, "summary"));
	if (json_object_has_member (item, "description"))
		as_review_set_description (rev, json_object_get_string_member (item, "description"));
	if (json_object_has_member (item, "version"))
		as_review_set_version (rev, json_object_get_string_member (item, "version"));

	/* add extra metadata for the plugin */
	if (json_object_has_member (item, "user_skey")) {
		as_review_add_metadata (rev, "user_skey",
					json_object_get_string_member (item, "user_skey"));
	}
	if (json_object_has_member (item, "app_id")) {
		as_review_add_metadata (rev, "app_id",
					json_object_get_string_member (item, "app_id"));
	}
	if (json_object_has_member (item, "review_id")) {
		g_autofree gchar *review_id = NULL;
		review_id = g_strdup_printf ("%" G_GINT64_FORMAT,
					json_object_get_int_member (item, "review_id"));
		as_review_set_id (rev, review_id);
	}

	/* don't allow multiple votes */
	if (json_object_has_member (item, "vote_id"))
		as_review_add_flags (rev, AS_REVIEW_FLAG_VOTED);

	return rev;
}

static GPtrArray *
gs_odrs_provider_parse_reviews (GsOdrsProvider  *self,
                                GsPlugin        *plugin,
                                const gchar     *data,
                                gssize           data_len,
                                GError         **error)
{
	JsonArray *json_reviews;
	JsonNode *json_root;
	guint i;
	g_autoptr(JsonParser) json_parser = NULL;
	g_autoptr(GHashTable) reviewer_ids = NULL;
	g_autoptr(GPtrArray) reviews = NULL;

	/* nothing */
	if (data == NULL) {
		if (!gs_plugin_get_network_available (plugin))
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NO_NETWORK,
					     "server couldn't be reached");
		else
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_INVALID_FORMAT,
					     "server returned no data");
		return NULL;
	}

	/* parse the data and find the array or ratings */
	json_parser = json_parser_new_immutable ();
	if (!json_parser_load_from_data (json_parser, data, data_len, error)) {
		gs_utils_error_convert_json_glib (error);
		return NULL;
	}
	json_root = json_parser_get_root (json_parser);
	if (json_root == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "no root");
		return NULL;
	}
	if (json_node_get_node_type (json_root) != JSON_NODE_ARRAY) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "no array");
		return NULL;
	}

	/* parse each rating */
	reviews = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	json_reviews = json_node_get_array (json_root);
	reviewer_ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (i = 0; i < json_array_get_length (json_reviews); i++) {
		JsonNode *json_review;
		JsonObject *json_item;
		const gchar *reviewer_id;
		g_autoptr(AsReview) review = NULL;

		/* extract the data */
		json_review = json_array_get_element (json_reviews, i);
		if (json_node_get_node_type (json_review) != JSON_NODE_OBJECT) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_INVALID_FORMAT,
					     "no object type");
			return NULL;
		}
		json_item = json_node_get_object (json_review);
		if (json_item == NULL) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_INVALID_FORMAT,
					     "no object");
			return NULL;
		}

		/* create review */
		review = gs_odrs_provider_parse_review_object (json_item);

		reviewer_id = as_review_get_reviewer_id (review);
		if (reviewer_id == NULL)
			continue;

		/* dedupe each on the user_hash */
		if (g_hash_table_lookup (reviewer_ids, reviewer_id) != NULL) {
			g_debug ("duplicate review %s, skipping", reviewer_id);
			continue;
		}
		g_hash_table_add (reviewer_ids, g_strdup (reviewer_id));
		g_ptr_array_add (reviews, g_object_ref (review));
	}
	return g_steal_pointer (&reviews);
}

static gboolean
gs_odrs_provider_parse_success (GsPlugin *plugin, const gchar *data, gssize data_len, GError **error)
{
	JsonNode *json_root;
	JsonObject *json_item;
	const gchar *msg = NULL;
	g_autoptr(JsonParser) json_parser = NULL;

	/* nothing */
	if (data == NULL) {
		if (!gs_plugin_get_network_available (plugin))
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NO_NETWORK,
					     "server couldn't be reached");
		else
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_INVALID_FORMAT,
					     "server returned no data");
		return FALSE;
	}

	/* parse the data and find the success */
	json_parser = json_parser_new_immutable ();
	if (!json_parser_load_from_data (json_parser, data, data_len, error)) {
		gs_utils_error_convert_json_glib (error);
		return FALSE;
	}
	json_root = json_parser_get_root (json_parser);
	if (json_root == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "no error root");
		return FALSE;
	}
	if (json_node_get_node_type (json_root) != JSON_NODE_OBJECT) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "no error object");
		return FALSE;
	}
	json_item = json_node_get_object (json_root);
	if (json_item == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     "no error object");
		return FALSE;
	}

	/* failed? */
	if (json_object_has_member (json_item, "msg"))
		msg = json_object_get_string_member (json_item, "msg");
	if (!json_object_get_boolean_member (json_item, "success")) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_INVALID_FORMAT,
				     msg != NULL ? msg : "unknown failure");
		return FALSE;
	}

	/* just for the console */
	if (msg != NULL)
		g_debug ("success: %s", msg);
	return TRUE;
}

static gboolean
gs_odrs_provider_json_post (GsPlugin     *plugin,
                            SoupSession  *session,
                            const gchar  *uri,
                            const gchar  *data,
                            GError      **error)
{
	guint status_code;
	g_autoptr(SoupMessage) msg = NULL;

	/* create the GET data */
	g_debug ("Sending ODRS request to %s: %s", uri, data);
	msg = soup_message_new (SOUP_METHOD_POST, uri);
	soup_message_set_request (msg, "application/json; charset=utf-8",
				  SOUP_MEMORY_COPY, data, strlen (data));

	/* set sync request */
	status_code = soup_session_send_message (session, msg);
	g_debug ("ODRS server returned status %u: %s", status_code, msg->response_body->data);
	if (status_code != SOUP_STATUS_OK) {
		g_warning ("Failed to set rating on ODRS: %s",
			   soup_status_get_phrase (status_code));
		g_set_error (error,
                             GS_PLUGIN_ERROR,
                             GS_PLUGIN_ERROR_FAILED,
                             "Failed to submit review to ODRS: %s", soup_status_get_phrase (status_code));
		return FALSE;
	}

	/* process returned JSON */
	return gs_odrs_provider_parse_success (plugin,
					       msg->response_body->data,
					       msg->response_body->length,
					       error);
}

static GPtrArray *
_gs_app_get_reviewable_ids (GsApp *app)
{
	GPtrArray *ids = g_ptr_array_new_with_free_func (g_free);
	GPtrArray *provided = gs_app_get_provided (app);

	/* add the main component id */
	g_ptr_array_add (ids, g_strdup (gs_app_get_id (app)));

	/* add any ID provides */
	for (guint i = 0; i < provided->len; i++) {
		GPtrArray *items;
		AsProvided *prov = g_ptr_array_index (provided, i);
		if (as_provided_get_kind (prov) != AS_PROVIDED_KIND_ID)
			continue;

		items = as_provided_get_items (prov);
		for (guint j = 0; j < items->len; j++) {
			const gchar *value = (const gchar *) g_ptr_array_index (items, j);
			if (value == NULL)
				continue;
			g_ptr_array_add (ids, g_strdup (value));
		}
	}
	return ids;
}

static gboolean
gs_odrs_provider_refine_ratings (GsOdrsProvider  *self,
                                 GsApp           *app,
                                 GCancellable    *cancellable,
                                 GError         **error)
{
	gint rating;
	guint32 ratings_raw[6] = { 0, 0, 0, 0, 0, 0 };
	guint cnt = 0;
	g_autoptr(GArray) review_ratings = NULL;
	g_autoptr(GPtrArray) reviewable_ids = NULL;
	g_autoptr(GMutexLocker) locker = NULL;

	/* get ratings for each reviewable ID */
	reviewable_ids = _gs_app_get_reviewable_ids (app);

	locker = g_mutex_locker_new (&self->ratings_mutex);

	if (!self->ratings) {
		g_autofree gchar *cache_filename = NULL;

		g_clear_pointer (&locker, g_mutex_locker_free);

		/* Load from the local cache, if available, when in offline or
		   when refresh/download disabled on start */
		cache_filename = gs_utils_get_cache_filename ("odrs",
							      "ratings.json",
							      GS_UTILS_CACHE_FLAG_WRITEABLE |
							      GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
							      error);

		if (!cache_filename ||
		    !gs_odrs_provider_load_ratings (self, cache_filename, NULL))
			return TRUE;

		locker = g_mutex_locker_new (&self->ratings_mutex);

		if (!self->ratings)
			return TRUE;
	}

	for (guint i = 0; i < reviewable_ids->len; i++) {
		const gchar *id = g_ptr_array_index (reviewable_ids, i);
		const GsOdrsRating search_rating = { (gchar *) id, { 0, }};
		guint found_index;
		const GsOdrsRating *found_rating;

		if (!g_array_binary_search (self->ratings, &search_rating,
					    (GCompareFunc) rating_compare, &found_index))
			continue;

		found_rating = &g_array_index (self->ratings, GsOdrsRating, found_index);

		/* copy into accumulator array */
		for (guint j = 0; j < 6; j++)
			ratings_raw[j] += found_rating->n_star_ratings[j];
		cnt++;
	}
	if (cnt == 0)
		return TRUE;

	/* Done with self->ratings now */
	g_clear_pointer (&locker, g_mutex_locker_free);

	/* merge to accumulator array back to one GArray blob */
	review_ratings = g_array_sized_new (FALSE, TRUE, sizeof(guint32), 6);
	for (guint i = 0; i < 6; i++)
		g_array_append_val (review_ratings, ratings_raw[i]);
	gs_app_set_review_ratings (app, review_ratings);

	/* find the wilson rating */
	rating = gs_utils_get_wilson_rating (g_array_index (review_ratings, guint32, 1),
					     g_array_index (review_ratings, guint32, 2),
					     g_array_index (review_ratings, guint32, 3),
					     g_array_index (review_ratings, guint32, 4),
					     g_array_index (review_ratings, guint32, 5));
	if (rating > 0)
		gs_app_set_rating (app, rating);
	return TRUE;
}

static JsonNode *
gs_odrs_provider_get_compat_ids (GsApp *app)
{
	GPtrArray *provided = gs_app_get_provided (app);
	g_autoptr(GHashTable) ids = NULL;
	g_autoptr(JsonArray) json_array = json_array_new ();
	g_autoptr(JsonNode) json_node = json_node_new (JSON_NODE_ARRAY);

	ids = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (guint i = 0; i < provided->len; i++) {
		GPtrArray *items;
		AsProvided *prov = g_ptr_array_index (provided, i);

		if (as_provided_get_kind (prov) != AS_PROVIDED_KIND_ID)
			continue;

		items = as_provided_get_items (prov);
		for (guint j = 0; j < items->len; j++) {
			const gchar *value = g_ptr_array_index (items, j);
			if (value == NULL)
				continue;

			if (g_hash_table_lookup (ids, value) != NULL)
				continue;
			g_hash_table_add (ids, g_strdup (value));
			json_array_add_string_element (json_array, value);
		}
	}
	if (json_array_get_length (json_array) == 0)
		return NULL;
	json_node_set_array (json_node, json_array);
	return g_steal_pointer (&json_node);
}

static GPtrArray *
gs_odrs_provider_fetch_for_app (GsOdrsProvider  *self,
                                GsPlugin        *plugin,
                                GsApp           *app,
                                GError         **error)
{
	JsonNode *json_compat_ids;
	const gchar *version;
	guint status_code;
	g_autofree gchar *cachefn_basename = NULL;
	g_autofree gchar *cachefn = NULL;
	g_autofree gchar *data = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(GFile) cachefn_file = NULL;
	g_autoptr(GPtrArray) reviews = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;
	g_autoptr(SoupMessage) msg = NULL;

	/* look in the cache */
	cachefn_basename = g_strdup_printf ("%s.json", gs_app_get_id (app));
	cachefn = gs_utils_get_cache_filename ("odrs",
					       cachefn_basename,
					       GS_UTILS_CACHE_FLAG_WRITEABLE |
					       GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
					       error);
	if (cachefn == NULL)
		return NULL;
	cachefn_file = g_file_new_for_path (cachefn);
	if (gs_utils_get_file_age (cachefn_file) < self->max_cache_age_secs) {
		g_autoptr(GMappedFile) mapped_file = NULL;

		mapped_file = g_mapped_file_new (cachefn, FALSE, error);
		if (mapped_file == NULL)
			return NULL;

		g_debug ("got review data for %s from %s",
			 gs_app_get_id (app), cachefn);
		return gs_odrs_provider_parse_reviews (self,
						       plugin,
						       g_mapped_file_get_contents (mapped_file),
						       g_mapped_file_get_length (mapped_file),
						       error);
	}

	/* not always available */
	version = gs_app_get_version (app);
	if (version == NULL)
		version = "unknown";

	/* create object with review data */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "user_hash");
	json_builder_add_string_value (builder, self->user_hash);
	json_builder_set_member_name (builder, "app_id");
	json_builder_add_string_value (builder, gs_app_get_id (app));
	json_builder_set_member_name (builder, "locale");
	json_builder_add_string_value (builder, setlocale (LC_MESSAGES, NULL));
	json_builder_set_member_name (builder, "distro");
	json_builder_add_string_value (builder, self->distro);
	json_builder_set_member_name (builder, "version");
	json_builder_add_string_value (builder, version);
	json_builder_set_member_name (builder, "limit");
	json_builder_add_int_value (builder, self->n_results_max);
	json_compat_ids = gs_odrs_provider_get_compat_ids (app);
	if (json_compat_ids != NULL) {
		json_builder_set_member_name (builder, "compat_ids");
		json_builder_add_value (builder, json_compat_ids);
	}
	json_builder_end_object (builder);

	/* export as a string */
	json_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	data = json_generator_to_data (json_generator, NULL);
	if (data == NULL)
		return NULL;
	uri = g_strdup_printf ("%s/fetch", self->review_server);
	g_debug ("Updating ODRS cache for %s from %s to %s; request %s", gs_app_get_id (app),
		 uri, cachefn, data);
	msg = soup_message_new (SOUP_METHOD_POST, uri);
	soup_message_set_request (msg, "application/json; charset=utf-8",
				  SOUP_MEMORY_COPY, data, strlen (data));
	status_code = soup_session_send_message (gs_plugin_get_soup_session (plugin), msg);
	if (status_code != SOUP_STATUS_OK) {
		if (!gs_odrs_provider_parse_success (plugin,
						     msg->response_body->data,
						     msg->response_body->length,
						     error))
			return NULL;
		/* not sure what to do here */
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
				     "status code invalid");
		gs_utils_error_add_origin_id (error, self->cached_origin);
		return NULL;
	}
	reviews = gs_odrs_provider_parse_reviews (self,
						  plugin,
						  msg->response_body->data,
						  msg->response_body->length,
						  error);
	if (reviews == NULL)
		return NULL;

	/* save to the cache */
	if (!g_file_set_contents (cachefn,
				  msg->response_body->data,
				  msg->response_body->length,
				  error))
		return NULL;

	/* success */
	return g_steal_pointer (&reviews);
}

static gboolean
gs_odrs_provider_refine_reviews (GsOdrsProvider  *self,
                                 GsPlugin        *plugin,
                                 GsApp           *app,
                                 GCancellable    *cancellable,
                                 GError         **error)
{
	AsReview *review;
	g_autoptr(GPtrArray) reviews = NULL;

	/* get from server */
	reviews = gs_odrs_provider_fetch_for_app (self, plugin, app, error);
	if (reviews == NULL)
		return FALSE;
	for (guint i = 0; i < reviews->len; i++) {
		review = g_ptr_array_index (reviews, i);

		/* save this on the application object so we can use it for
		 * submitting a new review */
		if (i == 0) {
			gs_app_set_metadata (app, "ODRS::user_skey",
					     as_review_get_metadata_item (review, "user_skey"));
		}

		/* ignore invalid reviews */
		if (as_review_get_rating (review) == 0)
			continue;

		/* the user_hash matches, so mark this as our own review */
		if (g_strcmp0 (as_review_get_reviewer_id (review),
			       self->user_hash) == 0) {
			as_review_set_flags (review, AS_REVIEW_FLAG_SELF);
		}
		gs_app_add_review (app, review);
	}
	return TRUE;
}

static gboolean
refine_app (GsOdrsProvider       *self,
            GsPlugin             *plugin,
            GsApp                *app,
            GsPluginRefineFlags   flags,
            GCancellable         *cancellable,
            GError              **error)
{
	/* not valid */
	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_ADDON)
		return TRUE;
	if (gs_app_get_id (app) == NULL)
		return TRUE;

	/* add reviews if possible */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS) {
		if (gs_app_get_reviews(app)->len > 0)
			return TRUE;
		if (!gs_odrs_provider_refine_reviews (self, plugin, app,
						      cancellable, error))
			return FALSE;
	}

	/* add ratings if possible */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS ||
	    flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING) {
		if (gs_app_get_review_ratings (app) != NULL)
			return TRUE;
		if (!gs_odrs_provider_refine_ratings (self, app, cancellable, error))
			return FALSE;
	}

	return TRUE;
}

static gchar *
gs_odrs_provider_trim_version (const gchar *version)
{
	gchar *str;
	gchar *tmp;

	/* nothing set */
	if (version == NULL)
		return g_strdup ("unknown");

	/* remove epoch */
	str = g_strrstr (version, ":");
	if (str != NULL)
		version = str + 1;

	/* remove release */
	tmp = g_strdup (version);
	g_strdelimit (tmp, "-", '\0');

	/* remove '+dfsg' suffix */
	str = g_strstr_len (tmp, -1, "+dfsg");
	if (str != NULL)
		*str = '\0';

	return tmp;
}

static gboolean
gs_odrs_provider_invalidate_cache (AsReview *review, GError **error)
{
	g_autofree gchar *cachefn_basename = NULL;
	g_autofree gchar *cachefn = NULL;
	g_autoptr(GFile) cachefn_file = NULL;

	/* look in the cache */
	cachefn_basename = g_strdup_printf ("%s.json",
					    as_review_get_metadata_item (review, "app_id"));
	cachefn = gs_utils_get_cache_filename ("odrs",
					       cachefn_basename,
					       GS_UTILS_CACHE_FLAG_WRITEABLE |
					       GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
					       error);
	if (cachefn == NULL)
		return FALSE;
	cachefn_file = g_file_new_for_path (cachefn);
	if (!g_file_query_exists (cachefn_file, NULL))
		return TRUE;
	return g_file_delete (cachefn_file, NULL, error);
}

static gboolean
gs_odrs_provider_vote (GsOdrsProvider  *self,
                       GsPlugin        *plugin,
                       AsReview        *review,
                       const gchar     *uri,
                       GError         **error)
{
	const gchar *tmp;
	g_autofree gchar *data = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	/* create object with vote data */
	builder = json_builder_new ();
	json_builder_begin_object (builder);

	json_builder_set_member_name (builder, "user_hash");
	json_builder_add_string_value (builder, self->user_hash);
	json_builder_set_member_name (builder, "user_skey");
	json_builder_add_string_value (builder,
				       as_review_get_metadata_item (review, "user_skey"));
	json_builder_set_member_name (builder, "app_id");
	json_builder_add_string_value (builder,
				       as_review_get_metadata_item (review, "app_id"));
	tmp = as_review_get_id (review);
	if (tmp != NULL) {
		gint64 review_id;
		json_builder_set_member_name (builder, "review_id");
		review_id = g_ascii_strtoll (tmp, NULL, 10);
		json_builder_add_int_value (builder, review_id);
	}
	json_builder_end_object (builder);

	/* export as a string */
	json_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	data = json_generator_to_data (json_generator, NULL);
	if (data == NULL)
		return FALSE;

	/* clear cache */
	if (!gs_odrs_provider_invalidate_cache (review, error))
		return FALSE;

	/* send to server */
	if (!gs_odrs_provider_json_post (plugin, gs_plugin_get_soup_session (plugin),
					 uri, data, error))
		return FALSE;

	/* mark as voted */
	as_review_add_flags (review, AS_REVIEW_FLAG_VOTED);

	/* success */
	return TRUE;
}

static GsApp *
gs_odrs_provider_create_app_dummy (const gchar *id)
{
	GsApp *app = gs_app_new (id);
	g_autoptr(GString) str = NULL;
	str = g_string_new (id);
	as_gstring_replace (str, ".desktop", "");
	g_string_prepend (str, "No description is available for ");
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, "Unknown Application");
	gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, "Application not found");
	gs_app_set_description (app, GS_APP_QUALITY_LOWEST, str->str);
	return app;
}

static void
gs_odrs_provider_init (GsOdrsProvider *self)
{
	g_mutex_init (&self->ratings_mutex);

	/* add source */
	self->cached_origin = gs_app_new ("odrs");
	gs_app_set_kind (self->cached_origin, AS_COMPONENT_KIND_REPOSITORY);
	gs_app_set_origin_hostname (self->cached_origin, self->review_server);
}

static void
gs_odrs_provider_constructed (GObject *object)
{
	GsOdrsProvider *self = GS_ODRS_PROVIDER (object);

	G_OBJECT_CLASS (gs_odrs_provider_parent_class)->constructed (object);

	/* Check all required properties have been set. */
	g_assert (self->review_server != NULL);
	g_assert (self->user_hash != NULL);
	g_assert (self->distro != NULL);
}

static void
gs_odrs_provider_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
	GsOdrsProvider *self = GS_ODRS_PROVIDER (object);

	switch ((GsOdrsProviderProperty) prop_id) {
	case PROP_REVIEW_SERVER:
		g_value_set_string (value, self->review_server);
		break;
	case PROP_USER_HASH:
		g_value_set_string (value, self->user_hash);
		break;
	case PROP_DISTRO:
		g_value_set_string (value, self->distro);
		break;
	case PROP_MAX_CACHE_AGE_SECS:
		g_value_set_uint64 (value, self->max_cache_age_secs);
		break;
	case PROP_N_RESULTS_MAX:
		g_value_set_uint (value, self->n_results_max);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_odrs_provider_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
	GsOdrsProvider *self = GS_ODRS_PROVIDER (object);

	switch ((GsOdrsProviderProperty) prop_id) {
	case PROP_REVIEW_SERVER:
		/* Construct-only */
		g_assert (self->review_server == NULL);
		self->review_server = g_value_dup_string (value);
		break;
	case PROP_USER_HASH:
		/* Construct-only */
		g_assert (self->user_hash == NULL);
		self->user_hash = g_value_dup_string (value);
		break;
	case PROP_DISTRO:
		/* Construct-only */
		g_assert (self->distro == NULL);
		self->distro = g_value_dup_string (value);
		break;
	case PROP_MAX_CACHE_AGE_SECS:
		/* Construct-only */
		g_assert (self->max_cache_age_secs == 0);
		self->max_cache_age_secs = g_value_get_uint64 (value);
		break;
	case PROP_N_RESULTS_MAX:
		/* Construct-only */
		g_assert (self->n_results_max == 0);
		self->n_results_max = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_odrs_provider_dispose (GObject *object)
{
	GsOdrsProvider *self = GS_ODRS_PROVIDER (object);

	g_clear_object (&self->cached_origin);

	G_OBJECT_CLASS (gs_odrs_provider_parent_class)->dispose (object);
}

static void
gs_odrs_provider_finalize (GObject *object)
{
	GsOdrsProvider *self = GS_ODRS_PROVIDER (object);

	g_free (self->user_hash);
	g_free (self->distro);
	g_free (self->review_server);
	g_clear_pointer (&self->ratings, g_array_unref);
	g_mutex_clear (&self->ratings_mutex);

	G_OBJECT_CLASS (gs_odrs_provider_parent_class)->finalize (object);
}

static void
gs_odrs_provider_class_init (GsOdrsProviderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = gs_odrs_provider_constructed;
	object_class->get_property = gs_odrs_provider_get_property;
	object_class->set_property = gs_odrs_provider_set_property;
	object_class->dispose = gs_odrs_provider_dispose;
	object_class->finalize = gs_odrs_provider_finalize;

	/**
	 * GsOdrsProvider:review-server: (not nullable)
	 *
	 * The URI of the ODRS review server to contact.
	 *
	 * Since: 41
	 */
	obj_props[PROP_REVIEW_SERVER] =
		g_param_spec_string ("review-server", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);

	/**
	 * GsOdrsProvider:user-hash: (not nullable)
	 *
	 * An opaque hash of the user identifier, used to identify the user on
	 * the server.
	 *
	 * Since: 41
	 */
	obj_props[PROP_USER_HASH] =
		g_param_spec_string ("user-hash", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);

	/**
	 * GsOdrsProvider:distro: (not nullable)
	 *
	 * A human readable string identifying the current distribution.
	 *
	 * Since: 41
	 */
	obj_props[PROP_DISTRO] =
		g_param_spec_string ("distro", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);

	/**
	 * GsOdrsProvider:max-cache-age-secs:
	 *
	 * The maximum age of the ODRS cache files, in seconds. Older files will
	 * be refreshed on demand.
	 *
	 * Since: 41
	 */
	obj_props[PROP_MAX_CACHE_AGE_SECS] =
		g_param_spec_uint64 ("max-cache-age-secs", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);

	/**
	 * GsOdrsProvider:n-results-max:
	 *
	 * Maximum number of reviews or ratings to download. The default value
	 * of 0 means no limit is applied.
	 *
	 * Since: 41
	 */
	obj_props[PROP_N_RESULTS_MAX] =
		g_param_spec_uint ("n-results-max", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);


	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);
}

/**
 * gs_odrs_provider_new:
 * @review_server: (not nullable): value for #GsOdrsProvider:review-server
 * @user_hash: (not nullable): value for #GsOdrsProvider:user-hash
 * @distro: (not nullable): value for #GsOdrsProvider:distro
 * @max_cache_age_secs: value for #GsOdrsProvider:max-cache-age-secs
 * @n_results_max: value for #GsOdrsProvider:n-results-max
 *
 * Create a new #GsOdrsProvider. This does no network activity.
 *
 * Returns: (transfer full): a new #GsOdrsProvider
 * Since: 41
 */
GsOdrsProvider *
gs_odrs_provider_new (const gchar *review_server,
                      const gchar *user_hash,
                      const gchar *distro,
                      guint64      max_cache_age_secs,
                      guint        n_results_max)
{
	g_return_val_if_fail (review_server != NULL && *review_server != '\0', NULL);
	g_return_val_if_fail (user_hash != NULL && *user_hash != '\0', NULL);
	g_return_val_if_fail (distro != NULL && *distro != '\0', NULL);

	return g_object_new (GS_TYPE_ODRS_PROVIDER,
			     "review-server", review_server,
			     "user-hash", user_hash,
			     "distro", distro,
			     "max-cache-age-secs", max_cache_age_secs,
			     "n-results-max", n_results_max,
			     NULL);
}

/**
 * gs_odrs_provider_refresh:
 * @self: a #GsOdrsProvider
 * @plugin: the #GsPlugin running this operation
 * @cache_age: cache age, in seconds, as passed to gs_plugin_refresh()
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: return location for a #GError
 *
 * Refresh the cached ODRS ratings and re-load them.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 41
 */
gboolean
gs_odrs_provider_refresh (GsOdrsProvider  *self,
                          GsPlugin        *plugin,
                          guint            cache_age,
                          GCancellable    *cancellable,
                          GError         **error)
{
	g_autofree gchar *cache_filename = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GsApp) app_dl = gs_app_new ("odrs");

	/* check cache age */
	cache_filename = gs_utils_get_cache_filename ("odrs",
						      "ratings.json",
						      GS_UTILS_CACHE_FLAG_WRITEABLE |
						      GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
						      error);
	if (cache_filename == NULL)
		return FALSE;
	if (cache_age > 0) {
		guint tmp;
		g_autoptr(GFile) file = NULL;
		file = g_file_new_for_path (cache_filename);
		tmp = gs_utils_get_file_age (file);
		if (tmp < cache_age) {
			g_debug ("%s is only %u seconds old, so ignoring refresh",
				 cache_filename, tmp);
			return gs_odrs_provider_load_ratings (self, cache_filename, error);
		}
	}

	/* download the complete file */
	uri = g_strdup_printf ("%s/ratings", self->review_server);
	g_debug ("Updating ODRS cache from %s to %s", uri, cache_filename);
	gs_app_set_summary_missing (app_dl,
				    /* TRANSLATORS: status text when downloading */
				    _("Downloading application ratings…"));
	if (!gs_plugin_download_file (plugin, app_dl, uri, cache_filename, cancellable, &error_local)) {
		g_autoptr(GsPluginEvent) event = gs_plugin_event_new ();

		gs_plugin_event_set_error (event, error_local);
		gs_plugin_event_set_action (event, GS_PLUGIN_ACTION_DOWNLOAD);
		gs_plugin_event_set_origin (event, self->cached_origin);
		if (gs_plugin_has_flags (plugin, GS_PLUGIN_FLAGS_INTERACTIVE))
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_INTERACTIVE);
		else
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
		gs_plugin_report_event (plugin, event);

		/* don't fail updates if the ratings server is unavailable */
		return TRUE;
	}
	return gs_odrs_provider_load_ratings (self, cache_filename, error);
}

/**
 * gs_odrs_provider_refine:
 * @self: a #GsOdrsProvider
 * @plugin: the #GsPlugin running this operation
 * @list: list of apps to refine
 * @flags: refine flags
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: return location for a #GError
 *
 * Refine the given @list of apps to add ratings and review data to them, as
 * specified in @flags.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 41
 */
gboolean
gs_odrs_provider_refine (GsOdrsProvider       *self,
                         GsPlugin             *plugin,
                         GsAppList            *list,
                         GsPluginRefineFlags   flags,
                         GCancellable         *cancellable,
                         GError              **error)
{
	/* nothing to do here */
	if ((flags & (GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS |
		      GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS |
		      GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING)) == 0)
		return TRUE;

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		g_autoptr(GError) local_error = NULL;
		if (!refine_app (self, plugin, app, flags, cancellable, &local_error)) {
			if (g_error_matches (local_error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_NO_NETWORK)) {
				g_debug ("failed to refine app %s: %s",
					 gs_app_get_unique_id (app), local_error->message);
			} else {
				g_prefix_error (&local_error, "failed to refine app: ");
				g_propagate_error (error, g_steal_pointer (&local_error));
				return FALSE;
			}
		}
	}

	return TRUE;
}

/**
 * gs_odrs_provider_submit_review:
 * @self: a #GsOdrsProvider
 * @plugin: the #GsPlugin running this operation
 * @app: the app being reviewed
 * @review: the review
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: return location for a #GError
 *
 * Submit a new @review for @app.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 41
 */
gboolean
gs_odrs_provider_submit_review (GsOdrsProvider  *self,
                                GsPlugin        *plugin,
                                GsApp           *app,
                                AsReview        *review,
                                GCancellable    *cancellable,
                                GError         **error)
{
	g_autofree gchar *data = NULL;
	g_autofree gchar *uri = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	/* save as we don't re-request the review from the server */
	as_review_add_flags (review, AS_REVIEW_FLAG_SELF);
	as_review_set_reviewer_name (review, g_get_real_name ());
	as_review_add_metadata (review, "app_id", gs_app_get_id (app));
	as_review_add_metadata (review, "user_skey",
				gs_app_get_metadata_item (app, "ODRS::user_skey"));

	/* create object with review data */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "user_hash");
	json_builder_add_string_value (builder, self->user_hash);
	json_builder_set_member_name (builder, "user_skey");
	json_builder_add_string_value (builder,
				       as_review_get_metadata_item (review, "user_skey"));
	json_builder_set_member_name (builder, "app_id");
	json_builder_add_string_value (builder,
				       as_review_get_metadata_item (review, "app_id"));
	json_builder_set_member_name (builder, "locale");
	json_builder_add_string_value (builder, setlocale (LC_MESSAGES, NULL));
	json_builder_set_member_name (builder, "distro");
	json_builder_add_string_value (builder, self->distro);
	json_builder_set_member_name (builder, "version");
	version = gs_odrs_provider_trim_version (as_review_get_version (review));
	json_builder_add_string_value (builder, version);
	json_builder_set_member_name (builder, "user_display");
	json_builder_add_string_value (builder, as_review_get_reviewer_name (review));
	json_builder_set_member_name (builder, "summary");
	json_builder_add_string_value (builder, as_review_get_summary (review));
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, as_review_get_description (review));
	json_builder_set_member_name (builder, "rating");
	json_builder_add_int_value (builder, as_review_get_rating (review));
	json_builder_end_object (builder);

	/* export as a string */
	json_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	data = json_generator_to_data (json_generator, NULL);

	/* clear cache */
	if (!gs_odrs_provider_invalidate_cache (review, error))
		return FALSE;

	/* POST */
	uri = g_strdup_printf ("%s/submit", self->review_server);
	return gs_odrs_provider_json_post (plugin, gs_plugin_get_soup_session (plugin),
					   uri, data, error);
}

/**
 * gs_odrs_provider_report_review:
 * @self: a #GsOdrsProvider
 * @plugin: the #GsPlugin running this operation
 * @app: the app whose review is being reported
 * @review: the review to report
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: return location for a #GError
 *
 * Report the given @review on @app for being incorrect or breaking the code of
 * conduct.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 41
 */
gboolean
gs_odrs_provider_report_review (GsOdrsProvider  *self,
                                GsPlugin        *plugin,
                                GsApp           *app,
                                AsReview        *review,
                                GCancellable    *cancellable,
                                GError         **error)
{
	g_autofree gchar *uri = NULL;
	uri = g_strdup_printf ("%s/report", self->review_server);
	return gs_odrs_provider_vote (self, plugin, review, uri, error);
}

/**
 * gs_odrs_provider_upvote_review:
 * @self: a #GsOdrsProvider
 * @plugin: the #GsPlugin running this operation
 * @app: the app whose review is being upvoted
 * @review: the review to upvote
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: return location for a #GError
 *
 * Add one vote to @review on @app.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 41
 */
gboolean
gs_odrs_provider_upvote_review (GsOdrsProvider  *self,
                                GsPlugin        *plugin,
                                GsApp           *app,
                                AsReview        *review,
                                GCancellable    *cancellable,
                                GError         **error)
{
	g_autofree gchar *uri = NULL;
	uri = g_strdup_printf ("%s/upvote", self->review_server);
	return gs_odrs_provider_vote (self, plugin, review, uri, error);
}

/**
 * gs_odrs_provider_downvote_review:
 * @self: a #GsOdrsProvider
 * @plugin: the #GsPlugin running this operation
 * @app: the app whose review is being downvoted
 * @review: the review to downvote
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: return location for a #GError
 *
 * Remove one vote from @review on @app.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 41
 */
gboolean
gs_odrs_provider_downvote_review (GsOdrsProvider  *self,
                                  GsPlugin        *plugin,
                                  GsApp           *app,
                                  AsReview        *review,
                                  GCancellable    *cancellable,
                                  GError         **error)
{
	g_autofree gchar *uri = NULL;
	uri = g_strdup_printf ("%s/downvote", self->review_server);
	return gs_odrs_provider_vote (self, plugin, review, uri, error);
}

/**
 * gs_odrs_provider_dismiss_review:
 * @self: a #GsOdrsProvider
 * @plugin: the #GsPlugin running this operation
 * @app: the app whose review is being dismissed
 * @review: the review to dismiss
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: return location for a #GError
 *
 * Dismiss (ignore) @review on @app when moderating.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 41
 */
gboolean
gs_odrs_provider_dismiss_review (GsOdrsProvider  *self,
                                 GsPlugin        *plugin,
                                 GsApp           *app,
                                 AsReview        *review,
                                 GCancellable    *cancellable,
                                 GError         **error)
{
	g_autofree gchar *uri = NULL;
	uri = g_strdup_printf ("%s/dismiss", self->review_server);
	return gs_odrs_provider_vote (self, plugin, review, uri, error);
}

/**
 * gs_odrs_provider_remove_review:
 * @self: a #GsOdrsProvider
 * @plugin: the #GsPlugin running this operation
 * @app: the app whose review is being removed
 * @review: the review to remove
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: return location for a #GError
 *
 * Remove a @review written by the user, from @app.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 41
 */
gboolean
gs_odrs_provider_remove_review (GsOdrsProvider  *self,
                                GsPlugin        *plugin,
                                GsApp           *app,
                                AsReview        *review,
                                GCancellable    *cancellable,
                                GError         **error)
{
	g_autofree gchar *uri = NULL;
	uri = g_strdup_printf ("%s/remove", self->review_server);
	return gs_odrs_provider_vote (self, plugin, review, uri, error);
}

/**
 * gs_odrs_provider_add_unvoted_reviews:
 * @self: a #GsOdrsProvider
 * @plugin: the #GsPlugin running this operation
 * @list: list of apps to add unvoted reviews to
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: return location for a #GError
 *
 * Add the unmoderated reviews for each app in @list to the apps.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 * Since: 41
 */
gboolean
gs_odrs_provider_add_unvoted_reviews (GsOdrsProvider  *self,
                                      GsPlugin        *plugin,
                                      GsAppList       *list,
                                      GCancellable    *cancellable,
                                      GError         **error)
{
	guint status_code;
	guint i;
	g_autofree gchar *uri = NULL;
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GPtrArray) reviews = NULL;
	g_autoptr(SoupMessage) msg = NULL;

	/* create the GET data *with* the machine hash so we can later
	 * review the application ourselves */
	uri = g_strdup_printf ("%s/moderate/%s/%s",
			       self->review_server,
			       self->user_hash,
			       setlocale (LC_MESSAGES, NULL));
	msg = soup_message_new (SOUP_METHOD_GET, uri);
	status_code = soup_session_send_message (gs_plugin_get_soup_session (plugin), msg);
	if (status_code != SOUP_STATUS_OK) {
		if (!gs_odrs_provider_parse_success (plugin,
						     msg->response_body->data,
						     msg->response_body->length,
						     error))
			return FALSE;
		/* not sure what to do here */
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
				     "status code invalid");
		gs_utils_error_add_origin_id (error, self->cached_origin);
		return FALSE;
	}
	g_debug ("odrs returned: %s", msg->response_body->data);
	reviews = gs_odrs_provider_parse_reviews (self,
						  plugin,
						  msg->response_body->data,
						  msg->response_body->length,
						  error);
	if (reviews == NULL)
		return FALSE;

	/* look at all the reviews; faking application objects */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal,
				      g_free, (GDestroyNotify) g_object_unref);
	for (i = 0; i < reviews->len; i++) {
		GsApp *app;
		AsReview *review;
		const gchar *app_id;

		/* same app? */
		review = g_ptr_array_index (reviews, i);
		app_id = as_review_get_metadata_item (review, "app_id");
		app = g_hash_table_lookup (hash, app_id);
		if (app == NULL) {
			app = gs_odrs_provider_create_app_dummy (app_id);
			gs_app_list_add (list, app);
			g_hash_table_insert (hash, g_strdup (app_id), app);
		}
		gs_app_add_review (app, review);
	}

	return TRUE;
}
