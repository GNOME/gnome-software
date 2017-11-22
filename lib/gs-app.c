/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
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
 * SECTION:gs-app
 * @title: GsApp
 * @include: gnome-software.h
 * @stability: Unstable
 * @short_description: An application that is either installed or that can be installed
 *
 * This object represents a 1:1 mapping to a .desktop file. The design is such
 * so you can't have different GsApp's for different versions or architectures
 * of a package. This rule really only applies to GsApps of kind %AS_APP_KIND_DESKTOP
 * and %AS_APP_KIND_GENERIC. We allow GsApps of kind %AS_APP_KIND_OS_UPDATE or
 * %AS_APP_KIND_GENERIC, which don't correspond to desktop files, but instead
 * represent a system update and its individual components.
 *
 * The #GsPluginLoader de-duplicates the GsApp instances that are produced by
 * plugins to ensure that there is a single instance of GsApp for each id, making
 * the id the primary key for this object. This ensures that actions triggered on
 * a #GsApp in different parts of gnome-software can be observed by connecting to
 * signals on the #GsApp.
 *
 * Information about other #GsApp objects can be stored in this object, for
 * instance in the gs_app_add_related() method or gs_app_get_history().
 */

#include "config.h"

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "gs-app-collation.h"
#include "gs-app-private.h"
#include "gs-plugin.h"
#include "gs-utils.h"

typedef struct
{
	GObject			 parent_instance;

	GMutex			 mutex;
	gchar			*id;
	gchar			*unique_id;
	gboolean		 unique_id_valid;
	gchar			*branch;
	gchar			*name;
	GsAppQuality		 name_quality;
	GPtrArray		*icons;
	GPtrArray		*sources;
	GPtrArray		*source_ids;
	gchar			*project_group;
	gchar			*developer_name;
	gchar			*agreement;
	gchar			*version;
	gchar			*version_ui;
	gchar			*summary;
	GsAppQuality		 summary_quality;
	gchar			*summary_missing;
	gchar			*description;
	GsAppQuality		 description_quality;
	GPtrArray		*screenshots;
	GPtrArray		*categories;
	GPtrArray		*key_colors;
	GPtrArray		*keywords;
	GHashTable		*urls;
	GHashTable		*launchables;
	gchar			*license;
	GsAppQuality		 license_quality;
	gchar			**menu_path;
	gchar			*origin;
	gchar			*origin_appstream;
	gchar			*origin_hostname;
	gchar			*update_version;
	gchar			*update_version_ui;
	gchar			*update_details;
	AsUrgencyKind		 update_urgency;
	gchar			*management_plugin;
	guint			 match_value;
	guint			 priority;
	gint			 rating;
	GArray			*review_ratings;
	GPtrArray		*reviews; /* of AsReview */
	GPtrArray		*provides; /* of AsProvide */
	guint64			 size_installed;
	guint64			 size_download;
	AsAppKind		 kind;
	AsAppState		 state;
	AsAppState		 state_recover;
	AsAppScope		 scope;
	AsBundleKind		 bundle_kind;
	guint			 progress;
	gboolean		 allow_cancel;
	GHashTable		*metadata;
	GsAppList		*addons;
	GsAppList		*related;
	GsAppList		*history;
	guint64			 install_date;
	guint64			 kudos;
	gboolean		 to_be_installed;
	AsAppQuirk		 quirk;
	gboolean		 license_is_free;
	GsApp			*runtime;
	GFile			*local_file;
	AsContentRating		*content_rating;
	GdkPixbuf		*pixbuf;
	GsPrice			*price;
	GPtrArray		*channels;
	GsChannel		*active_channel;
	GCancellable		*cancellable;
	GsPluginAction		 pending_action;
} GsAppPrivate;

enum {
	PROP_0,
	PROP_ID,
	PROP_NAME,
	PROP_VERSION,
	PROP_SUMMARY,
	PROP_DESCRIPTION,
	PROP_RATING,
	PROP_KIND,
	PROP_STATE,
	PROP_PROGRESS,
	PROP_CAN_CANCEL_INSTALLATION,
	PROP_INSTALL_DATE,
	PROP_QUIRK,
	PROP_PENDING_ACTION,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (GsApp, gs_app, G_TYPE_OBJECT)

static gboolean
_g_set_str (gchar **str_ptr, const gchar *new_str)
{
	if (*str_ptr == new_str || g_strcmp0 (*str_ptr, new_str) == 0)
		return FALSE;
	g_free (*str_ptr);
	*str_ptr = g_strdup (new_str);
	return TRUE;
}

static gboolean
_g_set_strv (gchar ***strv_ptr, gchar **new_strv)
{
	if (*strv_ptr == new_strv)
		return FALSE;
	g_strfreev (*strv_ptr);
	*strv_ptr = g_strdupv (new_strv);
	return TRUE;
}

static gboolean
_g_set_ptr_array (GPtrArray **array_ptr, GPtrArray *new_array)
{
	if (*array_ptr == new_array)
		return FALSE;
	if (*array_ptr != NULL)
		g_ptr_array_unref (*array_ptr);
	*array_ptr = g_ptr_array_ref (new_array);
	return TRUE;
}

static gboolean
_g_set_array (GArray **array_ptr, GArray *new_array)
{
	if (*array_ptr == new_array)
		return FALSE;
	if (*array_ptr != NULL)
		g_array_unref (*array_ptr);
	*array_ptr = g_array_ref (new_array);
	return TRUE;
}

static void
gs_app_kv_lpad (GString *str, const gchar *key, const gchar *value)
{
	gs_utils_append_key_value (str, 20, key, value);
}

static void
gs_app_kv_size (GString *str, const gchar *key, guint64 value)
{
	g_autofree gchar *tmp = NULL;
	if (value == GS_APP_SIZE_UNKNOWABLE) {
		gs_app_kv_lpad (str, key, "unknowable");
		return;
	}
	tmp = g_format_size (value);
	gs_app_kv_lpad (str, key, tmp);
}

G_GNUC_PRINTF (3, 4)
static void
gs_app_kv_printf (GString *str, const gchar *key, const gchar *fmt, ...)
{
	va_list args;
	g_autofree gchar *tmp = NULL;
	va_start (args, fmt);
	tmp = g_strdup_vprintf (fmt, args);
	va_end (args);
	gs_app_kv_lpad (str, key, tmp);
}

static const gchar *
_as_app_quirk_flag_to_string (AsAppQuirk quirk)
{
	if (quirk == AS_APP_QUIRK_PROVENANCE)
		return "provenance";
	if (quirk == AS_APP_QUIRK_COMPULSORY)
		return "compulsory";
	if (quirk == AS_APP_QUIRK_HAS_SOURCE)
		return "has-source";
	if (quirk == AS_APP_QUIRK_MATCH_ANY_PREFIX)
		return "match-any-prefix";
	if (quirk == AS_APP_QUIRK_NEEDS_REBOOT)
		return "needs-reboot";
	if (quirk == AS_APP_QUIRK_NOT_REVIEWABLE)
		return "not-reviewable";
	if (quirk == AS_APP_QUIRK_HAS_SHORTCUT)
		return "has-shortcut";
	if (quirk == AS_APP_QUIRK_NOT_LAUNCHABLE)
		return "not-launchable";
	if (quirk == AS_APP_QUIRK_NEEDS_USER_ACTION)
		return "needs-user-action";
	if (quirk == AS_APP_QUIRK_IS_PROXY)
		return "is-proxy";
	if (quirk == AS_APP_QUIRK_REMOVABLE_HARDWARE)
		return "removable-hardware";
	return NULL;
}

/* mutex must be held */
static const gchar *
gs_app_get_unique_id_unlocked (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);

	/* invalid */
	if (priv->id == NULL)
		return NULL;

	/* hmm, do what we can */
	if (priv->unique_id == NULL || !priv->unique_id_valid) {
		g_debug ("autogenerating unique-id for %s", priv->id);
		g_free (priv->unique_id);
		priv->unique_id = as_utils_unique_id_build (priv->scope,
							    priv->bundle_kind,
							    priv->origin,
							    priv->kind,
							    priv->id,
							    priv->branch);
		priv->unique_id_valid = TRUE;
	}
	return priv->unique_id;
}

/**
 * _as_app_quirk_to_string:
 * @quirk: a #AsAppQuirk
 *
 * Returns the quirk bitfield as a string.
 *
 * Returns: (transfer full): a string
 **/
