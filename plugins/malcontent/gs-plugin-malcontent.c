/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018-2019 Endless Mobile
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <glib/gi18n.h>
#include <gnome-software.h>
#include <libmalcontent/malcontent.h>
#include <string.h>
#include <math.h>

/*
 * SECTION:
 * Adds the %GS_APP_QUIRK_PARENTAL_FILTER and
 * %GS_APP_QUIRK_PARENTAL_NOT_LAUNCHABLE quirks to applications if they
 * contravene the effective user’s current parental controls policy.
 *
 * Specifically, %GS_APP_QUIRK_PARENTAL_FILTER will be added if an app’s OARS
 * rating is too extreme for the current parental controls OARS policy.
 * %GS_APP_QUIRK_PARENTAL_NOT_LAUNCHABLE will be added if the app is listed on
 * the current parental controls blacklist.
 *
 * Parental controls policy is loaded using libmalcontent.
 *
 * This plugin is ordered after flatpak and appstream as it uses OARS data from
 * them.
 *
 * Limiting access to applications by not allowing them to be launched by
 * gnome-software is only one part of a wider approach to parental controls.
 * In order to guarantee users do not have access to applications they shouldn’t
 * have access to, an LSM (such as AppArmor) needs to be used. That complements,
 * rather than substitutes for, filtering in user visible UIs.
 */

struct GsPluginData {
	GMutex		 mutex;  /* protects @app_filter **/
	MctManager	*manager;  /* (owned) */
	gulong		 manager_app_filter_changed_id;
	MctAppFilter	*app_filter;  /* (mutex) (owned) (nullable) */
};

/* Convert an #MctAppFilterOarsValue to an #AsContentRatingValue. This is
 * actually a trivial cast, since the types are defined the same; but throw in
 * a static assertion to be sure. */
static AsContentRatingValue
convert_app_filter_oars_value (MctAppFilterOarsValue filter_value)
{
  G_STATIC_ASSERT (AS_CONTENT_RATING_VALUE_LAST == MCT_APP_FILTER_OARS_VALUE_INTENSE + 1);

  return (AsContentRatingValue) filter_value;
}

static gboolean
app_is_expected_to_have_content_rating (GsApp *app)
{
	if (gs_app_has_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE))
		return FALSE;

	switch (gs_app_get_kind (app)) {
	case AS_APP_KIND_ADDON:
	case AS_APP_KIND_CODEC:
	case AS_APP_KIND_DRIVER:
	case AS_APP_KIND_FIRMWARE:
	case AS_APP_KIND_FONT:
	case AS_APP_KIND_GENERIC:
	case AS_APP_KIND_INPUT_METHOD:
	case AS_APP_KIND_LOCALIZATION:
	case AS_APP_KIND_OS_UPDATE:
	case AS_APP_KIND_OS_UPGRADE:
	case AS_APP_KIND_RUNTIME:
	case AS_APP_KIND_SOURCE:
		return FALSE;
	case AS_APP_KIND_UNKNOWN:
	case AS_APP_KIND_DESKTOP:
	case AS_APP_KIND_WEB_APP:
	case AS_APP_KIND_SHELL_EXTENSION:
	case AS_APP_KIND_CONSOLE:
	default:
		break;
	}

	return TRUE;
}

/* Check whether the OARS rating for @app is as, or less, extreme than the
 * user’s preferences in @app_filter. If so (i.e. if the app is suitable for
 * this user to use), return %TRUE; otherwise return %FALSE.
 *
 * The #AsContentRating in @app may be %NULL if no OARS ratings are provided for
 * the app. If so, we have to assume the most restrictive ratings. However, if
 * @rating is provided but is empty, we assume that every section in it has
 * value %AS_CONTENT_RATING_VALUE_NONE. See
 * https://github.com/hughsie/oars/blob/master/specification/oars-1.1.md */
static gboolean
app_is_content_rating_appropriate (GsApp *app, MctAppFilter *app_filter)
{
	AsContentRating *rating = gs_app_get_content_rating (app);  /* (nullable) */
	g_autofree const gchar **oars_sections = mct_app_filter_get_oars_sections (app_filter);
	AsContentRatingValue default_rating_value;

	if (rating == NULL && !app_is_expected_to_have_content_rating (app)) {
		/* Some apps, such as flatpak runtimes, are not expected to have
		 * content ratings. */
		return TRUE;
	} else if (rating == NULL) {
		g_debug ("No OARS ratings provided for ‘%s’: assuming most extreme",
		         gs_app_get_unique_id (app));
		default_rating_value = AS_CONTENT_RATING_VALUE_INTENSE;
	} else {
		default_rating_value = AS_CONTENT_RATING_VALUE_NONE;
	}

	for (gsize i = 0; oars_sections[i] != NULL; i++) {
		AsContentRatingValue rating_value;
		MctAppFilterOarsValue filter_value;

		filter_value = mct_app_filter_get_oars_value (app_filter, oars_sections[i]);

		if (rating != NULL)
			rating_value = as_content_rating_get_value (rating, oars_sections[i]);
		else
			rating_value = AS_CONTENT_RATING_VALUE_UNKNOWN;

		if (rating_value == AS_CONTENT_RATING_VALUE_UNKNOWN)
			rating_value = default_rating_value;

		if (filter_value == MCT_APP_FILTER_OARS_VALUE_UNKNOWN)
			continue;
		else if (convert_app_filter_oars_value (filter_value) < rating_value)
			return FALSE;
	}

	return TRUE;
}

