/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
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

#include <glib/gi18n.h>
#include <gnome-software.h>
#include <xmlb.h>

#include "gs-appstream.h"

/*
 * SECTION:
 * Uses offline AppStream data to populate and refine package results.
 *
 * This plugin calls UpdatesChanged() if any of the AppStream stores are
 * changed in any way.
 *
 * Methods:     | AddCategory
 * Refines:     | [source]->[name,summary,pixbuf,id,kind]
 */

struct GsPluginData {
	XbSilo			*silo;
	GSettings		*settings;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	/* need package name */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "dpkg");

	/* require settings */
	priv->settings = g_settings_new ("org.gnome.software");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_object_unref (priv->silo);
	g_object_unref (priv->settings);
}

static gboolean
gs_plugin_appstream_upgrade_cb (XbBuilderSource *self,
				XbBuilderNode *bn,
				gpointer user_data,
				GError **error)
{
	if (g_strcmp0 (xb_builder_node_get_element (bn), "application") == 0) {
		GPtrArray *children = xb_builder_node_get_children (bn);
		g_autofree gchar *kind = NULL;
		for (guint i = 0; i < children->len; i++) {
			XbBuilderNode *bc = g_ptr_array_index (children, i);
			if (g_strcmp0 (xb_builder_node_get_element (bc), "id") == 0) {
				kind = g_strdup (xb_builder_node_get_attribute (bc, "type"));
				xb_builder_node_remove_attr (bc, "type");
				break;
			}
		}
		if (kind != NULL)
			xb_builder_node_set_attr (bn, "type", kind);
		xb_builder_node_set_element (bn, "component");
	} else if (g_strcmp0 (xb_builder_node_get_element (bn), "metadata") == 0) {
		xb_builder_node_set_element (bn, "custom");
	}
	return TRUE;
}

static gboolean
gs_plugin_appstream_load_appdata (GsPlugin *plugin,
				  XbBuilder *builder,
				  const gchar *path,
				  GCancellable *cancellable,
				  GError **error)
{
	const gchar *fn;
	g_autoptr(GDir) dir = g_dir_open (path, 0, error);
	g_autoptr(GFile) parent = g_file_new_for_path (path);
	if (!g_file_query_exists (parent, cancellable))
		return TRUE;
	if (dir == NULL)
		return FALSE;
	while ((fn = g_dir_read_name (dir)) != NULL) {
		if (g_str_has_suffix (fn, ".appdata.xml") ||
		    g_str_has_suffix (fn, ".metainfo.xml")) {
			g_autofree gchar *filename = g_build_filename (path, fn, NULL);
			g_autoptr(GFile) file = g_file_new_for_path (filename);
			g_autoptr(XbBuilderSource) source = NULL;

			/* add source */
			source = xb_builder_source_new_file (file,
							     XB_BUILDER_SOURCE_FLAG_WATCH_FILE,
							     cancellable,
							     error);
			if (source == NULL)
				return FALSE;

			/* fix up any legacy installed files */
			xb_builder_source_add_node_func (source,
							 gs_plugin_appstream_upgrade_cb,
							 plugin, NULL);

			/* success */
			xb_builder_import_source (builder, source);
		}
	}

	/* success */
	return TRUE;
}