static gchar *
_as_app_quirk_to_string (AsAppQuirk quirk)
{
	GString *str = g_string_new ("");
	guint64 i;

	/* nothing set */
	if (quirk == AS_APP_QUIRK_NONE) {
		g_string_append (str, "none");
		return g_string_free (str, FALSE);
	}

	/* get flags */
	for (i = 1; i < AS_APP_QUIRK_LAST; i *= 2) {
		if ((quirk & i) == 0)
			continue;
		g_string_append_printf (str, "%s,",
					_as_app_quirk_flag_to_string (i));
	}

	/* nothing recognised */
	if (str->len == 0) {
		g_string_append (str, "unknown");
		return g_string_free (str, FALSE);
	}

	/* remove trailing comma */
	g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

static gchar *
gs_app_kudos_to_string (guint64 kudos)
{
	g_autoptr(GPtrArray) array = g_ptr_array_new ();
	if ((kudos & GS_APP_KUDO_MY_LANGUAGE) > 0)
		g_ptr_array_add (array, "my-language");
	if ((kudos & GS_APP_KUDO_RECENT_RELEASE) > 0)
		g_ptr_array_add (array, "recent-release");
	if ((kudos & GS_APP_KUDO_FEATURED_RECOMMENDED) > 0)
		g_ptr_array_add (array, "featured-recommended");
	if ((kudos & GS_APP_KUDO_MODERN_TOOLKIT) > 0)
		g_ptr_array_add (array, "modern-toolkit");
	if ((kudos & GS_APP_KUDO_SEARCH_PROVIDER) > 0)
		g_ptr_array_add (array, "search-provider");
	if ((kudos & GS_APP_KUDO_INSTALLS_USER_DOCS) > 0)
		g_ptr_array_add (array, "installs-user-docs");
	if ((kudos & GS_APP_KUDO_USES_NOTIFICATIONS) > 0)
		g_ptr_array_add (array, "uses-notifications");
	if ((kudos & GS_APP_KUDO_USES_APP_MENU) > 0)
		g_ptr_array_add (array, "uses-app-menu");
	if ((kudos & GS_APP_KUDO_HAS_KEYWORDS) > 0)
		g_ptr_array_add (array, "has-keywords");
	if ((kudos & GS_APP_KUDO_HAS_SCREENSHOTS) > 0)
		g_ptr_array_add (array, "has-screenshots");
	if ((kudos & GS_APP_KUDO_POPULAR) > 0)
		g_ptr_array_add (array, "popular");
	if ((kudos & GS_APP_KUDO_PERFECT_SCREENSHOTS) > 0)
		g_ptr_array_add (array, "perfect-screenshots");
	if ((kudos & GS_APP_KUDO_HIGH_CONTRAST) > 0)
		g_ptr_array_add (array, "high-contrast");
	if ((kudos & GS_APP_KUDO_HI_DPI_ICON) > 0)
		g_ptr_array_add (array, "hi-dpi-icon");
	if ((kudos & GS_APP_KUDO_SANDBOXED) > 0)
		g_ptr_array_add (array, "sandboxed");
	if ((kudos & GS_APP_KUDO_SANDBOXED_SECURE) > 0)
		g_ptr_array_add (array, "sandboxed-secure");
	g_ptr_array_add (array, NULL);
	return g_strjoinv ("|", (gchar **) array->pdata);
}

/**
 * gs_app_to_string:
 * @app: a #GsApp
 *
 * Converts the application to a string.
 * This is not designed to serialize the object but to produce a string suitable
 * for debugging.
 *
 * Returns: A multi-line string
 *
 * Since: 3.22
 **/
gchar *
gs_app_to_string (GsApp *app)
{
	GString *str = g_string_new ("GsApp:");
	gs_app_to_string_append (app, str);
	if (str->len > 0)
		g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

/**
 * gs_app_to_string_append:
 * @app: a #GsApp
 * @str: a #GString
 *
 * Appends the application to an existing string.
 *
 * Since: 3.26
 **/
void
gs_app_to_string_append (GsApp *app, GString *str)
{
	GsAppClass *klass = GS_APP_GET_CLASS (app);
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	AsImage *im;
	GList *keys;
	const gchar *tmp;
	guint i;

	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (str != NULL);

	g_string_append_printf (str, " [%p]\n", app);
	gs_app_kv_lpad (str, "kind", as_app_kind_to_string (priv->kind));
	gs_app_kv_lpad (str, "state", as_app_state_to_string (priv->state));
	if (priv->quirk > 0) {
		g_autofree gchar *qstr = _as_app_quirk_to_string (priv->quirk);
		gs_app_kv_lpad (str, "quirk", qstr);
	}
	if (priv->progress > 0)
		gs_app_kv_printf (str, "progress", "%u%%", priv->progress);
	if (priv->id != NULL)
		gs_app_kv_lpad (str, "id", priv->id);
	if (priv->unique_id != NULL)
		gs_app_kv_lpad (str, "unique-id", gs_app_get_unique_id (app));
	if (priv->scope != AS_APP_SCOPE_UNKNOWN)
		gs_app_kv_lpad (str, "scope", as_app_scope_to_string (priv->scope));
	if (priv->bundle_kind != AS_BUNDLE_KIND_UNKNOWN) {
		gs_app_kv_lpad (str, "bundle-kind",
				as_bundle_kind_to_string (priv->bundle_kind));
	}
	if (priv->kudos > 0) {
		g_autofree gchar *kudo_str = NULL;
		kudo_str = gs_app_kudos_to_string (priv->kudos);
		gs_app_kv_lpad (str, "kudos", kudo_str);
	}
	gs_app_kv_printf (str, "kudo-percentage", "%u",
			  gs_app_get_kudos_percentage (app));
	if (priv->name != NULL)
		gs_app_kv_lpad (str, "name", priv->name);
	if (priv->pixbuf != NULL)
		gs_app_kv_printf (str, "pixbuf", "%p", priv->pixbuf);
	for (i = 0; i < priv->icons->len; i++) {
		AsIcon *icon = g_ptr_array_index (priv->icons, i);
		gs_app_kv_lpad (str, "icon-kind",
				as_icon_kind_to_string (as_icon_get_kind (icon)));
		if (as_icon_get_pixbuf (icon) != NULL) {
			gs_app_kv_printf (str, "icon-pixbuf", "%p",
					  as_icon_get_pixbuf (icon));
		}
		if (as_icon_get_name (icon) != NULL)
			gs_app_kv_lpad (str, "icon-name",
					as_icon_get_name (icon));
		if (as_icon_get_prefix (icon) != NULL)
			gs_app_kv_lpad (str, "icon-prefix",
					as_icon_get_prefix (icon));
		if (as_icon_get_filename (icon) != NULL)
			gs_app_kv_lpad (str, "icon-filename",
					as_icon_get_filename (icon));
	}
	if (priv->match_value != 0)
		gs_app_kv_printf (str, "match-value", "%05x", priv->match_value);
	if (priv->priority != 0)
		gs_app_kv_printf (str, "priority", "%u", priv->priority);
	if (priv->version != NULL)
		gs_app_kv_lpad (str, "version", priv->version);
	if (priv->version_ui != NULL)
		gs_app_kv_lpad (str, "version-ui", priv->version_ui);
	if (priv->update_version != NULL)
		gs_app_kv_lpad (str, "update-version", priv->update_version);
	if (priv->update_version_ui != NULL)
		gs_app_kv_lpad (str, "update-version-ui", priv->update_version_ui);
	if (priv->update_details != NULL)
		gs_app_kv_lpad (str, "update-details", priv->update_details);
	if (priv->update_urgency != AS_URGENCY_KIND_UNKNOWN) {
		gs_app_kv_printf (str, "update-urgency", "%u",
				  priv->update_urgency);
	}
	if (priv->summary != NULL)
		gs_app_kv_lpad (str, "summary", priv->summary);
	if (priv->description != NULL)
		gs_app_kv_lpad (str, "description", priv->description);
	for (i = 0; i < priv->screenshots->len; i++) {
		AsScreenshot *ss = g_ptr_array_index (priv->screenshots, i);
		g_autofree gchar *key = NULL;
		tmp = as_screenshot_get_caption (ss, NULL);
		im = as_screenshot_get_image (ss, 0, 0);
		if (im == NULL)
			continue;
		key = g_strdup_printf ("screenshot-%02u", i);
		gs_app_kv_printf (str, key, "%s [%s]",
				  as_image_get_url (im),
				  tmp != NULL ? tmp : "<none>");
	}
	for (i = 0; i < priv->sources->len; i++) {
		g_autofree gchar *key = NULL;
		tmp = g_ptr_array_index (priv->sources, i);
		key = g_strdup_printf ("source-%02u", i);
		gs_app_kv_lpad (str, key, tmp);
	}
	for (i = 0; i < priv->source_ids->len; i++) {
		g_autofree gchar *key = NULL;
		tmp = g_ptr_array_index (priv->source_ids, i);
		key = g_strdup_printf ("source-id-%02u", i);
		gs_app_kv_lpad (str, key, tmp);
	}
	if (priv->local_file != NULL) {
		g_autofree gchar *fn = g_file_get_path (priv->local_file);
		gs_app_kv_lpad (str, "local-filename", fn);
	}
	if (priv->content_rating != NULL) {
		guint age = as_content_rating_get_minimum_age (priv->content_rating);
		if (age != G_MAXUINT) {
			g_autofree gchar *value = g_strdup_printf ("%u", age);
			gs_app_kv_lpad (str, "content-age", value);
		}
		gs_app_kv_lpad (str, "content-rating",
				as_content_rating_get_kind (priv->content_rating));
	}
	tmp = g_hash_table_lookup (priv->urls, as_url_kind_to_string (AS_URL_KIND_HOMEPAGE));
	if (tmp != NULL)
		gs_app_kv_lpad (str, "url{homepage}", tmp);
	keys = g_hash_table_get_keys (priv->launchables);
	for (GList *l = keys; l != NULL; l = l->next) {
		g_autofree gchar *key = NULL;
		key = g_strdup_printf ("launchable{%s}", (const gchar *) l->data);
		tmp = g_hash_table_lookup (priv->launchables, l->data);
		gs_app_kv_lpad (str, key, tmp);
	}
	g_list_free (keys);
	if (priv->license != NULL) {
		gs_app_kv_lpad (str, "license", priv->license);
		gs_app_kv_lpad (str, "license-is-free",
				gs_app_get_license_is_free (app) ? "yes" : "no");
	}
	if (priv->management_plugin != NULL)
		gs_app_kv_lpad (str, "management-plugin", priv->management_plugin);
	if (priv->summary_missing != NULL)
		gs_app_kv_lpad (str, "summary-missing", priv->summary_missing);
	if (priv->menu_path != NULL &&
	    priv->menu_path[0] != NULL &&
	    priv->menu_path[0][0] != '\0') {
		g_autofree gchar *path = g_strjoinv (" → ", priv->menu_path);
		gs_app_kv_lpad (str, "menu-path", path);
	}
	if (priv->branch != NULL)
		gs_app_kv_lpad (str, "branch", priv->branch);
	if (priv->origin != NULL && priv->origin[0] != '\0')
		gs_app_kv_lpad (str, "origin", priv->origin);
	if (priv->origin_appstream != NULL && priv->origin_appstream[0] != '\0')
		gs_app_kv_lpad (str, "origin-appstream", priv->origin_appstream);
	if (priv->origin_hostname != NULL && priv->origin_hostname[0] != '\0')
		gs_app_kv_lpad (str, "origin-hostname", priv->origin_hostname);
	if (priv->rating != -1)
		gs_app_kv_printf (str, "rating", "%i", priv->rating);
	if (priv->review_ratings != NULL) {
		for (i = 0; i < priv->review_ratings->len; i++) {
			gint rat = g_array_index (priv->review_ratings, gint, i);
			gs_app_kv_printf (str, "review-rating", "[%u:%i]",
					  i, rat);
		}
	}
	if (priv->reviews != NULL)
		gs_app_kv_printf (str, "reviews", "%u", priv->reviews->len);
	if (priv->provides != NULL)
		gs_app_kv_printf (str, "provides", "%u", priv->provides->len);
	if (priv->install_date != 0) {
		gs_app_kv_printf (str, "install-date", "%"
				  G_GUINT64_FORMAT "",
				  priv->install_date);
	}
	if (priv->size_installed != 0)
		gs_app_kv_size (str, "size-installed", priv->size_installed);
	if (priv->size_download != 0)
		gs_app_kv_size (str, "size-download", gs_app_get_size_download (app));
	if (priv->price != NULL)
		gs_app_kv_printf (str, "price", "%s %.2f",
				  gs_price_get_currency (priv->price),
				  gs_price_get_amount (priv->price));
	for (i = 0; i < gs_app_list_length (priv->related); i++) {
		GsApp *app_tmp = gs_app_list_index (priv->related, i);
		const gchar *id = gs_app_get_unique_id (app_tmp);
		if (id == NULL)
			id = gs_app_get_source_default (app_tmp);
		gs_app_kv_lpad (str, "related", id);
	}
	for (i = 0; i < gs_app_list_length (priv->history); i++) {
		GsApp *app_tmp = gs_app_list_index (priv->history, i);
		gs_app_kv_lpad (str, "history", gs_app_get_unique_id (app_tmp));
	}
	for (i = 0; i < priv->categories->len; i++) {
		tmp = g_ptr_array_index (priv->categories, i);
		gs_app_kv_lpad (str, "category", tmp);
	}
	for (i = 0; i < priv->key_colors->len; i++) {
		GdkRGBA *color = g_ptr_array_index (priv->key_colors, i);
		g_autofree gchar *key = NULL;
		key = g_strdup_printf ("key-color-%02u", i);
		gs_app_kv_printf (str, key, "%.0f,%.0f,%.0f",
				  color->red * 255.f,
				  color->green * 255.f,
				  color->blue * 255.f);
	}
	if (priv->keywords != NULL) {
		for (i = 0; i < priv->keywords->len; i++) {
			tmp = g_ptr_array_index (priv->keywords, i);
			gs_app_kv_lpad (str, "keyword", tmp);
		}
	}
	for (i = 0; i < priv->channels->len; i++) {
		GsChannel *channel = g_ptr_array_index (priv->channels, i);
		g_autofree gchar *key = NULL;
		key = g_strdup_printf ("channel-%02u", i);
		gs_app_kv_printf (str, key, "%s [%s]",
		                  gs_channel_get_name (channel),
		                  gs_channel_get_version (channel));
	}
	if (priv->active_channel != NULL) {
		gs_app_kv_printf (str, "active-channel", "%s",
		                  gs_channel_get_name (priv->active_channel));
	}
	keys = g_hash_table_get_keys (priv->metadata);
	for (GList *l = keys; l != NULL; l = l->next) {
		GVariant *val;
		const GVariantType *val_type;
		g_autofree gchar *key = NULL;
		g_autofree gchar *val_str = NULL;

		key = g_strdup_printf ("{%s}", (const gchar *) l->data);
		val = g_hash_table_lookup (priv->metadata, l->data);
		val_type = g_variant_get_type (val);
		if (g_variant_type_equal (val_type, G_VARIANT_TYPE_STRING)) {
			val_str = g_variant_dup_string (val, NULL);
		} else if (g_variant_type_equal (val_type, G_VARIANT_TYPE_BOOLEAN)) {
			val_str = g_strdup (g_variant_get_boolean (val) ? "True" : "False");
		} else if (g_variant_type_equal (val_type, G_VARIANT_TYPE_UINT32)) {
			val_str = g_strdup_printf ("%" G_GUINT32_FORMAT,
						   g_variant_get_uint32 (val));
		} else {
			val_str = g_strdup_printf ("unknown type of %s",
						   g_variant_get_type_string (val));
		}
		gs_app_kv_lpad (str, key, val_str);
	}
	g_list_free (keys);

	/* add subclassed info */
	if (klass->to_string != NULL)
		klass->to_string (app, str);

	/* print runtime data too */
	if (priv->runtime != NULL) {
		g_string_append (str, "\n\tRuntime:\n\t");
		gs_app_to_string_append (priv->runtime, str);
	}
	g_string_append_printf (str, "\n");
}

typedef struct {
	GsApp *app;
	gchar *property_name;
} AppNotifyData;

static gboolean
notify_idle_cb (gpointer data)
{
	AppNotifyData *notify_data = data;

	g_object_notify (G_OBJECT (notify_data->app),
			 notify_data->property_name);

	g_object_unref (notify_data->app);
	g_free (notify_data->property_name);
	g_free (notify_data);

	return G_SOURCE_REMOVE;
}

static void
gs_app_queue_notify (GsApp *app, const gchar *property_name)
{
	AppNotifyData *notify_data;

	notify_data = g_new (AppNotifyData, 1);
	notify_data->app = g_object_ref (app);
	notify_data->property_name = g_strdup (property_name);

	g_idle_add (notify_idle_cb, notify_data);
}

/**
 * gs_app_get_id:
 * @app: a #GsApp
 *
 * Gets the application ID.
 *
 * Returns: The whole ID, e.g. "gimp.desktop"
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_id (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->id;
}

/**
 * gs_app_set_id:
 * @app: a #GsApp
 * @id: a application ID, e.g. "gimp.desktop"
 *
 * Sets the application ID.
 */
void
gs_app_set_id (GsApp *app, const gchar *id)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	if (_g_set_str (&priv->id, id))
		priv->unique_id_valid = FALSE;
}

/**
 * gs_app_get_scope:
 * @app: a #GsApp
 *
 * Gets the scope of the application.
 *
 * Returns: the #AsAppScope, e.g. %AS_APP_SCOPE_USER
 *
 * Since: 3.22
 **/
AsAppScope
gs_app_get_scope (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), AS_APP_SCOPE_UNKNOWN);
	return priv->scope;
}

/**
 * gs_app_set_scope:
 * @app: a #GsApp
 * @scope: a #AsAppScope, e.g. AS_APP_SCOPE_SYSTEM
 *
 * This sets the scope of the application.
 *
 * Since: 3.22
 **/
void
gs_app_set_scope (GsApp *app, AsAppScope scope)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);

	g_return_if_fail (GS_IS_APP (app));

	/* same */
	if (scope == priv->scope)
		return;

	priv->scope = scope;

	/* no longer valid */
	priv->unique_id_valid = FALSE;
}

/**
 * gs_app_get_bundle_kind:
 * @app: a #GsApp
 *
 * Gets the bundle kind of the application.
 *
 * Returns: the #AsAppScope, e.g. %AS_BUNDLE_KIND_FLATPAK
 *
 * Since: 3.22
 **/
AsBundleKind
gs_app_get_bundle_kind (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), AS_BUNDLE_KIND_UNKNOWN);
	return priv->bundle_kind;
}

/**
 * gs_app_set_bundle_kind:
 * @app: a #GsApp
 * @bundle_kind: a #AsAppScope, e.g. AS_BUNDLE_KIND_FLATPAK
 *
 * This sets the bundle kind of the application.
 *
 * Since: 3.22
 **/
void
gs_app_set_bundle_kind (GsApp *app, AsBundleKind bundle_kind)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);

	g_return_if_fail (GS_IS_APP (app));

	/* same */
	if (bundle_kind == priv->bundle_kind)
		return;

	priv->bundle_kind = bundle_kind;

	/* no longer valid */
	priv->unique_id_valid = FALSE;
}

/**
 * gs_app_get_state:
 * @app: a #GsApp
 *
 * Gets the state of the application.
 *
 * Returns: the #AsAppState, e.g. %AS_APP_STATE_INSTALLED
 *
 * Since: 3.22
 **/
AsAppState
gs_app_get_state (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), AS_APP_STATE_UNKNOWN);
	return priv->state;
}

/**
 * gs_app_get_progress:
 * @app: a #GsApp
 *
 * Gets the percentage completion.
 *
 * Returns: the percentage completion, or 0 for unknown
 *
 * Since: 3.22
 **/
