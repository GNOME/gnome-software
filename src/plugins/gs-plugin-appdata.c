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

#include "appstream-common.h"

struct GsPluginPrivate {
	gchar			*cachedir;
	gsize			 done_init;
	GHashTable		*hash;		/* of "id" : "filename" */
};


/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "appdata";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->cachedir = g_build_filename (DATADIR,
						   "appdata",
						   NULL);
	plugin->priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal,
						    g_free, g_free);
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 1.01f;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_free (plugin->priv->cachedir);
	g_hash_table_unref (plugin->priv->hash);
}

/**
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GError **error)
{
	gboolean ret = TRUE;
	GDir *dir;
	const gchar *tmp;
	gchar *ext_tmp;
	gchar *id;

	/* find all the files installed */
	dir = g_dir_open (plugin->priv->cachedir, 0, error);
	if (dir == NULL)
		goto out;
	while ((tmp = g_dir_read_name (dir)) != NULL) {
		if (g_strcmp0 (tmp, "schema") == 0)
			continue;
		if (!g_str_has_suffix (tmp, ".appdata.xml")) {
			g_warning ("AppData: not a data file: %s/%s",
				   plugin->priv->cachedir, tmp);
			continue;
		}
		id = g_strdup (tmp);
		ext_tmp = g_strstr_len (id, -1, ".appdata.xml");
		if (ext_tmp != NULL)
			*ext_tmp = '\0';
		g_hash_table_insert (plugin->priv->hash,
				     id,
				     g_build_filename (plugin->priv->cachedir,
						       tmp, NULL));
	}
out:
	if (dir != NULL)
		g_dir_close (dir);
	return ret;
}

typedef enum {
	APPSTREAM_DESCRIPTION_TAG_START,
	APPSTREAM_DESCRIPTION_TAG_END,
	APPSTREAM_DESCRIPTION_TAG_P_START,
	APPSTREAM_DESCRIPTION_TAG_P_CONTENT,
	APPSTREAM_DESCRIPTION_TAG_P_END,
	APPSTREAM_DESCRIPTION_TAG_UL_START,
	APPSTREAM_DESCRIPTION_TAG_UL_CONTENT,
	APPSTREAM_DESCRIPTION_TAG_UL_END,
	APPSTREAM_DESCRIPTION_TAG_LI_START,
	APPSTREAM_DESCRIPTION_TAG_LI_CONTENT,
	APPSTREAM_DESCRIPTION_TAG_LI_END,
	APPSTREAM_DESCRIPTION_TAG_LAST
} AppStreamDescriptionTag;

typedef struct {
	AppstreamTag		 tag;
	GsApp			*app;
	GString			*string;
	AppStreamDescriptionTag	 description_tag;
	gchar			*lang;
	guint			 locale_value;
} AppstreamCacheHelper;

/**
 * appstream_description_build:
 */
static void
appstream_description_build (AppstreamCacheHelper *helper,
			     AppStreamDescriptionTag tag,
			     const gchar *text)
{
	guint locale_value;

	/* we are not interested */
	if (helper->string == NULL)
		return;

	/* is this worse than the locale we're already showing */
	locale_value = appstream_get_locale_value (helper->lang);
	if (locale_value > helper->locale_value)
		return;

	/* is this better than the previous locale */
	if (locale_value < helper->locale_value) {
		g_debug ("Dumping existing string for locale %s!", helper->lang);
		g_string_set_size (helper->string, 0);
		helper->locale_value = locale_value;
	}


	/* format markup in the same way as the distro pre-processor */
	switch (tag) {
	case APPSTREAM_DESCRIPTION_TAG_START:
	case APPSTREAM_DESCRIPTION_TAG_P_END:
	case APPSTREAM_DESCRIPTION_TAG_UL_START:
	case APPSTREAM_DESCRIPTION_TAG_UL_CONTENT:
	case APPSTREAM_DESCRIPTION_TAG_UL_END:
	case APPSTREAM_DESCRIPTION_TAG_LI_START:
	case APPSTREAM_DESCRIPTION_TAG_LI_END:
		/* ignore */
		break;
	case APPSTREAM_DESCRIPTION_TAG_END:
		/* remove trailing newline */
		g_string_truncate (helper->string, helper->string->len - 1);
		break;
		break;
	case APPSTREAM_DESCRIPTION_TAG_P_CONTENT:
		g_string_append_printf (helper->string, "%s\n", text);
		break;
	case APPSTREAM_DESCRIPTION_TAG_LI_CONTENT:
		g_string_append_printf (helper->string, " • %s\n", text);
		break;
	case APPSTREAM_DESCRIPTION_TAG_P_START:
		if (helper->string->len > 0)
			g_string_append (helper->string, "\n");
		break;
	default:
		break;
	}
}