static gboolean
gs_plugin_appstream_check_silo (GsPlugin *plugin,
				GCancellable *cancellable,
				GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *test_xml;
	g_autofree gchar *blobfn = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new ();
	g_autoptr(XbNode) n = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GPtrArray) parent_appdata = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GPtrArray) parent_appstream = g_ptr_array_new_with_free_func (g_free);
	const gchar *const *locales = g_get_language_names ();

	/* everything is okay */
	if (priv->silo != NULL && xb_silo_is_valid (priv->silo))
		return TRUE;

	/* drat! silo needs regenerating */
	g_clear_object (&priv->silo);

	/* add current locales */
	for (guint i = 0; locales[i] != NULL; i++)
		xb_builder_add_locale (builder, locales[i]);

	/* only when in self test */
	test_xml = g_getenv ("GS_SELF_TEST_APPSTREAM_XML");
	if (test_xml != NULL) {
		if (!xb_builder_import_xml (builder, test_xml,
					    XB_BUILDER_SOURCE_FLAG_NONE,
					    error))
			return FALSE;
	} else {
		/* add search paths */
		g_ptr_array_add (parent_appstream,
				 g_build_filename ("/usr/share", "app-info", "xmls", NULL));
		g_ptr_array_add (parent_appdata,
				 g_build_filename ("/usr/share", "appdata", NULL));
		g_ptr_array_add (parent_appdata,
				 g_build_filename ("/usr/share", "metainfo", NULL));

		/* import all files */
		for (guint i = 0; i < parent_appstream->len; i++) {
			const gchar *fn = g_ptr_array_index (parent_appstream, i);
			if (!xb_builder_import_dir (builder, fn,
						    XB_BUILDER_SOURCE_FLAG_WATCH_FILE |
						    XB_BUILDER_SOURCE_FLAG_LITERAL_TEXT,
						    cancellable, error))
				return FALSE;
		}
		for (guint i = 0; i < parent_appdata->len; i++) {
			const gchar *fn = g_ptr_array_index (parent_appdata, i);
			if (!gs_plugin_appstream_load_appdata (plugin, builder, fn,
							       cancellable, error))
				return FALSE;
		}
	}

	/* create per-user cache */
	blobfn = gs_utils_get_cache_filename ("appstream", "components.xmlb",
					      GS_UTILS_CACHE_FLAG_WRITEABLE,
					      error);
	if (blobfn == NULL)
		return FALSE;
	file = g_file_new_for_path (blobfn);
	g_debug ("ensuring %s", blobfn);
	priv->silo = xb_builder_ensure (builder, file,
					XB_BUILDER_COMPILE_FLAG_IGNORE_INVALID |
					XB_BUILDER_COMPILE_FLAG_SINGLE_LANG,
					NULL, error);
	if (priv->silo == NULL)
		return FALSE;

	/* watch all directories too */
	for (guint i = 0; i < parent_appstream->len; i++) {
		const gchar *fn = g_ptr_array_index (parent_appstream, i);
		g_autoptr(GFile) file_tmp = g_file_new_for_path (fn);
		if (!xb_silo_watch_file (priv->silo, file_tmp, cancellable, error))
			return FALSE;
	}
	for (guint i = 0; i < parent_appdata->len; i++) {
		const gchar *fn = g_ptr_array_index (parent_appdata, i);
		g_autoptr(GFile) file_tmp = g_file_new_for_path (fn);
		if (!xb_silo_watch_file (priv->silo, file_tmp, cancellable, error))
			return FALSE;
	}

	/* test we found something */
	n = xb_silo_query_first (priv->silo, "components/component", NULL);
	if (n == NULL) {
		g_warning ("No AppStream data, try 'make install-sample-data' in data/");
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "No AppStream data found");
		return FALSE;
	}

	/* success */
	return TRUE;
}

gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
	/* set up silo, compiling if required */
	return gs_plugin_appstream_check_silo (plugin, cancellable, error);
}

gboolean
gs_plugin_url_to_app (GsPlugin *plugin,
		      GsAppList *list,
		      const gchar *url,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(XbNode) component = NULL;
	g_autofree gchar *path = NULL;
	g_autofree gchar *scheme = NULL;
	g_autofree gchar *xpath = NULL;
	g_autoptr(GsApp) app = NULL;

	/* check silo is valid */
	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;

	/* not us */
	scheme = gs_utils_get_url_scheme (url);
	if (g_strcmp0 (scheme, "appstream") != 0)
		return TRUE;

	/* create app */
	path = gs_utils_get_url_path (url);
	xpath = g_strdup_printf ("components/component/id[text()='%s']", path);
	component = xb_silo_query_first (priv->silo, xpath, NULL);
	if (component == NULL)
		return TRUE;
	app = gs_appstream_create_app (plugin, priv->silo, component, error);
	if (app == NULL)
		return FALSE;
	gs_app_list_add (list, app);
	return TRUE;
}

