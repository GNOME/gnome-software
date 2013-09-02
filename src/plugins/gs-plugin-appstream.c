/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#define GS_PLUGIN_APPSTREAM_ICONS_HASH	"e6c366af886562819019fd25b408b7b94a6f9a16"

typedef enum {
	GS_APPSTREAM_XML_SECTION_UNKNOWN,
	GS_APPSTREAM_XML_SECTION_APPLICATIONS,
	GS_APPSTREAM_XML_SECTION_APPLICATION,
	GS_APPSTREAM_XML_SECTION_ID,
	GS_APPSTREAM_XML_SECTION_PKGNAME,
	GS_APPSTREAM_XML_SECTION_NAME,
	GS_APPSTREAM_XML_SECTION_SUMMARY,
	GS_APPSTREAM_XML_SECTION_ICON,
	GS_APPSTREAM_XML_SECTION_APPCATEGORIES,
	GS_APPSTREAM_XML_SECTION_APPCATEGORY,
	GS_APPSTREAM_XML_SECTION_LAST
} GsAppstreamXmlSection;

typedef struct {
	gchar		*id;
	gchar		*pkgname;
	gchar		*name;
	gchar		*summary;
	gchar		*icon;
	GPtrArray	*appcategories;
} GsAppstreamItem;

struct GsPluginPrivate {
	gchar			*cachedir;
	GsAppstreamXmlSection	 section;
	GsAppstreamItem		*item_temp;
	GPtrArray		*array;		/* of GsAppstreamItem */
	GHashTable		*hash_id;	/* of GsAppstreamItem{id} */
	GHashTable		*hash_pkgname;	/* of GsAppstreamItem{pkgname} */
	gsize    		 done_init;
};

/**
 * gs_appstream_item_free:
 */
static void
gs_appstream_item_free (gpointer data)
{
	GsAppstreamItem *item = (GsAppstreamItem *) data;
	g_free (item->id);
	g_free (item->pkgname);
	g_free (item->name);
	g_free (item->summary);
	g_free (item->icon);
	g_ptr_array_unref (item->appcategories);
	g_free (item);
}

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "appstream";
}

/**
 * gs_plugin_decompress_icons:
 */
static gboolean
gs_plugin_decompress_icons (GsPlugin *plugin, GError **error)
{
	const gchar *argv[6];
	gboolean ret = TRUE;
	gchar *hash_check_file = NULL;
	gint exit_status = 0;

	/* create directory */
	if (!g_file_test (plugin->priv->cachedir, G_FILE_TEST_EXISTS)) {
		exit_status = g_mkdir_with_parents (plugin->priv->cachedir, 0700);
		if (exit_status != 0) {
			ret = FALSE;
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "Failed to create %s",
				     plugin->priv->cachedir);
			goto out;
		}
	}

	/* is cache new enough? */
	hash_check_file = g_build_filename (plugin->priv->cachedir,
					    GS_PLUGIN_APPSTREAM_ICONS_HASH,
					    NULL);
	if (g_file_test (hash_check_file, G_FILE_TEST_EXISTS)) {
		g_debug ("Icon cache new enough, skipping decompression");
		goto out;
	}

	/* decompress */
	argv[0] = "tar";
	argv[1] = "-zxvf";
	argv[2] = DATADIR "/gnome-software/appstream-icons.tar.gz";
	argv[3] = "-C";
	argv[4] = plugin->priv->cachedir;
	argv[5] = NULL;
	g_debug ("Decompressing %s to %s", argv[2], plugin->priv->cachedir);
	ret = g_spawn_sync (NULL,
			    (gchar **) argv,
			    NULL,
			    G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
			    NULL, NULL,
			    NULL,
			    NULL,
			    &exit_status,
			    error);
	if (!ret)
		goto out;
	if (exit_status != 0) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to extract icon data to %s [%i]",
			     plugin->priv->cachedir, exit_status);
		goto out;
	}

	/* mark as done */
	ret = g_file_set_contents (hash_check_file, "", -1, error);
	if (!ret)
		goto out;
out:
	g_free (hash_check_file);
	return ret;
}

/**
 * gs_appstream_selection_from_text:
 */
