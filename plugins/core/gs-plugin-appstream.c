/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2014 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2019 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
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
	GRWLock			 silo_lock;
	GSettings		*settings;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	/* XbSilo needs external locking as we destroy the silo and build a new
	 * one when something changes */
	g_rw_lock_init (&priv->silo_lock);

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
	g_rw_lock_clear (&priv->silo_lock);
}

static const gchar *
gs_plugin_appstream_convert_component_kind (const gchar *kind)
{
	if (g_strcmp0 (kind, "web-application") == 0)
		return "webapp";
	if (g_strcmp0 (kind, "console-application") == 0)
		return "console";
	return kind;
}

static gboolean
gs_plugin_appstream_override_app_id_cb (XbBuilderFixup *self,
                                        XbBuilderNode *bn,
                                        gpointer user_data,
                                        GError **error)
{
	if (g_strcmp0 (xb_builder_node_get_element (bn), "component") == 0) {
		g_autoptr(XbBuilderNode) id = xb_builder_node_get_child (bn, "id", NULL);
		g_autoptr(XbBuilderNode) launchable = xb_builder_node_get_child (bn, "launchable", NULL);

		if (launchable != NULL && id != NULL) {
			const gchar *type = xb_builder_node_get_attr (launchable, "type");

			if (g_strcmp0 (type, "desktop-id") == 0) {
				const gchar *app_id = xb_builder_node_get_text (id);
				const gchar *launchable_id = xb_builder_node_get_text (launchable);

				if (app_id != NULL &&
				    launchable_id != NULL &&
				    g_strcmp0 (app_id, launchable_id) != 0) {
					g_debug ("Overriding appdata app-id %s with <launchable> desktop-id: %s",
						 app_id, launchable_id);
					xb_builder_node_set_text (id, launchable_id, -1);
				}
			}
		}
	}
	return TRUE;
}

static gboolean
gs_plugin_appstream_upgrade_cb (XbBuilderFixup *self,
				XbBuilderNode *bn,
				gpointer user_data,
				GError **error)
{
	if (g_strcmp0 (xb_builder_node_get_element (bn), "application") == 0) {
		g_autoptr(XbBuilderNode) id = xb_builder_node_get_child (bn, "id", NULL);
		g_autofree gchar *kind = NULL;
		if (id != NULL) {
			kind = g_strdup (xb_builder_node_get_attr (id, "type"));
			xb_builder_node_remove_attr (id, "type");
		}
		if (kind != NULL)
			xb_builder_node_set_attr (bn, "type", kind);
		xb_builder_node_set_element (bn, "component");
	} else if (g_strcmp0 (xb_builder_node_get_element (bn), "metadata") == 0) {
		xb_builder_node_set_element (bn, "custom");
	} else if (g_strcmp0 (xb_builder_node_get_element (bn), "component") == 0) {
		const gchar *type_old = xb_builder_node_get_attr (bn, "type");
		const gchar *type_new = gs_plugin_appstream_convert_component_kind (type_old);
		if (type_old != type_new)
			xb_builder_node_set_attr (bn, "type", type_new);
	}
	return TRUE;
}

static gboolean
gs_plugin_appstream_add_icons_cb (XbBuilderFixup *self,
				  XbBuilderNode *bn,
				  gpointer user_data,
				  GError **error)
{
	GsPlugin *plugin = GS_PLUGIN (user_data);
	if (g_strcmp0 (xb_builder_node_get_element (bn), "component") != 0)
		return TRUE;
	gs_appstream_component_add_extra_info (plugin, bn);
	return TRUE;
}

static gboolean
gs_plugin_appstream_add_origin_keyword_cb (XbBuilderFixup *self,
					   XbBuilderNode *bn,
					   gpointer user_data,
					   GError **error)
{
	if (g_strcmp0 (xb_builder_node_get_element (bn), "components") == 0) {
		const gchar *origin = xb_builder_node_get_attr (bn, "origin");
		GPtrArray *components = xb_builder_node_get_children (bn);
		if (origin == NULL || origin[0] == '\0')
			return TRUE;
		g_debug ("origin %s has %u components", origin, components->len);
		if (components->len < 200) {
			for (guint i = 0; i < components->len; i++) {
				XbBuilderNode *component = g_ptr_array_index (components, i);
				gs_appstream_component_add_keyword (component, origin);
			}
		}
	}
	return TRUE;
}

