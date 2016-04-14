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

#include <gs-plugin.h>

/*
 * SECTION:
 * Provides some dummy data that is useful in self test programs.
 *
 * Methods:     | Search, AddUpdates, AddInstalled, AddPopular
 * Refines:     | [id]->[name], [id]->[summary]
 */

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "dummy";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	if (g_getenv ("GNOME_SOFTWARE_SELF_TEST") == NULL) {
		g_debug ("disabling '%s' as not in self test", plugin->name);
		gs_plugin_set_enabled (plugin, FALSE);
	}
}

/**
 * gs_plugin_add_updates:
 */
gboolean
gs_plugin_add_updates (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsApp *app;

	/* update UI as this might take some time */
	gs_plugin_status_update (plugin, NULL, GS_PLUGIN_STATUS_WAITING);

	/* spin */
	g_usleep (2 * G_USEC_PER_SEC);

	/* add a normal application */
	app = gs_app_new ("gnome-boxes");
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Boxes");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "Do not segfault when using newer versons of libvirt.");
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_plugin_add_app (list, app);
	g_object_unref (app);

	/* add an OS update */
	app = gs_app_new ("libvirt-glib-devel;0.0.1;noarch;fedora");
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "libvirt-glib-devel");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "Fix several memory leaks.");
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_plugin_add_app (list, app);
	g_object_unref (app);

	/* add a second OS update */
	app = gs_app_new ("gnome-boxes-libs;0.0.1;i386;updates-testing");
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "gnome-boxes-libs");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "Do not segfault when using newer versons of libvirt.");
	gs_app_set_kind (app, AS_APP_KIND_GENERIC);
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_plugin_add_app (list, app);
	g_object_unref (app);

	return TRUE;
}

/**
 * gs_plugin_add_installed:
 */
gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GList **list,
			 GCancellable *cancellable,
			 GError **error)
{
	g_autoptr(GsApp) app = gs_app_new ("gnome-power-manager");
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Power Manager");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "Power Management Program");
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_plugin_add_app (list, app);
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);

	return TRUE;
}

/**
 * gs_plugin_add_popular:
 */
gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(GsApp) app = gs_app_new ("gnome-power-manager");
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Power Manager");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "Power Management Program");
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_plugin_add_app (list, app);
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);

	return TRUE;
}

/**
 * gs_plugin_refine_app:
 */
gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	/* pkgname */
	if (gs_app_get_name (app) == NULL) {
		if (g_strcmp0 (gs_app_get_id (app), "gnome-boxes") == 0) {
			gs_app_set_license (app, GS_APP_QUALITY_NORMAL,
					    "GPL-2.0+");
			gs_app_set_name (app, GS_APP_QUALITY_NORMAL,
					 "Boxes");
			gs_app_set_url (app, AS_URL_KIND_HOMEPAGE,
					"http://www.gimp.org/");
			gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
					    "A simple GNOME 3 application "
					    "to access remote or virtual systems");
			gs_app_set_description (app, GS_APP_QUALITY_NORMAL,
						"<p>long description!</p>");
		}
	}

	/* add fake review */
	if (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_REVIEWS) {
		g_autoptr(GsReview) review1 = NULL;
		g_autoptr(GsReview) review2 = NULL;
		g_autoptr(GDateTime) dt = NULL;

		dt = g_date_time_new_now_utc ();

		/* set first review */
		review1 = gs_review_new ();
		gs_review_set_rating (review1, 50);
		gs_review_set_reviewer (review1, "Angela Avery");
		gs_review_set_summary (review1, "Steep learning curve, but worth it");
		gs_review_set_text (review1, "Best overall 3D application I've ever used overall 3D application I've ever used. Best overall 3D application I've ever used overall 3D application I've ever used. Best overall 3D application I've ever used overall 3D application I've ever used. Best overall 3D application I've ever used overall 3D application I've ever used.");
		gs_review_set_version (review1, "3.16.4");
		gs_review_set_date (review1, dt);
		gs_app_add_review (app, review1);

		/* set self review */
		review2 = gs_review_new ();
		gs_review_set_rating (review2, 100);
		gs_review_set_reviewer (review2, "Just Myself");
		gs_review_set_summary (review2, "I like this application");
		gs_review_set_text (review2, "I'm not very wordy myself.");
		gs_review_set_version (review2, "3.16.3");
		gs_review_set_date (review2, dt);
		gs_review_set_flags (review2, GS_REVIEW_FLAG_SELF);
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

/**
 * gs_plugin_add_category_apps:
 */
gboolean
gs_plugin_add_category_apps (GsPlugin *plugin,
			     GsCategory *category,
			     GList **list,
			     GCancellable *cancellable,
			     GError **error)
{
	g_autoptr(GsApp) app = gs_app_new ("gnome-boxes");
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Boxes");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "View and use virtual machines");
	gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, "http://www.box.org");
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	gs_app_set_pixbuf (app, gdk_pixbuf_new_from_file ("/usr/share/icons/hicolor/48x48/apps/gnome-boxes.png", NULL));
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	gs_plugin_add_app (list, app);
	return TRUE;
}

