/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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
 * of a package. This rule really only applies to GsApps of kind GS_APP_KIND_NORMAL
 * and GS_APP_KIND_SYSTEM. We allow GsApps of kind GS_APP_KIND_SYSTEM_UPDATE or
 * GS_APP_KIND_PACKAGE, which don't correspond to desktop files, but instead
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

#include "gs-app.h"

static void	gs_app_finalize	(GObject	*object);

#define GS_APP_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_APP, GsAppPrivate))

struct GsAppPrivate
{
	gchar			*id;
	gchar			*name;
	gchar			*source;
	gchar			*project_group;
	gchar			*version;
	gchar			*summary;
	gchar			*description;
	GPtrArray		*screenshots;
	gchar			*url;
	gchar			*update_version;
	gchar			*update_details;
	gchar			*management_plugin;
	gint			 rating;
	GsAppKind		 kind;
	GsAppState		 state;
	GHashTable		*metadata;
	GdkPixbuf		*pixbuf;
	GdkPixbuf		*featured_pixbuf;
	GPtrArray		*related; /* of GsApp */
	GPtrArray		*history; /* of GsApp */
	guint64			 install_date;
};

enum {
	PROP_0,
	PROP_ID,
	PROP_NAME,
	PROP_VERSION,
	PROP_SUMMARY,
	PROP_DESCRIPTION,
	PROP_URL,
	PROP_RATING,
	PROP_KIND,
	PROP_STATE,
	PROP_INSTALL_DATE,
	PROP_LAST
};

G_DEFINE_TYPE (GsApp, gs_app, G_TYPE_OBJECT)

enum {
	SIGNAL_STATE_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

/**
 * gs_app_error_quark:
 * Return value: Our personal error quark.
 **/
GQuark
gs_app_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("gs_app_error");
	return quark;
}

/**
 * gs_app_kind_to_string:
 **/
const gchar *
gs_app_kind_to_string (GsAppKind kind)
{
	if (kind == GS_APP_KIND_UNKNOWN)
		return "unknown";
	if (kind == GS_APP_KIND_NORMAL)
		return "normal";
	if (kind == GS_APP_KIND_SYSTEM)
		return "system";
	if (kind == GS_APP_KIND_PACKAGE)
		return "package";
	if (kind == GS_APP_KIND_OS_UPDATE)
		return "os-update";
	return NULL;
}

/**
 * gs_app_state_to_string:
 **/
const gchar *
gs_app_state_to_string (GsAppState state)
{
	if (state == GS_APP_STATE_UNKNOWN)
		return "unknown";
	if (state == GS_APP_STATE_INSTALLED)
		return "installed";
	if (state == GS_APP_STATE_AVAILABLE)
		return "available";
	if (state == GS_APP_STATE_INSTALLING)
		return "installing";
	if (state == GS_APP_STATE_REMOVING)
		return "removing";
	if (state == GS_APP_STATE_UPDATABLE)
		return "updatable";
	return NULL;
}

/**
 * gs_app_to_string:
 **/
gchar *
gs_app_to_string (GsApp *app)
{
	GList *keys;
	GList *l;
	GString *str;
	GsAppPrivate *priv = app->priv;
	GsScreenshot *ss;
	const gchar *tmp;
	guint i;

	str = g_string_new ("GsApp:\n");
	g_string_append_printf (str, "\tkind:\t%s\n",
				gs_app_kind_to_string (priv->kind));
	g_string_append_printf (str, "\tstate:\t%s\n",
				gs_app_state_to_string (priv->state));
	if (priv->id != NULL)
		g_string_append_printf (str, "\tid:\t%s\n", priv->id);
	if (priv->name != NULL)
		g_string_append_printf (str, "\tname:\t%s\n", priv->name);
	if (priv->version != NULL)
		g_string_append_printf (str, "\tversion:\t%s\n", priv->version);
	if (priv->summary != NULL)
		g_string_append_printf (str, "\tsummary:\t%s\n", priv->summary);
	if (priv->description != NULL)
		g_string_append_printf (str, "\tdescription:\t%lu\n", strlen (priv->description));
	for (i = 0; i < priv->screenshots->len; i++) {
		ss = g_ptr_array_index (priv->screenshots, i);
		g_string_append_printf (str, "\tscreenshot-%02i:\t%s\n",
					i, gs_screenshot_get_url (ss, G_MAXUINT, G_MAXUINT));
	}
	if (priv->url != NULL)
		g_string_append_printf (str, "\turl:\t%s\n", priv->url);
	if (priv->rating != -1)
		g_string_append_printf (str, "\trating:\t%i\n", priv->rating);
	if (priv->pixbuf != NULL)
		g_string_append_printf (str, "\tpixbuf:\t%p\n", priv->pixbuf);
	if (priv->featured_pixbuf != NULL)
		g_string_append_printf (str, "\tfeatured-pixbuf:\t%p\n", priv->featured_pixbuf);
	if (priv->install_date != 0)
		g_string_append_printf (str, "\tinstall-date:\t%lu\n", priv->install_date);
	if (priv->related->len > 0)
		g_string_append_printf (str, "\trelated:\t%i\n", priv->related->len);
	if (priv->history->len > 0)
		g_string_append_printf (str, "\thistory:\t%i\n", priv->history->len);
	keys = g_hash_table_get_keys (priv->metadata);
	for (l = keys; l != NULL; l = l->next) {
		tmp = g_hash_table_lookup (priv->metadata, l->data);
		g_string_append_printf (str, "\t{%s}:\t%s\n",
					(const gchar *) l->data, tmp);
	}
	g_list_free (keys);
	return g_string_free (str, FALSE);
}

