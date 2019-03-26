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

#include <ostree.h>
#include <gnome-software.h>
#include <glib/gi18n.h>
#include <gs-plugin.h>
#include <gs-utils.h>
#include <math.h>

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

#define EOS_UPGRADE_ID "com.endlessm.EOS.upgrade"

/* the percentage of the progress bar to use for applying the OS upgrade;
 * we need to fake the progress in this percentage because applying the OS upgrade
 * can take a long time and we don't want the user to think that the upgrade has
 * stalled */
#define EOS_UPGRADE_APPLY_PROGRESS_RANGE 25 /* percentage */
#define EOS_UPGRADE_APPLY_MAX_TIME 600.0 /* sec */
#define EOS_UPGRADE_APPLY_STEP_TIME 0.250 /* sec */

static void setup_os_upgrade (GsPlugin *plugin);
static EosUpdaterState sync_state_from_updater (GsPlugin *plugin);

struct GsPluginData
{
	GsEosUpdater *updater_proxy;  /* (owned) */
	GsApp *os_upgrade;  /* (owned) */
	GCancellable *os_upgrade_cancellable;  /* (nullable) (owned) */
	gfloat upgrade_fake_progress;
	guint upgrade_fake_progress_handler;
};

static void
os_upgrade_cancelled_cb (GCancellable *cancellable,
			 GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	/* TODO: Make this sync? */
	gs_eos_updater_call_cancel (priv->updater_proxy, NULL, NULL, NULL);
}

static void
setup_os_upgrade_cancellable (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GCancellable *cancellable = gs_app_get_cancellable (priv->os_upgrade);

	if (g_set_object (&priv->os_upgrade_cancellable, cancellable))
		g_cancellable_connect (priv->os_upgrade_cancellable,
				       G_CALLBACK (os_upgrade_cancelled_cb),
				       plugin, NULL);
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

static void
os_upgrade_set_restart_on_error (GsApp *app, gboolean value)
{
	g_autoptr(GVariant) var = g_variant_new_boolean (value);
	app_ensure_set_metadata_variant (app, "eos::RestartOnError", var);
}

static gboolean
os_upgrade_get_restart_on_error (GsApp *app)
{
	GVariant *value = gs_app_get_metadata_variant (app, "eos::RestartOnError");
	if (value == NULL)
		return FALSE;
	return g_variant_get_boolean (value);
}

static void
os_upgrade_force_fetch (GsEosUpdater *updater)
{
	g_auto(GVariantDict) options_dict = G_VARIANT_DICT_INIT (NULL);
	g_variant_dict_insert (&options_dict, "force", "b", TRUE);

	/* TODO: Make this sync? */
	gs_eos_updater_call_fetch_full (updater,
					g_variant_dict_end (&options_dict),
					NULL, NULL, NULL);
}

static void
app_ensure_installing_state (GsApp *app)
{
	/* ensure the state transition to 'installing' is allowed */
	if (gs_app_get_state (app) != AS_APP_STATE_INSTALLING)
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);

	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
}

static gboolean
eos_updater_error_is_cancelled (const gchar *error_name)
{
	return (g_strcmp0 (error_name, "com.endlessm.Updater.Error.Cancelled") == 0);
}

static void
updater_state_changed (GsPlugin *plugin)
{
	sync_state_from_updater (plugin);
}

static void
updater_downloaded_bytes_changed (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	app_ensure_installing_state (priv->os_upgrade);
	sync_state_from_updater (plugin);
}

static void
updater_version_changed (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *version = gs_eos_updater_get_version (priv->updater_proxy);

	gs_app_set_version (priv->os_upgrade, version);
}

