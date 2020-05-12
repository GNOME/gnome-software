/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017-2020 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <gnome-software.h>

#include <fcntl.h>
#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib/gstdio.h>
#include <libdnf/libdnf.h>
#include <ostree.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmts.h>
#include <rpmostree.h>

#include "gs-rpmostree-generated.h"

/* This shows up in the `rpm-ostree status` as the software that
 * initiated the update.
 */
#define GS_RPMOSTREE_CLIENT_ID PACKAGE_NAME

G_DEFINE_AUTO_CLEANUP_FREE_FUNC(Header, headerFree, NULL)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(rpmts, rpmtsFree, NULL);
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(rpmdbMatchIterator, rpmdbFreeIterator, NULL);

struct GsPluginData {
	GMutex			 mutex;
	GsRPMOSTreeOS		*os_proxy;
	GsRPMOSTreeSysroot	*sysroot_proxy;
	OstreeRepo		*ot_repo;
	OstreeSysroot		*ot_sysroot;
	DnfContext		*dnf_context;
	gboolean		 update_triggered;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	/* only works on OSTree */
	if (!g_file_test ("/run/ostree-booted", G_FILE_TEST_EXISTS)) {
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}

	g_mutex_init (&priv->mutex);

	/* open transaction */
	rpmReadConfigFiles (NULL, NULL);

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
	if (priv->dnf_context != NULL)
		g_object_unref (priv->dnf_context);
	g_mutex_clear (&priv->mutex);
}

static void
gs_rpmostree_error_convert (GError **perror)
{
	GError *error = perror != NULL ? *perror : NULL;

	/* not set */
	if (error == NULL)
		return;

	/* parse remote RPM_OSTREED_ERROR */
	if (g_dbus_error_is_remote_error (error)) {
		g_autofree gchar *remote_error = g_dbus_error_get_remote_error (error);

		g_dbus_error_strip_remote_error (error);

		if (g_strcmp0 (remote_error, "org.projectatomic.rpmostreed.Error.NotAuthorized") == 0) {
			error->code = GS_PLUGIN_ERROR_NO_SECURITY;
		} else if (g_str_has_prefix (remote_error, "org.projectatomic.rpmostreed.Error")) {
			error->code = GS_PLUGIN_ERROR_FAILED;
		} else {
			g_warning ("can't reliably fixup remote error %s", remote_error);
			error->code = GS_PLUGIN_ERROR_FAILED;
		}
		error->domain = GS_PLUGIN_ERROR;
		return;
	}

	/* this are allowed for low-level errors */
	if (gs_utils_error_convert_gio (perror))
		return;

	/* this are allowed for low-level errors */
	if (gs_utils_error_convert_gdbus (perror))
		return;
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
			gs_rpmostree_error_convert (error);
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
			gs_rpmostree_error_convert (error);
			return FALSE;
		}

		priv->os_proxy = gs_rpmostree_os_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
		                                                         G_DBUS_PROXY_FLAGS_NONE,
		                                                         "org.projectatomic.rpmostree1",
		                                                         os_object_path,
		                                                         cancellable,
		                                                         error);
		if (priv->os_proxy == NULL) {
			gs_rpmostree_error_convert (error);
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
		gs_rpmostree_error_convert (error);
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
			gs_rpmostree_error_convert (error);
			return FALSE;
		}

		if (!ostree_sysroot_get_repo (priv->ot_sysroot, &priv->ot_repo, cancellable, error)) {
			gs_rpmostree_error_convert (error);
			return FALSE;
		}
	}

	return TRUE;
}

static void
app_set_rpm_ostree_packaging_format (GsApp *app)
{
	gs_app_set_metadata (app, "GnomeSoftware::PackagingFormat", "RPM");
}

void
gs_plugin_adopt_app (GsPlugin *plugin, GsApp *app)
{
	if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_PACKAGE &&
	    gs_app_get_scope (app) == AS_APP_SCOPE_SYSTEM) {
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
		gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
		app_set_rpm_ostree_packaging_format (app);
	}

	if (gs_app_get_kind (app) == AS_APP_KIND_OS_UPGRADE) {
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
		gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
	}
}

