/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

#include <gs-plugin.h>
#include <json-glib/json-glib.h>
#include <string.h>

#include "gs-app.h"
#include "gs-os-release.h"
#include "gs-plugin.h"
#include "gs-review.h"
#include "gs-utils.h"

/*
 * SECTION:
 * Provides review data from the Open Desktop Ratings Serice.
 */

#define XDG_APP_REVIEW_CACHE_AGE_MAX		237000 /* 1 week */
#define XDG_APP_REVIEW_NUMBER_RESULTS_MAX	5

struct GsPluginPrivate {
	GSettings		*settings;
	gchar			*distro;
	gchar			*user_hash;
	gchar			*review_server;
	GHashTable		*ratings;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "odrs";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	g_autoptr(GError) error = NULL;
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->settings = g_settings_new ("org.gnome.software");
	plugin->priv->review_server = g_settings_get_string (plugin->priv->settings,
							     "review-server");
	plugin->priv->ratings = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, (GDestroyNotify) g_array_unref);

	/* get the machine+user ID hash value */
	plugin->priv->user_hash = gs_utils_get_user_hash (&error);
	if (plugin->priv->user_hash == NULL) {
		g_warning ("Failed to get machine+user hash: %s", error->message);
		return;
	}

	/* get the distro name (e.g. 'Fedora') but allow a fallback */
	plugin->priv->distro = gs_os_release_get_name (&error);
	if (plugin->priv->distro == NULL) {
		g_warning ("Failed to get distro name: %s", error->message);
		plugin->priv->distro = g_strdup ("Unknown");
	}
}


/**
 * gs_plugin_order_after:
 */
const gchar **
gs_plugin_order_after (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"appstream",	/* need application IDs */
		"xdg-app",	/* need version */
		NULL };
	return deps;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_free (plugin->priv->user_hash);
	g_free (plugin->priv->distro);
	g_object_unref (plugin->priv->settings);
	g_hash_table_unref (plugin->priv->ratings);
}

static GArray *
gs_plugin_odrs_load_ratings_for_app (JsonObject *json_app)
{
	GArray *ratings;
	guint64 tmp;
	guint i;
	const gchar *names[] = { "star0", "star1", "star2", "star3",
				 "star4", "star5", NULL };

	ratings = g_array_sized_new (FALSE, TRUE, sizeof(guint32), 6);
	for (i = 0; names[i] != NULL; i++) {
		if (!json_object_has_member (json_app, names[i]))
			continue;
		tmp = json_object_get_int_member (json_app, names[i]);
		g_array_append_val (ratings, tmp);
	}

	return ratings;
}

static gboolean
gs_plugin_odrs_load_ratings (GsPlugin *plugin, const gchar *fn, GError **error)
{
	GList *l;
	JsonNode *json_root;
	JsonObject *json_item;
	g_autoptr(GList) apps = NULL;
	g_autoptr(JsonParser) json_parser = NULL;

	/* remove all existing */
	g_hash_table_remove_all (plugin->priv->ratings);

	/* parse the data and find the success */
	json_parser = json_parser_new ();
	if (!json_parser_load_from_file (json_parser, fn, error))
		return NULL;
	json_root = json_parser_get_root (json_parser);
	if (json_root == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no ratings root");
		return FALSE;
	}
	if (json_node_get_node_type (json_root) != JSON_NODE_OBJECT) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no ratings array");
		return FALSE;
	}

	/* parse each app */
	json_item = json_node_get_object (json_root);
	apps = json_object_get_members (json_item);
	for (l = apps; l != NULL; l = l->next) {
		const gchar *app_id = (const gchar *) l->data;
		JsonObject *json_app = json_object_get_object_member (json_item, app_id);
		g_autoptr(GArray) ratings = NULL;;
		ratings = gs_plugin_odrs_load_ratings_for_app (json_app);
		if (ratings->len == 6) {
			g_hash_table_insert (plugin->priv->ratings,
					     g_strdup (app_id),
					     g_array_ref (ratings));
		}
	}
	return TRUE;
}

