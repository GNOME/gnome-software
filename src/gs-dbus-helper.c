/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2017 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

#include "gnome-software-private.h"

#include "gs-dbus-helper.h"
#include "gs-packagekit-generated.h"
#include "gs-packagekit-modify2-generated.h"
#include "gs-resources.h"
#include "gs-extras-page.h"

struct _GsDbusHelper {
	GObject			 parent;
	GCancellable		*cancellable;
	GDBusInterfaceSkeleton	*query_interface;
	GDBusInterfaceSkeleton	*modify_interface;
	GDBusInterfaceSkeleton	*modify2_interface;
	PkTask			*task;
	guint			 dbus_own_name_id;
};

G_DEFINE_TYPE (GsDbusHelper, gs_dbus_helper, G_TYPE_OBJECT)

typedef struct {
	GDBusMethodInvocation	*invocation;
	GsDbusHelper		*dbus_helper;
	gboolean		 show_confirm_deps;
	gboolean		 show_confirm_install;
	gboolean		 show_confirm_search;
	gboolean		 show_finished;
	gboolean		 show_progress;
	gboolean		 show_warning;
} GsDbusHelperTask;

static void
gs_dbus_helper_task_free (GsDbusHelperTask *dtask)
{
	if (dtask->dbus_helper != NULL)
		g_object_unref (dtask->dbus_helper);

	g_free (dtask);
}

static void
gs_dbus_helper_task_set_interaction (GsDbusHelperTask *dtask, const gchar *interaction)
{
	guint i;
	g_auto(GStrv) interactions = NULL;

	interactions = g_strsplit (interaction, ",", -1);
	for (i = 0; interactions[i] != NULL; i++) {
		if (g_strcmp0 (interactions[i], "show-warnings") == 0)
			dtask->show_warning = TRUE;
		else if (g_strcmp0 (interactions[i], "hide-warnings") == 0)
			dtask->show_warning = FALSE;
		else if (g_strcmp0 (interactions[i], "show-progress") == 0)
			dtask->show_progress = TRUE;
		else if (g_strcmp0 (interactions[i], "hide-progress") == 0)
			dtask->show_progress = FALSE;
		else if (g_strcmp0 (interactions[i], "show-finished") == 0)
			dtask->show_finished = TRUE;
		else if (g_strcmp0 (interactions[i], "hide-finished") == 0)
			dtask->show_finished = FALSE;
		else if (g_strcmp0 (interactions[i], "show-confirm-search") == 0)
			dtask->show_confirm_search = TRUE;
		else if (g_strcmp0 (interactions[i], "hide-confirm-search") == 0)
			dtask->show_confirm_search = FALSE;
		else if (g_strcmp0 (interactions[i], "show-confirm-install") == 0)
			dtask->show_confirm_install = TRUE;
		else if (g_strcmp0 (interactions[i], "hide-confirm-install") == 0)
			dtask->show_confirm_install = FALSE;
		else if (g_strcmp0 (interactions[i], "show-confirm-deps") == 0)
			dtask->show_confirm_deps = TRUE;
		else if (g_strcmp0 (interactions[i], "hide-confirm-deps") == 0)
			dtask->show_confirm_deps = FALSE;
	}
}

static void
gs_dbus_helper_progress_cb (PkProgress *progress, PkProgressType type, gpointer data)
{
}

static void
gs_dbus_helper_query_is_installed_cb (GObject *source, GAsyncResult *res, gpointer data)
{
	GsDbusHelperTask *dtask = (GsDbusHelperTask *) data;
	PkClient *client = PK_CLIENT (source);
	g_autoptr(GError) error = NULL;
	g_autoptr(PkError) error_code = NULL;
	g_autoptr(PkResults) results = NULL;
	g_autoptr(GPtrArray) array = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_dbus_method_invocation_return_error (dtask->invocation,
						       G_IO_ERROR,
						       G_IO_ERROR_INVALID_ARGUMENT,
						       "failed to resolve: %s",
						       error->message);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_dbus_method_invocation_return_error (dtask->invocation,
						       G_IO_ERROR,
						       G_IO_ERROR_INVALID_ARGUMENT,
						       "failed to resolve: %s",
						       pk_error_get_details (error_code));
		goto out;
	}

	/* get results */
	array = pk_results_get_package_array (results);
	gs_package_kit_query_complete_is_installed (GS_PACKAGE_KIT_QUERY (dtask->dbus_helper->query_interface),
	                                            dtask->invocation,
	                                            array->len > 0);
