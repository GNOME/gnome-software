/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#include "gs-app-private.h"
#include "gs-plugin.h"
#include "gs-utils.h"

struct _GsApp
{
	GObject			 parent_instance;

	gchar			*id;
	gchar			*name;
	GsAppQuality		 name_quality;
	GPtrArray		*icons;
	GPtrArray		*sources;
	GPtrArray		*source_ids;
	gchar			*project_group;
	gchar			*version;
	gchar			*version_ui;
	gchar			*summary;
	GsAppQuality		 summary_quality;
	gchar			*summary_missing;
	gchar			*description;
	GsAppQuality		 description_quality;
	GError			*last_error;
	GPtrArray		*screenshots;
	GPtrArray		*categories;
	GPtrArray		*key_colors;
	GPtrArray		*keywords;
	GHashTable		*urls;
	gchar			*license;
	GsAppQuality		 license_quality;
	gchar			**menu_path;
	gchar			*origin;
	gchar			*origin_ui;
	gchar			*update_version;
	gchar			*update_version_ui;
	gchar			*update_details;
	AsUrgencyKind		 update_urgency;
	gchar			*management_plugin;
	guint			 match_value;
	guint			 priority;
	gint			 rating;
	GArray			*review_ratings;
	GPtrArray		*reviews; /* of GsReview */
	guint64			 size_installed;
	guint64			 size_download;
	AsAppKind		 kind;
	AsAppState		 state;
	AsAppState		 state_recover;
	guint			 progress;
	GHashTable		*metadata;
	GPtrArray		*addons; /* of GsApp */
	GHashTable		*addons_hash; /* of "id" */
	GPtrArray		*related; /* of GsApp */
	GHashTable		*related_hash; /* of "id-source" */
	GPtrArray		*history; /* of GsApp */
	guint64			 install_date;
	guint64			 kudos;
	gboolean		 to_be_installed;
	AsAppQuirk		 quirk;
	gboolean		 license_is_free;
	GsApp			*runtime;
	GFile			*local_file;
	GdkPixbuf		*pixbuf;
};

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
	PROP_INSTALL_DATE,
	PROP_QUIRK,
	PROP_LAST
};

G_DEFINE_TYPE (GsApp, gs_app, G_TYPE_OBJECT)

/**
 * gs_app_kv_lpad:
 **/
static void
gs_app_kv_lpad (GString *str, const gchar *key, const gchar *value)
{
	guint i;
	g_string_append_printf (str, "  %s:", key);
	for (i = strlen (key); i < 18; i++)
		g_string_append (str, " ");
	g_string_append_printf (str, " %s\n", value);
}

/**
 * gs_app_kv_printf:
 **/
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

/**
 * _as_app_quirk_flag_to_string:
 **/
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
	return NULL;
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

/**
 * gs_app_to_string:
 * @app: a #GsApp
 *
 * Converts the application to a string.
 * This is not designed to serialize the object but to produce a string suitable
 * for debugging.
 *
 * Returns: A multi-line string
 **/
