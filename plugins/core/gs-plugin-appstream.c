/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
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
#include "gs-external-appstream-utils.h"
#include "gs-plugin-appstream.h"

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

struct _GsPluginAppstream
{
	GsPlugin		 parent;

	GsWorkerThread		*worker;  /* (owned) */

	XbSilo			*silo;
	GRWLock			 silo_lock;
	GSettings		*settings;
};

G_DEFINE_TYPE (GsPluginAppstream, gs_plugin_appstream, GS_TYPE_PLUGIN)

#define assert_in_worker(self) \
	g_assert (gs_worker_thread_is_in_worker_context (self->worker))

static void
gs_plugin_appstream_dispose (GObject *object)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (object);

	g_clear_object (&self->silo);
	g_clear_object (&self->settings);
	g_rw_lock_clear (&self->silo_lock);
	g_clear_object (&self->worker);

	G_OBJECT_CLASS (gs_plugin_appstream_parent_class)->dispose (object);
}

static void
gs_plugin_appstream_init (GsPluginAppstream *self)
{
	GApplication *application = g_application_get_default ();

	/* XbSilo needs external locking as we destroy the silo and build a new
	 * one when something changes */
	g_rw_lock_init (&self->silo_lock);

	/* need package name */
	gs_plugin_add_rule (GS_PLUGIN (self), GS_PLUGIN_RULE_RUN_AFTER, "dpkg");

	/* require settings */
	self->settings = g_settings_new ("org.gnome.software");

	/* Can be NULL when running the self tests */
	if (application) {
		g_signal_connect_object (application, "repository-changed",
			G_CALLBACK (gs_plugin_update_cache_state_for_repository), self, G_CONNECT_SWAPPED);
	}
}

