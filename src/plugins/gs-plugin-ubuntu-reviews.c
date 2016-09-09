/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Canonical Ltd.
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

#include <string.h>
#include <math.h>
#include <json-glib/json-glib.h>
#include <sqlite3.h>
#include <oauth.h>
#include <gnome-software.h>

struct GsPluginData {
	gchar		*db_path;
	sqlite3		*db;
	gsize		 db_loaded;
	gchar		*origin;
	gchar		*distroseries;
};

typedef struct {
	guint64		 one_star_count;
	guint64		 two_star_count;
	guint64		 three_star_count;
	guint64		 four_star_count;
	guint64		 five_star_count;
} Histogram;

#define UBUNTU_REVIEWS_SERVER		"https://reviews.ubuntu.com/reviews"

/* Download new stats every three months */
// FIXME: Much shorter time?
#define REVIEW_STATS_AGE_MAX		(60 * 60 * 24 * 7 * 4 * 3)

/* Number of pages of reviews to download */
#define N_PAGES				3

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	g_autoptr(GsOsRelease) os_release = NULL;
	g_autoptr(GError) error = NULL;

	/* check that we are running on Ubuntu */
	if (!gs_plugin_check_distro_id (plugin, "ubuntu")) {
		gs_plugin_set_enabled (plugin, FALSE);
		g_debug ("disabling '%s' as we're not Ubuntu", gs_plugin_get_name (plugin));
		return;
	}

	priv->db_path = g_build_filename (g_get_user_data_dir (),
					  "gnome-software",
					  "ubuntu-reviews.db",
					  NULL);

	os_release = gs_os_release_new (&error);
	if (os_release == NULL) {
		g_warning ("Failed to determine OS information: %s", error->message);
		priv->origin = g_strdup ("unknown");
		priv->distroseries = g_strdup ("unknown");
	} else  {
		priv->origin = g_strdup (gs_os_release_get_id (os_release));
		if (priv->origin == NULL)
			priv->origin = g_strdup ("unknown");
		if (strcmp (priv->origin, "ubuntu") == 0)
			priv->distroseries = g_strdup (gs_os_release_get_distro_codename (os_release));
		if (priv->distroseries == NULL)
			priv->distroseries = g_strdup ("unknown");
	}

	/* we have more reviews than ORDS */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "odrs");

	/* need source */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_clear_pointer (&priv->db, sqlite3_close);
	g_free (priv->db_path);
	g_free (priv->origin);
	g_free (priv->distroseries);
}

static gint
get_timestamp_sqlite_cb (void *data, gint argc,
			 gchar **argv, gchar **col_name)
{
	gint64 *timestamp = (gint64 *) data;
	*timestamp = g_ascii_strtoll (argv[0], NULL, 10);
	return 0;
}

static gboolean
set_package_stats (GsPlugin *plugin,
		   const gchar *package_name,
		   Histogram *histogram,
		   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	char *error_msg = NULL;
	gint result;
	g_autofree gchar *statement = NULL;

	statement = g_strdup_printf ("INSERT OR REPLACE INTO review_stats (package_name, "
				     "one_star_count, two_star_count, three_star_count, "
				     "four_star_count, five_star_count) "
				     "VALUES ('%s', '%" G_GUINT64_FORMAT "', '%" G_GUINT64_FORMAT"', '%" G_GUINT64_FORMAT "', '%" G_GUINT64_FORMAT "', '%" G_GUINT64_FORMAT "');",
				     package_name, histogram->one_star_count, histogram->two_star_count,
				     histogram->three_star_count, histogram->four_star_count, histogram->five_star_count);
	result = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (result != SQLITE_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}

	return TRUE;
}

static gboolean
set_timestamp (GsPlugin *plugin,
	       const gchar *type,
	       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	char *error_msg = NULL;
	gint result;
	g_autofree gchar *statement = NULL;

	statement = g_strdup_printf ("INSERT OR REPLACE INTO timestamps (key, value) "
				     "VALUES ('%s', '%" G_GINT64_FORMAT "');",
				     type,
				     g_get_real_time () / G_USEC_PER_SEC);
	result = sqlite3_exec (priv->db, statement, NULL, NULL, &error_msg);
	if (result != SQLITE_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}
	return TRUE;
}