guint
gs_app_get_progress (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return priv->progress;
}

/**
 * gs_app_get_allow_cancel:
 * @app: a #GsApp
 *
 * Gets whether the app's installation or upgrade can be cancelled.
 *
 * Returns: TRUE if cancellation is possible, FALSE otherwise.
 *
 * Since: 3.26
 **/
gboolean
gs_app_get_allow_cancel (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), FALSE);
	return priv->allow_cancel;
}

/**
 * gs_app_set_state_recover:
 * @app: a #GsApp
 *
 * Sets the application state to the last status value that was not
 * transient.
 *
 * Since: 3.22
 **/
void
gs_app_set_state_recover (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	if (priv->state_recover == AS_APP_STATE_UNKNOWN)
		return;
	if (priv->state_recover == priv->state)
		return;

	g_debug ("recovering state on %s from %s to %s",
		 priv->id,
		 as_app_state_to_string (priv->state),
		 as_app_state_to_string (priv->state_recover));

	/* make sure progress gets reset when recovering state, to prevent
	 * confusing initial states when going through more than one attempt */
	gs_app_set_progress (app, 0);

	priv->state = priv->state_recover;
	gs_app_queue_notify (app, "state");
}

/* mutex must be held */
static gboolean
gs_app_set_state_internal (GsApp *app, AsAppState state)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	gboolean state_change_ok = FALSE;

	/* same */
	if (priv->state == state)
		return FALSE;

	/* check the state change is allowed */
	switch (priv->state) {
	case AS_APP_STATE_UNKNOWN:
		/* unknown has to go into one of the stable states */
		if (state == AS_APP_STATE_INSTALLED ||
		    state == AS_APP_STATE_QUEUED_FOR_INSTALL ||
		    state == AS_APP_STATE_AVAILABLE ||
		    state == AS_APP_STATE_AVAILABLE_LOCAL ||
		    state == AS_APP_STATE_UPDATABLE ||
		    state == AS_APP_STATE_UPDATABLE_LIVE ||
		    state == AS_APP_STATE_UNAVAILABLE ||
		    state == AS_APP_STATE_PURCHASABLE)
			state_change_ok = TRUE;
		break;
	case AS_APP_STATE_INSTALLED:
		/* installed has to go into an action state */
		if (state == AS_APP_STATE_UNKNOWN ||
		    state == AS_APP_STATE_REMOVING ||
		    state == AS_APP_STATE_UNAVAILABLE ||
		    state == AS_APP_STATE_UPDATABLE ||
		    state == AS_APP_STATE_UPDATABLE_LIVE)
			state_change_ok = TRUE;
		break;
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
		if (state == AS_APP_STATE_UNKNOWN ||
		    state == AS_APP_STATE_INSTALLING ||
		    state == AS_APP_STATE_AVAILABLE)
			state_change_ok = TRUE;
		break;
	case AS_APP_STATE_AVAILABLE:
		/* available has to go into an action state */
		if (state == AS_APP_STATE_UNKNOWN ||
		    state == AS_APP_STATE_QUEUED_FOR_INSTALL ||
		    state == AS_APP_STATE_INSTALLING)
			state_change_ok = TRUE;
		break;
	case AS_APP_STATE_INSTALLING:
		/* installing has to go into an stable state */
		if (state == AS_APP_STATE_UNKNOWN ||
		    state == AS_APP_STATE_INSTALLED ||
		    state == AS_APP_STATE_UPDATABLE ||
		    state == AS_APP_STATE_UPDATABLE_LIVE ||
		    state == AS_APP_STATE_AVAILABLE)
			state_change_ok = TRUE;
		break;
	case AS_APP_STATE_REMOVING:
		/* removing has to go into an stable state */
		if (state == AS_APP_STATE_UNKNOWN ||
		    state == AS_APP_STATE_AVAILABLE ||
		    state == AS_APP_STATE_PURCHASABLE ||
		    state == AS_APP_STATE_INSTALLED)
			state_change_ok = TRUE;
		break;
	case AS_APP_STATE_UPDATABLE:
		/* updatable has to go into an action state */
		if (state == AS_APP_STATE_UNKNOWN ||
		    state == AS_APP_STATE_AVAILABLE ||
		    state == AS_APP_STATE_REMOVING)
			state_change_ok = TRUE;
		break;
	case AS_APP_STATE_UPDATABLE_LIVE:
		/* updatable-live has to go into an action state */
		if (state == AS_APP_STATE_UNKNOWN ||
		    state == AS_APP_STATE_REMOVING ||
		    state == AS_APP_STATE_INSTALLING)
			state_change_ok = TRUE;
		break;
	case AS_APP_STATE_UNAVAILABLE:
		/* updatable has to go into an action state */
		if (state == AS_APP_STATE_UNKNOWN ||
		    state == AS_APP_STATE_AVAILABLE)
			state_change_ok = TRUE;
		break;
	case AS_APP_STATE_AVAILABLE_LOCAL:
		/* local has to go into an action state */
		if (state == AS_APP_STATE_UNKNOWN ||
		    state == AS_APP_STATE_INSTALLING)
			state_change_ok = TRUE;
		break;
	case AS_APP_STATE_PURCHASABLE:
		/* local has to go into an action state */
		if (state == AS_APP_STATE_UNKNOWN ||
		    state == AS_APP_STATE_PURCHASING)
			state_change_ok = TRUE;
		break;
	case AS_APP_STATE_PURCHASING:
		/* purchasing has to go into an stable state */
		if (state == AS_APP_STATE_UNKNOWN ||
		    state == AS_APP_STATE_AVAILABLE ||
		    state == AS_APP_STATE_PURCHASABLE)
			state_change_ok = TRUE;
		break;
	default:
		g_warning ("state %s unhandled",
			   as_app_state_to_string (priv->state));
		g_assert_not_reached ();
	}

	/* this state change was unexpected */
	if (!state_change_ok) {
		g_warning ("State change on %s from %s to %s is not OK",
			   gs_app_get_unique_id_unlocked (app),
			   as_app_state_to_string (priv->state),
			   as_app_state_to_string (state));
	}

	priv->state = state;

	if (state == AS_APP_STATE_UNKNOWN ||
	    state == AS_APP_STATE_AVAILABLE_LOCAL ||
	    state == AS_APP_STATE_AVAILABLE)
		priv->install_date = 0;

	/* save this to simplify error handling in the plugins */
	switch (state) {
	case AS_APP_STATE_INSTALLING:
	case AS_APP_STATE_REMOVING:
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
	case AS_APP_STATE_PURCHASING:
		/* transient, so ignore */
		break;
	default:
		if (priv->state_recover != state) {
			g_debug ("%s non-transient state now %s",
				 gs_app_get_unique_id_unlocked (app),
				 as_app_state_to_string (state));
			priv->state_recover = state;
		}
		break;
	}

	return TRUE;
}

/**
 * gs_app_set_progress:
 * @app: a #GsApp
 * @percentage: a percentage progress
 *
 * This sets the progress completion of the application.
 * If called more than once with the same value then subsequent calls
 * will be ignored.
 *
 * Since: 3.22
 **/
void
gs_app_set_progress (GsApp *app, guint percentage)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	if (priv->progress == percentage)
		return;
	if (percentage > 100) {
		g_debug ("cannot set %u%% for %s, setting instead: 100%%",
			 percentage, gs_app_get_unique_id_unlocked (app));
		percentage = 100;
	}
	priv->progress = percentage;
	gs_app_queue_notify (app, "progress");
}

/**
 * gs_app_set_allow_cancel:
 * @app: a #GsApp
 * @allow_cancel: if the installation or upgrade can be cancelled or not
 *
 * This sets a flag indicating whether the operation can be cancelled or not.
 * This is used by the UI to set the "Cancel" button insensitive as
 * appropriate.
 *
 * Since: 3.26
 **/
void
gs_app_set_allow_cancel (GsApp *app, gboolean allow_cancel)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	if (priv->allow_cancel == allow_cancel)
		return;
	priv->allow_cancel = allow_cancel;
	gs_app_queue_notify (app, "allow-cancel");
}

static void
gs_app_set_pending_action_internal (GsApp *app,
				    GsPluginAction action)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	if (priv->pending_action == action)
		return;

	priv->pending_action = action;
	gs_app_queue_notify (app, "pending-action");
}

/**
 * gs_app_set_state:
 * @app: a #GsApp
 * @state: a #AsAppState, e.g. AS_APP_STATE_UPDATABLE_LIVE
 *
 * This sets the state of the application.
 * The following state diagram explains the typical states.
 * All applications start in state %AS_APP_STATE_UNKNOWN,
 * but the frontend is not supposed to see GsApps with this state.
 *
 * Plugins are responsible for changing the state to one of the other
 * states before the GsApp is passed to the frontend.
 *
 * |[
 * UPDATABLE --> INSTALLING --> INSTALLED
 * UPDATABLE --> REMOVING   --> AVAILABLE
 * INSTALLED --> REMOVING   --> AVAILABLE
 * AVAILABLE --> INSTALLING --> INSTALLED
 * AVAILABLE <--> QUEUED --> INSTALLING --> INSTALLED
 * UNKNOWN   --> UNAVAILABLE
 * ]|
 *
 * Since: 3.22
 **/
void
gs_app_set_state (GsApp *app, AsAppState state)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);

	if (gs_app_set_state_internal (app, state)) {
		/* since the state changed, and the pending-action refers to
		 * actions that usually change the state, we assign it to the
		 * appropriate action here */
		GsPluginAction action = GS_PLUGIN_ACTION_UNKNOWN;
		if (priv->state == AS_APP_STATE_QUEUED_FOR_INSTALL)
			action = GS_PLUGIN_ACTION_INSTALL;
		gs_app_set_pending_action_internal (app, action);

		gs_app_queue_notify (app, "state");
	}
}

/**
 * gs_app_get_kind:
 * @app: a #GsApp
 *
 * Gets the kind of the application.
 *
 * Returns: the #AsAppKind, e.g. %AS_APP_KIND_UNKNOWN
 *
 * Since: 3.22
 **/
AsAppKind
gs_app_get_kind (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), AS_APP_KIND_UNKNOWN);
	return priv->kind;
}

/**
 * gs_app_set_kind:
 * @app: a #GsApp
 * @kind: a #AsAppKind, e.g. #AS_APP_KIND_DESKTOP
 *
 * This sets the kind of the application.
 * The following state diagram explains the typical states.
 * All applications start with kind %AS_APP_KIND_UNKNOWN.
 *
 * |[
 * PACKAGE --> NORMAL
 * PACKAGE --> SYSTEM
 * NORMAL  --> SYSTEM
 * ]|
 *
 * Since: 3.22
 **/
void
gs_app_set_kind (GsApp *app, AsAppKind kind)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	gboolean state_change_ok = FALSE;
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);

	/* same */
	if (priv->kind == kind)
		return;

	/* trying to change */
	if (priv->kind != AS_APP_KIND_UNKNOWN &&
	    kind == AS_APP_KIND_UNKNOWN) {
		g_warning ("automatically prevented from changing "
			   "kind on %s from %s to %s!",
			   gs_app_get_unique_id_unlocked (app),
			   as_app_kind_to_string (priv->kind),
			   as_app_kind_to_string (kind));
		return;
	}

	/* check the state change is allowed */
	switch (priv->kind) {
	case AS_APP_KIND_UNKNOWN:
	case AS_APP_KIND_GENERIC:
		/* all others derive from generic */
		state_change_ok = TRUE;
		break;
	case AS_APP_KIND_DESKTOP:
		/* desktop has to be reset to override */
		if (kind == AS_APP_KIND_UNKNOWN)
			state_change_ok = TRUE;
		break;
	default:
		/* this can never change state */
		break;
	}

	/* this state change was unexpected */
	if (!state_change_ok) {
		g_warning ("Kind change on %s from %s to %s is not OK",
			   priv->id,
			   as_app_kind_to_string (priv->kind),
			   as_app_kind_to_string (kind));
		return;
	}

	priv->kind = kind;
	gs_app_queue_notify (app, "kind");

	/* no longer valid */
	priv->unique_id_valid = FALSE;
}

/**
 * gs_app_get_unique_id:
 * @app: a #GsApp
 *
 * Gets the unique application ID used for de-duplication.
 * If nothing has been set the value from gs_app_get_id() will be used.
 *
 * Returns: The unique ID, e.g. `system/package/fedora/desktop/gimp.desktop/i386/master`, or %NULL
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_unique_id (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	locker = g_mutex_locker_new (&priv->mutex);
	return gs_app_get_unique_id_unlocked (app);
}

/**
 * gs_app_set_unique_id:
 * @app: a #GsApp
 * @unique_id: a unique application ID, e.g. `system/package/fedora/desktop/gimp.desktop/i386/master`
 *
 * Sets the unique application ID. Any #GsApp using the same ID will be
 * deduplicated. This means that applications that can exist from more than
 * one plugin should use this method.
 */
void
gs_app_set_unique_id (GsApp *app, const gchar *unique_id)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);

	/* check for sanity */
	if (!as_utils_unique_id_valid (unique_id))
		g_warning ("unique_id %s not valid", unique_id);

	g_free (priv->unique_id);
	priv->unique_id = g_strdup (unique_id);
	priv->unique_id_valid = TRUE;
}

/**
 * gs_app_get_name:
 * @app: a #GsApp
 *
 * Gets the application name.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_name (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->name;
}

/**
 * gs_app_set_name:
 * @app: a #GsApp
 * @quality: A #GsAppQuality, e.g. %GS_APP_QUALITY_LOWEST
 * @name: The short localized name, e.g. "Calculator"
 *
 * Sets the application name.
 *
 * Since: 3.22
 **/
void
gs_app_set_name (GsApp *app, GsAppQuality quality, const gchar *name)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);

	/* only save this if the data is sufficiently high quality */
	if (quality <= priv->name_quality)
		return;
	priv->name_quality = quality;
	if (_g_set_str (&priv->name, name))
		g_object_notify (G_OBJECT (app), "name");
}

/**
 * gs_app_get_branch:
 * @app: a #GsApp
 *
 * Gets the application branch.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_branch (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->branch;
}

/**
 * gs_app_set_branch:
 * @app: a #GsApp
 * @branch: The branch, e.g. "master"
 *
 * Sets the application branch.
 *
 * Since: 3.22
 **/