static void
disable_os_updater (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	if (priv->upgrade_fake_progress_handler != 0) {
		g_source_remove (priv->upgrade_fake_progress_handler);
		priv->upgrade_fake_progress_handler = 0;
	}

	if (priv->updater_proxy == NULL) {
		g_debug ("%s: Updater disabled", G_STRFUNC);
		return;
	}

	g_signal_handlers_disconnect_by_func (priv->updater_proxy,
					      G_CALLBACK (updater_state_changed),
					      plugin);
	g_signal_handlers_disconnect_by_func (priv->updater_proxy,
					      G_CALLBACK (updater_version_changed),
					      plugin);

	g_cancellable_cancel (priv->os_upgrade_cancellable);
	g_clear_object (&priv->os_upgrade_cancellable);

	g_clear_object (&priv->updater_proxy);
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

static gboolean
updater_is_stalled (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GsApp *app = priv->os_upgrade;

	/* in case the OS upgrade has been disabled */
	if (priv->updater_proxy == NULL) {
		g_debug ("%s: Updater disabled", G_STRFUNC);
		return FALSE;
	}

	return gs_eos_updater_get_state (priv->updater_proxy) == EOS_UPDATER_STATE_FETCHING &&
	       gs_app_get_state (app) != AS_APP_STATE_INSTALLING;
}

/* This method deals with the synchronization between the EOS updater's states
 * (DBus service) and the OS upgrade's states (GsApp), in order to show the user
 * what is happening and what they can do. */
static EosUpdaterState
sync_state_from_updater (GsPlugin *plugin)
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
		return EOS_UPDATER_STATE_NONE;
	}

	state = gs_eos_updater_get_state (priv->updater_proxy);
	g_debug ("EOS Updater state changed: %s", eos_updater_state_to_str (state));

	switch (state) {
	case EOS_UPDATER_STATE_NONE:
	case EOS_UPDATER_STATE_READY: {
		if (os_upgrade_get_download_by_user (app)) {
			app_ensure_installing_state (app);
			/* TODO: Make this sync? */
			gs_eos_updater_call_poll (priv->updater_proxy, NULL, NULL,
						  NULL);
		} else {
			gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
		}
		break;
	} case EOS_UPDATER_STATE_POLLING: {
		if (os_upgrade_get_download_by_user (app))
			app_ensure_installing_state (app);
		break;
	} case EOS_UPDATER_STATE_UPDATE_AVAILABLE: {
		if (os_upgrade_get_download_by_user (app)) {
			app_ensure_installing_state (app);
			/* when the OS upgrade was started by the user and the
			 * updater reports an available update, (meaning we were
			 * polling before), we should readily call fetch */
			os_upgrade_force_fetch (priv->updater_proxy);
		} else {
			guint64 total_size =
				gs_eos_updater_get_download_size (priv->updater_proxy);
			gs_app_set_size_download (app, total_size);

			gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		}

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
		 * we should make sure it get in the journal */
		if (!(os_upgrade_get_download_by_user (app) &&
		      eos_updater_error_is_cancelled (error_name)))
		    g_warning ("Got OS upgrade error state with name '%s': %s",
			       error_name, error_message);

		gs_app_set_state_recover (app);

		if ((g_strcmp0 (error_name, "com.endlessm.Updater.Error.LiveBoot") == 0) ||
		    (g_strcmp0 (error_name, "com.endlessm.Updater.Error.NotOstreeSystem") == 0)) {
			g_debug ("Disabling OS upgrades: %s", error_message);
			disable_os_updater (plugin);
			return state;
		}

		/* if we need to restart when an error occurred, just call poll
		 * since it will perform the full upgrade as the
		 * eos::DownloadByUser is true */
		if (os_upgrade_get_restart_on_error (app)) {
			g_debug ("Restarting OS upgrade on error");
			os_upgrade_set_restart_on_error (app, FALSE);
			app_ensure_installing_state (app);
			/* TODO: Make this sync? */
			gs_eos_updater_call_poll (priv->updater_proxy, NULL, NULL,
						  NULL);
			break;
		}

		/* only set up an error to be shown to the user if the user had
		 * manually started the upgrade, and if the error in question is not
		 * originated by the user canceling the upgrade */
		if (os_upgrade_get_download_by_user (app) &&
		    !eos_updater_error_is_cancelled (error_name)) {
			g_autoptr(GsPluginEvent) event = gs_plugin_event_new ();
			gs_utils_error_convert_gdbus (&local_error);
			gs_plugin_event_set_app (event, app);
			gs_plugin_event_set_error (event, local_error);
			gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
			gs_plugin_report_event (plugin, event);
		}

		break;
	}
	case EOS_UPDATER_STATE_FETCHING: {
		guint64 total_size = 0;
		guint64 downloaded = 0;
		gfloat progress = 0;

		if (!updater_is_stalled (plugin))
			app_ensure_installing_state (app);
		else
			gs_app_set_state (app, AS_APP_STATE_AVAILABLE);

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
		/* if there's an update ready to deployed, and it was started by
		 * the user, we should proceed to applying the upgrade */
		if (os_upgrade_get_download_by_user (app)) {
			app_ensure_installing_state (app);
			gs_app_set_progress (app, max_progress_for_update);
			/* TODO: Make this sync? */
			gs_eos_updater_call_apply (priv->updater_proxy, NULL, NULL,
						   NULL);
		} else {
			/* otherwise just show it as available so the user has a
			 * chance to click 'download' which deploys the update */
			gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		}

		break;
	}
	case EOS_UPDATER_STATE_APPLYING_UPDATE: {
		/* set as 'installing' because if it is applying the update, we
		 * want to show the progress bar */
		app_ensure_installing_state (app);

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
		/* ensure we can transition to state updatable */
		if (gs_app_get_state (app) == AS_APP_STATE_AVAILABLE)
			gs_app_set_state (app, AS_APP_STATE_INSTALLING);
		gs_app_set_state (app, AS_APP_STATE_UPDATABLE);

		break;
	}
	default:
		break;
	}

	current_app_state = gs_app_get_state (app);

	/* reset the 'download-by-user' state if the the app is no longer
	 * shown as downloading */
	if (current_app_state != AS_APP_STATE_INSTALLING) {
		os_upgrade_set_download_by_user (app, FALSE);
	} else {
		/* otherwise, ensure we have the right cancellable */
		setup_os_upgrade_cancellable (plugin);
	}

	/* if the state changed from or to 'unknown', we need to notify that a
	 * new update should be shown */
	if ((previous_app_state == AS_APP_STATE_UNKNOWN ||
	     current_app_state == AS_APP_STATE_UNKNOWN) &&
	    previous_app_state != current_app_state)
		gs_plugin_updates_changed (plugin);

	return state;
}

