/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * SPDX-FileCopyrightText: (C) 2026 Red Hat <www.redhat.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <config.h>

#include <gio/gio.h>

#include "gs-software-offline-updates-generated.h"
#include "gs-software-offline-updates-provider.h"

typedef struct {
	GsSoftwareOfflineUpdatesProvider *self;
	GDBusMethodInvocation *invocation;
} PendingJobData;

struct _GsSoftwareOfflineUpdatesProvider {
	GObject parent;

	GsSoftwareOfflineUpdates *skeleton;
	GsPluginLoader *plugin_loader;
	GCancellable *cancellable;
};

G_DEFINE_TYPE (GsSoftwareOfflineUpdatesProvider, gs_software_offline_updates_provider, G_TYPE_OBJECT)

static void
pending_job_data_free (PendingJobData *data)
{
	g_object_unref (data->invocation);
	g_free (data);
}

static void
get_state_done_cb (GObject *source_object,
		   GAsyncResult *result,
		   gpointer user_data)
{
	PendingJobData *data = user_data;
	GsSoftwareOfflineUpdatesProvider *self = data->self;
	g_autoptr(GsPluginJobGetOfflineUpdateState) plugin_job = NULL;
	g_autoptr(GError) local_error = NULL;

	if (gs_plugin_loader_job_process_finish (self->plugin_loader, result, (GsPluginJob **) &plugin_job, &local_error)) {
		GsPluginOfflineUpdateState state = GS_PLUGIN_OFFLINE_UPDATE_STATE_NONE;
		const gchar *state_str;

		state = gs_plugin_job_get_offline_update_state_get_result (plugin_job);

		switch (state) {
		case GS_PLUGIN_OFFLINE_UPDATE_STATE_SCHEDULED:
			state_str = "scheduled";
			break;
		case GS_PLUGIN_OFFLINE_UPDATE_STATE_NONE:
		default:
			state_str = "none";
			break;
		}
		gs_software_offline_updates_complete_get_state (self->skeleton, data->invocation, state_str);
	} else {
		g_prefix_error_literal (&local_error, "Failed to get offline update state: ");
		g_debug ("%s", local_error->message);
		g_dbus_method_invocation_return_gerror (data->invocation, local_error);
	}

	pending_job_data_free (data);
	g_application_release (g_application_get_default ());
}

static gboolean
handle_get_state (GsSoftwareOfflineUpdates *skeleton,
		  GDBusMethodInvocation *invocation,
		  gpointer user_data)
{
	GsSoftwareOfflineUpdatesProvider *self = user_data;
	GDBusMessage *message = g_dbus_method_invocation_get_message (invocation);
	GsPluginGetOfflineUpdateStateFlags flags = GS_PLUGIN_GET_OFFLINE_UPDATE_STATE_FLAGS_NONE;
	PendingJobData *data;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	if ((g_dbus_message_get_flags (message) & G_DBUS_MESSAGE_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION) != 0)
		flags |= GS_PLUGIN_GET_OFFLINE_UPDATE_STATE_FLAGS_INTERACTIVE;

	g_application_hold (g_application_get_default ());

	data = g_new0 (PendingJobData, 1);
	data->self = self;
	data->invocation = g_object_ref (invocation);

	plugin_job = gs_plugin_job_get_offline_update_state_new (flags);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable,
					    get_state_done_cb,
					    data);

	return TRUE;
}

static void
cancel_done_cb (GObject *source_object,
		GAsyncResult *result,
		gpointer user_data)
{
	PendingJobData *data = user_data;
	GsSoftwareOfflineUpdatesProvider *self = data->self;
	g_autoptr(GError) local_error = NULL;

	if (gs_plugin_loader_job_process_finish (self->plugin_loader, result, NULL, &local_error)) {
		gs_software_offline_updates_complete_cancel (self->skeleton, data->invocation);
	} else {
		g_prefix_error_literal (&local_error, "Failed to cancel offline update: ");
		g_debug ("%s", local_error->message);
		g_dbus_method_invocation_return_gerror (data->invocation, local_error);
	}

	pending_job_data_free (data);
	g_application_release (g_application_get_default ());
}

static gboolean
handle_cancel (GsSoftwareOfflineUpdates *skeleton,
	       GDBusMethodInvocation *invocation,
	       gpointer user_data)
{
	GsSoftwareOfflineUpdatesProvider *self = user_data;
	GDBusMessage *message = g_dbus_method_invocation_get_message (invocation);
	GsPluginCancelOfflineUpdateFlags flags = GS_PLUGIN_CANCEL_OFFLINE_UPDATE_FLAGS_NONE;
	PendingJobData *data;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	if ((g_dbus_message_get_flags (message) & G_DBUS_MESSAGE_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION) != 0)
		flags |= GS_PLUGIN_CANCEL_OFFLINE_UPDATE_FLAGS_INTERACTIVE;

	g_application_hold (g_application_get_default ());

	data = g_new0 (PendingJobData, 1);
	data->self = self;
	data->invocation = g_object_ref (invocation);

	plugin_job = gs_plugin_job_cancel_offline_update_new (flags);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable,
					    cancel_done_cb,
					    data);

	return TRUE;
}

