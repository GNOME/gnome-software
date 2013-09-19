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

#include "config.h"

#include "appstream-cache.h"

static void	appstream_cache_finalize	(GObject	*object);

#define APPSTREAM_CACHE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), APPSTREAM_TYPE_CACHE, AppstreamCachePrivate))

typedef enum {
	APPSTREAM_CACHE_SECTION_UNKNOWN,
	APPSTREAM_CACHE_SECTION_APPLICATIONS,
	APPSTREAM_CACHE_SECTION_APPLICATION,
	APPSTREAM_CACHE_SECTION_ID,
	APPSTREAM_CACHE_SECTION_PKGNAME,
	APPSTREAM_CACHE_SECTION_NAME,
	APPSTREAM_CACHE_SECTION_SUMMARY,
	APPSTREAM_CACHE_SECTION_DESCRIPTION,
	APPSTREAM_CACHE_SECTION_URL,
	APPSTREAM_CACHE_SECTION_ICON,
	APPSTREAM_CACHE_SECTION_APPCATEGORIES,
	APPSTREAM_CACHE_SECTION_APPCATEGORY,
	APPSTREAM_CACHE_SECTION_KEYWORDS,
	APPSTREAM_CACHE_SECTION_KEYWORD,
	APPSTREAM_CACHE_SECTION_PROJECT_GROUP,
	APPSTREAM_CACHE_SECTION_LAST
} AppstreamCacheSection;

struct AppstreamCachePrivate
{
	GPtrArray		*array;		/* of AppstreamApp */
	GPtrArray		*icon_path_array;
	GHashTable		*hash_id;	/* of AppstreamApp{id} */
	GHashTable		*hash_pkgname;	/* of AppstreamApp{pkgname} */
};

G_DEFINE_TYPE (AppstreamCache, appstream_cache, G_TYPE_OBJECT)

/**
 * appstream_cache_error_quark:
 **/
GQuark
appstream_cache_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("appstream_cache_error");
	return quark;
}

/**
 * appstream_cache_get_size:
 */
guint
appstream_cache_get_size (AppstreamCache *cache)
{
	g_return_val_if_fail (APPSTREAM_IS_CACHE (cache), 0);
	return cache->priv->array->len;
}

/**
 * appstream_cache_get_items:
 */
GPtrArray *
appstream_cache_get_items (AppstreamCache *cache)
{
	g_return_val_if_fail (APPSTREAM_IS_CACHE (cache), NULL);
	return cache->priv->array;
}

/**
 * appstream_cache_get_item_by_id:
 */
AppstreamApp *
appstream_cache_get_item_by_id (AppstreamCache *cache, const gchar *id)
{
	g_return_val_if_fail (APPSTREAM_IS_CACHE (cache), NULL);
	return g_hash_table_lookup (cache->priv->hash_id, id);
}

/**
 * appstream_cache_get_item_by_pkgname:
 */
AppstreamApp *
appstream_cache_get_item_by_pkgname (AppstreamCache *cache, const gchar *pkgname)
{
	g_return_val_if_fail (APPSTREAM_IS_CACHE (cache), NULL);
	return g_hash_table_lookup (cache->priv->hash_pkgname, pkgname);
}

/**
 * appstream_cache_selection_from_string:
 */
static AppstreamCacheSection
appstream_cache_selection_from_string (const gchar *element_name)
{
	if (g_strcmp0 (element_name, "applications") == 0)
		return APPSTREAM_CACHE_SECTION_APPLICATIONS;
	if (g_strcmp0 (element_name, "application") == 0)
		return APPSTREAM_CACHE_SECTION_APPLICATION;
	if (g_strcmp0 (element_name, "id") == 0)
		return APPSTREAM_CACHE_SECTION_ID;
	if (g_strcmp0 (element_name, "pkgname") == 0)
		return APPSTREAM_CACHE_SECTION_PKGNAME;
	if (g_strcmp0 (element_name, "name") == 0)
		return APPSTREAM_CACHE_SECTION_NAME;
	if (g_strcmp0 (element_name, "summary") == 0)
		return APPSTREAM_CACHE_SECTION_SUMMARY;
	if (g_strcmp0 (element_name, "project_group") == 0)
		return APPSTREAM_CACHE_SECTION_PROJECT_GROUP;
	if (g_strcmp0 (element_name, "url") == 0)
		return APPSTREAM_CACHE_SECTION_URL;
	if (g_strcmp0 (element_name, "description") == 0)
		return APPSTREAM_CACHE_SECTION_DESCRIPTION;
	if (g_strcmp0 (element_name, "icon") == 0)
		return APPSTREAM_CACHE_SECTION_ICON;
	if (g_strcmp0 (element_name, "appcategories") == 0)
		return APPSTREAM_CACHE_SECTION_APPCATEGORIES;
	if (g_strcmp0 (element_name, "appcategory") == 0)
		return APPSTREAM_CACHE_SECTION_APPCATEGORY;
	if (g_strcmp0 (element_name, "keywords") == 0)
		return APPSTREAM_CACHE_SECTION_KEYWORDS;
	if (g_strcmp0 (element_name, "keyword") == 0)
		return APPSTREAM_CACHE_SECTION_KEYWORD;
	return APPSTREAM_CACHE_SECTION_UNKNOWN;
}

