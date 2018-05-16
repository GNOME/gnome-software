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

/**
 * SECTION:gs-auth
 * @title: GsAuth
 * @include: gnome-software.h
 * @stability: Unstable
 * @short_description: User data used for authentication
 *
 * This object represents user data used for authentication.
 * This data is shared between all plugins.
 */

#include "config.h"

#include <libsecret/secret.h>

#include "gs-auth.h"
#include "gs-plugin.h"

struct _GsAuth
{
	GObject			 parent_instance;

	GsAuthFlags		 flags;
	gchar			*provider_id;
	gchar			*provider_name;
	gchar			*provider_logo;
	gchar			*provider_uri;
	gchar			*provider_schema;
	gchar			*username;
	gchar			*password;
	gchar			*pin;
	GHashTable		*metadata;	/* utf8: utf8 */
};

enum {
	PROP_0,
	PROP_USERNAME,
	PROP_PASSWORD,
	PROP_PIN,
	PROP_FLAGS,
	PROP_LAST
};

G_DEFINE_TYPE (GsAuth, gs_auth, G_TYPE_OBJECT)

/**
 * gs_auth_get_provider_id:
 * @auth: a #GsAuth
 *
 * Gets the authentication service ID.
 *
 * Returns: the string to use for searching, e.g. "UbuntuOne"
 */
const gchar *
gs_auth_get_provider_id (GsAuth *auth)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);
	return auth->provider_id;
}

/**
 * gs_auth_get_provider_name:
 * @auth: a #GsAuth
 *
 * Gets the authentication service name.
 *
 * Returns: the string to show in the UI
 */
const gchar *
gs_auth_get_provider_name (GsAuth *auth)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);
	return auth->provider_name;
}

/**
 * gs_auth_set_provider_name:
 * @auth: a #GsAuth
 * @provider_name: a service name, e.g. "GNOME Online Accounts"
 *
 * Sets the name to be used for the authentication dialog.
 */
void
gs_auth_set_provider_name (GsAuth *auth, const gchar *provider_name)
{
	g_return_if_fail (GS_IS_AUTH (auth));
	g_free (auth->provider_name);
	auth->provider_name = g_strdup (provider_name);
}

/**
 * gs_auth_get_provider_logo:
 * @auth: a #GsAuth
 *
 * Gets the authentication service image.
 *
 * Returns: the filename of an image, or %NULL
 */
const gchar *
gs_auth_get_provider_logo (GsAuth *auth)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);
	return auth->provider_logo;
}

/**
 * gs_auth_set_provider_logo:
 * @auth: a #GsAuth
 * @provider_logo: an image, e.g. "/usr/share/icons/gnome-online.png"
 *
 * Sets the image to be used for the authentication dialog.
 */
void
gs_auth_set_provider_logo (GsAuth *auth, const gchar *provider_logo)
{
	g_return_if_fail (GS_IS_AUTH (auth));
	g_free (auth->provider_logo);
	auth->provider_logo = g_strdup (provider_logo);
}

/**
 * gs_auth_get_provider_uri:
 * @auth: a #GsAuth
 *
 * Gets the authentication service website.
 *
 * Returns: the URI, or %NULL
 */
const gchar *
gs_auth_get_provider_uri (GsAuth *auth)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);
	return auth->provider_uri;
}

/**
 * gs_auth_set_provider_uri:
 * @auth: a #GsAuth
 * @provider_uri: a URI, e.g. "http://www.gnome.org/sso"
 *
 * Sets the website to be used for the authentication dialog.
 */
void
gs_auth_set_provider_uri (GsAuth *auth, const gchar *provider_uri)
{
	g_return_if_fail (GS_IS_AUTH (auth));
	g_free (auth->provider_uri);
	auth->provider_uri = g_strdup (provider_uri);
}

/**
 * gs_auth_get_provider_schema:
 * @auth: a #GsAuth
 *
 * Gets the authentication schema ID.
 *
 * Returns: the URI, or %NULL
 */