void
gs_app_set_branch (GsApp *app, const gchar *branch)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	if (_g_set_str (&priv->branch, branch))
		priv->unique_id_valid = FALSE;
}

/**
 * gs_app_get_source_default:
 * @app: a #GsApp
 *
 * Gets the default source.
 *
 * Returns: a string, or %NULL
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_source_default (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	if (priv->sources->len == 0)
		return NULL;
	return g_ptr_array_index (priv->sources, 0);
}

/**
 * gs_app_add_source:
 * @app: a #GsApp
 * @source: a source name
 *
 * Adds a source name for the application.
 *
 * Since: 3.22
 **/
void
gs_app_add_source (GsApp *app, const gchar *source)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	const gchar *tmp;
	guint i;
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (source != NULL);

	locker = g_mutex_locker_new (&priv->mutex);

	/* check source doesn't already exist */
	for (i = 0; i < priv->sources->len; i++) {
		tmp = g_ptr_array_index (priv->sources, i);
		if (g_strcmp0 (tmp, source) == 0)
			return;
	}
	g_ptr_array_add (priv->sources, g_strdup (source));
}

/**
 * gs_app_get_sources:
 * @app: a #GsApp
 *
 * Gets the list of sources for the application.
 *
 * Returns: (element-type utf8) (transfer none): a list
 *
 * Since: 3.22
 **/
GPtrArray *
gs_app_get_sources (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->sources;
}

/**
 * gs_app_set_sources:
 * @app: a #GsApp
 * @sources: The non-localized short names, e.g. ["gnome-calculator"]
 *
 * This name is used for the update page if the application is collected into
 * the 'OS Updates' group.
 * It is typically the package names, although this should not be relied upon.
 *
 * Since: 3.22
 **/
void
gs_app_set_sources (GsApp *app, GPtrArray *sources)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	_g_set_ptr_array (&priv->sources, sources);
}

/**
 * gs_app_get_source_id_default:
 * @app: a #GsApp
 *
 * Gets the default source ID.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_source_id_default (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	if (priv->source_ids->len == 0)
		return NULL;
	return g_ptr_array_index (priv->source_ids, 0);
}

/**
 * gs_app_get_source_ids:
 * @app: a #GsApp
 *
 * Gets the list of source IDs.
 *
 * Returns: (element-type utf8) (transfer none): a list
 *
 * Since: 3.22
 **/
GPtrArray *
gs_app_get_source_ids (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->source_ids;
}

/**
 * gs_app_clear_source_ids:
 * @app: a #GsApp
 *
 * Clear the list of source IDs.
 *
 * Since: 3.22
 **/
void
gs_app_clear_source_ids (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	g_ptr_array_set_size (priv->source_ids, 0);
}

/**
 * gs_app_set_source_ids:
 * @app: a #GsApp
 * @source_ids: The source-id, e.g. ["gnome-calculator;0.134;fedora"]
 *		or ["/home/hughsie/.local/share/applications/0ad.desktop"]
 *
 * This ID is used internally to the controlling plugin.
 *
 * Since: 3.22
 **/
void
gs_app_set_source_ids (GsApp *app, GPtrArray *source_ids)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	_g_set_ptr_array (&priv->source_ids, source_ids);
}

/**
 * gs_app_add_source_id:
 * @app: a #GsApp
 * @source_id: a source ID, e.g. "gnome-calculator;0.134;fedora"
 *
 * Adds a source ID to the application.
 *
 * Since: 3.22
 **/
void
gs_app_add_source_id (GsApp *app, const gchar *source_id)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	const gchar *tmp;
	guint i;

	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (source_id != NULL);

	/* only add if not already present */
	for (i = 0; i < priv->source_ids->len; i++) {
		tmp = g_ptr_array_index (priv->source_ids, i);
		if (g_strcmp0 (tmp, source_id) == 0)
			return;
	}
	g_ptr_array_add (priv->source_ids, g_strdup (source_id));
}

/**
 * gs_app_get_project_group:
 * @app: a #GsApp
 *
 * Gets a project group for the application.
 * Applications belonging to other project groups may not be shown in
 * this software center.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_project_group (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->project_group;
}

/**
 * gs_app_get_developer_name:
 * @app: a #GsApp
 *
 * Gets the developer name for the application.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_developer_name (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->developer_name;
}

/**
 * gs_app_set_project_group:
 * @app: a #GsApp
 * @project_group: The non-localized project group, e.g. "GNOME" or "KDE"
 *
 * Sets a project group for the application.
 *
 * Since: 3.22
 **/
void
gs_app_set_project_group (GsApp *app, const gchar *project_group)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	_g_set_str (&priv->project_group, project_group);
}

/**
 * gs_app_set_developer_name:
 * @app: a #GsApp
 * @developer_name: The developer name, e.g. "Richard Hughes"
 *
 * Sets a developer name for the application.
 *
 * Since: 3.22
 **/
void
gs_app_set_developer_name (GsApp *app, const gchar *developer_name)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	_g_set_str (&priv->developer_name, developer_name);
}

/**
 * gs_app_get_pixbuf:
 * @app: a #GsApp
 *
 * Gets a pixbuf to represent the application.
 *
 * Returns: (transfer none): a #GdkPixbuf, or %NULL
 *
 * Since: 3.22
 **/
GdkPixbuf *
gs_app_get_pixbuf (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->pixbuf;
}

/**
 * gs_app_get_icons:
 * @app: a #GsApp
 *
 * Gets the icons for the application.
 *
 * Returns: (transfer none) (element-type AsIcon): an array of icons
 *
 * Since: 3.22
 **/
GPtrArray *
gs_app_get_icons (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->icons;
}

/**
 * gs_app_add_icon:
 * @app: a #GsApp
 * @icon: a #AsIcon, or %NULL to remove all icons
 *
 * Adds an icon to use for the application.
 * If the first icon added cannot be loaded then the next one is tried.
 *
 * Since: 3.22
 **/
void
gs_app_add_icon (GsApp *app, AsIcon *icon)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	if (icon == NULL) {
		g_ptr_array_set_size (priv->icons, 0);
		return;
	}
	g_ptr_array_add (priv->icons, g_object_ref (icon));
}

/**
 * gs_app_get_agreement:
 * @app: a #GsApp
 *
 * Gets the agreement text for the application.
 *
 * Returns: a string in AppStream description format, or %NULL for unset
 *
 * Since: 3.28
 **/
const gchar *
gs_app_get_agreement (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->agreement;
}

/**
 * gs_app_set_agreement:
 * @app: a #GsApp
 * @agreement: The agreement text, e.g. "<p>Foobar</p>"
 *
 * Sets the application end-user agreement (e.g. a EULA) in AppStream
 * description format.
 *
 * Since: 3.28
 **/
void
gs_app_set_agreement (GsApp *app, const gchar *agreement)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	_g_set_str (&priv->agreement, agreement);
}

/**
 * gs_app_get_local_file:
 * @app: a #GsApp
 *
 * Gets the file that backs this application, for instance this might
 * be a local file in ~/Downloads that we are installing.
 *
 * Returns: (transfer none): a #GFile, or %NULL
 *
 * Since: 3.22
 **/
GFile *
gs_app_get_local_file (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->local_file;
}

/**
 * gs_app_set_local_file:
 * @app: a #GsApp
 * @local_file: a #GFile, or %NULL
 *
 * Sets the file that backs this application, for instance this might
 * be a local file in ~/Downloads that we are installing.
 *
 * Since: 3.22
 **/
void
gs_app_set_local_file (GsApp *app, GFile *local_file)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	g_set_object (&priv->local_file, local_file);
}

/**
 * gs_app_get_content_rating:
 * @app: a #GsApp
 *
 * Gets the content rating for this application.
 *
 * Returns: (transfer none): a #AsContentRating, or %NULL
 *
 * Since: 3.24
 **/
AsContentRating *
gs_app_get_content_rating (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->content_rating;
}

/**
 * gs_app_set_content_rating:
 * @app: a #GsApp
 * @content_rating: a #AsContentRating, or %NULL
 *
 * Sets the content rating for this application.
 *
 * Since: 3.24
 **/
void
gs_app_set_content_rating (GsApp *app, AsContentRating *content_rating)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	g_set_object (&priv->content_rating, content_rating);
}

/**
 * gs_app_get_runtime:
 * @app: a #GsApp
 *
 * Gets the runtime for the installed application.
 *
 * Returns: (transfer none): a #GsApp, or %NULL for unset
 *
 * Since: 3.22
 **/
GsApp *
gs_app_get_runtime (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->runtime;
}

/**
 * gs_app_set_runtime:
 * @app: a #GsApp
 * @runtime: a #GsApp
 *
 * Sets the runtime that the installed application requires.
 *
 * Since: 3.22
 **/
void
gs_app_set_runtime (GsApp *app, GsApp *runtime)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (app != runtime);
	locker = g_mutex_locker_new (&priv->mutex);
	g_set_object (&priv->runtime, runtime);
}

/**
 * gs_app_set_pixbuf:
 * @app: a #GsApp
 * @pixbuf: a #GdkPixbuf, or %NULL
 *
 * Sets a pixbuf used to represent the application.
 *
 * Since: 3.22
 **/
void
gs_app_set_pixbuf (GsApp *app, GdkPixbuf *pixbuf)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	g_set_object (&priv->pixbuf, pixbuf);
}

/**
 * gs_app_get_price:
 * @app: a #GsApp
 *
 * Gets the price required to purchase the application.
 *
 * Returns: (transfer none): a #GsPrice, or %NULL
 *
 * Since: 3.26
 **/
GsPrice *
gs_app_get_price (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->price;
}

/**
 * gs_app_set_price:
 * @app: a #GsApp
 * @amount: the amount of this price, e.g. 0.99
 * @currency: an ISO 4217 currency code, e.g. "USD"
 *
 * Sets a price required to purchase the application.
 *
 * Since: 3.26
 **/
void
gs_app_set_price (GsApp *app, gdouble amount, const gchar *currency)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	if (priv->price != NULL)
		g_object_unref (priv->price);
	priv->price = gs_price_new (amount, currency);
}

typedef enum {
	GS_APP_VERSION_FIXUP_RELEASE		= 1,
	GS_APP_VERSION_FIXUP_DISTRO_SUFFIX	= 2,
	GS_APP_VERSION_FIXUP_GIT_SUFFIX		= 4,
	GS_APP_VERSION_FIXUP_LAST,
} GsAppVersionFixup;

/**
 * gs_app_get_ui_version:
 *
 * convert 1:1.6.2-7.fc17 into "Version 1.6.2"
 **/
static gchar *
gs_app_get_ui_version (const gchar *version, guint64 flags)
{
	guint i;
	gchar *new;
	gchar *f;

	/* nothing set */
	if (version == NULL)
		return NULL;

	/* first remove any epoch */
	for (i = 0; version[i] != '\0'; i++) {
		if (version[i] == ':') {
			version = &version[i+1];
			break;
		}
		if (!g_ascii_isdigit (version[i]))
			break;
	}

	/* then remove any distro suffix */
	new = g_strdup (version);
	if ((flags & GS_APP_VERSION_FIXUP_DISTRO_SUFFIX) > 0) {
		f = g_strstr_len (new, -1, ".fc");
		if (f != NULL)
			*f= '\0';
		f = g_strstr_len (new, -1, ".el");
		if (f != NULL)
			*f= '\0';
	}

	/* then remove any release */
	if ((flags & GS_APP_VERSION_FIXUP_RELEASE) > 0) {
		f = g_strrstr_len (new, -1, "-");
		if (f != NULL)
			*f= '\0';
	}

	/* then remove any git suffix */
	if ((flags & GS_APP_VERSION_FIXUP_GIT_SUFFIX) > 0) {
		f = g_strrstr_len (new, -1, ".2012");
		if (f != NULL)
			*f= '\0';
		f = g_strrstr_len (new, -1, ".2013");
		if (f != NULL)
			*f= '\0';
	}

	return new;
}

static void
gs_app_ui_versions_invalidate (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_free (priv->version_ui);
	g_free (priv->update_version_ui);
	priv->version_ui = NULL;
	priv->update_version_ui = NULL;
}

static void
gs_app_ui_versions_populate (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	guint i;
	guint64 flags[] = { GS_APP_VERSION_FIXUP_RELEASE |
			    GS_APP_VERSION_FIXUP_DISTRO_SUFFIX |
			    GS_APP_VERSION_FIXUP_GIT_SUFFIX,
			    GS_APP_VERSION_FIXUP_DISTRO_SUFFIX |
			    GS_APP_VERSION_FIXUP_GIT_SUFFIX,
			    GS_APP_VERSION_FIXUP_DISTRO_SUFFIX,
			    0 };

	/* try each set of bitfields in order */
	for (i = 0; flags[i] != 0; i++) {
		priv->version_ui = gs_app_get_ui_version (priv->version, flags[i]);
		priv->update_version_ui = gs_app_get_ui_version (priv->update_version, flags[i]);
		if (g_strcmp0 (priv->version_ui, priv->update_version_ui) != 0) {
			gs_app_queue_notify (app, "version");
			return;
		}
		gs_app_ui_versions_invalidate (app);
	}

	/* we tried, but failed */
	priv->version_ui = g_strdup (priv->version);
	priv->update_version_ui = g_strdup (priv->update_version);
}

/**
 * gs_app_get_version:
 * @app: a #GsApp
 *
 * Gets the exact version for the application.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_version (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->version;
}

/**
 * gs_app_get_version_ui:
 * @app: a #GsApp
 *
 * Gets a version string that can be displayed in a UI.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_version_ui (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);

	/* work out the two version numbers */
	if (priv->version != NULL &&
	    priv->version_ui == NULL) {
		gs_app_ui_versions_populate (app);
	}

	return priv->version_ui;
}

/**
 * gs_app_set_version:
 * @app: a #GsApp
 * @version: The version, e.g. "2:1.2.3.fc19"
 *
 * This saves the version after stripping out any non-friendly parts, such as
 * distro tags, git revisions and that kind of thing.
 *
 * Since: 3.22
 **/
