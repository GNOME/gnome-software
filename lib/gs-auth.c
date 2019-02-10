/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Canonical Ltd
 *
 * SPDX-License-Identifier: GPL-2.0+
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

	gchar		*auth_id;
	gchar		*provider_name;
	gchar		*provider_type;

	GoaClient	*goa_client;
	GoaObject	*goa_object;

	GSettings	*settings;
};

static void gs_auth_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (GsAuth, gs_auth, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE(G_TYPE_INITABLE, gs_auth_initable_iface_init))

enum {
	SIGNAL_CHANGED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_AUTH_ID,
	PROP_PROVIDER_TYPE,
	PROP_GOA_OBJECT,
	PROP_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };


/**
 * gs_auth_get_header:
 * @auth: a #GsAuth
 * @n: the number of accounts
 *
 * Gets the header to be used in the authentication dialog in case there are @n
 * available accounts.
 *
 * Returns: (transfer none) : a string
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
 * @header_multiple: the header to be used if two or more accounts are present
 *
 * Sets the headers to be used for the authentication dialog.
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
	auth->header_none = g_strdup (header_none);

	g_free (auth->header_single);
	auth->header_single = g_strdup (header_single);

	g_free (auth->header_multiple);
	auth->header_multiple = g_strdup (header_multiple);
}

/**
 * gs_auth_get_auth_id:
 * @auth: a #GsAuth
 *
 * Gets the authentication service ID.
 *
 * Returns: (transfer none): a string
 */
const gchar *
gs_auth_get_auth_id (GsAuth *auth)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);
	return auth->auth_id;
}

/**
 * gs_auth_get_provider_name:
 * @auth: a #GsAuth
 *
 * Gets the authentication service name.
 *
 * Returns: (transfer none): a string
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
 * @provider_name: a service name, e.g. "Snap Store"
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
 * Gets the GoaProvider type to be used for the authentication dialog.
 *
 * Returns: (transfer none): a string
 */
const gchar *
gs_auth_get_provider_type (GsAuth *auth)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);
	return auth->provider_type;
}

/**
 * gs_auth_peek_goa_object:
 * @auth: a #GsAuth
 *
 * Gets the logged #GoaObject if any.
 *
 * Returns: (transfer none) (nullable): a #GoaObject, or %NULL
 */
GoaObject *
gs_auth_peek_goa_object (GsAuth *auth)
{
	g_return_val_if_fail (GS_IS_AUTH (auth), NULL);
	return auth->goa_object;
}

static gboolean
gs_auth_goa_account_equal (GoaAccount *acc1, GoaAccount *acc2)
{
	if (acc1 == acc2)
		return TRUE;

	if (acc1 == NULL || acc2 == NULL)
		return FALSE;

	return !g_strcmp0 (goa_account_get_id (acc1),
			   goa_account_get_id (acc2));
}

static gboolean
gs_auth_goa_object_equal (GoaObject *obj1, GoaObject *obj2)
{
	if (obj1 == obj2)
		return TRUE;

	if (obj1 == NULL || obj2 == NULL)
		return FALSE;

	return gs_auth_goa_account_equal(goa_object_peek_account (obj1),
					 goa_object_peek_account (obj2));
}

static void
gs_auth_account_changed_cb (GoaClient *client,
			    GoaObject *goa_object,
			    GsAuth *auth)
{
	if (!gs_auth_goa_object_equal (auth->goa_object, goa_object))
		return;

	g_signal_emit (auth, signals[SIGNAL_CHANGED], 0);
}

static void
gs_auth_account_removed_cb (GoaClient *client,
			    GoaObject *goa_object,
			    GsAuth *auth)
{
	if (!gs_auth_goa_object_equal (auth->goa_object, goa_object))
		return;

	gs_auth_set_goa_object (auth, NULL);
}

/**
 * gs_auth_set_goa_object:
 * @auth: a #GsAuth
 * @goa_object: (nullable) a #GoaObject
 *
 * Set the #GoaObject used to login in.
 */
void
gs_auth_set_goa_object (GsAuth *auth,
			GoaObject *goa_object)
{
	g_return_if_fail (GS_IS_AUTH (auth));

	if (gs_auth_goa_object_equal (auth->goa_object, goa_object))
		return;

	g_clear_object (&auth->goa_object);
	if (goa_object)
		auth->goa_object = g_object_ref (goa_object);

	g_object_notify (G_OBJECT (auth), "goa-object");
	g_signal_emit (auth, signals[SIGNAL_CHANGED], 0);
}

static gboolean
string_to_goa_object (GValue   *value,
		      GVariant *variant,
		      gpointer  user_data)
{
	GsAuth *auth = GS_AUTH (user_data);
	GoaObject *goa_object;
	const gchar *account_id;

	account_id = g_variant_get_string (variant, NULL);

	goa_object = goa_client_lookup_by_id (auth->goa_client, account_id);
	if (!goa_object)
		return TRUE;

	g_value_take_object (value, goa_object);
	return TRUE;
}

