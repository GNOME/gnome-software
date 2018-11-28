/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017-2018 Kalev Lember <klember@redhat.com>
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
#include <ostree.h>
#include <rpmostree.h>

#include "gs-rpmostree-generated.h"

/* This shows up in the `rpm-ostree status` as the software that
 * initiated the update.
 */
#define GS_RPMOSTREE_CLIENT_ID PACKAGE_NAME

struct GsPluginData {
	GsRPMOSTreeOS		*os_proxy;
	GsRPMOSTreeSysroot	*sysroot_proxy;
	OstreeRepo		*ot_repo;
	OstreeSysroot		*ot_sysroot;
	gboolean		 update_triggered;
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
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-local");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-offline");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-proxy");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-refine");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-refine-repos");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-refresh");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-upgrade");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "packagekit-url-to-app");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "repos");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "systemd-updates");

	/* need pkgname */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (priv->os_proxy != NULL)
		g_object_unref (priv->os_proxy);
	if (priv->sysroot_proxy != NULL)
		g_object_unref (priv->sysroot_proxy);
	if (priv->ot_sysroot != NULL)
		g_object_unref (priv->ot_sysroot);
	if (priv->ot_repo != NULL)
		g_object_unref (priv->ot_repo);
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GVariantBuilder) options_builder = NULL;

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

	options_builder = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_add (options_builder, "{sv}", "id",
			       g_variant_new_string (GS_RPMOSTREE_CLIENT_ID));
	/* Register as a client so that the rpm-ostree daemon doesn't exit */
	if (!gs_rpmostree_sysroot_call_register_client_sync (priv->sysroot_proxy,
	                                                     g_variant_builder_end (options_builder),
	                                                     cancellable,
	                                                     error)) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}

	/* Load ostree sysroot and repo */
	if (priv->ot_sysroot == NULL) {
		g_autofree gchar *sysroot_path = NULL;
		g_autoptr(GFile) sysroot_file = NULL;

		sysroot_path = gs_rpmostree_sysroot_dup_path (priv->sysroot_proxy);
		sysroot_file = g_file_new_for_path (sysroot_path);

		priv->ot_sysroot = ostree_sysroot_new (sysroot_file);
		if (!ostree_sysroot_load (priv->ot_sysroot, cancellable, error)) {
			gs_utils_error_convert_gio (error);
			return FALSE;
		}

		if (!ostree_sysroot_get_repo (priv->ot_sysroot, &priv->ot_repo, cancellable, error)) {
			gs_utils_error_convert_gio (error);
			return FALSE;
		}
	}

	return TRUE;
}

void
gs_plugin_adopt_app (GsPlugin *plugin, GsApp *app)
{
	if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_PACKAGE &&
	    gs_app_get_scope (app) == AS_APP_SCOPE_SYSTEM) {
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	}

	if (gs_app_get_kind (app) == AS_APP_KIND_OS_UPGRADE) {
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	}
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
	g_clear_error (&self->error);
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
			g_propagate_error (error, g_steal_pointer (&tp->error));
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

static GsApp *
app_from_modified_pkg_variant (GsPlugin *plugin, GVariant *variant)
{
	g_autoptr(GsApp) app = NULL;
	const char *name;
	const char *old_evr, *old_arch;
	const char *new_evr, *new_arch;
	g_autofree char *old_nevra = NULL;
	g_autofree char *new_nevra = NULL;

	g_variant_get (variant, "(us(ss)(ss))", NULL /* type*/, &name, &old_evr, &old_arch, &new_evr, &new_arch);
	old_nevra = g_strdup_printf ("%s-%s-%s", name, old_evr, old_arch);
	new_nevra = g_strdup_printf ("%s-%s-%s", name, new_evr, new_arch);

	app = gs_plugin_cache_lookup (plugin, old_nevra);
	if (app != NULL)
		return g_steal_pointer (&app);

	/* create new app */
	app = gs_app_new (NULL);
	gs_app_add_quirk (app, AS_APP_QUIRK_NEEDS_REBOOT);
	gs_app_set_management_plugin (app, "rpm-ostree");
	gs_app_set_size_download (app, 0);
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);

	/* update or downgrade */
	gs_app_add_source (app, name);
	gs_app_set_version (app, old_evr);
	gs_app_set_update_version (app, new_evr);
	gs_app_set_state (app, AS_APP_STATE_UPDATABLE);

	g_debug ("!%s\n", old_nevra);
	g_debug ("=%s\n", new_nevra);

	gs_plugin_cache_add (plugin, old_nevra, app);
	return g_steal_pointer (&app);
}