/**
 * gs_app_get_id:
 **/
const gchar *
gs_app_get_id (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->id;
}

/**
 * gs_app_set_id:
 */
void
gs_app_set_id (GsApp *app, const gchar *id)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->priv->id);
	app->priv->id = g_strdup (id);
}

/**
 * gs_app_get_state:
 */
GsAppState
gs_app_get_state (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), GS_APP_STATE_UNKNOWN);
	return app->priv->state;
}

/**
 * gs_app_set_state:
 *
 * This sets the state of the application. The following state diagram explains
 * the typical states. All applications start in state %GS_APP_STATE_UNKNOWN,
 * but the frontend is not supposed to see GsApps with this state, ever.
 * Backend plugins are reponsible for changing the state to one of the other
 * states before the GsApp is passed to the frontend. This is enforced by the
 * #GsPluginLoader.
 *
 * UPDATABLE --> INSTALLING --> INSTALLED
 * UPDATABLE --> REMOVING   --> AVAILABLE
 * INSTALLED --> REMOVING   --> AVAILABLE
 * AVAILABLE --> INSTALLING --> INSTALLED
 */
void
gs_app_set_state (GsApp *app, GsAppState state)
{
	gboolean state_change_ok = FALSE;
	GsAppPrivate *priv = app->priv;

	g_return_if_fail (GS_IS_APP (app));
	if (priv->state == state)
		return;

	/* check the state change is allowed */
	switch (priv->state) {
	case GS_APP_STATE_UNKNOWN:
		/* unknown has to go into one of the stable states */
		if (state == GS_APP_STATE_INSTALLED ||
		    state == GS_APP_STATE_AVAILABLE ||
		    state == GS_APP_STATE_UPDATABLE)
			state_change_ok = TRUE;
		break;
	case GS_APP_STATE_INSTALLED:
		/* installed has to go into an action state */
		if (state == GS_APP_STATE_UNKNOWN ||
		    state == GS_APP_STATE_REMOVING)
			state_change_ok = TRUE;
		break;
	case GS_APP_STATE_AVAILABLE:
		/* available has to go into an action state */
		if (state == GS_APP_STATE_UNKNOWN ||
		    state == GS_APP_STATE_INSTALLING)
			state_change_ok = TRUE;
		break;
	case GS_APP_STATE_INSTALLING:
		/* installing has to go into an stable state */
		if (state == GS_APP_STATE_UNKNOWN ||
		    state == GS_APP_STATE_INSTALLED ||
		    state == GS_APP_STATE_AVAILABLE)
			state_change_ok = TRUE;
		break;
	case GS_APP_STATE_REMOVING:
		/* removing has to go into an stable state */
		if (state == GS_APP_STATE_UNKNOWN ||
		    state == GS_APP_STATE_AVAILABLE ||
		    state == GS_APP_STATE_INSTALLED)
			state_change_ok = TRUE;
		break;
	case GS_APP_STATE_UPDATABLE:
		/* updatable has to go into an action state */
		if (state == GS_APP_STATE_UNKNOWN ||
		    state == GS_APP_STATE_REMOVING)
			state_change_ok = TRUE;
		break;
	default:
		g_warning ("state %s unhandled",
			   gs_app_state_to_string (priv->state));
		g_assert_not_reached ();
	}

	/* this state change was unexpected */
	if (!state_change_ok) {
		g_warning ("State change on %s from %s to %s is not OK",
			   priv->id,
			   gs_app_state_to_string (priv->state),
			   gs_app_state_to_string (state));
		return;
	}

	priv->state = state;
	g_object_notify (G_OBJECT (app), "state");
	g_signal_emit (app, signals[SIGNAL_STATE_CHANGED], 0);
}