const gchar *
gs_auth_get_provider_schema (GsAuth *auth)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);
	return auth->provider_schema;
}

/**
 * gs_auth_set_provider_schema:
 * @auth: a #GsAuth
 * @provider_schema: a URI, e.g. "com.distro.provider"
 *
 * Sets the schema ID to be used for saving the state to disk.
 */
void
gs_auth_set_provider_schema (GsAuth *auth, const gchar *provider_schema)
{
	g_return_if_fail (GS_IS_AUTH (auth));
	g_free (auth->provider_schema);
	auth->provider_schema = g_strdup (provider_schema);
}

/**
 * gs_auth_get_username:
 * @auth: a #GsAuth
 *
 * Gets the auth username.
 *
 * Returns: the username to be used for the authentication
 */
const gchar *
gs_auth_get_username (GsAuth *auth)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);
	return auth->username;
}

/**
 * gs_auth_set_username:
 * @auth: a #GsAuth
 * @username: a username, e.g. "hughsie"
 *
 * Sets the username to be used for the authentication.
 */
void
gs_auth_set_username (GsAuth *auth, const gchar *username)
{
	g_return_if_fail (GS_IS_AUTH (auth));
	g_free (auth->username);
	auth->username = g_strdup (username);
}

/**
 * gs_auth_get_password:
 * @auth: a #GsAuth
 *
 * Gets the password to be used for the authentication.
 *
 * Returns: the string, or %NULL
 **/
const gchar *
gs_auth_get_password (GsAuth *auth)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);
	return auth->password;
}

/**
 * gs_auth_set_password:
 * @auth: a #GsAuth
 * @password: password string, e.g. "p@ssw0rd"
 *
 * Sets the password to be used for the authentication.
 */
void
gs_auth_set_password (GsAuth *auth, const gchar *password)
{
	g_return_if_fail (GS_IS_AUTH (auth));
	g_free (auth->password);
	auth->password = g_strdup (password);
}

/**
 * gs_auth_get_flags:
 * @auth: a #GsAuth
 *
 * Gets any flags set on the authentication, for example if we should remember
 * credentials.
 *
 * Returns: a #GsAuthFlags, e.g. %GS_AUTH_FLAG_REMEMBER
 */
GsAuthFlags
gs_auth_get_flags (GsAuth *auth)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), 0);
	return auth->flags;
}

/**
 * gs_auth_set_flags:
 * @auth: a #GsAuth
 * @flags: a #GsAuthFlags, e.g. %GS_AUTH_FLAG_REMEMBER
 *
 * Gets any flags set on the authentication.
 */
void
gs_auth_set_flags (GsAuth *auth, GsAuthFlags flags)
{
	g_return_if_fail (GS_IS_AUTH (auth));
	auth->flags = flags;
}

/**
 * gs_auth_add_flags:
 * @auth: a #GsAuth
 * @flags: a #GsAuthFlags, e.g. %GS_AUTH_FLAG_REMEMBER
 *
 * Adds flags to an existing authentication without replacing the other flags.
 */
void
gs_auth_add_flags (GsAuth *auth, GsAuthFlags flags)
{
	g_return_if_fail (GS_IS_AUTH (auth));
	auth->flags |= flags;
}

/**
 * gs_auth_has_flag:
 * @auth: a #GsAuth
 * @flags: a #GsAuthFlags, e.g. %GS_AUTH_FLAG_REMEMBER
 *
 * Finds out if the authentication has a flag.
 *
 * Returns: %TRUE if set
 */
gboolean
gs_auth_has_flag (GsAuth *auth, GsAuthFlags flags)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), FALSE);
	return (auth->flags & flags) > 0;
}

/**
 * gs_auth_get_pin:
 * @auth: a #GsAuth
 *
 * Gets the PIN code.
 *
 * Returns: the 2 factor authentication PIN, or %NULL
 **/