static gboolean
gs_plugin_odrs_refresh_ratings (GsPlugin *plugin,
				guint cache_age,
				GCancellable *cancellable,
				GError **error)
{
	g_autofree gchar *cachedir = NULL;
	g_autofree gchar *fn = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(GsApp) app_dl = gs_app_new (plugin->name);

	/* check cache age */
	cachedir = gs_utils_get_cachedir ("ratings", error);
	if (cachedir == NULL)
		return NULL;
	fn = g_strdup_printf ("%s/odrs.json", cachedir);
	if (cache_age > 0) {
		guint tmp;
		g_autoptr(GFile) file = NULL;
		file = g_file_new_for_path (fn);
		tmp = gs_utils_get_file_age (file);
		if (tmp < cache_age) {
			g_debug ("%s is only %i seconds old, so ignoring refresh",
				 fn, tmp);
			if (!gs_plugin_odrs_load_ratings (plugin, fn, error))
				g_error ("MOO: %s", (*error)->message);
			return gs_plugin_odrs_load_ratings (plugin, fn, error);
		}
	}

	/* download the complete file */
	uri = g_strdup_printf ("%s/ratings", plugin->priv->review_server);
	if (!gs_plugin_download_file (plugin, app_dl, uri, fn, cancellable, error))
		return FALSE;
	return gs_plugin_odrs_load_ratings (plugin, fn, error);
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	/* just ensure there is any data, no matter how old */
	if (!gs_plugin_odrs_refresh_ratings (plugin, G_MAXUINT, cancellable, error))
		return FALSE;
	return TRUE;
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GsPluginRefreshFlags flags,
		   GCancellable *cancellable,
		   GError **error)
{
	/* get the reviews */
	if (flags & GS_PLUGIN_REFRESH_FLAGS_METADATA) {
		if (!gs_plugin_odrs_refresh_ratings (plugin,
						     cache_age,
						     cancellable,
						     error))
			return FALSE;
	}
	return TRUE;
}

/**
 * gs_plugin_odrs_parse_review_object:
 */
static GsReview *
gs_plugin_odrs_parse_review_object (JsonObject *item)
{
	GsReview *rev = gs_review_new ();

	/* date */
	if (json_object_has_member (item, "date_created")) {
		guint64 timestamp;
		g_autoptr(GDateTime) dt = NULL;
		timestamp = json_object_get_int_member (item, "date_created");
		dt = g_date_time_new_from_unix_utc (timestamp);
		gs_review_set_date (rev, dt);
	}

	/* assemble review */
	if (json_object_has_member (item, "rating"))
		gs_review_set_rating (rev, json_object_get_int_member (item, "rating"));
	if (json_object_has_member (item, "score"))
		gs_review_set_score (rev, json_object_get_int_member (item, "score"));
	if (json_object_has_member (item, "user_display"))
		gs_review_set_reviewer (rev, json_object_get_string_member (item, "user_display"));
	if (json_object_has_member (item, "summary"))
		gs_review_set_summary (rev, json_object_get_string_member (item, "summary"));
	if (json_object_has_member (item, "description"))
		gs_review_set_text (rev, json_object_get_string_member (item, "description"));
	if (json_object_has_member (item, "version"))
		gs_review_set_version (rev, json_object_get_string_member (item, "version"));
	if (json_object_has_member (item, "karma"))
		gs_review_set_karma (rev, json_object_get_int_member (item, "karma"));

	/* add extra metadata for the plugin */
	if (json_object_has_member (item, "user_hash")) {
		gs_review_add_metadata (rev, "user_hash",
					json_object_get_string_member (item, "user_hash"));
	}
	if (json_object_has_member (item, "user_skey")) {
		gs_review_add_metadata (rev, "user_skey",
					json_object_get_string_member (item, "user_skey"));
	}
	if (json_object_has_member (item, "app_id")) {
		gs_review_add_metadata (rev, "app_id",
					json_object_get_string_member (item, "app_id"));
	}
	if (json_object_has_member (item, "review_id")) {
		g_autofree gchar *review_id = NULL;
		review_id = g_strdup_printf ("%" G_GINT64_FORMAT,
					json_object_get_int_member (item, "review_id"));
		gs_review_add_metadata (rev, "review_id", review_id);
	}

	/* don't allow multiple votes */
	if (json_object_has_member (item, "vote_id"))
		gs_review_add_flags (rev, GS_REVIEW_FLAG_VOTED);

	return rev;
}

