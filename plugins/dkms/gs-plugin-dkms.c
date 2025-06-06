/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * SECTION:
 * Helps to manage DKMS and akmods key
 *
 * The DKMS and akmods key needs to be installed in the MOK, thus any drivers using them
 * can be properly signed and used by the kernel when the Secure Boot is enabled.
 *
 * This plugin code is not enough, there are also some GUI changes needed, which
 * cannot be done on the plugin side, thus the overall code is split into the several
 * parts. The plugin goes into the action only when needed, which means it does
 * nothing when the Secure Boot is not enabled on the machine. Then there's checked
 * whether the key is available and whether it's enrolled in the MOK. That's
 * done only if there's found an installed application, which requires the key.
 * All such apps are marked with a helper flag that they need special attention during
 * the refine, which the GUI part recognizes and modifies the GUI accordingly.
 * There are two metainfo keys considered, one is "GnomeSoftware:requires-dkms-key",
 * to operate with the DKMS key and the other is "GnomeSoftware:requires-akmods-key",
 * to operate with the akmods key. One app should not have set both keys.
 *
 * It follows the procedure of installing the akmods key as described here:
 * https://src.fedoraproject.org/rpms/akmods/blob/f40/f/README.secureboot
 * only by simulating user input with a GUI front end, not on the command line.
 *
 * This plugin runs entirely in the main thread, deferring the bulk of its work
 * to a `gnome-software-dkms-helper` subprocess, which it communicates with
 * asynchronously. No locking is required.
 */
#include "config.h"

#include <glib/gi18n-lib.h>
#include <gnome-software.h>

#include "gs-dkms-private.h"
#include "gs-worker-thread.h"

#include "gs-plugin-dkms.h"

struct _GsPluginDkms
{
	GsPlugin	parent;

	gboolean	did_notify;
};

G_DEFINE_TYPE (GsPluginDkms, gs_plugin_dkms, GS_TYPE_PLUGIN)

static void
gs_dkms_got_secureboot_state_cb (GObject *source_object,
				 GAsyncResult *result,
				 gpointer user_data)
{
	g_autoptr(GTask) task = user_data;
	g_autoptr(GError) local_error = NULL;
	GsSecurebootState sb_state;

	sb_state = gs_dkms_get_secureboot_state_finish (result, &local_error);
	if (sb_state == GS_SECUREBOOT_STATE_DISABLED ||
	    sb_state == GS_SECUREBOOT_STATE_NOT_SUPPORTED) {
		g_debug ("Disabling plugin, because Secure Boot is %s", sb_state == GS_SECUREBOOT_STATE_DISABLED ? "disabled" : "not supported");
		gs_plugin_set_enabled (GS_PLUGIN (g_task_get_source_object (task)), FALSE);
	}

	if (local_error != NULL)
		g_debug ("%s: %s", G_STRFUNC, local_error->message);

	/* do not pass the error to the caller, it's okay when the mokutil cannot be found */
	g_task_return_boolean (task, TRUE);
}

static void
gs_plugin_dkms_reload (GsPlugin *plugin)
{
	if (gs_dkms_get_last_secureboot_state () == GS_SECUREBOOT_STATE_UNKNOWN) {
		g_autoptr(GTask) task = NULL;

		/* only need the plugin, to disable it when Secure Boot is disabled or not supported */
		task = g_task_new (plugin, NULL, NULL, NULL);
		g_task_set_source_tag (task, gs_plugin_dkms_reload);

		/* mokutil was not installed probably; the reload can be called when some
		   app/package had been installed, thus re-try to check Secure Boot state */
		gs_dkms_get_secureboot_state_async (NULL, gs_dkms_got_secureboot_state_cb, g_steal_pointer (&task));
	}
}

static void
gs_plugin_dkms_setup_async (GsPlugin            *plugin,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
	g_autoptr(GTask) task = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dkms_setup_async);

	gs_dkms_get_secureboot_state_async (cancellable, gs_dkms_got_secureboot_state_cb, g_steal_pointer (&task));
}