out:
	gs_dbus_helper_task_free (dtask);
}

static void
gs_dbus_helper_query_search_file_cb (GObject *source, GAsyncResult *res, gpointer data)
{
	g_autoptr(GError) error = NULL;
	GsDbusHelperTask *dtask = (GsDbusHelperTask *) data;
	PkClient *client = PK_CLIENT (source);
	PkInfoEnum info;
	PkPackage *item;
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(PkError) error_code = NULL;
	g_autoptr(PkResults) results = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_dbus_method_invocation_return_error (dtask->invocation,
						       G_IO_ERROR,
						       G_IO_ERROR_INVALID_ARGUMENT,
						       "failed to search: %s",
						       error->message);
		return;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		g_dbus_method_invocation_return_error (dtask->invocation,
						       G_IO_ERROR,
						       G_IO_ERROR_INVALID_ARGUMENT,
						       "failed to search: %s",
						       pk_error_get_details (error_code));
		return;
	}

	/* get results */
	array = pk_results_get_package_array (results);
	if (array->len == 0) {
		//TODO: org.freedesktop.PackageKit.Query.unknown
		g_dbus_method_invocation_return_error (dtask->invocation,
						       G_IO_ERROR,
						       G_IO_ERROR_INVALID_ARGUMENT,
						       "failed to find any packages");
		return;
	}

	/* get first item */
	item = g_ptr_array_index (array, 0);
	info = pk_package_get_info (item);
	gs_package_kit_query_complete_search_file (GS_PACKAGE_KIT_QUERY (dtask->dbus_helper->query_interface),
	                                           dtask->invocation,
	                                           info == PK_INFO_ENUM_INSTALLED,
	                                           pk_package_get_name (item));
}

static gboolean
handle_query_search_file (GsPackageKitQuery	 *skeleton,
                          GDBusMethodInvocation	 *invocation,
                          const gchar		 *file_name,
                          const gchar		 *interaction,
                          gpointer		  user_data)
{
	GsDbusHelper *dbus_helper = user_data;
	GsDbusHelperTask *dtask;
	g_auto(GStrv) names = NULL;

	g_debug ("****** SearchFile");

	dtask = g_new0 (GsDbusHelperTask, 1);
	dtask->dbus_helper = g_object_ref (dbus_helper);
	dtask->invocation = invocation;
	gs_dbus_helper_task_set_interaction (dtask, interaction);
	names = g_strsplit (file_name, "&", -1);
	pk_client_search_files_async (PK_CLIENT (dbus_helper->task),
	                              pk_bitfield_value (PK_FILTER_ENUM_NEWEST),
	                              names, NULL,
	                              gs_dbus_helper_progress_cb, dtask,
	                              gs_dbus_helper_query_search_file_cb, dtask);

	return TRUE;
}

static gboolean
handle_query_is_installed (GsPackageKitQuery	 *skeleton,
                           GDBusMethodInvocation *invocation,
                           const gchar		 *package_name,
                           const gchar		 *interaction,
                           gpointer		  user_data)
{
	GsDbusHelper *dbus_helper = user_data;
	GsDbusHelperTask *dtask;
	g_auto(GStrv) names = NULL;

	g_debug ("****** IsInstalled");

	dtask = g_new0 (GsDbusHelperTask, 1);
	dtask->dbus_helper = g_object_ref (dbus_helper);
	dtask->invocation = invocation;
	gs_dbus_helper_task_set_interaction (dtask, interaction);
	names = g_strsplit (package_name, "|", 1);
	pk_client_resolve_async (PK_CLIENT (dbus_helper->task),
	                         pk_bitfield_value (PK_FILTER_ENUM_INSTALLED),
	                         names, NULL,
	                         gs_dbus_helper_progress_cb, dtask,
	                         gs_dbus_helper_query_is_installed_cb, dtask);

	return TRUE;
}

