/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016-2018 Richard Hughes <richard@hughsie.com>
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

#include <glib.h>

#include "gs-packagekit-helper.h"
#include "packagekit-common.h"

struct _GsPackagekitHelper {
	GObject			 parent_instance;
	GHashTable		*apps;
	GsPlugin		*plugin;
};

G_DEFINE_TYPE (GsPackagekitHelper, gs_packagekit_helper, G_TYPE_OBJECT)

void
gs_packagekit_helper_cb (PkProgress *progress, PkProgressType type, gpointer user_data)
{
	GsPackagekitHelper *self = (GsPackagekitHelper *) user_data;
	GsPlugin *plugin = gs_packagekit_helper_get_plugin (self);
	const gchar *package_id = pk_progress_get_package_id (progress);
	GsApp *app = NULL;

	/* optional */
	if (package_id != NULL)
		app = gs_packagekit_helper_get_app_by_id (self, package_id);

	if (type == PK_PROGRESS_TYPE_STATUS) {
		PkStatusEnum status = pk_progress_get_status (progress);
		GsPluginStatus plugin_status = packagekit_status_enum_to_plugin_status (status);
		if (plugin_status != GS_PLUGIN_STATUS_UNKNOWN)
			gs_plugin_status_update (plugin, app, plugin_status);
	} else if (type == PK_PROGRESS_TYPE_PERCENTAGE) {
		gint percentage = pk_progress_get_percentage (progress);
		if (app != NULL && percentage >= 0 && percentage <= 100)
			gs_app_set_progress (app, (guint) percentage);
	}

	/* Only go from TRUE to FALSE - it doesn't make sense for a package
	 * install to become uncancellable later on */
	if (app != NULL && gs_app_get_allow_cancel (app))
		gs_app_set_allow_cancel (app, pk_progress_get_allow_cancel (progress));
}

void
gs_packagekit_helper_add_app (GsPackagekitHelper *self, GsApp *app)
{
	GPtrArray *source_ids = gs_app_get_source_ids (app);

	g_return_if_fail (GS_IS_PACKAGEKIT_HELPER (self));
	g_return_if_fail (GS_IS_APP (app));

	for (guint i = 0; i < source_ids->len; i++) {
		const gchar *source_id = g_ptr_array_index (source_ids, i);
		g_hash_table_insert (self->apps,
				     g_strdup (source_id),
				     g_object_ref (app));
	}
}

GsPlugin *
gs_packagekit_helper_get_plugin (GsPackagekitHelper *self)
{
	g_return_val_if_fail (GS_IS_PACKAGEKIT_HELPER (self), NULL);
	return self->plugin;
}

GsApp *
gs_packagekit_helper_get_app_by_id (GsPackagekitHelper *self, const gchar *package_id)
{
	g_return_val_if_fail (GS_IS_PACKAGEKIT_HELPER (self), NULL);
	g_return_val_if_fail (package_id != NULL, NULL);
	return g_hash_table_lookup (self->apps, package_id);
}

static void
gs_packagekit_helper_finalize (GObject *object)
{
	GsPackagekitHelper *self;

	g_return_if_fail (GS_IS_PACKAGEKIT_HELPER (object));

	self = GS_PACKAGEKIT_HELPER (object);

	g_object_unref (self->plugin);
	g_hash_table_unref (self->apps);

	G_OBJECT_CLASS (gs_packagekit_helper_parent_class)->finalize (object);
}

static void
gs_packagekit_helper_class_init (GsPackagekitHelperClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_packagekit_helper_finalize;
}

static void
gs_packagekit_helper_init (GsPackagekitHelper *self)
{
	self->apps = g_hash_table_new_full (g_str_hash, g_str_equal,
					      g_free, (GDestroyNotify) g_object_unref);
}

GsPackagekitHelper *
gs_packagekit_helper_new (GsPlugin *plugin)
{
	GsPackagekitHelper *self;
	self = g_object_new (GS_TYPE_PACKAGEKIT_HELPER, NULL);
	self->plugin = g_object_ref (plugin);
	return GS_PACKAGEKIT_HELPER (self);
}
