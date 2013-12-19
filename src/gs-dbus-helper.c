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

#include "gs-dbus-helper.h"
#include "gs-resources.h"

struct _GsDbusHelper {
	GObject			 parent;
	GCancellable		*cancellable;
	GDBusNodeInfo		*introspection;
	PkTask			*task;
	guint			 owner_id;
};

struct _GsDbusHelperClass {
	GObjectClass	 parent_class;
};

G_DEFINE_TYPE (GsDbusHelper, gs_dbus_helper, G_TYPE_OBJECT)

typedef struct {
	GDBusMethodInvocation	*invocation;
} GsDbusHelperTask;

/**
 * gs_dbus_helper_task_free:
 **/
static void
gs_dbus_helper_task_free (GsDbusHelperTask *dtask)
{
	g_free (dtask);
}

/**
 * gs_dbus_helper_progress_cb:
 **/
static void
gs_dbus_helper_progress_cb (PkProgress *progress, PkProgressType type, gpointer data)
{
}

/**
 * gs_dbus_helper_is_installed_resolve_cb:
 **/
static void
gs_dbus_helper_is_installed_resolve_cb (GObject *source, GAsyncResult *res, gpointer data)
{
	GError *error = NULL;
	GPtrArray *array = NULL;
	GsDbusHelperTask *dtask = (GsDbusHelperTask *) data;
	PkClient *client = PK_CLIENT (source);
	PkError *error_code = NULL;
	PkResults *results = NULL;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		g_dbus_method_invocation_return_error (dtask->invocation,
						       G_IO_ERROR,
						       G_IO_ERROR_INVALID_ARGUMENT,
						       "failed to resolve: %s",
						       error->message);
		g_error_free (error);
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
	g_dbus_method_invocation_return_value (dtask->invocation,
					       g_variant_new ("(b)", array->len > 0));
out:
	gs_dbus_helper_task_free (dtask);
	if (error_code != NULL)
		g_object_unref (error_code);
	if (array != NULL)
		g_ptr_array_unref (array);
	if (results != NULL)
		g_object_unref (results);
}

static void
gs_dbus_helper_handle_method_call_query (GsDbusHelper *dbus_helper,
					 const gchar *method_name,
					 GVariant *parameters,
					 GDBusMethodInvocation *invocation)
{
	gchar **package_names;
	const gchar *package_name;
	const gchar *interaction;
	GsDbusHelperTask *dtask;

	if (g_strcmp0 (method_name, "IsInstalled") == 0) {
		g_variant_get (parameters, "(&s&s)",
			       &package_name, &interaction);
		package_names = g_strsplit (package_name, "|", 1);
		dtask = g_new0 (GsDbusHelperTask, 1);
		dtask->invocation = invocation;
		pk_client_resolve_async (PK_CLIENT (dbus_helper->task),
					 pk_bitfield_value (PK_FILTER_ENUM_INSTALLED),
					 package_names, NULL,
					 gs_dbus_helper_progress_cb, dtask,
					 gs_dbus_helper_is_installed_resolve_cb, dtask);
		g_strfreev (package_names);
	} else {
		g_dbus_method_invocation_return_error (invocation,
						       G_IO_ERROR,
						       G_IO_ERROR_INVALID_ARGUMENT,
						       "method %s not implemented "
						       "by gnome-software",
						       method_name);
	}
}

static void
gs_dbus_helper_handle_method_call_modify (GsDbusHelper *dbus_helper,
					  const gchar *method_name,
					  GVariant *parameters,
					  GDBusMethodInvocation *invocation)
{
	g_dbus_method_invocation_return_error (invocation,
					       G_IO_ERROR,
					       G_IO_ERROR_INVALID_ARGUMENT,
					       "method %s not implemented by gnome-software",
					       method_name);
}

static void
gs_dbus_helper_handle_method_call (GDBusConnection *connection,
				   const gchar *sender,
				   const gchar *object_path,
				   const gchar *interface_name,
				   const gchar *method_name,
				   GVariant *parameters,
				   GDBusMethodInvocation *invocation,
				   gpointer user_data)
{
	GsDbusHelper *dbus_helper = GS_DBUS_HELPER (user_data);
	if (g_strcmp0 (interface_name, "org.freedesktop.PackageKit.Query") == 0) {
		gs_dbus_helper_handle_method_call_query (dbus_helper,
							 method_name,
							 parameters,
							 invocation);
	} else if (g_strcmp0 (interface_name, "org.freedesktop.PackageKit.Modify") == 0) {
		gs_dbus_helper_handle_method_call_modify (dbus_helper,
							  method_name,
							  parameters,
							  invocation);
	} else {
		g_dbus_method_invocation_return_error (invocation,
						       G_IO_ERROR,
						       G_IO_ERROR_FAILED_HANDLED,
						       "Interface not handled");
	}
}

static const GDBusInterfaceVTable interface_vtable =
{
	gs_dbus_helper_handle_method_call,
	NULL,
	NULL
};

static void
gs_dbus_helper_bus_acquired_cb (GDBusConnection *connection,
				const gchar *name,
				gpointer user_data)
{
	guint id;
	guint i;
	GsDbusHelper *dbus_helper = GS_DBUS_HELPER (user_data);

	for (i = 0; dbus_helper->introspection->interfaces[i] != NULL; i++) {
		id = g_dbus_connection_register_object (connection,
							"/org/freedesktop/PackageKit",
							dbus_helper->introspection->interfaces[i],
							&interface_vtable,
							dbus_helper,  /* user_data */
							NULL,  /* user_data_free_func */
							NULL); /* GError** */
		g_assert (id > 0);
	}
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
gs_dbus_helper_init (GsDbusHelper *dbus_helper)
{
	GBytes *data;
	const gchar *xml;

	dbus_helper->task = pk_task_new ();
	dbus_helper->cancellable = g_cancellable_new ();

	/* load introspection */
	data = g_resource_lookup_data (gs_get_resource (),
				       "/org/gnome/software/org.freedesktop.PackageKit.xml",
				       G_RESOURCE_LOOKUP_FLAGS_NONE,
				       NULL);
	xml = g_bytes_get_data (data, NULL);
	dbus_helper->introspection = g_dbus_node_info_new_for_xml (xml, NULL);
	g_assert (dbus_helper->introspection != NULL);
	g_bytes_unref (data);

	/* own session daemon */
	dbus_helper->owner_id = g_bus_own_name (G_BUS_TYPE_SESSION,
						"org.freedesktop.PackageKit2",
						G_BUS_NAME_OWNER_FLAGS_NONE,
						gs_dbus_helper_bus_acquired_cb,
						gs_dbus_helper_name_acquired_cb,
						gs_dbus_helper_name_lost_cb,
						dbus_helper,
						NULL);
}

static void
gs_dbus_helper_finalize (GObject *object)
{
	GsDbusHelper *dbus_helper = GS_DBUS_HELPER (object);

	g_cancellable_cancel (dbus_helper->cancellable);
	g_bus_unown_name (dbus_helper->owner_id);

	g_dbus_node_info_unref (dbus_helper->introspection);
	g_clear_object (&dbus_helper->cancellable);
	g_clear_object (&dbus_helper->task);

	G_OBJECT_CLASS (gs_dbus_helper_parent_class)->finalize (object);
}

static void
gs_dbus_helper_class_init (GsDbusHelperClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_dbus_helper_finalize;
}

GsDbusHelper *
gs_dbus_helper_new (void)
{
	return GS_DBUS_HELPER (g_object_new (GS_TYPE_DBUS_HELPER, NULL));
}

/* vim: set noexpandtab: */