/**
 * appstream_cache_selection_to_string:
 */
static const gchar *
appstream_cache_selection_to_string (AppstreamCacheSection section)
{
	if (section == APPSTREAM_CACHE_SECTION_APPLICATIONS)
		return "applications";
	if (section == APPSTREAM_CACHE_SECTION_APPLICATION)
		return "application";
	if (section == APPSTREAM_CACHE_SECTION_ID)
		return "id";
	if (section == APPSTREAM_CACHE_SECTION_PKGNAME)
		return "pkgname";
	if (section == APPSTREAM_CACHE_SECTION_NAME)
		return "name";
	if (section == APPSTREAM_CACHE_SECTION_SUMMARY)
		return "summary";
	if (section == APPSTREAM_CACHE_SECTION_PROJECT_GROUP)
		return "project_group";
	if (section == APPSTREAM_CACHE_SECTION_URL)
		return "url";
	if (section == APPSTREAM_CACHE_SECTION_DESCRIPTION)
		return "description";
	if (section == APPSTREAM_CACHE_SECTION_ICON)
		return "icon";
	if (section == APPSTREAM_CACHE_SECTION_APPCATEGORIES)
		return "appcategories";
	if (section == APPSTREAM_CACHE_SECTION_APPCATEGORY)
		return "appcategory";
	if (section == APPSTREAM_CACHE_SECTION_KEYWORDS)
		return "keywords";
	if (section == APPSTREAM_CACHE_SECTION_KEYWORD)
		return "keyword";
	return NULL;
}

/**
 * appstream_cache_icon_kind_from_string:
 */
static AppstreamAppIconKind
appstream_cache_icon_kind_from_string (const gchar *kind_str)
{
	if (g_strcmp0 (kind_str, "stock") == 0)
		return APPSTREAM_APP_ICON_KIND_STOCK;
	if (g_strcmp0 (kind_str, "local") == 0 ||
	    g_strcmp0 (kind_str, "cached") == 0)
		return APPSTREAM_APP_ICON_KIND_CACHED;
	return APPSTREAM_APP_ICON_KIND_UNKNOWN;
}

typedef struct {
	const gchar		*path_icons;
	AppstreamApp		*item_temp;
	char			*lang_temp;
	AppstreamCache		*cache;
	AppstreamCacheSection	 section;
} AppstreamCacheHelper;

/**
 * appstream_cache_start_element_cb:
 */
