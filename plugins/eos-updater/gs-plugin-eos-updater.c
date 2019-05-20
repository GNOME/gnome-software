/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2019 Endless Mobile, Inc
 *
 * Authors:
 *   Joaquim Rocha <jrocha@endlessm.com>
 *   Philip Withnall <withnall@endlessm.com>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <config.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib-object.h>
#include <gnome-software.h>
#include <gs-plugin.h>
#include <gs-utils.h>
#include <math.h>
#include <ostree.h>

#include "gs-eos-updater-generated.h"

/*
 * SECTION:
 * Plugin to improve GNOME Software integration in the EOS desktop.
 */

typedef enum {
	EOS_UPDATER_STATE_NONE = 0,
	EOS_UPDATER_STATE_READY,
	EOS_UPDATER_STATE_ERROR,
	EOS_UPDATER_STATE_POLLING,
	EOS_UPDATER_STATE_UPDATE_AVAILABLE,
	EOS_UPDATER_STATE_FETCHING,
	EOS_UPDATER_STATE_UPDATE_READY,
	EOS_UPDATER_STATE_APPLYING_UPDATE,
	EOS_UPDATER_STATE_UPDATE_APPLIED,
} EosUpdaterState;
#define EOS_UPDATER_N_STATES (EOS_UPDATER_STATE_UPDATE_APPLIED + 1)

static const gchar *
eos_updater_state_to_str (EosUpdaterState state)
{
	const gchar * const eos_updater_state_str[] = {
		"None",
		"Ready",
		"Error",
		"Polling",
		"UpdateAvailable",
		"Fetching",
		"UpdateReady",
		"ApplyingUpdate",
		"UpdateApplied",
	};

	G_STATIC_ASSERT (G_N_ELEMENTS (eos_updater_state_str) == EOS_UPDATER_N_STATES);

	g_return_val_if_fail ((gint) state < EOS_UPDATER_N_STATES, "unknown");
	return eos_updater_state_str[state];
}

static void
gs_eos_updater_error_convert (GError **perror)
{
	GError *error = perror != NULL ? *perror : NULL;

	/* not set */
	if (error == NULL)
		return;

	/* parse remote eos-updater error */
	if (g_dbus_error_is_remote_error (error)) {
		g_autofree gchar *remote_error = g_dbus_error_get_remote_error (error);

		g_dbus_error_strip_remote_error (error);

		if (g_str_equal (remote_error, "com.endlessm.Updater.Error.WrongState")) {
			error->code = GS_PLUGIN_ERROR_FAILED;
		} else if (g_str_equal (remote_error, "com.endlessm.Updater.Error.LiveBoot") ||
			   g_str_equal (remote_error, "com.endlessm.Updater.Error.NotOstreeSystem") ||
			   g_str_equal (remote_error, "org.freedesktop.DBus.Error.ServiceUnknown")) {
			error->code = GS_PLUGIN_ERROR_NOT_SUPPORTED;
		} else if (g_str_equal (remote_error, "com.endlessm.Updater.Error.WrongConfiguration")) {
			error->code = GS_PLUGIN_ERROR_FAILED;
		} else if (g_str_equal (remote_error, "com.endlessm.Updater.Error.Fetching")) {
			error->code = GS_PLUGIN_ERROR_DOWNLOAD_FAILED;
		} else if (g_str_equal (remote_error, "com.endlessm.Updater.Error.MalformedAutoinstallSpec") ||
			   g_str_equal (remote_error, "com.endlessm.Updater.Error.UnknownEntryInAutoinstallSpec") ||
			   g_str_equal (remote_error, "com.endlessm.Updater.Error.FlatpakRemoteConflict")) {
			error->code = GS_PLUGIN_ERROR_FAILED;
		} else if (g_str_equal (remote_error, "com.endlessm.Updater.Error.MeteredConnection")) {
			error->code = GS_PLUGIN_ERROR_NO_NETWORK;
		} else if (g_str_equal (remote_error, "com.endlessm.Updater.Error.Cancelled")) {
			error->code = GS_PLUGIN_ERROR_CANCELLED;
		} else {
			g_warning ("Can’t reliably fixup remote error ‘%s’", remote_error);
			error->code = GS_PLUGIN_ERROR_FAILED;
		}
		error->domain = GS_PLUGIN_ERROR;
		return;
	}

	/* this is allowed for low-level errors */
	if (gs_utils_error_convert_gio (perror))
		return;

	/* this is allowed for low-level errors */
	if (gs_utils_error_convert_gdbus (perror))
		return;
}

