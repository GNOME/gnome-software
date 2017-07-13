/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
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

#include <string.h>

#include "gs-flatpak-app.h"

struct _GsFlatpakApp
{
	GsApp			 parent_instance;
	FlatpakRefKind		 ref_kind;
	gchar			*ref_arch;
	gchar			*ref_branch;
	gchar			*ref_display;
	gchar			*ref_name;
	gchar			*commit;
	gchar			*object_id;
	gchar			*repo_gpgkey;
	gchar			*repo_url;
	GsFlatpakAppFileKind	 file_kind;
};

G_DEFINE_TYPE (GsFlatpakApp, gs_flatpak_app, GS_TYPE_APP)

static gboolean
_g_set_str (gchar **str_ptr, const gchar *new_str)
{
	if (*str_ptr == new_str || g_strcmp0 (*str_ptr, new_str) == 0)
		return FALSE;
	g_free (*str_ptr);
	*str_ptr = g_strdup (new_str);
	return TRUE;
}

static const gchar *
gs_flatpak_app_file_kind_to_string (GsFlatpakAppFileKind file_kind)
{
	if (file_kind == GS_FLATPAK_APP_FILE_KIND_REPO)
		return "flatpakrepo";
	if (file_kind == GS_FLATPAK_APP_FILE_KIND_REF)
		return "flatpakref";
	if (file_kind == GS_FLATPAK_APP_FILE_KIND_BUNDLE)
		return "flatpak";
	return NULL;
}

static void
gs_flatpak_app_to_string (GsApp *app, GString *str)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	gs_utils_append_key_value (str, 20, "flatpak::ref-kind",
				   gs_flatpak_app_get_ref_kind_as_str (app));
	if (flatpak_app->ref_name != NULL) {
		gs_utils_append_key_value (str, 20, "flatpak::ref-name",
					   flatpak_app->ref_name);
	}
	if (flatpak_app->ref_arch != NULL) {
		gs_utils_append_key_value (str, 20, "flatpak::ref-arch",
					   flatpak_app->ref_arch);
	}
	if (flatpak_app->ref_branch != NULL) {
		gs_utils_append_key_value (str, 20, "flatpak::ref-branch",
					   flatpak_app->ref_branch);
	}
	if (flatpak_app->ref_display != NULL) {
		gs_utils_append_key_value (str, 20, "flatpak::ref-display",
					   flatpak_app->ref_display);
	}
	if (flatpak_app->commit != NULL)
		gs_utils_append_key_value (str, 20, "flatpak::commit",
				   flatpak_app->commit);
	if (flatpak_app->object_id != NULL) {
		gs_utils_append_key_value (str, 20, "flatpak::object-id",
					   flatpak_app->object_id);
	}
	if (flatpak_app->repo_gpgkey != NULL) {
		gs_utils_append_key_value (str, 20, "flatpak::repo-gpgkey",
					   flatpak_app->repo_gpgkey);
	}
	if (flatpak_app->repo_url != NULL) {
		gs_utils_append_key_value (str, 20, "flatpak::repo-url",
					   flatpak_app->repo_url);
	}
	if (flatpak_app->file_kind != GS_FLATPAK_APP_FILE_KIND_UNKNOWN) {
		gs_utils_append_key_value (str, 20, "flatpak::file-kind",
					   gs_flatpak_app_file_kind_to_string (flatpak_app->file_kind));
	}
}

const gchar *
gs_flatpak_app_get_ref_name (GsApp *app)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	return flatpak_app->ref_name;
}

const gchar *
gs_flatpak_app_get_ref_arch (GsApp *app)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	return flatpak_app->ref_arch;
}

const gchar *
gs_flatpak_app_get_ref_branch (GsApp *app)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	return flatpak_app->ref_branch;
}

const gchar *
gs_flatpak_app_get_commit (GsApp *app)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	return flatpak_app->commit;
}