/**
 * gs_plugin_odrs_parse_reviews:
 */
static GPtrArray *
gs_plugin_odrs_parse_reviews (const gchar *data, gsize data_len, GError **error)
{
	JsonArray *json_reviews;
	JsonNode *json_root;
	guint i;
	g_autoptr(JsonParser) json_parser = NULL;
	g_autoptr(GPtrArray) reviews = NULL;

	/* nothing */
	if (data == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "server returned no data");
		return NULL;
	}

	/* parse the data and find the array or ratings */
	json_parser = json_parser_new ();
	if (!json_parser_load_from_data (json_parser, data, data_len, error))
		return NULL;
	json_root = json_parser_get_root (json_parser);
	if (json_root == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no root");
		return NULL;
	}
	if (json_node_get_node_type (json_root) != JSON_NODE_ARRAY) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no array");
		return NULL;
	}

	/* parse each rating */
	reviews = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	json_reviews = json_node_get_array (json_root);
	for (i = 0; i < json_array_get_length (json_reviews); i++) {
		JsonNode *json_review;
		JsonObject *json_item;
		g_autoptr(GsReview) review = NULL;

		/* extract the data */
		json_review = json_array_get_element (json_reviews, i);
		if (json_node_get_node_type (json_review) != JSON_NODE_OBJECT) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     "no object type");
			return NULL;
		}
		json_item = json_node_get_object (json_review);
		if (json_item == NULL) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     "no object");
			return NULL;
		}

		/* create review */
		review = gs_plugin_odrs_parse_review_object (json_item);
		g_ptr_array_add (reviews, g_object_ref (review));
	}
	return g_steal_pointer (&reviews);
}

/**
 * gs_plugin_odrs_parse_success:
 */
static gboolean
gs_plugin_odrs_parse_success (const gchar *data, gsize data_len, GError **error)
{
	JsonNode *json_root;
	JsonObject *json_item;
	const gchar *msg = NULL;
	g_autoptr(JsonParser) json_parser = NULL;

	/* nothing */
	if (data == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "server returned no data");
		return FALSE;
	}

	/* parse the data and find the success */
	json_parser = json_parser_new ();
	if (!json_parser_load_from_data (json_parser, data, data_len, error))
		return FALSE;
	json_root = json_parser_get_root (json_parser);
	if (json_root == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no error root");
		return FALSE;
	}
	if (json_node_get_node_type (json_root) != JSON_NODE_OBJECT) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no error object");
		return FALSE;
	}
	json_item = json_node_get_object (json_root);
	if (json_item == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "no error object");
		return FALSE;
	}

	/* failed? */
	if (json_object_has_member (json_item, "msg"))
		msg = json_object_get_string_member (json_item, "msg");
	if (!json_object_get_boolean_member (json_item, "success")) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     msg != NULL ? msg : "unknown failure");
		return FALSE;
	}

	/* just for the console */
	if (msg != NULL)
		g_debug ("success: %s", msg);
	return TRUE;
}

/**
 * gs_plugin_odrs_json_post:
 */
static gboolean
gs_plugin_odrs_json_post (SoupSession *session,
			  const gchar *uri,
			  const gchar *data,
			  GError **error)
{
	guint status_code;
	g_autoptr(SoupMessage) msg = NULL;

	/* create the GET data */
	g_debug ("odrs sending: %s", data);
	msg = soup_message_new (SOUP_METHOD_POST, uri);
	soup_message_set_request (msg, "application/json; charset=utf-8",
				  SOUP_MEMORY_COPY, data, strlen (data));

	/* set sync request */
	status_code = soup_session_send_message (session, msg);
	if (status_code != SOUP_STATUS_OK) {
		g_warning ("Failed to set rating on odrs: %s",
			   soup_status_get_phrase (status_code));
	}

	/* process returned JSON */
	g_debug ("odrs returned: %s", msg->response_body->data);
	return gs_plugin_odrs_parse_success (msg->response_body->data,
					     msg->response_body->length,
					     error);
}