void
gs_app_set_version (GsApp *app, const gchar *version)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);

	if (_g_set_str (&priv->version, version)) {
		gs_app_ui_versions_invalidate (app);
		gs_app_queue_notify (app, "version");
	}
}

/**
 * gs_app_get_summary:
 * @app: a #GsApp
 *
 * Gets the single-line description of the application.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_summary (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->summary;
}

/**
 * gs_app_set_summary:
 * @app: a #GsApp
 * @quality: a #GsAppQuality, e.g. %GS_APP_QUALITY_LOWEST
 * @summary: a string, e.g. "A graphical calculator for GNOME"
 *
 * The medium length one-line localized name.
 *
 * Since: 3.22
 **/
void
gs_app_set_summary (GsApp *app, GsAppQuality quality, const gchar *summary)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);

	/* only save this if the data is sufficiently high quality */
	if (quality <= priv->summary_quality)
		return;
	priv->summary_quality = quality;
	if (_g_set_str (&priv->summary, summary))
		g_object_notify (G_OBJECT (app), "summary");
}

/**
 * gs_app_get_description:
 * @app: a #GsApp
 *
 * Gets the long multi-line description of the application.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_description (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->description;
}

/**
 * gs_app_set_description:
 * @app: a #GsApp
 * @quality: a #GsAppQuality, e.g. %GS_APP_QUALITY_LOWEST
 * @description: a string, e.g. "GNOME Calculator is a graphical calculator for GNOME..."
 *
 * Sets the long multi-line description of the application.
 *
 * Since: 3.22
 **/
void
gs_app_set_description (GsApp *app, GsAppQuality quality, const gchar *description)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);

	/* only save this if the data is sufficiently high quality */
	if (quality <= priv->description_quality)
		return;
	priv->description_quality = quality;
	_g_set_str (&priv->description, description);
}

/**
 * gs_app_get_url:
 * @app: a #GsApp
 * @kind: a #AsUrlKind, e.g. %AS_URL_KIND_HOMEPAGE
 *
 * Gets a web address of a specific type.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_url (GsApp *app, AsUrlKind kind)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return g_hash_table_lookup (priv->urls, as_url_kind_to_string (kind));
}

/**
 * gs_app_set_url:
 * @app: a #GsApp
 * @kind: a #AsUrlKind, e.g. %AS_URL_KIND_HOMEPAGE
 * @url: a web URL, e.g. "http://www.hughsie.com/"
 *
 * Sets a web address of a specific type.
 *
 * Since: 3.22
 **/
void
gs_app_set_url (GsApp *app, AsUrlKind kind, const gchar *url)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	g_hash_table_insert (priv->urls,
			     g_strdup (as_url_kind_to_string (kind)),
			     g_strdup (url));
}

/**
 * gs_app_get_launchable:
 * @app: a #GsApp
 * @kind: a #AsLaunchableKind, e.g. %AS_LAUNCHABLE_KIND_DESKTOP_ID
 *
 * Gets a launchable of a specific type.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.28
 **/
const gchar *
gs_app_get_launchable (GsApp *app, AsLaunchableKind kind)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return g_hash_table_lookup (priv->launchables,
				    as_launchable_kind_to_string (kind));
}

/**
 * gs_app_set_launchable:
 * @app: a #GsApp
 * @kind: a #AsLaunchableKind, e.g. %AS_LAUNCHABLE_KIND_DESKTOP_ID
 * @launchable: a way to launch, e.g. "org.gnome.Sysprof2.desktop"
 *
 * Sets a launchable of a specific type.
 *
 * Since: 3.28
 **/
void
gs_app_set_launchable (GsApp *app, AsLaunchableKind kind, const gchar *launchable)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	g_hash_table_insert (priv->launchables,
			     g_strdup (as_launchable_kind_to_string (kind)),
			     g_strdup (launchable));
}

/**
 * gs_app_get_license:
 * @app: a #GsApp
 *
 * Gets the project license of the application.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_license (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->license;
}

/**
 * gs_app_get_license_is_free:
 * @app: a #GsApp
 *
 * Returns if the application is free software.
 *
 * Returns: %TRUE if the application is free software
 *
 * Since: 3.22
 **/
gboolean
gs_app_get_license_is_free (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), FALSE);
	return priv->license_is_free;
}

static gboolean
gs_app_get_license_token_is_nonfree (const gchar *token)
{
	/* grammar */
	if (g_strcmp0 (token, "(") == 0)
		return FALSE;
	if (g_strcmp0 (token, ")") == 0)
		return FALSE;

	/* a token, but still nonfree */
	if (g_str_has_prefix (token, "@LicenseRef-proprietary"))
		return TRUE;

	/* if it has a prefix, assume it is free */
	return token[0] != '@';
}

/**
 * gs_app_set_license:
 * @app: a #GsApp
 * @quality: a #GsAppQuality, e.g. %GS_APP_QUALITY_NORMAL
 * @license: a SPDX license string, e.g. "GPL-3.0 AND LGPL-2.0+"
 *
 * Sets the project licenses used in the application.
 *
 * Since: 3.22
 **/
void
gs_app_set_license (GsApp *app, GsAppQuality quality, const gchar *license)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	guint i;
	g_auto(GStrv) tokens = NULL;

	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);

	/* only save this if the data is sufficiently high quality */
	if (quality <= priv->license_quality)
		return;
	if (license == NULL)
		return;
	priv->license_quality = quality;

	/* assume free software until we find a nonfree SPDX token */
	priv->license_is_free = TRUE;
	tokens = as_utils_spdx_license_tokenize (license);
	for (i = 0; tokens[i] != NULL; i++) {
		if (g_strcmp0 (tokens[i], "&") == 0 ||
		    g_strcmp0 (tokens[i], "+") == 0 ||
		    g_strcmp0 (tokens[i], "|") == 0)
			continue;
		if (gs_app_get_license_token_is_nonfree (tokens[i])) {
			g_debug ("nonfree license from %s: '%s'",
				 gs_app_get_id (app), tokens[i]);
			priv->license_is_free = FALSE;
			break;
		}
	}
	_g_set_str (&priv->license, license);
}

/**
 * gs_app_get_summary_missing:
 * @app: a #GsApp
 *
 * Gets the one-line summary to use when this application is missing.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_summary_missing (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->summary_missing;
}

/**
 * gs_app_set_summary_missing:
 * @app: a #GsApp
 * @summary_missing: a string, or %NULL
 *
 * Sets the one-line summary to use when this application is missing.
 *
 * Since: 3.22
 **/
void
gs_app_set_summary_missing (GsApp *app, const gchar *summary_missing)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	_g_set_str (&priv->summary_missing, summary_missing);
}

/**
 * gs_app_get_menu_path:
 * @app: a #GsApp
 *
 * Returns the menu path which is an array of path elements.
 * The resulting array is an internal structure and must not be
 * modified or freed.
 *
 * Returns: a %NULL-terminated array of strings
 *
 * Since: 3.22
 **/
gchar **
gs_app_get_menu_path (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->menu_path;
}

/**
 * gs_app_set_menu_path:
 * @app: a #GsApp
 * @menu_path: a %NULL-terminated array of strings
 *
 * Sets the new menu path. The menu path is an array of path elements.
 * This function creates a deep copy of the path.
 *
 * Since: 3.22
 **/
void
gs_app_set_menu_path (GsApp *app, gchar **menu_path)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	_g_set_strv (&priv->menu_path, menu_path);
}

/**
 * gs_app_get_origin:
 * @app: a #GsApp
 *
 * Gets the origin for the application, e.g. "fedora".
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_origin (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->origin;
}

/**
 * gs_app_set_origin:
 * @app: a #GsApp
 * @origin: a string, or %NULL
 *
 * The origin is the original source of the application e.g. "fedora-updates"
 *
 * Since: 3.22
 **/
void
gs_app_set_origin (GsApp *app, const gchar *origin)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);

	/* same */
	if (g_strcmp0 (origin, priv->origin) == 0)
		return;

	/* trying to change */
	if (priv->origin != NULL && origin != NULL) {
		g_warning ("automatically prevented from changing "
			   "origin on %s from %s to %s!",
			   gs_app_get_unique_id_unlocked (app),
			   priv->origin, origin);
		return;
	}

	g_free (priv->origin);
	priv->origin = g_strdup (origin);

	/* no longer valid */
	priv->unique_id_valid = FALSE;
}

/**
 * gs_app_get_origin_appstream:
 * @app: a #GsApp
 *
 * Gets the appstream origin for the application, e.g. "fedora".
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.28
 **/
const gchar *
gs_app_get_origin_appstream (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->origin_appstream;
}

/**
 * gs_app_set_origin_appstream:
 * @app: a #GsApp
 * @origin_appstream: a string, or %NULL
 *
 * The appstream origin is the appstream source of the application e.g. "fedora"
 *
 * Since: 3.28
 **/
void
gs_app_set_origin_appstream (GsApp *app, const gchar *origin_appstream)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);

	/* same */
	if (g_strcmp0 (origin_appstream, priv->origin_appstream) == 0)
		return;

	g_free (priv->origin_appstream);
	priv->origin_appstream = g_strdup (origin_appstream);
}

/**
 * gs_app_get_origin_hostname:
 * @app: a #GsApp
 *
 * Gets the hostname of the origin used to install the application, e.g.
 * "fedoraproject.org" or "sdk.gnome.org".
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_origin_hostname (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->origin_hostname;
}

/**
 * gs_app_set_origin_hostname:
 * @app: a #GsApp
 * @origin_hostname: a string, or %NULL
 *
 * The origin is the hostname of the source used to install the application
 * e.g. "fedoraproject.org"
 *
 * You can also use a full URL as @origin_hostname and this will be parsed and
 * the hostname extracted. This process will also remove any unnecessary DNS
 * prefixes like "download" or "mirrors".
 *
 * Since: 3.22
 **/
void
gs_app_set_origin_hostname (GsApp *app, const gchar *origin_hostname)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_autoptr(SoupURI) uri = NULL;
	guint i;
	const gchar *prefixes[] = { "download.", "mirrors.", NULL };

	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);

	/* same */
	if (g_strcmp0 (origin_hostname, priv->origin_hostname) == 0)
		return;
	g_free (priv->origin_hostname);

	/* use libsoup to convert a URL */
	uri = soup_uri_new (origin_hostname);
	if (uri != NULL)
		origin_hostname = soup_uri_get_host (uri);

	/* remove some common prefixes */
	for (i = 0; prefixes[i] != NULL; i++) {
		if (g_str_has_prefix (origin_hostname, prefixes[i]))
			origin_hostname += strlen (prefixes[i]);
	}

	/* fallback for localhost */
	if (g_strcmp0 (origin_hostname, "") == 0)
		origin_hostname = "localhost";

	/* success */
	priv->origin_hostname = g_strdup (origin_hostname);
}

/**
 * gs_app_add_screenshot:
 * @app: a #GsApp
 * @screenshot: a #AsScreenshot
 *
 * Adds a screenshot to the application.
 *
 * Since: 3.22
 **/
void
gs_app_add_screenshot (GsApp *app, AsScreenshot *screenshot)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (AS_IS_SCREENSHOT (screenshot));

	locker = g_mutex_locker_new (&priv->mutex);
	g_ptr_array_add (priv->screenshots, g_object_ref (screenshot));
}

/**
 * gs_app_get_screenshots:
 * @app: a #GsApp
 *
 * Gets the list of screenshots.
 *
 * Returns: (element-type AsScreenshot) (transfer none): a list
 *
 * Since: 3.22
 **/
GPtrArray *
gs_app_get_screenshots (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->screenshots;
}

/**
 * gs_app_get_update_version:
 * @app: a #GsApp
 *
 * Gets the newest update version.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_update_version (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->update_version;
}

/**
 * gs_app_get_update_version_ui:
 * @app: a #GsApp
 *
 * Gets the update version for the UI.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_update_version_ui (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);

	/* work out the two version numbers */
	if (priv->update_version != NULL &&
	    priv->update_version_ui == NULL) {
		gs_app_ui_versions_populate (app);
	}

	return priv->update_version_ui;
}

static void
gs_app_set_update_version_internal (GsApp *app, const gchar *update_version)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	if (_g_set_str (&priv->update_version, update_version))
		gs_app_ui_versions_invalidate (app);
}

/**
 * gs_app_set_update_version:
 * @app: a #GsApp
 * @update_version: a string, e.g. "0.1.2.3"
 *
 * Sets the new version number of the update.
 *
 * Since: 3.22
 **/
void
gs_app_set_update_version (GsApp *app, const gchar *update_version)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	gs_app_set_update_version_internal (app, update_version);
	gs_app_queue_notify (app, "version");
}

/**
 * gs_app_get_update_details:
 * @app: a #GsApp
 *
 * Gets the multi-line description for the update.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_update_details (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->update_details;
}

/**
 * gs_app_set_update_details:
 * @app: a #GsApp
 * @update_details: a string
 *
 * Sets the multi-line description for the update.
 *
 * Since: 3.22
 **/
void
gs_app_set_update_details (GsApp *app, const gchar *update_details)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	_g_set_str (&priv->update_details, update_details);
}

/**
 * gs_app_get_update_urgency:
 * @app: a #GsApp
 *
 * Gets the update urgency.
 *
 * Returns: a #AsUrgencyKind, or %AS_URGENCY_KIND_UNKNOWN for unset
 *
 * Since: 3.22
 **/
AsUrgencyKind
gs_app_get_update_urgency (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), AS_URGENCY_KIND_UNKNOWN);
	return priv->update_urgency;
}

/**
 * gs_app_set_update_urgency:
 * @app: a #GsApp
 * @update_urgency: a #AsUrgencyKind
 *
 * Sets the update urgency.
 *
 * Since: 3.22
 **/
void
gs_app_set_update_urgency (GsApp *app, AsUrgencyKind update_urgency)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));
	if (update_urgency == priv->update_urgency)
		return;
	priv->update_urgency = update_urgency;
}