/**
 * gs_plugin_add_distro_upgrades:
 */
gboolean
gs_plugin_add_distro_upgrades (GsPlugin *plugin,
			       GList **list,
			       GCancellable *cancellable,
			       GError **error)
{
	g_autoptr(GsApp) app = NULL;
	app = gs_app_new ("org.fedoraproject.release-24.upgrade");
	gs_app_set_kind (app, AS_APP_KIND_OS_UPGRADE);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST, "Fedora");
	gs_app_set_version (app, "24");
	gs_plugin_add_app (list, app);
	return TRUE;
}

typedef struct {
	GsPlugin	*plugin;
	GsApp		*app;
	GMainLoop	*loop;
	GCancellable	*cancellable;
	GError		**error;
	guint		 percentage;
} GsPluginDummyHelper;

/**
 * gs_plugin_dummy_delay_cb:
 */
static gboolean
gs_plugin_dummy_delay_cb (gpointer user_data)
{
	GsPluginDummyHelper *helper = (GsPluginDummyHelper *) user_data;
	helper->percentage += 10;
	if (helper->percentage >= 100) {
		g_main_loop_quit (helper->loop);
		return FALSE;
	}
	if (helper->error != NULL && *(helper->error) != NULL) {
		g_main_loop_quit (helper->loop);
		return FALSE;
	}
	g_debug ("dummy percentage=%i%%", helper->percentage);
	gs_app_set_progress (helper->app, helper->percentage);
	gs_plugin_status_update (helper->plugin,
				 helper->app,
				 GS_PLUGIN_STATUS_DOWNLOADING);
	return TRUE;
}

/**
 * gs_plugin_dummy_delay_cancel_cb:
 */
static void
gs_plugin_dummy_delay_cancel_cb (GCancellable *cancellable,
				 GsPluginDummyHelper *helper)
{
	g_debug ("dummy delay cancelled");
	g_cancellable_set_error_if_cancelled (cancellable, helper->error);
}

/**
 * gs_plugin_dummy_delay:
 */
static gboolean
gs_plugin_dummy_delay (GsPlugin *plugin,
		       GsApp *app,
		       guint timeout_ms,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autofree GsPluginDummyHelper *helper = g_new0 (GsPluginDummyHelper, 1);
	g_autoptr(GMainLoop) loop = g_main_loop_new (NULL, FALSE);

	/* cancel the delay on cancellation */
	if (cancellable != NULL) {
		g_cancellable_connect (cancellable,
				       G_CALLBACK (gs_plugin_dummy_delay_cancel_cb),
				       helper, NULL);
	}

	/* set up callbacks */
	helper->app = app;
	helper->cancellable = cancellable;
	helper->error = error;
	helper->loop = loop;
	helper->percentage = 0;
	helper->plugin = plugin;
	g_debug ("dummy waiting for %ims", timeout_ms);
	g_timeout_add (timeout_ms / 10, gs_plugin_dummy_delay_cb, helper);
	g_main_loop_run (loop);
	g_debug ("dummy done");
	return helper->error != NULL;
}

/**
 * gs_plugin_refresh:
 */
gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GsPluginRefreshFlags flags,
		   GCancellable *cancellable,
		   GError **error)
{
	guint delay_ms = 100;
	g_autoptr(GsApp) app = gs_app_new (NULL);

	/* do delay */
	return gs_plugin_dummy_delay (plugin, app, delay_ms, cancellable, error);
}

/**
 * gs_plugin_review_submit:
 */
gboolean
gs_plugin_review_submit (GsPlugin *plugin,
			 GsApp *app,
			 GsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	g_debug ("Submitting dummy review");
	return TRUE;
}

/**
 * gs_plugin_review_report:
 */
gboolean
gs_plugin_review_report (GsPlugin *plugin,
			 GsApp *app,
			 GsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	g_debug ("Reporting dummy review");
	gs_review_add_flags (review, GS_REVIEW_FLAG_VOTED);
	return TRUE;
}

/**
 * gs_plugin_review_upvote:
 */
gboolean
gs_plugin_review_upvote (GsPlugin *plugin,
			 GsApp *app,
			 GsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	g_debug ("Upvoting dummy review");
	gs_review_add_flags (review, GS_REVIEW_FLAG_VOTED);
	return TRUE;
}

/**
 * gs_plugin_review_downvote:
 */
gboolean
gs_plugin_review_downvote (GsPlugin *plugin,
			   GsApp *app,
			   GsReview *review,
			   GCancellable *cancellable,
			   GError **error)
{
	g_debug ("Downvoting dummy review");
	gs_review_add_flags (review, GS_REVIEW_FLAG_VOTED);
	return TRUE;
}

/**
 * gs_plugin_review_remove:
 */
gboolean
gs_plugin_review_remove (GsPlugin *plugin,
			 GsApp *app,
			 GsReview *review,
			 GCancellable *cancellable,
			 GError **error)
{
	g_debug ("Removing dummy self-review");
	return TRUE;
}