static void
set_action_done_cb (GObject *source_object,
		    GAsyncResult *result,
		    gpointer user_data)
{
	PendingJobData *data = user_data;
	GsSoftwareOfflineUpdatesProvider *self = data->self;
	g_autoptr(GError) local_error = NULL;

	if (gs_plugin_loader_job_process_finish (self->plugin_loader, result, NULL, &local_error)) {
		gs_software_offline_updates_complete_set_action (self->skeleton, data->invocation);
	} else {
		g_prefix_error_literal (&local_error, "Failed to set offline update action: ");
		g_debug ("%s", local_error->message);
		g_dbus_method_invocation_return_gerror (data->invocation, local_error);
	}

	pending_job_data_free (data);
	g_application_release (g_application_get_default ());
}

static gboolean
handle_set_action (GsSoftwareOfflineUpdates *skeleton,
		   GDBusMethodInvocation *invocation,
		   const gchar *action,
		   gpointer user_data)
{
	GsSoftwareOfflineUpdatesProvider *self = user_data;
	GDBusMessage *message = g_dbus_method_invocation_get_message (invocation);
	GsPluginSetOfflineUpdateActionFlags flags = GS_PLUGIN_SET_OFFLINE_UPDATE_ACTION_FLAGS_NONE;
	PendingJobData *data;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	if (g_strcmp0 (action, "reboot") == 0) {
		flags |= GS_PLUGIN_SET_OFFLINE_UPDATE_ACTION_FLAGS_REBOOT;
	} else if (g_strcmp0 (action, "shutdown") == 0) {
		flags |= GS_PLUGIN_SET_OFFLINE_UPDATE_ACTION_FLAGS_SHUTDOWN;
	} else {
		g_debug ("Unknown offline update action '%s', expects 'reboot' or 'shutdown'", action);
		g_dbus_method_invocation_return_error (invocation, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
						       "Unknown offline update action '%s', expects 'reboot' or 'shutdown'",
						       action);
		return TRUE;
	}

	if ((g_dbus_message_get_flags (message) & G_DBUS_MESSAGE_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION) != 0)
		flags |= GS_PLUGIN_SET_OFFLINE_UPDATE_ACTION_FLAGS_INTERACTIVE;

	g_application_hold (g_application_get_default ());

	data = g_new0 (PendingJobData, 1);
	data->self = self;
	data->invocation = g_object_ref (invocation);

	plugin_job = gs_plugin_job_set_offline_update_action_new (flags);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable,
					    set_action_done_cb,
					    data);

	return TRUE;
}

static void
gs_software_offline_updates_provider_dispose (GObject *obj)
{
	GsSoftwareOfflineUpdatesProvider *self = GS_SOFTWARE_OFFLINE_UPDATES_PROVIDER (obj);

	g_cancellable_cancel (self->cancellable);
	g_clear_object (&self->cancellable);

	g_clear_object (&self->plugin_loader);
	g_clear_object (&self->skeleton);

	G_OBJECT_CLASS (gs_software_offline_updates_provider_parent_class)->dispose (obj);
}

static void
gs_software_offline_updates_provider_init (GsSoftwareOfflineUpdatesProvider *self)
{
	self->skeleton = gs_software_offline_updates_skeleton_new ();

	g_signal_connect (self->skeleton, "handle-get-state",
			  G_CALLBACK (handle_get_state), self);
	g_signal_connect (self->skeleton, "handle-cancel",
			  G_CALLBACK (handle_cancel), self);
	g_signal_connect (self->skeleton, "handle-set-action",
			  G_CALLBACK (handle_set_action), self);
}

static void
gs_software_offline_updates_provider_class_init (GsSoftwareOfflineUpdatesProviderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = gs_software_offline_updates_provider_dispose;
}

GsSoftwareOfflineUpdatesProvider *
gs_software_offline_updates_provider_new (void)
{
	return g_object_new (gs_software_offline_updates_provider_get_type (), NULL);
}

void
gs_software_offline_updates_provider_setup (GsSoftwareOfflineUpdatesProvider *self,
					    GsPluginLoader *loader)
{
	self->plugin_loader = g_object_ref (loader);
}

gboolean
gs_software_offline_updates_provider_register (GsSoftwareOfflineUpdatesProvider *self,
					       GDBusConnection *connection,
					       GError **error)
{
	return g_dbus_interface_skeleton_export (G_DBUS_INTERFACE_SKELETON (self->skeleton),
	                                         connection,
	                                         "/org/gnome/Software/OfflineUpdates", error);
}

void
gs_software_offline_updates_provider_unregister (GsSoftwareOfflineUpdatesProvider *self)
{
	g_dbus_interface_skeleton_unexport (G_DBUS_INTERFACE_SKELETON (self->skeleton));
}
