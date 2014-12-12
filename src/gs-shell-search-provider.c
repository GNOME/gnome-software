/*
 * gs-shell-search-provider.c - Implementation of a GNOME Shell
 *   search provider
 *
 * Copyright (C) 2013 Matthias Clasen
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

#include <gio/gio.h>
#include <string.h>
#include <glib/gi18n.h>

#include "gs-plugin-loader-sync.h"

#include "gs-cleanup.h"
#include "gs-shell-search-provider-generated.h"
#include "gs-shell-search-provider.h"

typedef struct {
	GsShellSearchProvider *provider;
	GDBusMethodInvocation *invocation;
} PendingSearch;

struct _GsShellSearchProvider {
	GObject parent;

	guint name_owner_id;
	GDBusObjectManagerServer *object_manager;
	GsShellSearchProvider2 *skeleton;
	GsPluginLoader *plugin_loader;
	GCancellable *cancellable;

	GHashTable *metas_cache;
};

G_DEFINE_TYPE (GsShellSearchProvider, gs_shell_search_provider, G_TYPE_OBJECT)

static void
pending_search_free (PendingSearch *search)
{
	g_object_unref (search->invocation);
	g_slice_free (PendingSearch, search);
}

/**
 * search_sort_by_kudo_cb:
 **/
static gint
search_sort_by_kudo_cb (gconstpointer a, gconstpointer b)
{
	guint pa, pb;
	pa = gs_app_get_kudos_percentage (GS_APP (a));
	pb = gs_app_get_kudos_percentage (GS_APP (b));
	if (pa < pb)
		return 1;
	else if (pa > pb)
		return -1;
	return 0;
}

static void
search_done_cb (GObject *source,
		GAsyncResult *res,
		gpointer user_data)
{
	PendingSearch *search = user_data;
	GsShellSearchProvider *self = search->provider;
	GList *list, *l;
	GVariantBuilder builder;

	list = gs_plugin_loader_search_finish (self->plugin_loader, res, NULL);
	if (list == NULL) {
		g_dbus_method_invocation_return_value (search->invocation, g_variant_new ("(as)", NULL));
		pending_search_free (search);
		return;	
	}

	/* sort by kudos, as there is no ratings data by default */
	list = g_list_sort (list, search_sort_by_kudo_cb);

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("as"));
	for (l = list; l != NULL; l = l->next) {
		GsApp *app = GS_APP (l->data);
		if (gs_app_get_state (app) != AS_APP_STATE_AVAILABLE)
			continue;
		g_variant_builder_add (&builder, "s", gs_app_get_id (app));
	}
	g_dbus_method_invocation_return_value (search->invocation, g_variant_new ("(as)", &builder));

	g_list_free_full (list, g_object_unref);
	pending_search_free (search);
}

static void
execute_search (GsShellSearchProvider  *self,
		GDBusMethodInvocation  *invocation,
		gchar		 **terms)
{
	PendingSearch *pending_search;
	_cleanup_free_ gchar *string = NULL;

	string = g_strjoinv (" ", terms);

	if (self->cancellable != NULL) {
		g_cancellable_cancel (self->cancellable);
		g_clear_object (&self->cancellable);
	}

	/* don't attempt searches for a single character */
	if (g_strv_length (terms) == 1 &&
	    g_utf8_strlen (terms[0], -1) == 1) {
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(as)", NULL));
		return;
	}

	pending_search = g_slice_new (PendingSearch);
	pending_search->provider = self;
	pending_search->invocation = g_object_ref (invocation);

	self->cancellable = g_cancellable_new ();
	gs_plugin_loader_search_async (self->plugin_loader,
				       string, 0, self->cancellable,
				       search_done_cb,
				       pending_search);
}

static gboolean
handle_get_initial_result_set (GsShellSearchProvider2	*skeleton,
			       GDBusMethodInvocation	 *invocation,
			       gchar			**terms,
			       gpointer		       user_data)
{
	GsShellSearchProvider *self = user_data;

	g_debug ("****** GetInitialResultSet");
	execute_search (self, invocation, terms);
	return TRUE;
}

static gboolean
handle_get_subsearch_result_set (GsShellSearchProvider2	*skeleton,
				 GDBusMethodInvocation	 *invocation,
				 gchar			**previous_results,
				 gchar			**terms,
				 gpointer		       user_data)
{
	GsShellSearchProvider *self = user_data;

	g_debug ("****** GetSubSearchResultSet");
	execute_search (self, invocation, terms);
	return TRUE;
}

static gboolean
handle_get_result_metas (GsShellSearchProvider2	*skeleton,
			 GDBusMethodInvocation	 *invocation,
			 gchar			**results,
			 gpointer		       user_data)
{
	GsShellSearchProvider *self = user_data;
	GVariantBuilder meta;
	GVariant *meta_variant;
	GdkPixbuf *pixbuf;
	gint i;
	GVariantBuilder builder;
	GError *error = NULL;

	g_debug ("****** GetResultMetas");

	for (i = 0; results[i]; i++) {
		_cleanup_object_unref_ GsApp *app = NULL;

		if (g_hash_table_lookup (self->metas_cache, results[i]))
			continue;

		/* find the application with this ID */
		app = gs_plugin_loader_get_app_by_id (self->plugin_loader,
						      results[i],
						      GS_PLUGIN_REFINE_FLAGS_DEFAULT |
						      GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION,
						      NULL,
						      &error);
		if (app == NULL) {
			g_warning ("failed to refine %s: %s",
				   results[i], error->message);
			g_clear_error (&error);
			continue;
		}

		g_variant_builder_init (&meta, G_VARIANT_TYPE ("a{sv}"));
		g_variant_builder_add (&meta, "{sv}", "id", g_variant_new_string (gs_app_get_id (app)));
		g_variant_builder_add (&meta, "{sv}", "name", g_variant_new_string (gs_app_get_name (app)));
		pixbuf = gs_app_get_pixbuf (app);
		if (pixbuf != NULL)
			g_variant_builder_add (&meta, "{sv}", "icon", g_icon_serialize (G_ICON (pixbuf)));
		g_variant_builder_add (&meta, "{sv}", "description", g_variant_new_string (gs_app_get_summary (app)));
		meta_variant = g_variant_builder_end (&meta);
		g_hash_table_insert (self->metas_cache, g_strdup (gs_app_get_id (app)), g_variant_ref_sink (meta_variant));

	}

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("aa{sv}"));
	for (i = 0; results[i]; i++) {
		meta_variant = (GVariant*)g_hash_table_lookup (self->metas_cache, results[i]);
		if (meta_variant == NULL)
			continue;
		g_variant_builder_add_value (&builder, meta_variant);
	}

	g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", &builder));

	return TRUE;
}