gchar *
gs_app_to_string (GsApp *app)
{
	AsImage *im;
	AsScreenshot *ss;
	GList *keys;
	GList *l;
	GString *str;
	const gchar *tmp;
	guint i;

	g_return_val_if_fail (GS_IS_APP (app), NULL);

	str = g_string_new ("GsApp:");
	g_string_append_printf (str, " [%p]\n", app);
	gs_app_kv_lpad (str, "kind", as_app_kind_to_string (app->kind));
	if (app->last_error != NULL)
		gs_app_kv_lpad (str, "last-error", app->last_error->message);
	gs_app_kv_lpad (str, "state", as_app_state_to_string (app->state));
	if (app->quirk > 0) {
		g_autofree gchar *qstr = _as_app_quirk_to_string (app->quirk);
		gs_app_kv_lpad (str, "quirk", qstr);
	}
	if (app->progress > 0)
		gs_app_kv_printf (str, "progress", "%i%%", app->progress);
	if (app->id != NULL)
		gs_app_kv_lpad (str, "id", app->id);
	if ((app->kudos & GS_APP_KUDO_MY_LANGUAGE) > 0)
		gs_app_kv_lpad (str, "kudo", "my-language");
	if ((app->kudos & GS_APP_KUDO_RECENT_RELEASE) > 0)
		gs_app_kv_lpad (str, "kudo", "recent-release");
	if ((app->kudos & GS_APP_KUDO_FEATURED_RECOMMENDED) > 0)
		gs_app_kv_lpad (str, "kudo", "featured-recommended");
	if ((app->kudos & GS_APP_KUDO_MODERN_TOOLKIT) > 0)
		gs_app_kv_lpad (str, "kudo", "modern-toolkit");
	if ((app->kudos & GS_APP_KUDO_SEARCH_PROVIDER) > 0)
		gs_app_kv_lpad (str, "kudo", "search-provider");
	if ((app->kudos & GS_APP_KUDO_INSTALLS_USER_DOCS) > 0)
		gs_app_kv_lpad (str, "kudo", "installs-user-docs");
	if ((app->kudos & GS_APP_KUDO_USES_NOTIFICATIONS) > 0)
		gs_app_kv_lpad (str, "kudo", "uses-notifications");
	if ((app->kudos & GS_APP_KUDO_USES_APP_MENU) > 0)
		gs_app_kv_lpad (str, "kudo", "uses-app-menu");
	if ((app->kudos & GS_APP_KUDO_HAS_KEYWORDS) > 0)
		gs_app_kv_lpad (str, "kudo", "has-keywords");
	if ((app->kudos & GS_APP_KUDO_HAS_SCREENSHOTS) > 0)
		gs_app_kv_lpad (str, "kudo", "has-screenshots");
	if ((app->kudos & GS_APP_KUDO_POPULAR) > 0)
		gs_app_kv_lpad (str, "kudo", "popular");
	if ((app->kudos & GS_APP_KUDO_PERFECT_SCREENSHOTS) > 0)
		gs_app_kv_lpad (str, "kudo", "perfect-screenshots");
	if ((app->kudos & GS_APP_KUDO_HIGH_CONTRAST) > 0)
		gs_app_kv_lpad (str, "kudo", "high-contrast");
	if ((app->kudos & GS_APP_KUDO_HI_DPI_ICON) > 0)
		gs_app_kv_lpad (str, "kudo", "hi-dpi-icon");
	gs_app_kv_printf (str, "kudo-percentage", "%i",
			  gs_app_get_kudos_percentage (app));
	if (app->name != NULL)
		gs_app_kv_lpad (str, "name", app->name);
	gs_app_kv_printf (str, "pixbuf", "%p", app->pixbuf);
	for (i = 0; i < app->icons->len; i++) {
		AsIcon *icon = g_ptr_array_index (app->icons, i);
		gs_app_kv_lpad (str, "icon-kind",
				as_icon_kind_to_string (as_icon_get_kind (icon)));
		gs_app_kv_printf (str, "icon-pixbuf", "%p",
				  as_icon_get_pixbuf (icon));
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
	if (app->match_value != 0)
		gs_app_kv_printf (str, "match-value", "%05x", app->match_value);
	if (app->priority != 0)
		gs_app_kv_printf (str, "priority", "%i", app->priority);
	if (app->version != NULL)
		gs_app_kv_lpad (str, "version", app->version);
	if (app->version_ui != NULL)
		gs_app_kv_lpad (str, "version-ui", app->version_ui);
	if (app->update_version != NULL)
		gs_app_kv_lpad (str, "update-version", app->update_version);
	if (app->update_version_ui != NULL)
		gs_app_kv_lpad (str, "update-version-ui", app->update_version_ui);
	if (app->update_details != NULL)
		gs_app_kv_lpad (str, "update-details", app->update_details);
	if (app->update_urgency != AS_URGENCY_KIND_UNKNOWN) {
		gs_app_kv_printf (str, "update-urgency", "%i",
				  app->update_urgency);
	}
	if (app->summary != NULL)
		gs_app_kv_lpad (str, "summary", app->summary);
	if (app->description != NULL)
		gs_app_kv_lpad (str, "description", app->description);
	for (i = 0; i < app->screenshots->len; i++) {
		g_autofree gchar *key = NULL;
		ss = g_ptr_array_index (app->screenshots, i);
		tmp = as_screenshot_get_caption (ss, NULL);
		im = as_screenshot_get_image (ss, 0, 0);
		if (im == NULL)
			continue;
		key = g_strdup_printf ("screenshot-%02i", i);
		gs_app_kv_printf (str, key, "%s [%s]",
				  as_image_get_url (im),
				  tmp != NULL ? tmp : "<none>");
	}
	for (i = 0; i < app->sources->len; i++) {
		g_autofree gchar *key = NULL;
		tmp = g_ptr_array_index (app->sources, i);
		key = g_strdup_printf ("source-%02i", i);
		gs_app_kv_lpad (str, key, tmp);
	}
	for (i = 0; i < app->source_ids->len; i++) {
		g_autofree gchar *key = NULL;
		tmp = g_ptr_array_index (app->source_ids, i);
		key = g_strdup_printf ("source-id-%02i", i);
		gs_app_kv_lpad (str, key, tmp);
	}
	if (app->local_file != NULL) {
		g_autofree gchar *fn = g_file_get_path (app->local_file);
		gs_app_kv_lpad (str, "local-filename", fn);
	}
	tmp = g_hash_table_lookup (app->urls, as_url_kind_to_string (AS_URL_KIND_HOMEPAGE));
	if (tmp != NULL)
		gs_app_kv_lpad (str, "url{homepage}", tmp);
	if (app->license != NULL) {
		gs_app_kv_lpad (str, "license", app->license);
		gs_app_kv_lpad (str, "license-is-free",
				gs_app_get_license_is_free (app) ? "yes" : "no");
	}
	if (app->management_plugin != NULL)
		gs_app_kv_lpad (str, "management-plugin", app->management_plugin);
	if (app->summary_missing != NULL)
		gs_app_kv_lpad (str, "summary-missing", app->summary_missing);
	if (app->menu_path != NULL &&
	    app->menu_path[0] != NULL &&
	    app->menu_path[0][0] != '\0') {
		g_autofree gchar *path = g_strjoinv (" → ", app->menu_path);
		gs_app_kv_lpad (str, "menu-path", path);
	}
	if (app->origin != NULL && app->origin[0] != '\0')
		gs_app_kv_lpad (str, "origin", app->origin);
	if (app->origin_ui != NULL && app->origin_ui[0] != '\0')
		gs_app_kv_lpad (str, "origin-ui", app->origin_ui);
	if (app->rating != -1)
		gs_app_kv_printf (str, "rating", "%i", app->rating);
	if (app->review_ratings != NULL) {
		for (i = 0; i < app->review_ratings->len; i++) {
			gint rat = g_array_index (app->review_ratings, gint, i);
			gs_app_kv_printf (str, "review-rating", "[%i:%i]",
					  i, rat);
		}
	}
	if (app->reviews != NULL)
		gs_app_kv_printf (str, "reviews", "%i", app->reviews->len);
	if (app->install_date != 0) {
		gs_app_kv_printf (str, "install-date", "%"
				  G_GUINT64_FORMAT "",
				  app->install_date);
	}
	if (app->size_installed != 0) {
		gs_app_kv_printf (str, "size-installed",
				  "%" G_GUINT64_FORMAT "k",
				  app->size_installed / 1024);
	}
	if (app->size_download != 0) {
		gs_app_kv_printf (str, "size-download",
				  "%" G_GUINT64_FORMAT "k",
				  app->size_download / 1024);
	}
	if (app->related->len > 0)
		gs_app_kv_printf (str, "related", "%i", app->related->len);
	if (app->history->len > 0)
		gs_app_kv_printf (str, "history", "%i", app->history->len);
	for (i = 0; i < app->categories->len; i++) {
		tmp = g_ptr_array_index (app->categories, i);
		gs_app_kv_lpad (str, "category", tmp);
	}
	for (i = 0; i < app->key_colors->len; i++) {
		GdkRGBA *color = g_ptr_array_index (app->key_colors, i);
		g_autofree gchar *key = NULL;
		key = g_strdup_printf ("key-color-%02i", i);
		gs_app_kv_printf (str, key, "%.0f,%.0f,%.0f",
				  color->red, color->green, color->blue);
	}
	if (app->keywords != NULL) {
		for (i = 0; i < app->keywords->len; i++) {
			tmp = g_ptr_array_index (app->keywords, i);
			gs_app_kv_lpad (str, "keyword", tmp);
		}
	}
	keys = g_hash_table_get_keys (app->metadata);
	for (l = keys; l != NULL; l = l->next) {
		g_autofree gchar *key = NULL;
		key = g_strdup_printf ("{%s}", (const gchar *) l->data);
		tmp = g_hash_table_lookup (app->metadata, l->data);
		gs_app_kv_lpad (str, key, tmp);
	}
	g_list_free (keys);

	/* print runtime data too */
	if (app->runtime != NULL) {
		g_autofree gchar *runtime = gs_app_to_string (app->runtime);
		g_string_append_printf (str, "\n\tRuntime:\n\t%s\n", runtime);
	}

	return g_string_free (str, FALSE);
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
 * Returns: The whole ID, e.g. "gimp.desktop" or "flatpak:org.gnome.Gimp.desktop"
 **/
const gchar *
gs_app_get_id (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->id;
}

/**
 * gs_app_get_id_no_prefix:
 * @app: a #GsApp
 *
 * Gets the application ID without any prefix set.
 *
 * Returns: The whole ID, e.g. gimp.desktop" or "org.gnome.Gimp.desktop"
 **/
const gchar *
gs_app_get_id_no_prefix (GsApp *app)
{
	gchar *tmp;
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	if (app->id == NULL)
		return NULL;
	tmp = g_strrstr (app->id, ":");
	if (tmp != NULL)
		return tmp + 1;
	return app->id;
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
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->id);
	app->id = g_strdup (id);
}

/**
 * gs_app_get_state:
 * @app: a #GsApp
 *
 * Gets the state of the application.
 *
 * Returns: the #AsAppState, e.g. %AS_APP_STATE_INSTALLED
 **/
AsAppState
gs_app_get_state (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), AS_APP_STATE_UNKNOWN);
	return app->state;
}

/**
 * gs_app_get_progress:
 * @app: a #GsApp
 *
 * Gets the percentage completion.
 *
 * Returns: the percentage completion, or 0 for unknown
 **/
guint
gs_app_get_progress (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return app->progress;
}

/**
 * gs_app_set_state_recover:
 * @app: a #GsApp
 *
 * Sets the application state to the last status value that was not
 * transient.
 **/
void
gs_app_set_state_recover (GsApp *app)
{
	if (app->state_recover == AS_APP_STATE_UNKNOWN)
		return;
	if (app->state_recover == app->state)
		return;

	g_debug ("recovering state on %s from %s to %s",
		 app->id,
		 as_app_state_to_string (app->state),
		 as_app_state_to_string (app->state_recover));

	app->state = app->state_recover;
	gs_app_queue_notify (app, "state");
}

/**
 * gs_app_set_state_internal:
 */