const gchar *
gs_auth_get_pin (GsAuth *auth)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);
	return auth->pin;
}

/**
 * gs_auth_set_pin:
 * @auth: a #GsAuth
 * @pin: the PIN code, e.g. "12345"
 *
 * Sets the 2 factor authentication PIN, which can be left unset.
 */
void
gs_auth_set_pin (GsAuth *auth, const gchar *pin)
{
	g_return_if_fail (GS_IS_AUTH (auth));
	g_free (auth->pin);
	auth->pin = g_strdup (pin);
}

/**
 * gs_auth_get_metadata_item:
 * @auth: a #GsAuth
 * @key: a string
 *
 * Gets some metadata from a authentication object.
 * It is left for the the plugin to use this method as required, but a
 * typical use would be to retrieve some secure auth token.
 *
 * Returns: A string value, or %NULL for not found
 */
const gchar *
gs_auth_get_metadata_item (GsAuth *auth, const gchar *key)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);
	g_return_val_if_fail (key != NULL, NULL);
	return g_hash_table_lookup (auth->metadata, key);
}

/**
 * gs_auth_add_metadata:
 * @auth: a #GsAuth
 * @key: a string
 * @value: a string
 *
 * Adds metadata to the authentication object.
 * It is left for the the plugin to use this method as required, but a
 * typical use would be to store some secure auth token.
 */
void
gs_auth_add_metadata (GsAuth *auth, const gchar *key, const gchar *value)
{
	g_return_if_fail (GS_IS_AUTH (auth));
	g_hash_table_insert (auth->metadata, g_strdup (key), g_strdup (value));
}

static gboolean
_g_error_is_set (GError **error)
{
	if (error == NULL)
		return FALSE;
	return *error != NULL;
}

/**
 * gs_auth_store_load:
 * @auth: a #GsAuth
 * @flags: some #GsAuthStoreFlags, e.g. %GS_AUTH_STORE_FLAG_USERNAME
 * @cancellable: a #GCancellable or %NULL
 * @error: a #GError or %NULL
 *
 * Loads authentication tokens from disk in a secure way.
 * By default only the username and password are loaded, but they are not
 * overwritten if already set.
 *
 * If additional tokens are required to be loaded you must first tell the
 * GsAuth instance what metadata to load. This can be done using:
 * `gs_auth_add_metadata("additional-secret-key-name",NULL)`
 *
 * This function is expected to be called from gs_plugin_setup().
 *
 * Returns: %TRUE if the tokens were loaded correctly.
 */
gboolean
gs_auth_store_load (GsAuth *auth, GsAuthStoreFlags flags,
		    GCancellable *cancellable, GError **error)
{
	SecretSchema schema = {
		auth->provider_schema,
		SECRET_SCHEMA_NONE,
		{ { "key", SECRET_SCHEMA_ATTRIBUTE_STRING } }
	};

	/* no schema */
	if (auth->provider_schema == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "No provider schema set for %s",
			     auth->provider_id);
		return FALSE;
	}

	/* username */
	if ((flags & GS_AUTH_STORE_FLAG_USERNAME) > 0 && auth->username == NULL) {
		auth->username = secret_password_lookup_sync (&schema,
							      cancellable,
							      error,
							      "key", "username",
							      NULL);
		if (_g_error_is_set (error))
			return FALSE;
	}

	/* password */
	if ((flags & GS_AUTH_STORE_FLAG_PASSWORD) > 0 && auth->password == NULL) {
		auth->password = secret_password_lookup_sync (&schema,
							      cancellable,
							      error,
							      "key", "password",
							      NULL);
		if (_g_error_is_set (error))
			return FALSE;
	}

	/* metadata */
	if (flags & GS_AUTH_STORE_FLAG_METADATA) {
		g_autoptr(GList) keys = NULL;
		keys = g_hash_table_get_keys (auth->metadata);
		for (GList *l = keys; l != NULL; l = l->next) {
			g_autofree gchar *tmp = NULL;
			const gchar *key = l->data;
			const gchar *value = g_hash_table_lookup (auth->metadata, key);
			if (value != NULL)
				continue;
			tmp = secret_password_lookup_sync (&schema,
							   cancellable,
							   error,
							   "key", key,
							   NULL);
			if (_g_error_is_set (error))
				return FALSE;
			if (tmp != NULL)
				gs_auth_add_metadata (auth, key, tmp);
		}
	}

	/* success */
	return TRUE;
}