/**
 * gs_plugin_refine_ratings:
 */
static gboolean
gs_plugin_refine_ratings (GsPlugin *plugin,
			  GsApp *app,
			  GCancellable *cancellable,
			  GError **error)
{
	GArray *review_ratings;
	const guint to_percentage[] = { 0, 20, 40, 60, 80, 100 };
	guint32 cnt = 0;
	guint32 acc = 0;
	guint i;

	/* get ratings */
	review_ratings = g_hash_table_lookup (plugin->priv->ratings,
					      gs_app_get_id_no_prefix (app));
	if (review_ratings == NULL)
		return TRUE;
	gs_app_set_review_ratings (app, review_ratings);

	/* find the correct global rating */
	for (i = 1; i <= 5; i++) {
		guint32 tmp = g_array_index (review_ratings, guint32, i);
		acc += to_percentage[i] * tmp;
		cnt += tmp;
	}
	if (cnt == 0)
		gs_app_set_rating (app, cnt);
	else
		gs_app_set_rating (app, acc / cnt);

	return TRUE;
}

/**
 * gs_plugin_odrs_fetch_for_app:
 */
static GPtrArray *
gs_plugin_odrs_fetch_for_app (GsPlugin *plugin, GsApp *app, GError **error)
{
	const gchar *version;
	guint karma_min;
	guint status_code;
	g_autofree gchar *cachedir = NULL;
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
	cachedir = gs_utils_get_cachedir ("reviews", error);
	if (cachedir == NULL)
		return NULL;
	cachefn = g_strdup_printf ("%s/%s.json", cachedir, gs_app_get_id_no_prefix (app));
	cachefn_file = g_file_new_for_path (cachefn);
	if (gs_utils_get_file_age (cachefn_file) < XDG_APP_REVIEW_CACHE_AGE_MAX) {
		g_autofree gchar *json_data = NULL;
		if (!g_file_get_contents (cachefn, &json_data, NULL, error))
			return NULL;
		g_debug ("got review data for %s from %s",
			 gs_app_get_id_no_prefix (app), cachefn);
		return gs_plugin_odrs_parse_reviews (json_data, -1, error);
	}

	/* not always available */
	version = gs_app_get_version (app);
	if (version == NULL)
		version = "unknown";

	/* create object with review data */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "user_hash");
	json_builder_add_string_value (builder, plugin->priv->user_hash);
	json_builder_set_member_name (builder, "app_id");
	json_builder_add_string_value (builder, gs_app_get_id_no_prefix (app));
	json_builder_set_member_name (builder, "locale");
	json_builder_add_string_value (builder, plugin->locale);
	json_builder_set_member_name (builder, "distro");
	json_builder_add_string_value (builder, plugin->priv->distro);
	json_builder_set_member_name (builder, "version");
	json_builder_add_string_value (builder, version);
	json_builder_set_member_name (builder, "limit");
	json_builder_add_int_value (builder, XDG_APP_REVIEW_NUMBER_RESULTS_MAX);
	json_builder_set_member_name (builder, "karma");
	karma_min = g_settings_get_int (plugin->priv->settings,
					"review-karma-required");
	json_builder_add_int_value (builder, karma_min);
	json_builder_end_object (builder);

	/* export as a string */
	json_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	data = json_generator_to_data (json_generator, NULL);
	if (data == NULL)
		return NULL;
	uri = g_strdup_printf ("%s/fetch", plugin->priv->review_server);
	msg = soup_message_new (SOUP_METHOD_POST, uri);
	soup_message_set_request (msg, "application/json; charset=utf-8",
				  SOUP_MEMORY_COPY, data, strlen (data));
	status_code = soup_session_send_message (plugin->soup_session, msg);
	if (status_code != SOUP_STATUS_OK) {
		if (!gs_plugin_odrs_parse_success (msg->response_body->data,
						   msg->response_body->length,
						   error))
			return NULL;
		/* not sure what to do here */
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "status code invalid");
		return NULL;
	}
	reviews = gs_plugin_odrs_parse_reviews (msg->response_body->data,
						msg->response_body->length,
						error);
	if (reviews == NULL)
		return NULL;
	g_debug ("odrs returned: %s", msg->response_body->data);

	/* save to the cache */
	if (!g_file_set_contents (cachefn,
				  msg->response_body->data,
				  msg->response_body->length,
				  error))
		return NULL;

	/* success */
	return g_steal_pointer (&reviews);
}