/**
 * gs_app_get_management_plugin:
 * @app: a #GsApp
 *
 * Gets the management plugin.
 * This is some metadata about the application which is used to work out
 * which plugin should handle the install, remove or upgrade actions.
 *
 * Typically plugins will just set this to the plugin name using
 * gs_plugin_get_name().
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_management_plugin (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->management_plugin;
}

/**
 * gs_app_set_management_plugin:
 * @app: a #GsApp
 * @management_plugin: a string, or %NULL, e.g. "fwupd"
 *
 * The management plugin is the plugin that can handle doing install and remove
 * operations on the #GsApp.
 * Typical values include "packagekit" and "flatpak"
 *
 * It is an error to attempt to change the management plugin once it has been
 * previously set or to try to use this function on a wildcard application.
 *
 * Since: 3.22
 **/
void
gs_app_set_management_plugin (GsApp *app, const gchar *management_plugin)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);

	/* plugins cannot adopt wildcard packages */
	if (gs_app_has_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX)) {
		g_warning ("plugins should not set the management plugin on "
			   "%s to %s -- create a new GsApp in refine()!",
			   gs_app_get_unique_id_unlocked (app),
			   management_plugin);
		return;
	}

	/* same */
	if (g_strcmp0 (priv->management_plugin, management_plugin) == 0)
		return;

	/* trying to change */
	if (priv->management_plugin != NULL && management_plugin != NULL) {
		g_warning ("automatically prevented from changing "
			   "management plugin on %s from %s to %s!",
			   gs_app_get_unique_id_unlocked (app),
			   priv->management_plugin,
			   management_plugin);
		return;
	}

	g_free (priv->management_plugin);
	priv->management_plugin = g_strdup (management_plugin);
}

/**
 * gs_app_get_rating:
 * @app: a #GsApp
 *
 * Gets the percentage rating of the application, where 100 is 5 stars.
 *
 * Returns: a percentage, or -1 for unset
 *
 * Since: 3.22
 **/
gint
gs_app_get_rating (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), -1);
	return priv->rating;
}

/**
 * gs_app_set_rating:
 * @app: a #GsApp
 * @rating: a percentage, or -1 for invalid
 *
 * Gets the percentage rating of the application.
 *
 * Since: 3.22
 **/
void
gs_app_set_rating (GsApp *app, gint rating)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	if (rating == priv->rating)
		return;
	priv->rating = rating;
	gs_app_queue_notify (app, "rating");
}

/**
 * gs_app_get_review_ratings:
 * @app: a #GsApp
 *
 * Gets the review ratings.
 *
 * Returns: (element-type gint) (transfer none): a list
 *
 * Since: 3.22
 **/
GArray *
gs_app_get_review_ratings (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->review_ratings;
}

/**
 * gs_app_set_review_ratings:
 * @app: a #GsApp
 * @review_ratings: (element-type gint): a list
 *
 * Sets the review ratings.
 *
 * Since: 3.22
 **/
void
gs_app_set_review_ratings (GsApp *app, GArray *review_ratings)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	locker = g_mutex_locker_new (&priv->mutex);
	_g_set_array (&priv->review_ratings, review_ratings);
}

/**
 * gs_app_get_reviews:
 * @app: a #GsApp
 *
 * Gets all the user-submitted reviews for the application.
 *
 * Returns: (element-type AsReview) (transfer none): the list of reviews
 *
 * Since: 3.22
 **/
GPtrArray *
gs_app_get_reviews (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->reviews;
}

/**
 * gs_app_add_review:
 * @app: a #GsApp
 * @review: a #AsReview
 *
 * Adds a user-submitted review to the application.
 *
 * Since: 3.22
 **/
void
gs_app_add_review (GsApp *app, AsReview *review)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (AS_IS_REVIEW (review));
	g_ptr_array_add (priv->reviews, g_object_ref (review));
}

/**
 * gs_app_remove_review:
 * @app: a #GsApp
 * @review: a #AsReview
 *
 * Removes a user-submitted review to the application.
 *
 * Since: 3.22
 **/
void
gs_app_remove_review (GsApp *app, AsReview *review)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));
	g_ptr_array_remove (priv->reviews, review);
}

/**
 * gs_app_get_provides:
 * @app: a #GsApp
 *
 * Gets all the provides for the application.
 *
 * Returns: (element-type AsProvide) (transfer none): the list of provides
 *
 * Since: 3.22
 **/
GPtrArray *
gs_app_get_provides (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->provides;
}

/**
 * gs_app_add_provide:
 * @app: a #GsApp
 * @provide: a #AsProvide
 *
 * Adds a provide to the application.
 *
 * Since: 3.22
 **/
void
gs_app_add_provide (GsApp *app, AsProvide *provide)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (AS_IS_PROVIDE (provide));
	g_ptr_array_add (priv->provides, g_object_ref (provide));
}

/**
 * gs_app_get_size_download:
 * @app: A #GsApp
 *
 * Gets the size of the total download needed to either install an available
 * application, or update an already installed one.
 *
 * If there is a runtime not yet installed then this is also added.
 *
 * Returns: number of bytes, 0 for unknown, or %GS_APP_SIZE_UNKNOWABLE for invalid
 *
 * Since: 3.22
 **/
guint64
gs_app_get_size_download (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	guint64 sz;

	g_return_val_if_fail (GS_IS_APP (app), G_MAXUINT64);

	/* this app */
	sz = priv->size_download;

	/* add the runtime if this is not installed */
	if (priv->runtime != NULL) {
		if (gs_app_get_state (priv->runtime) == AS_APP_STATE_AVAILABLE)
			sz += gs_app_get_size_installed (priv->runtime);
	}

	/* add related apps */
	for (guint i = 0; i < gs_app_list_length (priv->related); i++) {
		GsApp *app_related = gs_app_list_index (priv->related, i);
		sz += gs_app_get_size_download (app_related);
	}

	return sz;
}

/**
 * gs_app_set_size_download:
 * @app: a #GsApp
 * @size_download: size in bytes, or %GS_APP_SIZE_UNKNOWABLE for invalid
 *
 * Sets the download size of the application, not including any
 * required runtime.
 *
 * Since: 3.22
 **/
void
gs_app_set_size_download (GsApp *app, guint64 size_download)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));
	if (size_download == priv->size_download)
		return;
	priv->size_download = size_download;
}

/**
 * gs_app_get_size_installed:
 * @app: a #GsApp
 *
 * Gets the size on disk, either for an existing application of one that could
 * be installed.
 *
 * Returns: size in bytes, 0 for unknown, or %GS_APP_SIZE_UNKNOWABLE for invalid.
 *
 * Since: 3.22
 **/
guint64
gs_app_get_size_installed (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	guint64 sz;

	g_return_val_if_fail (GS_IS_APP (app), G_MAXUINT64);

	/* this app */
	sz = priv->size_installed;

	/* add related apps */
	for (guint i = 0; i < gs_app_list_length (priv->related); i++) {
		GsApp *app_related = gs_app_list_index (priv->related, i);
		sz += gs_app_get_size_installed (app_related);
	}

	return sz;
}

/**
 * gs_app_set_size_installed:
 * @app: a #GsApp
 * @size_installed: size in bytes, or %GS_APP_SIZE_UNKNOWABLE for invalid
 *
 * Sets the installed size of the application.
 *
 * Since: 3.22
 **/
void
gs_app_set_size_installed (GsApp *app, guint64 size_installed)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));
	if (size_installed == priv->size_installed)
		return;
	priv->size_installed = size_installed;
}

/**
 * gs_app_get_metadata_item:
 * @app: a #GsApp
 * @key: a string, e.g. "fwupd::device-id"
 *
 * Gets some metadata for the application.
 * Is is expected that plugins namespace any plugin-specific metadata,
 * for example `fwupd::device-id`.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.22
 **/
const gchar *
gs_app_get_metadata_item (GsApp *app, const gchar *key)
{
	GVariant *tmp;
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	tmp = gs_app_get_metadata_variant (app, key);
	if (tmp == NULL)
		return NULL;
	return g_variant_get_string (tmp, NULL);
}

/**
 * gs_app_set_metadata:
 * @app: a #GsApp
 * @key: a string, e.g. "fwupd::DeviceID"
 * @value: a string, e.g. "fubar"
 *
 * Sets some metadata for the application.
 * Is is expected that plugins namespace any plugin-specific metadata.
 *
 * Since: 3.22
 **/
void
gs_app_set_metadata (GsApp *app, const gchar *key, const gchar *value)
{
	g_autoptr(GVariant) tmp = NULL;
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (key != NULL);
	if (value != NULL)
		tmp = g_variant_new_string (value);
	gs_app_set_metadata_variant (app, key, tmp);
}

/**
 * gs_app_get_metadata_variant:
 * @app: a #GsApp
 * @key: a string, e.g. "fwupd::device-id"
 *
 * Gets some metadata for the application.
 * Is is expected that plugins namespace any plugin-specific metadata.
 *
 * Returns: a string, or %NULL for unset
 *
 * Since: 3.26
 **/
GVariant *
gs_app_get_metadata_variant (GsApp *app, const gchar *key)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	return g_hash_table_lookup (priv->metadata, key);
}

/**
 * gs_app_set_metadata_variant:
 * @app: a #GsApp
 * @key: a string, e.g. "fwupd::DeviceID"
 * @value: a #GVariant
 *
 * Sets some metadata for the application.
 * Is is expected that plugins namespace any plugin-specific metadata,
 * for example `fwupd::device-id`.
 *
 * Since: 3.26
 **/
void
gs_app_set_metadata_variant (GsApp *app, const gchar *key, GVariant *value)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	GVariant *found;

	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);

	/* if no value, then remove the key */
	if (value == NULL) {
		g_hash_table_remove (priv->metadata, key);
		return;
	}

	/* check we're not overwriting */
	found = g_hash_table_lookup (priv->metadata, key);
	if (found != NULL) {
		if (g_variant_equal (found, value))
			return;
		if (g_variant_type_equal (g_variant_get_type (value), G_VARIANT_TYPE_STRING) &&
		    g_variant_type_equal (g_variant_get_type (found), G_VARIANT_TYPE_STRING)) {
			g_debug ("tried overwriting %s key %s from %s to %s",
				 priv->id, key,
				 g_variant_get_string (found, NULL),
				 g_variant_get_string (value, NULL));
		} else {
			g_debug ("tried overwriting %s key %s (%s->%s)",
				 priv->id, key,
				 g_variant_get_type_string (found),
				 g_variant_get_type_string (value));
		}
		return;
	}
	g_hash_table_insert (priv->metadata, g_strdup (key), g_variant_ref (value));
}

/**
 * gs_app_get_addons:
 * @app: a #GsApp
 *
 * Gets the list of addons for the application.
 *
 * Returns: (transfer none): a list of addons
 *
 * Since: 3.22
 **/
GsAppList *
gs_app_get_addons (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->addons;
}

/**
 * gs_app_add_addon:
 * @app: a #GsApp
 * @addon: a #GsApp
 *
 * Adds an addon to the list of application addons.
 *
 * Since: 3.22
 **/
void
gs_app_add_addon (GsApp *app, GsApp *addon)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (GS_IS_APP (addon));

	locker = g_mutex_locker_new (&priv->mutex);
	gs_app_list_add (priv->addons, addon);
}

/**
 * gs_app_remove_addon:
 * @app: a #GsApp
 * @addon: a #GsApp
 *
 * Removes an addon from the list of application addons.
 *
 * Since: 3.22
 **/
void
gs_app_remove_addon (GsApp *app, GsApp *addon)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (GS_IS_APP (addon));
	locker = g_mutex_locker_new (&priv->mutex);
	gs_app_list_remove (priv->addons, addon);
}

/**
 * gs_app_get_related:
 * @app: a #GsApp
 *
 * Gets any related applications.
 *
 * Returns: (transfer none): a list of applications
 *
 * Since: 3.22
 **/
GsAppList *
gs_app_get_related (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->related;
}

/**
 * gs_app_add_related:
 * @app: a #GsApp
 * @app2: a #GsApp
 *
 * Adds a related application.
 *
 * Since: 3.22
 **/
void
gs_app_add_related (GsApp *app, GsApp *app2)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	GsAppPrivate *priv2 = gs_app_get_instance_private (app2);
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (GS_IS_APP (app2));

	locker = g_mutex_locker_new (&priv->mutex);

	/* if the app is updatable-live and any related app is not then
	 * degrade to the offline state */
	if (priv->state == AS_APP_STATE_UPDATABLE_LIVE &&
	    priv2->state == AS_APP_STATE_UPDATABLE)
		priv->state = priv2->state;

	gs_app_list_add (priv->related, app2);
}

/**
 * gs_app_get_history:
 * @app: a #GsApp
 *
 * Gets the history of this application.
 *
 * Returns: (transfer none): a list
 *
 * Since: 3.22
 **/
GsAppList *
gs_app_get_history (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->history;
}

/**
 * gs_app_add_history:
 * @app: a #GsApp
 * @app2: a #GsApp
 *
 * Adds a history item for this package.
 *
 * Since: 3.22
 **/
void
gs_app_add_history (GsApp *app, GsApp *app2)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (GS_IS_APP (app2));
	locker = g_mutex_locker_new (&priv->mutex);
	gs_app_list_add (priv->history, app2);
}

/**
 * gs_app_get_install_date:
 * @app: a #GsApp
 *
 * Gets the date that an application was installed.
 *
 * Returns: A UNIX epoch, or 0 for unset
 *
 * Since: 3.22
 **/
guint64
gs_app_get_install_date (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return priv->install_date;
}

/**
 * gs_app_set_install_date:
 * @app: a #GsApp
 * @install_date: an epoch, or %GS_APP_INSTALL_DATE_UNKNOWN
 *
 * Sets the date that an application was installed.
 *
 * Since: 3.22
 **/
void
gs_app_set_install_date (GsApp *app, guint64 install_date)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));
	if (install_date == priv->install_date)
		return;
	priv->install_date = install_date;
}

/**
 * gs_app_is_installed:
 * @app: a #GsApp
 *
 * Gets whether the app is installed or not.
 *
 * Returns: %TRUE if the app is installed, %FALSE otherwise.
 *
 * Since: 3.22
 **/
gboolean
gs_app_is_installed (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), FALSE);
	return (priv->state == AS_APP_STATE_INSTALLED) ||
	       (priv->state == AS_APP_STATE_UPDATABLE) ||
	       (priv->state == AS_APP_STATE_UPDATABLE_LIVE) ||
	       (priv->state == AS_APP_STATE_REMOVING);
}