/**
 * gs_auth_store_save:
 * @auth: a #GsAuth
 * @flags: some #GsAuthStoreFlags, e.g. %GS_AUTH_STORE_FLAG_USERNAME
 * @cancellable: a #GCancellable or %NULL
 * @error: a #GError or %NULL
 *
 * Saves the username, password and all added metadata to disk in a secure way.
 *
 * This function is expected to be called from gs_plugin_setup().
 *
 * Returns: %TRUE if the tokens were all saved correctly.
 */
gboolean
gs_auth_store_save (GsAuth *auth, GsAuthStoreFlags flags,
		    GCancellable *cancellable, GError **error)
{
	SecretSchema schema = {
		auth->provider_schema,
		SECRET_SCHEMA_NONE,
		{ { "key", SECRET_SCHEMA_ATTRIBUTE_STRING } }
	};

	/* no schema */
	if (auth->provider_schema == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "No provider schema set for %s",
			     auth->provider_id);
		return FALSE;
	}

	/* username */
	if ((flags & GS_AUTH_STORE_FLAG_USERNAME) > 0 && auth->username != NULL) {
		if (!secret_password_store_sync (&schema,
						 NULL, /* collection */
						 auth->provider_schema,
						 auth->username,
						 cancellable, error,
						 "key", "username", NULL))
			return FALSE;
	}

	/* password */
	if ((flags & GS_AUTH_STORE_FLAG_PASSWORD) > 0 && auth->password != NULL) {
		if (!secret_password_store_sync (&schema,
						 NULL, /* collection */
						 auth->provider_schema,
						 auth->password,
						 cancellable, error,
						 "key", "password", NULL))
			return FALSE;
	}

	/* metadata */
	if (flags & GS_AUTH_STORE_FLAG_METADATA) {
		g_autoptr(GList) keys = NULL;
		keys = g_hash_table_get_keys (auth->metadata);
		for (GList *l = keys; l != NULL; l = l->next) {
			const gchar *key = l->data;
			const gchar *value = g_hash_table_lookup (auth->metadata, key);
			if (value == NULL)
				continue;
			if (!secret_password_store_sync (&schema,
							 NULL, /* collection */
							 auth->provider_schema,
							 value,
							 cancellable, error,
							 "key", key, NULL))
				return FALSE;
		}
	}

	/* success */
	return TRUE;
}

gboolean
gs_auth_store_clear (GsAuth *auth, GsAuthStoreFlags flags,
		     GCancellable *cancellable, GError **error)
{
	SecretSchema schema = {
		auth->provider_schema,
		SECRET_SCHEMA_NONE,
		{ { "key", SECRET_SCHEMA_ATTRIBUTE_STRING } }
	};

	/* no schema */
	if (auth->provider_schema == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "No provider schema set for %s",
			     auth->provider_id);
		return FALSE;
	}

	/* username */
	if ((flags & GS_AUTH_STORE_FLAG_USERNAME) > 0) {
		if (!secret_password_clear_sync (&schema,
						 cancellable, error,
						 "key", "username", NULL))
			return FALSE;
	}

	g_free (auth->username);
	auth->username = NULL;

	/* password */
	if ((flags & GS_AUTH_STORE_FLAG_PASSWORD) > 0) {
		if (!secret_password_clear_sync (&schema,
						 cancellable, error,
						 "key", "password", NULL))
			return FALSE;
	}

	g_free (auth->password);
	auth->password = NULL;

	/* metadata */
	if (flags & GS_AUTH_STORE_FLAG_METADATA) {
		g_autoptr(GList) keys = NULL;
		keys = g_hash_table_get_keys (auth->metadata);
		for (GList *l = keys; l != NULL; l = l->next) {
			const gchar *key = l->data;
			if (!secret_password_clear_sync (&schema,
							 cancellable, error,
							 "key", key, NULL))
				return FALSE;

		}
	}

	g_hash_table_remove_all (auth->metadata);

	/* success */
	return TRUE;
}