/**
 * gs_plugin_refine_reviews:
 */
static gboolean
gs_plugin_refine_reviews (GsPlugin *plugin,
			  GsApp *app,
			  GCancellable *cancellable,
			  GError **error)
{
	GsReview *review;
	guint i;
	g_autoptr(GPtrArray) reviews = NULL;

	/* get from server */
	reviews = gs_plugin_odrs_fetch_for_app (plugin, app, error);
	if (reviews == NULL)
		return FALSE;
	for (i = 0; i < reviews->len; i++) {
		review = g_ptr_array_index (reviews, i);

		/* save this on the application object so we can use it for
		 * submitting a new review */
		if (i == 0) {
			gs_app_set_metadata (app, "ODRS::user_skey",
					     gs_review_get_metadata_item (review, "user_skey"));
		}

		/* ignore invalid reviews */
		if (gs_review_get_rating (review) == 0)
			continue;
		if (gs_review_get_reviewer (review) == NULL)
			continue;

		/* the user_hash matches, so mark this as our own review */
		if (g_strcmp0 (gs_review_get_metadata_item (review, "user_hash"),
			       plugin->priv->user_hash) == 0) {
			gs_review_set_flags (review, GS_REVIEW_FLAG_SELF);
		}
		gs_app_add_review (app, review);
	}
	return TRUE;
}

/**
 * gs_plugin_refine_app:
 */
gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	/* not valid */
	if (gs_app_get_kind (app) == AS_APP_KIND_ADDON)
		return TRUE;
	if (gs_app_get_id_no_prefix (app) == NULL)
		return TRUE;

	/* add reviews if possible */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS) {
		if (gs_app_get_reviews(app)->len > 0)
			return TRUE;
		if (!gs_plugin_refine_reviews (plugin, app,
					       cancellable, error))
			return FALSE;
	}

	/* add ratings if possible */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS ||
	    flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING) {
		if (gs_app_get_review_ratings(app) != NULL)
			return TRUE;
		if (!gs_plugin_refine_ratings (plugin, app,
					       cancellable, error))
			return FALSE;
	}

	return TRUE;
}

/**
 * gs_plugin_odrs_sanitize_version:
 */
static gchar *
gs_plugin_odrs_sanitize_version (const gchar *version)
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

/**
 * gs_plugin_odrs_invalidate_cache:
 */
static gboolean
gs_plugin_odrs_invalidate_cache (GsReview *review, GError **error)
{
	g_autofree gchar *cachedir = NULL;
	g_autofree gchar *cachefn = NULL;
	g_autoptr(GFile) cachefn_file = NULL;

	/* look in the cache */
	cachedir = gs_utils_get_cachedir ("reviews", error);
	if (cachedir == NULL)
		return FALSE;
	cachefn = g_strdup_printf ("%s/%s.json",
				   cachedir,
				   gs_review_get_metadata_item (review, "app_id"));
	cachefn_file = g_file_new_for_path (cachefn);
	if (!g_file_query_exists (cachefn_file, NULL))
		return TRUE;
	return g_file_delete (cachefn_file, NULL, error);
}

/**
 * gs_plugin_review_submit:
 */
