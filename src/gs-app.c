/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include "gs-app.h"

static void	gs_app_finalize	(GObject	*object);

#define GS_APP_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_APP, GsAppPrivate))

struct GsAppPrivate
{
	gchar			*id;
	gchar			*name;
	gchar			*version;
	gchar			*summary;
	gchar			*description;
	gchar			*screenshot;
        gchar                   *url;
	gint			 rating;
	GsAppKind		 kind;
	GsAppState		 state;
	GHashTable		*metadata;
	GdkPixbuf		*pixbuf;
	GPtrArray		*related; /* of GsApp */
};

enum {
	PROP_0,
	PROP_ID,
	PROP_NAME,
	PROP_VERSION,
	PROP_SUMMARY,
	PROP_DESCRIPTION,
	PROP_URL,
	PROP_SCREENSHOT,
	PROP_RATING,
	PROP_KIND,
	PROP_STATE,
	PROP_LAST
};

G_DEFINE_TYPE (GsApp, gs_app, G_TYPE_OBJECT)

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
	g_return_if_fail (id != NULL);

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
 */
void
gs_app_set_state (GsApp *app, GsAppState state)
{
	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (app->priv->state == GS_APP_STATE_UNKNOWN);
	app->priv->state = state;
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
 */
void
gs_app_set_kind (GsApp *app, GsAppKind kind)
{
	g_return_if_fail (GS_IS_APP (app));
	app->priv->kind = kind;
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
 */
void
gs_app_set_name (GsApp *app, const gchar *name)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->priv->name);
	app->priv->name = g_strdup (name);
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
	g_return_if_fail (app->priv->pixbuf == NULL);
	app->priv->pixbuf = g_object_ref (pixbuf);
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
 */
void
gs_app_set_summary (GsApp *app, const gchar *summary)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->priv->summary);
	app->priv->summary = g_strdup (summary);
}

const gchar *
gs_app_get_description (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->description;
}

void
gs_app_set_description (GsApp *app, const gchar *description)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->priv->description);
	app->priv->description = g_strdup (description);
}

const gchar *
gs_app_get_url (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->url;
}

void
gs_app_set_url (GsApp *app, const gchar *url)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->priv->url);
	app->priv->url = g_strdup (url);
}

/**
 * gs_app_get_screenshot:
 */
const gchar *
gs_app_get_screenshot (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);
	return app->priv->screenshot;
}

/**
 * gs_app_set_screenshot:
 */
void
gs_app_set_screenshot (GsApp *app, const gchar *screenshot)
{
	g_return_if_fail (GS_IS_APP (app));
	g_free (app->priv->screenshot);
	app->priv->screenshot = g_strdup (screenshot);
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
	g_debug ("setting %s to %s for %s",
		 key, value, gs_app_get_id (app));
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
	case PROP_SCREENSHOT:
		g_value_set_string (value, priv->screenshot);
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
	case PROP_SCREENSHOT:
		gs_app_set_screenshot (app, g_value_get_string (value));
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
	 * GsApp:screenshot:
	 */
	pspec = g_param_spec_string ("screenshot", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_SCREENSHOT, pspec);

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
	g_free (priv->version);
	g_free (priv->summary);
	g_free (priv->screenshot);
	g_hash_table_unref (priv->metadata);
	g_ptr_array_unref (priv->related);
	if (priv->pixbuf != NULL)
		g_object_unref (priv->pixbuf);

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