typedef struct {
	GsPlugin *plugin;
	GError *error;
	GMainLoop *loop;
	GsApp *app;
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
	g_clear_object (&self->plugin);
	g_clear_error (&self->error);
	g_main_loop_unref (self->loop);
	g_clear_object (&self->app);
	g_slice_free (TransactionProgress, self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(TransactionProgress, transaction_progress_free);

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

	if (g_strcmp0 (signal_name, "PercentProgress") == 0) {
		const gchar *message = NULL;
		guint32 percentage;

		g_variant_get_child (parameters, 0, "&s", &message);
		g_variant_get_child (parameters, 1, "u", &percentage);
		g_debug ("PercentProgress: %u, %s\n", percentage, message);

		if (tp->app != NULL)
			gs_app_set_progress (tp->app, (guint) percentage);

		if (tp->app != NULL && tp->plugin != NULL) {
			GsPluginStatus plugin_status;

			switch (gs_app_get_state (tp->app)) {
			case AS_APP_STATE_INSTALLING:
				plugin_status = GS_PLUGIN_STATUS_INSTALLING;
				break;
			case AS_APP_STATE_REMOVING:
				plugin_status = GS_PLUGIN_STATUS_REMOVING;
				break;
			default:
				plugin_status = GS_PLUGIN_STATUS_DOWNLOADING;
				break;
			}
			gs_plugin_status_update (tp->plugin, tp->app, plugin_status);
		}
	} else if (g_strcmp0 (signal_name, "Finished") == 0) {
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
                                            TransactionProgress *tp,
                                            GCancellable *cancellable,
                                            GError **error)
{
	GsRPMOSTreeTransaction *transaction = NULL;
	g_autoptr(GDBusConnection) peer_connection = NULL;
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
	gs_app_set_management_plugin (app, "rpm-ostree");
	gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
	app_set_rpm_ostree_packaging_format (app);
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
	gs_app_set_management_plugin (app, "rpm-ostree");
	gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
	app_set_rpm_ostree_packaging_format (app);
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

static GVariant *
make_refresh_md_options_variant (gboolean force)
{
	GVariantDict dict;
	g_variant_dict_init (&dict, NULL);
	g_variant_dict_insert (&dict, "force", "b", force);
	return g_variant_ref_sink (g_variant_dict_end (&dict));
}

static gboolean
make_rpmostree_modifiers_variant (const char *install_package,
                                  const char *uninstall_package,
                                  const char *install_local_package,
                                  GVariant **out_modifiers,
                                  GUnixFDList **out_fd_list,
                                  GError **error)
{
	GVariantDict dict;
	g_autoptr(GUnixFDList) fd_list = g_unix_fd_list_new ();

	g_variant_dict_init (&dict, NULL);

	if (install_package != NULL) {
		g_autoptr(GPtrArray) repo_pkgs = g_ptr_array_new ();

		g_ptr_array_add (repo_pkgs, install_package);

		g_variant_dict_insert_value (&dict, "install-packages",
		                             g_variant_new_strv ((const char *const*)repo_pkgs->pdata,
		                             repo_pkgs->len));

	}

	if (uninstall_package != NULL) {
		g_autoptr(GPtrArray) repo_pkgs = g_ptr_array_new ();

		g_ptr_array_add (repo_pkgs, uninstall_package);

		g_variant_dict_insert_value (&dict, "uninstall-packages",
		                             g_variant_new_strv ((const char *const*)repo_pkgs->pdata,
		                             repo_pkgs->len));

	}

	if (install_local_package != NULL) {
		g_auto(GVariantBuilder) builder;
		int fd;
		int idx;

		g_variant_builder_init (&builder, G_VARIANT_TYPE ("ah"));

		fd = openat (AT_FDCWD, install_local_package, O_RDONLY | O_CLOEXEC | O_NOCTTY);
		if (fd == -1) {
			g_set_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
			             "Failed to open %s", install_local_package);
			return FALSE;
		}

		idx = g_unix_fd_list_append (fd_list, fd, error);
		if (idx < 0) {
			close (fd);
			return FALSE;
		}

		g_variant_builder_add (&builder, "h", idx);
		g_variant_dict_insert_value (&dict, "install-local-packages",
		                             g_variant_new ("ah", &builder));
		close (fd);
	}

	*out_fd_list = g_steal_pointer (&fd_list);
	*out_modifiers = g_variant_ref_sink (g_variant_dict_end (&dict));
	return TRUE;
}

static gboolean
rpmostree_update_deployment (GsRPMOSTreeOS *os_proxy,
                             const char *install_package,
                             const char *uninstall_package,
                             const char *install_local_package,
                             GVariant *options,
                             char **out_transaction_address,
                             GCancellable *cancellable,
                             GError **error)
{
	g_autoptr(GUnixFDList) fd_list = NULL;
	g_autoptr(GVariant) modifiers = NULL;

	if (!make_rpmostree_modifiers_variant (install_package,
	                                       uninstall_package,
	                                       install_local_package,
	                                       &modifiers, &fd_list, error))
		return FALSE;

	return gs_rpmostree_os_call_update_deployment_sync (os_proxy,
	                                                    modifiers,
	                                                    options,
	                                                    fd_list,
	                                                    out_transaction_address,
	                                                    NULL,
	                                                    cancellable,
	                                                    error);
}

#define RPMOSTREE_CORE_CACHEDIR "/var/cache/rpm-ostree/"
#define RPMOSTREE_DIR_CACHE_REPOMD "repomd"
#define RPMOSTREE_DIR_CACHE_SOLV "solv"

static gboolean
ensure_rpmostree_dnf_context (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autofree gchar *transaction_address = NULL;
	g_autoptr(GsApp) progress_app = gs_app_new (gs_plugin_get_name (plugin));
	g_autoptr(DnfContext) context = dnf_context_new ();
	g_autoptr(DnfState) state = dnf_state_new ();
	g_autoptr(GVariant) options = NULL;
	g_autoptr(TransactionProgress) tp = transaction_progress_new ();

	if (priv->dnf_context != NULL)
		return TRUE;

	tp->app = g_object_ref (progress_app);
	tp->plugin = g_object_ref (plugin);

	dnf_context_set_repo_dir (context, "/etc/yum.repos.d");
	dnf_context_set_cache_dir (context, RPMOSTREE_CORE_CACHEDIR RPMOSTREE_DIR_CACHE_REPOMD);
	dnf_context_set_solv_dir (context, RPMOSTREE_CORE_CACHEDIR RPMOSTREE_DIR_CACHE_SOLV);
	dnf_context_set_cache_age (context, G_MAXUINT);
	dnf_context_set_enable_filelists (context, FALSE);

	options = make_refresh_md_options_variant (FALSE /* force */);
	if (!gs_rpmostree_os_call_refresh_md_sync (priv->os_proxy,
	                                           options,
	                                           &transaction_address,
	                                           cancellable,
	                                           error)) {
		gs_rpmostree_error_convert (error);
		return FALSE;
	}

	if (!gs_rpmostree_transaction_get_response_sync (priv->sysroot_proxy,
	                                                 transaction_address,
	                                                 tp,
	                                                 cancellable,
	                                                 error)) {
		gs_rpmostree_error_convert (error);
		return FALSE;
	}

	if (!dnf_context_setup (context, cancellable, error)) {
		gs_rpmostree_error_convert (error);
		return FALSE;
	}

	if (!dnf_context_setup_sack_with_flags (context, state, DNF_CONTEXT_SETUP_SACK_FLAG_SKIP_RPMDB, error)) {
		gs_rpmostree_error_convert (error);
		return FALSE;
	}

	g_set_object (&priv->dnf_context, context);
	return TRUE;
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
                   guint cache_age,
                   GCancellable *cancellable,
                   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GMutexLocker) locker = NULL;

	locker = g_mutex_locker_new (&priv->mutex);

	if (!ensure_rpmostree_dnf_context (plugin, cancellable, error))
		return FALSE;

	if (cache_age == G_MAXUINT)
		return TRUE;

	{
		g_autofree gchar *transaction_address = NULL;
		g_autoptr(GsApp) progress_app = gs_app_new (gs_plugin_get_name (plugin));
		g_autoptr(GVariant) options = NULL;
		g_autoptr(TransactionProgress) tp = transaction_progress_new ();

		tp->app = g_object_ref (progress_app);
		tp->plugin = g_object_ref (plugin);

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
			gs_rpmostree_error_convert (error);
			return FALSE;
		}

		if (!gs_rpmostree_transaction_get_response_sync (priv->sysroot_proxy,
		                                                 transaction_address,
		                                                 tp,
		                                                 cancellable,
		                                                 error)) {
			gs_rpmostree_error_convert (error);
			return FALSE;
		}
	}

	{
		g_autofree gchar *transaction_address = NULL;
		g_autoptr(GsApp) progress_app = gs_app_new (gs_plugin_get_name (plugin));
		g_autoptr(GVariant) options = NULL;
		GVariantDict dict;
		g_autoptr(TransactionProgress) tp = transaction_progress_new ();

		tp->app = g_object_ref (progress_app);
		tp->plugin = g_object_ref (plugin);

		g_variant_dict_init (&dict, NULL);
		g_variant_dict_insert (&dict, "mode", "s", "check");
		options = g_variant_ref_sink (g_variant_dict_end (&dict));

		if (!gs_rpmostree_os_call_automatic_update_trigger_sync (priv->os_proxy,
		                                                         options,
		                                                         NULL,
		                                                         &transaction_address,
		                                                         cancellable,
		                                                         error)) {
			gs_rpmostree_error_convert (error);
			return FALSE;
		}

		if (!gs_rpmostree_transaction_get_response_sync (priv->sysroot_proxy,
		                                                 transaction_address,
		                                                 tp,
		                                                 cancellable,
		                                                 error)) {
			gs_rpmostree_error_convert (error);
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
	if (!gs_rpmostree_sysroot_call_reload_sync (priv->sysroot_proxy, cancellable, error)) {
		gs_rpmostree_error_convert (error);
		return FALSE;
	}

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
	g_autoptr(TransactionProgress) tp = transaction_progress_new ();

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
		gs_rpmostree_error_convert (error);
		return FALSE;
	}

	if (!gs_rpmostree_transaction_get_response_sync (priv->sysroot_proxy,
	                                                 transaction_address,
	                                                 tp,
	                                                 cancellable,
	                                                 error)) {
		gs_rpmostree_error_convert (error);
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
	if (!gs_app_has_quirk (app, GS_APP_QUIRK_IS_PROXY))
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
gs_plugin_app_upgrade_trigger (GsPlugin *plugin,
                               GsApp *app,
                               GCancellable *cancellable,
                               GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const char *packages[] = { NULL };
	g_autofree gchar *new_refspec = NULL;
	g_autofree gchar *transaction_address = NULL;
	g_autoptr(GVariant) options = NULL;
	g_autoptr(TransactionProgress) tp = transaction_progress_new ();

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* check is distro-upgrade */
	if (gs_app_get_kind (app) != AS_APP_KIND_OS_UPGRADE)
		return TRUE;

	/* construct new refspec based on the distro version we're upgrading to */
	new_refspec = g_strdup_printf ("ostree://fedora/%s/x86_64/silverblue",
	                               gs_app_get_version (app));

	/* trigger the upgrade */
	options = make_rpmostree_options_variant (FALSE,  /* reboot */
	                                          TRUE,   /* allow-downgrade */
	                                          TRUE,   /* cache-only */
	                                          FALSE,  /* download-only */
	                                          FALSE,  /* skip-purge */
	                                          FALSE,  /* no-pull-base */
	                                          FALSE,  /* dry-run */
	                                          FALSE); /* no-overrides */

	if (!gs_rpmostree_os_call_rebase_sync (priv->os_proxy,
	                                       options,
	                                       new_refspec,
	                                       packages,
	                                       NULL /* fd list */,
	                                       &transaction_address,
	                                       NULL /* fd list out */,
	                                       cancellable,
	                                       error)) {
		gs_rpmostree_error_convert (error);
		return FALSE;
	}

	if (!gs_rpmostree_transaction_get_response_sync (priv->sysroot_proxy,
	                                                 transaction_address,
	                                                 tp,
	                                                 cancellable,
	                                                 error)) {
		gs_rpmostree_error_convert (error);

		if (g_strrstr ((*error)->message, "Old and new refs are equal")) {
			/* don't error out if the correct tree is already deployed */
			g_debug ("ignoring rpm-ostree error: %s", (*error)->message);
			g_clear_error (error);
		} else {
			return FALSE;
		}
	}

	/* success */
	return TRUE;
}

static gboolean
gs_plugin_repo_enable (GsPlugin *plugin,
                       GsApp *app,
                       gboolean enable,
                       GCancellable *cancellable,
                       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autofree gchar *transaction_address = NULL;
	g_autoptr(GVariantBuilder) options_builder = NULL;
	g_autoptr(TransactionProgress) tp = NULL;

	if (enable)
		gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	else
		gs_app_set_state (app, AS_APP_STATE_REMOVING);

	options_builder = g_variant_builder_new (G_VARIANT_TYPE ("a{ss}"));
	g_variant_builder_add (options_builder, "{ss}", "enabled", enable ? "1" : "0");
	if (!gs_rpmostree_os_call_modify_yum_repo_sync (priv->os_proxy,
	                                                gs_app_get_id (app),
	                                                g_variant_builder_end (options_builder),
	                                                &transaction_address,
	                                                cancellable,
	                                                error)) {
		gs_rpmostree_error_convert (error);
		gs_app_set_state_recover (app);
		gs_utils_error_add_origin_id (error, app);
		return FALSE;
	}

	tp = transaction_progress_new ();
	tp->app = g_object_ref (app);
	if (!gs_rpmostree_transaction_get_response_sync (priv->sysroot_proxy,
	                                                 transaction_address,
	                                                 tp,
	                                                 cancellable,
	                                                 error)) {
		gs_rpmostree_error_convert (error);
		gs_app_set_state_recover (app);
		gs_utils_error_add_origin_id (error, app);
		return FALSE;
	}


	/* state is known */
	if (enable)
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	else
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);

	return TRUE;
}

gboolean
gs_plugin_app_install (GsPlugin *plugin,
                       GsApp *app,
                       GCancellable *cancellable,
                       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *install_package = NULL;
	g_autofree gchar *local_filename = NULL;
	g_autofree gchar *transaction_address = NULL;
	g_autoptr(GVariant) options = NULL;
	g_autoptr(TransactionProgress) tp = transaction_progress_new ();

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* enable repo */
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE)
		return gs_plugin_repo_enable (plugin, app, TRUE, cancellable, error);

	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_AVAILABLE:
		if (gs_app_get_source_default (app) == NULL) {
			g_set_error_literal (error,
			                     GS_PLUGIN_ERROR,
			                     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			                     "no source set");
			return FALSE;
		}

		install_package = gs_app_get_source_default (app);
		break;
	case AS_APP_STATE_AVAILABLE_LOCAL:
		if (gs_app_get_local_file (app) == NULL) {
			g_set_error_literal (error,
			                     GS_PLUGIN_ERROR,
			                     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			                     "local package, but no filename");
			return FALSE;
		}

		local_filename = g_file_get_path (gs_app_get_local_file (app));
		break;
	default:
		g_set_error (error,
		             GS_PLUGIN_ERROR,
		             GS_PLUGIN_ERROR_NOT_SUPPORTED,
		             "do not know how to install app in state %s",
		             as_app_state_to_string (gs_app_get_state (app)));
		return FALSE;
	}

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	tp->app = g_object_ref (app);

	options = make_rpmostree_options_variant (FALSE,  /* reboot */
	                                          FALSE,  /* allow-downgrade */
	                                          FALSE,   /* cache-only */
	                                          FALSE,  /* download-only */
	                                          FALSE,  /* skip-purge */
	                                          TRUE,  /* no-pull-base */
	                                          FALSE,  /* dry-run */
	                                          FALSE); /* no-overrides */

	if (!rpmostree_update_deployment (priv->os_proxy,
	                                  install_package,
	                                  NULL /* remove package */,
	                                  local_filename,
	                                  options,
	                                  &transaction_address,
	                                  cancellable,
	                                  error)) {
		gs_rpmostree_error_convert (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	if (!gs_rpmostree_transaction_get_response_sync (priv->sysroot_proxy,
	                                                 transaction_address,
	                                                 tp,
	                                                 cancellable,
	                                                 error)) {
		gs_rpmostree_error_convert (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* state is known */
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);

	/* get the new icon from the package */
	gs_app_set_local_file (app, NULL);
	gs_app_add_icon (app, NULL);
	gs_app_set_pixbuf (app, NULL);

	/* no longer valid */
	gs_app_clear_source_ids (app);

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
	g_autoptr(TransactionProgress) tp = transaction_progress_new ();

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* disable repo */
	if (gs_app_get_kind (app) == AS_APP_KIND_SOURCE)
		return gs_plugin_repo_enable (plugin, app, FALSE, cancellable, error);

	gs_app_set_state (app, AS_APP_STATE_REMOVING);
	tp->app = g_object_ref (app);

	options = make_rpmostree_options_variant (FALSE,  /* reboot */
	                                          FALSE,  /* allow-downgrade */
	                                          TRUE,   /* cache-only */
	                                          FALSE,  /* download-only */
	                                          FALSE,  /* skip-purge */
	                                          TRUE,  /* no-pull-base */
	                                          FALSE,  /* dry-run */
	                                          FALSE); /* no-overrides */

	if (!rpmostree_update_deployment (priv->os_proxy,
	                                  NULL /* install package */,
	                                  gs_app_get_source_default (app),
	                                  NULL /* install local package */,
	                                  options,
	                                  &transaction_address,
	                                  cancellable,
	                                  error)) {
		gs_rpmostree_error_convert (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	if (!gs_rpmostree_transaction_get_response_sync (priv->sysroot_proxy,
	                                                 transaction_address,
	                                                 tp,
	                                                 cancellable,
	                                                 error)) {
		gs_rpmostree_error_convert (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	/* state is not known: we don't know if we can re-install this app */
	gs_app_set_state (app, AS_APP_STATE_UNKNOWN);

	return TRUE;
}

static DnfPackage *
find_package_by_name (DnfSack     *sack,
                      const char  *pkgname)
{
	g_autoptr(GPtrArray) pkgs = NULL;
	hy_autoquery HyQuery query = hy_query_create (sack);

	hy_query_filter (query, HY_PKG_NAME, HY_EQ, pkgname);
	hy_query_filter_latest_per_arch (query, TRUE);

	pkgs = hy_query_run (query);
	if (pkgs->len == 0)
		return NULL;

	return g_object_ref (pkgs->pdata[pkgs->len-1]);
}

static GPtrArray *
find_packages_by_provides (DnfSack *sack,
                           gchar **search)
{
	g_autoptr(GPtrArray) pkgs = NULL;
	hy_autoquery HyQuery query = hy_query_create (sack);

	hy_query_filter_provides_in (query, search);
	hy_query_filter_latest_per_arch (query, TRUE);

	pkgs = hy_query_run (query);

	return g_steal_pointer (&pkgs);
}

static gboolean
resolve_installed_packages_app (GsPlugin *plugin,
                                GPtrArray *pkglist,
                                gchar **layered_packages,
                                gchar **layered_local_packages,
                                GsApp *app)
{
	for (guint i = 0; i < pkglist->len; i++) {
		RpmOstreePackage *pkg = g_ptr_array_index (pkglist, i);
		if (g_strcmp0 (rpm_ostree_package_get_name (pkg), gs_app_get_source_default (app)) == 0) {
			gs_app_set_version (app, rpm_ostree_package_get_evr (pkg));
			if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
				gs_app_set_state (app, AS_APP_STATE_INSTALLED);
			if (g_strv_contains ((const gchar * const *) layered_packages,
			                     rpm_ostree_package_get_name (pkg)) ||
			    g_strv_contains ((const gchar * const *) layered_local_packages,
			                     rpm_ostree_package_get_nevra (pkg))) {
				/* layered packages can always be removed */
				gs_app_remove_quirk (app, GS_APP_QUIRK_COMPULSORY);
			} else {
				/* can't remove packages that are part of the base system */
				gs_app_add_quirk (app, GS_APP_QUIRK_COMPULSORY);
			}
			if (gs_app_get_origin (app) == NULL)
				gs_app_set_origin (app, "rpm-ostree");
			return TRUE /* found */;
		}
	}

	return FALSE /* not found */;
}

static gboolean
resolve_available_packages_app (GsPlugin *plugin,
                                DnfSack *sack,
                                GsApp *app)
{
	g_autoptr(DnfPackage) pkg = NULL;

	pkg = find_package_by_name (sack, gs_app_get_source_default (app));
	if (pkg != NULL) {
		gs_app_set_version (app, dnf_package_get_evr (pkg));
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_AVAILABLE);

		/* anything not part of the base system can be removed */
		gs_app_remove_quirk (app, GS_APP_QUIRK_COMPULSORY);

		/* set origin */
		if (gs_app_get_origin (app) == NULL) {
			const gchar *reponame = dnf_package_get_reponame (pkg);
			gs_app_set_origin (app, reponame);
		}

		/* set more metadata for packages that don't have appstream data */
		gs_app_set_name (app, GS_APP_QUALITY_LOWEST, dnf_package_get_name (pkg));
		gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, dnf_package_get_summary (pkg));

		/* set hide-from-search quirk for available apps we don't want to show */
		if (!gs_app_is_installed (app)) {
			switch (gs_app_get_kind (app)) {
			case AS_APP_KIND_DESKTOP:
			case AS_APP_KIND_WEB_APP:
			case AS_APP_KIND_CONSOLE:
				gs_app_add_quirk (app, GS_APP_QUIRK_HIDE_FROM_SEARCH);
				break;
			default:
				break;
			}
		}

		return TRUE /* found */;
	}

	return FALSE /* not found */;
}

static gboolean
resolve_appstream_source_file_to_package_name (GsPlugin *plugin,
                                               GsApp *app,
                                               GsPluginRefineFlags flags,
                                               GCancellable *cancellable,
                                               GError **error)
{
	Header h;
	const gchar *fn;
	gint rc;
	g_auto(rpmdbMatchIterator) mi = NULL;
	g_auto(rpmts) ts = NULL;

	/* open db readonly */
	ts = rpmtsCreate();
	rpmtsSetRootDir (ts, NULL);
	rc = rpmtsOpenDB (ts, O_RDONLY);
	if (rc != 0) {
		g_set_error (error,
		             GS_PLUGIN_ERROR,
		             GS_PLUGIN_ERROR_NOT_SUPPORTED,
		             "Failed to open rpmdb: %i", rc);
		return FALSE;
	}

	/* look for a specific file */
	fn = gs_app_get_metadata_item (app, "appstream::source-file");
	if (fn == NULL)
		return TRUE;

	mi = rpmtsInitIterator (ts, RPMDBI_INSTFILENAMES, fn, 0);
	if (mi == NULL) {
		g_debug ("rpm: no search results for %s", fn);
		return TRUE;
	}

	/* process any results */
	g_debug ("rpm: querying for %s with %s", gs_app_get_id (app), fn);
	while ((h = rpmdbNextIterator (mi)) != NULL) {
		const gchar *name;

		/* add default source */
		name = headerGetString (h, RPMTAG_NAME);
		if (gs_app_get_source_default (app) == NULL) {
			g_debug ("rpm: setting source to %s", name);
			gs_app_add_source (app, name);
			gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
			gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
			app_set_rpm_ostree_packaging_format (app);
			gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
		}
	}

	return TRUE;
}

gboolean
gs_plugin_refine (GsPlugin *plugin,
                  GsAppList *list,
                  GsPluginRefineFlags flags,
                  GCancellable *cancellable,
                  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GMutexLocker) locker = NULL;
	g_autoptr(GPtrArray) pkglist = NULL;
	g_autoptr(GVariant) default_deployment = NULL;
	g_auto(GStrv) layered_packages = NULL;
	g_auto(GStrv) layered_local_packages = NULL;
	g_autofree gchar *checksum = NULL;

	locker = g_mutex_locker_new (&priv->mutex);

	/* ensure D-Bus properties are updated before reading them */
	if (!gs_rpmostree_sysroot_call_reload_sync (priv->sysroot_proxy, cancellable, error)) {
		gs_rpmostree_error_convert (error);
		return FALSE;
	}

	default_deployment = gs_rpmostree_os_dup_default_deployment (priv->os_proxy);
	g_assert (g_variant_lookup (default_deployment,
	                            "packages", "^as",
	                            &layered_packages));
	g_assert (g_variant_lookup (default_deployment,
	                            "requested-local-packages", "^as",
	                            &layered_local_packages));
	g_assert (g_variant_lookup (default_deployment,
	                            "checksum", "s",
	                            &checksum));

	pkglist = rpm_ostree_db_query_all (priv->ot_repo, checksum, cancellable, error);
	if (pkglist == NULL) {
		gs_rpmostree_error_convert (error);
		return FALSE;
	}

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		gboolean found;

		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD))
			continue;
		/* set management plugin for apps where appstream just added the source package name in refine() */
		if (gs_app_get_management_plugin (app) == NULL &&
		    gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_PACKAGE &&
		    gs_app_get_scope (app) == AS_APP_SCOPE_SYSTEM &&
		    gs_app_get_source_default (app) != NULL) {
			gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
			gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
			app_set_rpm_ostree_packaging_format (app);
		}
		/* resolve the source package name based on installed appdata/desktop file name */
		if (gs_app_get_management_plugin (app) == NULL &&
		    gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_UNKNOWN &&
		    gs_app_get_scope (app) == AS_APP_SCOPE_SYSTEM &&
		    gs_app_get_source_default (app) == NULL) {
			if (!resolve_appstream_source_file_to_package_name (plugin, app, flags, cancellable, error))
				return FALSE;
		}
		if (g_strcmp0 (gs_app_get_management_plugin (app), gs_plugin_get_name (plugin)) != 0)
			continue;
		if (gs_app_get_source_default (app) == NULL)
			continue;

		/* first try to resolve from installed packages */
		found = resolve_installed_packages_app (plugin, pkglist, layered_packages, layered_local_packages, app);

		/* if we didn't find anything, try resolving from available packages */
		if (!found && priv->dnf_context != NULL)
			found = resolve_available_packages_app (plugin, dnf_context_get_sack (priv->dnf_context), app);

		/* if we still didn't find anything then it's likely a package
		 * that is still in appstream data, but removed from the repos */
		if (!found)
			g_debug ("failed to resolve %s", gs_app_get_unique_id (app));
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
	g_autoptr(TransactionProgress) tp = transaction_progress_new ();

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
	                                          TRUE,   /* allow-downgrade */
	                                          FALSE,  /* cache-only */
	                                          TRUE,   /* download-only */
	                                          FALSE,  /* skip-purge */
	                                          FALSE,  /* no-pull-base */
	                                          FALSE,  /* dry-run */
	                                          FALSE); /* no-overrides */

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	tp->app = g_object_ref (app);

	if (!gs_rpmostree_os_call_rebase_sync (priv->os_proxy,
	                                       options,
	                                       new_refspec,
	                                       packages,
	                                       NULL /* fd list */,
	                                       &transaction_address,
	                                       NULL /* fd list out */,
	                                       cancellable,
	                                       error)) {
		gs_rpmostree_error_convert (error);
		gs_app_set_state_recover (app);
		return FALSE;
	}

	if (!gs_rpmostree_transaction_get_response_sync (priv->sysroot_proxy,
	                                                 transaction_address,
	                                                 tp,
	                                                 cancellable,
	                                                 error)) {
		gs_rpmostree_error_convert (error);

		if (g_strrstr ((*error)->message, "Old and new refs are equal")) {
			/* don't error out if the correct tree is already deployed */
			g_debug ("ignoring rpm-ostree error: %s", (*error)->message);
			g_clear_error (error);
		} else {
			gs_app_set_state_recover (app);
			return FALSE;
		}
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

	/* these are handled by the shell extensions plugin */
	if (gs_app_get_kind (app) == AS_APP_KIND_SHELL_EXTENSION)
		return TRUE;

	return gs_plugin_app_launch (plugin, app, error);
}

static void
add_quirks_from_package_name (GsApp *app, const gchar *package_name)
{
	/* these packages don't have a .repo file in their file lists, but
	 * instead install one through rpm scripts / cron job */
	const gchar *packages_with_repos[] = {
		"google-chrome-stable",
		"google-earth-pro-stable",
		"google-talkplugin",
		NULL };

	if (g_strv_contains (packages_with_repos, package_name))
		gs_app_add_quirk (app, GS_APP_QUIRK_HAS_SOURCE);
}

gboolean
gs_plugin_file_to_app (GsPlugin *plugin,
		       GsAppList *list,
		       GFile *file,
		       GCancellable *cancellable,
		       GError **error)
{
	gboolean ret = FALSE;
	FD_t rpmfd = NULL;
	int r;
	guint64 epoch;
	guint64 size;
	const gchar *name;
	const gchar *version;
	const gchar *release;
	const gchar *license;
	g_auto(Header) h = NULL;
	g_auto(rpmts) ts = NULL;
	g_autofree gchar *evr = NULL;
	g_autofree gchar *filename = NULL;
	g_autoptr(GsApp) app = NULL;

	filename = g_file_get_path (file);
	if (!g_str_has_suffix (filename, ".rpm")) {
		ret = TRUE;
		goto out;
	}

	ts = rpmtsCreate ();
	rpmtsSetVSFlags (ts, _RPMVSF_NOSIGNATURES);

	/* librpm needs Fopenfd */
	rpmfd = Fopen (filename, "r.fdio");
	if (rpmfd == NULL) {
		g_set_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
		             "Opening %s failed", filename);
		goto out;
	}
	if (Ferror (rpmfd)) {
		g_set_error (error,
		             GS_PLUGIN_ERROR,
		             GS_PLUGIN_ERROR_FAILED,
		             "Opening %s failed: %s",
		             filename,
		             Fstrerror (rpmfd));
		goto out;
	}

	if ((r = rpmReadPackageFile (ts, rpmfd, filename, &h)) != RPMRC_OK) {
		g_set_error (error,
		             GS_PLUGIN_ERROR,
		             GS_PLUGIN_ERROR_FAILED,
		             "Verification of %s failed",
		             filename);
		goto out;
	}

	app = gs_app_new (NULL);
	gs_app_set_metadata (app, "GnomeSoftware::Creator", gs_plugin_get_name (plugin));
	gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
	app_set_rpm_ostree_packaging_format (app);
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE_LOCAL);

	/* add default source */
	name = headerGetString (h, RPMTAG_NAME);
	g_debug ("rpm: setting source to %s", name);
	gs_app_add_source (app, name);

	/* add version */
	epoch = headerGetNumber (h, RPMTAG_EPOCH);
	version = headerGetString (h, RPMTAG_VERSION);
	release = headerGetString (h, RPMTAG_RELEASE);
	if (epoch > 0) {
		evr = g_strdup_printf ("%" G_GUINT64_FORMAT ":%s-%s",
		                       epoch, version, release);
	} else {
		evr = g_strdup_printf ("%s-%s",
		                       version, release);
	}
	g_debug ("rpm: setting version to %s", evr);
	gs_app_set_version (app, evr);

	/* set size */
	size = headerGetNumber (h, RPMTAG_SIZE);
	gs_app_set_size_installed (app, size);

	/* set license */
	license = headerGetString (h, RPMTAG_LICENSE);
	if (license != NULL) {
		g_autofree gchar *license_spdx = NULL;
		license_spdx = as_utils_license_to_spdx (license);
		gs_app_set_license (app, GS_APP_QUALITY_NORMAL, license_spdx);
		g_debug ("rpm: setting license to %s", license_spdx);
	}

	add_quirks_from_package_name (app, name);

	gs_app_list_add (list, app);
	ret = TRUE;

out:
	if (rpmfd != NULL)
		(void) Fclose (rpmfd);
	return ret;
}

