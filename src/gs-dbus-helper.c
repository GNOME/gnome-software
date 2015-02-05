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

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <packagekit-glib2/packagekit.h>

#include "gs-cleanup.h"
#include "gs-dbus-helper.h"
#include "gs-packagekit-generated.h"
#include "gs-resources.h"

struct _GsDbusHelper {
	GObject			 parent;
	GCancellable		*cancellable;
	GDBusInterfaceSkeleton	*query_interface;
	PkTask			*task;
	guint			 dbus_own_name_id;
};

struct _GsDbusHelperClass {
	GObjectClass	 parent_class;
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

/**
 * gs_dbus_helper_task_free:
 **/
static void
gs_dbus_helper_task_free (GsDbusHelperTask *dtask)
{
	if (dtask->dbus_helper != NULL)
		g_object_unref (dtask->dbus_helper);

	g_free (dtask);
}

/**
 * gs_dbus_helper_task_set_interaction:
 **/
static void
gs_dbus_helper_task_set_interaction (GsDbusHelperTask *dtask, const gchar *interaction)
{
	guint i;
	_cleanup_strv_free_ gchar **interactions = NULL;

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

/**
 * gs_dbus_helper_progress_cb:
 **/
static void
gs_dbus_helper_progress_cb (PkProgress *progress, PkProgressType type, gpointer data)
{
}

/**
 * gs_dbus_helper_query_is_installed_cb:
 **/
static void
gs_dbus_helper_query_is_installed_cb (GObject *source, GAsyncResult *res, gpointer data)
{
	GsDbusHelperTask *dtask = (GsDbusHelperTask *) data;
	PkClient *client = PK_CLIENT (source);
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ PkError *error_code = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;
	_cleanup_ptrarray_unref_ GPtrArray *array = NULL;

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

/**
 * gs_dbus_helper_query_search_file_cb:
 **/
static void
gs_dbus_helper_query_search_file_cb (GObject *source, GAsyncResult *res, gpointer data)
{
	_cleanup_error_free_ GError *error = NULL;
	GsDbusHelperTask *dtask = (GsDbusHelperTask *) data;
	PkClient *client = PK_CLIENT (source);
	PkInfoEnum info;
	PkPackage *item;
	_cleanup_ptrarray_unref_ GPtrArray *array = NULL;
	_cleanup_object_unref_ PkError *error_code = NULL;
	_cleanup_object_unref_ PkResults *results = NULL;

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
	_cleanup_strv_free_ gchar **names = NULL;

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
	_cleanup_strv_free_ gchar **names = NULL;

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
	_cleanup_object_unref_ GDBusConnection *connection = NULL;
	_cleanup_error_free_ GError *error = NULL;

	connection = g_bus_get_finish (res, &error);
	if (connection == NULL) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("Could not get session bus: %s", error->message);
		return;
	}

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

	dbus_helper->dbus_own_name_id = g_bus_own_name_on_connection (connection,
	                                                              "org.freedesktop.PackageKit2",
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

	if (dbus_helper->cancellable != NULL) {
		g_cancellable_cancel (dbus_helper->cancellable);
		g_clear_object (&dbus_helper->cancellable);
	}

	if (dbus_helper->dbus_own_name_id != 0) {
		g_bus_unown_name (dbus_helper->dbus_own_name_id);
		dbus_helper->dbus_own_name_id = 0;
	}

	if (dbus_helper->query_interface != NULL) {
		g_dbus_interface_skeleton_unexport (dbus_helper->query_interface);
		g_clear_object (&dbus_helper->query_interface);
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

/* vim: set noexpandtab: */
