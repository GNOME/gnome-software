/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Sundeep Anand <suanand@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 *
 * This plugin does following..
 *  1. locates the active locale, say, xx
 *  2. identifies related langpacks-xx
 *  3. tries to add langpack-xx in app list
 *  4. logs install information; not to try again
 */

#include <gnome-software.h>

struct GsPluginData {
	GHashTable	*locale_langpack_map;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

	/* this plugin should be fedora specific */
	if (!gs_plugin_check_distro_id (plugin, "fedora")) {
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}

	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");

	/*
	* A few language code may have more than one language packs.
	* Example: en {en_GB}, pt {pt_BR}, zh {zh_CN, zh_TW}
	*/
	priv->locale_langpack_map = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (priv->locale_langpack_map, "en_GB", "langpacks-en_GB");
	g_hash_table_insert (priv->locale_langpack_map, "pt_BR", "langpacks-pt_BR");
	g_hash_table_insert (priv->locale_langpack_map, "zh_CN", "langpacks-zh_CN");
	g_hash_table_insert (priv->locale_langpack_map, "zh_TW", "langpacks-zh_TW");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	if (priv->locale_langpack_map != NULL)
		g_hash_table_unref (priv->locale_langpack_map);
}

gboolean
gs_plugin_add_langpacks (GsPlugin *plugin,
			 GsAppList *list,
			 const gchar *locale,
			 GCancellable *cancellable,
			 GError **error)
{
	GsPluginData *priv = gs_plugin_get_data (plugin);
	const gchar *language_code;
	g_autofree gchar *cachefn = NULL;
	g_autofree gchar *langpack_pkgname = NULL;
	g_auto(GStrv) language_region = NULL;

	if (g_strrstr (locale, "_") != NULL &&
	    !g_hash_table_lookup (priv->locale_langpack_map, locale)) {
		/*
		 * language_code should be the langpack_source_id
		 * if input language_code is a locale and it doesn't
		 * not found in locale_langpack_map
		 */
		language_region = g_strsplit (locale, "_", 2);
		language_code = language_region[0];
	} else {
		language_code = locale;
	}

	/* per-user cache */
	langpack_pkgname = g_strconcat ("langpacks-", language_code, NULL);
	cachefn = gs_utils_get_cache_filename ("langpacks", langpack_pkgname,
					       GS_UTILS_CACHE_FLAG_WRITEABLE,
					       error);
	if (cachefn == NULL)
		return FALSE;
	if (!g_file_test (cachefn, G_FILE_TEST_EXISTS)) {
		g_autoptr(GsApp) app = gs_app_new (NULL);
		gs_app_set_metadata (app, "GnomeSoftware::Creator", gs_plugin_get_name (plugin));
		gs_app_set_kind (app, AS_APP_KIND_LOCALIZATION);
		gs_app_add_source (app, langpack_pkgname);
		gs_app_list_add (list, app);

		/* ensure we do not keep trying to install the langpack */
		if (!g_file_set_contents (cachefn, language_code, -1, error))
			return FALSE;
	}

	return TRUE;
}
