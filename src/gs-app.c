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
 * @short_description: An application that is either installed or that can be installed
 *
 * This object represents a 1:1 mapping to a .desktop file. The design is such
 * so you can't have different GsApp's for different versions or architectures
 * of a package. This rule really only applies to GsApps of kind AS_APP_KIND_DESKTOP
 * and AS_APP_KIND_GENERIC. We allow GsApps of kind AS_APP_KIND_OS_UPDATE or
 * AS_APP_KIND_GENERIC, which don't correspond to desktop files, but instead
 * represent a system update and its individual components.
 *
 * The #GsPluginLoader de-duplicates the GsApp instances that are produced by
 * plugins to ensure that there is a single instance of GsApp for each id, making
 * the id the primary key for this object. This ensures that actions triggered on
 * a GsApp in different parts of gnome-software can be observed by connecting to
 * signals on the GsApp.
 *
 * Information about other #GsApp objects can be stored in this object, for
 * instance in the gs_app_add_related() method or gs_app_get_history().
 */

#include "config.h"

#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "gs-app.h"
#include "gs-utils.h"

struct _GsApp
{
	GObject			 parent_instance;

	gchar			*id;
	gchar			*name;
	GsAppQuality		 name_quality;
	AsIcon			*icon;
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
	gint			 rating;
	GArray			*review_ratings;
	GPtrArray		*reviews; /* of GsReview */
	guint64			 size;
	AsAppKind		 kind;
	AsAppState		 state;
	AsAppState		 state_recover;
	guint			 progress;
	GHashTable		*metadata;
	GdkPixbuf		*pixbuf;
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
 * gs_app_error_quark:
 * Return value: Our personal error quark.
 **/
G_DEFINE_QUARK (gs-app-error-quark, gs_app_error)

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
 * gs_app_to_string:
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

	str = g_string_new ("GsApp:\n");
	gs_app_kv_lpad (str, "kind", as_app_kind_to_string (app->kind));
	if (app->last_error != NULL)
		gs_app_kv_lpad (str, "last-error", app->last_error->message);
	gs_app_kv_lpad (str, "compulsory",
			gs_app_has_quirk (app, AS_APP_QUIRK_COMPULSORY)
			? "True" : "False");
	gs_app_kv_lpad (str, "state", as_app_state_to_string (app->state));
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
	if ((app->kudos & GS_APP_KUDO_IBUS_HAS_SYMBOL) > 0)
		gs_app_kv_lpad (str, "kudo", "ibus-has-symbol");
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
	if (app->icon != NULL) {
		gs_app_kv_lpad (str, "icon-kind",
				as_icon_kind_to_string (as_icon_get_kind (app->icon)));
		if (as_icon_get_name (app->icon) != NULL)
			gs_app_kv_lpad (str, "icon-name",
					as_icon_get_name (app->icon));
		if (as_icon_get_prefix (app->icon) != NULL)
			gs_app_kv_lpad (str, "icon-prefix",
					as_icon_get_prefix (app->icon));
		if (as_icon_get_filename (app->icon) != NULL)
			gs_app_kv_lpad (str, "icon-filename",
					as_icon_get_filename (app->icon));
	}
	if (app->match_value != 0)
		gs_app_kv_printf (str, "match-value", "%05x", app->match_value);
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
	tmp = g_hash_table_lookup (app->urls, as_url_kind_to_string (AS_URL_KIND_HOMEPAGE));
	if (tmp != NULL)
		gs_app_kv_lpad (str, "url{homepage}", tmp);
	if (app->license != NULL)
		gs_app_kv_lpad (str, "license", app->license);
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
	if (app->pixbuf != NULL)
		gs_app_kv_printf (str, "pixbuf", "%p", app->pixbuf);
	if (app->install_date != 0) {
		gs_app_kv_printf (str, "install-date", "%"
				  G_GUINT64_FORMAT "",
				  app->install_date);
	}
	if (app->size != 0) {
		gs_app_kv_printf (str, "size", "%" G_GUINT64_FORMAT "k",
				  app->size / 1024);
	}
	if (app->related->len > 0)
		gs_app_kv_printf (str, "related", "%i", app->related->len);
	if (app->history->len > 0)
		gs_app_kv_printf (str, "history", "%i", app->history->len);
	for (i = 0; i < app->categories->len; i++) {
		tmp = g_ptr_array_index (app->categories, i);
		gs_app_kv_lpad (str, "category", tmp);
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
 **/
const gchar *
gs_app_get_id (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->id;
}

/**
 * gs_app_get_id_no_prefix:
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
 */
AsAppState
gs_app_get_state (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), AS_APP_STATE_UNKNOWN);
	return app->state;
}