static void
appstream_cache_start_element_cb (GMarkupParseContext *context,
				  const gchar *element_name,
				  const gchar **attribute_names,
				  const gchar **attribute_values,
				  gpointer user_data,
				  GError **error)
{
	AppstreamCacheHelper *helper = (AppstreamCacheHelper *) user_data;
	AppstreamCacheSection section_new;
	guint i;

	/* process tag start */
	section_new = appstream_cache_selection_from_string (element_name);
	switch (section_new) {
	case APPSTREAM_CACHE_SECTION_APPLICATIONS:
	case APPSTREAM_CACHE_SECTION_APPCATEGORIES:
	case APPSTREAM_CACHE_SECTION_APPCATEGORY:
	case APPSTREAM_CACHE_SECTION_KEYWORDS:
	case APPSTREAM_CACHE_SECTION_KEYWORD:
		/* ignore */
		break;
	case APPSTREAM_CACHE_SECTION_APPLICATION:
		if (helper->item_temp != NULL ||
		    helper->section != APPSTREAM_CACHE_SECTION_APPLICATIONS) {
			g_set_error (error,
				     APPSTREAM_CACHE_ERROR,
				     APPSTREAM_CACHE_ERROR_FAILED,
				     "XML start %s invalid, section %s",
				     element_name,
				     appstream_cache_selection_to_string (helper->section));
			return;
		}
		helper->item_temp = appstream_app_new ();
		appstream_app_set_userdata (helper->item_temp,
					    (gpointer) helper->path_icons,
					    NULL);
		break;

	case APPSTREAM_CACHE_SECTION_ICON:
		/* get the icon kind */
		for (i = 0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "type") == 0) {
				appstream_app_set_icon_kind (helper->item_temp,
							     appstream_cache_icon_kind_from_string (attribute_values[i]));
				break;
			}
		}
		if (appstream_app_get_icon_kind (helper->item_temp) == APPSTREAM_APP_ICON_KIND_UNKNOWN) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "icon type not set");
		}
		break;
	case APPSTREAM_CACHE_SECTION_ID:
	case APPSTREAM_CACHE_SECTION_PKGNAME:
	case APPSTREAM_CACHE_SECTION_URL:
	case APPSTREAM_CACHE_SECTION_PROJECT_GROUP:
		if (helper->item_temp == NULL ||
		    helper->section != APPSTREAM_CACHE_SECTION_APPLICATION) {
			g_set_error (error,
				     APPSTREAM_CACHE_ERROR,
				     APPSTREAM_CACHE_ERROR_FAILED,
				     "XML start %s invalid, section %s",
				     element_name,
				     appstream_cache_selection_to_string (helper->section));
			return;
		}
		break;

	case APPSTREAM_CACHE_SECTION_NAME:
	case APPSTREAM_CACHE_SECTION_SUMMARY:
	case APPSTREAM_CACHE_SECTION_DESCRIPTION:
		if (helper->item_temp == NULL ||
		    helper->section != APPSTREAM_CACHE_SECTION_APPLICATION) {
			g_set_error (error,
				     APPSTREAM_CACHE_ERROR,
				     APPSTREAM_CACHE_ERROR_FAILED,
				     "XML start %s invalid, section %s",
				     element_name,
				     appstream_cache_selection_to_string (helper->section));
			return;
		}
		if (!g_markup_collect_attributes (element_name, attribute_names, attribute_values, error,
						  G_MARKUP_COLLECT_STRDUP | G_MARKUP_COLLECT_OPTIONAL,
						  "xml:lang", &helper->lang_temp,
						  G_MARKUP_COLLECT_INVALID))
			return;
		if (!helper->lang_temp)
			helper->lang_temp = g_strdup ("C");
		break;
	default:
		/* ignore unknown entries */
		break;
	}

	/* save */
	helper->section = section_new;
}

/**
 * appstream_cache_end_element_cb:
 */
static void
appstream_cache_end_element_cb (GMarkupParseContext *context,
				const gchar *element_name,
				gpointer user_data,
				GError **error)
{
	const gchar *id;
	AppstreamCacheHelper *helper = (AppstreamCacheHelper *) user_data;
	AppstreamCachePrivate *priv = helper->cache->priv;
	AppstreamCacheSection section_new;
	AppstreamApp *item;

	section_new = appstream_cache_selection_from_string (element_name);
	switch (section_new) {
	case APPSTREAM_CACHE_SECTION_APPLICATIONS:
	case APPSTREAM_CACHE_SECTION_APPCATEGORY:
	case APPSTREAM_CACHE_SECTION_KEYWORD:
		/* ignore */
		break;
	case APPSTREAM_CACHE_SECTION_APPLICATION:

		/* have we recorded this before? */
		id = appstream_app_get_id (helper->item_temp);
		item = g_hash_table_lookup (priv->hash_id, id);
		if (item != NULL) {
			g_warning ("duplicate AppStream entry: %s", id);
			appstream_app_free (helper->item_temp);
		} else {
			g_ptr_array_add (priv->array, helper->item_temp);
			g_hash_table_insert (priv->hash_id,
					     (gpointer) appstream_app_get_id (helper->item_temp),
					     helper->item_temp);
			g_hash_table_insert (priv->hash_pkgname,
					     (gpointer) appstream_app_get_pkgname (helper->item_temp),
					     helper->item_temp);
		}
		helper->item_temp = NULL;
		helper->section = APPSTREAM_CACHE_SECTION_APPLICATIONS;
		break;
	case APPSTREAM_CACHE_SECTION_ID:
	case APPSTREAM_CACHE_SECTION_PKGNAME:
	case APPSTREAM_CACHE_SECTION_APPCATEGORIES:
	case APPSTREAM_CACHE_SECTION_KEYWORDS:
	case APPSTREAM_CACHE_SECTION_URL:
	case APPSTREAM_CACHE_SECTION_ICON:
		helper->section = APPSTREAM_CACHE_SECTION_APPLICATION;
		break;
	case APPSTREAM_CACHE_SECTION_NAME:
	case APPSTREAM_CACHE_SECTION_SUMMARY:
	case APPSTREAM_CACHE_SECTION_PROJECT_GROUP:
	case APPSTREAM_CACHE_SECTION_DESCRIPTION:
		helper->section = APPSTREAM_CACHE_SECTION_APPLICATION;
		g_free (helper->lang_temp);
		helper->lang_temp = NULL;
		break;
	default:
		/* ignore unknown entries */
		helper->section = APPSTREAM_CACHE_SECTION_APPLICATION;
		break;
	}
}