/**
 * gs_app_is_updatable:
 * @app: a #GsApp
 *
 * Gets whether the app is updatable or not.
 *
 * Returns: %TRUE if the app is updatable, %FALSE otherwise.
 *
 * Since: 3.22
 **/
gboolean
gs_app_is_updatable (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), FALSE);
	if (priv->kind == AS_APP_KIND_OS_UPGRADE)
		return TRUE;
	return (priv->state == AS_APP_STATE_UPDATABLE) ||
	       (priv->state == AS_APP_STATE_UPDATABLE_LIVE);
}

/**
 * gs_app_get_categories:
 * @app: a #GsApp
 *
 * Gets the list of categories for an application.
 *
 * Returns: (element-type utf8) (transfer none): a list
 *
 * Since: 3.22
 **/
GPtrArray *
gs_app_get_categories (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->categories;
}

/**
 * gs_app_has_category:
 * @app: a #GsApp
 * @category: a category ID, e.g. "AudioVideo"
 *
 * Checks if the application is in a specific category.
 *
 * Returns: %TRUE for success
 *
 * Since: 3.22
 **/
gboolean
gs_app_has_category (GsApp *app, const gchar *category)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	const gchar *tmp;
	guint i;

	g_return_val_if_fail (GS_IS_APP (app), FALSE);

	/* find the category */
	for (i = 0; i < priv->categories->len; i++) {
		tmp = g_ptr_array_index (priv->categories, i);
		if (g_strcmp0 (tmp, category) == 0)
			return TRUE;
	}
	return FALSE;
}

/**
 * gs_app_set_categories:
 * @app: a #GsApp
 * @categories: a set of categories
 *
 * Set the list of categories for an application.
 *
 * Since: 3.22
 **/
void
gs_app_set_categories (GsApp *app, GPtrArray *categories)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (categories != NULL);
	locker = g_mutex_locker_new (&priv->mutex);
	_g_set_ptr_array (&priv->categories, categories);
}

/**
 * gs_app_add_category:
 * @app: a #GsApp
 * @category: a category ID, e.g. "AudioVideo"
 *
 * Adds a category ID to an application.
 *
 * Since: 3.22
 **/
void
gs_app_add_category (GsApp *app, const gchar *category)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (category != NULL);
	locker = g_mutex_locker_new (&priv->mutex);
	if (gs_app_has_category (app, category))
		return;
	g_ptr_array_add (priv->categories, g_strdup (category));
}

/**
 * gs_app_remove_category:
 * @app: a #GsApp
 * @category: a category ID, e.g. "AudioVideo"
 *
 * Removes an category ID from an application, it exists.
 *
 * Returns: %TRUE for success
 *
 * Since: 3.24
 **/
gboolean
gs_app_remove_category (GsApp *app, const gchar *category)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	const gchar *tmp;
	guint i;
	g_autoptr(GMutexLocker) locker = NULL;

	g_return_val_if_fail (GS_IS_APP (app), FALSE);

	locker = g_mutex_locker_new (&priv->mutex);

	for (i = 0; i < priv->categories->len; i++) {
		tmp = g_ptr_array_index (priv->categories, i);
		if (g_strcmp0 (tmp, category) != 0)
			continue;
		g_ptr_array_remove_index_fast (priv->categories, i);
		return TRUE;
	}
	return FALSE;
}

/**
 * gs_app_get_key_colors:
 * @app: a #GsApp
 *
 * Gets the key colors used in the application icon.
 *
 * Returns: (element-type GdkRGBA) (transfer none): a list
 *
 * Since: 3.22
 **/
GPtrArray *
gs_app_get_key_colors (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->key_colors;
}

/**
 * gs_app_set_key_colors:
 * @app: a #GsApp
 * @key_colors: (element-type GdkRGBA): a set of key colors
 *
 * Sets the key colors used in the application icon.
 *
 * Since: 3.22
 **/
void
gs_app_set_key_colors (GsApp *app, GPtrArray *key_colors)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (key_colors != NULL);
	locker = g_mutex_locker_new (&priv->mutex);
	_g_set_ptr_array (&priv->key_colors, key_colors);
}

/**
 * gs_app_add_key_color:
 * @app: a #GsApp
 * @key_color: a #GdkRGBA
 *
 * Adds a key colors used in the application icon.
 *
 * Since: 3.22
 **/
void
gs_app_add_key_color (GsApp *app, GdkRGBA *key_color)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (key_color != NULL);
	g_ptr_array_add (priv->key_colors, gdk_rgba_copy (key_color));
}

/**
 * gs_app_get_keywords:
 * @app: a #GsApp
 *
 * Gets the list of application keywords in the users locale.
 *
 * Returns: (element-type utf8) (transfer none): a list
 *
 * Since: 3.22
 **/
GPtrArray *
gs_app_get_keywords (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->keywords;
}

/**
 * gs_app_set_keywords:
 * @app: a #GsApp
 * @keywords: (element-type utf8): a set of keywords
 *
 * Sets the list of application keywords in the users locale.
 *
 * Since: 3.22
 **/
void
gs_app_set_keywords (GsApp *app, GPtrArray *keywords)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (keywords != NULL);
	locker = g_mutex_locker_new (&priv->mutex);
	_g_set_ptr_array (&priv->keywords, keywords);
}

/**
 * gs_app_add_kudo:
 * @app: a #GsApp
 * @kudo: a #GsAppKudo, e.g. %GS_APP_KUDO_MY_LANGUAGE
 *
 * Adds a kudo to the application.
 *
 * Since: 3.22
 **/
void
gs_app_add_kudo (GsApp *app, GsAppKudo kudo)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));
	if (kudo & GS_APP_KUDO_SANDBOXED_SECURE)
		kudo |= GS_APP_KUDO_SANDBOXED;
	priv->kudos |= kudo;
}

/**
 * gs_app_remove_kudo:
 * @app: a #GsApp
 * @kudo: a #GsAppKudo, e.g. %GS_APP_KUDO_MY_LANGUAGE
 *
 * Removes a kudo from the application.
 *
 * Since: 3.30
 **/
void
gs_app_remove_kudo (GsApp *app, GsAppKudo kudo)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));
	priv->kudos &= ~kudo;
}

/**
 * gs_app_has_kudo:
 * @app: a #GsApp
 * @kudo: a #GsAppKudo, e.g. %GS_APP_KUDO_MY_LANGUAGE
 *
 * Finds out if a kudo has been awarded by the application.
 *
 * Returns: %TRUE if the app has the specified kudo
 *
 * Since: 3.22
 **/
gboolean
gs_app_has_kudo (GsApp *app, GsAppKudo kudo)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), FALSE);
	return (priv->kudos & kudo) > 0;
}

/**
 * gs_app_get_kudos:
 * @app: a #GsApp
 *
 * Gets all the kudos the application has been awarded.
 *
 * Returns: the kudos, as a bitfield
 *
 * Since: 3.22
 **/
guint64
gs_app_get_kudos (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return priv->kudos;
}

/**
 * gs_app_get_kudos_percentage:
 * @app: a #GsApp
 *
 * Gets the kudos, as a percentage value.
 *
 * Returns: a percentage, with 0 for no kudos and a maximum of 100.
 *
 * Since: 3.22
 **/
guint
gs_app_get_kudos_percentage (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	guint percentage = 0;

	g_return_val_if_fail (GS_IS_APP (app), 0);

	if ((priv->kudos & GS_APP_KUDO_MY_LANGUAGE) > 0)
		percentage += 20;
	if ((priv->kudos & GS_APP_KUDO_RECENT_RELEASE) > 0)
		percentage += 20;
	if ((priv->kudos & GS_APP_KUDO_FEATURED_RECOMMENDED) > 0)
		percentage += 20;
	if ((priv->kudos & GS_APP_KUDO_MODERN_TOOLKIT) > 0)
		percentage += 20;
	if ((priv->kudos & GS_APP_KUDO_SEARCH_PROVIDER) > 0)
		percentage += 10;
	if ((priv->kudos & GS_APP_KUDO_INSTALLS_USER_DOCS) > 0)
		percentage += 10;
	if ((priv->kudos & GS_APP_KUDO_USES_NOTIFICATIONS) > 0)
		percentage += 20;
	if ((priv->kudos & GS_APP_KUDO_HAS_KEYWORDS) > 0)
		percentage += 5;
	if ((priv->kudos & GS_APP_KUDO_USES_APP_MENU) > 0)
		percentage += 10;
	if ((priv->kudos & GS_APP_KUDO_HAS_SCREENSHOTS) > 0)
		percentage += 20;
	if ((priv->kudos & GS_APP_KUDO_PERFECT_SCREENSHOTS) > 0)
		percentage += 20;
	if ((priv->kudos & GS_APP_KUDO_HIGH_CONTRAST) > 0)
		percentage += 20;
	if ((priv->kudos & GS_APP_KUDO_HI_DPI_ICON) > 0)
		percentage += 20;
	if ((priv->kudos & GS_APP_KUDO_SANDBOXED) > 0)
		percentage += 20;
	if ((priv->kudos & GS_APP_KUDO_SANDBOXED_SECURE) > 0)
		percentage += 20;

	/* popular apps should be at *least* 50% */
	if ((priv->kudos & GS_APP_KUDO_POPULAR) > 0)
		percentage = MAX (percentage, 50);

	return MIN (percentage, 100);
}

/**
 * gs_app_get_to_be_installed:
 * @app: a #GsApp
 *
 * Gets if the application is queued for installation.
 *
 * This is only set for addons when the user has selected some addons to be
 * installed before installing the main application.
 * Plugins should check all the addons for this property when installing
 * main applications so that the chosen set of addons is also installed at the
 * same time. This is never set when applications do not have addons.
 *
 * Returns: %TRUE for success
 *
 * Since: 3.22
 **/
gboolean
gs_app_get_to_be_installed (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), FALSE);

	return priv->to_be_installed;
}

/**
 * gs_app_set_to_be_installed:
 * @app: a #GsApp
 * @to_be_installed: if the app is due to be installed
 *
 * Sets if the application is queued for installation.
 *
 * Since: 3.22
 **/
void
gs_app_set_to_be_installed (GsApp *app, gboolean to_be_installed)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));

	priv->to_be_installed = to_be_installed;
}

/**
 * gs_app_has_quirk:
 * @app: a #GsApp
 * @quirk: a #AsAppQuirk, e.g. %AS_APP_QUIRK_COMPULSORY
 *
 * Finds out if an application has a specific quirk.
 *
 * Returns: %TRUE for success
 *
 * Since: 3.22
 **/
gboolean
gs_app_has_quirk (GsApp *app, AsAppQuirk quirk)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), FALSE);

	return (priv->quirk & quirk) > 0;
}

/**
 * gs_app_add_quirk:
 * @app: a #GsApp
 * @quirk: a #AsAppQuirk, e.g. %AS_APP_QUIRK_COMPULSORY
 *
 * Adds a quirk to an application.
 *
 * Since: 3.22
 **/
void
gs_app_add_quirk (GsApp *app, AsAppQuirk quirk)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);
	priv->quirk |= quirk;
	gs_app_queue_notify (app, "quirk");
}

/**
 * gs_app_remove_quirk:
 * @app: a #GsApp
 * @quirk: a #AsAppQuirk, e.g. %AS_APP_QUIRK_COMPULSORY
 *
 * Removes a quirk from an application.
 *
 * Since: 3.22
 **/
void
gs_app_remove_quirk (GsApp *app, AsAppQuirk quirk)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = NULL;
	g_return_if_fail (GS_IS_APP (app));

	locker = g_mutex_locker_new (&priv->mutex);
	priv->quirk &= ~quirk;
	gs_app_queue_notify (app, "quirk");
}

/**
 * gs_app_set_match_value:
 * @app: a #GsApp
 * @match_value: a value
 *
 * Set a match quality value, where higher values correspond to a
 * "better" search match, and should be shown above lower results.
 *
 * Since: 3.22
 **/
void
gs_app_set_match_value (GsApp *app, guint match_value)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));
	priv->match_value = match_value;
}

/**
 * gs_app_get_match_value:
 * @app: a #GsApp
 *
 * Get a match quality value, where higher values correspond to a
 * "better" search match, and should be shown above lower results.
 *
 * Note: This value is only valid when processing the result set
 * and may be overwritten on subsequent searches if the plugin is using
 * a cache.
 *
 * Returns: a value, where higher is better
 *
 * Since: 3.22
 **/
guint
gs_app_get_match_value (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return priv->match_value;
}

/**
 * gs_app_set_priority:
 * @app: a #GsApp
 * @priority: a value
 *
 * Set a priority value.
 *
 * Since: 3.22
 **/
void
gs_app_set_priority (GsApp *app, guint priority)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));
	priv->priority = priority;
}

/**
 * gs_app_get_priority:
 * @app: a #GsApp
 *
 * Get a priority value, where higher values will be chosen where
 * multiple #GsApp's match a specific rule.
 *
 * Returns: a value, where higher is better
 *
 * Since: 3.22
 **/
guint
gs_app_get_priority (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return priv->priority;
}

/**
 * gs_app_add_channel:
 * @app: a #GsApp
 * @channel: a #GsChannel
 *
 * Adds a channel to the application.
 *
 * Since: 3.28
 **/
void
gs_app_add_channel (GsApp *app, GsChannel *channel)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (GS_IS_CHANNEL (channel));
	g_ptr_array_add (priv->channels, g_object_ref (channel));
	if (priv->active_channel == NULL && gs_channel_get_version (channel) != NULL)
		priv->active_channel = g_object_ref (channel);
}

/**
 * gs_app_get_channels:
 * @app: a #GsApp
 *
 * Gets the list of channels.
 *
 * Returns: (element-type GsChannel) (transfer none): a list
 *
 * Since: 3.28
 **/
GPtrArray *
gs_app_get_channels (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->channels;
}

/**
 * gs_app_set_active_channel:
 * @app: a #GsApp
 * @channel: a #GsChannel
 *
 * Set the currently active channel.
 *
 * Since: 3.28
 **/
