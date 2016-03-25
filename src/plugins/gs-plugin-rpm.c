/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include <fcntl.h>
#include <rpm/rpmdb.h>
#include <rpm/rpmlib.h>
#include <rpm/rpmts.h>

#include <gs-plugin.h>

struct GsPluginPrivate {
	rpmts			 ts;
	gboolean		 loaded;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "rpm";
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"appstream",	/* need application IDs */
		NULL };
	return deps;
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);

	/* only works with an rpmdb */
	if (!g_file_test ("/var/lib/rpm/Packages", G_FILE_TEST_EXISTS)) {
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}

	/* open transaction */
	rpmReadConfigFiles(NULL, NULL);
	plugin->priv->ts = rpmtsCreate();
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	if (plugin->priv->ts != NULL) {
		rpmtsCloseDB (plugin->priv->ts);
		rpmtsFree (plugin->priv->ts);
	}
}

/**
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	gint rc;

	/* already started */
	if (plugin->priv->loaded)
		return TRUE;

	/* open db readonly */
	rpmtsSetRootDir (plugin->priv->ts, NULL);
	rc = rpmtsOpenDB (plugin->priv->ts, O_RDONLY);
	if (rc != 0) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to open rpmdb: %i", rc);
		return FALSE;
	}
	plugin->priv->loaded = TRUE;
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
	Header h;
	const gchar *fn;
	rpmdbMatchIterator mi;

	/* required */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION) == 0 &&
	    (flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION) == 0)
		return TRUE;

	/* ensure database open */
	if (!gs_plugin_startup (plugin, cancellable, error))
		return FALSE;

	/* no need to run the plugin */
	if (gs_app_get_source_default (app) != NULL &&
	    gs_app_get_source_id_default (app) != NULL)
		return TRUE;

	/* look for a specific file */
	fn = gs_app_get_metadata_item (app, "appstream::source-file");
	if (fn == NULL)
		return TRUE;
	if (!g_str_has_prefix (fn, "/usr"))
		return TRUE;
	mi = rpmtsInitIterator(plugin->priv->ts, RPMDBI_INSTFILENAMES, fn, 0);
	if (mi == NULL) {
		g_debug ("rpm: no search results for %s", fn);
		return TRUE;
	}

	/* process any results */
	g_debug ("rpm: querying with %s", fn);
	while ((h = rpmdbNextIterator(mi)) != NULL) {
		guint64 epoch;
		const gchar *name;
		const gchar *version;
		const gchar *arch;
		const gchar *release;
		g_autofree gchar *tmp = NULL;

		/* add default source */
		name = headerGetString (h, RPMTAG_NAME);
		if (gs_app_get_source_default (app) == NULL) {
			g_debug ("rpm: setting source to %s", name);
			gs_app_add_source (app, name);
		}

		/* add version */
		version = headerGetString (h, RPMTAG_VERSION);
		if (gs_app_get_version (app) == NULL) {
			g_debug ("rpm: setting version to %s", version);
			gs_app_set_version (app, version);
		}

		/* add source-id */
		if (gs_app_get_source_id_default (app) == NULL) {
			release = headerGetString (h, RPMTAG_RELEASE);
			arch = headerGetString (h, RPMTAG_ARCH);
			epoch = headerGetNumber (h, RPMTAG_EPOCH);
			if (epoch > 0) {
				tmp = g_strdup_printf ("%s;%" G_GUINT64_FORMAT ":%s-%s;%s;installed",
						       name, epoch, version, release, arch);
			} else {
				tmp = g_strdup_printf ("%s;%s-%s;%s;installed",
						       name, version, release, arch);
			}
			g_debug ("rpm: setting source-id to %s", tmp);
			gs_app_add_source_id (app, tmp);
		}
	}
	rpmdbFreeIterator (mi);
	return TRUE;
}
