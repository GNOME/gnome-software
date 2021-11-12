/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include <config.h>

#include <gnome-software.h>

#include "gs-plugin-os-release.h"

struct _GsPluginOsRelease
{
	GsPlugin		 parent;

	GsApp			*app_system;
};

G_DEFINE_TYPE (GsPluginOsRelease, gs_plugin_os_release, GS_TYPE_PLUGIN)

static void
gs_plugin_os_release_dispose (GObject *object)
{
	GsPluginOsRelease *self = GS_PLUGIN_OS_RELEASE (object);

	g_clear_object (&self->app_system);

	G_OBJECT_CLASS (gs_plugin_os_release_parent_class)->dispose (object);
}

static void
gs_plugin_os_release_init (GsPluginOsRelease *self)
{
	self->app_system = gs_app_new ("system");
	gs_app_set_kind (self->app_system, AS_COMPONENT_KIND_OPERATING_SYSTEM);
	gs_app_set_state (self->app_system, GS_APP_STATE_INSTALLED);
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	GsPluginOsRelease *self = GS_PLUGIN_OS_RELEASE (plugin);
	const gchar *cpe_name;
	const gchar *home_url;
	const gchar *name;
	const gchar *version;
	const gchar *os_id;
	g_autoptr(GsOsRelease) os_release = NULL;

	/* parse os-release, wherever it may be */
	os_release = gs_os_release_new (error);
	if (os_release == NULL)
		return FALSE;
	cpe_name = gs_os_release_get_cpe_name (os_release);
	if (cpe_name != NULL)
		gs_app_set_metadata (self->app_system, "GnomeSoftware::CpeName", cpe_name);
	name = gs_os_release_get_name (os_release);
	if (name != NULL)
		gs_app_set_name (self->app_system, GS_APP_QUALITY_LOWEST, name);
	version = gs_os_release_get_version_id (os_release);
	if (version != NULL)
		gs_app_set_version (self->app_system, version);

	os_id = gs_os_release_get_id (os_release);

	/* use libsoup to convert a URL */
	home_url = gs_os_release_get_home_url (os_release);
	if (home_url != NULL) {
		g_autoptr(GUri) uri = NULL;

		/* homepage */
		gs_app_set_url (self->app_system, AS_URL_KIND_HOMEPAGE, home_url);

		/* Build ID from the reverse-DNS URL and the ID and version. */
		uri = g_uri_parse (home_url, SOUP_HTTP_URI_FLAGS, NULL);
		if (uri != NULL) {
			g_auto(GStrv) split = NULL;
			const gchar *home_host = g_uri_get_host (uri);
			split = g_strsplit_set (home_host, ".", -1);
			if (g_strv_length (split) >= 2) {
				g_autofree gchar *id = NULL;
				id = g_strdup_printf ("%s.%s.%s-%s",
						      split[1],
						      split[0],
						      (os_id != NULL) ? os_id : "unnamed",
						      (version != NULL) ? version : "unversioned");
				gs_app_set_id (self->app_system, id);
			}
		}
	}

	/* success */
	return TRUE;
}

gboolean
gs_plugin_refine_wildcard (GsPlugin *plugin,
			   GsApp *app,
			   GsAppList *list,
			   GsPluginRefineFlags flags,
			   GCancellable *cancellable,
			   GError **error)
{
	GsPluginOsRelease *self = GS_PLUGIN_OS_RELEASE (plugin);

	/* match meta-id */
	if (g_strcmp0 (gs_app_get_id (app), "system") == 0) {
		/* copy over interesting metadata */
		if (gs_app_get_install_date (app) != 0 &&
		    gs_app_get_install_date (self->app_system) == 0) {
			gs_app_set_install_date (self->app_system,
			                         gs_app_get_install_date (app));
		}

		gs_app_list_add (list, self->app_system);
		return TRUE;
	}

	/* success */
	return TRUE;
}

static void
gs_plugin_os_release_class_init (GsPluginOsReleaseClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = gs_plugin_os_release_dispose;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_OS_RELEASE;
}