static gint
get_review_stats_sqlite_cb (void *data,
			    gint argc,
			    gchar **argv,
			    gchar **col_name)
{
	Histogram *histogram = (Histogram *) data;
	histogram->one_star_count = g_ascii_strtoull (argv[0], NULL, 10);
	histogram->two_star_count = g_ascii_strtoull (argv[1], NULL, 10);
	histogram->three_star_count = g_ascii_strtoull (argv[2], NULL, 10);
	histogram->four_star_count = g_ascii_strtoull (argv[3], NULL, 10);
	histogram->five_star_count = g_ascii_strtoull (argv[4], NULL, 10);
	return 0;
}

static gboolean
get_review_stats (GsPlugin *plugin,
		  const gchar *package_name,
		  gint *rating,
		  gint *review_ratings,
		  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	Histogram histogram = { 0, 0, 0, 0, 0 };
	gchar *error_msg = NULL;
	gint result;
	g_autofree gchar *statement = NULL;

	/* Get histogram from the database */
	statement = g_strdup_printf ("SELECT one_star_count, two_star_count, three_star_count, four_star_count, five_star_count FROM review_stats "
				     "WHERE package_name = '%s'", package_name);
	result = sqlite3_exec (priv->db,
			       statement,
			       get_review_stats_sqlite_cb,
			       &histogram,
			       &error_msg);
	if (result != SQLITE_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "SQL error: %s", error_msg);
		sqlite3_free (error_msg);
		return FALSE;
	}

	*rating = gs_utils_get_wilson_rating (histogram.one_star_count,
					      histogram.two_star_count,
					      histogram.three_star_count,
					      histogram.four_star_count,
					      histogram.five_star_count);
	review_ratings[0] = 0;
	review_ratings[1] = (gint) histogram.one_star_count;
	review_ratings[2] = (gint) histogram.two_star_count;
	review_ratings[3] = (gint) histogram.three_star_count;
	review_ratings[4] = (gint) histogram.four_star_count;
	review_ratings[5] = (gint) histogram.five_star_count;

	return TRUE;
}

static gboolean
parse_histogram (const gchar *text, Histogram *histogram)
{
	g_autoptr(JsonParser) parser = NULL;
	JsonArray *array;

	/* Histogram is a five element JSON array, e.g. "[1, 3, 5, 8, 4]" */
	parser = json_parser_new ();
	if (!json_parser_load_from_data (parser, text, -1, NULL))
		return FALSE;
	if (!JSON_NODE_HOLDS_ARRAY (json_parser_get_root (parser)))
		return FALSE;
	array = json_node_get_array (json_parser_get_root (parser));
	if (json_array_get_length (array) != 5)
		return FALSE;
	histogram->one_star_count = (guint64) json_array_get_int_element (array, 0);
	histogram->two_star_count = (guint64) json_array_get_int_element (array, 1);
	histogram->three_star_count = (guint64) json_array_get_int_element (array, 2);
	histogram->four_star_count = (guint64) json_array_get_int_element (array, 3);
	histogram->five_star_count = (guint64) json_array_get_int_element (array, 4);

	return TRUE;
}

static gboolean
parse_review_entry (JsonNode *node, const gchar **package_name, Histogram *histogram)
{
	JsonObject *object;
	const gchar *name = NULL, *histogram_text = NULL;

	if (!JSON_NODE_HOLDS_OBJECT (node))
		return FALSE;

	object = json_node_get_object (node);

	name = json_object_get_string_member (object, "package_name");
	histogram_text = json_object_get_string_member (object, "histogram");
	if (!name || !histogram_text)
		return FALSE;

	if (!parse_histogram (histogram_text, histogram))
		return FALSE;
	*package_name = name;

	return TRUE;
}

static gboolean
parse_review_entries (GsPlugin *plugin, JsonParser *parser, GError **error)
{
	JsonArray *array;
	guint i;

	if (!JSON_NODE_HOLDS_ARRAY (json_parser_get_root (parser)))
		return FALSE;
	array = json_node_get_array (json_parser_get_root (parser));
	for (i = 0; i < json_array_get_length (array); i++) {
		const gchar *package_name;
		Histogram histogram;

		/* Read in from JSON... (skip bad entries) */
		if (!parse_review_entry (json_array_get_element (array, i), &package_name, &histogram))
			continue;

		/* ...write into the database (abort everything if can't write) */
		if (!set_package_stats (plugin, package_name, &histogram, error))
			return FALSE;
	}

	return TRUE;
}