/**
 * gs_app_get_progress:
 */
guint
gs_app_get_progress (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return app->progress;
}

/**
 * gs_app_set_state_recover:
 *
 * Sets the application state to the last status value that was not
 * transient.
 */
void
gs_app_set_state_recover (GsApp *app)
{
	if (app->state_recover == AS_APP_STATE_UNKNOWN)
		return;
	if (app->state_recover == app->state)
		return;
	app->state = app->state_recover;
	app->state_recover = AS_APP_STATE_UNKNOWN;
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
		g_debug ("non-transient state now %s",
			 as_app_state_to_string (state));
		app->state_recover = state;

		/* clear the error as the application has changed state */
		g_clear_error (&app->last_error);
		break;
	}

	return TRUE;
}

/**
 * gs_app_set_progress:
 *
 * This sets the progress completion of the application.
 */
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
 *
 * This sets the state of the application. The following state diagram explains
 * the typical states. All applications start in state %AS_APP_STATE_UNKNOWN,
 * but the frontend is not supposed to see GsApps with this state, ever.
 * Backend plugins are reponsible for changing the state to one of the other
 * states before the GsApp is passed to the frontend. This is enforced by the
 * #GsPluginLoader.
 *
 * UPDATABLE --> INSTALLING --> INSTALLED
 * UPDATABLE --> REMOVING   --> AVAILABLE
 * INSTALLED --> REMOVING   --> AVAILABLE
 * AVAILABLE --> INSTALLING --> INSTALLED
 * AVAILABLE <--> QUEUED --> INSTALLING --> INSTALLED
 * UNKNOWN   --> UNAVAILABLE
 */
void
gs_app_set_state (GsApp *app, AsAppState state)
{
	g_return_if_fail (GS_IS_APP (app));

	if (gs_app_set_state_internal (app, state))
		gs_app_queue_notify (app, "state");
}

/**
 * gs_app_get_kind:
 */
AsAppKind
gs_app_get_kind (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), AS_APP_KIND_UNKNOWN);
	return app->kind;
}

/**
 * gs_app_set_kind:
 *
 * This sets the kind of the application. The following state diagram explains
 * the typical states. All applications start with kind %AS_APP_KIND_UNKNOWN.
 *
 * PACKAGE --> NORMAL
 * PACKAGE --> SYSTEM
 * NORMAL  --> SYSTEM
 */
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
 */
const gchar *
gs_app_get_name (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->name;
}

/**
 * gs_app_set_name:
 * @app:	A #GsApp instance
 * @quality:	A data quality, e.g. %GS_APP_QUALITY_LOWEST
 * @name:	The short localized name, e.g. "Calculator"
 */
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
 */
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
 */
void
gs_app_add_source (GsApp *app, const gchar *source)
{
	const gchar *tmp;
	guint i;

	g_return_if_fail (GS_IS_APP (app));

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
 */
GPtrArray *
gs_app_get_sources (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->sources;
}

/**
 * gs_app_set_sources:
 * @app:	A #GsApp instance
 * @source:	The non-localized short names, e.g. ["gnome-calculator"]
 *
 * This name is used for the update page if the application is collected into
 * the 'OS Updates' group. It is typically the package names, although this
 * should not be relied upon.
 */
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
 */
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
 */
GPtrArray *
gs_app_get_source_ids (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->source_ids;
}

/**
 * gs_app_clear_source_ids:
 */
void
gs_app_clear_source_ids (GsApp *app)
{
	g_return_if_fail (GS_IS_APP (app));
	g_ptr_array_set_size (app->source_ids, 0);
}

/**
 * gs_app_set_source_ids:
 * @app:	A #GsApp instance
 * @source_id:	The source-id, e.g. ["gnome-calculator;0.134;fedora"]
 * 		or ["/home/hughsie/.local/share/applications/0ad.desktop"]
 *
 * This ID is used internally to the controlling plugin.
 */
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
 */
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
 */
const gchar *
gs_app_get_project_group (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->project_group;
}

/**
 * gs_app_set_project_group:
 * @app:	A #GsApp instance
 * @project_group:	The non-localized project group, e.g. "GNOME" or "KDE"
 */
void
gs_app_set_project_group (GsApp *app, const gchar *project_group)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->project_group);
	app->project_group = g_strdup (project_group);
}