static gboolean
gs_plugin_appstream_load_appdata_fn (GsPlugin *plugin,
				     XbBuilder *builder,
				     const gchar *filename,
				     GCancellable *cancellable,
				     GError **error)
{
	g_autoptr(GFile) file = g_file_new_for_path (filename);
	g_autoptr(XbBuilderFixup) fixup = NULL;
	g_autoptr(XbBuilderFixup) fixup1 = NULL;
	g_autoptr(XbBuilderNode) info = NULL;
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();

	/* add source */
	if (!xb_builder_source_load_file (source, file,
					  XB_BUILDER_SOURCE_FLAG_WATCH_FILE,
					  cancellable,
					  error)) {
		return FALSE;
	}

	/* fix up any legacy installed files */
	fixup = xb_builder_fixup_new ("AppStreamUpgrade2",
				      gs_plugin_appstream_upgrade_cb,
				      plugin, NULL);
	xb_builder_fixup_set_max_depth (fixup, 3);
	xb_builder_source_add_fixup (source, fixup);

	/* Override <id> with <launchable type="desktop-id"> to establish
	 * desktop file <> appdata mapping */
	fixup1 = xb_builder_fixup_new ("OverrideAppId",
				       gs_plugin_appstream_override_app_id_cb,
				       plugin, NULL);
	xb_builder_fixup_set_max_depth (fixup1, 2);
	xb_builder_source_add_fixup (source, fixup1);

	/* add metadata */
	info = xb_builder_node_insert (NULL, "info", NULL);
	xb_builder_node_insert_text (info, "filename", filename, NULL);
	xb_builder_source_set_info (source, info);

	/* success */
	xb_builder_import_source (builder, source);
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
			g_autoptr(GError) error_local = NULL;
			if (!gs_plugin_appstream_load_appdata_fn (plugin,
								  builder,
								  filename,
								  cancellable,
								  &error_local)) {
				g_debug ("ignoring %s: %s", filename, error_local->message);
				continue;
			}
		}
	}

	/* success */
	return TRUE;
}

static GInputStream *
gs_plugin_appstream_load_desktop_cb (XbBuilderSource *self,
				     XbBuilderSourceCtx *ctx,
				     gpointer user_data,
				     GCancellable *cancellable,
				     GError **error)
{
	GString *xml;
	g_autoptr(AsApp) app = as_app_new ();
	g_autoptr(GBytes) bytes = NULL;
	bytes = xb_builder_source_ctx_get_bytes (ctx, cancellable, error);
	if (bytes == NULL)
		return NULL;
	as_app_set_id (app, xb_builder_source_ctx_get_filename (ctx));
	if (!as_app_parse_data (app, bytes, AS_APP_PARSE_FLAG_USE_FALLBACKS, error))
		return NULL;
	xml = as_app_to_xml (app, error);
	if (xml == NULL)
		return NULL;
	g_string_prepend (xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	return g_memory_input_stream_new_from_data (g_string_free (xml, FALSE), -1, g_free);
}

static gboolean
gs_plugin_appstream_load_desktop_fn (GsPlugin *plugin,
				     XbBuilder *builder,
				     const gchar *filename,
				     GCancellable *cancellable,
				     GError **error)
{
	g_autoptr(GFile) file = g_file_new_for_path (filename);
	g_autoptr(XbBuilderFixup) fixup = NULL;
	g_autoptr(XbBuilderNode) info = NULL;
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();

	/* add support for desktop files */
	xb_builder_source_add_adapter (source, "application/x-desktop",
				       gs_plugin_appstream_load_desktop_cb, NULL, NULL);

	/* add source */
	if (!xb_builder_source_load_file (source, file,
					  XB_BUILDER_SOURCE_FLAG_WATCH_FILE,
					  cancellable,
					  error)) {
		return FALSE;
	}

	/* add metadata */
	info = xb_builder_node_insert (NULL, "info", NULL);
	xb_builder_node_insert_text (info, "filename", filename, NULL);
	xb_builder_source_set_info (source, info);

	/* success */
	xb_builder_import_source (builder, source);
	return TRUE;
}

static gboolean
gs_plugin_appstream_load_desktop (GsPlugin *plugin,
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
		if (g_str_has_suffix (fn, ".desktop")) {
			g_autofree gchar *filename = g_build_filename (path, fn, NULL);
			g_autoptr(GError) error_local = NULL;
			if (g_strcmp0 (fn, "mimeinfo.cache") == 0)
				continue;
			if (!gs_plugin_appstream_load_desktop_fn (plugin,
								  builder,
								  filename,
								  cancellable,
								  &error_local)) {
				g_debug ("ignoring %s: %s", filename, error_local->message);
				continue;
			}
		}
	}

	/* success */
	return TRUE;
}