static gchar **
what_provides_decompose (gchar **values)
{
	GPtrArray *array = g_ptr_array_new ();

	/* iter on each provide string, and wrap it with the Fedora prefix */
	for (guint i = 0; values[i] != NULL; i++) {
		g_ptr_array_add (array, g_strdup (values[i]));
		g_ptr_array_add (array, g_strdup_printf ("gstreamer0.10(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("gstreamer1(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("font(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("mimehandler(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("postscriptdriver(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("plasma4(%s)", values[i]));
		g_ptr_array_add (array, g_strdup_printf ("plasma5(%s)", values[i]));
	}
	g_ptr_array_add (array, NULL);
	return (gchar **) g_ptr_array_free (array, FALSE);
}

gboolean
gs_plugin_add_search_what_provides (GsPlugin *plugin,
                                    gchar **search,
                                    GsAppList *list,
                                    GCancellable *cancellable,
                                    GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GMutexLocker) locker = NULL;
	g_autoptr(GPtrArray) pkglist = NULL;
	g_auto(GStrv) provides = NULL;

	locker = g_mutex_locker_new (&priv->mutex);

	if (priv->dnf_context == NULL)
		return TRUE;

	provides = what_provides_decompose (search);
	pkglist = find_packages_by_provides (dnf_context_get_sack (priv->dnf_context), provides);
	for (guint i = 0; i < pkglist->len; i++) {
		DnfPackage *pkg = g_ptr_array_index (pkglist, i);
		g_autoptr(GsApp) app = NULL;

		app = gs_plugin_cache_lookup (plugin, dnf_package_get_nevra (pkg));
		if (app != NULL) {
			gs_app_list_add (list, app);
			continue;
		}

		/* create new app */
		app = gs_app_new (NULL);
		gs_app_set_metadata (app, "GnomeSoftware::Creator", gs_plugin_get_name (plugin));
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
		gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
		app_set_rpm_ostree_packaging_format (app);
		gs_app_set_kind (app, AS_APP_KIND_GENERIC);
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
		gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
		gs_app_add_source (app, dnf_package_get_name (pkg));

		gs_plugin_cache_add (plugin, dnf_package_get_nevra (pkg), app);
		gs_app_list_add (list, app);
	}

	return TRUE;
}

gboolean
gs_plugin_add_sources (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GMutexLocker) locker = NULL;
	GPtrArray *repos;

	locker = g_mutex_locker_new (&priv->mutex);

	if (priv->dnf_context == NULL)
		return TRUE;

	repos = dnf_context_get_repos (priv->dnf_context);
	if (repos == NULL)
		return TRUE;

	for (guint i = 0; i < repos->len; i++) {
		DnfRepo *repo = g_ptr_array_index (repos, i);
		g_autofree gchar *description = NULL;
		g_autoptr(GsApp) app = NULL;
		gboolean enabled;

		/* hide these from the user */
		if (dnf_repo_is_devel (repo) || dnf_repo_is_source (repo))
			continue;

		app = gs_app_new (dnf_repo_get_id (repo));
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
		gs_app_set_kind (app, AS_APP_KIND_SOURCE);
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
		gs_app_add_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);

		enabled = (dnf_repo_get_enabled (repo) & DNF_REPO_ENABLED_PACKAGES) > 0;
		gs_app_set_state (app, enabled ? AS_APP_STATE_INSTALLED : AS_APP_STATE_AVAILABLE);

		description = dnf_repo_get_description (repo);
		gs_app_set_name (app, GS_APP_QUALITY_LOWEST, description);
		gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, description);

		gs_app_list_add (list, app);
	}

	return TRUE;
}