static gboolean
handle_activate_result (GsShellSearchProvider2 	     *skeleton,
			GDBusMethodInvocation	*invocation,
			gchar			*result,
			gchar		       **terms,
			guint32		       timestamp,
			gpointer		      user_data)
{
	GApplication *app = g_application_get_default ();
	_cleanup_free_ gchar *string = NULL;

	string = g_strjoinv (" ", terms);

	g_action_group_activate_action (G_ACTION_GROUP (app), "details",
				  	g_variant_new ("(ss)", result, string));

	gs_shell_search_provider2_complete_activate_result (skeleton, invocation);
	return TRUE;
}

static gboolean
handle_launch_search (GsShellSearchProvider2 	   *skeleton,
		      GDBusMethodInvocation	*invocation,
		      gchar		       **terms,
		      guint32		       timestamp,
		      gpointer		      user_data)
{
	GApplication *app = g_application_get_default ();
	_cleanup_free_ gchar *string = g_strjoinv (" ", terms);

	g_action_group_activate_action (G_ACTION_GROUP (app), "search",
				  	g_variant_new ("s", string));

	gs_shell_search_provider2_complete_launch_search (skeleton, invocation);
	return TRUE;
}

static void
search_provider_name_acquired_cb (GDBusConnection *connection,
				  const gchar     *name,
				  gpointer	 user_data)
{
	g_debug ("Search provider name acquired: %s", name);
}

static void
search_provider_name_lost_cb (GDBusConnection *connection,
			      const gchar     *name,
			      gpointer	 user_data)
{
	g_debug ("Search provider name lost: %s", name);
}

static void
search_provider_bus_acquired_cb (GDBusConnection *connection,
				 const gchar *name,
				 gpointer user_data)
{
	GsShellSearchProvider *self = user_data;

	self->object_manager = g_dbus_object_manager_server_new ("/org/gnome/Software/SearchProvider");
	self->skeleton = gs_shell_search_provider2_skeleton_new ();

	g_signal_connect (self->skeleton, "handle-get-initial-result-set",
			G_CALLBACK (handle_get_initial_result_set), self);
	g_signal_connect (self->skeleton, "handle-get-subsearch-result-set",
			G_CALLBACK (handle_get_subsearch_result_set), self);
	g_signal_connect (self->skeleton, "handle-get-result-metas",
			G_CALLBACK (handle_get_result_metas), self);
	g_signal_connect (self->skeleton, "handle-activate-result",
			G_CALLBACK (handle_activate_result), self);
	g_signal_connect (self->skeleton, "handle-launch-search",
			G_CALLBACK (handle_launch_search), self);

	g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self->skeleton),
					connection,
					"/org/gnome/Software/SearchProvider", NULL);
	g_dbus_object_manager_server_set_connection (self->object_manager, connection);
}

static void
search_provider_dispose (GObject *obj)
{
	GsShellSearchProvider *self = GS_SHELL_SEARCH_PROVIDER (obj);

	if (self->name_owner_id != 0) {
		g_bus_unown_name (self->name_owner_id);
		self->name_owner_id = 0;
	}

	if (self->skeleton != NULL) {
		g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self->skeleton));
		g_clear_object (&self->skeleton);
	}

	if (self->cancellable != NULL) {
		g_cancellable_cancel (self->cancellable);
		g_clear_object (&self->cancellable);
	}

	g_clear_object (&self->object_manager);
	g_clear_object (&self->plugin_loader);
	g_clear_object (&self->cancellable);
	g_hash_table_destroy (self->metas_cache);
	g_application_release (g_application_get_default ());

	G_OBJECT_CLASS (gs_shell_search_provider_parent_class)->dispose (obj);
}

static void
gs_shell_search_provider_init (GsShellSearchProvider *self)
{
	self->metas_cache = g_hash_table_new_full (g_str_hash, g_str_equal,
						   g_free, (GDestroyNotify) g_variant_unref);

	g_application_hold (g_application_get_default ());
	self->name_owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
					"org.gnome.Software.SearchProvider",
					G_BUS_NAME_OWNER_FLAGS_NONE,
					search_provider_bus_acquired_cb,
					search_provider_name_acquired_cb,
					search_provider_name_lost_cb,
					self, NULL);
}

static void
gs_shell_search_provider_class_init (GsShellSearchProviderClass *klass)
{
	GObjectClass *oclass = G_OBJECT_CLASS (klass);

	oclass->dispose = search_provider_dispose;
}

GsShellSearchProvider *
gs_shell_search_provider_new (void)
{
	return g_object_new (gs_shell_search_provider_get_type (), NULL);
}

void
gs_shell_search_provider_setup (GsShellSearchProvider *provider,
				GsPluginLoader *loader)
{
	provider->plugin_loader = g_object_ref (loader);
}