#define EOS_UPGRADE_ID "com.endlessm.EOS.upgrade"

/* the percentage of the progress bar to use for applying the OS upgrade;
 * we need to fake the progress in this percentage because applying the OS upgrade
 * can take a long time and we don't want the user to think that the upgrade has
 * stalled */
#define EOS_UPGRADE_APPLY_PROGRESS_RANGE 25 /* percentage */
#define EOS_UPGRADE_APPLY_MAX_TIME 600.0 /* sec */
#define EOS_UPGRADE_APPLY_STEP_TIME 0.250 /* sec */

static gboolean setup_os_upgrade (GsPlugin *plugin, GCancellable *cancellable, GError **error);
static gboolean sync_state_from_updater (GsPlugin *plugin, GCancellable *cancellable, GError **error);

struct GsPluginData
{
	GsEosUpdater *updater_proxy;  /* (owned) */
	GsApp *os_upgrade;  /* (owned) */
	GCancellable *os_upgrade_cancellable;  /* (nullable) (owned) */
	gulong os_upgrade_cancelled_id;
	gfloat upgrade_fake_progress;
	guint upgrade_fake_progress_handler;
};

static void
os_upgrade_cancelled_cb (GCancellable *cancellable,
			 GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	g_debug ("%s: Cancelling upgrade", G_STRFUNC);
	gs_eos_updater_call_cancel (priv->updater_proxy, NULL, NULL, NULL);
}

static void
os_upgrade_set_cancellable (GsPlugin *plugin, GCancellable *cancellable)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	if (priv->os_upgrade_cancellable != NULL &&
	    priv->os_upgrade_cancelled_id != 0) {
		g_cancellable_disconnect (priv->os_upgrade_cancellable,
					  priv->os_upgrade_cancelled_id);
	}
	g_clear_object (&priv->os_upgrade_cancellable);

	if (cancellable != NULL) {
		priv->os_upgrade_cancellable = g_object_ref (cancellable);
		priv->os_upgrade_cancelled_id =
			g_cancellable_connect (priv->os_upgrade_cancellable,
					       G_CALLBACK (os_upgrade_cancelled_cb),
					       plugin, NULL);
	}
}

static void
app_ensure_set_metadata_variant (GsApp *app, const gchar *key, GVariant *var)
{
	/* we need to assign it to NULL in order to be able to override it
	 * (safeguard mechanism in GsApp...) */
	gs_app_set_metadata_variant (app, key, NULL);
	gs_app_set_metadata_variant (app, key, var);
}

static void
os_upgrade_set_download_by_user (GsApp *app, gboolean value)
{
	g_autoptr(GVariant) var = g_variant_new_boolean (value);

	g_debug ("%s: %s", G_STRFUNC, value ? "true" : "false");

	app_ensure_set_metadata_variant (app, "eos::DownloadByUser", var);
}

static gboolean
os_upgrade_get_download_by_user (GsApp *app)
{
	GVariant *value = gs_app_get_metadata_variant (app, "eos::DownloadByUser");
	if (value == NULL)
		return FALSE;
	return g_variant_get_boolean (value);
}

static gboolean
should_add_os_upgrade (AsAppState state)
{
	switch (state) {
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_AVAILABLE_LOCAL:
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
	case AS_APP_STATE_INSTALLING:
	case AS_APP_STATE_UPDATABLE_LIVE:
	case AS_APP_STATE_PURCHASABLE:
	case AS_APP_STATE_PURCHASING:
		return TRUE;
	case AS_APP_STATE_UNKNOWN:
	case AS_APP_STATE_INSTALLED:
	case AS_APP_STATE_UNAVAILABLE:
	case AS_APP_STATE_REMOVING:
	default:
		return FALSE;
	}
}

/* Wrapper around gs_app_set_state() which ensures we also notify of update
 * changes if we change between non-upgradable and upgradable states, so that
 * the app is notified to appear in the UI. */