static GsAppstreamXmlSection
gs_appstream_selection_from_text (const gchar *element_name)
{
	if (g_strcmp0 (element_name, "applications") == 0)
		return GS_APPSTREAM_XML_SECTION_APPLICATIONS;
	if (g_strcmp0 (element_name, "application") == 0)
		return GS_APPSTREAM_XML_SECTION_APPLICATION;
	if (g_strcmp0 (element_name, "id") == 0)
		return GS_APPSTREAM_XML_SECTION_ID;
	if (g_strcmp0 (element_name, "pkgname") == 0)
		return GS_APPSTREAM_XML_SECTION_PKGNAME;
	if (g_strcmp0 (element_name, "name") == 0)
		return GS_APPSTREAM_XML_SECTION_NAME;
	if (g_strcmp0 (element_name, "summary") == 0)
		return GS_APPSTREAM_XML_SECTION_SUMMARY;
	if (g_strcmp0 (element_name, "icon") == 0)
		return GS_APPSTREAM_XML_SECTION_ICON;
	if (g_strcmp0 (element_name, "appcategories") == 0)
		return 	GS_APPSTREAM_XML_SECTION_APPCATEGORIES;
	if (g_strcmp0 (element_name, "appcategory") == 0)
		return GS_APPSTREAM_XML_SECTION_APPCATEGORY;
	return GS_APPSTREAM_XML_SECTION_UNKNOWN;
}

/**
 * gs_appstream_selection_to_text:
 */
static const gchar *
gs_appstream_selection_to_text (GsAppstreamXmlSection section)
{
	if (section == GS_APPSTREAM_XML_SECTION_APPLICATIONS)
		return "applications";
	if (section == GS_APPSTREAM_XML_SECTION_APPLICATION)
		return "application";
	if (section == GS_APPSTREAM_XML_SECTION_ID)
		return "id";
	if (section == GS_APPSTREAM_XML_SECTION_PKGNAME)
		return "pkgname";
	if (section == GS_APPSTREAM_XML_SECTION_NAME)
		return "name";
	if (section == GS_APPSTREAM_XML_SECTION_SUMMARY)
		return "summary";
	if (section == GS_APPSTREAM_XML_SECTION_ICON)
		return "icon";
	if (section == GS_APPSTREAM_XML_SECTION_APPCATEGORIES)
		return "appcategories";
	if (section == GS_APPSTREAM_XML_SECTION_APPCATEGORY)
		return "appcategory";
	return NULL;
}

/**
 * gs_appstream_start_element_cb:
 */
static void
gs_appstream_start_element_cb (GMarkupParseContext *context,
			       const gchar *element_name,
			       const gchar **attribute_names,
			       const gchar **attribute_values,
			       gpointer user_data,
			       GError **error)
{
	GsPlugin *plugin = (GsPlugin *) user_data;
	GsAppstreamXmlSection section_new;

	/* process tag start */
	section_new = gs_appstream_selection_from_text (element_name);
	switch (section_new) {
	case GS_APPSTREAM_XML_SECTION_APPLICATIONS:
	case GS_APPSTREAM_XML_SECTION_APPCATEGORIES:
	case GS_APPSTREAM_XML_SECTION_APPCATEGORY:
		/* ignore */
		break;
	case GS_APPSTREAM_XML_SECTION_APPLICATION:
		if (plugin->priv->item_temp != NULL ||
		    plugin->priv->section != GS_APPSTREAM_XML_SECTION_APPLICATIONS) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "XML start %s invalid, section %s",
				     element_name,
				     gs_appstream_selection_to_text (plugin->priv->section));
			return;
		}
		plugin->priv->item_temp = g_new0 (GsAppstreamItem, 1);
		plugin->priv->item_temp->appcategories = g_ptr_array_new_with_free_func (g_free);
		break;
	case GS_APPSTREAM_XML_SECTION_ID:
	case GS_APPSTREAM_XML_SECTION_PKGNAME:
	case GS_APPSTREAM_XML_SECTION_NAME:
	case GS_APPSTREAM_XML_SECTION_SUMMARY:
	case GS_APPSTREAM_XML_SECTION_ICON:
		if (plugin->priv->item_temp == NULL ||
		    plugin->priv->section != GS_APPSTREAM_XML_SECTION_APPLICATION) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "XML start %s invalid, section %s",
				     element_name,
				     gs_appstream_selection_to_text (plugin->priv->section));
			return;
		}
		break;
	default:
		/* ignore unknown entries */
		break;
	}

	/* save */
	plugin->priv->section = section_new;
}

/**
 * gs_appstream_end_element_cb:
 */
