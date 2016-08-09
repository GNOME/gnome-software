/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2013 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

#include <gnome-software.h>

/*
 * SECTION:
 * Provides some dummy data that is useful in self test programs.
 */

struct GsPluginData {
	guint			 quirk_id;
	guint			 has_auth;
	GsAuth			*auth;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));
	if (g_getenv ("GS_SELF_TEST_DUMMY_ENABLE") == NULL) {
		g_debug ("disabling '%s' as not in self test",
			 gs_plugin_get_name (plugin));
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}

	/* set up a dummy authentication provider */
	priv->auth = gs_auth_new (gs_plugin_get_name (plugin));
	gs_auth_set_provider_name (priv->auth, "GNOME SSO");
	gs_auth_set_provider_logo (priv->auth, "/usr/share/pixmaps/gnome-about-logo.png");
	gs_auth_set_provider_uri (priv->auth, "http://www.gnome.org/sso");
	gs_plugin_add_auth (plugin, priv->auth);

	/* lets assume we read this from disk somewhere */
	gs_auth_set_username (priv->auth, "dummy");

	/* need help from appstream */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_CONFLICTS, "odrs");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (priv->quirk_id > 0)
		g_source_remove (priv->quirk_id);
}

void
gs_plugin_adopt_app (GsPlugin *plugin, GsApp *app)
{
	if (gs_app_get_id (app) != NULL &&
	    g_str_has_prefix (gs_app_get_id (app), "dummy:")) {
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
		return;
	}
	if (g_strcmp0 (gs_app_get_id (app), "mate-spell.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "chiron.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "zeus.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "zeus-spell.addon") == 0 ||
	    g_strcmp0 (gs_app_get_source_default (app), "chiron") == 0)
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
}

static gboolean
gs_plugin_dummy_delay (GsPlugin *plugin,
		       GsApp *app,
		       guint timeout_ms,
		       GCancellable *cancellable,
		       GError **error)
{
	gboolean ret = TRUE;
	guint i;
	guint timeout_us = timeout_ms * 10;

	/* do blocking delay in 1% increments */
	for (i = 0; i < 100; i++) {
		g_usleep (timeout_us);
		if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			ret = FALSE;
			break;
		}
		if (app != NULL)
			gs_app_set_progress (app, i);
		gs_plugin_status_update (plugin, app,
					 GS_PLUGIN_STATUS_DOWNLOADING);
	}
	return ret;
}

static gboolean
gs_plugin_dummy_poll_cb (gpointer user_data)
{
	g_autoptr(GsApp) app = NULL;
	GsPlugin *plugin = GS_PLUGIN (user_data);

	/* find the app in the per-plugin cache -- this assumes that we can
	 * calculate the same key as used when calling gs_plugin_cache_add() */
	app = gs_plugin_cache_lookup (plugin, "chiron");
	if (app == NULL) {
		g_warning ("app not found in cache!");
		return FALSE;
	}

	/* toggle this to animate the hide/show the 3rd party banner */
	if (!gs_app_has_quirk (app, AS_APP_QUIRK_PROVENANCE)) {
		g_debug ("about to make app distro-provided");
		gs_app_add_quirk (app, AS_APP_QUIRK_PROVENANCE);
	} else {
		g_debug ("about to make app 3rd party");
		gs_app_remove_quirk (app, AS_APP_QUIRK_PROVENANCE);
	}

	/* continue polling */
	return TRUE;
}

gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      gchar **values,
		      GsAppList *list,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GsApp) app = NULL;
	g_autoptr(AsIcon) ic = NULL;

	/* we're very specific */
	if (g_strcmp0 (values[0], "chiron") != 0)
		return TRUE;

	/* does the app already exist? */
	app = gs_plugin_cache_lookup (plugin, "chiron");
	if (app != NULL) {
		g_debug ("using %s fom the cache", gs_app_get_id (app));
		gs_app_list_add (list, app);
		return TRUE;
	}

	/* set up a timeout to emulate getting a GFileMonitor callback */
	priv->quirk_id =
		g_timeout_add_seconds (1, gs_plugin_dummy_poll_cb, plugin);

	/* use a generic stock icon */
	ic = as_icon_new ();
	as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
	as_icon_set_name (ic, "drive-harddisk");

	/* add a live updatable normal application */
	app = gs_app_new ("chiron.desktop");
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Chiron");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "A teaching application");
	gs_app_add_icon (app, ic);
	gs_app_set_size_installed (app, 42 * 1024 * 1024);
	gs_app_set_size_download (app, 50 * 1024 * 1024);
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	gs_app_set_metadata (app, "GnomeSoftware::Creator",
			     gs_plugin_get_name (plugin));
	gs_app_list_add (list, app);

	/* add to cache so it can be found by the flashing callback */
	gs_plugin_cache_add (plugin, "chiron", app);

	return TRUE;
}

gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsApp *app;
	g_autoptr(AsIcon) ic = NULL;

	/* update UI as this might take some time */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);

	/* spin */
	if (!gs_plugin_dummy_delay (plugin, NULL, 2000, cancellable, error))
		return FALSE;

	/* use a generic stock icon */
	ic = as_icon_new ();
	as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
	as_icon_set_name (ic, "drive-harddisk");

	/* add a live updatable normal application */
	app = gs_app_new ("chiron.desktop");
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Chiron");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "A teaching application");
	gs_app_set_update_details (app, "Do not crash when using libvirt.");
	gs_app_set_update_urgency (app, AS_URGENCY_KIND_HIGH);
	gs_app_add_icon (app, ic);
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);
	gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	gs_app_list_add (list, app);
	g_object_unref (app);

	/* add a offline OS update */
	app = gs_app_new (NULL);
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "libvirt-glib-devel");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "Development files for libvirt");
	gs_app_set_update_details (app, "Fix several memory leaks.");
	gs_app_set_update_urgency (app, AS_URGENCY_KIND_LOW);
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
	gs_app_add_source (app, "libvirt-glib-devel");
	gs_app_add_source_id (app, "libvirt-glib-devel;0.0.1;noarch;fedora");
	gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	gs_app_list_add (list, app);
	g_object_unref (app);

	/* add a live OS update */
	app = gs_app_new (NULL);
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "chiron-libs");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "library for chiron");
	gs_app_set_update_details (app, "Do not crash when using libvirt.");
	gs_app_set_update_urgency (app, AS_URGENCY_KIND_HIGH);
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);
	gs_app_add_source (app, "chiron-libs");
	gs_app_add_source_id (app, "chiron-libs;0.0.1;i386;updates-testing");
	gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	gs_app_list_add (list, app);
	g_object_unref (app);

	return TRUE;
}

gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GsAppList *list,
			 GCancellable *cancellable,
			 GError **error)
{
	const gchar *packages[] = { "zeus", "zeus-common", NULL };
	const gchar *app_ids[] = { "Uninstall Zeus.desktop", NULL };
	guint i;

	/* add all packages */
	for (i = 0; packages[i] != NULL; i++) {
		g_autoptr(GsApp) app = gs_app_new (NULL);
		gs_app_add_source (app, packages[i]);
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		gs_app_set_kind (app, AS_APP_KIND_GENERIC);
		gs_app_set_origin (app, "london-west");
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
		gs_app_list_add (list, app);
	}

	/* add all app-ids */
	for (i = 0; app_ids[i] != NULL; i++) {
		g_autoptr(GsApp) app = gs_app_new (app_ids[i]);
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
		gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
		gs_app_list_add (list, app);
	}

	return TRUE;
}

gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(GsApp) app1 = NULL;
	g_autoptr(GsApp) app2 = NULL;

	/* add wildcard */
	app1 = gs_app_new ("zeus.desktop");
	gs_app_add_quirk (app1, AS_APP_QUIRK_MATCH_ANY_PREFIX);
	gs_app_set_metadata (app1, "GnomeSoftware::Creator",
			     gs_plugin_get_name (plugin));
	gs_app_list_add (list, app1);

	/* add again, this time with a prefix so it gets deduplicated */
	app2 = gs_app_new ("zeus.desktop");
	gs_app_set_scope (app2, AS_APP_SCOPE_USER);
	gs_app_set_unique_id (app2, "user/dummy/*/*/zeus.desktop/*");
	gs_app_set_metadata (app2, "GnomeSoftware::Creator",
			     gs_plugin_get_name (plugin));
	gs_app_list_add (list, app2);
	return TRUE;
}

gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* remove app */
	if (g_strcmp0 (gs_app_get_id (app), "chiron.desktop") == 0) {
		gs_app_set_state (app, AS_APP_STATE_REMOVING);
		if (!gs_plugin_dummy_delay (plugin, app, 500, cancellable, error)) {
			gs_app_set_state_recover (app);
			return FALSE;
		}
		gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
	}
	return TRUE;
}

gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* install app */
	if (g_strcmp0 (gs_app_get_id (app), "chiron.desktop") == 0) {
		gs_app_set_state (app, AS_APP_STATE_INSTALLING);
		if (!gs_plugin_dummy_delay (plugin, app, 500, cancellable, error)) {
			gs_app_set_state_recover (app);
			return FALSE;
		}
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	}
	return TRUE;
}

gboolean
gs_plugin_update_app (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* always fail */
	g_set_error_literal (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NO_NETWORK,
			     "no network connection is available");
	return FALSE;
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	/* default */
	if (g_strcmp0 (gs_app_get_id (app), "chiron.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "mate-spell.desktop") == 0 ||
	    g_strcmp0 (gs_app_get_id (app), "zeus.desktop") == 0) {
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		if (gs_app_get_kind (app) == AS_APP_KIND_UNKNOWN)
			gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	}

	/* license */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE) {
		if (g_strcmp0 (gs_app_get_id (app), "chiron.desktop") == 0 ||
		    g_strcmp0 (gs_app_get_id (app), "zeus.desktop") == 0)
			gs_app_set_license (app, GS_APP_QUALITY_HIGHEST, "GPL-2.0+");
	}

	/* homepage */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL) {
		if (g_strcmp0 (gs_app_get_id (app), "chiron.desktop") == 0) {
			gs_app_set_url (app, AS_URL_KIND_HOMEPAGE,
					"http://www.test.org/");
		}
	}

	/* origin */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN) {
		if (g_strcmp0 (gs_app_get_id (app), "zeus-spell.addon") == 0)
			gs_app_set_origin (app, "london-east");
	}

	/* default */
	if (g_strcmp0 (gs_app_get_id (app), "chiron.desktop") == 0) {
		if (gs_app_get_name (app) == NULL)
			gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "tmp");
		if (gs_app_get_summary (app) == NULL)
			gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "tmp");
		if (gs_app_get_icons(app)->len == 0) {
			g_autoptr(AsIcon) ic = NULL;
			ic = as_icon_new ();
			as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
			as_icon_set_name (ic, "drive-harddisk");
			gs_app_add_icon (app, ic);
		}
	}

	/* description */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION) {
		if (g_strcmp0 (gs_app_get_id (app), "chiron.desktop") == 0) {
			gs_app_set_description (app, GS_APP_QUALITY_NORMAL,
						"long description!");
		}
	}

	/* add fake review */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS) {
		g_autoptr(AsReview) review1 = NULL;
		g_autoptr(AsReview) review2 = NULL;
		g_autoptr(GDateTime) dt = NULL;

		dt = g_date_time_new_now_utc ();

		/* set first review */
		review1 = as_review_new ();
		as_review_set_rating (review1, 50);
		as_review_set_reviewer_name (review1, "Angela Avery");
		as_review_set_summary (review1, "Steep learning curve, but worth it");
		as_review_set_description (review1, "Best overall 3D application I've ever used overall 3D application I've ever used. Best overall 3D application I've ever used overall 3D application I've ever used. Best overall 3D application I've ever used overall 3D application I've ever used. Best overall 3D application I've ever used overall 3D application I've ever used.");
		as_review_set_version (review1, "3.16.4");
		as_review_set_date (review1, dt);
		gs_app_add_review (app, review1);

		/* set self review */
		review2 = as_review_new ();
		as_review_set_rating (review2, 100);
		as_review_set_reviewer_name (review2, "Just Myself");
		as_review_set_summary (review2, "I like this application");
		as_review_set_description (review2, "I'm not very wordy myself.");
		as_review_set_version (review2, "3.16.3");
		as_review_set_date (review2, dt);
		as_review_set_flags (review2, AS_REVIEW_FLAG_SELF);
		gs_app_add_review (app, review2);
	}

	/* add fake ratings */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEW_RATINGS) {
		g_autoptr(GArray) ratings = NULL;
		const gint data[] = { 0, 10, 20, 30, 15, 2 };
		ratings = g_array_sized_new (FALSE, FALSE, sizeof (gint), 6);
		g_array_append_vals (ratings, data, 6);
		gs_app_set_review_ratings (app, ratings);
	}

	/* add a rating */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_RATING) {
		gs_app_set_rating (app, 66);
	}

	return TRUE;
}

