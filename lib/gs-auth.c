/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Canonical Ltd
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

#include "gs-auth.h"
#include "gs-plugin.h"

struct _GsAuth
{
	GObject		 parent_instance;

	gchar		*header_none;
	gchar		*header_single;
	gchar		*header_multiple;

	gchar		*provider_id;
	gchar		*provider_name;
	gchar		*provider_type;

	GoaClient	*goa_client;
	GoaObject	*goa_object;

	GSettings	*settings;
};

G_DEFINE_TYPE (GsAuth, gs_auth, G_TYPE_OBJECT)

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };


/**
 * gs_auth_get_header:
 * @auth: a #GsAuth
 * @n: the number of accounts
 *
 * Gets the header to be used in the UI in case there are @n accounts available.
 *
 * Returns: the string to show in the UI
 */
const gchar *
gs_auth_get_header (GsAuth *auth, guint n)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);

	if (n == 0)
		return auth->header_none;
	else if (n == 1)
		return auth->header_single;
	else
		return auth->header_multiple;
}

/**
 * gs_auth_set_header:
 * @auth: a #GsAuth
 * @header_none: the header to be used if no account is present
 * @header_single: the header to be used if one account is present
 * header_multiple: the header to be used if two or more accounts are present
 *
 * Sets the header to be used for the authentication dialog.
 */
void
gs_auth_set_header (GsAuth *auth,
		    const gchar *header_none,
		    const gchar *header_single,
		    const gchar *header_multiple)
{
	g_return_if_fail (GS_IS_AUTH (auth));
	g_return_if_fail (header_none != NULL);
	g_return_if_fail (header_single != NULL);
	g_return_if_fail (header_multiple != NULL);

	g_free (auth->header_none);
	g_free (auth->header_single);
	g_free (auth->header_multiple);

	auth->header_none = g_strdup (header_none);
	auth->header_single = g_strdup (header_single);
	auth->header_multiple = g_strdup (header_multiple);
}

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
	g_return_if_fail (provider_name != NULL);

	g_free (auth->provider_name);
	auth->provider_name = g_strdup (provider_name);
}

/**
 * gs_auth_get_provider_type:
 * @auth: a #GsAuth
 *
 * Gets GoaProvider type to be used for the authentication dialog
 *
 * Returns: the GoaProvider type to be used for the authentication dialog
 */
const gchar *
gs_auth_get_provider_type (GsAuth *auth)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);
	return auth->provider_type;
}

/**
 * gs_auth_get_account:
 * @auth: a #GsAuth
 *
 * Gets the logged #GoaObject.
 *
 * Returns: the #GoaObject
 */
GoaObject *
gs_auth_get_account (GsAuth *auth)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);
	return auth->goa_object;
}

/**
 * gs_auth_set_account:
 * @auth: a #GsAuth
 * @account_id: an account id
 *
 * Set the account-id used to login in.
 */
void
gs_auth_set_account_id (GsAuth *auth, const gchar *account_id)
{
	g_autoptr(GError) error = NULL;

	g_return_if_fail (GS_IS_AUTH (auth));

	/* lazy initialize goa_client */
	if (auth->goa_client == NULL) {
		auth->goa_client = goa_client_new_sync (NULL, &error);

		if (auth->goa_client == NULL) {
			g_warning ("Failed to get a goa client: %s",
				   error->message);
			return;
		}
	}

	/* check for no change */
	if (auth->goa_object == NULL && account_id == NULL)
		return;

	if (auth->goa_object != NULL) {
		GoaAccount *goa_account =
			goa_object_peek_account (auth->goa_object);

		if (!g_strcmp0 (account_id, goa_account_get_id (goa_account)))
			return;
	}

	g_clear_object (&auth->goa_object);
	if (account_id != NULL) {
		auth->goa_object = goa_client_lookup_by_id (auth->goa_client,
							    account_id);
	}

	g_signal_emit (auth, signals[SIGNAL_CHANGED], 0, account_id);
}