/**
 * appdata_parse_start_element_cb:
 */
static void
appdata_parse_start_element_cb (GMarkupParseContext *context,
				const gchar *element_name,
				const gchar **attribute_names,
				const gchar **attribute_values,
				gpointer user_data,
				GError **error)
{
	AppstreamCacheHelper *helper = (AppstreamCacheHelper *) user_data;
	const gchar *lang_tmp = NULL;
	guint i;

	/* description markup */
	if (helper->tag == APPSTREAM_TAG_DESCRIPTION) {

		/* save xml:lang if different to existing */
		for (i = 0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "xml:lang") == 0) {
				lang_tmp = attribute_values[i];
				break;
			}
		}
		if (lang_tmp == NULL)
			lang_tmp = "C";
		if (g_strcmp0 (lang_tmp, helper->lang) != 0) {
			g_free (helper->lang);
			helper->lang = g_strdup (lang_tmp);
		}

		/* build string */
		if (g_strcmp0 (element_name, "p") == 0) {
			appstream_description_build (helper,
						     APPSTREAM_DESCRIPTION_TAG_P_START,
						     NULL);
			helper->description_tag = APPSTREAM_DESCRIPTION_TAG_P_CONTENT;
		} else if (g_strcmp0 (element_name, "ul") == 0) {
			appstream_description_build (helper,
						     APPSTREAM_DESCRIPTION_TAG_UL_START,
						     NULL);
			helper->description_tag = APPSTREAM_DESCRIPTION_TAG_UL_CONTENT;
		} else if (g_strcmp0 (element_name, "li") == 0) {
			appstream_description_build (helper,
						     APPSTREAM_DESCRIPTION_TAG_LI_START,
						     NULL);
			helper->description_tag = APPSTREAM_DESCRIPTION_TAG_LI_CONTENT;
		}
		return;
	}

	helper->tag = appstream_tag_from_string (element_name);
	switch (helper->tag) {
	case APPSTREAM_TAG_DESCRIPTION:
		helper->string = NULL;
		/* only process the description if it's not already been set;
		 * doing all this string munging is moderately expensive */
		if (gs_app_get_description (helper->app) == NULL)
			helper->string = g_string_new ("");
		appstream_description_build (helper,
					     APPSTREAM_DESCRIPTION_TAG_START,
					     NULL);
		break;
	case APPSTREAM_TAG_UNKNOWN:
		g_warning ("AppData: tag %s unknown", element_name);
		break;
	default:
		break;
	}
}


/**
 * appdata_parse_end_element_cb:
 */
static void
appdata_parse_end_element_cb (GMarkupParseContext *context,
			      const gchar *element_name,
			      gpointer user_data,
			      GError **error)
{
	AppstreamCacheHelper *helper = (AppstreamCacheHelper *) user_data;
	if (helper->tag == APPSTREAM_TAG_DESCRIPTION) {
		if (g_strcmp0 (element_name, "p") == 0) {
			appstream_description_build (helper,
						     APPSTREAM_DESCRIPTION_TAG_P_END,
						     NULL);
		} else if (g_strcmp0 (element_name, "ul") == 0) {
			appstream_description_build (helper,
						     APPSTREAM_DESCRIPTION_TAG_UL_END,
						     NULL);
		} else if (g_strcmp0 (element_name, "li") == 0) {
			appstream_description_build (helper,
						     APPSTREAM_DESCRIPTION_TAG_LI_END,
						     NULL);
		} else if (g_strcmp0 (element_name, "description") == 0) {
			appstream_description_build (helper,
						     APPSTREAM_DESCRIPTION_TAG_END,
						     NULL);
			if (helper->string != NULL) {
				g_debug ("AppData: Setting description: %s",
					 helper->string->str);
				gs_app_set_description (helper->app,
							helper->string->str);
				g_string_free (helper->string, TRUE);
			}
			helper->tag = APPSTREAM_TAG_APPLICATION;
		}
	} else {
		helper->tag = APPSTREAM_TAG_APPLICATION;
	}
}

/**
 * appdata_xml_unmunge:
 */
static gchar *
appdata_xml_unmunge (const gchar *text, guint text_length)
{
	GString *str;
	guint i;
	gboolean ignore_whitespace = TRUE;

	/* ignore repeated whitespace */
	str = g_string_sized_new (text_length);
	for (i = 0; i < text_length; i++) {
		if (text[i] == ' ') {
			if (!ignore_whitespace)
				g_string_append_c (str, ' ');
			ignore_whitespace = TRUE;
		} else if (text[i] == '\n') {
			continue;
		} else {
			g_string_append_c (str, text[i]);
			ignore_whitespace = FALSE;
		}
	}

	/* nothing left */
	if (str->len == 0) {
		g_string_free (str, TRUE);
		return NULL;
	}

	/* remove trailing space */
	if (str->str[str->len - 1] == ' ')
		g_string_truncate (str, str->len - 1);
	return g_string_free (str, FALSE);
}