/**
 * gs_app_is_addon_id_kind
 **/
static gboolean
gs_app_is_addon_id_kind (GsApp *app)
{
	AsAppKind kind;
	kind = gs_app_get_kind (app);
	if (kind == AS_APP_KIND_DESKTOP)
		return FALSE;
	if (kind == AS_APP_KIND_WEB_APP)
		return FALSE;
	return TRUE;
}

static GtkIconTheme	*icon_theme_singleton;
static GMutex		 icon_theme_lock;
static GHashTable	*icon_theme_paths;

/**
 * icon_theme_get:
 */
static GtkIconTheme *
icon_theme_get (void)
{
	if (icon_theme_singleton == NULL)
		icon_theme_singleton = gtk_icon_theme_new ();

	return icon_theme_singleton;
}

/**
 * icon_theme_add_path:
 */
static void
icon_theme_add_path (const gchar *path)
{
	if (path == NULL)
		return;

	if (icon_theme_paths == NULL)
		icon_theme_paths = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	if (!g_hash_table_contains (icon_theme_paths, path)) {
		gtk_icon_theme_prepend_search_path (icon_theme_get (), path);
		g_hash_table_add (icon_theme_paths, g_strdup (path));
	}
}

/**
 * gs_app_get_pixbuf:
 */
GdkPixbuf *
gs_app_get_pixbuf (GsApp *app)
{
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&icon_theme_lock);

	g_return_val_if_fail (GS_IS_APP (app), NULL);

	/* has an icon */
	if (app->pixbuf == NULL &&
	    app->icon != NULL &&
	    as_icon_get_kind (app->icon) == AS_ICON_KIND_STOCK) {
		app->pixbuf = gtk_icon_theme_load_icon (icon_theme_get (),
							      as_icon_get_name (app->icon), 64,
							      GTK_ICON_LOOKUP_USE_BUILTIN |
							      GTK_ICON_LOOKUP_FORCE_SIZE,
							      NULL);

	} else if (app->pixbuf == NULL && gs_app_get_state (app) == AS_APP_STATE_AVAILABLE_LOCAL) {
		const gchar *icon_name;
		if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE)
			icon_name = "x-package-repository";
		else if (gs_app_is_addon_id_kind (app))
			icon_name = "application-x-addon";
		else
			icon_name = "application-x-executable";
		app->pixbuf = gtk_icon_theme_load_icon (icon_theme_get (),
		                                              icon_name, 96,
		                                              GTK_ICON_LOOKUP_USE_BUILTIN |
		                                              GTK_ICON_LOOKUP_FORCE_SIZE,
		                                              NULL);

	} else if (app->pixbuf == NULL && gs_app_get_kind (app) == AS_APP_KIND_GENERIC) {
		app->pixbuf = gtk_icon_theme_load_icon (icon_theme_get (),
		                                              "application-x-addon-symbolic", 64,
		                                              GTK_ICON_LOOKUP_USE_BUILTIN |
		                                              GTK_ICON_LOOKUP_FORCE_SIZE,
		                                              NULL);
	} else if (app->pixbuf == NULL && gs_app_get_kind (app) == AS_APP_KIND_OS_UPDATE) {
		app->pixbuf = gtk_icon_theme_load_icon (icon_theme_get (),
		                                              "software-update-available-symbolic", 64,
		                                              GTK_ICON_LOOKUP_USE_BUILTIN |
		                                              GTK_ICON_LOOKUP_FORCE_SIZE,
		                                              NULL);
	} else if (app->pixbuf == NULL &&
		   gs_app_get_state (app) == AS_APP_STATE_UNAVAILABLE) {
		app->pixbuf = gtk_icon_theme_load_icon (icon_theme_get (),
		                                              "dialog-question-symbolic", 16,
		                                              GTK_ICON_LOOKUP_USE_BUILTIN |
		                                              GTK_ICON_LOOKUP_FORCE_SIZE,
		                                              NULL);
	}

	return app->pixbuf;
}

/**
 * gs_app_get_icon:
 */
AsIcon *
gs_app_get_icon (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->icon;
}

/**
 * gs_app_set_icon:
 */
