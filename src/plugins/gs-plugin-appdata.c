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
#include "appstream-markup.h"

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
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"appstream",		/* faster than parsing the local file */
		NULL };
	return deps;
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
	GDir *dir;
	GError *error_local = NULL;
	const gchar *tmp;
	gboolean ret = TRUE;
	gchar *ext_tmp;
	gchar *id;

	/* find all the files installed */
	dir = g_dir_open (plugin->priv->cachedir, 0, &error_local);
	if (dir == NULL) {
		g_debug ("Could not open AppData directory: %s",
			 error_local->message);
		g_error_free (error_local);
		goto out;
	}
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

typedef struct {
	AppstreamTag		 tag;
	GsApp			*app;
	AppstreamMarkup		*markup;
} AppstreamCacheHelper;

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
	guint i;

	/* description markup */
	if (helper->tag == APPSTREAM_TAG_DESCRIPTION) {
		for (i = 0; attribute_names[i] != NULL; i++) {
			if (g_strcmp0 (attribute_names[i], "xml:lang") == 0) {
				appstream_markup_set_lang (helper->markup,
							   attribute_values[i]);
				break;
			}
		}
		if (g_strcmp0 (element_name, "p") == 0) {
			appstream_markup_set_mode (helper->markup,
						   APPSTREAM_MARKUP_MODE_P_START);
		} else if (g_strcmp0 (element_name, "ul") == 0) {
			appstream_markup_set_mode (helper->markup,
						   APPSTREAM_MARKUP_MODE_UL_START);
		} else if (g_strcmp0 (element_name, "li") == 0) {
			appstream_markup_set_mode (helper->markup,
						   APPSTREAM_MARKUP_MODE_LI_START);
		}
		return;
	}

	helper->tag = appstream_tag_from_string (element_name);
	switch (helper->tag) {
	case APPSTREAM_TAG_DESCRIPTION:
		/* only process the description if it's not already been set;
		 * doing all this string munging is moderately expensive */
		appstream_markup_set_enabled (helper->markup,
					      gs_app_get_description (helper->app) == NULL);
		appstream_markup_set_mode (helper->markup,
					   APPSTREAM_MARKUP_MODE_START);
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
	const gchar *tmp;
	AppstreamCacheHelper *helper = (AppstreamCacheHelper *) user_data;
	if (helper->tag == APPSTREAM_TAG_DESCRIPTION) {
		if (g_strcmp0 (element_name, "p") == 0) {
			appstream_markup_set_mode (helper->markup,
						   APPSTREAM_MARKUP_MODE_P_END);
		} else if (g_strcmp0 (element_name, "ul") == 0) {
			appstream_markup_set_mode (helper->markup,
						   APPSTREAM_MARKUP_MODE_UL_END);
		} else if (g_strcmp0 (element_name, "li") == 0) {
			appstream_markup_set_mode (helper->markup,
						   APPSTREAM_MARKUP_MODE_LI_END);
		} else if (g_strcmp0 (element_name, "description") == 0) {
			appstream_markup_set_mode (helper->markup,
						   APPSTREAM_MARKUP_MODE_END);
			tmp = appstream_markup_get_text (helper->markup);
			if (tmp != NULL)
				gs_app_set_description (helper->app,
							GS_APP_QUALITY_NORMAL,
							tmp);
			helper->tag = APPSTREAM_TAG_APPLICATION;
		}
	} else {
		helper->tag = APPSTREAM_TAG_APPLICATION;
	}
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
	case APPSTREAM_TAG_PROJECT_LICENSE:
	case APPSTREAM_TAG_SCREENSHOTS:
	case APPSTREAM_TAG_UPDATECONTACT:
	case APPSTREAM_TAG_COMPULSORY_FOR_DESKTOP:
		/* ignore */
		break;
	case APPSTREAM_TAG_DESCRIPTION:
		appstream_markup_add_content (helper->markup, text, text_len);
		break;
	case APPSTREAM_TAG_SCREENSHOT:
		gs_app_add_kudo (helper->app, GS_APP_KUDO_HAS_SCREENSHOTS);
		/* FIXME: actually add to API */
		//tmp = appstream_xml_unmunge (text, text_len);
		//gs_app_add_screenshot (helper->app, tmp);
		break;
	case APPSTREAM_TAG_NAME:
		// FIXME: does not get best language
		tmp = appstream_xml_unmunge (text, text_len);
		if (tmp == NULL)
			break;
		g_debug ("AppData: Setting name: %s", tmp);
		gs_app_set_name (helper->app, GS_APP_QUALITY_NORMAL, tmp);
		break;
	case APPSTREAM_TAG_SUMMARY:
		// FIXME: does not get best language
		tmp = appstream_xml_unmunge (text, text_len);
		if (tmp == NULL)
			break;
		g_debug ("AppData: Setting summary: %s", tmp);
		gs_app_set_summary (helper->app, GS_APP_QUALITY_NORMAL, tmp);
		break;
	case APPSTREAM_TAG_URL:
		if (gs_app_get_url (helper->app, GS_APP_URL_KIND_HOMEPAGE) == NULL) {
			tmp = appstream_xml_unmunge (text, text_len);
			if (tmp == NULL)
				break;
			g_debug ("AppData: Setting URL: %s", tmp);
			gs_app_set_url (helper->app, GS_APP_URL_KIND_HOMEPAGE, tmp);
		}
		break;
	case APPSTREAM_TAG_PROJECT_GROUP:
		if (gs_app_get_project_group (helper->app) == NULL) {
			tmp = appstream_xml_unmunge (text, text_len);
			if (tmp == NULL)
				break;
			g_debug ("AppData: Setting project-group: %s", tmp);
			gs_app_set_project_group (helper->app, tmp);
		}
		break;
	default:
		tmp = appstream_xml_unmunge (text, text_len);
		if (tmp == NULL)
			break;
		if (helper->tag != APPSTREAM_TAG_UNKNOWN) {
			g_warning ("AppData: unknown data '%s' is '%s'",
				   appstream_tag_to_string (helper->tag), tmp);
		}
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
	helper->markup = appstream_markup_new ();
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
	if (helper != NULL)
		appstream_markup_free (helper->markup);
	g_free (helper);
	g_free (data);
	return ret;
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList **list,
		  GsPluginRefineFlags flags,
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

	for (l = *list; l != NULL; l = l->next) {
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