static gboolean
get_ubuntuone_token (GsPlugin *plugin,
		     gchar **consumer_key, gchar **consumer_secret,
		     gchar **token_key, gchar **token_secret,
		     GCancellable *cancellable, GError **error)
{
	GsAuth *auth = gs_plugin_get_auth_by_id (plugin, "ubuntuone");
	if (auth == NULL) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "No UbuntuOne authentication provider");
		return FALSE;
	}
	*consumer_key = g_strdup (gs_auth_get_metadata_item (auth, "consumer-key"));
	*consumer_secret = g_strdup (gs_auth_get_metadata_item (auth, "consumer-secret"));
	*token_key = g_strdup (gs_auth_get_metadata_item (auth, "token-key"));
	*token_secret = g_strdup (gs_auth_get_metadata_item (auth, "token-secret"));
	return TRUE;
}

static void
sign_message (SoupMessage *message, OAuthMethod method,
	      const gchar *consumer_key, const gchar *consumer_secret,
	      const gchar *token_key, const gchar *token_secret)
{
	g_autofree gchar *url = NULL, *oauth_authorization_parameters = NULL, *authorization_text = NULL;
	gchar **url_parameters = NULL;
	int url_parameters_length;

	url = soup_uri_to_string (soup_message_get_uri (message), FALSE);

	url_parameters_length = oauth_split_url_parameters(url, &url_parameters);
	oauth_sign_array2_process (&url_parameters_length, &url_parameters,
				   NULL,
				   method,
				   message->method,
				   consumer_key, consumer_secret,
				   token_key, token_secret);
	oauth_authorization_parameters = oauth_serialize_url_sep (url_parameters_length, 1, url_parameters, ", ", 6);
	oauth_free_array (&url_parameters_length, &url_parameters);
	authorization_text = g_strdup_printf ("OAuth realm=\"Ratings and Reviews\", %s",
					      oauth_authorization_parameters);
	soup_message_headers_append (message->request_headers, "Authorization", authorization_text);
}

static gboolean
send_review_request (GsPlugin *plugin,
		     const gchar *method, const gchar *path,
		     JsonBuilder *request,
		     gboolean do_sign,
		     guint *status_code,
		     JsonParser **result,
		     GCancellable *cancellable, GError **error)
{
	g_autofree gchar *consumer_key = NULL;
	g_autofree gchar *consumer_secret = NULL;
	g_autofree gchar *token_key = NULL;
	g_autofree gchar *token_secret = NULL;
	g_autofree gchar *uri = NULL;
	g_autoptr(SoupMessage) msg = NULL;

	if (do_sign && !get_ubuntuone_token (plugin,
					     &consumer_key, &consumer_secret,
					     &token_key, &token_secret,
					     cancellable, NULL)) {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_AUTH_REQUIRED,
				     "Requires authentication with @ubuntuone");
		return FALSE;
	}

	uri = g_strdup_printf ("%s%s",
			       UBUNTU_REVIEWS_SERVER, path);
	msg = soup_message_new (method, uri);

	if (request != NULL) {
		g_autoptr(JsonGenerator) generator = NULL;
		gchar *data;
		gsize length;

		generator = json_generator_new ();
		json_generator_set_root (generator, json_builder_get_root (request));
		data = json_generator_to_data (generator, &length);
		soup_message_set_request (msg, "application/json", SOUP_MEMORY_TAKE, data, length);
	}

	if (do_sign)
		sign_message (msg,
			      OA_PLAINTEXT,
			      consumer_key, consumer_secret,
			      token_key, token_secret);

	*status_code = soup_session_send_message (gs_plugin_get_soup_session (plugin), msg);

	if (result != NULL) {
		g_autoptr(JsonParser) parser = NULL;
		const gchar *content_type;

		content_type = soup_message_headers_get_content_type (msg->response_headers, NULL);
		if (g_strcmp0 (content_type, "application/json") != 0) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "Got unknown content type %s from reviews.ubuntu.com",
				     content_type);
			return FALSE;
		}

		parser = json_parser_new ();
		if (!json_parser_load_from_data (parser, msg->response_body->data, -1, error)) {
			return FALSE;
		}
		*result = g_steal_pointer (&parser);
	}

	return TRUE;
}

