/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2019 Sundeep Anand <suanand@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This plugin does following..
 *  1. locates the active locale, say, xx
 *  2. identifies related langpacks-xx
 *  3. tries to add langpack-xx in app list
 *  4. logs install information; not to try again
 */

#include <gnome-software.h>

#include "gs-plugin-fedora-langpacks.h"

struct _GsPluginFedoraLangpacks {
	GsPlugin	 parent;

	GHashTable	*locale_langpack_map;
};

G_DEFINE_TYPE (GsPluginFedoraLangpacks, gs_plugin_fedora_langpacks, GS_TYPE_PLUGIN)

static void
gs_plugin_fedora_langpacks_init (GsPluginFedoraLangpacks *self)
{
	GsPlugin *plugin = GS_PLUGIN (self);

	/* this plugin should be fedora specific */
	if (!gs_plugin_check_distro_id (plugin, "fedora")) {
		gs_plugin_set_enabled (plugin, FALSE);
		return;
	}

	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "appstream");

	/*
	* A few language code may have more than one language packs.
	* Example: en {en_GB}, pt {pt_BR}, zh {zh_CN, zh_TW, zh_HK}
	*/
	self->locale_langpack_map = g_hash_table_new (g_str_hash, g_str_equal);
	g_hash_table_insert (self->locale_langpack_map, (gpointer) "en_GB", (gpointer) "langpacks-en_GB");
	g_hash_table_insert (self->locale_langpack_map, (gpointer) "pt_BR", (gpointer) "langpacks-pt_BR");
	g_hash_table_insert (self->locale_langpack_map, (gpointer) "zh_CN", (gpointer) "langpacks-zh_CN");
	g_hash_table_insert (self->locale_langpack_map, (gpointer) "zh_TW", (gpointer) "langpacks-zh_TW");
	g_hash_table_insert (self->locale_langpack_map, (gpointer) "zh_HK", (gpointer) "langpacks-zh_HK");
}

static void
gs_plugin_fedora_langpacks_dispose (GObject *object)
{
	GsPluginFedoraLangpacks *self = GS_PLUGIN_FEDORA_LANGPACKS (object);

	g_clear_pointer (&self->locale_langpack_map, g_hash_table_unref);

	G_OBJECT_CLASS (gs_plugin_fedora_langpacks_parent_class)->dispose (object);
}

static void
gs_plugin_fedora_langpacks_get_langpacks_async (GsPlugin *plugin,
						const gchar *locale,
						GsPluginGetLangpacksFlags flags,
						GCancellable *cancellable,
						GAsyncReadyCallback callback,
						gpointer user_data)
{
	GsPluginFedoraLangpacks *self = GS_PLUGIN_FEDORA_LANGPACKS (plugin);
	gchar *separator;
	const gchar *language_code;
	g_autofree gchar *cachefn = NULL;
	g_autofree gchar *langpack_pkgname = NULL;
	g_auto(GStrv) language_region = NULL;
	g_autoptr(GTask) task = NULL;
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GError) local_error = NULL;

	task = gs_plugin_get_langpacks_data_new_task (self, locale, flags, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_fedora_langpacks_get_langpacks_async);

	/* This plugin may receive user locale in the form as documented in `man 3 setlocale`:
	 *
	 * language[_territory][.codeset][@modifier]
	 *
	 * e.g. `ja_JP.UTF-8` or `en_GB.iso88591` or `uz_UZ.utf8@cyrillic` or `de_DE@euro`
	 * Get the locale without codeset and modifier as required for langpacks.
	 */
	separator = strpbrk (locale, ".@");
	if (separator != NULL)
		*separator = '\0';

	if (g_strrstr (locale, "_") != NULL &&
	    !g_hash_table_lookup (self->locale_langpack_map, locale)) {
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
					       GS_UTILS_CACHE_FLAG_WRITEABLE |
					       GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
					       &local_error);
	if (cachefn == NULL) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}
	if (!g_file_test (cachefn, G_FILE_TEST_EXISTS)) {
		/* ensure we do not keep trying to install the langpack */
		if (!g_file_set_contents (cachefn, language_code, -1, &local_error)) {
			g_task_return_error (task, g_steal_pointer (&local_error));
			return;
		}
	}

	list = gs_app_list_new ();
	app = gs_app_new (NULL);
	gs_app_set_metadata (app, "GnomeSoftware::Creator", gs_plugin_get_name (plugin));
	gs_app_set_kind (app, AS_COMPONENT_KIND_LOCALIZATION);
	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
	gs_app_add_source (app, langpack_pkgname);
	gs_app_list_add (list, app);

	g_task_return_pointer (task, g_steal_pointer (&list), g_object_unref);
}

static GsAppList *
gs_plugin_fedora_langpacks_get_langpacks_finish (GsPlugin *plugin,
						 GAsyncResult *result,
						 GError **error)
{
	return g_task_propagate_pointer (G_TASK (result), error);
}

static void
gs_plugin_fedora_langpacks_class_init (GsPluginFedoraLangpacksClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsPluginClass *plugin_class = GS_PLUGIN_CLASS (klass);

	object_class->dispose = gs_plugin_fedora_langpacks_dispose;

	plugin_class->get_langpacks_async = gs_plugin_fedora_langpacks_get_langpacks_async;
	plugin_class->get_langpacks_finish = gs_plugin_fedora_langpacks_get_langpacks_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_FEDORA_LANGPACKS;
}