static GsApp *
app_from_single_pkg_variant (GsPlugin *plugin, GVariant *variant, gboolean addition)
{
	g_autoptr(GsApp) app = NULL;
	const char *name;
	const char *evr;
	const char *arch;
	g_autofree char *nevra = NULL;

	g_variant_get (variant, "(usss)", NULL /* type*/, &name, &evr, &arch);
	nevra = g_strdup_printf ("%s-%s-%s", name, evr, arch);

	app = gs_plugin_cache_lookup (plugin, nevra);
	if (app != NULL)
		return g_steal_pointer (&app);

	/* create new app */
	app = gs_app_new (NULL);
	gs_app_add_quirk (app, AS_APP_QUIRK_NEEDS_REBOOT);
	gs_app_set_management_plugin (app, "rpm-ostree");
	gs_app_set_size_download (app, 0);
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);

	if (addition) {
		/* addition */
		gs_app_add_source (app, name);
		gs_app_set_version (app, evr);
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);

		g_debug ("+%s\n", nevra);
	} else {
		/* removal */
		gs_app_add_source (app, name);
		gs_app_set_version (app, evr);
		gs_app_set_state (app, AS_APP_STATE_UNAVAILABLE);

		g_debug ("-%s\n", nevra);
	}

	gs_plugin_cache_add (plugin, nevra, app);
	return g_steal_pointer (&app);
}