void
gs_app_set_icon (GsApp *app, AsIcon *icon)
{
	g_return_if_fail (GS_IS_APP (app));
	g_set_object (&app->icon, icon);
}

/**
 * gs_app_get_runtime:
 */
GsApp *
gs_app_get_runtime (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->runtime;
}

/**
 * gs_app_set_runtime:
 */
void
gs_app_set_runtime (GsApp *app, GsApp *runtime)
{
	g_return_if_fail (GS_IS_APP (app));
	g_set_object (&app->runtime, runtime);
}

/**
 * gs_app_load_icon:
 */
gboolean
gs_app_load_icon (GsApp *app, gint scale, GError **error)
{
	AsIcon *icon;
	g_autoptr(GdkPixbuf) pixbuf = NULL;

	g_return_val_if_fail (GS_IS_APP (app), FALSE);
	g_return_val_if_fail (app->icon != NULL, FALSE);

	/* either load from the theme or from a file */
	icon = gs_app_get_icon (app);
	switch (as_icon_get_kind (icon)) {
	case AS_ICON_KIND_LOCAL:
		if (as_icon_get_filename (icon) == NULL) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "%s icon has no filename",
				     as_icon_get_name (icon));
			return FALSE;
		}
		pixbuf = gdk_pixbuf_new_from_file_at_size (as_icon_get_filename (icon),
							   64 * scale,
							   64 * scale,
							   error);
		break;
	case AS_ICON_KIND_STOCK:
	{
		g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&icon_theme_lock);

		icon_theme_add_path (as_icon_get_prefix (icon));
		pixbuf = gtk_icon_theme_load_icon (icon_theme_get (),
						   as_icon_get_name (icon),
						   64 * scale,
						   GTK_ICON_LOOKUP_USE_BUILTIN |
						   GTK_ICON_LOOKUP_FORCE_SIZE,
						   error);
		break;
	}
	default:
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "%s icon cannot be loaded",
			     as_icon_kind_to_string (as_icon_get_kind (icon)));
		break;
	}
	if (pixbuf == NULL)
		return FALSE;
	gs_app_set_pixbuf (app, pixbuf);
	return TRUE;
}

/**
 * gs_app_set_pixbuf:
 */
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
 */
const gchar *
gs_app_get_version (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->version;
}

/**
 * gs_app_get_version_ui:
 */
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
 * @app:	A #GsApp instance
 * @version:	The version, e.g. "2:1.2.3.fc19"
 *
 * This saves the version after stripping out any non-friendly parts, such as
 * distro tags, git revisions and that kind of thing.
 */
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
 */
const gchar *
gs_app_get_summary (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->summary;
}

/**
 * gs_app_set_summary:
 * @app:	A #GsApp instance
 * @quality:	A data quality, e.g. %GS_APP_QUALITY_LOWEST
 * @summary:	The medium length localized name, e.g. "A graphical calculator for GNOME"
 */
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
 */
const gchar *
gs_app_get_description (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->description;
}

/**
 * gs_app_set_description:
 * @app:	A #GsApp instance
 * @quality:	A data quality, e.g. %GS_APP_QUALITY_LOWEST
 * @summary:	The multiline localized description, e.g. "GNOME Calculator is a graphical calculator for GNOME....."
 */
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
 */
const gchar *
gs_app_get_url (GsApp *app, AsUrlKind kind)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return g_hash_table_lookup (app->urls, as_url_kind_to_string (kind));
}

/**
 * gs_app_set_url:
 */
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
 */
const gchar *
gs_app_get_license (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->license;
}

/**
 * gs_app_get_license_is_free:
 */
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
 */