gboolean
gs_plugin_add_category_apps (GsPlugin *plugin,
			     GsCategory *category,
			     GsAppList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	g_autoptr(GsApp) app = gs_app_new ("chiron.desktop");
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Chiron");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "View and use virtual machines");
	gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, "http://www.box.org");
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	gs_app_set_pixbuf (app, gdk_pixbuf_new_from_file ("/usr/share/icons/hicolor/48x48/apps/chiron.desktop.png", NULL));
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	gs_app_list_add (list, app);
	return TRUE;
}

gboolean
gs_plugin_add_distro_upgrades (GsPlugin *plugin,
			       GsAppList *list,
			       GCancellable *cancellable,
			       GError **error)
{
	g_autoptr(GsApp) app = NULL;
	g_autoptr(AsIcon) ic = NULL;

	/* use stock icon */
	ic = as_icon_new ();
	as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
	as_icon_set_name (ic, "application-x-addon");

	/* get existing item from the cache */
	app = gs_plugin_cache_lookup (plugin, "release-rawhide");
	if (app != NULL) {
		gs_app_list_add (list, app);
		return TRUE;
	}

	app = gs_app_new ("org.fedoraproject.release-rawhide.upgrade");
	gs_app_set_kind (app, AS_APP_KIND_OS_UPGRADE);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, "Fedora");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
			    "A major upgrade, with new features and added polish.");
	gs_app_set_description (app, GS_APP_QUALITY_LOWEST,
				"Dummy Core is a unfinished, overdesigned, "
				"hard to use operating system unikernel for "
				"Apollo industrial flight computers, with an "
				"incomplete set of tools for almost everyone "
				"including idiots of all kinds.");
	gs_app_set_url (app, AS_URL_KIND_HOMEPAGE,
			"https://fedoraproject.org/wiki/Releases/24/Schedule");
	gs_app_add_quirk (app, AS_APP_QUIRK_NEEDS_REBOOT);
	gs_app_add_quirk (app, AS_APP_QUIRK_PROVENANCE);
	gs_app_add_quirk (app, AS_APP_QUIRK_NOT_REVIEWABLE);
	gs_app_set_version (app, "25");
	gs_app_set_size_installed (app, 256 * 1024 * 1024);
	gs_app_set_size_download (app, 1024 * 1024 * 1024);
	gs_app_set_license (app, GS_APP_QUALITY_LOWEST, "LicenseRef-free");
	gs_app_set_origin_ui (app, "Dummy");
	gs_app_set_management_plugin (app, gs_plugin_get_name (plugin));
	gs_app_set_metadata (app, "GnomeSoftware::UpgradeBanner-css",
			     "background: url('" DATADIR "/gnome-software/upgrade-bg.png');"
			     "background-size: 100% 100%;");
	gs_app_add_icon (app, ic);
	gs_app_list_add (list, app);

	gs_plugin_cache_add (plugin, "release-rawhide", app);

	return TRUE;
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GsPluginRefreshFlags flags,
		   GCancellable *cancellable,
		   GError **error)
{
	guint delay_ms = 100;
	g_autoptr(GsApp) app = gs_app_new (NULL);

	/* each one takes more time */
	if (flags & GS_PLUGIN_REFRESH_FLAGS_METADATA)
		delay_ms += 3000;
	if (flags & GS_PLUGIN_REFRESH_FLAGS_PAYLOAD)
		delay_ms += 5000;

	/* do delay */
	return gs_plugin_dummy_delay (plugin, app, delay_ms, cancellable, error);
}

gboolean
gs_plugin_app_upgrade_download (GsPlugin *plugin, GsApp *app,
			        GCancellable *cancellable, GError **error)
{
	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	g_debug ("starting download");
	gs_app_set_state (app, AS_APP_STATE_INSTALLING);
	if (!gs_plugin_dummy_delay (plugin, app, 5000, cancellable, error)) {
		gs_app_set_state_recover (app);
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_UPDATABLE);
	return TRUE;
}

