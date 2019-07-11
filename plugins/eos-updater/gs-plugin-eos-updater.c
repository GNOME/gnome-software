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
 * Plugin to poll for, download and apply OS updates using the `eos-updater`
 * service when running on Endless OS.
 *
 * This plugin is only useful on Endless OS.
 *
 * It creates a proxy for the `eos-updater` D-Bus service, which implements a
 * basic state machine which progresses through several states in order to
 * download updates: `Ready` (doing nothing) → `Poll` (checking for updates) →
 * `Fetch` (downloading an update) → `Apply` (deploying the update’s OSTree,
 * before a reboot). Any state may transition to the `Error` state at any time,
 * and the daemon may disappear at any time.
 *
 * This plugin follows the state transitions signalled by the daemon, and
 * updates the state of a single #GsApp instance (`os_upgrade`) to reflect the
 * OS upgrade in the UI.
 *
 * Calling gs_plugin_refresh() will result in this plugin calling the `Poll()`
 * method on the `eos-updater` daemon to check for a new update.
 *
 * Calling gs_plugin_app_upgrade_download() will result in this plugin calling
 * a sequence of methods on the `eos-updater` daemon to check for, download and
 * apply an update. Typically, gs_plugin_app_upgrade_download() should be called
 * once `eos-updater` is already in the `UpdateAvailable` state. It will report
 * progress information, with the first 75 percentage points of the progress
 * reporting the download progress, and the final 25 percentage points reporting
 * the OSTree deployment progress. The final 25 percentage points are currently
 * faked because we can’t get reasonable progress data out of OSTree.
 *
 * The proxy object (`updater_proxy`) uses the thread-default main context from
 * the gs_plugin_setup() function, which is currently the global default main
 * context from gnome-software’s main thread. This means all the signal
 * callbacks from the proxy will be executed in the main thread, and *must not
 * block*.
 *
 * The other functions (gs_plugin_refresh(), gs_plugin_app_upgrade_download(),
 * etc.) are called in #GTask worker threads. They are allowed to call methods
 * on the proxy; the main thread is only allowed to receive signals and check
 * properties on the proxy, to avoid blocking. Consequently, worker threads need
 * to block on the main thread receiving state change signals from
 * `eos-updater`. Receipt of these signals is notified through
 * `state_change_cond`. This means that all functions which access the
 * `GsPluginData` must lock it using the `mutex`.
 *
 * `updater_proxy`, `os_upgrade` and `cancellable` are only set in
 * gs_plugin_setup(), and are both internally thread-safe — so they can both be
 * dereferenced and have their methods called from any thread without
 * necessarily holding `mutex`.
 *
 * Cancellation of any operations on the `eos-updater` daemon (polling, fetching
 * or applying) is implemented by calling the `Cancel()` method on it. This is
 * permanently connected to the private `cancellable` #GCancellable instance,
 * which persists for the lifetime of the plugin. The #GCancellable instances
 * for various operations can be temporarily chained to it for the duration of
 * each operation.
 */

static const guint max_progress_for_update = 75;  /* percent */

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

/* the percentage of the progress bar to use for applying the OS upgrade;
 * we need to fake the progress in this percentage because applying the OS upgrade
 * can take a long time and we don't want the user to think that the upgrade has
 * stalled */
static const guint upgrade_apply_progress_range = 100 - max_progress_for_update; /* percent */
static const gfloat upgrade_apply_max_time = 600.0; /* sec */
static const gfloat upgrade_apply_step_time = 0.250; /* sec */

static void sync_state_from_updater_unlocked (GsPlugin *plugin);

struct GsPluginData
{
	/* These members are only set once in gs_plugin_setup(), and are
	 * internally thread-safe, so can be accessed without holding @mutex: */
	GsEosUpdater *updater_proxy;  /* (owned) */
	GsApp *os_upgrade;  /* (owned) */
	GCancellable *cancellable;  /* (owned) */
	gulong cancelled_id;