static gboolean
download_review_stats (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	g_autofree gchar *uri = NULL;
	g_autoptr(SoupMessage) msg = NULL;
	guint status_code;
	g_autoptr(JsonParser) result = NULL;

	if (!send_review_request (plugin, SOUP_METHOD_GET, "/api/1.0/review-stats/any/any/",
				  NULL, FALSE,
				  &status_code, &result, cancellable, error))
		return FALSE;

	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_DOWNLOAD_FAILED,
			     "Failed to download review stats, server returned status code %u",
			     status_code);
		return FALSE;
	}

	/* Extract the stats from the data */
	if (!parse_review_entries (plugin, result, error))
		return FALSE;

	/* Record the time we downloaded it */
	return set_timestamp (plugin, "stats_mtime", error);
}

static gboolean
load_database (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *statement;
	gboolean rebuild_ratings = FALSE;
	char *error_msg = NULL;
	gint result;
	gint64 stats_mtime = 0;
	gint64 now;
	g_autoptr(GError) error_local = NULL;

	g_debug ("trying to open database '%s'", priv->db_path);
	if (!gs_mkdir_parent (priv->db_path, error))
		return FALSE;
	result = sqlite3_open (priv->db_path, &priv->db);
	if (result != SQLITE_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Can't open Ubuntu review statistics database: %s",
			     sqlite3_errmsg (priv->db));
		return FALSE;
	}

	/* We don't need to keep doing fsync */
	sqlite3_exec (priv->db, "PRAGMA synchronous=OFF",
		      NULL, NULL, NULL);

	/* Create a table to store the stats */
	result = sqlite3_exec (priv->db, "SELECT * FROM review_stats LIMIT 1", NULL, NULL, &error_msg);
	if (result != SQLITE_OK) {
		g_debug ("creating table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "CREATE TABLE review_stats ("
			    "package_name TEXT PRIMARY KEY,"
			    "one_star_count INTEGER DEFAULT 0,"
			    "two_star_count INTEGER DEFAULT 0,"
			    "three_star_count INTEGER DEFAULT 0,"
			    "four_star_count INTEGER DEFAULT 0,"
			    "five_star_count INTEGER DEFAULT 0);";
		sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
		rebuild_ratings = TRUE;
	}

	/* Create a table to store local reviews */
	result = sqlite3_exec (priv->db, "SELECT * FROM reviews LIMIT 1", NULL, NULL, &error_msg);
	if (result != SQLITE_OK) {
		g_debug ("creating table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "CREATE TABLE reviews ("
			    "package_name TEXT PRIMARY KEY,"
			    "id TEXT,"
			    "version TEXT,"
			    "date TEXT,"
			    "rating INTEGER,"
			    "summary TEXT,"
			    "text TEXT);";
		sqlite3_exec (priv->db, statement, NULL, NULL, NULL);
		rebuild_ratings = TRUE;
	}

	/* Create a table to store timestamps */
	result = sqlite3_exec (priv->db,
			       "SELECT value FROM timestamps WHERE key = 'stats_mtime' LIMIT 1",
			       get_timestamp_sqlite_cb, &stats_mtime,
			       &error_msg);
	if (result != SQLITE_OK) {
		g_debug ("creating table to repair: %s", error_msg);
		sqlite3_free (error_msg);
		statement = "CREATE TABLE timestamps ("
			    "key TEXT PRIMARY KEY,"
			    "value INTEGER DEFAULT 0);";
		sqlite3_exec (priv->db, statement, NULL, NULL, NULL);

		/* Set the time of database creation */
		if (!set_timestamp (plugin, "stats_ctime", error))
			return FALSE;
	}

	/* Download data if we have none or it is out of date */
	now = g_get_real_time () / G_USEC_PER_SEC;
	if (stats_mtime == 0 || rebuild_ratings) {
		g_debug ("No Ubuntu review statistics");
		if (!download_review_stats (plugin, cancellable, &error_local)) {
			g_warning ("Failed to get Ubuntu review statistics: %s",
				   error_local->message);
			return TRUE;
		}
	} else if (now - stats_mtime > REVIEW_STATS_AGE_MAX) {
		g_debug ("Ubuntu review statistics was %" G_GINT64_FORMAT
			 " days old, so regetting",
			 (now - stats_mtime) / ( 60 * 60 * 24));
		if (!download_review_stats (plugin, cancellable, error))
			return FALSE;
	} else {
		g_debug ("Ubuntu review statistics %" G_GINT64_FORMAT
			 " days old, so no need to redownload",
			 (now - stats_mtime) / ( 60 * 60 * 24));
	}
	return TRUE;
}

static GDateTime *
parse_date_time (const gchar *text)
{
	const gchar *format = "YYYY-MM-DD HH:MM:SS";
	int i, value_index, values[6] = { 0, 0, 0, 0, 0, 0 };

	if (!text)
		return NULL;

	/* Extract the numbers as shown in the format */
	for (i = 0, value_index = 0; text[i] && format[i] && value_index < 6; i++) {
		char c = text[i];

		if (c == '-' || c == ' ' || c == ':') {
			if (format[i] != c)
				return NULL;
			value_index++;
		} else {
			int d = c - '0';
			if (d < 0 || d > 9)
				return NULL;
			values[value_index] = values[value_index] * 10 + d;
		}
	}

	/* We didn't match the format */
	if (format[i] != '\0' || text[i] != '\0' || value_index != 5)
		return NULL;

	/* Use the numbers to create a GDateTime object */
	return g_date_time_new_utc (values[0], values[1], values[2], values[3], values[4], values[5]);
}

static gboolean
parse_review (AsReview *review, const gchar *our_username, JsonNode *node)
{
	JsonObject *object;
	gint64 star_rating;
	g_autofree gchar *id_string = NULL;

	if (!JSON_NODE_HOLDS_OBJECT (node))
		return FALSE;

	object = json_node_get_object (node);

	if (g_strcmp0 (our_username, json_object_get_string_member (object, "reviewer_username")) == 0)
		as_review_add_flags (review, AS_REVIEW_FLAG_SELF);
	as_review_set_reviewer_name (review, json_object_get_string_member (object, "reviewer_displayname"));
	as_review_set_summary (review, json_object_get_string_member (object, "summary"));
	as_review_set_description (review, json_object_get_string_member (object, "review_text"));
	as_review_set_version (review, json_object_get_string_member (object, "version"));
	star_rating = json_object_get_int_member (object, "rating");
	if (star_rating > 0)
		as_review_set_rating (review, (gint) (star_rating * 20 - 10));
	as_review_set_date (review, parse_date_time (json_object_get_string_member (object, "date_created")));
	id_string = g_strdup_printf ("%" G_GINT64_FORMAT, json_object_get_int_member (object, "id"));
	as_review_add_metadata (review, "ubuntu-id", id_string);

	return TRUE;
}

static gboolean
parse_reviews (GsPlugin *plugin, JsonParser *parser, GsApp *app, GCancellable *cancellable, GError **error)
{
	GsAuth *auth;
	JsonArray *array;
	const gchar *consumer_key = NULL;
	guint i;

	auth = gs_plugin_get_auth_by_id (plugin, "ubuntuone");
	if (auth != NULL)
		consumer_key = gs_auth_get_metadata_item (auth, "consumer-key");

	if (!JSON_NODE_HOLDS_ARRAY (json_parser_get_root (parser)))
		return FALSE;
	array = json_node_get_array (json_parser_get_root (parser));
	for (i = 0; i < json_array_get_length (array); i++) {
		g_autoptr(AsReview) review = NULL;

		/* Read in from JSON... (skip bad entries) */
		review = as_review_new ();
		if (parse_review (review, consumer_key, json_array_get_element (array, i)))
			gs_app_add_review (app, review);
	}

	return TRUE;
}

static gboolean
download_reviews (GsPlugin *plugin, GsApp *app,
		  const gchar *package_name, guint page_number,
		  GCancellable *cancellable, GError **error)
{
	const gchar *language;
	guint status_code;
	g_autofree gchar *path = NULL;
	g_autoptr(JsonParser) result = NULL;

	/* Get the review stats using HTTP */
	language = gs_plugin_get_language (plugin);
	path = g_strdup_printf ("/api/1.0/reviews/filter/%s/any/any/any/%s/page/%u/",
				language, package_name, page_number + 1);
	if (!send_review_request (plugin, SOUP_METHOD_GET, path,
				  NULL, FALSE,
				  &status_code, &result, cancellable, error))
		return FALSE;

	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to download reviews, server returned status code %u",
			     status_code);
		return FALSE;
	}

	/* Extract the stats from the data */
	return parse_reviews (plugin, result, app, cancellable, error);
}

static gboolean
refine_rating (GsPlugin *plugin, GsApp *app, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GPtrArray *sources;
	guint i;

	/* Load database once */
	if (g_once_init_enter (&priv->db_loaded)) {
		gboolean ret = load_database (plugin, cancellable, error);
		g_once_init_leave (&priv->db_loaded, TRUE);
		if (!ret)
			return FALSE;
	}

	/* Skip if already has a rating */
	if (gs_app_get_rating (app) != -1)
		return TRUE;

	sources = gs_app_get_sources (app);
	for (i = 0; i < sources->len; i++) {
		const gchar *package_name;
		gint rating;
		gint review_ratings[6];
		gboolean ret;

		/* Otherwise use the statistics */
		package_name = g_ptr_array_index (sources, i);
		ret = get_review_stats (plugin, package_name, &rating, review_ratings, error);
		if (!ret)
			return FALSE;
		if (rating != -1) {
			g_autoptr(GArray) ratings = NULL;

			g_debug ("ubuntu-reviews setting rating on %s to %i%%",
				 package_name, rating);
			gs_app_set_rating (app, rating);
			ratings = g_array_sized_new (FALSE, FALSE, sizeof (gint), 6);
			g_array_append_vals (ratings, review_ratings, 6);
			gs_app_set_review_ratings (app, ratings);
			if (rating > 80)
				gs_app_add_kudo (app, GS_APP_KUDO_POPULAR);
		}
	}

	return TRUE;
}

static gboolean
refine_reviews (GsPlugin *plugin, GsApp *app, GCancellable *cancellable, GError **error)
{
	GPtrArray *sources;
	guint i, j;

	/* Skip if already has reviews */
	if (gs_app_get_reviews (app)->len > 0)
		return TRUE;

	sources = gs_app_get_sources (app);
	for (i = 0; i < sources->len; i++) {
		const gchar *package_name;

		package_name = g_ptr_array_index (sources, i);
		for (j = 0; j < N_PAGES; j++) {
			gboolean ret;

			ret = download_reviews (plugin, app, package_name, j, cancellable, error);
			if (!ret)
				return FALSE;
		}
	}

	return TRUE;
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	if ((flags & (GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING | GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS)) != 0) {
		if (!refine_rating (plugin, app, cancellable, error))
			return FALSE;
	}
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS) != 0) {
		if (!refine_reviews (plugin, app, cancellable, error))
			return FALSE;
	}

	return TRUE;
}