void
gs_app_set_license (GsApp *app, GsAppQuality quality, const gchar *license)
{
	GString *urld;
	guint i;
	g_auto(GStrv) tokens = NULL;

	g_return_if_fail (GS_IS_APP (app));

	/* only save this if the data is sufficiently high quality */
	if (quality <= app->license_quality)
		return;
	app->license_quality = quality;

	/* assume free software until we find an unmatched SPDX token */
	app->license_is_free = TRUE;

	/* tokenize the license string and URLify any SPDX IDs */
	urld = g_string_sized_new (strlen (license) + 1);
	tokens = as_utils_spdx_license_tokenize (license);
	for (i = 0; tokens[i] != NULL; i++) {

		/* translated join */
		if (g_strcmp0 (tokens[i], "&") == 0) {
			/* TRANSLATORS: This is how we join the licenses and can
			 * be considered a "Conjunctive AND Operator" according
			 * to the SPDX specification. For example:
			 * "LGPL-2.1 and MIT and BSD-2-Clause" */
			g_string_append (urld, _(" and "));
			continue;
		}
		if (g_strcmp0 (tokens[i], "|") == 0) {
			/* TRANSLATORS: This is how we join the licenses and can
			 * be considered a "Disjunctive OR Operator" according
			 * to the SPDX specification. For example:
			 * "LGPL-2.1 or MIT" */
			g_string_append (urld, _(" or "));
			continue;
		}

		/* do the best we can */
		if (gs_app_get_license_token_is_nonfree (tokens[i])) {
			g_debug ("nonfree license from %s: '%s'",
				 gs_app_get_id (app), tokens[i]);
			app->license_is_free = FALSE;
		}

		/* legacy literal text */
		if (g_str_has_prefix (tokens[i], "#")) {
			g_string_append (urld, tokens[i] + 1);
			continue;
		}

		/* proprietary software */
		if (g_strcmp0 (tokens[i], "@LicenseRef-proprietary") == 0) {
			const gchar *url = "https://en.wikipedia.org/wiki/Proprietary_software";
			g_string_append_printf (urld,
						"<a href=\"%s\">%s</a>",
						/* TRANSLATORS: non-free app */
						url, _("Proprietary"));
			continue;
		}

		/* public domain */
		if (g_strcmp0 (tokens[i], "@LicenseRef-public-domain") == 0) {
			const gchar *url = "https://en.wikipedia.org/wiki/Public_domain";
			g_string_append_printf (urld,
						"<a href=\"%s\">%s</a>",
						/* TRANSLATORS: see the wikipedia page */
						url, _("Public domain"));
			continue;
		}

		/* free software, license unspecified */
		if (g_str_has_prefix (tokens[i], "@LicenseRef-free")) {
			const gchar *url = "http://www.gnu.org/philosophy/free-sw.en.html";
			gchar *tmp;

			/* we support putting a custom URL in the
			 * token string, e.g. @LicenseRef-free=http://ubuntu.com */
			tmp = g_strstr_len (tokens[i], -1, "=");
			if (tmp != NULL)
				url = tmp + 1;
			g_string_append_printf (urld,
						"<a href=\"%s\">%s</a>",
						/* TRANSLATORS: see GNU page */
						url, _("Free Software"));
			continue;
		}

		/* SPDX value */
		if (g_str_has_prefix (tokens[i], "@")) {
			g_string_append_printf (urld,
						"<a href=\"http://spdx.org/licenses/%s\">%s</a>",
						tokens[i] + 1, tokens[i] + 1);
			continue;
		}

		/* new SPDX value the extractor didn't know about */
		if (as_utils_is_spdx_license_id (tokens[i])) {
			g_string_append_printf (urld,
						"<a href=\"http://spdx.org/licenses/%s\">%s</a>",
						tokens[i], tokens[i]);
			continue;
		}

		/* unknown value */
		g_string_append (urld, tokens[i]);
	}

	g_free (app->license);
	app->license = g_string_free (urld, FALSE);
}

/**
 * gs_app_get_summary_missing:
 */
const gchar *
gs_app_get_summary_missing (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->summary_missing;
}

/**
 * gs_app_set_summary_missing:
 */
void
gs_app_set_summary_missing (GsApp *app, const gchar *summary_missing)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->summary_missing);
	app->summary_missing = g_strdup (summary_missing);
}

/**
 * gs_app_get_menu_path:
 *
 * Returns the menu path which is an array of path elements.
 * The resulting array is an internal structure and must not be
 * modified or freed.
 *
 * Returns: a %NULL-terminated array of strings.
 */
gchar **
gs_app_get_menu_path (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->menu_path;
}

/**
 * gs_app_set_menu_path:
 * @menu_path: a %NULL-terminated array of strings
 *
 * Sets the new menu path. The menu path is an array of path elements.
 * This function creates a deep copy of the path.
 */
void
gs_app_set_menu_path (GsApp *app, gchar **menu_path)
{
	g_return_if_fail (GS_IS_APP (app));
	g_strfreev (app->menu_path);
	app->menu_path = g_strdupv (menu_path);
}

/**
 * gs_app_get_origin:
 */
const gchar *
gs_app_get_origin (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->origin;
}