static gboolean
gs_plugin_dkms_setup_finish (GsPlugin      *plugin,
                             GAsyncResult  *result,
                             GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

typedef struct {
	GsAppList *list; /* (owned) */
	gboolean requires_akmods_key;
	gboolean requires_dkms_key;
	GsDkmsState akmods_key_state;
	GsDkmsState dkms_key_state;
} GsPluginDkmsRefineData;

static void
gs_plugin_dkms_refine_data_free (GsPluginDkmsRefineData *data)
{
	g_clear_object (&data->list);
	g_free (data);
}

static void
gs_dkms_complete_refine_task (GTask *in_task)
{
	g_autoptr(GTask) task = in_task;
	g_autoptr(GsApp) notify_for_app = NULL;
	GsPluginDkms *self = GS_PLUGIN_DKMS (g_task_get_source_object (task));
	GsPluginDkmsRefineData *data = g_task_get_task_data (task);
	GsAppList *list = data->list;

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		gboolean check_akmods_key, check_dkms_key;
		check_akmods_key = g_strcmp0 (gs_app_get_metadata_item (app, "GnomeSoftware::requires-akmods-key"), "True") == 0;
		check_dkms_key = g_strcmp0 (gs_app_get_metadata_item (app, "GnomeSoftware::requires-dkms-key"), "True") == 0;
		if (!check_akmods_key && !check_dkms_key)
			continue;
		if (!gs_app_is_installed (app) &&
		    gs_app_get_state (app) != GS_APP_STATE_PENDING_INSTALL)
			continue;
		if ((check_akmods_key && data->akmods_key_state == GS_DKMS_STATE_ENROLLED) ||
		    (check_dkms_key && data->dkms_key_state == GS_DKMS_STATE_ENROLLED)) {
			gs_app_remove_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
		} else {
			/* only restart is missing, thus do not bother the user with the MOK password */
			gs_app_set_mok_key_pending (app,
						    (check_akmods_key && data->akmods_key_state == GS_DKMS_STATE_PENDING) ||
						    (check_dkms_key && data->dkms_key_state == GS_DKMS_STATE_PENDING));
			gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
			gs_app_set_state (app, GS_APP_STATE_PENDING_INSTALL);

			if (notify_for_app == NULL && !self->did_notify)
				notify_for_app = g_object_ref (app);
		}
	}

	if (notify_for_app != NULL) {
		GApplication *application;

		self->did_notify = TRUE;

		application = g_application_get_default ();

		if (G_IS_APPLICATION (application)) {
			g_autoptr(GNotification) notif = NULL;
			g_autofree gchar *summary = NULL;
			g_autofree gchar *body = NULL;

			/* Translators: The "%s" is replaced with an app name, like "NVIDIA Linux Graphics Driver".
			   This is the first part of a system notification. */
			summary = g_strdup_printf (_("%s Ready"), gs_app_get_name (notify_for_app));
			/* Translators: The "%s" is replaced with an app name, like "NVIDIA Linux Graphics Driver".
			   This is the second part of a system notification, which looks like:

			   NVIDIA Linux Graphics Driver Ready

			   The NVIDIA Linux Graphics Driver is ready to be enabled and staged for the next boot.
			*/
			body = g_strdup_printf (_("The %s is ready to be enabled and staged for the next boot."), gs_app_get_name (notify_for_app));
			notif = g_notification_new (summary);
			g_notification_set_body (notif, body);
			g_notification_set_default_action_and_target (notif, "app.details", "(ss)",
								      gs_app_get_unique_id (notify_for_app), "");
			g_notification_add_button_with_target (notif, _("Enable"), "app.details", "(ss)",
							       gs_app_get_unique_id (notify_for_app), "");
			g_application_send_notification (G_APPLICATION (application), "dkms-key-pending", notif);
		}
	}

	g_task_return_boolean (task, TRUE);
}

static void
gs_dkms_got_dkms_key_state_refine_cb (GObject *source_object,
				      GAsyncResult *result,
				      gpointer user_data)
{
	g_autoptr(GTask) task = user_data;
	g_autoptr(GError) local_error = NULL;
	GsPluginDkmsRefineData *data = g_task_get_task_data (task);

	data->dkms_key_state = gs_dkms_get_key_state_finish (result, &local_error);
	if (local_error != NULL || data->dkms_key_state == GS_DKMS_STATE_ERROR) {
		g_debug ("%s: Failed to get DKMS key state: %s", G_STRFUNC, local_error != NULL ? local_error->message : "Unknown error");

		if (!data->requires_akmods_key) {
			/* ignore when the DKMS key state cannot be determined and akmods is not needed */
			g_task_return_boolean (task, TRUE);
			return;
		}
	}

	gs_dkms_complete_refine_task (g_steal_pointer (&task));
}