gboolean
gs_plugin_review_submit (GsPlugin *plugin,
			 GsApp *app,
			 GsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	g_autofree gchar *data = NULL;
	g_autofree gchar *uri = NULL;
	g_autofree gchar *version = NULL;
	g_autoptr(JsonBuilder) builder = NULL;
	g_autoptr(JsonGenerator) json_generator = NULL;
	g_autoptr(JsonNode) json_root = NULL;

	/* save as we don't re-request the review from the server */
	gs_review_set_reviewer (review, g_get_real_name ());
	gs_review_add_metadata (review, "app_id", gs_app_get_id_no_prefix (app));
	gs_review_add_metadata (review, "user_skey",
				gs_app_get_metadata_item (app, "ODRS::user_skey"));

	/* create object with review data */
	builder = json_builder_new ();
	json_builder_begin_object (builder);
	json_builder_set_member_name (builder, "user_hash");
	json_builder_add_string_value (builder, plugin->priv->user_hash);
	json_builder_set_member_name (builder, "user_skey");
	json_builder_add_string_value (builder,
				       gs_review_get_metadata_item (review, "user_skey"));
	json_builder_set_member_name (builder, "app_id");
	json_builder_add_string_value (builder,
				       gs_review_get_metadata_item (review, "app_id"));
	json_builder_set_member_name (builder, "locale");
	json_builder_add_string_value (builder, plugin->locale);
	json_builder_set_member_name (builder, "distro");
	json_builder_add_string_value (builder, plugin->priv->distro);
	json_builder_set_member_name (builder, "version");
	version = gs_plugin_odrs_sanitize_version (gs_review_get_version (review));
	json_builder_add_string_value (builder, version);
	json_builder_set_member_name (builder, "user_display");
	json_builder_add_string_value (builder, gs_review_get_reviewer (review));
	json_builder_set_member_name (builder, "summary");
	json_builder_add_string_value (builder, gs_review_get_summary (review));
	json_builder_set_member_name (builder, "description");
	json_builder_add_string_value (builder, gs_review_get_text (review));
	json_builder_set_member_name (builder, "rating");
	json_builder_add_int_value (builder, gs_review_get_rating (review));
	json_builder_end_object (builder);

	/* export as a string */
	json_root = json_builder_get_root (builder);
	json_generator = json_generator_new ();
	json_generator_set_pretty (json_generator, TRUE);
	json_generator_set_root (json_generator, json_root);
	data = json_generator_to_data (json_generator, NULL);

	/* clear cache */
	if (!gs_plugin_odrs_invalidate_cache (review, error))
		return FALSE;

	/* POST */
	uri = g_strdup_printf ("%s/submit", plugin->priv->review_server);
	return gs_plugin_odrs_json_post (plugin->soup_session,
						    uri, data, error);
}

/**
 * gs_plugin_odrs_vote:
 */
static gboolean
gs_plugin_odrs_vote (GsPlugin *plugin, GsReview *review,
		     const gchar *uri, GError **error)
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
	json_builder_add_string_value (builder, plugin->priv->user_hash);
	json_builder_set_member_name (builder, "user_skey");
	json_builder_add_string_value (builder,
				       gs_review_get_metadata_item (review, "user_skey"));
	json_builder_set_member_name (builder, "app_id");
	json_builder_add_string_value (builder,
				       gs_review_get_metadata_item (review, "app_id"));
	tmp = gs_review_get_metadata_item (review, "review_id");
	if (tmp != NULL) {
		guint64 review_id;
		json_builder_set_member_name (builder, "review_id");
		review_id = g_ascii_strtoull (tmp, NULL, 10);
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
	if (!gs_plugin_odrs_invalidate_cache (review, error))
		return FALSE;

	/* send to server */
	if (!gs_plugin_odrs_json_post (plugin->soup_session,
						  uri, data, error))
		return FALSE;

	/* mark as voted */
	gs_review_add_flags (review, GS_REVIEW_FLAG_VOTED);

	/* success */
	return TRUE;
}

/**
 * gs_plugin_review_report:
 */
gboolean
gs_plugin_review_report (GsPlugin *plugin,
			 GsApp *app,
			 GsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	g_autofree gchar *uri = NULL;
	uri = g_strdup_printf ("%s/report", plugin->priv->review_server);
	return gs_plugin_odrs_vote (plugin, review, uri, error);
}

/**
 * gs_plugin_review_upvote:
 */