static void
app_set_state (GsPlugin   *plugin,
               GsApp      *app,
               AsAppState  new_state)
{
	AsAppState old_state = gs_app_get_state (app);

	if (new_state == old_state)
		return;

	gs_app_set_state (app, new_state);

	if (should_add_os_upgrade (old_state) !=
	    should_add_os_upgrade (new_state)) {
		g_debug ("%s: Calling gs_plugin_updates_changed()", G_STRFUNC);
		gs_plugin_updates_changed (plugin);
	}
}

static gboolean
eos_updater_error_is_cancelled (const gchar *error_name)
{
	return (g_strcmp0 (error_name, "com.endlessm.Updater.Error.Cancelled") == 0);
}

static void
updater_state_changed (GsPlugin *plugin)
{
	g_autoptr(GError) local_error = NULL;

	g_debug ("%s", G_STRFUNC);

	if (!sync_state_from_updater (plugin, NULL, &local_error)) {
		GsPluginData *priv = gs_plugin_get_data (plugin);
		GsApp *app = priv->os_upgrade;

		g_warning ("Error syncing state from updater: %s", local_error->message);

		/* only set up an error to be shown to the user if the user had
		 * manually started the upgrade, and if the error in question
		 * is not originated by the user canceling the upgrade; errors
		 * are typically reported here due to eos-updater dying part-way
		 * through an upgrade (and hence the state reverting to NONE) */
		if (os_upgrade_get_download_by_user (app) &&
		    !g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			g_autoptr(GsPluginEvent) event = gs_plugin_event_new ();
			gs_eos_updater_error_convert (&local_error);
			gs_plugin_event_set_app (event, app);
			gs_plugin_event_set_action (event, GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD);
			gs_plugin_event_set_error (event, local_error);
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
			gs_plugin_report_event (plugin, event);
		}
	}
}

static void
updater_downloaded_bytes_changed (GsPlugin *plugin)
{
	g_autoptr(GError) local_error = NULL;

	if (!sync_state_from_updater (plugin, NULL, &local_error))
		g_warning ("Error syncing downloaded bytes from updater: %s", local_error->message);
}

static void
updater_version_changed (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *version = gs_eos_updater_get_version (priv->updater_proxy);

	/* If eos-updater goes away, we want to retain the previously set value
	 * of the version, for use in error messages. */
	if (version != NULL)
		gs_app_set_version (priv->os_upgrade, version);
}

static gboolean
fake_os_upgrade_progress (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	gfloat normal_step;
	guint new_progress;
	const gfloat fake_progress_max = 99.0;

	if (gs_eos_updater_get_state (priv->updater_proxy) != EOS_UPDATER_STATE_APPLYING_UPDATE ||
	    priv->upgrade_fake_progress > fake_progress_max) {
		priv->upgrade_fake_progress = 0;
		priv->upgrade_fake_progress_handler = 0;
		return G_SOURCE_REMOVE;
	}

	normal_step = (gfloat) EOS_UPGRADE_APPLY_PROGRESS_RANGE /
		      (EOS_UPGRADE_APPLY_MAX_TIME / EOS_UPGRADE_APPLY_STEP_TIME);

	priv->upgrade_fake_progress += normal_step;

	new_progress = (100 - EOS_UPGRADE_APPLY_PROGRESS_RANGE) +
		       (guint) round (priv->upgrade_fake_progress);
	gs_app_set_progress (priv->os_upgrade,
			     MIN (new_progress, (guint) fake_progress_max));

	g_debug ("OS upgrade fake progress: %f", priv->upgrade_fake_progress);

	return G_SOURCE_CONTINUE;
}

/* This method deals with the synchronization between the EOS updater's states
 * (D-Bus service) and the OS upgrade's states (GsApp), in order to show the user
 * what is happening and what they can do. */