static void
add_string_member (JsonBuilder *builder, const gchar *name, const gchar *value)
{
	json_builder_set_member_name (builder, name);
	json_builder_add_string_value (builder, value);
}

static void
add_int_member (JsonBuilder *builder, const gchar *name, gint64 value)
{
	json_builder_set_member_name (builder, name);
	json_builder_add_int_value (builder, value);
}

gboolean
gs_plugin_review_submit (GsPlugin *plugin,
			 GsApp *app,
			 AsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GsAuth *auth;
	const gchar *consumer_key = NULL;
	const gchar *language;
	gint rating;
	gint n_stars;
	g_autofree gchar *architecture = NULL;
	g_autoptr(JsonBuilder) request = NULL;
	guint status_code;
	g_autoptr(JsonParser) result = NULL;

	/* Ubuntu reviews require a summary and description - just make one up for now */
	rating = as_review_get_rating (review);
	if (rating > 80)
		n_stars = 5;
	else if (rating > 60)
		n_stars = 4;
	else if (rating > 40)
		n_stars = 3;
	else if (rating > 20)
		n_stars = 2;
	else
		n_stars = 1;

	language = gs_plugin_get_language (plugin);

	// FIXME: Need to get Apt::Architecture configuration value from APT
	architecture = g_strdup ("amd64");

	/* Create message for reviews.ubuntu.com */
	request = json_builder_new ();
	json_builder_begin_object (request);
	add_string_member (request, "package_name", gs_app_get_source_default (app));
	add_string_member (request, "summary", as_review_get_summary (review));
	add_string_member (request, "review_text", as_review_get_description (review));
	add_string_member (request, "language", language);
	add_string_member (request, "origin", priv->origin);
	add_string_member (request, "distroseries", priv->distroseries);
	add_string_member (request, "version", as_review_get_version (review));
	add_int_member (request, "rating", n_stars);
	add_string_member (request, "arch_tag", architecture);
	json_builder_end_object (request);

	if (!send_review_request (plugin, SOUP_METHOD_POST, "/api/1.0/reviews/",
				  request, TRUE,
				  &status_code, &result, cancellable, error))
		return FALSE;

	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to submit review, server returned status code %u",
			     status_code);
		return FALSE;
	}

	// Extract new fields from posted review
	auth = gs_plugin_get_auth_by_id (plugin, "ubuntuone");
	if (auth != NULL)
		consumer_key = gs_auth_get_metadata_item (auth, "consumer-key");
	parse_review (review, consumer_key, json_parser_get_root (result));

	return TRUE;
}