static gboolean
is_show_confirm_search_set (const gchar *interaction)
{
	GsDbusHelperTask *dtask;
	gboolean ret;

	dtask = g_new0 (GsDbusHelperTask, 1);
	dtask->show_confirm_search = TRUE;
	gs_dbus_helper_task_set_interaction (dtask, interaction);
	ret = dtask->show_confirm_search;
	gs_dbus_helper_task_free (dtask);

	return ret;
}

static void
notify_search_resources (GsExtrasPageMode   mode,
                         const gchar       *desktop_id,
                         gchar            **resources)
{
	const gchar *app_name = NULL;
	const gchar *mode_string;
	const gchar *title = NULL;
	g_autofree gchar *body = NULL;
	g_autoptr(GDesktopAppInfo) app_info = NULL;
	g_autoptr(GNotification) n = NULL;

	if (desktop_id != NULL) {
		app_info = gs_utils_get_desktop_app_info (desktop_id);
		if (app_info != NULL)
			app_name = g_app_info_get_name (G_APP_INFO (app_info));
	}

	if (app_name == NULL) {
		/* TRANSLATORS: this is a what we use in notifications if the app's name is unknown */
		app_name = _("An application");
	}

	switch (mode) {
	case GS_EXTRAS_PAGE_MODE_INSTALL_MIME_TYPES:
		/* TRANSLATORS: this is a notification displayed when an app needs additional MIME types. */
		body = g_strdup_printf (_("%s is requesting additional file format support."), app_name);
		/* TRANSLATORS: notification title */
		title = _("Additional MIME Types Required");
		break;
	case GS_EXTRAS_PAGE_MODE_INSTALL_FONTCONFIG_RESOURCES:
		/* TRANSLATORS: this is a notification displayed when an app needs additional fonts. */
		body = g_strdup_printf (_("%s is requesting additional fonts."), app_name);
		/* TRANSLATORS: notification title */
		title = _("Additional Fonts Required");
		break;
	case GS_EXTRAS_PAGE_MODE_INSTALL_GSTREAMER_RESOURCES:
		/* TRANSLATORS: this is a notification displayed when an app needs additional codecs. */
		body = g_strdup_printf (_("%s is requesting additional multimedia codecs."), app_name);
		/* TRANSLATORS: notification title */
		title = _("Additional Multimedia Codecs Required");
		break;
	case GS_EXTRAS_PAGE_MODE_INSTALL_PRINTER_DRIVERS:
		/* TRANSLATORS: this is a notification displayed when an app needs additional printer drivers. */
		body = g_strdup_printf (_("%s is requesting additional printer drivers."), app_name);
		/* TRANSLATORS: notification title */
		title = _("Additional Printer Drivers Required");
		break;
	default:
		/* TRANSLATORS: this is a notification displayed when an app wants to install additional packages. */
		body = g_strdup_printf (_("%s is requesting additional packages."), app_name);
		/* TRANSLATORS: notification title */
		title = _("Additional Packages Required");
		break;
	}

	mode_string = gs_extras_page_mode_to_string (mode);

	n = g_notification_new (title);
	g_notification_set_body (n, body);
	/* TRANSLATORS: this is a button that launches gnome-software */
	g_notification_add_button_with_target (n, _("Find in Software"), "app.install-resources", "(s^ass)", mode_string, resources, "");
	g_notification_set_default_action_and_target (n, "app.install-resources", "(s^ass)", mode_string, resources, "");
	g_application_send_notification (g_application_get_default (), "install-resources", n);
}