static gboolean
gs_app_set_state_internal (GsApp *app, AsAppState state)
{
	gboolean state_change_ok = FALSE;

	if (app->state == state)
		return FALSE;

	/* check the state change is allowed */
	switch (app->state) {
	case AS_APP_STATE_UNKNOWN:
		/* unknown has to go into one of the stable states */
		if (state == AS_APP_STATE_INSTALLED ||
		    state == AS_APP_STATE_QUEUED_FOR_INSTALL ||
		    state == AS_APP_STATE_AVAILABLE ||
		    state == AS_APP_STATE_AVAILABLE_LOCAL ||
		    state == AS_APP_STATE_UPDATABLE ||
		    state == AS_APP_STATE_UPDATABLE_LIVE ||
		    state == AS_APP_STATE_UNAVAILABLE)
			state_change_ok = TRUE;
		break;
	case AS_APP_STATE_INSTALLED:
		/* installed has to go into an action state */
		if (state == AS_APP_STATE_UNKNOWN ||
		    state == AS_APP_STATE_REMOVING)
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
	default:
		g_warning ("state %s unhandled",
			   as_app_state_to_string (app->state));
		g_assert_not_reached ();
	}

	/* this state change was unexpected */
	if (!state_change_ok) {
		g_warning ("State change on %s from %s to %s is not OK",
			   app->id,
			   as_app_state_to_string (app->state),
			   as_app_state_to_string (state));
		return FALSE;
	}

	app->state = state;

	if (state == AS_APP_STATE_UNKNOWN ||
	    state == AS_APP_STATE_AVAILABLE_LOCAL ||
	    state == AS_APP_STATE_AVAILABLE)
		app->install_date = 0;

	/* save this to simplify error handling in the plugins */
	switch (state) {
	case AS_APP_STATE_INSTALLING:
	case AS_APP_STATE_REMOVING:
		/* transient, so ignore */
		break;
	default:
		if (app->state_recover != state) {
			g_debug ("%s non-transient state now %s",
				 app->id, as_app_state_to_string (state));
			app->state_recover = state;
		}

		/* clear the error as the application has changed state */
		g_clear_error (&app->last_error);
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
 **/
void
gs_app_set_progress (GsApp *app, guint percentage)
{
	g_return_if_fail (GS_IS_APP (app));
	if (app->progress == percentage)
		return;
	app->progress = percentage;
	gs_app_queue_notify (app, "progress");
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
 * Plugins are reponsible for changing the state to one of the other
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
 **/
void
gs_app_set_state (GsApp *app, AsAppState state)
{
	g_return_if_fail (GS_IS_APP (app));

	if (gs_app_set_state_internal (app, state))
		gs_app_queue_notify (app, "state");
}

/**
 * gs_app_get_kind:
 * @app: a #GsApp
 *
 * Gets the kind of the application.
 *
 * Returns: the #AsAppKind, e.g. %AS_APP_KIND_UNKNOWN
 **/
AsAppKind
gs_app_get_kind (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), AS_APP_KIND_UNKNOWN);
	return app->kind;
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
 **/
void
gs_app_set_kind (GsApp *app, AsAppKind kind)
{
	gboolean state_change_ok = FALSE;

	g_return_if_fail (GS_IS_APP (app));
	if (app->kind == kind)
		return;

	/* check the state change is allowed */
	switch (app->kind) {
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
			   app->id,
			   as_app_kind_to_string (app->kind),
			   as_app_kind_to_string (kind));
		return;
	}

	app->kind = kind;
	gs_app_queue_notify (app, "kind");
}

/**
 * gs_app_get_name:
 * @app: a #GsApp
 *
 * Gets the application name.
 *
 * Returns: a string, or %NULL for unset
 **/
const gchar *
gs_app_get_name (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->name;
}

/**
 * gs_app_set_name:
 * @app: a #GsApp
 * @quality: A #GsAppQuality, e.g. %GS_APP_QUALITY_LOWEST
 * @name: The short localized name, e.g. "Calculator"
 *
 * Sets the application name.
 **/
void
gs_app_set_name (GsApp *app, GsAppQuality quality, const gchar *name)
{
	g_return_if_fail (GS_IS_APP (app));

	/* only save this if the data is sufficiently high quality */
	if (quality <= app->name_quality)
		return;
	app->name_quality = quality;

	g_free (app->name);
	app->name = g_strdup (name);
}

/**
 * gs_app_get_source_default:
 * @app: a #GsApp
 *
 * Gets the default source.
 *
 * Returns: a string, or %NULL
 **/
const gchar *
gs_app_get_source_default (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	if (app->sources->len == 0)
		return NULL;
	return g_ptr_array_index (app->sources, 0);
}

/**
 * gs_app_add_source:
 * @app: a #GsApp
 * @source: a source name
 *
 * Adds a source name for the application.
 **/
void
gs_app_add_source (GsApp *app, const gchar *source)
{
	const gchar *tmp;
	guint i;

	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (source != NULL);

	/* check source doesn't already exist */
	for (i = 0; i < app->sources->len; i++) {
		tmp = g_ptr_array_index (app->sources, i);
		if (g_strcmp0 (tmp, source) == 0)
			return;
	}
	g_ptr_array_add (app->sources, g_strdup (source));
}

/**
 * gs_app_get_sources:
 * @app: a #GsApp
 *
 * Gets the list of sources for the application.
 *
 * Returns: (element-type utf8) (transfer none): a list
 **/
GPtrArray *
gs_app_get_sources (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->sources;
}

/**
 * gs_app_set_sources:
 * @app: a #GsApp
 * @sources: The non-localized short names, e.g. ["gnome-calculator"]
 *
 * This name is used for the update page if the application is collected into
 * the 'OS Updates' group.
 * It is typically the package names, although this should not be relied upon.
 **/
void
gs_app_set_sources (GsApp *app, GPtrArray *sources)
{
	g_return_if_fail (GS_IS_APP (app));
	if (app->sources != NULL)
		g_ptr_array_unref (app->sources);
	app->sources = g_ptr_array_ref (sources);
}

/**
 * gs_app_get_source_id_default:
 * @app: a #GsApp
 *
 * Gets the default source ID.
 *
 * Returns: a string, or %NULL for unset
 **/
const gchar *
gs_app_get_source_id_default (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	if (app->source_ids->len == 0)
		return NULL;
	return g_ptr_array_index (app->source_ids, 0);
}

/**
 * gs_app_get_source_ids:
 * @app: a #GsApp
 *
 * Gets the list of source IDs.
 *
 * Returns: (element-type utf8) (transfer none): a list
 **/
GPtrArray *
gs_app_get_source_ids (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->source_ids;
}

/**
 * gs_app_clear_source_ids:
 * @app: a #GsApp
 *
 * Clear the list of source IDs.
 **/
void
gs_app_clear_source_ids (GsApp *app)
{
	g_return_if_fail (GS_IS_APP (app));
	g_ptr_array_set_size (app->source_ids, 0);
}

/**
 * gs_app_set_source_ids:
 * @app: a #GsApp
 * @source_ids: The source-id, e.g. ["gnome-calculator;0.134;fedora"]
 *		or ["/home/hughsie/.local/share/applications/0ad.desktop"]
 *
 * This ID is used internally to the controlling plugin.
 **/
void
gs_app_set_source_ids (GsApp *app, GPtrArray *source_ids)
{
	g_return_if_fail (GS_IS_APP (app));
	if (app->source_ids != NULL)
		g_ptr_array_unref (app->source_ids);
	app->source_ids = g_ptr_array_ref (source_ids);
}

/**
 * gs_app_add_source_id:
 * @app: a #GsApp
 * @source_id: a source ID, e.g. "gnome-calculator;0.134;fedora"
 *
 * Adds a source ID to the application.
 **/
void
gs_app_add_source_id (GsApp *app, const gchar *source_id)
{
	const gchar *tmp;
	guint i;

	g_return_if_fail (GS_IS_APP (app));

	/* only add if not already present */
	for (i = 0; i < app->source_ids->len; i++) {
		tmp = g_ptr_array_index (app->source_ids, i);
		if (g_strcmp0 (tmp, source_id) == 0)
			return;
	}
	g_ptr_array_add (app->source_ids, g_strdup (source_id));
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
 **/
const gchar *
gs_app_get_project_group (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->project_group;
}

/**
 * gs_app_set_project_group:
 * @app: a #GsApp
 * @project_group: The non-localized project group, e.g. "GNOME" or "KDE"
 *
 * Sets a project group for the application.
 **/
void
gs_app_set_project_group (GsApp *app, const gchar *project_group)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->project_group);
	app->project_group = g_strdup (project_group);
}