static gboolean
sync_state_from_updater (GsPlugin      *plugin,
                         GCancellable  *cancellable,
                         GError       **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GsApp *app = priv->os_upgrade;
	EosUpdaterState state;
	AsAppState previous_app_state = gs_app_get_state (app);
	AsAppState current_app_state;
	const guint max_progress_for_update = 75;

	/* in case the OS upgrade has been disabled */
	if (priv->updater_proxy == NULL) {
		g_debug ("%s: Updater disabled", G_STRFUNC);
		return TRUE;
	}

	state = gs_eos_updater_get_state (priv->updater_proxy);
	g_debug ("EOS Updater state changed: %s", eos_updater_state_to_str (state));

	switch (state) {
	case EOS_UPDATER_STATE_NONE:
	case EOS_UPDATER_STATE_READY: {
		app_set_state (plugin, app, AS_APP_STATE_UNKNOWN);

		if (os_upgrade_get_download_by_user (app)) {
			if (!gs_eos_updater_call_poll_sync (priv->updater_proxy,
							    cancellable, error)) {
				gs_eos_updater_error_convert (error);
				return FALSE;
			}
		}
		break;
	} case EOS_UPDATER_STATE_POLLING: {
		/* Nothing to do here. */
		break;
	} case EOS_UPDATER_STATE_UPDATE_AVAILABLE: {
		app_set_state (plugin, app, AS_APP_STATE_AVAILABLE);

		if (os_upgrade_get_download_by_user (app)) {
			g_auto(GVariantDict) options_dict = G_VARIANT_DICT_INIT (NULL);

			/* when the OS upgrade was started by the user and the
			 * updater reports an available update, (meaning we were
			 * polling before), we should readily call fetch */
			g_variant_dict_insert (&options_dict, "force", "b", TRUE);

			if (!gs_eos_updater_call_fetch_full_sync (priv->updater_proxy,
								  g_variant_dict_end (&options_dict),
								  cancellable, error)) {
				gs_eos_updater_error_convert (error);
				return FALSE;
			}
		} else {
			guint64 total_size =
				gs_eos_updater_get_download_size (priv->updater_proxy);
			gs_app_set_size_download (app, total_size);
		}

		break;
	}
	case EOS_UPDATER_STATE_FETCHING: {
		guint64 total_size = 0;
		guint64 downloaded = 0;
		gfloat progress = 0;

		/* FIXME: Set to QUEUED_FOR_INSTALL if we’re waiting for metered
		 * data permission. */
		app_set_state (plugin, app, AS_APP_STATE_INSTALLING);

		downloaded = gs_eos_updater_get_downloaded_bytes (priv->updater_proxy);
		total_size = gs_eos_updater_get_download_size (priv->updater_proxy);

		if (total_size == 0) {
			g_debug ("OS upgrade %s total size is 0!",
				 gs_app_get_unique_id (app));
		} else {
			/* set progress only up to a max percentage, leaving the
			 * remaining for applying the update */
			progress = (gfloat) downloaded / (gfloat) total_size *
				   (gfloat) max_progress_for_update;
		}
		gs_app_set_progress (app, (guint) progress);

		break;
	}
	case EOS_UPDATER_STATE_UPDATE_READY: {
		app_set_state (plugin, app, AS_APP_STATE_UPDATABLE);

		/* if there's an update ready to deployed, and it was started by
		 * the user, we should proceed to applying the upgrade */
		if (os_upgrade_get_download_by_user (app)) {
			gs_app_set_progress (app, max_progress_for_update);

			if (!gs_eos_updater_call_apply_sync (priv->updater_proxy,
							     cancellable, error)) {
				gs_eos_updater_error_convert (error);
				return FALSE;
			}
		}

		break;
	}
	case EOS_UPDATER_STATE_APPLYING_UPDATE: {
		/* set as 'installing' because if it is applying the update, we
		 * want to show the progress bar */
		app_set_state (plugin, app, AS_APP_STATE_INSTALLING);

		/* set up the fake progress to inform the user that something
		 * is still being done (we don't get progress reports from
		 * deploying updates) */
		if (priv->upgrade_fake_progress_handler != 0)
			g_source_remove (priv->upgrade_fake_progress_handler);
		priv->upgrade_fake_progress = 0;
		priv->upgrade_fake_progress_handler =
			g_timeout_add ((guint) (1000.0 * EOS_UPGRADE_APPLY_STEP_TIME),
				       (GSourceFunc) fake_os_upgrade_progress,
				       plugin);

		break;
	}
	case EOS_UPDATER_STATE_UPDATE_APPLIED: {
		app_set_state (plugin, app, AS_APP_STATE_UPDATABLE);

		break;
	}
	case EOS_UPDATER_STATE_ERROR: {
		const gchar *error_name;
		const gchar *error_message;
		g_autoptr(GError) local_error = NULL;

		error_name = gs_eos_updater_get_error_name (priv->updater_proxy);
		error_message = gs_eos_updater_get_error_message (priv->updater_proxy);
		local_error = g_dbus_error_new_for_dbus_error (error_name, error_message);

		/* unless the error is because the user cancelled the upgrade,
		 * we should make sure it gets in the journal */
		if (!(os_upgrade_get_download_by_user (app) &&
		      eos_updater_error_is_cancelled (error_name)))
			g_warning ("Got OS upgrade error state with name '%s': %s",
				   error_name, error_message);

		/* We can’t recover the app state since eos-updater needs to
		 * go through the ready → poll → fetch → apply loop again in
		 * order to recover its state. So go back to ‘unknown’. */
		app_set_state (plugin, app, AS_APP_STATE_UNKNOWN);

		/* if we need to restart when an error occurred, just call poll
		 * since it will perform the full upgrade as the
		 * eos::DownloadByUser is true */
		if (os_upgrade_get_download_by_user (app)) {
			g_debug ("Restarting OS upgrade on error");
			if (!gs_eos_updater_call_poll_sync (priv->updater_proxy,
							    cancellable, error)) {
				gs_eos_updater_error_convert (error);
				return FALSE;
			}
		}

		/* only set up an error to be shown to the user if the user had
		 * manually started the upgrade, and if the error in question is not
		 * originated by the user canceling the upgrade */
		if (os_upgrade_get_download_by_user (app) &&
		    !eos_updater_error_is_cancelled (error_name)) {
			g_autoptr(GsPluginEvent) event = gs_plugin_event_new ();
			gs_eos_updater_error_convert (&local_error);
			gs_plugin_event_set_app (event, app);
			gs_plugin_event_set_action (event, GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD);
			gs_plugin_event_set_error (event, local_error);
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
			gs_plugin_report_event (plugin, event);
		}

		break;
	}
	default:
		g_warning ("Encountered unknown eos-updater state: %u", state);
		break;
	}

	current_app_state = gs_app_get_state (app);

	g_debug ("%s: Old app state: %s; new app state: %s",
		 G_STRFUNC, as_app_state_to_string (previous_app_state),
		 as_app_state_to_string (current_app_state));

	/* reset the 'download-by-user' state on error or completion */
	if (state == EOS_UPDATER_STATE_ERROR ||
	    state == EOS_UPDATER_STATE_UPDATE_APPLIED ||
	    state == EOS_UPDATER_STATE_NONE ||
	    state == EOS_UPDATER_STATE_READY) {
		os_upgrade_set_download_by_user (app, FALSE);
	}

	/* if the state changed from or to 'unknown', we need to notify that a
	 * new update should be shown */
	if (should_add_os_upgrade (previous_app_state) !=
	    should_add_os_upgrade (current_app_state)) {
		g_debug ("%s: Calling gs_plugin_updates_changed()", G_STRFUNC);
		gs_plugin_updates_changed (plugin);
	}

	return TRUE;
}