gboolean
gs_plugin_review_report (GsPlugin *plugin,
			 GsApp *app,
			 AsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	const gchar *review_id;
	g_autofree gchar *reason = NULL;
	g_autofree gchar *text = NULL;
	g_autofree gchar *path = NULL;
	guint status_code;

	/* Can only modify Ubuntu reviews */
	review_id = as_review_get_metadata_item (review, "ubuntu-id");
	if (review_id == NULL)
		return TRUE;

	/* Create message for reviews.ubuntu.com */
	reason = soup_uri_encode ("FIXME: gnome-software", NULL);
	text = soup_uri_encode ("FIXME: gnome-software", NULL);
	path = g_strdup_printf ("/api/1.0/reviews/%s/recommendations/?reason=%s&text=%s",
				review_id, reason, text);
	if (!send_review_request (plugin, SOUP_METHOD_POST, path,
				  NULL, TRUE,
				  &status_code, NULL, cancellable, error))
		return FALSE;

	if (status_code != SOUP_STATUS_CREATED) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to report review, server returned status code %u",
			     status_code);
		return FALSE;
	}

	as_review_add_flags (review, AS_REVIEW_FLAG_VOTED);
	return TRUE;
}

static gboolean
set_review_usefulness (GsPlugin *plugin,
		       const gchar *review_id,
		       gboolean is_useful,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autofree gchar *path = NULL;
	guint status_code;

	/* Create message for reviews.ubuntu.com */
	path = g_strdup_printf ("/api/1.0/reviews/%s/recommendations/?useful=%s",
				review_id, is_useful ? "True" : "False");
	if (!send_review_request (plugin, SOUP_METHOD_POST, path,
				  NULL, TRUE,
				  &status_code, NULL, cancellable, error))
		return FALSE;

	if (status_code != SOUP_STATUS_CREATED) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Got status code %u from reviews.ubuntu.com",
			     status_code);
		return FALSE;
	}

	return TRUE;
}