/**
 * gs_app_set_origin:
 *
 * The origin is the original source of the application e.g. "fedora-updates"
 */
void
gs_app_set_origin (GsApp *app, const gchar *origin)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->origin);
	app->origin = g_strdup (origin);
}

/**
 * gs_app_get_origin_ui:
 */
const gchar *
gs_app_get_origin_ui (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->origin_ui;
}

/**
 * gs_app_set_origin_ui:
 *
 * The origin is the original source of the application to show in the UI,
 * e.g. "Fedora"
 */
void
gs_app_set_origin_ui (GsApp *app, const gchar *origin_ui)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->origin_ui);
	app->origin_ui = g_strdup (origin_ui);
}

/**
 * gs_app_add_screenshot:
 */
void
gs_app_add_screenshot (GsApp *app, AsScreenshot *screenshot)
{
	g_return_if_fail (GS_IS_APP (app));
	g_ptr_array_add (app->screenshots, g_object_ref (screenshot));
}

/**
 * gs_app_get_screenshots:
 */
GPtrArray *
gs_app_get_screenshots (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->screenshots;
}

/**
 * gs_app_get_update_version:
 */
const gchar *
gs_app_get_update_version (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->update_version;
}

/**
 * gs_app_get_update_version_ui:
 */
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
 */
void
gs_app_set_update_version (GsApp *app, const gchar *update_version)
{
	g_return_if_fail (GS_IS_APP (app));
	gs_app_set_update_version_internal (app, update_version);
	gs_app_queue_notify (app, "version");
}

/**
 * gs_app_get_update_details:
 */
const gchar *
gs_app_get_update_details (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->update_details;
}

/**
 * gs_app_set_update_details:
 */
void
gs_app_set_update_details (GsApp *app, const gchar *update_details)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->update_details);
	app->update_details = g_strdup (update_details);
}

/**
 * gs_app_get_update_urgency:
 */
AsUrgencyKind
gs_app_get_update_urgency (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), AS_URGENCY_KIND_UNKNOWN);
	return app->update_urgency;
}

/**
 * gs_app_set_update_urgency:
 */
void
gs_app_set_update_urgency (GsApp *app, AsUrgencyKind update_urgency)
{
	g_return_if_fail (GS_IS_APP (app));
	app->update_urgency = update_urgency;
}

/**
 * gs_app_get_management_plugin:
 */
const gchar *
gs_app_get_management_plugin (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->management_plugin;
}

/**
 * gs_app_set_management_plugin:
 *
 * The management plugin is the plugin that can handle doing install and remove
 * operations on the #GsApp. Typical values include "PackageKit" and "jhbuild"
 */
void
gs_app_set_management_plugin (GsApp *app, const gchar *management_plugin)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->management_plugin);
	app->management_plugin = g_strdup (management_plugin);
}

/**
 * gs_app_get_rating:
 */
gint
gs_app_get_rating (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), -1);
	return app->rating;
}

/**
 * gs_app_set_rating:
 */
void
gs_app_set_rating (GsApp *app, gint rating)
{
	g_return_if_fail (GS_IS_APP (app));
	app->rating = rating;
	gs_app_queue_notify (app, "rating");
}

/**
 * gs_app_get_review_ratings:
 */
GArray *
gs_app_get_review_ratings (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->review_ratings;
}

/**
 * gs_app_set_review_ratings:
 */
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
 */
GPtrArray *
gs_app_get_reviews (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->reviews;
}

/**
 * gs_app_add_review:
 */
void
gs_app_add_review (GsApp *app, GsReview *review)
{
	g_return_if_fail (GS_IS_APP (app));
	g_ptr_array_add (app->reviews, g_object_ref (review));
}

/**
 * gs_app_remove_review:
 */
void
gs_app_remove_review (GsApp *app, GsReview *review)
{
	g_return_if_fail (GS_IS_APP (app));
	g_ptr_array_remove (app->reviews, review);
}

/**
 * gs_app_get_size:
 */
guint64
gs_app_get_size (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), G_MAXUINT64);
	return app->size;
}

/**
 * gs_app_set_size:
 */
void
gs_app_set_size (GsApp *app, guint64 size)
{
	g_return_if_fail (GS_IS_APP (app));
	app->size = size;
}

/**
 * gs_app_get_metadata_item:
 */
const gchar *
gs_app_get_metadata_item (GsApp *app, const gchar *key)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return g_hash_table_lookup (app->metadata, key);
}