static void
install_resources (GsExtrasPageMode   mode,
                   gchar            **resources,
                   const gchar       *interaction,
                   const gchar       *desktop_id,
                   GVariant          *platform_data)
{
	GApplication *app;
	const gchar *mode_string;
	const gchar *startup_id = NULL;

	if (is_show_confirm_search_set (interaction)) {
		notify_search_resources (mode, desktop_id, resources);
		return;
	}

	if (platform_data != NULL) {
		g_variant_lookup (platform_data, "desktop-startup-id",
		                  "&s", &startup_id);
	}

	app = g_application_get_default ();
	mode_string = gs_extras_page_mode_to_string (mode);
	g_action_group_activate_action (G_ACTION_GROUP (app), "install-resources",
	                                g_variant_new ("(s^ass)", mode_string, resources, startup_id));
}

static gboolean
handle_modify_install_package_files (GsPackageKitModify		 *object,
                                     GDBusMethodInvocation	 *invocation,
                                     guint			  arg_xid,
                                     gchar			**arg_files,
                                     const gchar		 *arg_interaction,
                                     gpointer			  user_data)
{
	g_debug ("****** Modify.InstallPackageFiles");

	notify_search_resources (GS_EXTRAS_PAGE_MODE_INSTALL_PACKAGE_FILES, NULL, arg_files);
	gs_package_kit_modify_complete_install_package_files (object, invocation);

	return TRUE;
}

static gboolean
handle_modify_install_provide_files (GsPackageKitModify		 *object,
                                     GDBusMethodInvocation	 *invocation,
                                     guint			  arg_xid,
                                     gchar			**arg_files,
                                     const gchar		 *arg_interaction,
                                     gpointer			  user_data)
{
	g_debug ("****** Modify.InstallProvideFiles");

	notify_search_resources (GS_EXTRAS_PAGE_MODE_INSTALL_PROVIDE_FILES, NULL, arg_files);
	gs_package_kit_modify_complete_install_provide_files (object, invocation);

	return TRUE;
}

static gboolean
handle_modify_install_package_names (GsPackageKitModify		 *object,
                                     GDBusMethodInvocation	 *invocation,
                                     guint			  arg_xid,
                                     gchar			**arg_package_names,
                                     const gchar		 *arg_interaction,
                                     gpointer			  user_data)
{
	g_debug ("****** Modify.InstallPackageNames");

	notify_search_resources (GS_EXTRAS_PAGE_MODE_INSTALL_PACKAGE_NAMES, NULL, arg_package_names);
	gs_package_kit_modify_complete_install_package_names (object, invocation);

	return TRUE;
}

static gboolean
handle_modify_install_mime_types (GsPackageKitModify    *object,
                                  GDBusMethodInvocation *invocation,
                                  guint                  arg_xid,
                                  gchar                **arg_mime_types,
                                  const gchar           *arg_interaction,
                                  gpointer               user_data)
{
	g_debug ("****** Modify.InstallMimeTypes");

	notify_search_resources (GS_EXTRAS_PAGE_MODE_INSTALL_MIME_TYPES, NULL, arg_mime_types);
	gs_package_kit_modify_complete_install_mime_types (object, invocation);

	return TRUE;
}

static gboolean
handle_modify_install_fontconfig_resources (GsPackageKitModify		 *object,
                                            GDBusMethodInvocation	 *invocation,
                                            guint			  arg_xid,
                                            gchar			**arg_resources,
                                            const gchar			 *arg_interaction,
                                            gpointer			  user_data)
{
	g_debug ("****** Modify.InstallFontconfigResources");

	notify_search_resources (GS_EXTRAS_PAGE_MODE_INSTALL_FONTCONFIG_RESOURCES, NULL, arg_resources);
	gs_package_kit_modify_complete_install_fontconfig_resources (object, invocation);

	return TRUE;
}