gboolean
gs_plugin_review_upvote (GsPlugin *plugin,
			 GsApp *app,
			 AsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	const gchar *review_id;

	/* Can only modify Ubuntu reviews */
	review_id = as_review_get_metadata_item (review, "ubuntu-id");
	if (review_id == NULL)
		return TRUE;

	if (!set_review_usefulness (plugin, review_id, TRUE, cancellable, error))
		return FALSE;

	as_review_add_flags (review, AS_REVIEW_FLAG_VOTED);
	return TRUE;
}

gboolean
gs_plugin_review_downvote (GsPlugin *plugin,
			   GsApp *app,
			   AsReview *review,
			   GCancellable *cancellable,
			   GError **error)
{
	const gchar *review_id;

	/* Can only modify Ubuntu reviews */
	review_id = as_review_get_metadata_item (review, "ubuntu-id");
	if (review_id == NULL)
		return TRUE;

	if (!set_review_usefulness (plugin, review_id, FALSE, cancellable, error))
		return FALSE;

	as_review_add_flags (review, AS_REVIEW_FLAG_VOTED);
	return TRUE;
}

gboolean
gs_plugin_review_remove (GsPlugin *plugin,
			 GsApp *app,
			 AsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	const gchar *review_id;
	g_autofree gchar *path = NULL;
	guint status_code;

	/* Can only modify Ubuntu reviews */
	review_id = as_review_get_metadata_item (review, "ubuntu-id");
	if (review_id == NULL)
		return TRUE;

	/* Create message for reviews.ubuntu.com */
	path = g_strdup_printf ("/api/1.0/reviews/delete/%s/", review_id);
	if (!send_review_request (plugin, SOUP_METHOD_POST, path,
				  NULL, TRUE,
				  &status_code, NULL, cancellable, error))
		return FALSE;

	if (status_code != SOUP_STATUS_OK) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to remove review, server returned status code %u",
			     status_code);
		return FALSE;
	}

	return TRUE;
}