gboolean
gs_plugin_setup (GsPlugin *plugin,
		 GCancellable *cancellable,
		 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GError) local_error = NULL;
	g_autofree gchar *name_owner = NULL;

	g_debug ("%s", G_STRFUNC);

	/* Check that the proxy exists (and is owned; it should auto-start) so
	 * we can disable the plugin for systems which don’t have eos-updater.
	 * Throughout the rest of the plugin, errors from the daemon
	 * (particularly where it has disappeared off the bus) are ignored, and
	 * the poll/fetch/apply sequence is run through again to recover from
	 * the error. This is the only point in the plugin where we consider an
	 * error from eos-updater to be fatal to the plugin. */
	priv->updater_proxy = gs_eos_updater_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
								     G_DBUS_PROXY_FLAGS_NONE,
								     "com.endlessm.Updater",
								     "/com/endlessm/Updater",
								     cancellable,
								     error);
	if (priv->updater_proxy == NULL) {
		gs_eos_updater_error_convert (error);
		return FALSE;
	}

	name_owner = g_dbus_proxy_get_name_owner (G_DBUS_PROXY (priv->updater_proxy));

	if (name_owner == NULL) {
		g_set_error_literal (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "Couldn’t create EOS Updater proxy: couldn’t get name owner");
		return FALSE;
	}

	g_signal_connect_object (priv->updater_proxy, "notify::state",
				 G_CALLBACK (updater_state_changed),
				 plugin, G_CONNECT_SWAPPED);
	g_signal_connect_object (priv->updater_proxy,
				 "notify::downloaded-bytes",
				 G_CALLBACK (updater_downloaded_bytes_changed),
				 plugin, G_CONNECT_SWAPPED);
	g_signal_connect_object (priv->updater_proxy, "notify::version",
				 G_CALLBACK (updater_version_changed),
				 plugin, G_CONNECT_SWAPPED);

	/* prepare EOS upgrade app + sync initial state */
	return setup_os_upgrade (plugin, cancellable, error);
}