/**
 * appdata_parse_text_cb:
 */
static void
appdata_parse_text_cb (GMarkupParseContext *context,
			const gchar *text,
			gsize text_len,
			gpointer user_data,
			GError **error)
{
	AppstreamCacheHelper *helper = (AppstreamCacheHelper *) user_data;
	gchar *tmp = NULL;

	/* no useful content */
	if (text_len == 0)
		return;

	switch (helper->tag) {
	case APPSTREAM_TAG_APPLICATION:
	case APPSTREAM_TAG_APPLICATIONS:
	case APPSTREAM_TAG_ID:
	case APPSTREAM_TAG_LICENCE:
	case APPSTREAM_TAG_SCREENSHOTS:
	case APPSTREAM_TAG_UPDATECONTACT:
		/* ignore */
		break;
	case APPSTREAM_TAG_DESCRIPTION:
		tmp = appdata_xml_unmunge (text, text_len);
		if (tmp == NULL)
			break;
		appstream_description_build (helper,
					     helper->description_tag,
					     tmp);
		break;
	case APPSTREAM_TAG_SCREENSHOT:
		/* FIXME: actually add to API */
		//tmp = appdata_xml_unmunge (text, text_len);
		//gs_app_add_screenshot (helper->app, tmp);
		break;
	case APPSTREAM_TAG_NAME:
		// FIXME: does not get best language
		if (gs_app_get_name (helper->app) == NULL) {
			tmp = appdata_xml_unmunge (text, text_len);
			if (tmp == NULL)
				break;
			g_debug ("AppData: Setting name: %s", tmp);
			gs_app_set_name (helper->app, tmp);
		}
		break;
	case APPSTREAM_TAG_SUMMARY:
		// FIXME: does not get best language
		if (gs_app_get_summary (helper->app) == NULL) {
			tmp = appdata_xml_unmunge (text, text_len);
			if (tmp == NULL)
				break;
			g_debug ("AppData: Setting summary: %s", tmp);
			gs_app_set_summary (helper->app, tmp);
		}
		break;
	case APPSTREAM_TAG_URL:
		if (gs_app_get_url (helper->app) == NULL) {
			tmp = appdata_xml_unmunge (text, text_len);
			if (tmp == NULL)
				break;
			g_debug ("AppData: Setting URL: %s", tmp);
			gs_app_set_url (helper->app, tmp);
		}
		break;
	default:
		tmp = appdata_xml_unmunge (text, text_len);
		if (tmp == NULL)
			break;
		g_warning ("AppData: unknown data '%s' is '%s'",
			   appstream_tag_to_string (helper->tag), tmp);
		break;
	}
	g_free (tmp);

}

/**
 * gs_plugin_refine_by_local_appdata:
 */
static gboolean
gs_plugin_refine_by_local_appdata (GsApp *app,
				   const gchar *filename,
				   GError **error)
{
	const GMarkupParser parser = {
		appdata_parse_start_element_cb,
		appdata_parse_end_element_cb,
		appdata_parse_text_cb,
		NULL /* passthrough */,
		NULL /* error */ };
	AppstreamCacheHelper *helper = NULL;
	gchar *data;
	gboolean ret;
	GMarkupParseContext *ctx = NULL;

	/* read file */
	ret = g_file_get_contents (filename, &data, NULL, error);
	if (!ret)
		goto out;

	/* parse file */
	helper = g_new0 (AppstreamCacheHelper, 1);
	helper->app = app;
	helper->locale_value = G_MAXUINT;
	ctx = g_markup_parse_context_new (&parser,
					  G_MARKUP_PREFIX_ERROR_POSITION,
					  helper,
					  NULL);
	ret = g_markup_parse_context_parse (ctx, data, -1, error);
	if (!ret)
		goto out;
out:
	if (ctx != NULL)
		g_markup_parse_context_free (ctx);
	g_free (helper->lang);
	g_free (helper);
	g_free (data);
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
	GList *l;
	GsApp *app;
	const gchar *id;
	const gchar *tmp;
	gboolean ret = TRUE;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			goto out;
	}

	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		id = gs_app_get_id (app);
		if (id == NULL)
			continue;
		tmp = g_hash_table_lookup (plugin->priv->hash, id);
		if (tmp != NULL) {
			g_debug ("AppData: refine %s with %s", id, tmp);
			ret = gs_plugin_refine_by_local_appdata (app, tmp, error);
			if (!ret)
				goto out;
		}
	}
out:
	return ret;
}