/**
 * gs_app_get_pixbuf:
 * @app: a #GsApp
 *
 * Gets a pixbuf to represent the application.
 *
 * Returns: (transfer none): a #GdkPixbuf, or %NULL
 **/
GdkPixbuf *
gs_app_get_pixbuf (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->pixbuf;
}

/**
 * gs_app_get_icons:
 * @app: a #GsApp
 *
 * Gets the icons for the application.
 *
 * Returns: (transfer none) (element-type AsIcon): an array of icons
 **/
GPtrArray *
gs_app_get_icons (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->icons;
}

/**
 * gs_app_add_icon:
 * @app: a #GsApp
 * @icon: a #AsIcon, or %NULL to remove all icons
 *
 * Adds an icon to use for the application.
 * If the first icon added cannot be loaded then the next one is tried.
 **/
void
gs_app_add_icon (GsApp *app, AsIcon *icon)
{
	g_return_if_fail (GS_IS_APP (app));
	if (icon == NULL) {
		g_ptr_array_set_size (app->icons, 0);
		return;
	}
	g_ptr_array_add (app->icons, g_object_ref (icon));
}

/**
 * gs_app_get_local_file:
 * @app: a #GsApp
 *
 * Gets the file that backs this application, for instance this might
 * be a local file in ~/Downloads that we are installing.
 *
 * Returns: (transfer none): a #GFile, or %NULL
 **/
GFile *
gs_app_get_local_file (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->local_file;
}

/**
 * gs_app_set_local_file:
 * @app: a #GsApp
 * @local_file: a #GFile, or %NULL
 *
 * Sets the file that backs this application, for instance this might
 * be a local file in ~/Downloads that we are installing.
 **/
void
gs_app_set_local_file (GsApp *app, GFile *local_file)
{
	g_return_if_fail (GS_IS_APP (app));
	g_set_object (&app->local_file, local_file);
}

/**
 * gs_app_get_runtime:
 * @app: a #GsApp
 *
 * Gets the runtime for the application.
 *
 * Returns: (transfer none): a #GsApp, or %NULL for unset
 **/
GsApp *
gs_app_get_runtime (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->runtime;
}

/**
 * gs_app_set_runtime:
 * @app: a #GsApp
 * @runtime: a #GsApp
 *
 * Sets the runtime that the application requires.
 **/
void
gs_app_set_runtime (GsApp *app, GsApp *runtime)
{
	g_return_if_fail (GS_IS_APP (app));
	g_set_object (&app->runtime, runtime);
}

/**
 * gs_app_set_pixbuf:
 * @app: a #GsApp
 * @pixbuf: a #GdkPixbuf, or %NULL
 *
 * Sets a pixbuf used to represent the application.
 **/
void
gs_app_set_pixbuf (GsApp *app, GdkPixbuf *pixbuf)
{
	g_return_if_fail (GS_IS_APP (app));
	g_set_object (&app->pixbuf, pixbuf);
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

/**
 * gs_app_ui_versions_invalidate:
 */
static void
gs_app_ui_versions_invalidate (GsApp *app)
{
	g_free (app->version_ui);
	g_free (app->update_version_ui);
	app->version_ui = NULL;
	app->update_version_ui = NULL;
}

/**
 * gs_app_ui_versions_populate:
 */
static void
gs_app_ui_versions_populate (GsApp *app)
{
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
		app->version_ui = gs_app_get_ui_version (app->version, flags[i]);
		app->update_version_ui = gs_app_get_ui_version (app->update_version, flags[i]);
		if (g_strcmp0 (app->version_ui, app->update_version_ui) != 0) {
			gs_app_queue_notify (app, "version");
			return;
		}
		gs_app_ui_versions_invalidate (app);
	}

	/* we tried, but failed */
	app->version_ui = g_strdup (app->version);
	app->update_version_ui = g_strdup (app->update_version);
}

/**
 * gs_app_get_version:
 * @app: a #GsApp
 *
 * Gets the exact version for the application.
 *
 * Returns: a string, or %NULL for unset
 **/
const gchar *
gs_app_get_version (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->version;
}

/**
 * gs_app_get_version_ui:
 * @app: a #GsApp
 *
 * Gets a version string that can be displayed in a UI.
 *
 * Returns: a string, or %NULL for unset
 **/
const gchar *
gs_app_get_version_ui (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);

	/* work out the two version numbers */
	if (app->version != NULL &&
	    app->version_ui == NULL) {
		gs_app_ui_versions_populate (app);
	}

	return app->version_ui;
}

/**
 * gs_app_set_version:
 * @app: a #GsApp
 * @version: The version, e.g. "2:1.2.3.fc19"
 *
 * This saves the version after stripping out any non-friendly parts, such as
 * distro tags, git revisions and that kind of thing.
 **/
void
gs_app_set_version (GsApp *app, const gchar *version)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->version);
	app->version = g_strdup (version);
	gs_app_ui_versions_invalidate (app);
	gs_app_queue_notify (app, "version");
}

/**
 * gs_app_get_summary:
 * @app: a #GsApp
 *
 * Gets the single-line description of the application.
 *
 * Returns: a string, or %NULL for unset
 **/
const gchar *
gs_app_get_summary (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->summary;
}

/**
 * gs_app_set_summary:
 * @app: a #GsApp
 * @quality: a #GsAppQuality, e.g. %GS_APP_QUALITY_LOWEST
 * @summary: a string, e.g. "A graphical calculator for GNOME"
 *
 * The medium length one-line localized name.
 **/
void
gs_app_set_summary (GsApp *app, GsAppQuality quality, const gchar *summary)
{
	g_return_if_fail (GS_IS_APP (app));

	/* only save this if the data is sufficiently high quality */
	if (quality <= app->summary_quality)
		return;
	app->summary_quality = quality;

	g_free (app->summary);
	app->summary = g_strdup (summary);
}

/**
 * gs_app_get_description:
 * @app: a #GsApp
 *
 * Gets the long multi-line description of the application.
 *
 * Returns: a string, or %NULL for unset
 **/
const gchar *
gs_app_get_description (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->description;
}

/**
 * gs_app_set_description:
 * @app: a #GsApp
 * @quality: a #GsAppQuality, e.g. %GS_APP_QUALITY_LOWEST
 * @description: a string, e.g. "GNOME Calculator is a graphical calculator for GNOME..."
 *
 * Sets the long multi-line description of the application.
 **/
void
gs_app_set_description (GsApp *app, GsAppQuality quality, const gchar *description)
{
	g_return_if_fail (GS_IS_APP (app));

	/* only save this if the data is sufficiently high quality */
	if (quality <= app->description_quality)
		return;
	app->description_quality = quality;

	g_free (app->description);
	app->description = g_strdup (description);
}

/**
 * gs_app_get_url:
 * @app: a #GsApp
 * @kind: a #AsUrlKind, e.g. %AS_URL_KIND_HOMEPAGE
 *
 * Gets a web address of a specific type.
 *
 * Returns: a string, or %NULL for unset
 **/
const gchar *
gs_app_get_url (GsApp *app, AsUrlKind kind)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return g_hash_table_lookup (app->urls, as_url_kind_to_string (kind));
}

/**
 * gs_app_set_url:
 * @app: a #GsApp
 * @kind: a #AsUrlKind, e.g. %AS_URL_KIND_HOMEPAGE
 * @url: a web URL, e.g. "http://www.hughsie.com/"
 *
 * Sets a web address of a specific type.
 **/
void
gs_app_set_url (GsApp *app, AsUrlKind kind, const gchar *url)
{
	g_return_if_fail (GS_IS_APP (app));
	g_hash_table_insert (app->urls,
			     g_strdup (as_url_kind_to_string (kind)),
			     g_strdup (url));
}

/**
 * gs_app_get_license:
 * @app: a #GsApp
 *
 * Gets the project license of the application.
 *
 * Returns: a string, or %NULL for unset
 **/
