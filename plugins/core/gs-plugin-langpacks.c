/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2019 Sundeep Anand <suanand@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 * 
 * This plugin does following..
 *  1. locate the active locale, say, xx
 *  2. look for related langpack-xx
 *  3. check for app's state of langpack-xx
 *  4. if not installed,
 *          tries to add langpack-xx in next update set
 *  5. save update information; not to try again
 */

#include <gnome-software.h>
#include "gnome-software-private.h"


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
  const gchar *plugin_name = gs_plugin_get_name(plugin);
  gchar *lang_pack_candidate = NULL;

  GHashTable* locale_langpack_map = g_hash_table_new(g_str_hash, g_str_equal);

  g_autoptr(GsApp) app = gs_app_new (NULL);
  g_autoptr(GsPluginJob) plugin_job = NULL;
  g_autoptr(GsAppList) search_app_list = NULL;
  g_autoptr(GError) error1 = NULL;

  GsPluginLoader	*plugin_loader = NULL;
  gchar *identified_langpack_unique_id = NULL;

  g_debug ("=> msg from langpack plugin; Plugin Name: %s ", plugin_name);
  g_debug ("=> msg from langpack plugin; Input Language Code: %s ", language_code);

  lang_pack_candidate = g_strconcat("LangPack-", language_code, NULL);
	g_debug ("=> msg from langpack plugin; langpack candidate: %s ", lang_pack_candidate);

  plugin_loader = gs_plugin_loader_new ();
  if (plugin_loader != NULL) {
    g_debug ("=> msg from langpack plugin; plugin_loader is instantiated!!");
  }

  /* trying to search appropriate langpack available */
  
  plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH, "search", "langpack", NULL);
  if (plugin_job != NULL) {
    g_debug ("=> msg from langpack plugin; plugin_job is instantiated!!");
  }

  search_app_list = gs_plugin_loader_job_process (plugin_loader, plugin_job, NULL, &error1);
  
  /*g_assert_no_error (error1);
	g_assert (search_app_list != NULL);*/

  if (search_app_list == NULL) {
    g_debug ("=> msg from langpack plugin; search_app_list is NOT instantiated!!");
  } else {
    g_debug ("=> msg from langpack plugin; Search applist length: %u", gs_app_list_length (search_app_list));
  }
  
  /*for (guint i = 0; i < gs_app_list_length (search_app_list); i++) {
    GsApp *app_tmp = gs_app_list_index (search_app_list, i);
    g_debug ("Listing app: %s ", gs_app_to_string(app_tmp));
  }*/

  /* end of block - trying to search appropriate langpack available */

  /*
   * In case we could not search for appropriate langpack,
   * we may fall back to static mapping between locale to langpack_unique_app_id.
   *
   * This is required because a few locales may not have their langpacks.
   *  Moreover, there could be different scripts for a language
   *  and langpack may be available only for a single script.
   *
   * This data can be extracted to an external source.
   */

  g_hash_table_insert(locale_langpack_map, "af_ZA", "system/package/fedora/localization/org.fedoraproject.LangPack-af/*");
  g_hash_table_insert(locale_langpack_map, "am_ET", "system/package/fedora/localization/org.fedoraproject.LangPack-am/*");
  g_hash_table_insert(locale_langpack_map, "ar_EG", "system/package/fedora/localization/org.fedoraproject.LangPack-ar/*");
  g_hash_table_insert(locale_langpack_map, "as_IN", "system/package/fedora/localization/org.fedoraproject.LangPack-as/*");
  g_hash_table_insert(locale_langpack_map, "ast_ES", "system/package/fedora/localization/org.fedoraproject.LangPack-ast/*");
  g_hash_table_insert(locale_langpack_map, "be_BY", "system/package/fedora/localization/org.fedoraproject.LangPack-be/*");
  g_hash_table_insert(locale_langpack_map, "bg_BG", "system/package/fedora/localization/org.fedoraproject.LangPack-bg/*");
  g_hash_table_insert(locale_langpack_map, "bn_BD", "system/package/fedora/localization/org.fedoraproject.LangPack-bn/*");
  g_hash_table_insert(locale_langpack_map, "bn_IN", "system/package/fedora/localization/org.fedoraproject.LangPack-bn/*");
  g_hash_table_insert(locale_langpack_map, "br_FR", "system/package/fedora/localization/org.fedoraproject.LangPack-br/*");
  g_hash_table_insert(locale_langpack_map, "bs_BA", "system/package/fedora/localization/org.fedoraproject.LangPack-bs/*");
  g_hash_table_insert(locale_langpack_map, "ca_ES", "system/package/fedora/localization/org.fedoraproject.LangPack-ca/*");
  g_hash_table_insert(locale_langpack_map, "cs_CZ", "system/package/fedora/localization/org.fedoraproject.LangPack-cs/*");
  g_hash_table_insert(locale_langpack_map, "cy_GB", "system/package/fedora/localization/org.fedoraproject.LangPack-cy/*");
  g_hash_table_insert(locale_langpack_map, "da_DK", "system/package/fedora/localization/org.fedoraproject.LangPack-da/*");
  g_hash_table_insert(locale_langpack_map, "de_DE", "system/package/fedora/localization/org.fedoraproject.LangPack-de/*");
  g_hash_table_insert(locale_langpack_map, "el_GR", "system/package/fedora/localization/org.fedoraproject.LangPack-el/*");
  g_hash_table_insert(locale_langpack_map, "en_US", "system/package/koji-override-0/localization/org.fedoraproject.LangPack-en/*");
  g_hash_table_insert(locale_langpack_map, "en_GB", "system/package/fedora/localization/org.fedoraproject.LangPack-en_GB/*");
  g_hash_table_insert(locale_langpack_map, "es_ES", "system/package/fedora/localization/org.fedoraproject.LangPack-es/*");
  g_hash_table_insert(locale_langpack_map, "et_EE", "system/package/fedora/localization/org.fedoraproject.LangPack-et/*");
  g_hash_table_insert(locale_langpack_map, "eu_ES", "system/package/fedora/localization/org.fedoraproject.LangPack-eu/*");
  g_hash_table_insert(locale_langpack_map, "fa_IR", "system/package/fedora/localization/org.fedoraproject.LangPack-fa/*");
  g_hash_table_insert(locale_langpack_map, "fi_FI", "system/package/fedora/localization/org.fedoraproject.LangPack-fi/*");
  g_hash_table_insert(locale_langpack_map, "fr_FR", "system/package/fedora/localization/org.fedoraproject.LangPack-fr/*");
  g_hash_table_insert(locale_langpack_map, "ga_IE", "system/package/fedora/localization/org.fedoraproject.LangPack-ga/*");
  g_hash_table_insert(locale_langpack_map, "gl_ES", "system/package/fedora/localization/org.fedoraproject.LangPack-gl/*");
  g_hash_table_insert(locale_langpack_map, "gu_IN", "system/package/fedora/localization/org.fedoraproject.LangPack-gu/*");
  g_hash_table_insert(locale_langpack_map, "he_IL", "system/package/fedora/localization/org.fedoraproject.LangPack-he/*");
  g_hash_table_insert(locale_langpack_map, "hi_IN", "system/package/fedora/localization/org.fedoraproject.LangPack-hi/*");
  g_hash_table_insert(locale_langpack_map, "hr_HR", "system/package/fedora/localization/org.fedoraproject.LangPack-hr/*");
  g_hash_table_insert(locale_langpack_map, "hu_HU", "system/package/fedora/localization/org.fedoraproject.LangPack-hu/*");
  g_hash_table_insert(locale_langpack_map, "ia_FR", "system/package/fedora/localization/org.fedoraproject.LangPack-ia/*");
  g_hash_table_insert(locale_langpack_map, "id_ID", "system/package/fedora/localization/org.fedoraproject.LangPack-id/*");
  g_hash_table_insert(locale_langpack_map, "is_IS", "system/package/fedora/localization/org.fedoraproject.LangPack-is/*");
  g_hash_table_insert(locale_langpack_map, "it_IT", "system/package/fedora/localization/org.fedoraproject.LangPack-it/*");
  g_hash_table_insert(locale_langpack_map, "ja_JP", "system/package/fedora/localization/org.fedoraproject.LangPack-ja/*");
  g_hash_table_insert(locale_langpack_map, "kk_KZ", "system/package/fedora/localization/org.fedoraproject.LangPack-kk/*");
  g_hash_table_insert(locale_langpack_map, "kn_IN", "system/package/fedora/localization/org.fedoraproject.LangPack-kn/*");
  g_hash_table_insert(locale_langpack_map, "ko_KR", "system/package/fedora/localization/org.fedoraproject.LangPack-ko/*");
  g_hash_table_insert(locale_langpack_map, "lt_LT", "system/package/fedora/localization/org.fedoraproject.LangPack-lt/*");
  g_hash_table_insert(locale_langpack_map, "lv_LV", "system/package/fedora/localization/org.fedoraproject.LangPack-lv/*");
  g_hash_table_insert(locale_langpack_map, "mai_IN", "system/package/fedora/localization/org.fedoraproject.LangPack-mai/*");
  g_hash_table_insert(locale_langpack_map, "mk_MK", "system/package/fedora/localization/org.fedoraproject.LangPack-mk/*");
  g_hash_table_insert(locale_langpack_map, "ml_IN", "system/package/fedora/localization/org.fedoraproject.LangPack-ml/*");
  g_hash_table_insert(locale_langpack_map, "mr_IN", "system/package/fedora/localization/org.fedoraproject.LangPack-mr/*");
  g_hash_table_insert(locale_langpack_map, "ms_MY", "system/package/fedora/localization/org.fedoraproject.LangPack-ms/*");
  g_hash_table_insert(locale_langpack_map, "nb_NO", "system/package/fedora/localization/org.fedoraproject.LangPack-nb/*");
  g_hash_table_insert(locale_langpack_map, "ne_NP", "system/package/fedora/localization/org.fedoraproject.LangPack-ne/*");
  g_hash_table_insert(locale_langpack_map, "nl_NL", "system/package/fedora/localization/org.fedoraproject.LangPack-nl/*");
  g_hash_table_insert(locale_langpack_map, "nn_NO", "system/package/fedora/localization/org.fedoraproject.LangPack-nn/*");

  g_hash_table_insert(locale_langpack_map, "nr_ZA", "system/package/*/localization/org.fedoraproject.LangPack-nr/*");
  g_hash_table_insert(locale_langpack_map, "nso_ZA", "system/package/*/localization/org.fedoraproject.LangPack-nso/*");
  g_hash_table_insert(locale_langpack_map, "or_IN", "system/package/*/localization/org.fedoraproject.LangPack-or/*");
  g_hash_table_insert(locale_langpack_map, "pa_IN", "system/package/*/localization/org.fedoraproject.LangPack-pa/*");
  g_hash_table_insert(locale_langpack_map, "pl_PL", "system/package/*/localization/org.fedoraproject.LangPack-pl/*");
  g_hash_table_insert(locale_langpack_map, "pt_PT", "system/package/*/localization/org.fedoraproject.LangPack-pt/*");
  g_hash_table_insert(locale_langpack_map, "pt_BR", "system/package/*/localization/org.fedoraproject.LangPack-pt_BR/*");
  g_hash_table_insert(locale_langpack_map, "ro_RO", "system/package/*/localization/org.fedoraproject.LangPack-ro/*");
  g_hash_table_insert(locale_langpack_map, "ru_RU", "system/package/*/localization/org.fedoraproject.LangPack-ru/*");
  g_hash_table_insert(locale_langpack_map, "si_LK", "system/package/*/localization/org.fedoraproject.LangPack-si/*");
  g_hash_table_insert(locale_langpack_map, "sk_SK", "system/package/*/localization/org.fedoraproject.LangPack-sk/*");
  g_hash_table_insert(locale_langpack_map, "sl_SI", "system/package/*/localization/org.fedoraproject.LangPack-sl/*");
  g_hash_table_insert(locale_langpack_map, "sq_AL", "system/package/*/localization/org.fedoraproject.LangPack-sq/*");
  g_hash_table_insert(locale_langpack_map, "sr_RS", "system/package/*/localization/org.fedoraproject.LangPack-sr/*");
  g_hash_table_insert(locale_langpack_map, "ss_ZA", "system/package/*/localization/org.fedoraproject.LangPack-ss/*");
  g_hash_table_insert(locale_langpack_map, "sv_SE", "system/package/*/localization/org.fedoraproject.LangPack-sv/*");
  g_hash_table_insert(locale_langpack_map, "ta_IN", "system/package/*/localization/org.fedoraproject.LangPack-ta/*");
  g_hash_table_insert(locale_langpack_map, "te_IN", "system/package/*/localization/org.fedoraproject.LangPack-te/*");
  g_hash_table_insert(locale_langpack_map, "th_TH", "system/package/*/localization/org.fedoraproject.LangPack-th/*");
  g_hash_table_insert(locale_langpack_map, "tn_ZA", "system/package/*/localization/org.fedoraproject.LangPack-tn/*");
  g_hash_table_insert(locale_langpack_map, "tr_TR", "system/package/*/localization/org.fedoraproject.LangPack-tr/*");
  g_hash_table_insert(locale_langpack_map, "ts_ZA", "system/package/*/localization/org.fedoraproject.LangPack-ts/*");
  g_hash_table_insert(locale_langpack_map, "uk_UA", "system/package/*/localization/org.fedoraproject.LangPack-uk/*");
  g_hash_table_insert(locale_langpack_map, "ur_IN", "system/package/*/localization/org.fedoraproject.LangPack-ur/*");
  g_hash_table_insert(locale_langpack_map, "ur_PK", "system/package/*/localization/org.fedoraproject.LangPack-ur/*");
  g_hash_table_insert(locale_langpack_map, "ve_ZA", "system/package/*/localization/org.fedoraproject.LangPack-ve/*");
  g_hash_table_insert(locale_langpack_map, "vi_VN", "system/package/*/localization/org.fedoraproject.LangPack-vi/*");
  g_hash_table_insert(locale_langpack_map, "xh_ZA", "system/package/*/localization/org.fedoraproject.LangPack-xh/*");
  g_hash_table_insert(locale_langpack_map, "zh_CN", "system/package/*/localization/org.fedoraproject.LangPack-zh_CN/*");
  g_hash_table_insert(locale_langpack_map, "zh_TW", "system/package/*/localization/org.fedoraproject.LangPack-zh_TW/*");
  g_hash_table_insert(locale_langpack_map, "zu_ZA", "system/package/*/localization/org.fedoraproject.LangPack-zu/*");

  /* end of block - mapping data */
  
  identified_langpack_unique_id = g_hash_table_lookup(locale_langpack_map, language_code);
  if (identified_langpack_unique_id != NULL) {
    gs_app_set_from_unique_id (app, identified_langpack_unique_id);

    gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
    gs_app_set_kind(app, AS_APP_KIND_LOCALIZATION);
    gs_app_set_to_be_installed(app, TRUE);
    /* gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Language Pack"); */

    g_debug ("=> msg from langpack plugin; If app ready to be installed? %d", gs_app_get_to_be_installed(app));
    gs_app_list_add(list, app);
  }

  return TRUE;
}