void
gs_plugin_initialize (GsPlugin *plugin)
{
	gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	if (priv->upgrade_fake_progress_handler != 0) {
		g_source_remove (priv->upgrade_fake_progress_handler);
		priv->upgrade_fake_progress_handler = 0;
	}

	if (priv->updater_proxy != NULL) {
		g_signal_handlers_disconnect_by_func (priv->updater_proxy,
						      G_CALLBACK (updater_state_changed),
						      plugin);
		g_signal_handlers_disconnect_by_func (priv->updater_proxy,
						      G_CALLBACK (updater_downloaded_bytes_changed),
						      plugin);
		g_signal_handlers_disconnect_by_func (priv->updater_proxy,
						      G_CALLBACK (updater_version_changed),
						      plugin);
	}

	g_cancellable_cancel (priv->os_upgrade_cancellable);
	os_upgrade_set_cancellable (plugin, NULL);

	g_clear_object (&priv->updater_proxy);

	g_clear_object (&priv->os_upgrade);
}

static gboolean
setup_os_upgrade (GsPlugin      *plugin,
                  GCancellable  *cancellable,
                  GError       **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GsApp) app = NULL;
	g_autoptr(AsIcon) ic = NULL;

	if (priv->os_upgrade != NULL)
		return TRUE;

	/* use stock icon */
	ic = as_icon_new ();
	as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
	as_icon_set_name (ic, "application-x-addon");

	/* create the OS upgrade */
	app = gs_app_new (EOS_UPGRADE_ID);
	gs_app_add_icon (app, ic);
	gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
	gs_app_set_kind (app, AS_APP_KIND_OS_UPGRADE);
	/* TRANSLATORS: ‘Endless OS’ is a brand name; https://endlessos.com/ */
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, _("Endless OS"));
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
			    /* TRANSLATORS: ‘Endless OS’ is a brand name; https://endlessos.com/ */
			    _("An Endless OS update with new features and fixes."));
	/* ensure that the version doesn't appear as (NULL) in the banner, it
	 * should be changed to the right value when it changes in the eos-updater */
	gs_app_set_version (app, "");
	gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
	gs_app_add_quirk (app, GS_APP_QUIRK_PROVENANCE);
	gs_app_add_quirk (app, GS_APP_QUIRK_NOT_REVIEWABLE);
	gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	gs_app_set_metadata (app, "GnomeSoftware::UpgradeBanner-css",
			     "background: url('" DATADIR "/gnome-software/upgrade-bg.png');"
			     "background-size: 100% 100%;");

	priv->os_upgrade = g_steal_pointer (&app);

	/* sync initial state */
	if (!sync_state_from_updater (plugin, cancellable, error))
		return FALSE;

	return TRUE;
}