static GVariant *
goa_object_to_string (const GValue       *value,
		      const GVariantType *expected_type,
		      gpointer            user_data)
{
	GObject *object = g_value_get_object (value);

	GoaObject *goa_object = object != NULL ? GOA_OBJECT (object) : NULL;
	GoaAccount *goa_account = goa_object != NULL ? goa_object_peek_account (goa_object) : NULL;

	if (goa_account != NULL)
		return g_variant_new_string (goa_account_get_id (goa_account));
	else
		return g_variant_new_string ("");
}

/* GObject */

static void
gs_auth_init (GsAuth *auth)
{
}

static void
gs_auth_get_property (GObject *object, guint prop_id,
		      GValue *value, GParamSpec *pspec)
{
	GsAuth *auth = GS_AUTH (object);

	switch (prop_id) {
	case PROP_AUTH_ID:
		g_value_set_string (value, auth->auth_id);
		break;
	case PROP_PROVIDER_TYPE:
		g_value_set_string (value, auth->provider_type);
		break;
	case PROP_GOA_OBJECT:
		g_value_set_object (value, auth->goa_object);
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
	case PROP_AUTH_ID:
		auth->auth_id = g_value_dup_string (value);
		break;
	case PROP_PROVIDER_TYPE:
		auth->provider_type = g_value_dup_string (value);
		break;
	case PROP_GOA_OBJECT:
		gs_auth_set_goa_object (auth, g_value_get_object (value));
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

	g_free (auth->header_none);
	g_free (auth->header_single);
	g_free (auth->header_multiple);
	g_free (auth->auth_id);
	g_free (auth->provider_name);

	g_clear_object (&auth->goa_client);
	g_clear_object (&auth->goa_object);

	g_clear_object (&auth->settings);

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

	pspec = g_param_spec_string ("auth-id", NULL, NULL, NULL,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT_ONLY);
	g_object_class_install_property (object_class, PROP_AUTH_ID, pspec);

	pspec = g_param_spec_string ("provider-type", NULL, NULL, NULL,
				     G_PARAM_READWRITE |
				     G_PARAM_CONSTRUCT_ONLY);
	g_object_class_install_property (object_class, PROP_PROVIDER_TYPE, pspec);

	pspec = g_param_spec_object ("goa-object", NULL, NULL,
				     GOA_TYPE_OBJECT,
				     G_PARAM_READWRITE |
				     G_PARAM_EXPLICIT_NOTIFY);
	g_object_class_install_property (object_class, PROP_GOA_OBJECT, pspec);

	signals [SIGNAL_CHANGED] =
		g_signal_new ("changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 0);
}

/* GInitable */

static gboolean
gs_auth_initable_init (GInitable *initable,
		       GCancellable *cancellable,
		       GError  **error)
{
	GsAuth *self;
	g_autofree gchar *path = NULL;

	g_return_val_if_fail (GS_IS_AUTH (initable), FALSE);

	self = GS_AUTH (initable);

	self->goa_client = goa_client_new_sync (NULL, error);
	if (self->goa_client == NULL)
		return FALSE;

	g_signal_connect (self->goa_client, "account-changed",
			  G_CALLBACK (gs_auth_account_changed_cb), self);
	g_signal_connect (self->goa_client, "account-removed",
			  G_CALLBACK (gs_auth_account_removed_cb), self);

	path = g_strdup_printf ("/org/gnome/software/auth/%s/", self->auth_id);
	self->settings = g_settings_new_with_path ("org.gnome.software.auth", path);

	g_settings_bind_with_mapping (self->settings, "account-id",
				      self, "goa-object",
				      G_SETTINGS_BIND_DEFAULT,
				      string_to_goa_object,
				      goa_object_to_string,
				      self, NULL);

	return TRUE;
}

static void
gs_auth_initable_iface_init (GInitableIface *iface)
{
	iface->init = gs_auth_initable_init;
}

/**
 * gs_auth_new:
 * @auth_id: an identifier used for mapping, e.g. "snapd"
 * @provider_type: the name of the GoaProvider466 to be used, e.g. "ubuntusso"
 * @error: A #GError
 *
 * Return value: (transfer full) (nullable): a new #GsAuth object.
 **/
GsAuth *
gs_auth_new (const gchar *auth_id,
	     const gchar *provider_type,
	     GError **error)
{
	GsAuth *auth;

	g_return_val_if_fail (auth_id != NULL, NULL);
	g_return_val_if_fail (provider_type != NULL, NULL);

	auth = g_initable_new (GS_TYPE_AUTH, NULL, error,
			       "auth-id", auth_id,
			       "provider-type", provider_type,
			       NULL);

	return GS_AUTH (auth);
}