static void
gs_appstream_end_element_cb (GMarkupParseContext *context,
			     const gchar *element_name,
			     gpointer user_data,
			     GError **error)
{
	GsPlugin *plugin = (GsPlugin *) user_data;
	GsAppstreamXmlSection section_new;

	section_new = gs_appstream_selection_from_text (element_name);
	switch (section_new) {
	case GS_APPSTREAM_XML_SECTION_APPLICATIONS:
	case GS_APPSTREAM_XML_SECTION_APPCATEGORIES:
	case GS_APPSTREAM_XML_SECTION_APPCATEGORY:
		/* ignore */
		break;
	case GS_APPSTREAM_XML_SECTION_APPLICATION:
		/* save */
		g_ptr_array_add (plugin->priv->array, plugin->priv->item_temp);
		g_hash_table_insert (plugin->priv->hash_id,
				     (gpointer) plugin->priv->item_temp->id,
				     plugin->priv->item_temp);
		g_hash_table_insert (plugin->priv->hash_pkgname,
				     (gpointer) plugin->priv->item_temp->pkgname,
				     plugin->priv->item_temp);
		plugin->priv->item_temp = NULL;
		plugin->priv->section = GS_APPSTREAM_XML_SECTION_APPLICATIONS;
		break;
	case GS_APPSTREAM_XML_SECTION_ID:
	case GS_APPSTREAM_XML_SECTION_PKGNAME:
	case GS_APPSTREAM_XML_SECTION_NAME:
	case GS_APPSTREAM_XML_SECTION_ICON:
	case GS_APPSTREAM_XML_SECTION_SUMMARY:
		plugin->priv->section = GS_APPSTREAM_XML_SECTION_APPLICATION;
		break;
	default:
		/* ignore unknown entries */
		break;
	}
}

/**
 * gs_appstream_text_cb:
 */
static void
gs_appstream_text_cb (GMarkupParseContext *context,
		      const gchar *text,
		      gsize text_len,
		      gpointer user_data,
		      GError **error)
{
	GsPlugin *plugin = (GsPlugin *) user_data;

	switch (plugin->priv->section) {
	case GS_APPSTREAM_XML_SECTION_UNKNOWN:
	case GS_APPSTREAM_XML_SECTION_APPLICATIONS:
	case GS_APPSTREAM_XML_SECTION_APPLICATION:
	case GS_APPSTREAM_XML_SECTION_APPCATEGORIES:
		/* ignore */
		break;
	case GS_APPSTREAM_XML_SECTION_APPCATEGORY:
		if (plugin->priv->item_temp == NULL) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     "item_temp category invalid");
			return;
		}
		g_ptr_array_add (plugin->priv->item_temp->appcategories,
				 g_strndup (text, text_len));
		break;
	case GS_APPSTREAM_XML_SECTION_ID:
		if (plugin->priv->item_temp == NULL ||
		    plugin->priv->item_temp->id != NULL) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     "item_temp id invalid");
			return;
		}
		if (text_len < 9) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     "desktop id invalid");
			return;
		}
		plugin->priv->item_temp->id = g_strndup (text, text_len - 8);
		break;
	case GS_APPSTREAM_XML_SECTION_PKGNAME:
		if (plugin->priv->item_temp == NULL ||
		    plugin->priv->item_temp->pkgname != NULL) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     "item_temp pkgname invalid");
			return;
		}
		plugin->priv->item_temp->pkgname = g_strndup (text, text_len);
		break;
	case GS_APPSTREAM_XML_SECTION_NAME:
		if (plugin->priv->item_temp == NULL ||
		    plugin->priv->item_temp->name != NULL) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     "item_temp name invalid");
			return;
		}
		plugin->priv->item_temp->name = g_strndup (text, text_len);
		break;
	case GS_APPSTREAM_XML_SECTION_SUMMARY:
		if (plugin->priv->item_temp == NULL ||
		    plugin->priv->item_temp->summary != NULL) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     "item_temp summary invalid");
			return;
		}
		plugin->priv->item_temp->summary = g_strndup (text, text_len);
		break;
	case GS_APPSTREAM_XML_SECTION_ICON:
		if (plugin->priv->item_temp == NULL ||
		    plugin->priv->item_temp->icon != NULL) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_FAILED,
					     "item_temp icon invalid");
			return;
		}
		plugin->priv->item_temp->icon = g_strndup (text, text_len);
		break;
	default:
		/* ignore unknown entries */
		break;
	}
}