/**
 * appstream_cache_text_cb:
 */
static void
appstream_cache_text_cb (GMarkupParseContext *context,
			 const gchar *text,
			 gsize text_len,
			 gpointer user_data,
			 GError **error)
{
	AppstreamCacheHelper *helper = (AppstreamCacheHelper *) user_data;

	switch (helper->section) {
	case APPSTREAM_CACHE_SECTION_UNKNOWN:
	case APPSTREAM_CACHE_SECTION_APPLICATIONS:
	case APPSTREAM_CACHE_SECTION_APPLICATION:
	case APPSTREAM_CACHE_SECTION_APPCATEGORIES:
	case APPSTREAM_CACHE_SECTION_KEYWORDS:
		/* ignore */
		break;
	case APPSTREAM_CACHE_SECTION_APPCATEGORY:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp category invalid");
			return;
		}
		appstream_app_add_category (helper->item_temp, text, text_len);
		break;
	case APPSTREAM_CACHE_SECTION_KEYWORD:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp category invalid");
			return;
		}
		appstream_app_add_keyword (helper->item_temp, text, text_len);
		break;
	case APPSTREAM_CACHE_SECTION_ID:
		if (helper->item_temp == NULL ||
		    appstream_app_get_id (helper->item_temp) != NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp id invalid");
			return;
		}
		appstream_app_set_id (helper->item_temp, text, text_len);
		break;
	case APPSTREAM_CACHE_SECTION_PKGNAME:
		if (helper->item_temp == NULL ||
		    appstream_app_get_pkgname (helper->item_temp) != NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp pkgname invalid");
			return;
		}
		appstream_app_set_pkgname (helper->item_temp, text, text_len);
		break;
	case APPSTREAM_CACHE_SECTION_NAME:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp name invalid");
			return;
		}
		appstream_app_set_name (helper->item_temp, helper->lang_temp, text, text_len);
		break;
	case APPSTREAM_CACHE_SECTION_SUMMARY:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp summary invalid");
			return;
		}
		appstream_app_set_summary (helper->item_temp, helper->lang_temp, text, text_len);
		break;
	case APPSTREAM_CACHE_SECTION_PROJECT_GROUP:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp project_group invalid");
			return;
		}
		appstream_app_set_project_group (helper->item_temp, text, text_len);
		break;
	case APPSTREAM_CACHE_SECTION_URL:
		if (helper->item_temp == NULL ||
		    appstream_app_get_url (helper->item_temp) != NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp url invalid");
			return;
		}
		appstream_app_set_url (helper->item_temp, text, text_len);
		break;
	case APPSTREAM_CACHE_SECTION_DESCRIPTION:
		if (helper->item_temp == NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp description invalid");
			return;
		}
		appstream_app_set_description (helper->item_temp, helper->lang_temp, text, text_len);
		break;
	case APPSTREAM_CACHE_SECTION_ICON:
		if (helper->item_temp == NULL ||
		    appstream_app_get_icon (helper->item_temp) != NULL) {
			g_set_error_literal (error,
					     APPSTREAM_CACHE_ERROR,
					     APPSTREAM_CACHE_ERROR_FAILED,
					     "item_temp icon invalid");
			return;
		}
		appstream_app_set_icon (helper->item_temp, text, text_len);
		break;
	default:
		/* ignore unknown entries */
		break;
	}
}

/**
 * appstream_cache_parse_file:
 */