static gboolean
handle_modify_install_gstreamer_resources (GsPackageKitModify	 *object,
                                           GDBusMethodInvocation *invocation,
                                           guint		  arg_xid,
                                           gchar		**arg_resources,
                                           const gchar		 *arg_interaction,
                                           gpointer		  user_data)
{
	g_debug ("****** Modify.InstallGStreamerResources");

	notify_search_resources (GS_EXTRAS_PAGE_MODE_INSTALL_GSTREAMER_RESOURCES, NULL, arg_resources);
	gs_package_kit_modify_complete_install_gstreamer_resources (object, invocation);

	return TRUE;
}

static gboolean
handle_modify_install_resources (GsPackageKitModify	 *object,
                                 GDBusMethodInvocation	 *invocation,
                                 guint			  arg_xid,
                                 const gchar		 *arg_type,
                                 gchar			**arg_resources,
                                 const gchar		 *arg_interaction,
                                 gpointer		  user_data)
{
	gboolean ret;

	g_debug ("****** Modify.InstallResources");

	if (g_strcmp0 (arg_type, "plasma-service") == 0) {
		notify_search_resources (GS_EXTRAS_PAGE_MODE_INSTALL_PLASMA_RESOURCES, NULL, arg_resources);
		ret = TRUE;
	} else {
		ret = FALSE;
	}
	gs_package_kit_modify_complete_install_resources (object, invocation);

	return ret;
}

static gboolean
handle_modify_install_printer_drivers (GsPackageKitModify	 *object,
                                       GDBusMethodInvocation	 *invocation,
                                       guint			  arg_xid,
                                       gchar			**arg_device_ids,
                                       const gchar		 *arg_interaction,
                                       gpointer			  user_data)
{
	g_debug ("****** Modify.InstallPrinterDrivers");

	notify_search_resources (GS_EXTRAS_PAGE_MODE_INSTALL_PRINTER_DRIVERS, NULL, arg_device_ids);
	gs_package_kit_modify_complete_install_printer_drivers (object, invocation);

	return TRUE;
}

static gboolean
handle_modify2_install_package_files (GsPackageKitModify2	 *object,
                                      GDBusMethodInvocation	 *invocation,
                                      gchar			**arg_files,
                                      const gchar		 *arg_interaction,
                                      const gchar		 *arg_desktop_id,
                                      GVariant			 *arg_platform_data,
                                      gpointer			  user_data)
{
	g_debug ("****** Modify2.InstallPackageFiles");

	install_resources (GS_EXTRAS_PAGE_MODE_INSTALL_PACKAGE_FILES, arg_files, arg_interaction, arg_desktop_id, arg_platform_data);
	gs_package_kit_modify2_complete_install_package_files (object, invocation);

	return TRUE;
}

static gboolean
handle_modify2_install_provide_files (GsPackageKitModify2	 *object,
                                      GDBusMethodInvocation	 *invocation,
                                      gchar			**arg_files,
                                      const gchar		 *arg_interaction,
                                      const gchar		 *arg_desktop_id,
                                      GVariant			 *arg_platform_data,
                                      gpointer			  user_data)
{
	g_debug ("****** Modify2.InstallProvideFiles");

	install_resources (GS_EXTRAS_PAGE_MODE_INSTALL_PROVIDE_FILES, arg_files, arg_interaction, arg_desktop_id, arg_platform_data);
	gs_package_kit_modify2_complete_install_provide_files (object, invocation);

	return TRUE;
}

static gboolean
handle_modify2_install_package_names (GsPackageKitModify2	 *object,
                                      GDBusMethodInvocation	 *invocation,
                                      gchar			**arg_package_names,
                                      const gchar		 *arg_interaction,
                                      const gchar		 *arg_desktop_id,
                                      GVariant			 *arg_platform_data,
                                      gpointer			  user_data)
{
	g_debug ("****** Modify2.InstallPackageNames");

	install_resources (GS_EXTRAS_PAGE_MODE_INSTALL_PACKAGE_NAMES, arg_package_names, arg_interaction, arg_desktop_id, arg_platform_data);
	gs_package_kit_modify2_complete_install_package_names (object, invocation);

	return TRUE;
}