gboolean
gs_plugin_setup (GsPlugin *plugin,
		 GCancellable *cancellable,
		 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GError) local_error = NULL;

	/* Errors here aren’t propagated to the caller, as the rest of the plugin
	 * needs to be able to handle a missing @updater_proxy anyway. So rather
	 * than disabling the plugin (by returning an error here), we just
	 * handle the disabled state throughout. */
	priv->updater_proxy = gs_eos_updater_proxy_new_for_bus_sync (G_BUS_TYPE_SYSTEM,
								     G_DBUS_PROXY_FLAGS_NONE,
								     "com.endlessm.Updater",
								     "/com/endlessm/Updater",
								     NULL,
								     &local_error);
	if (priv->updater_proxy == NULL) {
		g_warning ("Couldn't create EOS Updater proxy: %s",
			   local_error->message);
		g_clear_error (&local_error);
	}

	/* prepare EOS upgrade app + sync initial state */
	setup_os_upgrade (plugin);

	return TRUE;
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

	disable_os_updater (plugin);
	g_clear_object (&priv->os_upgrade);
}

static void
setup_os_upgrade (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GsApp *app = NULL;
	g_autoptr(AsIcon) ic = NULL;

	if (priv->os_upgrade != NULL)
		return;

	/* use stock icon */
	ic = as_icon_new ();
	as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
	as_icon_set_name (ic, "application-x-addon");

	/* create the OS upgrade */
	app = gs_app_new (EOS_UPGRADE_ID);
	gs_app_add_icon (app, ic);
	gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
	gs_app_set_kind (app, AS_APP_KIND_OS_UPGRADE);
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, "Endless OS");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
			    _("An Endless update with new features and fixes."));
	/* ensure that the version doesn't appear as (NULL) in the banner, it
	 * should be changed to the right value when it changes in the eos-updater */
	gs_app_set_version (app, "");
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, "Endless OS");
	gs_app_add_quirk (app, AS_APP_QUIRK_NEEDS_REBOOT);
	gs_app_add_quirk (app, AS_APP_QUIRK_PROVENANCE);
	gs_app_add_quirk (app, AS_APP_QUIRK_NOT_REVIEWABLE);
	gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	gs_app_set_metadata (app, "GnomeSoftware::UpgradeBanner-css",
			     "background: url('" DATADIR "/gnome-software/upgrade-bg.png');"
			     "background-size: 100% 100%;");

	priv->os_upgrade = app;

	/* for debug purposes we create the OS upgrade even if the EOS updater is NULL */
	if (priv->updater_proxy != NULL) {
		/* sync initial state */
		sync_state_from_updater (plugin);

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
	}
}

static gboolean
should_add_os_upgrade (GsApp *os_upgrade)
{
	switch (gs_app_get_state (os_upgrade)) {
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_INSTALLING:
	case AS_APP_STATE_UPDATABLE:
		return TRUE;
	default:
		return FALSE;
	}
}

