/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Kalev Lember <klember@redhat.com>
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
#include <glib/gstdio.h>

#include <gnome-software.h>

#include "gs-rpmostree-generated.h"

struct GsPluginData {
	GsRPMOSTreeOS		*os_proxy;
	GsRPMOSTreeSysroot	*sysroot_proxy;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	/* only works on OSTree */
	if (!g_file_test ("/run/ostree-booted", G_FILE_TEST_EXISTS)) {
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}

	/* rpm-ostree is already a daemon with a DBus API; hence it makes
	 * more sense to use a custom plugin instead of using PackageKit.
	 */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-history");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-offline");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-origin");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-proxy");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-local");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-refine");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-refresh");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-upgrade");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "systemd-updates");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (priv->os_proxy != NULL)
		g_object_unref (priv->os_proxy);
	if (priv->sysroot_proxy != NULL)
		g_object_unref (priv->sysroot_proxy);
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* Create a proxy for sysroot */
	if (priv->sysroot_proxy == NULL) {
		priv->sysroot_proxy = gs_rpmostree_sysroot_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
		                                                                   G_DBUS_PROXY_FLAGS_NONE,
		                                                                   "org.projectatomic.rpmostree1",
		                                                                   "/org/projectatomic/rpmostree1/Sysroot",
		                                                                   cancellable,
		                                                                   error);
		if (priv->sysroot_proxy == NULL) {
			gs_utils_error_convert_gio (error);
			return FALSE;
		}
	}

	/* Create a proxy for currently booted OS */
	if (priv->os_proxy == NULL) {
		g_autofree gchar *os_object_path = NULL;

		os_object_path = gs_rpmostree_sysroot_dup_booted (priv->sysroot_proxy);
		if (os_object_path == NULL &&
		    !gs_rpmostree_sysroot_call_get_os_sync (priv->sysroot_proxy,
		                                            "",
		                                            &os_object_path,
		                                            cancellable,
		                                            error)) {
			gs_utils_error_convert_gio (error);
			return FALSE;
		}

		priv->os_proxy = gs_rpmostree_os_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
		                                                         G_DBUS_PROXY_FLAGS_NONE,
		                                                         "org.projectatomic.rpmostree1",
		                                                         os_object_path,
		                                                         cancellable,
		                                                         error);
		if (priv->os_proxy == NULL) {
			gs_utils_error_convert_gio (error);
			return FALSE;
		}
	}

	return TRUE;
}

typedef struct {
	GsPlugin *plugin;
	GError *error;
	GMainLoop *loop;
	gboolean complete;
} TransactionProgress;

static TransactionProgress *
transaction_progress_new (void)
{
	TransactionProgress *self;

	self = g_slice_new0 (TransactionProgress);
	self->loop = g_main_loop_new (NULL, FALSE);

	return self;
}

static void
transaction_progress_free (TransactionProgress *self)
{
	g_main_loop_unref (self->loop);
	g_slice_free (TransactionProgress, self);
}

static void
transaction_progress_end (TransactionProgress *self)
{
	g_main_loop_quit (self->loop);
}

static void
on_transaction_progress (GDBusProxy *proxy,
                         gchar *sender_name,
                         gchar *signal_name,
                         GVariant *parameters,
                         gpointer user_data)
{
	TransactionProgress *tp = user_data;

	if (g_strcmp0 (signal_name, "Finished") == 0) {
		if (tp->error == NULL) {
			g_autofree gchar *error_message = NULL;
			gboolean success = FALSE;

			g_variant_get (parameters, "(bs)", &success, &error_message);

			if (!success) {
				tp->error = g_dbus_error_new_for_dbus_error ("org.projectatomic.rpmostreed.Error.Failed",
				                                             error_message);
			}
		}

		transaction_progress_end (tp);
	}
}

static void
cancelled_handler (GCancellable *cancellable,
                   gpointer user_data)
{
	GsRPMOSTreeTransaction *transaction = user_data;
	gs_rpmostree_transaction_call_cancel_sync (transaction, NULL, NULL);
}

