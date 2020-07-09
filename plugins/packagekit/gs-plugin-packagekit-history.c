/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <packagekit-glib2/packagekit.h>

#include <gnome-software.h>

#include "packagekit-common.h"

#define GS_PLUGIN_PACKAGEKIT_HISTORY_TIMEOUT	5000 /* ms */

/*
 * SECTION:
 * This returns update history using the system PackageKit instance.
 */

struct GsPluginData {
	GDBusConnection		*connection;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	/* need pkgname */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit-refine");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (priv->connection != NULL)
		g_object_unref (priv->connection);
}

static void
gs_plugin_packagekit_refine_add_history (GsApp *app, GVariant *dict)
{
	const gchar *version;
	gboolean ret;
	guint64 timestamp;
	PkInfoEnum info_enum;
	g_autoptr(GsApp) history = NULL;

	/* create new history item with same ID as parent */
	history = gs_app_new (gs_app_get_id (app));
	gs_app_set_kind (history, AS_APP_KIND_GENERIC);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_name (history, GS_APP_QUALITY_NORMAL, gs_app_get_name (app));

	/* get the installed state */
	ret = g_variant_lookup (dict, "info", "u", &info_enum);
	g_assert (ret);
	switch (info_enum) {
	case PK_INFO_ENUM_INSTALLING:
		gs_app_set_state (history, AS_APP_STATE_INSTALLED);
		break;
	case PK_INFO_ENUM_REMOVING:
		gs_app_set_state (history, AS_APP_STATE_AVAILABLE);
		break;
	case PK_INFO_ENUM_UPDATING:
		gs_app_set_state (history, AS_APP_STATE_UPDATABLE);
		break;
	default:
		g_debug ("ignoring history kind: %s",
			 pk_info_enum_to_string (info_enum));
		return;
	}

	/* set the history time and date */
	ret = g_variant_lookup (dict, "timestamp", "t", &timestamp);
	g_assert (ret);
	gs_app_set_install_date (history, timestamp);

	/* set the history version number */
	ret = g_variant_lookup (dict, "version", "&s", &version);
	g_assert (ret);
	gs_app_set_version (history, version);

	/* add the package to the main application */
	gs_app_add_history (app, history);

	/* use the last event as approximation of the package timestamp */
	gs_app_set_install_date (app, timestamp);
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	priv->connection = g_bus_get_sync (G_BUS_TYPE_SYSTEM,
						   cancellable,
						   error);
	return priv->connection != NULL;
}

static gboolean
gs_plugin_packagekit_refine (GsPlugin *plugin,
			     GsAppList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	gboolean ret;
	guint j;
	GsApp *app;
	guint i = 0;
	GVariantIter iter;
	GVariant *value;
	g_autofree const gchar **package_names = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GVariant) result = NULL;
	g_autoptr(GVariant) tuple = NULL;

	/* get an array of package names */
	package_names = g_new0 (const gchar *, gs_app_list_length (list) + 1);
	for (j = 0; j < gs_app_list_length (list); j++) {
		app = gs_app_list_index (list, j);
		package_names[i++] = gs_app_get_source_default (app);
	}

	g_debug ("getting history for %u packages", gs_app_list_length (list));
	result = g_dbus_connection_call_sync (priv->connection,
					      "org.freedesktop.PackageKit",
					      "/org/freedesktop/PackageKit",
					      "org.freedesktop.PackageKit",
					      "GetPackageHistory",
					      g_variant_new ("(^asu)", package_names, 0),
					      NULL,
					      G_DBUS_CALL_FLAGS_NONE,
					      GS_PLUGIN_PACKAGEKIT_HISTORY_TIMEOUT,
					      cancellable,
					      &error_local);
	if (result == NULL) {
		if (g_error_matches (error_local,
				     G_DBUS_ERROR,
				     G_DBUS_ERROR_UNKNOWN_METHOD)) {
			g_debug ("No history available as PackageKit is too old: %s",
				 error_local->message);

			/* just set this to something non-zero so we don't keep
			 * trying to call GetPackageHistory */
			for (i = 0; i < gs_app_list_length (list); i++) {
				app = gs_app_list_index (list, i);
				gs_app_set_install_date (app, GS_APP_INSTALL_DATE_UNKNOWN);
			}
		} else if (g_error_matches (error_local,
					    G_IO_ERROR,
					    G_IO_ERROR_CANCELLED)) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_CANCELLED,
				     "Failed to get history: %s",
				     error_local->message);
			return FALSE;
		} else if (g_error_matches (error_local,
					    G_IO_ERROR,
					    G_IO_ERROR_TIMED_OUT)) {
			g_debug ("No history as PackageKit took too long: %s",
				 error_local->message);
			for (i = 0; i < gs_app_list_length (list); i++) {
				app = gs_app_list_index (list, i);
				gs_app_set_install_date (app, GS_APP_INSTALL_DATE_UNKNOWN);
			}
		}
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "Failed to get history: %s",
			     error_local->message);
		return FALSE;
	}

	/* get any results */
	tuple = g_variant_get_child_value (result, 0);
	for (i = 0; i < gs_app_list_length (list); i++) {
		g_autoptr(GVariant) entries = NULL;
		app = gs_app_list_index (list, i);
		ret = g_variant_lookup (tuple,
					gs_app_get_source_default (app),
					"@aa{sv}",
					&entries);
		if (!ret) {
			/* make up a fake entry as we know this package was at
			 * least installed at some point in time */
			if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED) {
				g_autoptr(GsApp) app_dummy = NULL;
				app_dummy = gs_app_new (gs_app_get_id (app));
				gs_plugin_packagekit_set_packaging_format (plugin, app);
				gs_app_set_metadata (app_dummy, "GnomeSoftware::Creator",
						     gs_plugin_get_name (plugin));
				gs_app_set_install_date (app_dummy, GS_APP_INSTALL_DATE_UNKNOWN);
				gs_app_set_kind (app_dummy, AS_APP_KIND_GENERIC);
				gs_app_set_state (app_dummy, AS_APP_STATE_INSTALLED);
				gs_app_set_version (app_dummy, gs_app_get_version (app));
				gs_app_add_history (app, app_dummy);
			}
			gs_app_set_install_date (app, GS_APP_INSTALL_DATE_UNKNOWN);
			continue;
		}

		/* add history for application */
		g_variant_iter_init (&iter, entries);
		while ((value = g_variant_iter_next_value (&iter))) {
			gs_plugin_packagekit_refine_add_history (app, value);
			g_variant_unref (value);
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
	gboolean ret;
	guint i;
	GsApp *app;
	GPtrArray *sources;
	g_autoptr(GsAppList) packages = NULL;

	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_HISTORY) == 0)
		return TRUE;

	/* add any missing history data */
	packages = gs_app_list_new ();
	for (i = 0; i < gs_app_list_length (list); i++) {
		app = gs_app_list_index (list, i);
		if (g_strcmp0 (gs_app_get_management_plugin (app), "packagekit") != 0)
			continue;
		sources = gs_app_get_sources (app);
		if (sources->len == 0)
			continue;
		if (gs_app_get_install_date (app) != 0)
			continue;
		gs_app_list_add (packages, app);
	}
	if (gs_app_list_length (packages) > 0) {
		ret = gs_plugin_packagekit_refine (plugin,
						   packages,
						   cancellable,
						   error);
		if (!ret)
			return FALSE;
	}
	return TRUE;
}