GsFlatpakAppFileKind
gs_flatpak_app_get_file_kind (GsApp *app)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	return flatpak_app->file_kind;
}

FlatpakRefKind
gs_flatpak_app_get_ref_kind (GsApp *app)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	return flatpak_app->ref_kind;
}

const gchar *
gs_flatpak_app_get_ref_kind_as_str (GsApp *app)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	if (flatpak_app->ref_kind == FLATPAK_REF_KIND_APP)
		return "app";
	if (flatpak_app->ref_kind == FLATPAK_REF_KIND_RUNTIME)
		return "runtime";
	return NULL;
}

const gchar *
gs_flatpak_app_get_object_id (GsApp *app)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	return flatpak_app->object_id;
}

const gchar *
gs_flatpak_app_get_repo_gpgkey (GsApp *app)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	return flatpak_app->repo_gpgkey;
}

const gchar *
gs_flatpak_app_get_repo_url (GsApp *app)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	return flatpak_app->repo_url;
}

const gchar *
gs_flatpak_app_get_ref_display (GsApp *app)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	return flatpak_app->ref_display;
}

void
gs_flatpak_app_set_ref_name (GsApp *app, const gchar *val)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	_g_set_str (&flatpak_app->ref_name, val);
}

void
gs_flatpak_app_set_ref_arch (GsApp *app, const gchar *val)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	_g_set_str (&flatpak_app->ref_arch, val);
}

void
gs_flatpak_app_set_ref_branch (GsApp *app, const gchar *val)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	_g_set_str (&flatpak_app->ref_branch, val);
}

void
gs_flatpak_app_set_commit (GsApp *app, const gchar *val)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	_g_set_str (&flatpak_app->commit, val);
}

void
gs_flatpak_app_set_file_kind (GsApp *app, GsFlatpakAppFileKind file_kind)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	flatpak_app->file_kind = file_kind;
}

void
gs_flatpak_app_set_ref_kind (GsApp *app, FlatpakRefKind ref_kind)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	flatpak_app->ref_kind = ref_kind;
}

void
gs_flatpak_app_set_object_id (GsApp *app, const gchar *val)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	_g_set_str (&flatpak_app->object_id, val);
}

void
gs_flatpak_app_set_repo_gpgkey (GsApp *app, const gchar *val)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	_g_set_str (&flatpak_app->repo_gpgkey, val);
}

void
gs_flatpak_app_set_repo_url (GsApp *app, const gchar *val)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	_g_set_str (&flatpak_app->repo_url, val);
}

void
gs_flatpak_app_set_ref_display (GsApp *app, const gchar *val)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (app);
	_g_set_str (&flatpak_app->ref_display, val);
}

static void
gs_flatpak_app_finalize (GObject *object)
{
	GsFlatpakApp *flatpak_app = GS_FLATPAK_APP (object);
	g_free (flatpak_app->ref_arch);
	g_free (flatpak_app->ref_branch);
	g_free (flatpak_app->ref_display);
	g_free (flatpak_app->ref_name);
	g_free (flatpak_app->commit);
	g_free (flatpak_app->object_id);
	g_free (flatpak_app->repo_gpgkey);
	g_free (flatpak_app->repo_url);
	G_OBJECT_CLASS (gs_flatpak_app_parent_class)->finalize (object);
}

static void
gs_flatpak_app_class_init (GsFlatpakAppClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GsAppClass *klass_app = GS_APP_CLASS (klass);
	klass_app->to_string = gs_flatpak_app_to_string;
	object_class->finalize = gs_flatpak_app_finalize;
}

static void
gs_flatpak_app_init (GsFlatpakApp *flatpak_app)
{
}

GsApp *
gs_flatpak_app_new (const gchar *id)
{
	return GS_APP (g_object_new (GS_TYPE_FLATPAK_APP, "id", id, NULL));
}

/* vim: set noexpandtab: */