gboolean
gs_plugin_review_upvote (GsPlugin *plugin,
			 GsApp *app,
			 GsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	g_autofree gchar *uri = NULL;
	uri = g_strdup_printf ("%s/upvote", plugin->priv->review_server);
	return gs_plugin_odrs_vote (plugin, review, uri, error);
}

/**
 * gs_plugin_review_downvote:
 */
gboolean
gs_plugin_review_downvote (GsPlugin *plugin,
			   GsApp *app,
			   GsReview *review,
			   GCancellable *cancellable,
			   GError **error)
{
	g_autofree gchar *uri = NULL;
	uri = g_strdup_printf ("%s/downvote", plugin->priv->review_server);
	return gs_plugin_odrs_vote (plugin, review, uri, error);
}

/**
 * gs_plugin_review_dismiss:
 */
gboolean
gs_plugin_review_dismiss (GsPlugin *plugin,
			  GsApp *app,
			  GsReview *review,
			  GCancellable *cancellable,
			  GError **error)
{
	g_autofree gchar *uri = NULL;
	uri = g_strdup_printf ("%s/dismiss", plugin->priv->review_server);
	return gs_plugin_odrs_vote (plugin, review, uri, error);
}

/**
 * gs_plugin_review_remove:
 */
gboolean
gs_plugin_review_remove (GsPlugin *plugin,
			 GsApp *app,
			 GsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	g_autofree gchar *uri = NULL;
	uri = g_strdup_printf ("%s/remove", plugin->priv->review_server);
	return gs_plugin_odrs_vote (plugin, review, uri, error);
}

/**
 * gs_plugin_create_app_dummy:
 */
static GsApp *
gs_plugin_create_app_dummy (const gchar *id)
{
	GsApp *app = gs_app_new (id);
	g_autoptr(GString) str = NULL;
	str = g_string_new (id);
	as_utils_string_replace (str, ".desktop", "");
	g_string_prepend (str, "No description is available for ");
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, "Unknown Application");
	gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, "Application not found");
	gs_app_set_description (app, GS_APP_QUALITY_LOWEST, str->str);
	return app;
}

/**
 * gs_plugin_add_unvoted_reviews:
 */
gboolean
gs_plugin_add_unvoted_reviews (GsPlugin *plugin,
			       GList **list,
			       GCancellable *cancellable,
			       GError **error)
{
	guint status_code;
	guint i;
	g_autofree gchar *uri = NULL;
	g_autoptr(GHashTable) hash = NULL;
	g_autoptr(GPtrArray) reviews = NULL;
	g_autoptr(SoupMessage) msg = NULL;

	/* create the GET data *with* the machine hash so we can later
	 * review the application ourselves */
	uri = g_strdup_printf ("%s/moderate/%s",
			       plugin->priv->review_server,
			       plugin->priv->user_hash);
	msg = soup_message_new (SOUP_METHOD_GET, uri);
	status_code = soup_session_send_message (plugin->soup_session, msg);
	if (status_code != SOUP_STATUS_OK) {
		if (!gs_plugin_odrs_parse_success (msg->response_body->data,
						   msg->response_body->length,
						   error))
			return FALSE;
		/* not sure what to do here */
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "status code invalid");
		return FALSE;
	}
	g_debug ("odrs returned: %s", msg->response_body->data);
	reviews = gs_plugin_odrs_parse_reviews (msg->response_body->data,
						msg->response_body->length,
						error);
	if (reviews == NULL)
		return FALSE;

	/* look at all the reviews; faking application objects */
	hash = g_hash_table_new_full (g_str_hash, g_str_equal,
				      g_free, (GDestroyNotify) g_object_unref);
	for (i = 0; i < reviews->len; i++) {
		GsApp *app;
		GsReview *review;
		const gchar *app_id;

		/* same app? */
		review = g_ptr_array_index (reviews, i);
		app_id = gs_review_get_metadata_item (review, "app_id");
		app = g_hash_table_lookup (hash, app_id);
		if (app == NULL) {
			app = gs_plugin_create_app_dummy (app_id);
			gs_plugin_add_app (list, app);
			g_hash_table_insert (hash, g_strdup (app_id), app);
		}
		gs_app_add_review (app, review);
	}

	return TRUE;
}