/**
 * gs_app_get_kind:
 */
GsAppKind
gs_app_get_kind (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), GS_APP_KIND_UNKNOWN);
	return app->priv->kind;
}

/**
 * gs_app_set_kind:
 *
 * This sets the kind of the application. The following state diagram explains
 * the typical states. All applications start with kind %GS_APP_KIND_UNKNOWN.
 *
 * PACKAGE --> NORMAL
 * PACKAGE --> SYSTEM
 * NORMAL  --> SYSTEM
 */
void
gs_app_set_kind (GsApp *app, GsAppKind kind)
{
	gboolean state_change_ok = FALSE;
	GsAppPrivate *priv = app->priv;

	g_return_if_fail (GS_IS_APP (app));
	if (priv->kind == kind)
		return;

	/* check the state change is allowed */
	switch (priv->kind) {
	case GS_APP_KIND_UNKNOWN:
		/* unknown can go into any state */
		state_change_ok = TRUE;
		break;
	case GS_APP_KIND_PACKAGE:
		/* package can become either normal or a system application */
		if (kind == GS_APP_KIND_NORMAL ||
		    kind == GS_APP_KIND_SYSTEM)
			state_change_ok = TRUE;
		break;
	case GS_APP_KIND_NORMAL:
		/* normal can only be promoted to system */
		if (kind == GS_APP_KIND_SYSTEM)
			state_change_ok = TRUE;
		break;
	case GS_APP_KIND_SYSTEM:
	case GS_APP_KIND_OS_UPDATE:
		/* this can never change state */
		break;
	default:
		g_warning ("kind %s unhandled",
			   gs_app_kind_to_string (priv->kind));
		g_assert_not_reached ();
	}

	/* this state change was unexpected */
	if (!state_change_ok) {
		g_warning ("Kind change on %s from %s to %s is not OK",
			   priv->id,
			   gs_app_kind_to_string (priv->kind),
			   gs_app_kind_to_string (kind));
		return;
	}

	priv->kind = kind;
	g_object_notify (G_OBJECT (app), "kind");
	g_signal_emit (app, signals[SIGNAL_STATE_CHANGED], 0);
}

/**
 * gs_app_get_name:
 */
const gchar *
gs_app_get_name (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->name;
}

/**
 * gs_app_set_name:
 * @app:	A #GsApp instance
 * @name:	The short localized name, e.g. "Calculator"
 */
void
gs_app_set_name (GsApp *app, const gchar *name)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->priv->name);
	app->priv->name = g_strdup (name);
}

/**
 * gs_app_get_source:
 */
const gchar *
gs_app_get_source (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->source;
}

/**
 * gs_app_set_source:
 * @app:	A #GsApp instance
 * @source:	The non-localized short name, e.g. "gnome-calculator"
 *
 * This name is used for the update page if the application is collected into
 * the 'OS Updates' group. It is typically the package name, although this
 * should not be relied upon.
 */
void
gs_app_set_source (GsApp *app, const gchar *source)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->priv->source);
	app->priv->source = g_strdup (source);
}

/**
 * gs_app_get_project_group:
 */
const gchar *
gs_app_get_project_group (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->project_group;
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
	g_free (app->priv->project_group);
	app->priv->project_group = g_strdup (project_group);
}

/**
 * gs_app_get_pixbuf:
 */
GdkPixbuf *
gs_app_get_pixbuf (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->pixbuf;
}

/**
 * gs_app_set_pixbuf:
 */
void
gs_app_set_pixbuf (GsApp *app, GdkPixbuf *pixbuf)
{
	g_return_if_fail (GS_IS_APP (app));
	if (app->priv->pixbuf != NULL)
		g_object_unref (app->priv->pixbuf);
	app->priv->pixbuf = g_object_ref (pixbuf);
}

/**
 * gs_app_get_featured_pixbuf:
 */
GdkPixbuf *
gs_app_get_featured_pixbuf (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->featured_pixbuf;
}

/**
 * gs_app_set_featured_pixbuf:
 */
void
gs_app_set_featured_pixbuf (GsApp *app, GdkPixbuf *pixbuf)
{
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (app->priv->featured_pixbuf == NULL);
	app->priv->featured_pixbuf = g_object_ref (pixbuf);
}

/**
 * gs_app_get_version:
 */
const gchar *
gs_app_get_version (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->version;
}