/**
 * gs_auth_store_load:
 * @auth: a #GsAuth
 *
 * Loads the account-id from disk
 *
 * This function is expected to be called from gs_plugin_setup().
 *
 * Returns: %TRUE if the account-id was loaded correctly.
 */
gboolean
gs_auth_store_load (GsAuth *auth)
{
	g_autoptr(GVariant) variant = NULL;
	GVariantDict dict;
	g_autofree gchar *id = NULL;

	g_return_val_if_fail (GS_IS_AUTH (auth), FALSE);

	variant = g_settings_get_value (auth->settings, "account-id");
	g_variant_dict_init (&dict, variant);

	if (!g_variant_dict_lookup (&dict, auth->provider_id, "s", &id)) {
		g_variant_dict_clear (&dict);
		gs_auth_set_account_id (auth, NULL);
		return FALSE;
	}

	gs_auth_set_account_id (auth, id);
	g_variant_dict_clear (&dict);
	return TRUE;
}

static void
gs_auth_settings_changed (GSettings *settings,
			  const gchar *key,
			  GsAuth *auth)
{
	gs_auth_store_load (auth);
}

/**
 * gs_auth_store_save:
 * @auth: a #GsAuth
 *
 * Saves the account-id to disk.
 *
 */
gboolean
gs_auth_store_save (GsAuth *auth)
{
	g_autoptr(GVariant) variant = NULL;
	GVariantDict dict;

	g_return_val_if_fail (GS_IS_AUTH (auth), FALSE);

	variant = g_settings_get_value (auth->settings, "account-id");
	g_variant_dict_init (&dict, variant);

	if (auth->goa_object) {
		GoaAccount *goa_account =
			goa_object_peek_account (auth->goa_object);

		g_variant_dict_insert (&dict,
				       auth->provider_id,
				       "s",
				       goa_account_get_id (goa_account));
	} else {
		g_variant_dict_remove (&dict, auth->provider_id);
	}

	g_signal_handlers_block_by_func (auth->settings,
					 gs_auth_settings_changed, auth);
	g_settings_set_value (auth->settings,
			      "account-id", g_variant_dict_end (&dict));
	g_signal_handlers_unblock_by_func (auth->settings,
					   gs_auth_settings_changed, auth);

	return TRUE;
}

static void
gs_auth_finalize (GObject *object)
{
	GsAuth *auth = GS_AUTH (object);

	g_free (auth->header_none);
	g_free (auth->header_single);
	g_free (auth->header_multiple);
	g_free (auth->provider_id);
	g_free (auth->provider_name);

	g_clear_object (&auth->goa_client);
	g_clear_object (&auth->goa_object);

	g_clear_object (&auth->settings);

	G_OBJECT_CLASS (gs_auth_parent_class)->finalize (object);
}

static void
gs_auth_class_init (GsAuthClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_auth_finalize;

	signals [SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void
gs_auth_init (GsAuth *auth)
{
	auth->settings = g_settings_new ("org.gnome.software");
	g_signal_connect (auth->settings, "changed::account-id",
			  G_CALLBACK (gs_auth_settings_changed), auth);
}

/**
 * gs_auth_new:
 * @provider_id: a provider ID used for mapping, e.g. "GnomeSSO"
 *
 * Return value: a new #GsAuth object.
 **/
GsAuth *
gs_auth_new (const gchar *provider_id,
	     const gchar *provider_type)
{
	GsAuth *auth;

	g_return_val_if_fail (provider_id != NULL, NULL);
	g_return_val_if_fail (provider_type != NULL, NULL);

	auth = g_object_new (GS_TYPE_AUTH, NULL);
	auth->provider_id = g_strdup (provider_id);
	auth->provider_type = g_strdup (provider_type);
	return GS_AUTH (auth);
}

/* vim: set noexpandtab: */