gboolean
appstream_cache_parse_file (AppstreamCache *cache,
			    GFile *file,
			    const gchar *path_icons,
			    GCancellable *cancellable,
			    GError **error)
{
	const gchar *content_type = NULL;
	gboolean ret = TRUE;
	gchar *data = NULL;
	gchar *icon_path_tmp = NULL;
	GConverter *converter = NULL;
	GFileInfo *info = NULL;
	GInputStream *file_stream;
	GInputStream *stream_data = NULL;
	GMarkupParseContext *ctx = NULL;
	AppstreamCacheHelper *helper = NULL;
	gssize len;
	const GMarkupParser parser = {
		appstream_cache_start_element_cb,
		appstream_cache_end_element_cb,
		appstream_cache_text_cb,
		NULL /* passthrough */,
		NULL /* error */ };

	g_return_val_if_fail (APPSTREAM_IS_CACHE (cache), FALSE);

	file_stream = G_INPUT_STREAM (g_file_read (file, cancellable, error));
	if (file_stream == NULL) {
		ret = FALSE;
		goto out;
	}

	/* what kind of file is this */
	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
				  G_FILE_QUERY_INFO_NONE,
				  cancellable,
				  error);
	if (info == NULL) {
		ret = FALSE;
		goto out;
	}

	/* decompress if required */
	content_type = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);
	if (g_strcmp0 (content_type, "application/gzip") == 0 ||
	    g_strcmp0 (content_type, "application/x-gzip") == 0) {
		converter = G_CONVERTER (g_zlib_decompressor_new (G_ZLIB_COMPRESSOR_FORMAT_GZIP));
		stream_data = g_converter_input_stream_new (file_stream, converter);
	} else if (g_strcmp0 (content_type, "application/xml") == 0) {
		stream_data = g_object_ref (file_stream);
	} else {
		ret = FALSE;
		g_set_error (error,
			     APPSTREAM_CACHE_ERROR,
			     APPSTREAM_CACHE_ERROR_FAILED,
			     "cannot process file of type %s",
			     content_type);
	}

	/* add to array to maintain a ref for the lifetime of the AppstreamApp */
	icon_path_tmp = g_strdup (path_icons);
	g_ptr_array_add (cache->priv->icon_path_array, icon_path_tmp);

	helper = g_new0 (AppstreamCacheHelper, 1);
	helper->cache = cache;
	helper->path_icons = icon_path_tmp;
	ctx = g_markup_parse_context_new (&parser,
					  G_MARKUP_PREFIX_ERROR_POSITION,
					  helper,
					  NULL);
	data = g_malloc (32 * 1024);
	while ((len = g_input_stream_read (stream_data, data, 32 * 1024, NULL, error)) > 0) {
		ret = g_markup_parse_context_parse (ctx, data, len, error);
		if (!ret)
			goto out;
	}
	if (len < 0)
		ret = FALSE;
out:
	if (helper != NULL && helper->item_temp != NULL)
		appstream_app_free (helper->item_temp);
	if (info != NULL)
		g_object_unref (info);
	g_free (helper);
	g_free (data);
	if (ctx != NULL)
		g_markup_parse_context_free (ctx);
	if (stream_data != NULL)
		g_object_unref (stream_data);
	if (converter != NULL)
		g_object_unref (converter);
	g_object_unref (file_stream);
	return ret;
}

/**
 * appstream_cache_class_init:
 **/
static void
appstream_cache_class_init (AppstreamCacheClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = appstream_cache_finalize;
	g_type_class_add_private (klass, sizeof (AppstreamCachePrivate));
}

/**
 * appstream_cache_init:
 **/
static void
appstream_cache_init (AppstreamCache *cache)
{
	AppstreamCachePrivate *priv;
	priv = cache->priv = APPSTREAM_CACHE_GET_PRIVATE (cache);
	priv->array = g_ptr_array_new_with_free_func ((GDestroyNotify) appstream_app_free);
	priv->icon_path_array = g_ptr_array_new_with_free_func (g_free);
	priv->hash_id = g_hash_table_new_full (g_str_hash,
					       g_str_equal,
					       NULL,
					       NULL);
	priv->hash_pkgname = g_hash_table_new_full (g_str_hash,
						    g_str_equal,
						    NULL,
						    NULL);
}

/**
 * appstream_cache_finalize:
 **/
static void
appstream_cache_finalize (GObject *object)
{
	AppstreamCache *cache = APPSTREAM_CACHE (object);
	AppstreamCachePrivate *priv = cache->priv;

	g_ptr_array_unref (priv->array);
	g_ptr_array_unref (priv->icon_path_array);
	g_hash_table_unref (priv->hash_id);
	g_hash_table_unref (priv->hash_pkgname);

	G_OBJECT_CLASS (appstream_cache_parent_class)->finalize (object);
}

/**
 * appstream_cache_new:
 **/
AppstreamCache *
appstream_cache_new (void)
{
	AppstreamCache *cache;
	cache = g_object_new (APPSTREAM_TYPE_CACHE, NULL);
	return APPSTREAM_CACHE (cache);
}