void
gs_app_set_active_channel (GsApp *app, GsChannel *channel)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (GS_IS_CHANNEL (channel));
	g_set_object (&priv->active_channel, channel);
}

/**
 * gs_app_get_active_channel:
 * @app: a #GsApp
 *
 * Gets the currently active channel.
 *
 * Returns: a #GsChannel or %NULL.
 *
 * Since: 3.28
 **/
GsChannel *
gs_app_get_active_channel (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return priv->active_channel;
}

/**
 * gs_app_get_cancellable:
 * @app: a #GsApp
 *
 * Get a cancellable to be used with operations related to the #GsApp. This is a
 * way for views to be able to cancel an on-going operation. If the #GCancellable
 * is canceled, it will be unreferenced and renewed before returning it, i.e. the
 * cancellable object will always be ready to use for new operations. So be sure
 * to keep a reference to it if you do more than just passing the cancellable to
 * a process.
 *
 * Returns: a #GCancellable
 *
 * Since: 3.28
 **/
GCancellable *
gs_app_get_cancellable (GsApp *app)
{
	g_autoptr(GCancellable) cancellable = NULL;
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);

	if (priv->cancellable == NULL || g_cancellable_is_cancelled (priv->cancellable)) {
		cancellable = g_cancellable_new ();
		g_set_object (&priv->cancellable, cancellable);
	}
	return priv->cancellable;
}

/**
 * gs_app_get_pending_action:
 * @app: a #GsApp
 *
 * Get the pending action for this #GsApp, or %NULL if no action is pending.
 *
 * Returns: the #GsAppAction of the @app.
 **/
GsPluginAction
gs_app_get_pending_action (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);
	return priv->pending_action;
}

/**
 * gs_app_set_pending_action:
 * @app: a #GsApp
 * @action: a #GsPluginAction
 *
 * Set an action that is pending on this #GsApp.
 **/
void
gs_app_set_pending_action (GsApp *app,
			   GsPluginAction action)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);
	gs_app_set_pending_action_internal (app, action);
}

static void
gs_app_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsApp *app = GS_APP (object);
	GsAppPrivate *priv = gs_app_get_instance_private (app);

	switch (prop_id) {
	case PROP_ID:
		g_value_set_string (value, priv->id);
		break;
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_VERSION:
		g_value_set_string (value, priv->version);
		break;
	case PROP_SUMMARY:
		g_value_set_string (value, priv->summary);
		break;
	case PROP_DESCRIPTION:
		g_value_set_string (value, priv->description);
		break;
	case PROP_RATING:
		g_value_set_int (value, priv->rating);
		break;
	case PROP_KIND:
		g_value_set_uint (value, priv->kind);
		break;
	case PROP_STATE:
		g_value_set_uint (value, priv->state);
		break;
	case PROP_PROGRESS:
		g_value_set_uint (value, priv->progress);
		break;
	case PROP_CAN_CANCEL_INSTALLATION:
		g_value_set_boolean (value, priv->allow_cancel);
		break;
	case PROP_INSTALL_DATE:
		g_value_set_uint64 (value, priv->install_date);
		break;
	case PROP_QUIRK:
		g_value_set_uint64 (value, priv->quirk);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsApp *app = GS_APP (object);
	GsAppPrivate *priv = gs_app_get_instance_private (app);

	switch (prop_id) {
	case PROP_ID:
		gs_app_set_id (app, g_value_get_string (value));
		break;
	case PROP_NAME:
		gs_app_set_name (app,
				 GS_APP_QUALITY_UNKNOWN,
				 g_value_get_string (value));
		break;
	case PROP_VERSION:
		gs_app_set_version (app, g_value_get_string (value));
		break;
	case PROP_SUMMARY:
		gs_app_set_summary (app,
				    GS_APP_QUALITY_UNKNOWN,
				    g_value_get_string (value));
		break;
	case PROP_DESCRIPTION:
		gs_app_set_description (app,
					GS_APP_QUALITY_UNKNOWN,
					g_value_get_string (value));
		break;
	case PROP_RATING:
		gs_app_set_rating (app, g_value_get_int (value));
		break;
	case PROP_KIND:
		gs_app_set_kind (app, g_value_get_uint (value));
		break;
	case PROP_STATE:
		gs_app_set_state_internal (app, g_value_get_uint (value));
		break;
	case PROP_PROGRESS:
		priv->progress = g_value_get_uint (value);
		break;
	case PROP_CAN_CANCEL_INSTALLATION:
		priv->allow_cancel = g_value_get_boolean (value);
		break;
	case PROP_INSTALL_DATE:
		gs_app_set_install_date (app, g_value_get_uint64 (value));
		break;
	case PROP_QUIRK:
		priv->quirk = g_value_get_uint64 (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_dispose (GObject *object)
{
	GsApp *app = GS_APP (object);
	GsAppPrivate *priv = gs_app_get_instance_private (app);

	g_clear_object (&priv->runtime);

	g_clear_pointer (&priv->addons, g_object_unref);
	g_clear_pointer (&priv->history, g_object_unref);
	g_clear_pointer (&priv->related, g_object_unref);
	g_clear_pointer (&priv->screenshots, g_ptr_array_unref);
	g_clear_pointer (&priv->review_ratings, g_array_unref);
	g_clear_pointer (&priv->reviews, g_ptr_array_unref);
	g_clear_pointer (&priv->provides, g_ptr_array_unref);
	g_clear_pointer (&priv->icons, g_ptr_array_unref);
	g_clear_pointer (&priv->channels, g_ptr_array_unref);
	g_clear_object (&priv->active_channel);

	G_OBJECT_CLASS (gs_app_parent_class)->dispose (object);
}

static void
gs_app_finalize (GObject *object)
{
	GsApp *app = GS_APP (object);
	GsAppPrivate *priv = gs_app_get_instance_private (app);

	g_mutex_clear (&priv->mutex);
	g_free (priv->id);
	g_free (priv->unique_id);
	g_free (priv->branch);
	g_free (priv->name);
	g_hash_table_unref (priv->urls);
	g_hash_table_unref (priv->launchables);
	g_free (priv->license);
	g_strfreev (priv->menu_path);
	g_free (priv->origin);
	g_free (priv->origin_appstream);
	g_free (priv->origin_hostname);
	g_ptr_array_unref (priv->sources);
	g_ptr_array_unref (priv->source_ids);
	g_free (priv->project_group);
	g_free (priv->developer_name);
	g_free (priv->agreement);
	g_free (priv->version);
	g_free (priv->version_ui);
	g_free (priv->summary);
	g_free (priv->summary_missing);
	g_free (priv->description);
	g_free (priv->update_version);
	g_free (priv->update_version_ui);
	g_free (priv->update_details);
	g_free (priv->management_plugin);
	g_hash_table_unref (priv->metadata);
	g_ptr_array_unref (priv->categories);
	g_ptr_array_unref (priv->key_colors);
	g_clear_object (&priv->cancellable);
	if (priv->keywords != NULL)
		g_ptr_array_unref (priv->keywords);
	if (priv->local_file != NULL)
		g_object_unref (priv->local_file);
	if (priv->content_rating != NULL)
		g_object_unref (priv->content_rating);
	if (priv->pixbuf != NULL)
		g_object_unref (priv->pixbuf);
	if (priv->price != NULL)
		g_object_unref (priv->price);

	G_OBJECT_CLASS (gs_app_parent_class)->finalize (object);
}

static void
gs_app_class_init (GsAppClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = gs_app_dispose;
	object_class->finalize = gs_app_finalize;
	object_class->get_property = gs_app_get_property;
	object_class->set_property = gs_app_set_property;

	/**
	 * GsApp:id:
	 */
	pspec = g_param_spec_string ("id", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_ID, pspec);

	/**
	 * GsApp:name:
	 */
	pspec = g_param_spec_string ("name", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_NAME, pspec);

	/**
	 * GsApp:version:
	 */
	pspec = g_param_spec_string ("version", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_VERSION, pspec);

	/**
	 * GsApp:summary:
	 */
	pspec = g_param_spec_string ("summary", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_SUMMARY, pspec);

	/**
	 * GsApp:description:
	 */
	pspec = g_param_spec_string ("description", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_DESCRIPTION, pspec);

	/**
	 * GsApp:rating:
	 */
	pspec = g_param_spec_int ("rating", NULL, NULL,
				  -1, 100, -1,
				  G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_RATING, pspec);

	/**
	 * GsApp:kind:
	 */
	pspec = g_param_spec_uint ("kind", NULL, NULL,
				   AS_APP_KIND_UNKNOWN,
				   AS_APP_KIND_LAST,
				   AS_APP_KIND_UNKNOWN,
				   G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_KIND, pspec);

	/**
	 * GsApp:state:
	 */
	pspec = g_param_spec_uint ("state", NULL, NULL,
				   AS_APP_STATE_UNKNOWN,
				   AS_APP_STATE_LAST,
				   AS_APP_STATE_UNKNOWN,
				   G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_STATE, pspec);

	/**
	 * GsApp:progress:
	 */
	pspec = g_param_spec_uint ("progress", NULL, NULL, 0, 100, 0,
				   G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_PROGRESS, pspec);

	/**
	 * GsApp:allow-cancel:
	 */
	pspec = g_param_spec_boolean ("allow-cancel", NULL, NULL, TRUE,
				      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_CAN_CANCEL_INSTALLATION, pspec);

	/**
	 * GsApp:install-date:
	 */
	pspec = g_param_spec_uint64 ("install-date", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_INSTALL_DATE, pspec);

	/**
	 * GsApp:quirk:
	 */
	pspec = g_param_spec_uint64 ("quirk", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_QUIRK, pspec);

	/**
	 * GsApp:pending-action:
	 */
	pspec = g_param_spec_uint64 ("pending-action", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READABLE | G_PARAM_PRIVATE);
	g_object_class_install_property (object_class, PROP_PENDING_ACTION, pspec);
}

static void
gs_app_init (GsApp *app)
{
	GsAppPrivate *priv = gs_app_get_instance_private (app);
	priv->rating = -1;
	priv->sources = g_ptr_array_new_with_free_func (g_free);
	priv->source_ids = g_ptr_array_new_with_free_func (g_free);
	priv->categories = g_ptr_array_new_with_free_func (g_free);
	priv->key_colors = g_ptr_array_new_with_free_func ((GDestroyNotify) gdk_rgba_free);
	priv->addons = gs_app_list_new ();
	priv->related = gs_app_list_new ();
	priv->history = gs_app_list_new ();
	priv->screenshots = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->reviews = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->provides = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->icons = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->channels = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	priv->metadata = g_hash_table_new_full (g_str_hash,
	                                        g_str_equal,
	                                        g_free,
	                                        (GDestroyNotify) g_variant_unref);
	priv->urls = g_hash_table_new_full (g_str_hash,
	                                    g_str_equal,
	                                    g_free,
	                                    g_free);
	priv->launchables = g_hash_table_new_full (g_str_hash,
	                                           g_str_equal,
	                                           g_free,
	                                           g_free);
	priv->allow_cancel = TRUE;
	g_mutex_init (&priv->mutex);
}

/**
 * gs_app_new:
 * @id: an application ID, or %NULL, e.g. "org.gnome.Software.desktop"
 *
 * Creates a new application object.
 *
 * The ID should only be set when the application ID (with optional prefix) is
 * known; it is perfectly valid to use gs_app_new() with an @id of %NULL, and
 * then relying on another plugin to set the @id using gs_app_set_id() based on
 * some other information.
 *
 * For instance, a #GsApp is created with no ID when returning results from the
 * packagekit plugin, but with the default source name set as the package name.
 * The source name is read by the appstream plugin, and if matched in the
 * AppStream XML the correct ID is set, along with other higher quality data
 * like the application icon and long description.
 *
 * Returns: a new #GsApp
 *
 * Since: 3.22
 **/
GsApp *
gs_app_new (const gchar *id)
{
	GsApp *app;
	app = g_object_new (GS_TYPE_APP,
			    "id", id,
			    NULL);
	return GS_APP (app);
}

/**
 * gs_app_set_from_unique_id:
 * @app: a #GsApp
 * @unique_id: an application unique ID, e.g.
 *	`system/flatpak/gnome/desktop/org.gnome.Software.desktop/master`
 *
 * Sets details on an application object.
 *
 * The unique ID will be parsed to set some information in the application such
 * as the scope, bundle kind, id, etc.
 *
 * Since: 3.26
 **/
void
gs_app_set_from_unique_id (GsApp *app, const gchar *unique_id)
{
	g_auto(GStrv) split = NULL;

	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (unique_id != NULL);

	split = g_strsplit (unique_id, "/", -1);
	if (g_strv_length (split) != 6)
		return;
	if (g_strcmp0 (split[0], "*") != 0)
		gs_app_set_scope (app, as_app_scope_from_string (split[0]));
	if (g_strcmp0 (split[1], "*") != 0)
		gs_app_set_bundle_kind (app, as_bundle_kind_from_string (split[1]));
	if (g_strcmp0 (split[2], "*") != 0)
		gs_app_set_origin (app, split[2]);
	if (g_strcmp0 (split[3], "*") != 0)
		gs_app_set_kind (app, as_app_kind_from_string (split[3]));
	if (g_strcmp0 (split[4], "*") != 0)
		gs_app_set_id (app, split[4]);
	if (g_strcmp0 (split[5], "*") != 0)
		gs_app_set_branch (app, split[5]);
}

/**
 * gs_app_new_from_unique_id:
 * @unique_id: an application unique ID, e.g.
 *	`system/flatpak/gnome/desktop/org.gnome.Software.desktop/master`
 *
 * Creates a new application object.
 *
 * The unique ID will be parsed to set some information in the application such
 * as the scope, bundle kind, id, etc. Unlike gs_app_new(), it cannot take a
 * %NULL argument.
 *
 * Returns: a new #GsApp
 *
 * Since: 3.22
 **/
GsApp *
gs_app_new_from_unique_id (const gchar *unique_id)
{
	GsApp *app;
	g_return_val_if_fail (unique_id != NULL, NULL);
	app = gs_app_new (NULL);
	gs_app_set_from_unique_id (app, unique_id);
	return app;
}

/* vim: set noexpandtab: */