	/* These members must only ever be accessed from the main thread, so
	 * can be accessed without holding @mutex: */
	gfloat upgrade_fake_progress;
	guint upgrade_fake_progress_handler;

	/* State synchronisation between threads: */
	GMutex mutex;
	GCond state_change_cond;  /* locked by @mutex */
};

static void
os_upgrade_cancelled_cb (GCancellable *cancellable,
			 GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	g_debug ("%s: Cancelling upgrade", G_STRFUNC);
	gs_eos_updater_call_cancel (priv->updater_proxy, NULL, NULL, NULL);
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

/* This will be invoked in the main thread. */
static void
updater_state_changed (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);

	g_debug ("%s", G_STRFUNC);

	sync_state_from_updater_unlocked (plugin);

	/* Signal any blocked threads; typically this will be
	 * gs_plugin_app_upgrade_download() in a #GTask worker thread. */
	g_cond_broadcast (&priv->state_change_cond);
}

/* This will be invoked in the main thread. */
static void
updater_downloaded_bytes_changed (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);

	sync_state_from_updater_unlocked (plugin);
}

/* This will be invoked in the main thread, but doesn’t currently need to hold
 * `mutex` since it only accesses `priv->updater_proxy` and `priv->os_upgrade`,
 * both of which are internally thread-safe. */
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

/* This will be invoked in the main thread, but doesn’t currently need to hold
 * `mutex` since `priv->updater_proxy` and `priv->os_upgrade` are both
 * thread-safe, and `priv->upgrade_fake_progress` and
 * `priv->upgrade_fake_progress_handler` are only ever accessed from the main
 * thread. */
static gboolean
fake_os_upgrade_progress_cb (GsPlugin *plugin)
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

	normal_step = (gfloat) upgrade_apply_progress_range /
		      (upgrade_apply_max_time / upgrade_apply_step_time);

	priv->upgrade_fake_progress += normal_step;

	new_progress = max_progress_for_update +
		       (guint) round (priv->upgrade_fake_progress);
	gs_app_set_progress (priv->os_upgrade,
			     MIN (new_progress, (guint) fake_progress_max));

	g_debug ("OS upgrade fake progress: %f", priv->upgrade_fake_progress);

	return G_SOURCE_CONTINUE;
}

/* This method deals with the synchronization between the EOS updater's states
 * (D-Bus service) and the OS upgrade's states (GsApp), in order to show the user
 * what is happening and what they can do.
 *
 * It must be called with priv->mutex already locked. */