static void
gs_plugin_appstream_set_compulsory_quirk (GsApp *app, XbNode *component)
{
	g_autoptr(GPtrArray) array = NULL;
	const gchar *current_desktop;

	/*
	 * Set the core applications for the current desktop that cannot be
	 * removed.
	 *
	 * If XDG_CURRENT_DESKTOP contains ":", indicating that it is made up
	 * of multiple components per the Desktop Entry Specification, an app
	 * is compulsory if any of the components in XDG_CURRENT_DESKTOP match
	 * any value in <compulsory_for_desktops />. In that way,
	 * "GNOME-Classic:GNOME" shares compulsory apps with GNOME.
	 *
	 * As a special case, if the <compulsory_for_desktop /> value contains
	 * a ":", we match the entire XDG_CURRENT_DESKTOP. This lets people set
	 * compulsory apps for such compound desktops if they want.
	 *
	 */
	array = xb_node_query (component, "compulsory_for_desktop", 0, NULL);
	if (array == NULL)
		return;
	current_desktop = g_getenv ("XDG_CURRENT_DESKTOP");
	if (current_desktop != NULL) {
		g_auto(GStrv) xdg_current_desktops = g_strsplit (current_desktop, ":", 0);
		for (guint i = 0; i < array->len; i++) {
			XbNode *n = g_ptr_array_index (array, i);
			const gchar *tmp = xb_node_get_text (n);
			/* if the value has a :, check the whole string */
			if (g_strstr_len (tmp, -1, ":")) {
				if (g_strcmp0 (current_desktop, tmp) == 0) {
					gs_app_add_quirk (app, AS_APP_QUIRK_COMPULSORY);
					break;
				}
			/* otherwise check if any element matches this one */
			} else if (g_strv_contains ((const gchar * const *) xdg_current_desktops, tmp)) {
				gs_app_add_quirk (app, AS_APP_QUIRK_COMPULSORY);
				break;
			}
		}
	}
}

static gboolean
gs_plugin_refine_from_id (GsPlugin *plugin,
			  GsApp *app,
			  GsPluginRefineFlags flags,
			  gboolean *found,
			  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *id;
	g_autofree gchar *xpath = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) components = NULL;

	/* not enough info to find */
	id = gs_app_get_id (app);
	if (id == NULL)
		return TRUE;

	/* nothing found */
	g_debug ("searching appstream for %s", id);

	/* find all apps when matching any prefixes */
	xpath = g_strdup_printf ("components/component/id[text()='%s']/..", id);
	components = xb_silo_query (priv->silo, xpath, 0, &error_local);
	if (components == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);
		if (!gs_appstream_refine_app (plugin, app, priv->silo,
					      component, flags, error))
			return FALSE;
		gs_plugin_appstream_set_compulsory_quirk (app, component);
	}

	/* success */
	*found = TRUE;
	return TRUE;
}