/**
 * gs_plugin_parse_xml:
 */
static gboolean
gs_plugin_parse_xml (GsPlugin *plugin, GError **error)
{
	gboolean ret;
	GMarkupParseContext *ctx;
	gchar *data;
	gssize len;
	GFile *file;
	GConverter *converter;
	GInputStream *file_stream;
	GInputStream *converter_stream;
	const GMarkupParser parser = {
		gs_appstream_start_element_cb,
		gs_appstream_end_element_cb,
		gs_appstream_text_cb,
		NULL /* passthrough */,
		NULL /* error */ };

	file = g_file_new_for_path (DATADIR "/gnome-software/appstream.xml.gz");
	file_stream = G_INPUT_STREAM (g_file_read (file, NULL, error));
	g_object_unref (file);
	if (!file_stream)
		return FALSE;

	converter = G_CONVERTER (g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP));
	converter_stream = g_converter_input_stream_new (file_stream, converter);

	ctx = g_markup_parse_context_new (&parser,
					  G_MARKUP_PREFIX_ERROR_POSITION,
					  plugin,
					  NULL);

	ret = TRUE;
	data = g_malloc (32 * 1024);
	while ((len = g_input_stream_read (converter_stream, data, 32 * 1024, NULL, error)) > 0) {
		ret = g_markup_parse_context_parse (ctx, data, len, error);
		if (!ret)
			goto out;
	}

	if (len < 0)
		ret = FALSE;

 out:
	/* Reset in case we failed parsing */
	if (plugin->priv->item_temp) {
		gs_appstream_item_free (plugin->priv->item_temp);
		plugin->priv->item_temp = NULL;
	}

	g_markup_parse_context_free (ctx);
	g_free (data);
	g_object_unref (converter_stream);
	g_object_unref (file_stream);
	g_object_unref (converter);
	return ret;
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* create private area */
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->array = g_ptr_array_new_with_free_func (gs_appstream_item_free);
	plugin->priv->hash_id = g_hash_table_new_full (g_str_hash,
						       g_str_equal,
						       NULL,
						       NULL);
	plugin->priv->hash_pkgname = g_hash_table_new_full (g_str_hash,
							    g_str_equal,
							    NULL,
							    NULL);

	/* this is per-user */
	plugin->priv->cachedir = g_build_filename (g_get_user_cache_dir(),
						   "gnome-software",
						   NULL);
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 1.0f;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_free (plugin->priv->cachedir);
	g_ptr_array_unref (plugin->priv->array);
	g_hash_table_unref (plugin->priv->hash_id);
	g_hash_table_unref (plugin->priv->hash_pkgname);
}

/**
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GError **error)
{
	gboolean ret = TRUE;
	GTimer *timer = NULL;

	/* get the icons ready for use */
	timer = g_timer_new ();
	ret = gs_plugin_decompress_icons (plugin, error);
	if (!ret)
		goto out;
	g_debug ("Decompressing icons\t:%.1fms", g_timer_elapsed (timer, NULL) * 1000);

	/* Parse the XML */
	g_timer_reset (timer);
	ret = gs_plugin_parse_xml (plugin, error);
	if (!ret)
		goto out;
	g_debug ("Parsed %i entries of XML\t:%.1fms",
		 plugin->priv->array->len,
		 g_timer_elapsed (timer, NULL) * 1000);

out:
	if (timer != NULL)
		g_timer_destroy (timer);
	return ret;
}

/**
 * gs_plugin_refine_item:
 */