/**
 * gs_app_set_metadata:
 */
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
 */
GPtrArray *
gs_app_get_addons (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->addons;
}

/**
 * gs_app_add_addon:
 */
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
 */
GPtrArray *
gs_app_get_related (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->related;
}

/**
 * gs_app_add_related:
 */
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
 */
GPtrArray *
gs_app_get_history (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->history;
}

/**
 * gs_app_add_history:
 */
void
gs_app_add_history (GsApp *app, GsApp *app2)
{
	g_return_if_fail (GS_IS_APP (app));
	g_ptr_array_add (app->history, g_object_ref (app2));
}

guint64
gs_app_get_install_date (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return app->install_date;
}

void
gs_app_set_install_date (GsApp *app, guint64 install_date)
{
	g_return_if_fail (GS_IS_APP (app));
	app->install_date = install_date;
}

/**
 * gs_app_get_categories:
 */
GPtrArray *
gs_app_get_categories (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->categories;
}

/**
 * gs_app_has_category:
 */
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
 */
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
 */
void
gs_app_add_category (GsApp *app, const gchar *category)
{
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (category != NULL);
	g_ptr_array_add (app->categories, g_strdup (category));
}

/**
 * gs_app_get_keywords:
 */
GPtrArray *
gs_app_get_keywords (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->keywords;
}

/**
 * gs_app_set_keywords:
 */
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
 */
void
gs_app_add_kudo (GsApp *app, GsAppKudo kudo)
{
	g_return_if_fail (GS_IS_APP (app));
	app->kudos |= kudo;
}

/**
 * gs_app_get_kudos:
 */
guint64
gs_app_get_kudos (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return app->kudos;
}

/**
 * gs_app_get_kudos_percentage:
 */
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
	if ((app->kudos & GS_APP_KUDO_IBUS_HAS_SYMBOL) > 0)
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
 */
gboolean
gs_app_get_to_be_installed (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), FALSE);

	return app->to_be_installed;
}

/**
 * gs_app_set_to_be_installed:
 */
void
gs_app_set_to_be_installed (GsApp *app, gboolean to_be_installed)
{
	g_return_if_fail (GS_IS_APP (app));

	app->to_be_installed = to_be_installed;
}

/**
 * gs_app_has_quirk:
 **/
gboolean
gs_app_has_quirk (GsApp *app, AsAppQuirk quirk)
{
	g_return_val_if_fail (GS_IS_APP (app), FALSE);

	return (app->quirk & quirk) > 0;
}

/**
 * gs_app_add_quirk:
 **/
void
gs_app_add_quirk (GsApp *app, AsAppQuirk quirk)
{
	g_return_if_fail (GS_IS_APP (app));

	app->quirk |= quirk;
	gs_app_queue_notify (app, "quirk");
}

/**
 * gs_app_clear_quirk:
 **/
void
gs_app_clear_quirk (GsApp *app, AsAppQuirk quirk)
{
	g_return_if_fail (GS_IS_APP (app));

	app->quirk &= ~quirk;
	gs_app_queue_notify (app, "quirk");
}

/**
 * gs_app_set_match_value:
 */
void
gs_app_set_match_value (GsApp *app, guint match_value)
{
	g_return_if_fail (GS_IS_APP (app));
	app->match_value = match_value;
}

/**
 * gs_app_get_match_value:
 */
guint
gs_app_get_match_value (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return app->match_value;
}

/**
 * gs_app_get_last_error:
 */
GError *
gs_app_get_last_error (GsApp *app)
{
	return app->last_error;
}

/**
 * gs_app_set_last_error:
 */
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

	g_clear_object (&app->icon);
	g_clear_object (&app->runtime);
	g_clear_object (&app->pixbuf);

	g_clear_pointer (&app->addons, g_ptr_array_unref);
	g_clear_pointer (&app->history, g_ptr_array_unref);
	g_clear_pointer (&app->related, g_ptr_array_unref);
	g_clear_pointer (&app->screenshots, g_ptr_array_unref);
	g_clear_pointer (&app->reviews, g_ptr_array_unref);

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
	if (app->keywords != NULL)
		g_ptr_array_unref (app->keywords);
	if (app->last_error != NULL)
		g_error_free (app->last_error);

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
	app->addons = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	app->related = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	app->history = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	app->screenshots = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	app->reviews = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
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
 *
 * Return value: a new GsApp object.
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