static void
sync_state_from_updater_unlocked (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GsApp *app = priv->os_upgrade;
	EosUpdaterState state;
	AsAppState previous_app_state = gs_app_get_state (app);
	AsAppState current_app_state;

	/* in case the OS upgrade has been disabled */
	if (priv->updater_proxy == NULL) {
		g_debug ("%s: Updater disabled", G_STRFUNC);
		return;
	}

	state = gs_eos_updater_get_state (priv->updater_proxy);
	g_debug ("EOS Updater state changed: %s", eos_updater_state_to_str (state));

	switch (state) {
	case EOS_UPDATER_STATE_NONE:
	case EOS_UPDATER_STATE_READY: {
		app_set_state (plugin, app, AS_APP_STATE_UNKNOWN);
		break;
	} case EOS_UPDATER_STATE_POLLING: {
		/* Nothing to do here. */
		break;
	} case EOS_UPDATER_STATE_UPDATE_AVAILABLE: {
		guint64 total_size;

		app_set_state (plugin, app, AS_APP_STATE_AVAILABLE);

		total_size = gs_eos_updater_get_download_size (priv->updater_proxy);
		gs_app_set_size_download (app, total_size);

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
			g_timeout_add ((guint) (1000.0 * upgrade_apply_step_time),
				       (GSourceFunc) fake_os_upgrade_progress_cb,
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

		error_name = gs_eos_updater_get_error_name (priv->updater_proxy);
		error_message = gs_eos_updater_get_error_message (priv->updater_proxy);

		/* unless the error is because the user cancelled the upgrade,
		 * we should make sure it gets in the journal */
		if (!eos_updater_error_is_cancelled (error_name))
			g_warning ("Got OS upgrade error state with name '%s': %s",
				   error_name, error_message);

		/* We can’t recover the app state since eos-updater needs to
		 * go through the ready → poll → fetch → apply loop again in
		 * order to recover its state. So go back to ‘unknown’. */
		app_set_state (plugin, app, AS_APP_STATE_UNKNOWN);

		/* Cancelling anything in the updater will result in a
		 * transition to the Error state. Use that as a cue to reset
		 * our #GCancellable ready for next time. */
		g_cancellable_reset (priv->cancellable);

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

	/* if the state changed from or to 'unknown', we need to notify that a
	 * new update should be shown */
	if (should_add_os_upgrade (previous_app_state) !=
	    should_add_os_upgrade (current_app_state)) {
		g_debug ("%s: Calling gs_plugin_updates_changed()", G_STRFUNC);
		gs_plugin_updates_changed (plugin);
	}
}

/* This is called in the main thread, so will end up creating an @updater_proxy
 * which is tied to the main thread’s #GMainContext. */
gboolean
gs_plugin_setup (GsPlugin *plugin,
		 GCancellable *cancellable,
		 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GError) error_local = NULL;
	g_autofree gchar *name_owner = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(AsIcon) ic = NULL;
	g_autoptr(GMutexLocker) locker = NULL;

	g_debug ("%s", G_STRFUNC);

	g_mutex_init (&priv->mutex);
	g_cond_init (&priv->state_change_cond);

	locker = g_mutex_locker_new (&priv->mutex);

	priv->cancellable = g_cancellable_new ();
	priv->cancelled_id =
		g_cancellable_connect (priv->cancellable,
				       G_CALLBACK (os_upgrade_cancelled_cb),
				       plugin, NULL);

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

	/* use stock icon */
	ic = as_icon_new ();
	as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
	as_icon_set_name (ic, "application-x-addon");

	/* create the OS upgrade */
	app = gs_app_new ("com.endlessm.EOS.upgrade");
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
	sync_state_from_updater_unlocked (plugin);

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

	g_cancellable_cancel (priv->cancellable);
	if (priv->cancellable != NULL && priv->cancelled_id != 0)
		g_cancellable_disconnect (priv->cancellable, priv->cancelled_id);
	g_clear_object (&priv->cancellable);

	g_clear_object (&priv->updater_proxy);

	g_clear_object (&priv->os_upgrade);

	g_cond_clear (&priv->state_change_cond);
	g_mutex_clear (&priv->mutex);
}

/* Called in a #GTask worker thread, but it can run without holding
 * `priv->mutex` since it doesn’t need to synchronise on state. */
gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GCancellable *cancellable,
		   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	EosUpdaterState updater_state;
	gboolean success;

	/* We let the eos-updater daemon do its own caching, so ignore the
	 * @cache_age, unless it’s %G_MAXUINT, which signifies startup of g-s.
	 * In that case, it’s probably just going to load the system too much to
	 * do an update check now. We can wait. */
	g_debug ("%s: cache_age: %u", G_STRFUNC, cache_age);

	if (cache_age == G_MAXUINT)
		return TRUE;

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
		/* This sync call will block the job thread, which is OK. */
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

/* Called in a #GTask worker thread, but it can run without holding
 * `priv->mutex` since it doesn’t need to synchronise on state. */
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

/* Must be called with priv->mutex already locked. */
static gboolean
wait_for_state_change_unlocked (GsPlugin      *plugin,
                                GCancellable  *cancellable,
                                GError       **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	EosUpdaterState old_state, new_state;

	old_state = new_state = gs_eos_updater_get_state (priv->updater_proxy);
	g_debug ("%s: Old state ‘%s’", G_STRFUNC, eos_updater_state_to_str (old_state));

	while (new_state == old_state &&
	       !g_cancellable_is_cancelled (cancellable)) {
		g_cond_wait (&priv->state_change_cond, &priv->mutex);
		new_state = gs_eos_updater_get_state (priv->updater_proxy);
	}

	if (!g_cancellable_set_error_if_cancelled (cancellable, error)) {
		g_debug ("%s: New state ‘%s’", G_STRFUNC, eos_updater_state_to_str (new_state));
		return TRUE;
	} else {
		g_debug ("%s: Cancelled", G_STRFUNC);
		return FALSE;
	}
}

/* Could be executed in any thread. No need to hold `priv->mutex` since we don’t
 * access anything which is not thread-safe. */
static void
cancelled_cb (GCancellable *ui_cancellable,
              gpointer      user_data)
{
	GsPlugin *plugin = GS_PLUGIN (user_data);
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* Chain cancellation. */
	g_debug ("Propagating OS download cancellation from %p to %p",
		 ui_cancellable, priv->cancellable);
	g_cancellable_cancel (priv->cancellable);

	/* And wake up anything blocking on a state change. */
	g_cond_broadcast (&priv->state_change_cond);
}

/* Called in a #GTask worker thread, and it needs to hold `priv->mutex` due to
 * synchronising on state with the main thread. */
gboolean
gs_plugin_app_upgrade_download (GsPlugin *plugin,
				GsApp *app,
			        GCancellable *cancellable,
				GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	gulong cancelled_id = 0;
	EosUpdaterState state;
	gboolean done, allow_restart;
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* if the OS upgrade has been disabled */
	if (priv->updater_proxy == NULL) {
		g_set_error (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_FAILED,
			     "The OS upgrade has been disabled in the EOS plugin");
		return FALSE;
	}

	g_assert (app == priv->os_upgrade);

	/* Set up cancellation. */
	g_debug ("Chaining cancellation from %p to %p", cancellable, priv->cancellable);
	if (cancellable != NULL) {
		cancelled_id = g_cancellable_connect (cancellable,
						      G_CALLBACK (cancelled_cb),
						      plugin, NULL);
	}

	/* Step through the state machine until we are finished downloading and
	 * applying the update, or until an error occurs. All of the D-Bus calls
	 * here will block until the method call is complete. */
	state = gs_eos_updater_get_state (priv->updater_proxy);

	done = FALSE;
	allow_restart = (state == EOS_UPDATER_STATE_NONE ||
			 state == EOS_UPDATER_STATE_READY ||
			 state == EOS_UPDATER_STATE_ERROR);

	while (!done && !g_cancellable_is_cancelled (cancellable)) {
		state = gs_eos_updater_get_state (priv->updater_proxy);
		g_debug ("%s: State ‘%s’", G_STRFUNC, eos_updater_state_to_str (state));

		switch (state) {
		case EOS_UPDATER_STATE_NONE:
		case EOS_UPDATER_STATE_READY: {
			/* Poll for an update. This typically only happens if
			 * we’ve drifted out of sync with the updater process
			 * due to it dying. In that case, only restart once
			 * before giving up, so we don’t end up in an endless
			 * loop (say, if eos-updater always died 50% of the way
			 * through a download). */
			if (allow_restart) {
				allow_restart = FALSE;
				g_debug ("Restarting OS upgrade from none/ready state");
				if (!gs_eos_updater_call_poll_sync (priv->updater_proxy,
								    cancellable, error)) {
					gs_eos_updater_error_convert (error);
					return FALSE;
				}
			} else {
				/* Display an error to the user. */
				g_autoptr(GError) error_local = NULL;
				g_autoptr(GsPluginEvent) event = gs_plugin_event_new ();
				g_set_error_literal (&error_local, GS_PLUGIN_ERROR,
						     GS_PLUGIN_ERROR_FAILED,
						     _("EOS update service could not fetch and apply the update."));
				gs_eos_updater_error_convert (&error_local);
				gs_plugin_event_set_app (event, app);
				gs_plugin_event_set_action (event, GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD);
				gs_plugin_event_set_error (event, error_local);
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
				gs_plugin_report_event (plugin, event);

				/* Error out. */
				done = TRUE;
			}

			break;
		} case EOS_UPDATER_STATE_POLLING: {
			/* Nothing to do here. */
			break;
		} case EOS_UPDATER_STATE_UPDATE_AVAILABLE: {
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

			break;
		}
		case EOS_UPDATER_STATE_FETCHING: {
			/* Nothing to do here. */
			break;
		}
		case EOS_UPDATER_STATE_UPDATE_READY: {
			/* if there's an update ready to deployed, and it was started by
			 * the user, we should proceed to applying the upgrade */
			gs_app_set_progress (app, max_progress_for_update);

			if (!gs_eos_updater_call_apply_sync (priv->updater_proxy,
							     cancellable, error)) {
				gs_eos_updater_error_convert (error);
				return FALSE;
			}

			break;
		}
		case EOS_UPDATER_STATE_APPLYING_UPDATE: {
			/* Nothing to do here. */
			break;
		}
		case EOS_UPDATER_STATE_UPDATE_APPLIED: {
			/* Done! */
			done = TRUE;
			break;
		}
		case EOS_UPDATER_STATE_ERROR: {
			const gchar *error_name;
			const gchar *error_message;
			g_autoptr(GError) error_local = NULL;

			error_name = gs_eos_updater_get_error_name (priv->updater_proxy);
			error_message = gs_eos_updater_get_error_message (priv->updater_proxy);
			error_local = g_dbus_error_new_for_dbus_error (error_name, error_message);

			/* Display an error to the user, unless they cancelled
			 * the download. */
			if (!eos_updater_error_is_cancelled (error_name)) {
				g_autoptr(GsPluginEvent) event = gs_plugin_event_new ();
				gs_eos_updater_error_convert (&error_local);
				gs_plugin_event_set_app (event, app);
				gs_plugin_event_set_action (event, GS_PLUGIN_ACTION_UPGRADE_DOWNLOAD);
				gs_plugin_event_set_error (event, error_local);
				gs_plugin_event_add_flag (event, GS_PLUGIN_EVENT_FLAG_WARNING);
				gs_plugin_report_event (plugin, event);
			}

			/* Unconditionally call Poll() to get the updater out
			 * of the error state and to allow the update to be
			 * displayed in the UI again and retried. Exit the
			 * state change loop immediately, though, to prevent
			 * possible endless loops between the Poll/Error
			 * states. */
			allow_restart = FALSE;
			g_debug ("Restarting OS upgrade on error");
			if (!gs_eos_updater_call_poll_sync (priv->updater_proxy,
							    cancellable, error)) {
				gs_eos_updater_error_convert (error);
				return FALSE;
			}

			/* Error out. */
			done = TRUE;

			break;
		}
		default:
			g_warning ("Encountered unknown eos-updater state: %u", state);
			break;
		}

		/* Block on the next state change. */
		if (!done &&
		    !wait_for_state_change_unlocked (plugin, cancellable, error)) {
			gs_eos_updater_error_convert (error);
			return FALSE;
		}
	}

	if (cancellable != NULL && cancelled_id != 0) {
		g_debug ("Disconnecting cancellable %p", cancellable);
		g_cancellable_disconnect (cancellable, cancelled_id);
	}

	/* Process the final state. */
	if (gs_eos_updater_get_state (priv->updater_proxy) == EOS_UPDATER_STATE_ERROR) {
		const gchar *error_name;
		const gchar *error_message;
		g_autoptr(GError) error_local = NULL;

		error_name = gs_eos_updater_get_error_name (priv->updater_proxy);
		error_message = gs_eos_updater_get_error_message (priv->updater_proxy);
		error_local = g_dbus_error_new_for_dbus_error (error_name, error_message);
		gs_eos_updater_error_convert (&error_local);
		g_propagate_error (error, g_steal_pointer (&error_local));

		return FALSE;
	} else if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		gs_eos_updater_error_convert (error);
		return FALSE;
	}

	return TRUE;
}