static gboolean
gs_plugin_refine_item (GsPlugin *plugin,
		       GsApp *app,
		       GsAppstreamItem *item,
		       GError **error)
{
	gboolean ret = TRUE;
	gchar *icon_path = NULL;
	GdkPixbuf *pixbuf = NULL;

	g_debug ("AppStream: Refining %s", gs_app_get_id (app));

	/* is an app */
	if (gs_app_get_kind (app) == GS_APP_KIND_UNKNOWN ||
	    gs_app_get_kind (app) == GS_APP_KIND_PACKAGE)
		gs_app_set_kind (app, GS_APP_KIND_NORMAL);

	/* set id */
	if (item->id != NULL && gs_app_get_id (app) == NULL)
		gs_app_set_id (app, item->id);

	/* set name */
	if (item->name != NULL && gs_app_get_name (app) == NULL)
		gs_app_set_name (app, item->name);

	/* set summary */
	if (item->summary != NULL && gs_app_get_summary (app) == NULL)
		gs_app_set_summary (app, item->summary);

	/* set icon */
	if (item->icon != NULL && gs_app_get_pixbuf (app) == NULL) {
		icon_path = g_strdup_printf ("%s/%s.png",
					     plugin->priv->cachedir,
					     item->icon);
		pixbuf = gdk_pixbuf_new_from_file_at_size (icon_path,
							   plugin->pixbuf_size,
							   plugin->pixbuf_size,
							   error);
		if (pixbuf == NULL) {
			ret = FALSE;
			goto out;
		}
		gs_app_set_pixbuf (app, pixbuf);
	}

	/* set package name */
	if (item->pkgname != NULL && gs_app_get_metadata_item (app, "package-name") == NULL)
		gs_app_set_metadata (app, "package-name", item->pkgname);
out:
	g_free (icon_path);
	if (pixbuf != NULL)
		g_object_unref (pixbuf);
	return ret;
}

/**
 * gs_plugin_refine_from_id:
 */
static gboolean
gs_plugin_refine_from_id (GsPlugin *plugin,
			  GsApp *app,
			  GError **error)
{
	const gchar *id;
	gboolean ret = TRUE;
	GsAppstreamItem *item;

	/* find anything that matches the ID */
	id = gs_app_get_id (app);
	if (id == NULL)
		goto out;
	item = g_hash_table_lookup (plugin->priv->hash_id, id);
	if (item == NULL) {
		g_debug ("no AppStream match for [id] %s", id);
		goto out;
	}

	/* set new properties */
	ret = gs_plugin_refine_item (plugin, app, item, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * gs_plugin_refine_from_pkgname:
 */
static gboolean
gs_plugin_refine_from_pkgname (GsPlugin *plugin,
			       GsApp *app,
			       GError **error)
{
	const gchar *pkgname;
	gboolean ret = TRUE;
	GsAppstreamItem *item;

	/* find anything that matches the ID */
	pkgname = gs_app_get_metadata_item (app, "package-name");
	if (pkgname == NULL)
		goto out;
	item = g_hash_table_lookup (plugin->priv->hash_pkgname, pkgname);
	if (item == NULL) {
		g_debug ("no AppStream match for {pkgname} %s", pkgname);
		goto out;
	}

	/* set new properties */
	ret = gs_plugin_refine_item (plugin, app, item, error);
	if (!ret)
		goto out;
out:
	return ret;
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList *list,
		  GCancellable *cancellable,
		  GError **error)
{
	gboolean ret;
	GList *l;
	GsApp *app;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);

		if (!ret)
			goto out;
	}

	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		ret = gs_plugin_refine_from_id (plugin, app, error);
		if (!ret)
			goto out;
		ret = gs_plugin_refine_from_pkgname (plugin, app, error);
		if (!ret)
			goto out;
	}

	/* sucess */
	ret = TRUE;
out:
	return ret;
}

static gboolean
in_array (GPtrArray *array, const gchar *search)
{
	const gchar *tmp;
	guint i;

	for (i = 0; i < array->len; i++) {
		tmp = g_ptr_array_index (array, i);
		if (g_strcmp0 (tmp, search) == 0)
			return TRUE;
	}
	return FALSE;
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
	const gchar *search_id1;
	const gchar *search_id2 = NULL;
	gboolean ret = TRUE;
	GsApp *app;
	GsAppstreamItem *item;
	GsCategory *parent;
	guint i;

	/* get the two search terms */
	search_id1 = gs_category_get_id (category);
	parent = gs_category_get_parent (category);
	if (parent != NULL)
		search_id2 = gs_category_get_id (parent);

	/* the "General" item has no ID */
	if (search_id1 == NULL) {
		search_id1 = search_id2;
		search_id2 = NULL;
	}

	/* just look at each app in turn */
	for (i = 0; i < plugin->priv->array->len; i++) {
		item = g_ptr_array_index (plugin->priv->array, i);
		if (item->id == NULL)
			continue;
		if (!in_array (item->appcategories, search_id1))
			continue;
		if (search_id2 != NULL && !in_array (item->appcategories, search_id2))
			continue;

		/* got a search match, so add all the data we can */
		app = gs_app_new (item->id);
		ret = gs_plugin_refine_item (plugin, app, item, error);
		if (!ret)
			goto out;
		gs_plugin_add_app (list, app);
	}
out:
	return ret;
}