static gboolean
gs_rpmostree_transaction_get_response_sync (GsRPMOSTreeSysroot *sysroot_proxy,
                                            const gchar *transaction_address,
                                            GCancellable *cancellable,
                                            GError **error)
{
	GsRPMOSTreeTransaction *transaction = NULL;
	g_autoptr(GDBusConnection) peer_connection = NULL;
	TransactionProgress *tp = transaction_progress_new ();
	gint cancel_handler;
	gulong signal_handler = 0;
	gboolean success = FALSE;
	gboolean just_started = FALSE;

	peer_connection = g_dbus_connection_new_for_address_sync (transaction_address,
	                                                          G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT,
	                                                          NULL,
	                                                          cancellable,
	                                                          error);

	if (peer_connection == NULL)
		goto out;

	transaction = gs_rpmostree_transaction_proxy_new_sync (peer_connection,
	                                                       G_DBUS_PROXY_FLAGS_NONE,
	                                                       NULL,
	                                                       "/",
	                                                       cancellable,
	                                                       error);
	if (transaction == NULL)
		goto out;

	/* setup cancel handler */
	cancel_handler = g_cancellable_connect (cancellable,
	                                        G_CALLBACK (cancelled_handler),
	                                        transaction, NULL);

	signal_handler = g_signal_connect (transaction, "g-signal",
	                                   G_CALLBACK (on_transaction_progress),
	                                   tp);

	/* Tell the server we're ready to receive signals. */
	if (!gs_rpmostree_transaction_call_start_sync (transaction,
	                                               &just_started,
	                                               cancellable,
	                                               error))
		goto out;

	g_main_loop_run (tp->loop);

	g_cancellable_disconnect (cancellable, cancel_handler);

	if (!g_cancellable_set_error_if_cancelled (cancellable, error)) {
		if (tp->error) {
			g_propagate_error (error, tp->error);
		} else {
			success = TRUE;
		}
	}

out:
	if (signal_handler)
		g_signal_handler_disconnect (transaction, signal_handler);
	if (transaction != NULL)
		g_object_unref (transaction);

	transaction_progress_free (tp);
	return success;
}

static gint
pkg_diff_variant_compare (gconstpointer a,
                          gconstpointer b,
                          gpointer unused)
{
	const char *pkg_name_a = NULL;
	const char *pkg_name_b = NULL;

	g_variant_get_child ((GVariant *) a, 0, "&s", &pkg_name_a);
	g_variant_get_child ((GVariant *) b, 0, "&s", &pkg_name_b);

	return g_strcmp0 (pkg_name_a, pkg_name_b);
}

static void
pkg_diff_variant_print (GVariant *variant)
{
	g_autoptr(GVariant) details = NULL;
	const char *old_name, *old_evr, *old_arch;
	const char *new_name, *new_evr, *new_arch;
	gboolean have_old = FALSE;
	gboolean have_new = FALSE;

	details = g_variant_get_child_value (variant, 2);
	g_return_if_fail (details != NULL);

	have_old = g_variant_lookup (details,
	                             "PreviousPackage", "(&s&s&s)",
	                             &old_name, &old_evr, &old_arch);

	have_new = g_variant_lookup (details,
	                             "NewPackage", "(&s&s&s)",
	                             &new_name, &new_evr, &new_arch);

	if (have_old && have_new) {
		g_print ("!%s-%s-%s\n", old_name, old_evr, old_arch);
		g_print ("=%s-%s-%s\n", new_name, new_evr, new_arch);
	} else if (have_old) {
		g_print ("-%s-%s-%s\n", old_name, old_evr, old_arch);
	} else if (have_new) {
		g_print ("+%s-%s-%s\n", new_name, new_evr, new_arch);
	}
}