const gchar *
gs_app_get_license (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->license;
}

/**
 * gs_app_get_license_is_free:
 * @app: a #GsApp
 *
 * Returns if the application is free software.
 *
 * Returns: %TRUE if the application is free software
 **/
gboolean
gs_app_get_license_is_free (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), FALSE);
	return app->license_is_free;
}

/**
 * gs_app_get_license_token_is_nonfree:
 */
static gboolean
gs_app_get_license_token_is_nonfree (const gchar *token)
{
	/* grammar */
	if (g_strcmp0 (token, "(") == 0)
		return FALSE;
	if (g_strcmp0 (token, ")") == 0)
		return FALSE;

	/* a token, but still nonfree */
	if (g_strcmp0 (token, "@LicenseRef-proprietary") == 0)
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
 **/
void
gs_app_set_license (GsApp *app, GsAppQuality quality, const gchar *license)
{
	guint i;
	g_auto(GStrv) tokens = NULL;

	g_return_if_fail (GS_IS_APP (app));

	/* only save this if the data is sufficiently high quality */
	if (quality <= app->license_quality)
		return;
	if (license == NULL)
		return;
	app->license_quality = quality;

	/* assume free software until we find a nonfree SPDX token */
	app->license_is_free = TRUE;
	tokens = as_utils_spdx_license_tokenize (license);
	for (i = 0; tokens[i] != NULL; i++) {
		if (g_strcmp0 (tokens[i], "&") == 0 ||
		    g_strcmp0 (tokens[i], "|") == 0)
			continue;
		if (gs_app_get_license_token_is_nonfree (tokens[i])) {
			g_debug ("nonfree license from %s: '%s'",
				 gs_app_get_id (app), tokens[i]);
			app->license_is_free = FALSE;
			break;
		}
	}

	g_free (app->license);
	app->license = g_strdup (license);
}

/**
 * gs_app_get_summary_missing:
 * @app: a #GsApp
 *
 * Gets the one-line summary to use when this application is missing.
 *
 * Returns: a string, or %NULL for unset
 **/
const gchar *
gs_app_get_summary_missing (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->summary_missing;
}

/**
 * gs_app_set_summary_missing:
 * @app: a #GsApp
 * @summary_missing: a string, or %NULL
 *
 * Sets the one-line summary to use when this application is missing.
 **/
void
gs_app_set_summary_missing (GsApp *app, const gchar *summary_missing)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->summary_missing);
	app->summary_missing = g_strdup (summary_missing);
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
 **/
gchar **
gs_app_get_menu_path (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->menu_path;
}

/**
 * gs_app_set_menu_path:
 * @app: a #GsApp
 * @menu_path: a %NULL-terminated array of strings
 *
 * Sets the new menu path. The menu path is an array of path elements.
 * This function creates a deep copy of the path.
 **/
void
gs_app_set_menu_path (GsApp *app, gchar **menu_path)
{
	g_return_if_fail (GS_IS_APP (app));
	g_strfreev (app->menu_path);
	app->menu_path = g_strdupv (menu_path);
}

/**
 * gs_app_get_origin:
 * @app: a #GsApp
 *
 * Gets the origin for the application, e.g. "fedora".
 *
 * Returns: a string, or %NULL for unset
 **/
const gchar *
gs_app_get_origin (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->origin;
}

/**
 * gs_app_set_origin:
 * @app: a #GsApp
 * @origin: a string, or %NULL
 *
 * The origin is the original source of the application e.g. "fedora-updates"
 **/
void
gs_app_set_origin (GsApp *app, const gchar *origin)
{
	g_return_if_fail (GS_IS_APP (app));
	if (origin == app->origin)
		return;
	g_free (app->origin);
	app->origin = g_strdup (origin);
}

/**
 * gs_app_get_origin_ui:
 * @app: a #GsApp
 *
 * Gets the UI-visible origin used to install the application, e.g. "Fedora".
 *
 * Returns: a string, or %NULL for unset
 **/
const gchar *
gs_app_get_origin_ui (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->origin_ui;
}

/**
 * gs_app_set_origin_ui:
 * @app: a #GsApp
 * @origin_ui: a string, or %NULL
 *
 * The origin is the original source of the application to show in the UI,
 * e.g. "Fedora"
 **/
void
gs_app_set_origin_ui (GsApp *app, const gchar *origin_ui)
{
	g_return_if_fail (GS_IS_APP (app));
	if (origin_ui == app->origin_ui)
		return;
	g_free (app->origin_ui);
	app->origin_ui = g_strdup (origin_ui);
}

/**
 * gs_app_add_screenshot:
 * @app: a #GsApp
 * @screenshot: a #AsScreenshot
 *
 * Adds a screenshot to the applicaton.
 **/
void
gs_app_add_screenshot (GsApp *app, AsScreenshot *screenshot)
{
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (AS_IS_SCREENSHOT (screenshot));
	g_ptr_array_add (app->screenshots, g_object_ref (screenshot));
}

/**
 * gs_app_get_screenshots:
 * @app: a #GsApp
 *
 * Gets the list of screenshots.
 *
 * Returns: (element-type AsScreenshot) (transfer none): a list
 **/
GPtrArray *
gs_app_get_screenshots (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->screenshots;
}

/**
 * gs_app_get_update_version:
 * @app: a #GsApp
 *
 * Gets the newest update version.
 *
 * Returns: a string, or %NULL for unset
 **/
const gchar *
gs_app_get_update_version (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->update_version;
}

/**
 * gs_app_get_update_version_ui:
 * @app: a #GsApp
 *
 * Gets the update version for the UI.
 *
 * Returns: a string, or %NULL for unset
 **/
const gchar *
gs_app_get_update_version_ui (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);

	/* work out the two version numbers */
	if (app->update_version != NULL &&
	    app->update_version_ui == NULL) {
		gs_app_ui_versions_populate (app);
	}

	return app->update_version_ui;
}

/**
 * gs_app_set_update_version_internal:
 */
static void
gs_app_set_update_version_internal (GsApp *app, const gchar *update_version)
{
	g_free (app->update_version);
	app->update_version = g_strdup (update_version);
	gs_app_ui_versions_invalidate (app);
}

/**
 * gs_app_set_update_version:
 * @app: a #GsApp
 * @update_version: a string, e.g. "0.1.2.3"
 *
 * Sets the new version number of the update.
 **/
void
gs_app_set_update_version (GsApp *app, const gchar *update_version)
{
	g_return_if_fail (GS_IS_APP (app));
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
 **/
const gchar *
gs_app_get_update_details (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->update_details;
}

/**
 * gs_app_set_update_details:
 * @app: a #GsApp
 * @update_details: a string
 *
 * Sets the multi-line description for the update.
 **/
void
gs_app_set_update_details (GsApp *app, const gchar *update_details)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->update_details);
	app->update_details = g_strdup (update_details);
}

/**
 * gs_app_get_update_urgency:
 * @app: a #GsApp
 *
 * Gets the update urgency.
 *
 * Returns: a #AsUrgencyKind, or %AS_URGENCY_KIND_UNKNOWN for unset
 **/
AsUrgencyKind
gs_app_get_update_urgency (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), AS_URGENCY_KIND_UNKNOWN);
	return app->update_urgency;
}

/**
 * gs_app_set_update_urgency:
 * @app: a #GsApp
 * @update_urgency: a #AsUrgencyKind
 *
 * Sets the update urgency.
 **/