gboolean
gs_plugin_app_upgrade_trigger (GsPlugin *plugin, GsApp *app,
			       GCancellable *cancellable, GError **error)
{
	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app),
		       gs_plugin_get_name (plugin)) != 0)
		return TRUE;

	/* NOP */
	return TRUE;
}

gboolean
gs_plugin_update_cancel (GsPlugin *plugin, GsApp *app,
			 GCancellable *cancellable, GError **error)
{
	return TRUE;
}

gboolean
gs_plugin_review_submit (GsPlugin *plugin,
			 GsApp *app,
			 AsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	g_debug ("Submitting dummy review");
	return TRUE;
}

gboolean
gs_plugin_review_report (GsPlugin *plugin,
			 GsApp *app,
			 AsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	g_debug ("Reporting dummy review");
	as_review_add_flags (review, AS_REVIEW_FLAG_VOTED);
	return TRUE;
}

gboolean
gs_plugin_review_upvote (GsPlugin *plugin,
			 GsApp *app,
			 AsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	g_debug ("Upvoting dummy review");
	as_review_add_flags (review, AS_REVIEW_FLAG_VOTED);
	return TRUE;
}

gboolean
gs_plugin_review_downvote (GsPlugin *plugin,
			   GsApp *app,
			   AsReview *review,
			   GCancellable *cancellable,
			   GError **error)
{
	g_debug ("Downvoting dummy review");
	as_review_add_flags (review, AS_REVIEW_FLAG_VOTED);
	return TRUE;
}

gboolean
gs_plugin_review_remove (GsPlugin *plugin,
			 GsApp *app,
			 AsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* simulate an auth check */
	if (!priv->has_auth) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_AUTH_REQUIRED,
			     "authentication is required using @%s",
			     gs_plugin_get_name (plugin));
		return FALSE;
	}

	/* all okay */
	g_debug ("Removing dummy self-review");
	return TRUE;
}

gboolean
gs_plugin_auth_login (GsPlugin *plugin, GsAuth *auth,
		      GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* not us */
	if (g_strcmp0 (gs_auth_get_provider_id (auth),
		       gs_auth_get_provider_id (priv->auth)) != 0)
		return TRUE;

	/* already done */
	if (priv->has_auth) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "authentication already done");
		return FALSE;
	}

	/* check username and password */
	if (g_strcmp0 (gs_auth_get_username (priv->auth), "dummy") != 0 ||
	    g_strcmp0 (gs_auth_get_password (priv->auth), "dummy") != 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_AUTH_INVALID,
			     "The password was not correct.");
		return FALSE;
	}

	priv->has_auth = TRUE;
	gs_auth_add_flags (priv->auth, GS_AUTH_FLAG_VALID);
	g_debug ("dummy now authenticated");
	return TRUE;
}

gboolean
gs_plugin_auth_logout (GsPlugin *plugin, GsAuth *auth,
		       GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* not us */
	if (g_strcmp0 (gs_auth_get_provider_id (auth),
		       gs_auth_get_provider_id (priv->auth)) != 0)
		return TRUE;

	/* not done */
	if (!priv->has_auth) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "authentication not already done");
		return FALSE;
	}
	priv->has_auth = FALSE;
	gs_auth_set_flags (priv->auth, 0);
	g_debug ("dummy now not authenticated");
	return TRUE;
}

gboolean
gs_plugin_auth_lost_password (GsPlugin *plugin, GsAuth *auth,
			      GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* not us */
	if (g_strcmp0 (gs_auth_get_provider_id (auth),
		       gs_auth_get_provider_id (priv->auth)) != 0)
		return TRUE;

	/* return with data */
	g_set_error_literal (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_AUTH_INVALID,
			     "do online using @http://www.gnome.org/lost-password/");
	return FALSE;
}

gboolean
gs_plugin_auth_register (GsPlugin *plugin, GsAuth *auth,
			 GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* not us */
	if (g_strcmp0 (gs_auth_get_provider_id (auth),
		       gs_auth_get_provider_id (priv->auth)) != 0)
		return TRUE;

	/* return with data */
	g_set_error_literal (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_AUTH_INVALID,
			     "do online using @http://www.gnome.org/register/");
	return FALSE;
}