static gboolean
check_for_os_updates (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	EosUpdaterState updater_state;
	gboolean success;

	/* check if the OS upgrade has been disabled */
	if (priv->updater_proxy == NULL) {
		g_debug ("%s: Updater disabled", G_STRFUNC);
		return TRUE;
	}

	/* poll in the error/none/ready states to check if there's an
	 * update available */
	updater_state = gs_eos_updater_get_state (priv->updater_proxy);
	switch (updater_state) {
	case EOS_UPDATER_STATE_ERROR:
	case EOS_UPDATER_STATE_NONE:
	case EOS_UPDATER_STATE_READY:
		success = gs_eos_updater_call_poll_sync (priv->updater_proxy,
							 cancellable, error);
		gs_eos_updater_error_convert (error);
		return success;
	default:
		g_debug ("%s: Updater in state %s; not polling",
			 G_STRFUNC, eos_updater_state_to_str (updater_state));
		return TRUE;
	}
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GCancellable *cancellable,
		   GError **error)
{
	g_debug ("%s: cache_age: %u", G_STRFUNC, cache_age);

	/* We let the eos-updater daemon do its own caching, so ignore the @cache_age. */
	return check_for_os_updates (plugin, cancellable, error);
}

gboolean
gs_plugin_add_distro_upgrades (GsPlugin *plugin,
			       GsAppList *list,
			       GCancellable *cancellable,
			       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	g_debug ("%s", G_STRFUNC);

	/* if we are testing the plugin, then always add the OS upgrade */
	if (g_getenv ("GS_PLUGIN_EOS_TEST") != NULL) {
		gs_app_set_state (priv->os_upgrade, AS_APP_STATE_AVAILABLE);
		gs_app_list_add (list, priv->os_upgrade);
		return TRUE;
	}

	/* check if the OS upgrade has been disabled */
	if (priv->updater_proxy == NULL) {
		g_debug ("%s: Updater disabled", G_STRFUNC);
		return TRUE;
	}

	if (should_add_os_upgrade (gs_app_get_state (priv->os_upgrade))) {
		g_debug ("Adding EOS upgrade: %s",
			 gs_app_get_unique_id (priv->os_upgrade));
		gs_app_list_add (list, priv->os_upgrade);
	} else {
		g_debug ("Not adding EOS upgrade");
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
	g_autoptr(GMainContext) context = NULL;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	g_debug ("%s", G_STRFUNC);

	/* if the OS upgrade has been disabled */
	if (priv->updater_proxy == NULL) {
		g_set_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
			     "The OS upgrade has been disabled in the EOS plugin");
		return FALSE;
	}

	g_assert (app == priv->os_upgrade);
	os_upgrade_set_download_by_user (app, TRUE);
	os_upgrade_set_cancellable (plugin, cancellable);

	g_debug ("GsPluginEosUpdater: %s: set cancellable to %p", G_STRFUNC, cancellable);

	/* we need to poll again if there has been an error; the state of the
	 * OS upgrade will then be dealt with from outside this function,
	 * according to the state changes of the update itself */
	if (gs_eos_updater_get_state (priv->updater_proxy) == EOS_UPDATER_STATE_ERROR) {
		gboolean success;
		success = gs_eos_updater_call_poll_sync (priv->updater_proxy,
							 cancellable, error);
		gs_eos_updater_error_convert (error);
		return success;
	} else {
		/* Now that we’ve called os_upgrade_set_download_by_user(TRUE),
		 * calling sync_state_from_updater() will call Fetch() on the
		 * updater service and start the download. */
		if (!sync_state_from_updater (plugin, cancellable, error))
			return FALSE;
	}

	/* Block until the download is complete or failed. Cancellation should
	 * result in the updater changing state to %EOS_UPDATER_STATE_ERROR.
	 * Updates of the updater’s progress properties should result in
	 * callbacks to updater_downloaded_bytes_changed() to update the app
	 * download progress. */
	context = g_main_context_ref_thread_default ();
	while (gs_eos_updater_get_state (priv->updater_proxy) == EOS_UPDATER_STATE_FETCHING)
		g_main_context_iteration (context, TRUE);

	/* Process the final state. */
	if (gs_eos_updater_get_state (priv->updater_proxy) == EOS_UPDATER_STATE_ERROR) {
		const gchar *error_name;
		const gchar *error_message;
		g_autoptr(GError) local_error = NULL;

		error_name = gs_eos_updater_get_error_name (priv->updater_proxy);
		error_message = gs_eos_updater_get_error_message (priv->updater_proxy);
		local_error = g_dbus_error_new_for_dbus_error (error_name, error_message);
		gs_eos_updater_error_convert (&local_error);
		g_propagate_error (error, g_steal_pointer (&local_error));

		return FALSE;
	}

	return TRUE;
}