void
gs_app_set_update_urgency (GsApp *app, AsUrgencyKind update_urgency)
{
	g_return_if_fail (GS_IS_APP (app));
	app->update_urgency = update_urgency;
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
 **/
const gchar *
gs_app_get_management_plugin (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->management_plugin;
}

/**
 * gs_app_set_management_plugin:
 * @app: a #GsApp
 * @management_plugin: a string, or %NULL, e.g. "fwupd"
 *
 * The management plugin is the plugin that can handle doing install and remove
 * operations on the #GsApp.
 * Typical values include "packagekit" and "jhbuild"
 **/
void
gs_app_set_management_plugin (GsApp *app, const gchar *management_plugin)
{
	g_return_if_fail (GS_IS_APP (app));

	if (g_strcmp0 (app->management_plugin, management_plugin) == 0)
		return;

	g_free (app->management_plugin);
	app->management_plugin = g_strdup (management_plugin);
}

/**
 * gs_app_get_rating:
 * @app: a #GsApp
 *
 * Gets the percentage rating of the application, where 100 is 5 stars.
 *
 * Returns: a percentage, or -1 for unset
 **/
gint
gs_app_get_rating (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), -1);
	return app->rating;
}

/**
 * gs_app_set_rating:
 * @app: a #GsApp
 * @rating: a percentage, or -1 for invalid
 *
 * Gets the percentage rating of the application.
 **/
void
gs_app_set_rating (GsApp *app, gint rating)
{
	g_return_if_fail (GS_IS_APP (app));
	app->rating = rating;
	gs_app_queue_notify (app, "rating");
}

/**
 * gs_app_get_review_ratings:
 * @app: a #GsApp
 *
 * Gets the review ratings.
 *
 * Returns: (element-type gint) (transfer none): a list
 **/
GArray *
gs_app_get_review_ratings (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->review_ratings;
}

/**
 * gs_app_set_review_ratings:
 * @app: a #GsApp
 * @review_ratings: (element-type gint): a list
 *
 * Sets the review ratings.
 **/
void
gs_app_set_review_ratings (GsApp *app, GArray *review_ratings)
{
	g_return_if_fail (GS_IS_APP (app));
	if (app->review_ratings != NULL)
		g_array_unref (app->review_ratings);
	app->review_ratings = g_array_ref (review_ratings);
}

/**
 * gs_app_get_reviews:
 * @app: a #GsApp
 *
 * Gets all the user-submitted reviews for the application.
 *
 * Returns: (element-type GsReview) (transfer none): the list of reviews
 **/
GPtrArray *
gs_app_get_reviews (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->reviews;
}

/**
 * gs_app_add_review:
 * @app: a #GsApp
 * @review: a #GsReview
 *
 * Adds a user-submitted review to the application.
 **/
void
gs_app_add_review (GsApp *app, GsReview *review)
{
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (GS_IS_REVIEW (review));
	g_ptr_array_add (app->reviews, g_object_ref (review));
}

/**
 * gs_app_remove_review:
 * @app: a #GsApp
 * @review: a #GsReview
 *
 * Removes a user-submitted review to the application.
 **/