static void
gs_auth_get_property (GObject *object, guint prop_id,
			GValue *value, GParamSpec *pspec)
{
	GsAuth *auth = GS_AUTH (object);

	switch (prop_id) {
	case PROP_USERNAME:
		g_value_set_string (value, auth->username);
		break;
	case PROP_PASSWORD:
		g_value_set_string (value, auth->password);
		break;
	case PROP_FLAGS:
		g_value_set_uint64 (value, auth->flags);
		break;
	case PROP_PIN:
		g_value_set_string (value, auth->pin);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_auth_set_property (GObject *object, guint prop_id,
			const GValue *value, GParamSpec *pspec)
{
	GsAuth *auth = GS_AUTH (object);

	switch (prop_id) {
	case PROP_USERNAME:
		gs_auth_set_username (auth, g_value_get_string (value));
		break;
	case PROP_PASSWORD:
		gs_auth_set_password (auth, g_value_get_string (value));
		break;
	case PROP_FLAGS:
		gs_auth_set_flags (auth, g_value_get_uint64 (value));
		break;
	case PROP_PIN:
		gs_auth_set_pin (auth, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_auth_finalize (GObject *object)
{
	GsAuth *auth = GS_AUTH (object);

	g_free (auth->provider_id);
	g_free (auth->provider_name);
	g_free (auth->provider_logo);
	g_free (auth->provider_uri);
	g_free (auth->provider_schema);
	g_free (auth->username);
	g_free (auth->password);
	g_free (auth->pin);
	g_hash_table_unref (auth->metadata);

	G_OBJECT_CLASS (gs_auth_parent_class)->finalize (object);
}

static void
gs_auth_class_init (GsAuthClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_auth_finalize;
	object_class->get_property = gs_auth_get_property;
	object_class->set_property = gs_auth_set_property;

	/**
	 * GsAuth:username:
	 */
	pspec = g_param_spec_string ("username", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_USERNAME, pspec);

	/**
	 * GsAuth:password:
	 */
	pspec = g_param_spec_string ("password", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_PASSWORD, pspec);

	/**
	 * GsAuth:flags:
	 */
	pspec = g_param_spec_uint64 ("flags", NULL, NULL,
				     GS_AUTH_FLAG_NONE,
				     GS_AUTH_FLAG_LAST,
				     GS_AUTH_FLAG_NONE,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_FLAGS, pspec);

	/**
	 * GsAuth:pin:
	 */
	pspec = g_param_spec_string ("pin", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_PIN, pspec);
}

static void
gs_auth_init (GsAuth *auth)
{
	auth->metadata = g_hash_table_new_full (g_str_hash, g_str_equal,
						g_free, g_free);
}

/**
 * gs_auth_new:
 * @provider_id: a provider ID used for mapping, e.g. "GnomeSSO"
 *
 * Return value: a new #GsAuth object.
 **/
GsAuth *
gs_auth_new (const gchar *provider_id)
{
	GsAuth *auth;
	auth = g_object_new (GS_TYPE_AUTH, NULL);
	auth->provider_id = g_strdup (provider_id);
	return GS_AUTH (auth);
}

/* vim: set noexpandtab: */