static gboolean
handle_modify2_install_mime_types (GsPackageKitModify2	 *object,
                                   GDBusMethodInvocation *invocation,
                                   gchar		 **arg_mime_types,
                                   const gchar		 *arg_interaction,
                                   const gchar		 *arg_desktop_id,
                                   GVariant		 *arg_platform_data,
                                   gpointer		  user_data)
{
	g_debug ("****** Modify2.InstallMimeTypes");

	install_resources (GS_EXTRAS_PAGE_MODE_INSTALL_MIME_TYPES, arg_mime_types, arg_interaction, arg_desktop_id, arg_platform_data);
	gs_package_kit_modify2_complete_install_mime_types (object, invocation);

	return TRUE;
}

static gboolean
handle_modify2_install_fontconfig_resources (GsPackageKitModify2	 *object,
                                             GDBusMethodInvocation	 *invocation,
                                             gchar			**arg_resources,
                                             const gchar		 *arg_interaction,
                                             const gchar		 *arg_desktop_id,
                                             GVariant			 *arg_platform_data,
                                             gpointer			  user_data)
{
	g_debug ("****** Modify2.InstallFontconfigResources");

	install_resources (GS_EXTRAS_PAGE_MODE_INSTALL_FONTCONFIG_RESOURCES, arg_resources, arg_interaction, arg_desktop_id, arg_platform_data);
	gs_package_kit_modify2_complete_install_fontconfig_resources (object, invocation);

	return TRUE;
}

static gboolean
handle_modify2_install_gstreamer_resources (GsPackageKitModify2		 *object,
                                            GDBusMethodInvocation	 *invocation,
                                            gchar			**arg_resources,
                                            const gchar			 *arg_interaction,
                                            const gchar			 *arg_desktop_id,
                                            GVariant			 *arg_platform_data,
                                            gpointer			  user_data)
{
	g_debug ("****** Modify2.InstallGStreamerResources");

	install_resources (GS_EXTRAS_PAGE_MODE_INSTALL_GSTREAMER_RESOURCES, arg_resources, arg_interaction, arg_desktop_id, arg_platform_data);
	gs_package_kit_modify2_complete_install_gstreamer_resources (object, invocation);

	return TRUE;
}

static gboolean
handle_modify2_install_resources (GsPackageKitModify2	 *object,
                                  GDBusMethodInvocation	 *invocation,
                                  const gchar		 *arg_type,
                                  gchar			**arg_resources,
                                  const gchar		 *arg_interaction,
                                  const gchar		 *arg_desktop_id,
                                  GVariant		 *arg_platform_data,
                                  gpointer		  user_data)
{
	gboolean ret;

	g_debug ("****** Modify2.InstallResources");

	if (g_strcmp0 (arg_type, "plasma-service") == 0) {
		install_resources (GS_EXTRAS_PAGE_MODE_INSTALL_PLASMA_RESOURCES, arg_resources, arg_interaction, arg_desktop_id, arg_platform_data);
		ret = TRUE;
	} else {
		ret = FALSE;
	}
	gs_package_kit_modify2_complete_install_resources (object, invocation);

	return ret;
}

static gboolean
handle_modify2_install_printer_drivers (GsPackageKitModify2	 *object,
                                        GDBusMethodInvocation	 *invocation,
                                        gchar			**arg_device_ids,
                                        const gchar		 *arg_interaction,
                                        const gchar		 *arg_desktop_id,
                                        GVariant		 *arg_platform_data,
                                        gpointer		  user_data)
{
	g_debug ("****** Modify2.InstallPrinterDrivers");

	install_resources (GS_EXTRAS_PAGE_MODE_INSTALL_PRINTER_DRIVERS, arg_device_ids, arg_interaction, arg_desktop_id, arg_platform_data);
	gs_package_kit_modify2_complete_install_printer_drivers (object, invocation);

	return TRUE;
}