static GInputStream *
gs_plugin_appstream_load_dep11_cb (XbBuilderSource *self,
				   XbBuilderSourceCtx *ctx,
				   gpointer user_data,
				   GCancellable *cancellable,
				   GError **error)
{
	GString *xml;
	g_autoptr(AsStore) store = as_store_new ();
	g_autoptr(GBytes) bytes = NULL;
	bytes = xb_builder_source_ctx_get_bytes (ctx, cancellable, error);
	if (bytes == NULL)
		return NULL;
	if (!as_store_from_bytes (store, bytes, cancellable, error))
		return FALSE;
	xml = as_store_to_xml (store, AS_NODE_INSERT_FLAG_NONE);
	if (xml == NULL)
		return NULL;
	g_string_prepend (xml, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	return g_memory_input_stream_new_from_data (g_string_free (xml, FALSE), -1, g_free);
}

static gboolean
gs_plugin_appstream_load_appstream_fn (GsPlugin *plugin,
				       XbBuilder *builder,
				       const gchar *filename,
				       GCancellable *cancellable,
				       GError **error)
{
	g_autoptr(GFile) file = g_file_new_for_path (filename);
	g_autoptr(XbBuilderNode) info = NULL;
	g_autoptr(XbBuilderFixup) fixup1 = NULL;
	g_autoptr(XbBuilderFixup) fixup2 = NULL;
	g_autoptr(XbBuilderFixup) fixup3 = NULL;
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();

	/* add support for DEP-11 files */
	xb_builder_source_add_adapter (source,
				       "application/x-yaml",
				       gs_plugin_appstream_load_dep11_cb,
				       NULL, NULL);

	/* add source */
	if (!xb_builder_source_load_file (source, file,
					  XB_BUILDER_SOURCE_FLAG_WATCH_FILE,
					  cancellable,
					  error)) {
		return FALSE;
	}

	/* add metadata */
	info = xb_builder_node_insert (NULL, "info", NULL);
	xb_builder_node_insert_text (info, "scope", "system", NULL);
	xb_builder_node_insert_text (info, "filename", filename, NULL);
	xb_builder_source_set_info (source, info);

	/* add missing icons as required */
	fixup1 = xb_builder_fixup_new ("AddIcons",
				       gs_plugin_appstream_add_icons_cb,
				       plugin, NULL);
	xb_builder_fixup_set_max_depth (fixup1, 2);
	xb_builder_source_add_fixup (source, fixup1);

	/* fix up any legacy installed files */
	fixup2 = xb_builder_fixup_new ("AppStreamUpgrade2",
				       gs_plugin_appstream_upgrade_cb,
				       plugin, NULL);
	xb_builder_fixup_set_max_depth (fixup2, 3);
	xb_builder_source_add_fixup (source, fixup2);

	/* add the origin as a search keyword for small repos */
	fixup3 = xb_builder_fixup_new ("AddOriginKeyword",
				       gs_plugin_appstream_add_origin_keyword_cb,
				       plugin, NULL);
	xb_builder_fixup_set_max_depth (fixup3, 1);
	xb_builder_source_add_fixup (source, fixup3);

	/* success */
	xb_builder_import_source (builder, source);
	return TRUE;
}

static gboolean
gs_plugin_appstream_load_appstream (GsPlugin *plugin,
				    XbBuilder *builder,
				    const gchar *path,
				    GCancellable *cancellable,
				    GError **error)
{
	const gchar *fn;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GFile) parent = g_file_new_for_path (path);

	/* parent patch does not exist */
	if (!g_file_query_exists (parent, cancellable))
		return TRUE;
	dir = g_dir_open (path, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((fn = g_dir_read_name (dir)) != NULL) {
		if (g_str_has_suffix (fn, ".xml") ||
		    g_str_has_suffix (fn, ".yml") ||
		    g_str_has_suffix (fn, ".yml.gz") ||
		    g_str_has_suffix (fn, ".xml.gz")) {
			g_autofree gchar *filename = g_build_filename (path, fn, NULL);
			g_autoptr(GError) error_local = NULL;
			if (!gs_plugin_appstream_load_appstream_fn (plugin,
								    builder,
								    filename,
								    cancellable,
								    &error_local)) {
				g_debug ("ignoring %s: %s", filename, error_local->message);
				continue;
			}
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
	const gchar *locale;
	const gchar *test_xml;
	g_autofree gchar *blobfn = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new ();
	g_autoptr(XbNode) n = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GRWLockReaderLocker) reader_locker = NULL;
	g_autoptr(GRWLockWriterLocker) writer_locker = NULL;
	g_autoptr(GPtrArray) parent_appdata = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GPtrArray) parent_appstream = g_ptr_array_new_with_free_func (g_free);


	reader_locker = g_rw_lock_reader_locker_new (&priv->silo_lock);
	/* everything is okay */
	if (priv->silo != NULL && xb_silo_is_valid (priv->silo))
		return TRUE;
	g_clear_pointer (&reader_locker, g_rw_lock_reader_locker_free);

	/* drat! silo needs regenerating */
	writer_locker = g_rw_lock_writer_locker_new (&priv->silo_lock);
	g_clear_object (&priv->silo);

	/* verbose profiling */
	if (g_getenv ("GS_XMLB_VERBOSE") != NULL) {
		xb_builder_set_profile_flags (builder,
					      XB_SILO_PROFILE_FLAG_XPATH |
					      XB_SILO_PROFILE_FLAG_DEBUG);
	}

	/* add current locales */
	locale = g_getenv ("GS_SELF_TEST_LOCALE");
	if (locale == NULL) {
		const gchar *const *locales = g_get_language_names ();
		for (guint i = 0; locales[i] != NULL; i++)
			xb_builder_add_locale (builder, locales[i]);
	} else {
		xb_builder_add_locale (builder, locale);
	}

	/* only when in self test */
	test_xml = g_getenv ("GS_SELF_TEST_APPSTREAM_XML");
	if (test_xml != NULL) {
		g_autoptr(XbBuilderFixup) fixup1 = NULL;
		g_autoptr(XbBuilderFixup) fixup2 = NULL;
		g_autoptr(XbBuilderSource) source = xb_builder_source_new ();
		if (!xb_builder_source_load_xml (source, test_xml,
						 XB_BUILDER_SOURCE_FLAG_NONE,
						 error))
			return FALSE;
		fixup1 = xb_builder_fixup_new ("AddOriginKeywords",
					       gs_plugin_appstream_add_origin_keyword_cb,
					       plugin, NULL);
		xb_builder_fixup_set_max_depth (fixup1, 1);
		xb_builder_source_add_fixup (source, fixup1);
		fixup2 = xb_builder_fixup_new ("AddIcons",
					       gs_plugin_appstream_add_icons_cb,
					       plugin, NULL);
		xb_builder_fixup_set_max_depth (fixup2, 2);
		xb_builder_source_add_fixup (source, fixup2);
		xb_builder_import_source (builder, source);
	} else {
		/* add search paths */
		g_ptr_array_add (parent_appstream,
				 g_build_filename ("/usr/share", "app-info", "xmls", NULL));
		g_ptr_array_add (parent_appstream,
				 g_build_filename ("/usr/share", "app-info", "yaml", NULL));
		g_ptr_array_add (parent_appdata,
				 g_build_filename ("/usr/share", "appdata", NULL));
		g_ptr_array_add (parent_appdata,
				 g_build_filename ("/usr/share", "metainfo", NULL));
		g_ptr_array_add (parent_appstream,
				 g_build_filename ("/var/cache", "app-info", "xmls", NULL));
		g_ptr_array_add (parent_appstream,
				 g_build_filename ("/var/cache", "app-info", "yaml", NULL));
		g_ptr_array_add (parent_appstream,
				 g_build_filename ("/var/lib", "app-info", "xmls", NULL));
		g_ptr_array_add (parent_appstream,
				 g_build_filename ("/var/lib", "app-info", "yaml", NULL));

		/* import all files */
		for (guint i = 0; i < parent_appstream->len; i++) {
			const gchar *fn = g_ptr_array_index (parent_appstream, i);
			if (!gs_plugin_appstream_load_appstream (plugin, builder, fn,
								 cancellable, error))
				return FALSE;
		}
		for (guint i = 0; i < parent_appdata->len; i++) {
			const gchar *fn = g_ptr_array_index (parent_appdata, i);
			if (!gs_plugin_appstream_load_appdata (plugin, builder, fn,
							       cancellable, error))
				return FALSE;
		}
		if (!gs_plugin_appstream_load_desktop (plugin, builder,
						       "/usr/share/applications",
						       cancellable, error)) {
			return FALSE;
		}
	}

	/* regenerate with each minor release */
	xb_builder_append_guid (builder, PACKAGE_VERSION);

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
	g_autofree gchar *path = NULL;
	g_autofree gchar *scheme = NULL;
	g_autofree gchar *xpath = NULL;
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(XbNode) component = NULL;

	/* check silo is valid */
	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;

	/* not us */
	scheme = gs_utils_get_url_scheme (url);
	if (g_strcmp0 (scheme, "appstream") != 0)
		return TRUE;

	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);

	/* create app */
	path = gs_utils_get_url_path (url);
	xpath = g_strdup_printf ("components/component/id[text()='%s']", path);
	component = xb_silo_query_first (priv->silo, xpath, NULL);
	if (component == NULL)
		return TRUE;
	app = gs_appstream_create_app (plugin, priv->silo, component, error);
	if (app == NULL)
		return FALSE;
	gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
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
					gs_app_add_quirk (app, GS_APP_QUIRK_COMPULSORY);
					break;
				}
			/* otherwise check if any element matches this one */
			} else if (g_strv_contains ((const gchar * const *) xdg_current_desktops, tmp)) {
				gs_app_add_quirk (app, GS_APP_QUIRK_COMPULSORY);
				break;
			}
		}
	}
}

