/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2012-2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

/**
 * SECTION:gs-plugin-vfuncs
 * @title: GsPlugin Exports
 * @include: gnome-software.h
 * @stability: Unstable
 * @short_description: Vfuncs that plugins can implement
 */

#include <appstream.h>
#include <glib-object.h>
#include <gmodule.h>
#include <gio/gio.h>
#include <libsoup/soup.h>

#include "gs-app.h"
#include "gs-app-list.h"
#include "gs-category.h"

G_BEGIN_DECLS

/**
 * gs_plugin_query_type:
 *
 * Returns the #GType for a subclass of #GsPlugin provided by this plugin
 * module. It should not do any other computation.
 *
 * The init function for that type should initialize the plugin. If the plugin
 * should not be run then gs_plugin_set_enabled() should be called from the
 * init function.
 *
 * NOTE: Do not do any failable actions in the plugin class’ init function; use
 * #GsPluginClass.setup_async instead.
 *
 * Since: 42
 */
GType		 gs_plugin_query_type			(void);

/**
 * gs_plugin_adopt_app:
 * @plugin: a #GsPlugin
 * @app: a #GsApp
 *
 * Called when an #GsApp has not been claimed (i.e. a management plugin has not
 * been set).
 *
 * A claimed app means other plugins will not try to perform actions
 * such as install, remove or update. Most apps are claimed when they
 * are created.
 *
 * If a plugin can adopt this app then it should call
 * gs_app_set_management_plugin() on @app.
 **/
void		 gs_plugin_adopt_app			(GsPlugin	*plugin,
							 GsApp		*app);

/**
 * gs_plugin_add_langpacks:
 * @plugin: a #GsPlugin
 * @list: a #GsAppList
 * @locale: a #LANGUAGE_CODE or #LOCALE, e.g. "ja" or "ja_JP"
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Returns a list of language packs, as per input language code or locale.
 *
 * Returns: %TRUE for success or if not relevant
 **/
gboolean	 gs_plugin_add_langpacks		(GsPlugin	*plugin,
							 GsAppList	*list,
							 const gchar    *locale,
							 GCancellable	*cancellable,
							 GError		**error);

G_END_DECLS