static gboolean
check_for_os_updates (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	EosUpdaterState updater_state;

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
		return gs_eos_updater_call_poll_sync (priv->updater_proxy,
						      cancellable, error);
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

	/* if we are testing the plugin, then always add the OS upgrade */
	if (g_getenv ("GS_PLUGIN_EOS_TEST") != NULL) {
		if  (gs_app_get_state (priv->os_upgrade) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (priv->os_upgrade, AS_APP_STATE_AVAILABLE);
		gs_app_list_add (list, priv->os_upgrade);
		return TRUE;
	}

	/* check if the OS upgrade has been disabled */
	if (priv->updater_proxy == NULL) {
		g_debug ("%s: Updater disabled", G_STRFUNC);
		return TRUE;
	}

	if (should_add_os_upgrade (priv->os_upgrade)) {
		g_debug ("Adding EOS upgrade: %s",
			 gs_app_get_unique_id (priv->os_upgrade));
		gs_app_list_add (list, priv->os_upgrade);
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

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	if (app != priv->os_upgrade) {
		g_warning ("The OS upgrade to download (%s) differs from the "
			   "one in the EOS plugin, yet it's managed by it!",
			   gs_app_get_unique_id (app));
		return TRUE;
	}

	/* if the OS upgrade has been disabled */
	if (priv->updater_proxy == NULL) {
		g_set_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
			     "The OS upgrade has been disabled in the EOS plugin");
		return FALSE;
	}

	os_upgrade_set_download_by_user (app, TRUE);

	if (updater_is_stalled (plugin)) {
		os_upgrade_set_restart_on_error (app, TRUE);
		/* TODO: Make this sync? */
		gs_eos_updater_call_cancel (priv->updater_proxy, NULL, NULL, NULL);
		return TRUE;
	}

	/* we need to poll again if there has been an error; the state of the
	 * OS upgrade will then be dealt with from outside this function,
	 * according to the state changes of the update itself */
	if (gs_eos_updater_get_state (priv->updater_proxy) == EOS_UPDATER_STATE_ERROR)
		return gs_eos_updater_call_poll_sync (priv->updater_proxy,
						      cancellable, error);
	else
		sync_state_from_updater (plugin);

	return TRUE;
}

gboolean
gs_plugin_file_to_app (GsPlugin *plugin,
		       GsAppList *list,
		       GFile *file,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autofree gchar *content_type = NULL;
	const gchar * const mimetypes_repo[] = {
		"inode/directory",
		NULL };

	/* does this match any of the mimetypes we support */
	content_type = gs_utils_get_content_type (file, cancellable, error);
	if (content_type == NULL)
		return FALSE;
	if (g_strv_contains (mimetypes_repo, content_type)) {
		/* If it looks like an ostree repo that could be on a USB drive,
		 * have eos-updater check it for available OS updates */
		g_autoptr (GFile) repo_dir = NULL;

		repo_dir = g_file_get_child (file, ".ostree");
		if (g_file_query_exists (repo_dir, NULL))
			return check_for_os_updates (plugin, cancellable, error);
	}

	return TRUE;
}

static char *
get_os_collection_id (GError **error)
{
	OstreeDeployment *booted_deployment;
	GKeyFile *origin;
	g_autofree char *refspec = NULL;
	g_autofree char *remote = NULL;
	g_autofree char *collection_id = NULL;
	g_autoptr(OstreeRepo) repo = NULL;
	g_autoptr(OstreeSysroot) sysroot = NULL;

	sysroot = ostree_sysroot_new_default ();
	if (!ostree_sysroot_load (sysroot, NULL, error))
		return NULL;

	booted_deployment = ostree_sysroot_get_booted_deployment (sysroot);
	if (booted_deployment == NULL) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     "Could not get booted deployment");
		return NULL;
	}

	origin = ostree_deployment_get_origin (booted_deployment);
	if (origin == NULL) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED,
			     "Could not get deployment origin");
		return NULL;
	}

	refspec = g_key_file_get_string (origin, "origin", "refspec", error);
	if (refspec == NULL)
		return NULL;

	ostree_parse_refspec (refspec, &remote, NULL, error);
	if (remote == NULL)
		return NULL;

	repo = ostree_repo_new_default ();
	if (!ostree_repo_open (repo, NULL, error))
		return NULL;

	if (!ostree_repo_get_remote_option (repo, remote, "collection-id", NULL, &collection_id, error))
		return NULL;

	return g_steal_pointer (&collection_id);
}

gboolean
gs_plugin_os_get_copyable (GsPlugin *plugin,
			   const gchar *copy_dest,
			   gboolean *copyable,
			   GCancellable *cancellable,
			   GError **error)
{
	g_autoptr(GError) local_error = NULL;
	g_autofree char *collection_id = get_os_collection_id (&local_error);

	if (local_error != NULL)
		g_debug ("Failed to get OSTree collection ID: %s", local_error->message);

	*copyable = (collection_id != NULL);

	return TRUE;
}