static void
rpmostree_print_package_diffs (GVariant *variant)
{
	GQueue queue = G_QUEUE_INIT;
	GVariantIter iter;
	GVariant *child;

	/* GVariant format should be a(sua{sv}) */

	g_return_if_fail (variant != NULL);

	g_variant_iter_init (&iter, variant);

	/* Queue takes ownership of the child variant. */
	while ((child = g_variant_iter_next_value (&iter)) != NULL)
		g_queue_insert_sorted (&queue, child, pkg_diff_variant_compare, NULL);

	while (!g_queue_is_empty (&queue)) {
		child = g_queue_pop_head (&queue);
		pkg_diff_variant_print (child);
		g_variant_unref (child);
	}
}

static GsApp *
make_app (GVariant *variant)
{
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GVariant) details = NULL;
	const char *old_name, *old_evr, *old_arch;
	const char *new_name, *new_evr, *new_arch;
	gboolean have_old = FALSE;
	gboolean have_new = FALSE;

	app = gs_app_new (NULL);

	/* create new app */
	app = gs_app_new (NULL);
	gs_app_add_quirk (app, AS_APP_QUIRK_NEEDS_REBOOT);
	gs_app_set_management_plugin (app, "rpm-ostree");
	gs_app_set_size_download (app, 0);
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);

	details = g_variant_get_child_value (variant, 2);
	g_return_val_if_fail (details != NULL, NULL);

	have_old = g_variant_lookup (details,
	                             "PreviousPackage", "(&s&s&s)",
	                             &old_name, &old_evr, &old_arch);

	have_new = g_variant_lookup (details,
	                             "NewPackage", "(&s&s&s)",
	                             &new_name, &new_evr, &new_arch);

	if (have_old && have_new) {
		g_assert (g_strcmp0 (old_name, new_name) == 0);

		/* update */
		gs_app_add_source (app, old_name);
		gs_app_set_version (app, old_evr);
		gs_app_set_update_version (app, new_evr);
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE);

		g_print ("!%s-%s-%s\n", old_name, old_evr, old_arch);
		g_print ("=%s-%s-%s\n", new_name, new_evr, new_arch);
	} else if (have_old) {
		/* removal */
		gs_app_add_source (app, old_name);
		gs_app_set_version (app, old_evr);
		gs_app_set_state (app, AS_APP_STATE_UNAVAILABLE);

		g_print ("-%s-%s-%s\n", old_name, old_evr, old_arch);
	} else if (have_new) {
		/* install */
		gs_app_add_source (app, new_name);
		gs_app_set_version (app, new_evr);
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);

		g_print ("+%s-%s-%s\n", new_name, new_evr, new_arch);
	}

	return g_steal_pointer (&app);
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
                   guint cache_age,
                   GsPluginRefreshFlags flags,
                   GCancellable *cancellable,
                   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	if (flags & GS_PLUGIN_REFRESH_FLAGS_METADATA) {
		g_autofree gchar *transaction_address = NULL;

		if (!gs_rpmostree_os_call_download_update_rpm_diff_sync (priv->os_proxy,
		                                                         &transaction_address,
		                                                         cancellable,
		                                                         error)) {
			gs_utils_error_convert_gio (error);
			return FALSE;
		}

		if (!gs_rpmostree_transaction_get_response_sync (priv->sysroot_proxy,
		                                                 transaction_address,
		                                                 cancellable,
		                                                 error)) {
			gs_utils_error_convert_gio (error);
			return FALSE;
		}
	}

	return TRUE;
}

gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GVariant) result = NULL;
	g_autoptr(GVariant) details = NULL;
	GVariantIter iter;
	GVariant *child;

	if (!gs_rpmostree_os_call_get_cached_update_rpm_diff_sync (priv->os_proxy,
	                                                           "",
	                                                           &result,
	                                                           &details,
	                                                           cancellable,
	                                                           error)) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}

	if (g_variant_n_children (result) == 0)
		return TRUE;

	rpmostree_print_package_diffs (result);

	/* GVariant format should be a(sua{sv}) */
	g_variant_iter_init (&iter, result);

	while ((child = g_variant_iter_next_value (&iter)) != NULL) {
		g_autoptr(GsApp) app = make_app (child);
		if (app != NULL) {
			gs_app_list_add (list, app);
		}
		g_variant_unref (child);
	}

	return TRUE;
}