static gboolean
gs_plugin_refine_from_pkgname (GsPlugin *plugin,
			       GsApp *app,
			       GsPluginRefineFlags flags,
			       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	GPtrArray *sources = gs_app_get_sources (app);
	g_autoptr(GError) error_local = NULL;

	/* not enough info to find */
	if (sources->len == 0)
		return TRUE;

	/* find all apps when matching any prefixes */
	for (guint j = 0; j < sources->len; j++) {
		const gchar *pkgname = g_ptr_array_index (sources, j);
		g_autofree gchar *xpath = NULL;
		g_autoptr(GPtrArray) components = NULL;

		g_debug ("searching appstream for pkg %s", pkgname);
		xpath = g_strdup_printf ("components/component/pkgname[text()='%s']/..",
					 pkgname);
		components = xb_silo_query (priv->silo, xpath, 0, &error_local);
		if (components == NULL) {
			if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
				continue;
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
		for (guint i = 0; i < components->len; i++) {
			XbNode *component = g_ptr_array_index (components, i);
			if (!gs_appstream_refine_app (plugin, app, priv->silo,
						      component, flags, error))
				return FALSE;
			gs_plugin_appstream_set_compulsory_quirk (app, component);
		}
	}

	/* success */
	return TRUE;
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	gboolean found = FALSE;

	/* check silo is valid */
	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;

	/* find by ID then fall back to package name */
	if (!gs_plugin_refine_from_id (plugin, app, flags, &found, error))
		return FALSE;
	if (!found) {
		if (!gs_plugin_refine_from_pkgname (plugin, app, flags, error))
			return FALSE;
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
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *id;
	g_autofree gchar *xpath = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) components = NULL;

	/* check silo is valid */
	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;

	/* not enough info to find */
	id = gs_app_get_id (app);
	if (id == NULL)
		return TRUE;

	/* find all apps when matching any prefixes */
	xpath = g_strdup_printf ("components/component/id[text()='%s']/..", id);
	components = xb_silo_query (priv->silo, xpath, 0, &error_local);
	if (components == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);
		g_autoptr(GsApp) new = NULL;

		/* does the app have an installation method */
		if (xb_node_query_text (component, "pkgname", NULL) == NULL) {
			g_debug ("not using %s for wildcard as no pkgname",
				 xb_node_query_text (component, "id", NULL));
			continue;
		}

		/* new app */
		g_debug ("found component for wildcard %s", id);
		new = gs_appstream_create_app (plugin, priv->silo, component, error);
		if (new == NULL)
			return FALSE;
		gs_app_list_add (list, new);
	}

	/* success */
	return TRUE;
}

gboolean
gs_plugin_add_category_apps (GsPlugin *plugin,
			     GsCategory *category,
			     GsAppList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;
	return gs_appstream_silo_add_category_apps (plugin,
						    priv->silo,
						    category,
						    list,
						    cancellable,
						    error);
}

gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      gchar **values,
		      GsAppList *list,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;
	return gs_appstream_silo_search (plugin,
					 priv->silo,
					 values,
					 list,
					 cancellable,
					 error);
}

gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GsAppList *list,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GPtrArray) components = NULL;

	/* check silo is valid */
	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;

	/* get all installed appdata files (notice no 'components/' prefix...) */
	components = xb_silo_query (priv->silo, "component", 0, NULL);
	if (components == NULL)
		return TRUE;
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);
		g_autoptr(GsApp) app = gs_appstream_create_app (plugin, priv->silo, component, error);
		if (app == NULL)
			return FALSE;
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		gs_app_list_add (list, app);
	}
	return TRUE;
}

gboolean
gs_plugin_add_categories (GsPlugin *plugin,
			  GPtrArray *list,
			  GCancellable *cancellable,
			  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;
	return gs_appstream_silo_add_categories (plugin, priv->silo, list,
						 cancellable, error);
}

gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;
	return gs_appstream_add_popular (plugin, priv->silo, list, cancellable, error);
}

gboolean
gs_plugin_add_featured (GsPlugin *plugin,
			GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;
	return gs_appstream_add_featured (plugin, priv->silo, list, cancellable, error);
}

gboolean
gs_plugin_add_recent (GsPlugin *plugin,
		      GsAppList *list,
		      guint64 age,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;
	return gs_appstream_add_recent (plugin, priv->silo, list, age,
					cancellable, error);
}

gboolean
gs_plugin_add_alternates (GsPlugin *plugin,
			  GsApp *app,
			  GsAppList *list,
			  GCancellable *cancellable,
			  GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;
	return gs_appstream_add_alternates (plugin, priv->silo, app, list,
					    cancellable, error);
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
		   guint cache_age,
		   GCancellable *cancellable,
		   GError **error)
{
	return gs_plugin_appstream_check_silo (plugin, cancellable, error);
}