void
gs_app_remove_review (GsApp *app, GsReview *review)
{
	g_return_if_fail (GS_IS_APP (app));
	g_ptr_array_remove (app->reviews, review);
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
 **/
guint64
gs_app_get_size_download (GsApp *app)
{
	guint64 sz;

	g_return_val_if_fail (GS_IS_APP (app), G_MAXUINT64);

	/* this app */
	sz = app->size_download;

	/* add the runtime if this is not installed */
	if (app->runtime != NULL) {
		if (gs_app_get_state (app->runtime) == AS_APP_STATE_AVAILABLE)
			sz += gs_app_get_size_installed (app->runtime);
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
 **/
void
gs_app_set_size_download (GsApp *app, guint64 size_download)
{
	g_return_if_fail (GS_IS_APP (app));
	app->size_download = size_download;
}

/**
 * gs_app_get_size_installed:
 * @app: a #GsApp
 *
 * Gets the size on disk, either for an existing application of one that could
 * be installed.
 *
 * If there is a runtime not yet installed then this is also added.
 *
 * Returns: size in bytes, 0 for unknown, or %GS_APP_SIZE_UNKNOWABLE for invalid.
 **/
guint64
gs_app_get_size_installed (GsApp *app)
{
	guint64 sz;

	g_return_val_if_fail (GS_IS_APP (app), G_MAXUINT64);

	/* this app */
	sz = app->size_installed;

	/* add the runtime if this is not installed */
	if (app->runtime != NULL) {
		if (gs_app_get_state (app->runtime) == AS_APP_STATE_AVAILABLE)
			sz += gs_app_get_size_installed (app->runtime);
	}

	return sz;
}

/**
 * gs_app_set_size_installed:
 * @app: a #GsApp
 * @size_installed: size in bytes, or %GS_APP_SIZE_UNKNOWABLE for invalid
 *
 * Sets the installed size of the application.
 **/
void
gs_app_set_size_installed (GsApp *app, guint64 size_installed)
{
	g_return_if_fail (GS_IS_APP (app));
	app->size_installed = size_installed;
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
 **/
const gchar *
gs_app_get_metadata_item (GsApp *app, const gchar *key)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	return g_hash_table_lookup (app->metadata, key);
}

/**
 * gs_app_set_metadata:
 * @app: a #GsApp
 * @key: a string, e.g. "fwupd::device-id"
 * @value: a string, e.g. "fubar"
 *
 * Sets some metadata for the application.
 * Is is expected that plugins namespace any plugin-specific metadata,
 * for example `fwupd::device-id`.
 **/
void
gs_app_set_metadata (GsApp *app, const gchar *key, const gchar *value)
{
	const gchar *found;
	GString *str;

	g_return_if_fail (GS_IS_APP (app));

	/* if no value, then remove the key */
	if (value == NULL) {
		g_hash_table_remove (app->metadata, key);
		return;
	}

	/* check we're not overwriting */
	found = g_hash_table_lookup (app->metadata, key);
	if (found != NULL) {
		if (g_strcmp0 (found, value) == 0)
			return;
		g_warning ("tried overwriting key %s from %s to %s",
			   key, found, value);
		return;
	}
	str = g_string_new (value);
	as_utils_string_replace (str, "@datadir@", DATADIR);
	g_hash_table_insert (app->metadata,
			     g_strdup (key),
			     g_string_free (str, FALSE));
}

/**
 * gs_app_get_addons:
 * @app: a #GsApp
 *
 * Gets the list of addons for the application.
 *
 * Returns: (element-type GsApp) (transfer none): a list of addons
 **/
GPtrArray *
gs_app_get_addons (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->addons;
}

/**
 * gs_app_add_addon:
 * @app: a #GsApp
 * @addon: a #GsApp
 *
 * Adds an addon to the list of application addons.
 **/
void
gs_app_add_addon (GsApp *app, GsApp *addon)
{
	gpointer found;
	const gchar *id;

	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (GS_IS_APP (addon));

	id = gs_app_get_id (addon);
	found = g_hash_table_lookup (app->addons_hash, id);
	if (found != NULL)
		return;
	g_hash_table_insert (app->addons_hash, g_strdup (id), GINT_TO_POINTER (1));

	g_ptr_array_add (app->addons, g_object_ref (addon));
}

/**
 * gs_app_get_related:
 * @app: a #GsApp
 *
 * Gets any related applications.
 *
 * Returns: (element-type GsApp) (transfer none): a list of applications
 **/
GPtrArray *
gs_app_get_related (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->related;
}

/**
 * gs_app_add_related:
 * @app: a #GsApp
 * @app2: a #GsApp
 *
 * Adds a related application.
 **/
void
gs_app_add_related (GsApp *app, GsApp *app2)
{
	gchar *key;
	gpointer found;

	g_return_if_fail (GS_IS_APP (app));

	/* if the app is updatable-live and any related app is not then
	 * degrade to the offline state */
	if (app->state == AS_APP_STATE_UPDATABLE_LIVE &&
	    app2->state == AS_APP_STATE_UPDATABLE) {
		app->state = app2->state;
	}

	key = g_strdup_printf ("%s-%s",
			       gs_app_get_id (app2),
			       gs_app_get_source_default (app2));
	found = g_hash_table_lookup (app->related_hash, key);
	if (found != NULL) {
		g_debug ("Already added %s as a related item", key);
		g_free (key);
		return;
	}
	g_hash_table_insert (app->related_hash, key, GINT_TO_POINTER (1));
	g_ptr_array_add (app->related, g_object_ref (app2));
}

/**
 * gs_app_get_history:
 * @app: a #GsApp
 *
 * Gets the history of this application.
 *
 * Returns: (element-type GsApp) (transfer none): a list
 **/
GPtrArray *
gs_app_get_history (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->history;
}

/**
 * gs_app_add_history:
 * @app: a #GsApp
 * @app2: a #GsApp
 *
 * Adds a history item for this package.
 **/
void
gs_app_add_history (GsApp *app, GsApp *app2)
{
	g_return_if_fail (GS_IS_APP (app));
	g_ptr_array_add (app->history, g_object_ref (app2));
}

/**
 * gs_app_get_install_date:
 * @app: a #GsApp
 *
 * Gets the date that an application was installed.
 *
 * Returns: A UNIX epoch, or 0 for unset
 **/
guint64
gs_app_get_install_date (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return app->install_date;
}

/**
 * gs_app_set_install_date:
 * @app: a #GsApp
 * @install_date: an epoch, or %GS_APP_INSTALL_DATE_UNKNOWN
 *
 * Sets the date that an application was installed.
 **/
void
gs_app_set_install_date (GsApp *app, guint64 install_date)
{
	g_return_if_fail (GS_IS_APP (app));
	app->install_date = install_date;
}

/**
 * gs_app_get_categories:
 * @app: a #GsApp
 *
 * Gets the list of categories for an application.
 *
 * Returns: (element-type utf8) (transfer none): a list
 **/
GPtrArray *
gs_app_get_categories (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->categories;
}

/**
 * gs_app_has_category:
 * @app: a #GsApp
 * @category: a category ID, e.g. "AudioVideo"
 *
 * Checks if the application is in a specific category.
 *
 * Returns: %TRUE for success
 **/
gboolean
gs_app_has_category (GsApp *app, const gchar *category)
{
	const gchar *tmp;
	guint i;

	g_return_val_if_fail (GS_IS_APP (app), FALSE);

	/* find the category */
	for (i = 0; i < app->categories->len; i++) {
		tmp = g_ptr_array_index (app->categories, i);
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
 **/
void
gs_app_set_categories (GsApp *app, GPtrArray *categories)
{
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (categories != NULL);
	if (app->categories != NULL)
		g_ptr_array_unref (app->categories);
	app->categories = g_ptr_array_ref (categories);
}

/**
 * gs_app_add_category:
 * @app: a #GsApp
 * @category: a category ID, e.g. "AudioVideo"
 *
 * Adds a category ID to an application.
 **/
void
gs_app_add_category (GsApp *app, const gchar *category)
{
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (category != NULL);
	if (gs_app_has_category (app, category))
		return;
	g_ptr_array_add (app->categories, g_strdup (category));
}

/**
 * gs_app_get_key_colors:
 * @app: a #GsApp
 *
 * Gets the key colors used in the application icon.
 *
 * Returns: (element-type GdkRGBA) (transfer none): a list
 **/
GPtrArray *
gs_app_get_key_colors (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->key_colors;
}

/**
 * gs_app_set_key_colors:
 * @app: a #GsApp
 * @key_colors: (element-type GdkRGBA): a set of key colors
 *
 * Sets the key colors used in the application icon.
 **/
void
gs_app_set_key_colors (GsApp *app, GPtrArray *key_colors)
{
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (key_colors != NULL);
	if (app->key_colors != NULL)
		g_ptr_array_unref (app->key_colors);
	app->key_colors = g_ptr_array_ref (key_colors);
}

/**
 * gs_app_add_key_color:
 * @app: a #GsApp
 * @key_color: a #GdkRGBA
 *
 * Adds a key colors used in the application icon.
 **/
void
gs_app_add_key_color (GsApp *app, GdkRGBA *key_color)
{
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (key_color != NULL);
	g_ptr_array_add (app->key_colors, gdk_rgba_copy (key_color));
}

/**
 * gs_app_get_keywords:
 * @app: a #GsApp
 *
 * Gets the list of application keywords in the users locale.
 *
 * Returns: (element-type utf8) (transfer none): a list
 **/
GPtrArray *
gs_app_get_keywords (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->keywords;
}

/**
 * gs_app_set_keywords:
 * @app: a #GsApp
 * @keywords: (element-type utf8): a set of keywords
 *
 * Sets the list of application keywords in the users locale.
 **/
void
gs_app_set_keywords (GsApp *app, GPtrArray *keywords)
{
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (keywords != NULL);
	if (app->keywords != NULL)
		g_ptr_array_unref (app->keywords);
	app->keywords = g_ptr_array_ref (keywords);
}

/**
 * gs_app_add_kudo:
 * @app: a #GsApp
 * @kudo: a #GsAppKudo, e.g. %GS_APP_KUDO_MY_LANGUAGE
 *
 * Adds a kudo to the application.
 **/
void
gs_app_add_kudo (GsApp *app, GsAppKudo kudo)
{
	g_return_if_fail (GS_IS_APP (app));
	app->kudos |= kudo;
}

/**
 * gs_app_get_kudos:
 * @app: a #GsApp
 *
 * Gets all the kudos the application has been awarded.
 *
 * Returns: the kudos, as a bitfield
 **/
guint64
gs_app_get_kudos (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return app->kudos;
}

/**
 * gs_app_get_kudos_percentage:
 * @app: a #GsApp
 *
 * Gets the kudos, as a percentage value.
 *
 * Returns: a percentage, with 0 for no kudos and a maximum of 100.
 **/
guint
gs_app_get_kudos_percentage (GsApp *app)
{
	guint percentage = 0;

	g_return_val_if_fail (GS_IS_APP (app), 0);

	if ((app->kudos & GS_APP_KUDO_MY_LANGUAGE) > 0)
		percentage += 20;
	if ((app->kudos & GS_APP_KUDO_RECENT_RELEASE) > 0)
		percentage += 20;
	if ((app->kudos & GS_APP_KUDO_FEATURED_RECOMMENDED) > 0)
		percentage += 20;
	if ((app->kudos & GS_APP_KUDO_MODERN_TOOLKIT) > 0)
		percentage += 20;
	if ((app->kudos & GS_APP_KUDO_SEARCH_PROVIDER) > 0)
		percentage += 10;
	if ((app->kudos & GS_APP_KUDO_INSTALLS_USER_DOCS) > 0)
		percentage += 10;
	if ((app->kudos & GS_APP_KUDO_USES_NOTIFICATIONS) > 0)
		percentage += 20;
	if ((app->kudos & GS_APP_KUDO_HAS_KEYWORDS) > 0)
		percentage += 5;
	if ((app->kudos & GS_APP_KUDO_USES_APP_MENU) > 0)
		percentage += 10;
	if ((app->kudos & GS_APP_KUDO_HAS_SCREENSHOTS) > 0)
		percentage += 20;
	if ((app->kudos & GS_APP_KUDO_PERFECT_SCREENSHOTS) > 0)
		percentage += 20;
	if ((app->kudos & GS_APP_KUDO_HIGH_CONTRAST) > 0)
		percentage += 20;
	if ((app->kudos & GS_APP_KUDO_HI_DPI_ICON) > 0)
		percentage += 20;

	/* popular apps should be at *least* 50% */
	if ((app->kudos & GS_APP_KUDO_POPULAR) > 0)
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
 **/
gboolean
gs_app_get_to_be_installed (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), FALSE);

	return app->to_be_installed;
}

/**
 * gs_app_set_to_be_installed:
 * @app: a #GsApp
 * @to_be_installed: if the app is due to be installed
 *
 * Sets if the application is queued for installation.
 **/
void
gs_app_set_to_be_installed (GsApp *app, gboolean to_be_installed)
{
	g_return_if_fail (GS_IS_APP (app));

	app->to_be_installed = to_be_installed;
}

/**
 * gs_app_has_quirk:
 * @app: a #GsApp
 * @quirk: a #AsAppQuirk, e.g. %AS_APP_QUIRK_COMPULSORY
 *
 * Finds out if an application has a specific quirk.
 *
 * Returns: %TRUE for success
 **/
gboolean
gs_app_has_quirk (GsApp *app, AsAppQuirk quirk)
{
	g_return_val_if_fail (GS_IS_APP (app), FALSE);

	return (app->quirk & quirk) > 0;
}

/**
 * gs_app_add_quirk:
 * @app: a #GsApp
 * @quirk: a #AsAppQuirk, e.g. %AS_APP_QUIRK_COMPULSORY
 *
 * Adds a quirk to an application.
 **/
void
gs_app_add_quirk (GsApp *app, AsAppQuirk quirk)
{
	g_return_if_fail (GS_IS_APP (app));

	app->quirk |= quirk;
	gs_app_queue_notify (app, "quirk");
}

/**
 * gs_app_remove_quirk:
 * @app: a #GsApp
 * @quirk: a #AsAppQuirk, e.g. %AS_APP_QUIRK_COMPULSORY
 *
 * Removes a quirk from an application.
 **/
void
gs_app_remove_quirk (GsApp *app, AsAppQuirk quirk)
{
	g_return_if_fail (GS_IS_APP (app));

	app->quirk &= ~quirk;
	gs_app_queue_notify (app, "quirk");
}

/**
 * gs_app_set_match_value:
 * @app: a #GsApp
 * @match_value: a value
 *
 * Set a match quality value, where higher values correspond to a
 * "better" search match, and should be shown above lower results.
 **/
void
gs_app_set_match_value (GsApp *app, guint match_value)
{
	g_return_if_fail (GS_IS_APP (app));
	app->match_value = match_value;
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
 **/
guint
gs_app_get_match_value (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return app->match_value;
}

/**
 * gs_app_set_priority:
 * @app: a #GsApp
 * @priority: a value
 *
 * Set a priority value.
 **/
void
gs_app_set_priority (GsApp *app, guint priority)
{
	g_return_if_fail (GS_IS_APP (app));
	app->priority = priority;
}

/**
 * gs_app_get_priority:
 * @app: a #GsApp
 *
 * Get a priority value, where higher values will be chosen where
 * multiple #GsApp's match a specific rule.
 *
 * Returns: a value, where higher is better
 **/
guint
gs_app_get_priority (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return app->priority;
}

/**
 * gs_app_get_last_error:
 * @app: a #GsApp
 *
 * Get the last error.
 *
 * Returns: a #GError, or %NULL
 **/
GError *
gs_app_get_last_error (GsApp *app)
{
	return app->last_error;
}

/**
 * gs_app_set_last_error:
 * @app: a #GsApp
 * @error: a #GError
 *
 * Sets the last error.
 **/
void
gs_app_set_last_error (GsApp *app, GError *error)
{
	g_clear_error (&app->last_error);
	app->last_error = g_error_copy (error);
}

/**
 * gs_app_get_property:
 */
static void
gs_app_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsApp *app = GS_APP (object);

	switch (prop_id) {
	case PROP_ID:
		g_value_set_string (value, app->id);
		break;
	case PROP_NAME:
		g_value_set_string (value, app->name);
		break;
	case PROP_VERSION:
		g_value_set_string (value, app->version);
		break;
	case PROP_SUMMARY:
		g_value_set_string (value, app->summary);
		break;
	case PROP_DESCRIPTION:
		g_value_set_string (value, app->description);
		break;
	case PROP_RATING:
		g_value_set_int (value, app->rating);
		break;
	case PROP_KIND:
		g_value_set_uint (value, app->kind);
		break;
	case PROP_STATE:
		g_value_set_uint (value, app->state);
		break;
	case PROP_PROGRESS:
		g_value_set_uint (value, app->progress);
		break;
	case PROP_INSTALL_DATE:
		g_value_set_uint64 (value, app->install_date);
		break;
	case PROP_QUIRK:
		g_value_set_uint64 (value, app->quirk);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * gs_app_set_property:
 */
static void
gs_app_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsApp *app = GS_APP (object);

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
		app->progress = g_value_get_uint (value);
		break;
	case PROP_INSTALL_DATE:
		gs_app_set_install_date (app, g_value_get_uint64 (value));
		break;
	case PROP_QUIRK:
		app->quirk = g_value_get_uint64 (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * gs_app_dispose:
 * @object: The object to dispose
 **/
static void
gs_app_dispose (GObject *object)
{
	GsApp *app = GS_APP (object);

	g_clear_object (&app->runtime);

	g_clear_pointer (&app->addons, g_ptr_array_unref);
	g_clear_pointer (&app->history, g_ptr_array_unref);
	g_clear_pointer (&app->related, g_ptr_array_unref);
	g_clear_pointer (&app->screenshots, g_ptr_array_unref);
	g_clear_pointer (&app->reviews, g_ptr_array_unref);
	g_clear_pointer (&app->icons, g_ptr_array_unref);

	G_OBJECT_CLASS (gs_app_parent_class)->dispose (object);
}

/**
 * gs_app_finalize:
 * @object: The object to finalize
 **/
static void
gs_app_finalize (GObject *object)
{
	GsApp *app = GS_APP (object);

	g_free (app->id);
	g_free (app->name);
	g_hash_table_unref (app->urls);
	g_free (app->license);
	g_strfreev (app->menu_path);
	g_free (app->origin);
	g_free (app->origin_ui);
	g_ptr_array_unref (app->sources);
	g_ptr_array_unref (app->source_ids);
	g_free (app->project_group);
	g_free (app->version);
	g_free (app->version_ui);
	g_free (app->summary);
	g_free (app->summary_missing);
	g_free (app->description);
	g_free (app->update_version);
	g_free (app->update_version_ui);
	g_free (app->update_details);
	g_free (app->management_plugin);
	g_hash_table_unref (app->metadata);
	g_hash_table_unref (app->addons_hash);
	g_hash_table_unref (app->related_hash);
	g_ptr_array_unref (app->categories);
	g_ptr_array_unref (app->key_colors);
	if (app->keywords != NULL)
		g_ptr_array_unref (app->keywords);
	if (app->last_error != NULL)
		g_error_free (app->last_error);
	if (app->local_file != NULL)
		g_object_unref (app->local_file);
	if (app->pixbuf != NULL)
		g_object_unref (app->pixbuf);

	G_OBJECT_CLASS (gs_app_parent_class)->finalize (object);
}

/**
 * gs_app_class_init:
 * @klass: The GsAppClass
 **/
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
}

/**
 * gs_app_init:
 **/
static void
gs_app_init (GsApp *app)
{
	app->rating = -1;
	app->sources = g_ptr_array_new_with_free_func (g_free);
	app->source_ids = g_ptr_array_new_with_free_func (g_free);
	app->categories = g_ptr_array_new_with_free_func (g_free);
	app->key_colors = g_ptr_array_new_with_free_func ((GDestroyNotify) gdk_rgba_free);
	app->addons = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	app->related = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	app->history = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	app->screenshots = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	app->reviews = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	app->icons = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	app->metadata = g_hash_table_new_full (g_str_hash,
	                                        g_str_equal,
	                                        g_free,
	                                        g_free);
	app->addons_hash = g_hash_table_new_full (g_str_hash,
	                                           g_str_equal,
	                                           g_free,
	                                           NULL);
	app->related_hash = g_hash_table_new_full (g_str_hash,
	                                            g_str_equal,
	                                            g_free,
	                                            NULL);
	app->urls = g_hash_table_new_full (g_str_hash,
	                                    g_str_equal,
	                                    g_free,
	                                    g_free);
}

/**
 * gs_app_new:
 * @id: an application ID, or %NULL, e.g. "flatpak:org.gnome.Software.desktop"
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

/* vim: set noexpandtab: */