/**
 * gs_app_get_pretty_version:
 *
 * convert 1:1.6.2-7.fc17 into "Version 1.6.2"
 **/
static gchar *
gs_app_get_pretty_version (const gchar *version)
{
	guint i;
	gchar *new = NULL;
	gchar *f;

	/* nothing set */
	if (version == NULL)
		goto out;

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
	f = g_strstr_len (new, -1, ".fc");
	if (f != NULL)
		*f= '\0';

	/* then remove any release */
	f = g_strrstr_len (new, -1, "-");
	if (f != NULL)
		*f= '\0';

	/* then remove any git suffix */
	f = g_strrstr_len (new, -1, ".2012");
	if (f != NULL)
		*f= '\0';
	f = g_strrstr_len (new, -1, ".2013");
	if (f != NULL)
		*f= '\0';
out:
	return new;
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
	g_free (app->priv->version);
	app->priv->version = gs_app_get_pretty_version (version);
}

/**
 * gs_app_get_summary:
 */
const gchar *
gs_app_get_summary (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->summary;
}

/**
 * gs_app_set_summary:
 * @app:	A #GsApp instance
 * @summary:	The medium length localized name, e.g. "A graphical calculator for GNOME"
 */
void
gs_app_set_summary (GsApp *app, const gchar *summary)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->priv->summary);
	app->priv->summary = g_strdup (summary);
}

/**
 * gs_app_get_description:
 */
const gchar *
gs_app_get_description (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->description;
}

/**
 * gs_app_set_description:
 * @app:	A #GsApp instance
 * @summary:	The multiline localized description, e.g. "GNOME Calculator is a graphical calculator for GNOME....."
 */
void
gs_app_set_description (GsApp *app, const gchar *description)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->priv->description);
	app->priv->description = g_strdup (description);
}

/**
 * gs_app_get_url:
 */
const gchar *
gs_app_get_url (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->url;
}

/**
 * gs_app_set_url:
 * @app:	A #GsApp instance
 * @summary:	The home page URL, e.g. "http://www.foo.com/gcalctool/"
 */
void
gs_app_set_url (GsApp *app, const gchar *url)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->priv->url);
	app->priv->url = g_strdup (url);
}

/**
 * gs_app_add_screenshot:
 */
void
gs_app_add_screenshot (GsApp *app, GsScreenshot *screenshot)
{
	g_return_if_fail (GS_IS_APP (app));
	g_ptr_array_add (app->priv->screenshots, g_object_ref (screenshot));
}

/**
 * gs_app_get_screenshots:
 */
GPtrArray *
gs_app_get_screenshots (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->screenshots;
}

/**
 * gs_app_get_update_version:
 */
const gchar *
gs_app_get_update_version (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->update_version;
}

/**
 * gs_app_set_update_version:
 */
void
gs_app_set_update_version (GsApp *app, const gchar *update_version)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->priv->update_version);
	app->priv->update_version = gs_app_get_pretty_version (update_version);
}

/**
 * gs_app_get_update_details:
 */
const gchar *
gs_app_get_update_details (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->update_details;
}

/**
 * gs_app_set_update_details:
 */
void
gs_app_set_update_details (GsApp *app, const gchar *update_details)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->priv->update_details);
	app->priv->update_details = g_strdup (update_details);
}

/**
 * gs_app_get_management_plugin:
 */
const gchar *
gs_app_get_management_plugin (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->management_plugin;
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
	g_free (app->priv->management_plugin);
	app->priv->management_plugin = g_strdup (management_plugin);
}

/**
 * gs_app_get_rating:
 */
gint
gs_app_get_rating (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), -1);
	return app->priv->rating;
}

/**
 * gs_app_set_rating:
 */
void
gs_app_set_rating (GsApp *app, gint rating)
{
	g_return_if_fail (GS_IS_APP (app));
	app->priv->rating = rating;
}

/**
 * gs_app_get_metadata_item:
 */
const gchar *
gs_app_get_metadata_item (GsApp *app, const gchar *key)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return g_hash_table_lookup (app->priv->metadata, key);
}

/**
 * gs_app_set_metadata:
 */
void
gs_app_set_metadata (GsApp *app, const gchar *key, const gchar *value)
{
	g_return_if_fail (GS_IS_APP (app));
	g_hash_table_insert (app->priv->metadata,
			     g_strdup (key),
			     g_strdup (value));
}

/**
 * gs_app_get_related:
 */
GPtrArray *
gs_app_get_related (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->related;
}

/**
 * gs_app_add_related:
 */