static void
gs_dkms_got_akmods_key_state_refine_cb (GObject *source_object,
					GAsyncResult *result,
					gpointer user_data)
{
	g_autoptr(GTask) task = user_data;
	g_autoptr(GError) local_error = NULL;
	GsPluginDkmsRefineData *data = g_task_get_task_data (task);

	data->akmods_key_state = gs_dkms_get_key_state_finish (result, &local_error);
	if (local_error != NULL || data->akmods_key_state == GS_DKMS_STATE_ERROR) {
		g_debug ("%s: Failed to get akmods key state: %s", G_STRFUNC, local_error != NULL ? local_error->message : "Unknown error");

		if (!data->requires_dkms_key) {
			/* ignore when the akmods key state cannot be determined and DKMS is not needed */
			g_task_return_boolean (task, TRUE);
			return;
		}
	}

	if (data->requires_dkms_key) {
		GCancellable *cancellable = g_task_get_cancellable (task);
		gs_dkms_get_key_state_async (GS_DKMS_KEY_KIND_DKMS, cancellable, gs_dkms_got_dkms_key_state_refine_cb, g_steal_pointer (&task));
	} else {
		gs_dkms_complete_refine_task (g_steal_pointer (&task));
	}
}

static void
gs_dkms_got_secureboot_state_refine_cb (GObject *source_object,
					GAsyncResult *result,
					gpointer user_data)
{
	g_autoptr(GTask) task = user_data;
	GsPluginDkmsRefineData *data = g_task_get_task_data (task);
	GsSecurebootState sb_state;

	sb_state = gs_dkms_get_secureboot_state_finish (result, NULL);
	if (sb_state == GS_SECUREBOOT_STATE_ENABLED) {
		GCancellable *cancellable = g_task_get_cancellable (task);
		if (data->requires_akmods_key) {
			gs_dkms_get_key_state_async (GS_DKMS_KEY_KIND_AKMODS, cancellable, gs_dkms_got_akmods_key_state_refine_cb, g_steal_pointer (&task));
		} else {
			g_assert (data->requires_dkms_key);
			gs_dkms_get_key_state_async (GS_DKMS_KEY_KIND_DKMS, cancellable, gs_dkms_got_dkms_key_state_refine_cb, g_steal_pointer (&task));
		}
	} else {
		g_task_return_boolean (task, TRUE);
	}
}

static void
gs_plugin_dkms_refine_async (GsPlugin                   *plugin,
                             GsAppList                  *list,
                             GsPluginRefineFlags         job_flags,
                             GsPluginRefineRequireFlags  require_flags,
                             GsPluginEventCallback       event_callback,
                             void                       *event_user_data,
                             GCancellable               *cancellable,
                             GAsyncReadyCallback         callback,
                             gpointer                    user_data)
{
	g_autoptr(GTask) task = NULL;
	gboolean requires_akmods_key = FALSE;
	gboolean requires_dkms_key = FALSE;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_dkms_refine_async);

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		gboolean check_akmods_key, check_dkms_key;
		check_akmods_key = !requires_akmods_key &&
				   g_strcmp0 (gs_app_get_metadata_item (app, "GnomeSoftware::requires-akmods-key"), "True") == 0;
		check_dkms_key = !requires_dkms_key &&
				 g_strcmp0 (gs_app_get_metadata_item (app, "GnomeSoftware::requires-dkms-key"), "True") == 0;
		if (!check_akmods_key && !check_dkms_key)
			continue;
		if (!gs_app_is_installed (app) &&
		    gs_app_get_state (app) != GS_APP_STATE_PENDING_INSTALL)
			continue;
		requires_akmods_key = requires_akmods_key || check_akmods_key;
		requires_dkms_key = requires_dkms_key || check_dkms_key;
	}

	if (requires_akmods_key || requires_dkms_key) {
		GsPluginDkmsRefineData *data;
		data = g_new0 (GsPluginDkmsRefineData, 1);
		data->list = g_object_ref (list);
		data->requires_akmods_key = requires_akmods_key;
		data->requires_dkms_key = requires_dkms_key;
		g_task_set_task_data (task, data, (GDestroyNotify) gs_plugin_dkms_refine_data_free);
		gs_dkms_get_secureboot_state_async (cancellable, gs_dkms_got_secureboot_state_refine_cb, g_steal_pointer (&task));
	} else {
		g_task_return_boolean (task, TRUE);
	}
}

static gboolean
gs_plugin_dkms_refine_finish (GsPlugin      *plugin,
			      GAsyncResult  *result,
			      GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_dkms_init (GsPluginDkms *self)
{
	GsPlugin *plugin = GS_PLUGIN (self);

	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "rpm-ostree");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "flatpak");
}

static void
gs_plugin_dkms_class_init (GsPluginDkmsClass *klass)
{
	GsPluginClass *plugin_class;

	plugin_class = GS_PLUGIN_CLASS (klass);
	plugin_class->reload = gs_plugin_dkms_reload;
	plugin_class->setup_async = gs_plugin_dkms_setup_async;
	plugin_class->setup_finish = gs_plugin_dkms_setup_finish;
	plugin_class->refine_async = gs_plugin_dkms_refine_async;
	plugin_class->refine_finish = gs_plugin_dkms_refine_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_DKMS;
}