static gboolean
app_is_parentally_blacklisted (GsApp *app, MctAppFilter *app_filter)
{
	const gchar *desktop_id;
	g_autoptr(GAppInfo) appinfo = NULL;

	desktop_id = gs_app_get_id (app);
	if (desktop_id == NULL)
		return FALSE;
	appinfo = G_APP_INFO (gs_utils_get_desktop_app_info (desktop_id));
	if (appinfo == NULL)
		return FALSE;

	return !mct_app_filter_is_appinfo_allowed (app_filter, appinfo);
}

static gboolean
app_set_parental_quirks (GsPlugin *plugin, GsApp *app, MctAppFilter *app_filter)
{
	/* note that both quirks can be set on an app at the same time, and they
	 * have slightly different meanings */
	gboolean filtered = FALSE;

	/* check the OARS ratings to see if this app should be installable */
	if (!app_is_content_rating_appropriate (app, app_filter)) {
		g_debug ("Filtering ‘%s’: app OARS rating is too extreme for this user",
		         gs_app_get_unique_id (app));
		gs_app_add_quirk (app, GS_APP_QUIRK_PARENTAL_FILTER);
		filtered = TRUE;
	} else {
		gs_app_remove_quirk (app, GS_APP_QUIRK_PARENTAL_FILTER);
	}

	/* check the app blacklist to see if this app should be launchable */
	if (app_is_parentally_blacklisted (app, app_filter)) {
		g_debug ("Filtering ‘%s’: app is blacklisted for this user",
		         gs_app_get_unique_id (app));
		gs_app_add_quirk (app, GS_APP_QUIRK_PARENTAL_NOT_LAUNCHABLE);
		filtered = TRUE;
	} else {
		gs_app_remove_quirk (app, GS_APP_QUIRK_PARENTAL_NOT_LAUNCHABLE);
	}

	return filtered;
}

static MctAppFilter *
query_app_filter (GsPlugin      *plugin,
                  GCancellable  *cancellable,
                  GError       **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	return mct_manager_get_app_filter (priv->manager, getuid (),
					   MCT_GET_APP_FILTER_FLAGS_INTERACTIVE, cancellable,
					   error);
}

static void
app_filter_changed_cb (MctManager *manager,
                       guint64     user_id,
                       gpointer    user_data)
{
	GsPlugin *plugin = GS_PLUGIN (user_data);

	if (user_id == getuid ()) {
		/* The user’s app filter has changed, which means that different
		 * apps could be filtered from before. Reload everything to be
		 * sure of re-filtering correctly. */
		g_debug ("Reloading due to app filter changing for user %" G_GUINT64_FORMAT, user_id);
		gs_plugin_reload (plugin);
	}
}

void
gs_plugin_initialize (GsPlugin *plugin)
{
	gs_plugin_alloc_data (plugin, sizeof (GsPluginData));

	/* need application IDs and content ratings */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "flatpak");

	/* set plugin name; it’s not a loadable plugin, but this is descriptive and harmless */
	gs_plugin_set_appstream_id (plugin, "org.gnome.Software.Plugin.Malcontent");
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);
	g_autoptr(GDBusConnection) system_bus = NULL;

	system_bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, cancellable, error);
	if (system_bus == NULL)
		return FALSE;

	priv->manager = mct_manager_new (system_bus);
	priv->manager_app_filter_changed_id = g_signal_connect (priv->manager,
								"app-filter-changed",
								(GCallback) app_filter_changed_cb,
								plugin);
	priv->app_filter = query_app_filter (plugin, cancellable, error);

	return (priv->app_filter != NULL);
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	/* not valid */
	if (gs_app_get_id (app) == NULL)
		return TRUE;

	/* Filter by various parental filters. The filter can’t be %NULL,
	 * otherwise setup() would have failed and the plugin would have been
	 * disabled. */
	{
		g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);
		g_assert (priv->app_filter != NULL);

		app_set_parental_quirks (plugin, app, priv->app_filter);

		return TRUE;
	}
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GCancellable *cancellable,
		   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(MctAppFilter) new_app_filter = NULL;
	g_autoptr(MctAppFilter) old_app_filter = NULL;

	/* Refresh the app filter. This blocks on a D-Bus request. */
	new_app_filter = query_app_filter (plugin, cancellable, error);

	/* on failure, keep the old app filter around since it might be more
	 * useful than nothing */
	if (new_app_filter == NULL)
		return FALSE;

	{
		g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);
		old_app_filter = g_steal_pointer (&priv->app_filter);
		priv->app_filter = g_steal_pointer (&new_app_filter);
	}

	return TRUE;
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);

	g_clear_pointer (&priv->app_filter, mct_app_filter_unref);
	if (priv->manager != NULL && priv->manager_app_filter_changed_id != 0) {
		g_signal_handler_disconnect (priv->manager,
					     priv->manager_app_filter_changed_id);
		priv->manager_app_filter_changed_id = 0;
	}
	g_clear_object (&priv->manager);
}