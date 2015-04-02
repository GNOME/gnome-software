/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2011-2014 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#include <glib/gi18n.h>

#include "gs-cleanup.h"
#include <gs-plugin.h>
#include <gs-category.h>

#include "gs-moduleset.h"

struct GsPluginPrivate {
	GsModuleset		*moduleset;
	gsize			 done_init;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "moduleset";
}

/**
 * gs_plugin_get_deps:
 */
const gchar **
gs_plugin_get_deps (GsPlugin *plugin)
{
	static const gchar *deps[] = {
		"menu-spec-categories",	/* featured subcat added to existing categories */
		"packagekit",		/* pkgname */
		NULL };
	return deps;
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);
	plugin->priv->moduleset = gs_moduleset_new ();
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	g_object_unref (plugin->priv->moduleset);
}

/**
 * gs_plugin_startup:
 */
static gboolean
gs_plugin_startup (GsPlugin *plugin, GError **error)
{
	gboolean ret;

	/* Parse the XML */
	gs_profile_start (plugin->profile, "moduleset::startup");
	ret = gs_moduleset_parse_path (plugin->priv->moduleset,
				       GS_MODULESETDIR,
				       error);
	gs_profile_stop (plugin->profile, "moduleset::startup");
	return ret;
}

gboolean
gs_plugin_add_categories (GsPlugin *plugin,
                          GList **list,
                          GCancellable *cancellable,
                          GError **error)
{
	GList *l;
	GsCategory *parent;
	const gchar *id;
	guint i;
	_cleanup_strv_free_ gchar **categories = NULL;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		gboolean ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			return FALSE;
	}

	categories = gs_moduleset_get_featured_categories (plugin->priv->moduleset);
	if (categories == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "No moduleset data found");
		return FALSE;
	}

	for (i = 0; categories[i]; i++) {
		for (l = *list; l; l = l->next) {
			parent = l->data;
			id = gs_category_get_id (parent);
			if (g_strcmp0 (categories[i], id) == 0) {
				guint size;
				_cleanup_object_unref_ GsCategory *cat = NULL;

				cat = gs_category_new (parent, "featured", _("Featured"));
				gs_category_add_subcategory (parent, cat);
				size = gs_moduleset_get_n_featured (plugin->priv->moduleset, id);
				gs_category_set_size (cat, size);
				break;
			}
		}
	}

	return TRUE;
}

gboolean
gs_plugin_add_category_apps (GsPlugin *plugin,
			     GsCategory *category,
			     GList **list,
			     GCancellable *cancellable,
			     GError **error)
{
	GsCategory *parent;
	guint i;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		gboolean ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			return FALSE;
	}

	/* Populate the "featured" subcategory */
	if (g_strcmp0 (gs_category_get_id (category), "featured") == 0) {
		_cleanup_strv_free_ gchar **apps = NULL;

		parent = gs_category_get_parent (category);
		if (parent != NULL) {
			apps = gs_moduleset_get_featured_apps (plugin->priv->moduleset,
			                                       gs_category_get_id (parent));
		}
		if (apps == NULL) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_FAILED,
				     "No moduleset data found");
			return FALSE;
		}

		/* just add all */
		for (i = 0; apps[i]; i++) {
			_cleanup_object_unref_ GsApp *app = NULL;
			app = gs_app_new (apps[i]);
			gs_plugin_add_app (list, app);
		}
	}

	return TRUE;
}

/**
 * gs_plugin_add_popular:
 */
gboolean
gs_plugin_add_popular (GsPlugin *plugin,
		       GList **list,
		       GCancellable *cancellable,
		       GError **error)
{
	gboolean ret = TRUE;
	guint i;
	_cleanup_strv_free_ gchar **apps = NULL;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			return FALSE;
	}

	if (g_getenv ("GNOME_SOFTWARE_POPULAR")) {
		apps = g_strsplit (g_getenv ("GNOME_SOFTWARE_POPULAR"), ",", 0);
	} else {
		apps = gs_moduleset_get_popular_apps (plugin->priv->moduleset);
	}
	if (apps == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "No moduleset data found");
		return FALSE;
	}

	/* just add all */
	for (i = 0; apps[i]; i++) {
		_cleanup_object_unref_ GsApp *app = NULL;
		app = gs_app_new (apps[i]);
		gs_plugin_add_app (list, app);
	}
	return TRUE;
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
	gboolean ret = TRUE;
	guint i;
	_cleanup_strv_free_ gchar **featured_apps = NULL;
	_cleanup_strv_free_ gchar **popular_apps = NULL;
	_cleanup_strv_free_ gchar **system_apps = NULL;
	_cleanup_strv_free_ gchar **core_pkgs = NULL;

	/* load XML files */
	if (g_once_init_enter (&plugin->priv->done_init)) {
		ret = gs_plugin_startup (plugin, error);
		g_once_init_leave (&plugin->priv->done_init, TRUE);
		if (!ret)
			return FALSE;
	}

	featured_apps = gs_moduleset_get_featured_apps (plugin->priv->moduleset, NULL);
	popular_apps = gs_moduleset_get_popular_apps (plugin->priv->moduleset);
	system_apps = gs_moduleset_get_system_apps (plugin->priv->moduleset);
	core_pkgs = gs_moduleset_get_core_packages (plugin->priv->moduleset);
	if (featured_apps == NULL ||
	    popular_apps == NULL ||
	    system_apps == NULL ||
	    core_pkgs == NULL) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "No moduleset data found");
		return FALSE;
	}

	for (l = *list; l != NULL; l = l->next) {
		app = GS_APP (l->data);

		/* add a kudo to featured apps */
		for (i = 0; featured_apps[i] != NULL; i++) {
			if (g_strcmp0 (featured_apps[i], gs_app_get_id (app)) == 0) {
				gs_app_add_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED);
				break;
			}
		}

		/* add a kudo to popular apps */
		for (i = 0; popular_apps[i] != NULL; i++) {
			if (g_strcmp0 (popular_apps[i], gs_app_get_id (app)) == 0) {
				gs_app_add_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED);
				break;
			}
		}

		/* mark each one as system */
		for (i = 0; system_apps[i] != NULL; i++) {
			if (g_strcmp0 (system_apps[i], gs_app_get_id (app)) == 0) {
				gs_app_set_kind (app, GS_APP_KIND_SYSTEM);
				break;
			}
		}

		/* mark each one as core */
		for (i = 0; core_pkgs[i] != NULL; i++) {
			if (g_strcmp0 (core_pkgs[i], gs_app_get_source_default (app)) == 0) {
				gs_app_set_kind (app, GS_APP_KIND_CORE);
				break;
			}
		}
	}

	return TRUE;
}

/* vim: set noexpandtab: */
