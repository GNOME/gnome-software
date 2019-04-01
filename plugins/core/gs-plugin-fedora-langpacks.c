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
 *
 *  ToDo
 *  4. log install information; not to try again
 */

#include <gnome-software.h>


void
gs_plugin_initialize (GsPlugin *plugin)
{

        /* this plugin should be fedora specific */
        if (!gs_plugin_check_distro_id (plugin, "fedora")) {
                gs_plugin_set_enabled (plugin, FALSE);
                return;
        }

        gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");
}

gboolean
gs_plugin_add_language_packs (GsPlugin *plugin,
                              GsAppList *list,
                              const gchar  *language_code,
                              GCancellable *cancellable,
                              GError **error)
{
        const gchar *identified_lang_id;
        g_autofree gchar *identified_langpack_source = NULL;
        g_autofree gchar *lang_code_from_locale = NULL;

        g_autoptr(GHashTable) locale_langpack_map = g_hash_table_new (g_str_hash, g_str_equal);

        /*
        * A few language code may have more than one language packs.
        * Example: en {en_GB}, pt {pt_BR}, zh {zh_CN, zh_TW}
        */

        g_hash_table_insert (locale_langpack_map, "en_GB", "langpacks-en_GB");
        g_hash_table_insert (locale_langpack_map, "pt_BR", "langpacks-pt_BR");
        g_hash_table_insert (locale_langpack_map, "zh_CN", "langpacks-zh_CN");
        g_hash_table_insert (locale_langpack_map, "zh_TW", "langpacks-zh_TW");

        if (g_strrstr (language_code, "_") != NULL &&
                !g_hash_table_lookup (locale_langpack_map, language_code)) {

                /*
                 * language_code should be the langpack_source_id
                 * if locale is passed in language_code and it doesn't
                 * not found in locale_langpack_map
                 */
                lang_code_from_locale = g_strsplit (language_code, "_", 2)[0];
                identified_lang_id = lang_code_from_locale;

        } else {
                identified_lang_id = language_code;
        }

        identified_langpack_source = g_strconcat ("langpacks-", identified_lang_id, NULL);
        if (identified_langpack_source != NULL) {

                g_autoptr(GsApp) app = gs_app_new (NULL);
                gs_app_set_metadata (app, "GnomeSoftware::Creator", gs_plugin_get_name (plugin));
                gs_app_set_kind (app, AS_APP_KIND_LOCALIZATION);
                gs_app_add_source (app, identified_langpack_source);
                gs_app_list_add (list, app);

        }

        return TRUE;
}