static GVariant *
make_rpmostree_options_variant (gboolean reboot,
                                gboolean allow_downgrade,
                                gboolean cache_only,
                                gboolean download_only,
                                gboolean skip_purge,
                                gboolean no_pull_base,
                                gboolean dry_run,
                                gboolean no_overrides)
{
	GVariantDict dict;
	g_variant_dict_init (&dict, NULL);
	g_variant_dict_insert (&dict, "reboot", "b", reboot);
	g_variant_dict_insert (&dict, "allow-downgrade", "b", allow_downgrade);
	g_variant_dict_insert (&dict, "cache-only", "b", cache_only);
	g_variant_dict_insert (&dict, "download-only", "b", download_only);
	g_variant_dict_insert (&dict, "skip-purge", "b", skip_purge);
	g_variant_dict_insert (&dict, "no-pull-base", "b", no_pull_base);
	g_variant_dict_insert (&dict, "dry-run", "b", dry_run);
	g_variant_dict_insert (&dict, "no-overrides", "b", no_overrides);
	return g_variant_ref_sink (g_variant_dict_end (&dict));
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
                   guint cache_age,
                   GCancellable *cancellable,
                   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	if (cache_age == G_MAXUINT)
		return TRUE;

	{
		g_autofree gchar *transaction_address = NULL;
		g_autoptr(GVariant) options = NULL;

		options = make_rpmostree_options_variant (FALSE,  /* reboot */
		                                          FALSE,  /* allow-downgrade */
		                                          FALSE,  /* cache-only */
		                                          TRUE,   /* download-only */
		                                          FALSE,  /* skip-purge */
		                                          FALSE,  /* no-pull-base */
		                                          FALSE,  /* dry-run */
		                                          FALSE); /* no-overrides */
		if (!gs_rpmostree_os_call_upgrade_sync (priv->os_proxy,
		                                        options,
		                                        NULL /* fd list */,
		                                        &transaction_address,
		                                        NULL /* fd list out */,
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

	{
		g_autofree gchar *transaction_address = NULL;
		g_autoptr(GVariant) options = NULL;
		GVariantDict dict;

		g_variant_dict_init (&dict, NULL);
		g_variant_dict_insert (&dict, "mode", "s", "check");
		options = g_variant_ref_sink (g_variant_dict_end (&dict));

		if (!gs_rpmostree_os_call_automatic_update_trigger_sync (priv->os_proxy,
		                                                         options,
		                                                         NULL,
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

	/* update UI */
	gs_plugin_updates_changed (plugin);

	return TRUE;
}

gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GVariant) cached_update = NULL;
	g_autoptr(GVariant) rpm_diff = NULL;
	const gchar *checksum = NULL;
	const gchar *version = NULL;
	g_auto(GVariantDict) cached_update_dict;

	/* ensure D-Bus properties are updated before reading them */
	gs_rpmostree_sysroot_call_reload_sync (priv->sysroot_proxy, cancellable, error);

	cached_update = gs_rpmostree_os_dup_cached_update (priv->os_proxy);
	g_variant_dict_init (&cached_update_dict, cached_update);

	if (!g_variant_dict_lookup (&cached_update_dict, "checksum", "&s", &checksum))
		return TRUE;
	if (!g_variant_dict_lookup (&cached_update_dict, "version", "&s", &version))
		return TRUE;

	g_debug ("got CachedUpdate version '%s', checksum '%s'", version, checksum);

	rpm_diff = g_variant_dict_lookup_value (&cached_update_dict, "rpm-diff", G_VARIANT_TYPE ("a{sv}"));
	if (rpm_diff != NULL) {
		GVariantIter iter;
		GVariant *child;
		g_autoptr(GVariant) upgraded = NULL;
		g_autoptr(GVariant) downgraded = NULL;
		g_autoptr(GVariant) removed = NULL;
		g_autoptr(GVariant) added = NULL;
		g_auto(GVariantDict) rpm_diff_dict;
		g_variant_dict_init (&rpm_diff_dict, rpm_diff);

		upgraded = g_variant_dict_lookup_value (&rpm_diff_dict, "upgraded", G_VARIANT_TYPE ("a(us(ss)(ss))"));
		if (upgraded == NULL) {
			g_set_error_literal (error,
			                     GS_PLUGIN_ERROR,
			                     GS_PLUGIN_ERROR_INVALID_FORMAT,
			                     "no 'upgraded' in rpm-diff dict");
			return FALSE;
		}
		downgraded = g_variant_dict_lookup_value (&rpm_diff_dict, "downgraded", G_VARIANT_TYPE ("a(us(ss)(ss))"));
		if (downgraded == NULL) {
			g_set_error_literal (error,
			                     GS_PLUGIN_ERROR,
			                     GS_PLUGIN_ERROR_INVALID_FORMAT,
			                     "no 'downgraded' in rpm-diff dict");
			return FALSE;
		}
		removed = g_variant_dict_lookup_value (&rpm_diff_dict, "removed", G_VARIANT_TYPE ("a(usss)"));
		if (removed == NULL) {
			g_set_error_literal (error,
			                     GS_PLUGIN_ERROR,
			                     GS_PLUGIN_ERROR_INVALID_FORMAT,
			                     "no 'removed' in rpm-diff dict");
			return FALSE;
		}
		added = g_variant_dict_lookup_value (&rpm_diff_dict, "added", G_VARIANT_TYPE ("a(usss)"));
		if (added == NULL) {
			g_set_error_literal (error,
			                     GS_PLUGIN_ERROR,
			                     GS_PLUGIN_ERROR_INVALID_FORMAT,
			                     "no 'added' in rpm-diff dict");
			return FALSE;
		}

		/* iterate over all upgraded packages and add them */
		g_variant_iter_init (&iter, upgraded);
		while ((child = g_variant_iter_next_value (&iter)) != NULL) {
			g_autoptr(GsApp) app = app_from_modified_pkg_variant (plugin, child);
			if (app != NULL)
				gs_app_list_add (list, app);
			g_variant_unref (child);
		}

		/* iterate over all downgraded packages and add them */
		g_variant_iter_init (&iter, downgraded);
		while ((child = g_variant_iter_next_value (&iter)) != NULL) {
			g_autoptr(GsApp) app = app_from_modified_pkg_variant (plugin, child);
			if (app != NULL)
				gs_app_list_add (list, app);
			g_variant_unref (child);
		}

		/* iterate over all removed packages and add them */
		g_variant_iter_init (&iter, removed);
		while ((child = g_variant_iter_next_value (&iter)) != NULL) {
			g_autoptr(GsApp) app = app_from_single_pkg_variant (plugin, child, FALSE);
			if (app != NULL)
				gs_app_list_add (list, app);
			g_variant_unref (child);
		}

		/* iterate over all added packages and add them */
		g_variant_iter_init (&iter, added);
		while ((child = g_variant_iter_next_value (&iter)) != NULL) {
			g_autoptr(GsApp) app = app_from_single_pkg_variant (plugin, child, TRUE);
			if (app != NULL)
				gs_app_list_add (list, app);
			g_variant_unref (child);
		}
	}

	return TRUE;
}

static gboolean
trigger_rpmostree_update (GsPlugin *plugin,
                          GsApp *app,
                          GCancellable *cancellable,
                          GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autofree gchar *transaction_address = NULL;
	g_autoptr(GVariant) options = NULL;

	/* if we can process this online do not require a trigger */
	if (gs_app_get_state (app) != AS_APP_STATE_UPDATABLE)
		return TRUE;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* already in correct state */
	if (priv->update_triggered)
		return TRUE;

	/* trigger the update */
	options = make_rpmostree_options_variant (FALSE,  /* reboot */
	                                          FALSE,  /* allow-downgrade */
	                                          TRUE,   /* cache-only */
	                                          FALSE,  /* download-only */
	                                          FALSE,  /* skip-purge */
	                                          FALSE,  /* no-pull-base */
	                                          FALSE,  /* dry-run */
	                                          FALSE); /* no-overrides */
	if (!gs_rpmostree_os_call_upgrade_sync (priv->os_proxy,
	                                        options,
	                                        NULL /* fd list */,
	                                        &transaction_address,
	                                        NULL /* fd list out */,
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

	priv->update_triggered = TRUE;

	/* success */
	return TRUE;
}

gboolean
gs_plugin_update_app (GsPlugin *plugin,
                      GsApp *app,
                      GCancellable *cancellable,
                      GError **error)
{
	GsAppList *related = gs_app_get_related (app);

	/* we don't currently don't put all updates in the OsUpdate proxy app */
	if (!gs_app_has_quirk (app, AS_APP_QUIRK_IS_PROXY))
		return trigger_rpmostree_update (plugin, app, cancellable, error);

	/* try to trigger each related app */
	for (guint i = 0; i < gs_app_list_length (related); i++) {
		GsApp *app_tmp = gs_app_list_index (related, i);
		if (!trigger_rpmostree_update (plugin, app_tmp, cancellable, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
gs_plugin_app_remove (GsPlugin *plugin,
                      GsApp *app,
                      GCancellable *cancellable,
                      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autofree gchar *transaction_address = NULL;
	g_autoptr(GVariant) options = NULL;
	g_auto(GStrv) packages_to_remove = NULL;
	char *strv_empty[] = { NULL };

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	gs_app_set_state (app, AS_APP_STATE_REMOVING);

	options = make_rpmostree_options_variant (FALSE,  /* reboot */
	                                          FALSE,  /* allow-downgrade */
	                                          TRUE,   /* cache-only */
	                                          FALSE,  /* download-only */
	                                          FALSE,  /* skip-purge */
	                                          TRUE,  /* no-pull-base */
	                                          FALSE,  /* dry-run */
	                                          FALSE); /* no-overrides */

	packages_to_remove = g_new0 (gchar *, 2);
	packages_to_remove[0] = g_strdup (gs_app_get_source_default (app));

	if (!gs_rpmostree_os_call_pkg_change_sync (priv->os_proxy,
	                                           options,
	                                           (const gchar * const*)strv_empty /* packages to add */,
	                                           (const gchar * const*)packages_to_remove,
	                                           NULL /* fd list */,
	                                           &transaction_address,
	                                           NULL /* fd list out */,
	                                           cancellable,
	                                           error)) {
		gs_utils_error_convert_gio (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	if (!gs_rpmostree_transaction_get_response_sync (priv->sysroot_proxy,
	                                                 transaction_address,
	                                                 cancellable,
	                                                 error)) {
		gs_utils_error_convert_gio (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* state is not known: we don't know if we can re-install this app */
	gs_app_set_state (app, AS_APP_STATE_UNKNOWN);

	return TRUE;
}

static void
resolve_packages_app (GsPlugin *plugin,
                      GPtrArray *pkglist,
                      gchar **layered_packages,
                      GsApp *app)
{
	for (guint i = 0; i < pkglist->len; i++) {
		RpmOstreePackage *pkg = g_ptr_array_index (pkglist, i);
		if (g_strcmp0 (rpm_ostree_package_get_name (pkg), gs_app_get_source_default (app)) == 0) {
			gs_app_set_version (app, rpm_ostree_package_get_evr (pkg));
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
			if (!g_strv_contains ((const gchar * const *) layered_packages,
			                      rpm_ostree_package_get_name (pkg))) {
				/* on rpm-ostree this package cannot be removed 'live' */
				gs_app_add_quirk (app, AS_APP_QUIRK_COMPULSORY);
			}
		}
	}
}

gboolean
gs_plugin_refine (GsPlugin *plugin,
                  GsAppList *list,
                  GsPluginRefineFlags flags,
                  GCancellable *cancellable,
                  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GPtrArray) pkglist = NULL;
	g_autoptr(GVariant) booted_deployment = NULL;
	g_auto(GStrv) layered_packages = NULL;
	g_autofree gchar *checksum = NULL;

	/* ensure D-Bus properties are updated before reading them */
	gs_rpmostree_sysroot_call_reload_sync (priv->sysroot_proxy, cancellable, error);

	booted_deployment = gs_rpmostree_os_dup_booted_deployment (priv->os_proxy);
	g_assert (g_variant_lookup (booted_deployment,
	                            "packages", "^as",
	                            &layered_packages));
	g_assert (g_variant_lookup (booted_deployment,
	                            "checksum", "s",
	                            &checksum));

	pkglist = rpm_ostree_db_query_all (priv->ot_repo, checksum, cancellable, error);
	if (pkglist == NULL) {
		gs_utils_error_convert_gio (error);
		return FALSE;
	}

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		GPtrArray *sources;
		if (gs_app_has_quirk (app, AS_APP_QUIRK_MATCH_ANY_PREFIX))
			continue;
		if (gs_app_get_kind (app) == AS_APP_KIND_WEB_APP)
			continue;
		if (g_strcmp0 (gs_app_get_management_plugin (app), "rpm-ostree") != 0)
			continue;
		sources = gs_app_get_sources (app);
		if (sources->len == 0)
			continue;

		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			resolve_packages_app (plugin, pkglist, layered_packages, app);
	}

	return TRUE;
}

gboolean
gs_plugin_app_upgrade_download (GsPlugin *plugin,
                                GsApp *app,
                                GCancellable *cancellable,
                                GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const char *packages[] = { NULL };
	g_autofree gchar *new_refspec = NULL;
	g_autofree gchar *transaction_address = NULL;
	g_autoptr(GVariant) options = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* check is distro-upgrade */
	if (gs_app_get_kind (app) != AS_APP_KIND_OS_UPGRADE)
		return TRUE;

	/* construct new refspec based on the distro version we're upgrading to */
	new_refspec = g_strdup_printf ("ostree://fedora/%s/x86_64/silverblue",
	                               gs_app_get_version (app));

	options = make_rpmostree_options_variant (FALSE,  /* reboot */
	                                          FALSE,  /* allow-downgrade */
	                                          FALSE,  /* cache-only */
	                                          TRUE,   /* download-only */
	                                          FALSE,  /* skip-purge */
	                                          FALSE,  /* no-pull-base */
	                                          FALSE,  /* dry-run */
	                                          FALSE); /* no-overrides */

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	if (!gs_rpmostree_os_call_rebase_sync (priv->os_proxy,
	                                       options,
	                                       new_refspec,
	                                       packages,
	                                       NULL /* fd list */,
	                                       &transaction_address,
	                                       NULL /* fd list out */,
	                                       cancellable,
	                                       error)) {
		gs_utils_error_convert_gio (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	if (!gs_rpmostree_transaction_get_response_sync (priv->sysroot_proxy,
	                                                 transaction_address,
	                                                 cancellable,
	                                                 error)) {
		gs_utils_error_convert_gio (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* state is known */
	gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
	return TRUE;
}

gboolean
gs_plugin_launch (GsPlugin *plugin,
                  GsApp *app,
                  GCancellable *cancellable,
                  GError **error)
{
	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
	               gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	return gs_plugin_app_launch (plugin, app, error);
}