typedef struct {
	GCancellable *cancellable;  /* (owned) */
	gulong cancelled_id;
	gboolean finished;
	GError *error;  /* (nullable) (owned) */
	GMainContext *context;  /* (owned) */
} OsCopyProcessHelper;

static void
os_copy_process_helper_free (OsCopyProcessHelper *helper)
{
	g_clear_object (&helper->cancellable);
	g_assert (helper->cancelled_id == 0);  /* disconnected in watch_cb() */
	g_clear_error (&helper->error);
	g_main_context_unref (helper->context);
	g_free (helper);
}

static OsCopyProcessHelper *
os_copy_process_helper_new (GMainContext *context,
                            GCancellable *cancellable,
                            gulong        cancelled_id)
{
	OsCopyProcessHelper *helper = g_new0 (OsCopyProcessHelper, 1);
	helper->cancellable = g_object_ref (cancellable);
	helper->cancelled_id = cancelled_id;
	helper->finished = FALSE;
	helper->error = NULL;
	helper->context = g_main_context_ref (context);

	return helper;
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC (OsCopyProcessHelper, os_copy_process_helper_free)

static void
os_copy_process_watch_cb (GPid pid, gint status, gpointer user_data)
{
	OsCopyProcessHelper *helper = user_data;
	g_autoptr(GError) error = NULL;

	if (!g_cancellable_is_cancelled (helper->cancellable) && status != 0)
		g_set_error (&helper->error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to copy OS to removable media: command "
			     "failed with status %d", status);

	g_cancellable_disconnect (helper->cancellable, helper->cancelled_id);
	helper->cancelled_id = 0;
	g_spawn_close_pid (pid);

	/* once the copy terminates (successfully or not), set plugin status to
	 * update UI accordingly */

	helper->finished = TRUE;
	g_main_context_wakeup (helper->context);
}

static void
os_copy_cancelled_cb (GCancellable *cancellable, gpointer user_data)
{
	GPid pid = GPOINTER_TO_INT (user_data);

	/* terminate the process which is copying the OS */
	kill (pid, SIGTERM);
}

gboolean
gs_plugin_os_copy (GsPlugin *plugin,
		   const gchar *copy_dest,
		   GCancellable *cancellable,
		   GError **error)
{
	/* this is used in an async function but we block here until that
	 * returns so we won't auto-free while other threads depend on this */
	g_autoptr(OsCopyProcessHelper) helper = NULL;
	gboolean spawn_retval;
	const gchar *argv[] = {"/usr/bin/pkexec",
			       "/usr/bin/eos-updater-prepare-volume",
			       copy_dest,
			       NULL};
	GPid child_pid;
	gulong cancelled_id;
	g_autoptr(GMainContext) context = NULL;
	g_autoptr(GSource) child_watch_source = NULL;

	context = g_main_context_new ();
	g_main_context_push_thread_default (context);

	g_debug ("Copying OS to: %s", copy_dest);

	spawn_retval = g_spawn_async (".",
				      (gchar **) argv,
				      NULL,
				      G_SPAWN_DO_NOT_REAP_CHILD,
				      NULL,
				      NULL,
				      &child_pid,
				      error);

	if (spawn_retval) {
		cancelled_id = g_cancellable_connect (cancellable,
						      G_CALLBACK (os_copy_cancelled_cb),
						      GINT_TO_POINTER (child_pid),
						      NULL);

		helper = os_copy_process_helper_new (context, cancellable, cancelled_id);
		child_watch_source = g_child_watch_source_new (child_pid);
		g_source_set_callback (child_watch_source,
				       G_SOURCE_FUNC (os_copy_process_watch_cb), helper, NULL);
		g_source_attach (child_watch_source, context);
	} else {
		g_main_context_pop_thread_default (context);
		return FALSE;
	}

	/* Iterate the main loop until either the copy process completes or the
	 * user cancels the copy. Without this, it is impossible to cancel the
	 * copy because we reach the end of this function, its parent GTask
	 * returns and we disconnect the handler that would kill the copy
	 * process. */
	while (!helper->finished)
		g_main_context_iteration (context, TRUE);

	g_source_destroy (child_watch_source);
	g_main_context_pop_thread_default (context);

	if (helper->error) {
		g_propagate_error (error, g_steal_pointer (&helper->error));
		return FALSE;
	}

	return TRUE;
}