static void
gs_dbus_helper_name_acquired_cb (GDBusConnection *connection,
				 const gchar *name,
				 gpointer user_data)
{
	g_debug ("acquired session service");
}

static void
gs_dbus_helper_name_lost_cb (GDBusConnection *connection,
			     const gchar *name,
			     gpointer user_data)
{
	g_warning ("lost session service");
}

static void
bus_gotten_cb (GObject      *source_object,
               GAsyncResult *res,
               gpointer      user_data)
{
	GsDbusHelper *dbus_helper = GS_DBUS_HELPER (user_data);
	g_autoptr(GDBusConnection) connection = NULL;
	g_autoptr(GDesktopAppInfo) app_info = NULL;
	g_autoptr(GError) error = NULL;

	connection = g_bus_get_finish (res, &error);
	if (connection == NULL) {
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("Could not get session bus: %s", error->message);
		return;
	}

	/* Query interface */
	dbus_helper->query_interface = G_DBUS_INTERFACE_SKELETON (gs_package_kit_query_skeleton_new ());

	g_signal_connect (dbus_helper->query_interface, "handle-is-installed",
	                  G_CALLBACK (handle_query_is_installed), dbus_helper);
	g_signal_connect (dbus_helper->query_interface, "handle-search-file",
	                  G_CALLBACK (handle_query_search_file), dbus_helper);

	if (!g_dbus_interface_skeleton_export (dbus_helper->query_interface,
	                                       connection,
	                                       "/org/freedesktop/PackageKit",
	                                       &error)) {
	        g_warning ("Could not export dbus interface: %s", error->message);
	        return;
	}

	/* Modify interface */
	dbus_helper->modify_interface = G_DBUS_INTERFACE_SKELETON (gs_package_kit_modify_skeleton_new ());

	g_signal_connect (dbus_helper->modify_interface, "handle-install-package-files",
	                  G_CALLBACK (handle_modify_install_package_files), dbus_helper);
	g_signal_connect (dbus_helper->modify_interface, "handle-install-provide-files",
	                  G_CALLBACK (handle_modify_install_provide_files), dbus_helper);
	g_signal_connect (dbus_helper->modify_interface, "handle-install-package-names",
	                  G_CALLBACK (handle_modify_install_package_names), dbus_helper);
	g_signal_connect (dbus_helper->modify_interface, "handle-install-mime-types",
	                  G_CALLBACK (handle_modify_install_mime_types), dbus_helper);
	g_signal_connect (dbus_helper->modify_interface, "handle-install-fontconfig-resources",
	                  G_CALLBACK (handle_modify_install_fontconfig_resources), dbus_helper);
	g_signal_connect (dbus_helper->modify_interface, "handle-install-gstreamer-resources",
	                  G_CALLBACK (handle_modify_install_gstreamer_resources), dbus_helper);
	g_signal_connect (dbus_helper->modify_interface, "handle-install-resources",
	                  G_CALLBACK (handle_modify_install_resources), dbus_helper);
	g_signal_connect (dbus_helper->modify_interface, "handle-install-printer-drivers",
	                  G_CALLBACK (handle_modify_install_printer_drivers), dbus_helper);

	if (!g_dbus_interface_skeleton_export (dbus_helper->modify_interface,
	                                       connection,
	                                       "/org/freedesktop/PackageKit",
	                                       &error)) {
	        g_warning ("Could not export dbus interface: %s", error->message);
	        return;
	}

	/* Modify2 interface */
	dbus_helper->modify2_interface = G_DBUS_INTERFACE_SKELETON (gs_package_kit_modify2_skeleton_new ());

	g_signal_connect (dbus_helper->modify2_interface, "handle-install-package-files",
	                  G_CALLBACK (handle_modify2_install_package_files), dbus_helper);
	g_signal_connect (dbus_helper->modify2_interface, "handle-install-provide-files",
	                  G_CALLBACK (handle_modify2_install_provide_files), dbus_helper);
	g_signal_connect (dbus_helper->modify2_interface, "handle-install-package-names",
	                  G_CALLBACK (handle_modify2_install_package_names), dbus_helper);
	g_signal_connect (dbus_helper->modify2_interface, "handle-install-mime-types",
	                  G_CALLBACK (handle_modify2_install_mime_types), dbus_helper);
	g_signal_connect (dbus_helper->modify2_interface, "handle-install-fontconfig-resources",
	                  G_CALLBACK (handle_modify2_install_fontconfig_resources), dbus_helper);
	g_signal_connect (dbus_helper->modify2_interface, "handle-install-gstreamer-resources",
	                  G_CALLBACK (handle_modify2_install_gstreamer_resources), dbus_helper);
	g_signal_connect (dbus_helper->modify2_interface, "handle-install-resources",
	                  G_CALLBACK (handle_modify2_install_resources), dbus_helper);
	g_signal_connect (dbus_helper->modify2_interface, "handle-install-printer-drivers",
	                  G_CALLBACK (handle_modify2_install_printer_drivers), dbus_helper);

	/* Look up our own localized name and export it as a property on the bus */
	app_info = g_desktop_app_info_new ("org.gnome.Software.desktop");
	if (app_info != NULL) {
		const gchar *app_name = g_app_info_get_name (G_APP_INFO (app_info));
		if (app_name != NULL)
			g_object_set (G_OBJECT (dbus_helper->modify2_interface),
			              "display-name", app_name,
			              NULL);
	}

	if (!g_dbus_interface_skeleton_export (dbus_helper->modify2_interface,
	                                       connection,
	                                       "/org/freedesktop/PackageKit",
	                                       &error)) {
	        g_warning ("Could not export dbus interface: %s", error->message);
	        return;
	}

	dbus_helper->dbus_own_name_id = g_bus_own_name_on_connection (connection,
	                                                              "org.freedesktop.PackageKit",
	                                                              G_BUS_NAME_OWNER_FLAGS_NONE,
	                                                              gs_dbus_helper_name_acquired_cb,
	                                                              gs_dbus_helper_name_lost_cb,
	                                                              NULL, NULL);
}