static gboolean
gs_plugin_appstream_refine_state (GsPlugin *plugin, GsApp *app, GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autofree gchar *xpath = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	g_autoptr(XbNode) component = NULL;

	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);

	xpath = g_strdup_printf ("component/id[text()='%s']", gs_app_get_id (app));
	component = xb_silo_query_first (priv->silo, xpath, &error_local);
	if (component == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	gs_app_set_state (app, AS_APP_STATE_INSTALLED);
	return TRUE;
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
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	g_autoptr(GString) xpath = g_string_new (NULL);
	g_autoptr(GPtrArray) components = NULL;

	/* not enough info to find */
	id = gs_app_get_id (app);
	if (id == NULL)
		return TRUE;

	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);

	/* look in AppStream then fall back to AppData */
	xb_string_append_union (xpath, "components/component/id[text()='%s']/../pkgname/..", id);
	xb_string_append_union (xpath, "components/component[@type='webapp']/id[text()='%s']/..", id);
	xb_string_append_union (xpath, "component/id[text()='%s']/..", id);
	components = xb_silo_query (priv->silo, xpath->str, 0, &error_local);
	if (components == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
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

	/* if an installed desktop or appdata file exists set to installed */
	if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN) {
		if (!gs_plugin_appstream_refine_state (plugin, app, error))
			return FALSE;
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
		g_autoptr(GRWLockReaderLocker) locker = NULL;
		g_autoptr(GString) xpath = g_string_new (NULL);
		g_autoptr(XbNode) component = NULL;

		locker = g_rw_lock_reader_locker_new (&priv->silo_lock);

		/* prefer actual apps and then fallback to anything else */
		xb_string_append_union (xpath, "components/component[@type='desktop']/pkgname[text()='%s']/..", pkgname);
		xb_string_append_union (xpath, "components/component[@type='console']/pkgname[text()='%s']/..", pkgname);
		xb_string_append_union (xpath, "components/component[@type='webapp']/pkgname[text()='%s']/..", pkgname);
		xb_string_append_union (xpath, "components/component/pkgname[text()='%s']/..", pkgname);
		component = xb_silo_query_first (priv->silo, xpath->str, &error_local);
		if (component == NULL) {
			if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
				continue;
			if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
				continue;
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
		if (!gs_appstream_refine_app (plugin, app, priv->silo, component, flags, error))
			return FALSE;
		gs_plugin_appstream_set_compulsory_quirk (app, component);
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

	/* not us */
	if (gs_app_get_bundle_kind (app) != AS_BUNDLE_KIND_PACKAGE &&
	    gs_app_get_bundle_kind (app) != AS_BUNDLE_KIND_UNKNOWN)
		return TRUE;

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
			   GsPluginRefineFlags refine_flags,
			   GCancellable *cancellable,
			   GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *id;
	g_autofree gchar *xpath = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	g_autoptr(GPtrArray) components = NULL;

	/* check silo is valid */
	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;

	/* not enough info to find */
	id = gs_app_get_id (app);
	if (id == NULL)
		return TRUE;

	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);

	/* find all app with package names when matching any prefixes */
	xpath = g_strdup_printf ("components/component/id[text()='%s']/../pkgname/..", id);
	components = xb_silo_query (priv->silo, xpath, 0, &error_local);
	if (components == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);
		g_autoptr(GsApp) new = NULL;

		/* new app */
		new = gs_appstream_create_app (plugin, priv->silo, component, error);
		if (new == NULL)
			return FALSE;
		gs_app_set_scope (new, AS_APP_SCOPE_SYSTEM);
		gs_app_subsume_metadata (new, app);
		if (!gs_appstream_refine_app (plugin, new, priv->silo, component,
					      refine_flags, error))
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
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);
	return gs_appstream_add_category_apps (plugin,
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
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);
	return gs_appstream_search (plugin,
				    priv->silo,
				    (const gchar * const *) values,
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
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	g_autoptr(GPtrArray) components = NULL;

	/* check silo is valid */
	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);

	/* get all installed appdata files (notice no 'components/' prefix...) */
	components = xb_silo_query (priv->silo, "component/description/..", 0, NULL);
	if (components == NULL)
		return TRUE;
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);
		g_autoptr(GsApp) app = gs_appstream_create_app (plugin, priv->silo, component, error);
		if (app == NULL)
			return FALSE;
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		gs_app_set_scope (app, AS_APP_SCOPE_SYSTEM);
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
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);
	return gs_appstream_add_categories (plugin, priv->silo, list,
					    cancellable, error);
}

gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);
	return gs_appstream_add_popular (plugin, priv->silo, list, cancellable, error);
}

gboolean
gs_plugin_add_featured (GsPlugin *plugin,
			GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);
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
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);
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
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	if (!gs_plugin_appstream_check_silo (plugin, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&priv->silo_lock);
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