static const gchar *
gs_plugin_appstream_convert_component_kind (const gchar *kind)
{
	if (g_strcmp0 (kind, "webapp") == 0)
		return "web-application";
	return kind;
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
	if (g_strcmp0 (xb_builder_node_get_element (bn), "component") != 0)
		return TRUE;
	gs_appstream_component_add_extra_info (bn);
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

static void
gs_plugin_appstream_media_baseurl_free (gpointer user_data)
{
	g_string_free ((GString *) user_data, TRUE);
}

static gboolean
gs_plugin_appstream_media_baseurl_cb (XbBuilderFixup *self,
				      XbBuilderNode *bn,
				      gpointer user_data,
				      GError **error)
{
	GString *baseurl = user_data;
	if (g_strcmp0 (xb_builder_node_get_element (bn), "components") == 0) {
		const gchar *url = xb_builder_node_get_attr (bn, "media_baseurl");
		if (url == NULL) {
			g_string_truncate (baseurl, 0);
			return TRUE;
		}
		g_string_assign (baseurl, url);
		return TRUE;
	}

	if (baseurl->len == 0)
		return TRUE;

	if (g_strcmp0 (xb_builder_node_get_element (bn), "icon") == 0) {
		const gchar *type = xb_builder_node_get_attr (bn, "type");
		if (g_strcmp0 (type, "remote") != 0)
			return TRUE;
		gs_appstream_component_fix_url (bn, baseurl->str);
	} else if (g_strcmp0 (xb_builder_node_get_element (bn), "screenshots") == 0) {
		GPtrArray *screenshots = xb_builder_node_get_children (bn);
		for (guint i = 0; i < screenshots->len; i++) {
			XbBuilderNode *screenshot = g_ptr_array_index (screenshots, i);
			GPtrArray *children = NULL;
			/* Type-check for security */
			if (g_strcmp0 (xb_builder_node_get_element (screenshot), "screenshot") != 0) {
				continue;
			}
			children = xb_builder_node_get_children (screenshot);
			for (guint j = 0; j < children->len; j++) {
				XbBuilderNode *child = g_ptr_array_index (children, j);
				const gchar *element = xb_builder_node_get_element (child);
				if (g_strcmp0 (element, "image") != 0 &&
				    g_strcmp0 (element, "video") != 0)
					continue;
				gs_appstream_component_fix_url (child, baseurl->str);
			}
		}
	}
	return TRUE;
}

static gboolean
gs_plugin_appstream_load_appdata_fn (GsPluginAppstream  *self,
                                     XbBuilder          *builder,
                                     const gchar        *filename,
                                     GCancellable       *cancellable,
                                     GError            **error)
{
	g_autoptr(GFile) file = g_file_new_for_path (filename);
	g_autoptr(XbBuilderFixup) fixup = NULL;
	g_autoptr(XbBuilderNode) info = NULL;
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();

	/* add source */
	if (!xb_builder_source_load_file (source, file,
#if LIBXMLB_CHECK_VERSION(0, 2, 0)
					  XB_BUILDER_SOURCE_FLAG_WATCH_DIRECTORY,
#else
					  XB_BUILDER_SOURCE_FLAG_WATCH_FILE,
#endif
					  cancellable,
					  error)) {
		return FALSE;
	}

	/* fix up any legacy installed files */
	fixup = xb_builder_fixup_new ("AppStreamUpgrade2",
				      gs_plugin_appstream_upgrade_cb,
				      self, NULL);
	xb_builder_fixup_set_max_depth (fixup, 3);
	xb_builder_source_add_fixup (source, fixup);

	/* add metadata */
	info = xb_builder_node_insert (NULL, "info", NULL);
	xb_builder_node_insert_text (info, "filename", filename, NULL);
	xb_builder_source_set_info (source, info);

	/* success */
	xb_builder_import_source (builder, source);
	return TRUE;
}

static gboolean
gs_plugin_appstream_load_appdata (GsPluginAppstream  *self,
                                  XbBuilder          *builder,
                                  const gchar        *path,
                                  GCancellable       *cancellable,
                                  GError            **error)
{
	const gchar *fn;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GFile) parent = g_file_new_for_path (path);
	if (!g_file_query_exists (parent, cancellable))
		return TRUE;

	dir = g_dir_open (path, 0, error);
	if (dir == NULL)
		return FALSE;

	while ((fn = g_dir_read_name (dir)) != NULL) {
		if (g_str_has_suffix (fn, ".appdata.xml") ||
		    g_str_has_suffix (fn, ".metainfo.xml")) {
			g_autofree gchar *filename = g_build_filename (path, fn, NULL);
			g_autoptr(GError) error_local = NULL;
			if (!gs_plugin_appstream_load_appdata_fn (self,
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
	g_autofree gchar *xml = NULL;
	g_autoptr(AsComponent) cpt = as_component_new ();
	g_autoptr(AsContext) actx = as_context_new ();
	g_autoptr(GBytes) bytes = NULL;
	gboolean ret;

	bytes = xb_builder_source_ctx_get_bytes (ctx, cancellable, error);
	if (bytes == NULL)
		return NULL;

	as_component_set_id (cpt, xb_builder_source_ctx_get_filename (ctx));
	ret = as_component_load_from_bytes (cpt,
					   actx,
					   AS_FORMAT_KIND_DESKTOP_ENTRY,
					   bytes,
					   error);
	if (!ret)
		return NULL;
	xml = as_component_to_xml_data (cpt, actx, error);
	if (xml == NULL)
		return NULL;
	return g_memory_input_stream_new_from_data (g_steal_pointer (&xml), (gssize) -1, g_free);
}

static gboolean
gs_plugin_appstream_load_desktop_fn (GsPluginAppstream  *self,
                                     XbBuilder          *builder,
                                     const gchar        *filename,
                                     GCancellable       *cancellable,
                                     GError            **error)
{
	g_autoptr(GFile) file = g_file_new_for_path (filename);
	g_autoptr(XbBuilderNode) info = NULL;
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();

	/* add support for desktop files */
	xb_builder_source_add_adapter (source, "application/x-desktop",
				       gs_plugin_appstream_load_desktop_cb, NULL, NULL);

	/* add source */
	if (!xb_builder_source_load_file (source, file,
#if LIBXMLB_CHECK_VERSION(0, 2, 0)
					  XB_BUILDER_SOURCE_FLAG_WATCH_DIRECTORY,
#else
					  XB_BUILDER_SOURCE_FLAG_WATCH_FILE,
#endif
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
gs_plugin_appstream_load_desktop (GsPluginAppstream  *self,
                                  XbBuilder          *builder,
                                  const gchar        *path,
                                  GCancellable       *cancellable,
                                  GError            **error)
{
	const gchar *fn;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GFile) parent = g_file_new_for_path (path);
	if (!g_file_query_exists (parent, cancellable))
		return TRUE;

	dir = g_dir_open (path, 0, error);
	if (dir == NULL)
		return FALSE;

	while ((fn = g_dir_read_name (dir)) != NULL) {
		if (g_str_has_suffix (fn, ".desktop")) {
			g_autofree gchar *filename = g_build_filename (path, fn, NULL);
			g_autoptr(GError) error_local = NULL;
			if (g_strcmp0 (fn, "mimeinfo.cache") == 0)
				continue;
			if (!gs_plugin_appstream_load_desktop_fn (self,
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
	g_autoptr(AsMetadata) mdata = as_metadata_new ();
	g_autoptr(GBytes) bytes = NULL;
	g_autoptr(GError) tmp_error = NULL;
	g_autofree gchar *xml = NULL;

	bytes = xb_builder_source_ctx_get_bytes (ctx, cancellable, error);
	if (bytes == NULL)
		return NULL;

	as_metadata_set_format_style (mdata, AS_FORMAT_STYLE_COLLECTION);
	as_metadata_parse_bytes (mdata,
				 bytes,
				 AS_FORMAT_KIND_YAML,
				 &tmp_error);
	if (tmp_error != NULL) {
		g_propagate_error (error, g_steal_pointer (&tmp_error));
		return NULL;
	}

	xml = as_metadata_components_to_collection (mdata, AS_FORMAT_KIND_XML, &tmp_error);
	if (xml == NULL) {
		// This API currently returns NULL if there is nothing to serialize, so we
		// have to test if this is an error or not.
		// See https://gitlab.gnome.org/GNOME/gnome-software/-/merge_requests/763
		// for discussion about changing this API.
		if (tmp_error != NULL) {
			g_propagate_error (error, g_steal_pointer (&tmp_error));
			return NULL;
		}

		xml = g_strdup("");
	}

	return g_memory_input_stream_new_from_data (g_steal_pointer (&xml), (gssize) -1, g_free);
}

#if LIBXMLB_CHECK_VERSION(0,3,1)
static gboolean
gs_plugin_appstream_tokenize_cb (XbBuilderFixup *self,
				 XbBuilderNode *bn,
				 gpointer user_data,
				 GError **error)
{
	const gchar * const elements_to_tokenize[] = {
		"id",
		"keyword",
		"launchable",
		"mimetype",
		"name",
		"pkgname",
		"summary",
		NULL };
	if (xb_builder_node_get_element (bn) != NULL &&
	    g_strv_contains (elements_to_tokenize, xb_builder_node_get_element (bn)))
		xb_builder_node_tokenize_text (bn);
	return TRUE;
}
#endif

static gboolean
gs_plugin_appstream_load_appstream_fn (GsPluginAppstream  *self,
                                       XbBuilder          *builder,
                                       const gchar        *filename,
                                       GCancellable       *cancellable,
                                       GError            **error)
{
	g_autoptr(GFile) file = g_file_new_for_path (filename);
	g_autoptr(XbBuilderNode) info = NULL;
	g_autoptr(XbBuilderFixup) fixup1 = NULL;
	g_autoptr(XbBuilderFixup) fixup2 = NULL;
	g_autoptr(XbBuilderFixup) fixup3 = NULL;
#if LIBXMLB_CHECK_VERSION(0,3,1)
	g_autoptr(XbBuilderFixup) fixup4 = NULL;
#endif
	g_autoptr(XbBuilderFixup) fixup5 = NULL;
	GString *media_baseurl = g_string_new (NULL);
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();

	/* add support for DEP-11 files */
	xb_builder_source_add_adapter (source,
				       "application/x-yaml",
				       gs_plugin_appstream_load_dep11_cb,
				       NULL, NULL);

	/* add source */
	if (!xb_builder_source_load_file (source, file,
#if LIBXMLB_CHECK_VERSION(0, 2, 0)
					  XB_BUILDER_SOURCE_FLAG_WATCH_DIRECTORY,
#else
					  XB_BUILDER_SOURCE_FLAG_WATCH_FILE,
#endif
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
				       self, NULL);
	xb_builder_fixup_set_max_depth (fixup1, 2);
	xb_builder_source_add_fixup (source, fixup1);

	/* fix up any legacy installed files */
	fixup2 = xb_builder_fixup_new ("AppStreamUpgrade2",
				       gs_plugin_appstream_upgrade_cb,
				       self, NULL);
	xb_builder_fixup_set_max_depth (fixup2, 3);
	xb_builder_source_add_fixup (source, fixup2);

	/* add the origin as a search keyword for small repos */
	fixup3 = xb_builder_fixup_new ("AddOriginKeyword",
				       gs_plugin_appstream_add_origin_keyword_cb,
				       self, NULL);
	xb_builder_fixup_set_max_depth (fixup3, 1);
	xb_builder_source_add_fixup (source, fixup3);

#if LIBXMLB_CHECK_VERSION(0,3,1)
	fixup4 = xb_builder_fixup_new ("TextTokenize",
				       gs_plugin_appstream_tokenize_cb,
				       NULL, NULL);
	xb_builder_fixup_set_max_depth (fixup4, 2);
	xb_builder_source_add_fixup (source, fixup4);
#endif

	/* prepend media_baseurl to remote relative URLs */
	fixup5 = xb_builder_fixup_new ("MediaBaseUrl",
				       gs_plugin_appstream_media_baseurl_cb,
				       media_baseurl,
				       gs_plugin_appstream_media_baseurl_free);
	xb_builder_fixup_set_max_depth (fixup5, 3);
	xb_builder_source_add_fixup (source, fixup5);

	/* success */
	xb_builder_import_source (builder, source);
	return TRUE;
}

static gboolean
gs_plugin_appstream_load_appstream (GsPluginAppstream  *self,
                                    XbBuilder          *builder,
                                    const gchar        *path,
                                    GCancellable       *cancellable,
                                    GError            **error)
{
	const gchar *fn;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GFile) parent = g_file_new_for_path (path);

	/* parent path does not exist */
	if (!g_file_query_exists (parent, cancellable))
		return TRUE;
	dir = g_dir_open (path, 0, error);
	if (dir == NULL)
		return FALSE;
	while ((fn = g_dir_read_name (dir)) != NULL) {
#ifdef ENABLE_EXTERNAL_APPSTREAM
		/* Ignore our own system-installed files when
		   external-appstream-system-wide is FALSE */
		if (!g_settings_get_boolean (self->settings, "external-appstream-system-wide") &&
		    g_strcmp0 (path, gs_external_appstream_utils_get_system_dir ()) == 0 &&
		    g_str_has_prefix (fn, EXTERNAL_APPSTREAM_PREFIX))
			continue;
#endif
		if (g_str_has_suffix (fn, ".xml") ||
		    g_str_has_suffix (fn, ".yml") ||
		    g_str_has_suffix (fn, ".yml.gz") ||
		    g_str_has_suffix (fn, ".xml.gz")) {
			g_autofree gchar *filename = g_build_filename (path, fn, NULL);
			g_autoptr(GError) error_local = NULL;
			if (!gs_plugin_appstream_load_appstream_fn (self,
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
gs_plugin_appstream_check_silo (GsPluginAppstream  *self,
                                GCancellable       *cancellable,
                                GError            **error)
{
	const gchar *test_xml;
	g_autofree gchar *blobfn = NULL;
	g_autoptr(XbBuilder) builder = NULL;
	g_autoptr(XbNode) n = NULL;
	g_autoptr(GFile) file = NULL;
	g_autoptr(GRWLockReaderLocker) reader_locker = NULL;
	g_autoptr(GRWLockWriterLocker) writer_locker = NULL;
	g_autoptr(GPtrArray) parent_appdata = g_ptr_array_new_with_free_func (g_free);
	g_autoptr(GPtrArray) parent_appstream = g_ptr_array_new_with_free_func (g_free);
	const gchar *const *locales = g_get_language_names ();
	g_autoptr(GMainContext) old_thread_default = NULL;

	reader_locker = g_rw_lock_reader_locker_new (&self->silo_lock);
	/* everything is okay */
	if (self->silo != NULL && xb_silo_is_valid (self->silo))
		return TRUE;
	g_clear_pointer (&reader_locker, g_rw_lock_reader_locker_free);

	/* drat! silo needs regenerating */
	writer_locker = g_rw_lock_writer_locker_new (&self->silo_lock);
	g_clear_object (&self->silo);

	/* FIXME: https://gitlab.gnome.org/GNOME/gnome-software/-/issues/1422 */
	old_thread_default = g_main_context_ref_thread_default ();
	if (old_thread_default == g_main_context_default ())
		g_clear_pointer (&old_thread_default, g_main_context_unref);
	if (old_thread_default != NULL)
		g_main_context_pop_thread_default (old_thread_default);
	builder = xb_builder_new ();
	if (old_thread_default != NULL)
		g_main_context_push_thread_default (old_thread_default);
	g_clear_pointer (&old_thread_default, g_main_context_unref);

	/* verbose profiling */
	if (g_getenv ("GS_XMLB_VERBOSE") != NULL) {
		xb_builder_set_profile_flags (builder,
					      XB_SILO_PROFILE_FLAG_XPATH |
					      XB_SILO_PROFILE_FLAG_DEBUG);
	}

	/* add current locales */
	for (guint i = 0; locales[i] != NULL; i++)
		xb_builder_add_locale (builder, locales[i]);

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
					       self, NULL);
		xb_builder_fixup_set_max_depth (fixup1, 1);
		xb_builder_source_add_fixup (source, fixup1);
		fixup2 = xb_builder_fixup_new ("AddIcons",
					       gs_plugin_appstream_add_icons_cb,
					       self, NULL);
		xb_builder_fixup_set_max_depth (fixup2, 2);
		xb_builder_source_add_fixup (source, fixup2);
		xb_builder_import_source (builder, source);
	} else {
		/* add search paths */
		g_ptr_array_add (parent_appstream,
				 g_build_filename (DATADIR, "app-info", "xmls", NULL));
		g_ptr_array_add (parent_appstream,
				 g_build_filename (DATADIR, "app-info", "yaml", NULL));
		g_ptr_array_add (parent_appdata,
				 g_build_filename (DATADIR, "appdata", NULL));
		g_ptr_array_add (parent_appdata,
				 g_build_filename (DATADIR, "metainfo", NULL));
		g_ptr_array_add (parent_appstream,
				 g_build_filename (LOCALSTATEDIR, "cache", "app-info", "xmls", NULL));
		g_ptr_array_add (parent_appstream,
				 g_build_filename (LOCALSTATEDIR, "cache", "app-info", "yaml", NULL));
		g_ptr_array_add (parent_appstream,
				 g_build_filename (LOCALSTATEDIR, "lib", "app-info", "xmls", NULL));
		g_ptr_array_add (parent_appstream,
				 g_build_filename (LOCALSTATEDIR, "lib", "app-info", "yaml", NULL));
#ifdef ENABLE_EXTERNAL_APPSTREAM
		/* check for the corresponding setting */
		if (!g_settings_get_boolean (self->settings, "external-appstream-system-wide")) {
			g_ptr_array_add (parent_appstream,
					 g_build_filename (g_get_user_data_dir (), "app-info", "xmls", NULL));
			g_ptr_array_add (parent_appstream,
					 g_build_filename (g_get_user_data_dir (), "app-info", "yaml", NULL));
		}
#endif

		/* Add the normal system directories if the installation prefix
		 * is different from normal — typically this happens when doing
		 * development builds. It’s useful to still list the system apps
		 * during development. */
		if (g_strcmp0 (DATADIR, "/usr/share") != 0) {
			g_ptr_array_add (parent_appstream,
					 g_build_filename ("/usr/share", "app-info", "xmls", NULL));
			g_ptr_array_add (parent_appstream,
					 g_build_filename ("/usr/share", "app-info", "yaml", NULL));
			g_ptr_array_add (parent_appdata,
					 g_build_filename ("/usr/share", "appdata", NULL));
			g_ptr_array_add (parent_appdata,
					 g_build_filename ("/usr/share", "metainfo", NULL));
		}
		if (g_strcmp0 (LOCALSTATEDIR, "/var") != 0) {
			g_ptr_array_add (parent_appstream,
					 g_build_filename ("/var", "cache", "app-info", "xmls", NULL));
			g_ptr_array_add (parent_appstream,
					 g_build_filename ("/var", "cache", "app-info", "yaml", NULL));
			g_ptr_array_add (parent_appstream,
					 g_build_filename ("/var", "lib", "app-info", "xmls", NULL));
			g_ptr_array_add (parent_appstream,
					 g_build_filename ("/var", "lib", "app-info", "yaml", NULL));
		}

		/* import all files */
		for (guint i = 0; i < parent_appstream->len; i++) {
			const gchar *fn = g_ptr_array_index (parent_appstream, i);
			if (!gs_plugin_appstream_load_appstream (self, builder, fn,
								 cancellable, error))
				return FALSE;
		}
		for (guint i = 0; i < parent_appdata->len; i++) {
			const gchar *fn = g_ptr_array_index (parent_appdata, i);
			if (!gs_plugin_appstream_load_appdata (self, builder, fn,
							       cancellable, error))
				return FALSE;
		}
		if (!gs_plugin_appstream_load_desktop (self, builder,
						       DATADIR "/applications",
						       cancellable, error)) {
			return FALSE;
		}
		if (g_strcmp0 (DATADIR, "/usr/share") != 0 &&
		    !gs_plugin_appstream_load_desktop (self, builder,
						       "/usr/share/applications",
						       cancellable, error)) {
			return FALSE;
		}
	}

	/* regenerate with each minor release */
	xb_builder_append_guid (builder, PACKAGE_VERSION);

	/* create per-user cache */
	blobfn = gs_utils_get_cache_filename ("appstream", "components.xmlb",
					      GS_UTILS_CACHE_FLAG_WRITEABLE |
					      GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
					      error);
	if (blobfn == NULL)
		return FALSE;
	file = g_file_new_for_path (blobfn);
	g_debug ("ensuring %s", blobfn);

	/* FIXME: https://gitlab.gnome.org/GNOME/gnome-software/-/issues/1422 */
	old_thread_default = g_main_context_ref_thread_default ();
	if (old_thread_default == g_main_context_default ())
		g_clear_pointer (&old_thread_default, g_main_context_unref);
	if (old_thread_default != NULL)
		g_main_context_pop_thread_default (old_thread_default);

	self->silo = xb_builder_ensure (builder, file,
					XB_BUILDER_COMPILE_FLAG_IGNORE_INVALID |
					XB_BUILDER_COMPILE_FLAG_SINGLE_LANG,
					NULL, error);
	if (self->silo == NULL) {
		if (old_thread_default != NULL)
			g_main_context_push_thread_default (old_thread_default);
		return FALSE;
	}

	/* watch all directories too */
	for (guint i = 0; i < parent_appstream->len; i++) {
		const gchar *fn = g_ptr_array_index (parent_appstream, i);
		g_autoptr(GFile) file_tmp = g_file_new_for_path (fn);
		if (!xb_silo_watch_file (self->silo, file_tmp, cancellable, error)) {
			if (old_thread_default != NULL)
				g_main_context_push_thread_default (old_thread_default);
			return FALSE;
		}
	}
	for (guint i = 0; i < parent_appdata->len; i++) {
		const gchar *fn = g_ptr_array_index (parent_appdata, i);
		g_autoptr(GFile) file_tmp = g_file_new_for_path (fn);
		if (!xb_silo_watch_file (self->silo, file_tmp, cancellable, error)) {
			if (old_thread_default != NULL)
				g_main_context_push_thread_default (old_thread_default);
			return FALSE;
		}
	}

	if (old_thread_default != NULL)
		g_main_context_push_thread_default (old_thread_default);

	/* test we found something */
	n = xb_silo_query_first (self->silo, "components/component", NULL);
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

static void setup_thread_cb (GTask        *task,
                             gpointer      source_object,
                             gpointer      task_data,
                             GCancellable *cancellable);

static void
gs_plugin_appstream_setup_async (GsPlugin            *plugin,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_appstream_setup_async);

	/* Start up a worker thread to process all the plugin’s function calls. */
	self->worker = gs_worker_thread_new ("gs-plugin-appstream");

	/* Queue a job to check the silo, which will cause it to be loaded. */
	gs_worker_thread_queue (self->worker, G_PRIORITY_DEFAULT,
				setup_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
setup_thread_cb (GTask        *task,
                 gpointer      source_object,
                 gpointer      task_data,
                 GCancellable *cancellable)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (source_object);
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	if (!gs_plugin_appstream_check_silo (self, cancellable, &local_error))
		g_task_return_error (task, g_steal_pointer (&local_error));
	else
		g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_appstream_setup_finish (GsPlugin      *plugin,
                                  GAsyncResult  *result,
                                  GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void shutdown_cb (GObject      *source_object,
                         GAsyncResult *result,
                         gpointer      user_data);

static void
gs_plugin_appstream_shutdown_async (GsPlugin            *plugin,
                                    GCancellable        *cancellable,
                                    GAsyncReadyCallback  callback,
                                    gpointer             user_data)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_appstream_shutdown_async);

	/* Stop the worker thread. */
	gs_worker_thread_shutdown_async (self->worker, cancellable, shutdown_cb, g_steal_pointer (&task));
}

static void
shutdown_cb (GObject      *source_object,
             GAsyncResult *result,
             gpointer      user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	GsPluginAppstream *self = g_task_get_source_object (task);
	g_autoptr(GsWorkerThread) worker = NULL;
	g_autoptr(GError) local_error = NULL;

	worker = g_steal_pointer (&self->worker);

	if (!gs_worker_thread_shutdown_finish (worker, result, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_appstream_shutdown_finish (GsPlugin      *plugin,
                                     GAsyncResult  *result,
                                     GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

gboolean
gs_plugin_url_to_app (GsPlugin *plugin,
		      GsAppList *list,
		      const gchar *url,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (plugin);
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	/* check silo is valid */
	if (!gs_plugin_appstream_check_silo (self, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&self->silo_lock);

	return gs_appstream_url_to_app (plugin, self->silo, list, url, cancellable, error);
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
gs_plugin_appstream_refine_state (GsPluginAppstream  *self,
                                  GsApp              *app,
                                  GError            **error)
{
	g_autofree gchar *xpath = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	g_autoptr(XbNode) component = NULL;

	locker = g_rw_lock_reader_locker_new (&self->silo_lock);

	xpath = g_strdup_printf ("component/id[text()='%s']", gs_app_get_id (app));
	component = xb_silo_query_first (self->silo, xpath, &error_local);
	if (component == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	gs_app_set_state (app, GS_APP_STATE_INSTALLED);
	return TRUE;
}

static gboolean
gs_plugin_refine_from_id (GsPluginAppstream    *self,
                          GsApp                *app,
                          GsPluginRefineFlags   flags,
                          gboolean             *found,
                          GError              **error)
{
	const gchar *id, *origin;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	g_autoptr(GString) xpath = g_string_new (NULL);
	g_autoptr(GPtrArray) components = NULL;

	/* not enough info to find */
	id = gs_app_get_id (app);
	if (id == NULL)
		return TRUE;

	locker = g_rw_lock_reader_locker_new (&self->silo_lock);

	origin = gs_app_get_origin_appstream (app);

	/* look in AppStream then fall back to AppData */
	if (origin && *origin) {
		xb_string_append_union (xpath, "components[@origin='%s']/component/id[text()='%s']/../pkgname/..", origin, id);
		xb_string_append_union (xpath, "components[@origin='%s']/component[@type='webapp']/id[text()='%s']/..", origin, id);
	} else {
		xb_string_append_union (xpath, "components/component/id[text()='%s']/../pkgname/..", id);
		xb_string_append_union (xpath, "components/component[@type='webapp']/id[text()='%s']/..", id);
	}
	xb_string_append_union (xpath, "component/id[text()='%s']/..", id);
	components = xb_silo_query (self->silo, xpath->str, 0, &error_local);
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
		if (!gs_appstream_refine_app (GS_PLUGIN (self), app, self->silo,
					      component, flags, error))
			return FALSE;
		gs_plugin_appstream_set_compulsory_quirk (app, component);
	}

	/* if an installed desktop or appdata file exists set to installed */
	if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN) {
		if (!gs_plugin_appstream_refine_state (self, app, error))
			return FALSE;
	}

	/* success */
	*found = TRUE;
	return TRUE;
}

static gboolean
gs_plugin_refine_from_pkgname (GsPluginAppstream    *self,
                               GsApp                *app,
                               GsPluginRefineFlags   flags,
                               GError              **error)
{
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

		locker = g_rw_lock_reader_locker_new (&self->silo_lock);

		/* prefer actual apps and then fallback to anything else */
		xb_string_append_union (xpath, "components/component[@type='desktop']/pkgname[text()='%s']/..", pkgname);
		xb_string_append_union (xpath, "components/component[@type='console']/pkgname[text()='%s']/..", pkgname);
		xb_string_append_union (xpath, "components/component[@type='webapp']/pkgname[text()='%s']/..", pkgname);
		xb_string_append_union (xpath, "components/component/pkgname[text()='%s']/..", pkgname);
		component = xb_silo_query_first (self->silo, xpath->str, &error_local);
		if (component == NULL) {
			if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
				continue;
			if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
				continue;
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
		if (!gs_appstream_refine_app (GS_PLUGIN (self), app, self->silo, component, flags, error))
			return FALSE;
		gs_plugin_appstream_set_compulsory_quirk (app, component);
	}

	/* if an installed desktop or appdata file exists set to installed */
	if (gs_app_get_state (app) == GS_APP_STATE_UNKNOWN) {
		if (!gs_plugin_appstream_refine_state (self, app, error))
			return FALSE;
	}

	/* success */
	return TRUE;
}

static void refine_thread_cb (GTask        *task,
                              gpointer      source_object,
                              gpointer      task_data,
                              GCancellable *cancellable);

static void
gs_plugin_appstream_refine_async (GsPlugin            *plugin,
                                  GsAppList           *list,
                                  GsPluginRefineFlags  flags,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (plugin);
	g_autoptr(GTask) task = NULL;

	task = gs_plugin_refine_data_new_task (plugin, list, flags, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_appstream_refine_async);

	/* Queue a job for the refine. */
	gs_worker_thread_queue (self->worker, G_PRIORITY_DEFAULT,
				refine_thread_cb, g_steal_pointer (&task));
}

static gboolean refine_wildcard (GsPluginAppstream    *self,
                                 GsApp                *app,
                                 GsAppList            *list,
                                 GsPluginRefineFlags   refine_flags,
                                 GCancellable         *cancellable,
                                 GError              **error);

/* Run in @worker. */
static void
refine_thread_cb (GTask        *task,
                  gpointer      source_object,
                  gpointer      task_data,
                  GCancellable *cancellable)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (source_object);
	GsPluginRefineData *data = task_data;
	GsAppList *list = data->list;
	GsPluginRefineFlags flags = data->flags;
	gboolean found = FALSE;
	g_autoptr(GsAppList) app_list = NULL;
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	/* check silo is valid */
	if (!gs_plugin_appstream_check_silo (self, cancellable, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);

		/* not us */
		if (gs_app_get_bundle_kind (app) != AS_BUNDLE_KIND_PACKAGE &&
		    gs_app_get_bundle_kind (app) != AS_BUNDLE_KIND_UNKNOWN)
			continue;

		/* find by ID then fall back to package name */
		if (!gs_plugin_refine_from_id (self, app, flags, &found, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}
		if (!found) {
			if (!gs_plugin_refine_from_pkgname (self, app, flags, &local_error)) {
				g_task_return_error (task, g_steal_pointer (&local_error));
				return;
			}
		}
	}

	/* Refine wildcards.
	 *
	 * Use a copy of the list for the loop because a function called
	 * on the plugin may affect the list which can lead to problems
	 * (e.g. inserting an app in the list on every call results in
	 * an infinite loop) */
	app_list = gs_app_list_copy (list);

	for (guint j = 0; j < gs_app_list_length (app_list); j++) {
		GsApp *app = gs_app_list_index (app_list, j);

		if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD) &&
		    !refine_wildcard (self, app, list, flags, cancellable, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}
	}

	/* success */
	g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_appstream_refine_finish (GsPlugin      *plugin,
                                   GAsyncResult  *result,
                                   GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

/* Run in @worker. Silo must be valid */
static gboolean
refine_wildcard (GsPluginAppstream    *self,
                 GsApp                *app,
                 GsAppList            *list,
                 GsPluginRefineFlags   refine_flags,
                 GCancellable         *cancellable,
                 GError              **error)
{
	const gchar *id;
	g_autofree gchar *xpath = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	g_autoptr(GPtrArray) components = NULL;

	/* not enough info to find */
	id = gs_app_get_id (app);
	if (id == NULL)
		return TRUE;

	locker = g_rw_lock_reader_locker_new (&self->silo_lock);

	/* find all app with package names when matching any prefixes */
	xpath = g_strdup_printf ("components/component/id[text()='%s']/../pkgname/..", id);
	components = xb_silo_query (self->silo, xpath, 0, &error_local);
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
		new = gs_appstream_create_app (GS_PLUGIN (self), self->silo, component, error);
		if (new == NULL)
			return FALSE;
		gs_app_set_scope (new, AS_COMPONENT_SCOPE_SYSTEM);
		gs_app_subsume_metadata (new, app);
		if (!gs_appstream_refine_app (GS_PLUGIN (self), new, self->silo, component,
					      refine_flags, error))
			return FALSE;
		gs_plugin_appstream_set_compulsory_quirk (new, component);

		/* if an installed desktop or appdata file exists set to installed */
		if (gs_app_get_state (new) == GS_APP_STATE_UNKNOWN) {
			if (!gs_plugin_appstream_refine_state (self, new, error))
				return FALSE;
		}

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
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (plugin);
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	if (!gs_plugin_appstream_check_silo (self, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&self->silo_lock);
	return gs_appstream_add_category_apps (self->silo,
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
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (plugin);
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	if (!gs_plugin_appstream_check_silo (self, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&self->silo_lock);
	return gs_appstream_search (plugin,
				    self->silo,
				    (const gchar * const *) values,
				    list,
				    cancellable,
				    error);
}

static void list_installed_apps_thread_cb (GTask        *task,
                                           gpointer      source_object,
                                           gpointer      task_data,
                                           GCancellable *cancellable);

static void
gs_plugin_appstream_list_installed_apps_async (GsPlugin            *plugin,
                                               GCancellable        *cancellable,
                                               GAsyncReadyCallback  callback,
                                               gpointer             user_data)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_appstream_list_installed_apps_async);

	/* Queue a job to check the silo, which will cause it to be loaded. */
	gs_worker_thread_queue (self->worker, G_PRIORITY_DEFAULT,
				list_installed_apps_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
list_installed_apps_thread_cb (GTask        *task,
                               gpointer      source_object,
                               gpointer      task_data,
                               GCancellable *cancellable)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (source_object);
	g_autoptr(GRWLockReaderLocker) locker = NULL;
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GsAppList) list = gs_app_list_new ();
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	/* check silo is valid */
	if (!gs_plugin_appstream_check_silo (self, cancellable, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	locker = g_rw_lock_reader_locker_new (&self->silo_lock);

	/* get all installed appdata files (notice no 'components/' prefix...) */
	components = xb_silo_query (self->silo, "component/description/..", 0, NULL);
	if (components == NULL) {
		g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
		return;
	}

	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);
		g_autoptr(GsApp) app = gs_appstream_create_app (GS_PLUGIN (self), self->silo, component, &local_error);
		if (app == NULL) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}

		/* Can get cached gsApp, which has the state already updated */
		if (gs_app_get_state (app) != GS_APP_STATE_UPDATABLE &&
		    gs_app_get_state (app) != GS_APP_STATE_UPDATABLE_LIVE)
			gs_app_set_state (app, GS_APP_STATE_INSTALLED);
		gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
		gs_app_list_add (list, app);
	}

	g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
}

static GsAppList *
gs_plugin_appstream_list_installed_apps_finish (GsPlugin      *plugin,
                                                GAsyncResult  *result,
                                                GError       **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

gboolean
gs_plugin_add_categories (GsPlugin *plugin,
			  GPtrArray *list,
			  GCancellable *cancellable,
			  GError **error)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (plugin);
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	if (!gs_plugin_appstream_check_silo (self, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&self->silo_lock);
	return gs_appstream_add_categories (self->silo, list,
					    cancellable, error);
}

gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GsAppList *list,
		       GCancellable *cancellable,
		       GError **error)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (plugin);
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	if (!gs_plugin_appstream_check_silo (self, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&self->silo_lock);
	return gs_appstream_add_popular (self->silo, list, cancellable, error);
}

gboolean
gs_plugin_add_featured (GsPlugin *plugin,
			GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (plugin);
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	if (!gs_plugin_appstream_check_silo (self, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&self->silo_lock);
	return gs_appstream_add_featured (self->silo, list, cancellable, error);
}

gboolean
gs_plugin_add_recent (GsPlugin *plugin,
		      GsAppList *list,
		      guint64 age,
		      GCancellable *cancellable,
		      GError **error)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (plugin);
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	if (!gs_plugin_appstream_check_silo (self, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&self->silo_lock);
	return gs_appstream_add_recent (GS_PLUGIN (self), self->silo, list, age,
					cancellable, error);
}

gboolean
gs_plugin_add_alternates (GsPlugin *plugin,
			  GsApp *app,
			  GsAppList *list,
			  GCancellable *cancellable,
			  GError **error)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (plugin);
	g_autoptr(GRWLockReaderLocker) locker = NULL;

	if (!gs_plugin_appstream_check_silo (self, cancellable, error))
		return FALSE;

	locker = g_rw_lock_reader_locker_new (&self->silo_lock);
	return gs_appstream_add_alternates (self->silo, app, list,
					    cancellable, error);
}

static void refresh_metadata_thread_cb (GTask        *task,
                                        gpointer      source_object,
                                        gpointer      task_data,
                                        GCancellable *cancellable);

static void
gs_plugin_appstream_refresh_metadata_async (GsPlugin                     *plugin,
                                            guint64                       cache_age_secs,
                                            GsPluginRefreshMetadataFlags  flags,
                                            GCancellable                 *cancellable,
                                            GAsyncReadyCallback           callback,
                                            gpointer                      user_data)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_appstream_refresh_metadata_async);

	/* Queue a job to check the silo, which will cause it to be refreshed if needed. */
	gs_worker_thread_queue (self->worker, G_PRIORITY_DEFAULT,
				refresh_metadata_thread_cb, g_steal_pointer (&task));
}

/* Run in @worker. */
static void
refresh_metadata_thread_cb (GTask        *task,
                            gpointer      source_object,
                            gpointer      task_data,
                            GCancellable *cancellable)
{
	GsPluginAppstream *self = GS_PLUGIN_APPSTREAM (source_object);
	g_autoptr(GError) local_error = NULL;

	assert_in_worker (self);

	/* Checking the silo will refresh it if needed. */
	if (!gs_plugin_appstream_check_silo (self, cancellable, &local_error))
		g_task_return_error (task, g_steal_pointer (&local_error));
	else
		g_task_return_boolean (task, TRUE);
}

static gboolean
gs_plugin_appstream_refresh_metadata_finish (GsPlugin      *plugin,
                                             GAsyncResult  *result,
                                             GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_appstream_class_init (GsPluginAppstreamClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_appstream_dispose;

	plugin_class->setup_async = gs_plugin_appstream_setup_async;
	plugin_class->setup_finish = gs_plugin_appstream_setup_finish;
	plugin_class->shutdown_async = gs_plugin_appstream_shutdown_async;
	plugin_class->shutdown_finish = gs_plugin_appstream_shutdown_finish;
	plugin_class->refine_async = gs_plugin_appstream_refine_async;
	plugin_class->refine_finish = gs_plugin_appstream_refine_finish;
	plugin_class->list_installed_apps_async = gs_plugin_appstream_list_installed_apps_async;
	plugin_class->list_installed_apps_finish = gs_plugin_appstream_list_installed_apps_finish;
	plugin_class->refresh_metadata_async = gs_plugin_appstream_refresh_metadata_async;
	plugin_class->refresh_metadata_finish = gs_plugin_appstream_refresh_metadata_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_APPSTREAM;
}