void
gs_app_add_related (GsApp *app, GsApp *app2)
{
	g_return_if_fail (GS_IS_APP (app));
	g_ptr_array_add (app->priv->related, g_object_ref (app2));
}

/**
 * gs_app_get_history:
 */
GPtrArray *
gs_app_get_history (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->history;
}

/**
 * gs_app_add_history:
 */
void
gs_app_add_history (GsApp *app, GsApp *app2)
{
	g_return_if_fail (GS_IS_APP (app));
	g_ptr_array_add (app->priv->history, g_object_ref (app2));
}

guint64
gs_app_get_install_date (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), 0);
	return app->priv->install_date;
}

void
gs_app_set_install_date (GsApp *app, guint64 install_date)
{
	g_return_if_fail (GS_IS_APP (app));
	app->priv->install_date = install_date;
}

/**
 * gs_app_get_property:
 */
static void
gs_app_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsApp *app = GS_APP (object);
	GsAppPrivate *priv = app->priv;

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
	case PROP_URL:
		g_value_set_string (value, priv->url);
		break;
	case PROP_RATING:
		g_value_set_uint (value, priv->rating);
		break;
	case PROP_KIND:
		g_value_set_uint (value, priv->kind);
		break;
	case PROP_STATE:
		g_value_set_uint (value, priv->state);
		break;
	case PROP_INSTALL_DATE:
		g_value_set_uint64 (value, priv->install_date);
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
		gs_app_set_name (app, g_value_get_string (value));
		break;
	case PROP_VERSION:
		gs_app_set_version (app, g_value_get_string (value));
		break;
	case PROP_SUMMARY:
		gs_app_set_summary (app, g_value_get_string (value));
		break;
	case PROP_DESCRIPTION:
		gs_app_set_description (app, g_value_get_string (value));
		break;
	case PROP_URL:
		gs_app_set_url (app, g_value_get_string (value));
		break;
	case PROP_RATING:
		gs_app_set_rating (app, g_value_get_int (value));
		break;
	case PROP_KIND:
		gs_app_set_kind (app, g_value_get_uint (value));
		break;
	case PROP_STATE:
		gs_app_set_state (app, g_value_get_uint (value));
		break;
	case PROP_INSTALL_DATE:
		gs_app_set_install_date (app, g_value_get_uint64 (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
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

	pspec = g_param_spec_string ("url", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_URL, pspec);

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
				   GS_APP_KIND_UNKNOWN,
				   GS_APP_KIND_LAST,
				   GS_APP_KIND_UNKNOWN,
				   G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_KIND, pspec);

	/**
	 * GsApp:state:
	 */
	pspec = g_param_spec_uint ("state", NULL, NULL,
				   GS_APP_STATE_UNKNOWN,
				   GS_APP_STATE_LAST,
				   GS_APP_STATE_UNKNOWN,
				   G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_STATE, pspec);

	pspec = g_param_spec_uint64 ("install-date", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_INSTALL_DATE, pspec);

	signals [SIGNAL_STATE_CHANGED] =
		g_signal_new ("state-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsAppClass, state_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (GsAppPrivate));
}

/**
 * gs_app_init:
 **/
static void
gs_app_init (GsApp *app)
{
	app->priv = GS_APP_GET_PRIVATE (app);
	app->priv->rating = -1;
	app->priv->related = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	app->priv->history = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	app->priv->screenshots = g_ptr_array_new_with_free_func ((GDestroyNotify) g_object_unref);
	app->priv->metadata = g_hash_table_new_full (g_str_hash,
						     g_str_equal,
						     g_free,
						     g_free);
}

/**
 * gs_app_finalize:
 * @object: The object to finalize
 **/
static void
gs_app_finalize (GObject *object)
{
	GsApp *app = GS_APP (object);
	GsAppPrivate *priv = app->priv;

	g_free (priv->id);
	g_free (priv->name);
	g_free (priv->url);
	g_free (priv->source);
	g_free (priv->project_group);
	g_free (priv->version);
	g_free (priv->summary);
	g_free (priv->description);
	g_ptr_array_unref (priv->screenshots);
	g_free (priv->update_version);
	g_free (priv->update_details);
	g_free (priv->management_plugin);
	g_hash_table_unref (priv->metadata);
	g_ptr_array_unref (priv->related);
	g_ptr_array_unref (priv->history);
	if (priv->pixbuf != NULL)
		g_object_unref (priv->pixbuf);
	if (priv->featured_pixbuf != NULL)
		g_object_unref (priv->featured_pixbuf);

	G_OBJECT_CLASS (gs_app_parent_class)->finalize (object);
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