static void
gs_dbus_helper_init (GsDbusHelper *dbus_helper)
{
	dbus_helper->task = pk_task_new ();
	dbus_helper->cancellable = g_cancellable_new ();

	g_bus_get (G_BUS_TYPE_SESSION,
	           dbus_helper->cancellable,
	           (GAsyncReadyCallback) bus_gotten_cb,
	           dbus_helper);
}

static void
gs_dbus_helper_dispose (GObject *object)
{
	GsDbusHelper *dbus_helper = GS_DBUS_HELPER (object);

	g_cancellable_cancel (dbus_helper->cancellable);
	g_clear_object (&dbus_helper->cancellable);

	if (dbus_helper->dbus_own_name_id != 0) {
		g_bus_unown_name (dbus_helper->dbus_own_name_id);
		dbus_helper->dbus_own_name_id = 0;
	}

	if (dbus_helper->query_interface != NULL) {
		g_dbus_interface_skeleton_unexport (dbus_helper->query_interface);
		g_clear_object (&dbus_helper->query_interface);
	}

	if (dbus_helper->modify_interface != NULL) {
		g_dbus_interface_skeleton_unexport (dbus_helper->modify_interface);
		g_clear_object (&dbus_helper->modify_interface);
	}

	if (dbus_helper->modify2_interface != NULL) {
		g_dbus_interface_skeleton_unexport (dbus_helper->modify2_interface);
		g_clear_object (&dbus_helper->modify2_interface);
	}

	g_clear_object (&dbus_helper->task);

	G_OBJECT_CLASS (gs_dbus_helper_parent_class)->dispose (object);
}

static void
gs_dbus_helper_class_init (GsDbusHelperClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = gs_dbus_helper_dispose;
}

GsDbusHelper *
gs_dbus_helper_new (void)
{
	return GS_DBUS_HELPER (g_object_new (GS_TYPE_DBUS_HELPER, NULL));
}
