/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <string.h>

#include "gs-flatpak-app.h"

struct _GsFlatpakApp
{
	GsApp			 parent_instance;
	FlatpakRefKind		 ref_kind;
	gchar			*ref_arch;
	gchar			*ref_name;
	gchar			*commit;
	gchar			*object_id;
	gchar			*repo_gpgkey;
	gchar			*repo_url;
	gchar			*runtime_url;
	gchar			*main_app_ref;
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
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	gs_utils_append_key_value (str, 20, "flatpak::ref-kind",
				   gs_flatpak_app_get_ref_kind_as_str (app));
	if (self->ref_name != NULL) {
		gs_utils_append_key_value (str, 20, "flatpak::ref-name",
					   self->ref_name);
	}
	if (self->ref_arch != NULL) {
		gs_utils_append_key_value (str, 20, "flatpak::ref-arch",
					   self->ref_arch);
	}
	if (self->commit != NULL)
		gs_utils_append_key_value (str, 20, "flatpak::commit",
				   self->commit);
	if (self->object_id != NULL) {
		gs_utils_append_key_value (str, 20, "flatpak::object-id",
					   self->object_id);
	}
	if (self->repo_gpgkey != NULL) {
		gs_utils_append_key_value (str, 20, "flatpak::repo-gpgkey",
					   self->repo_gpgkey);
	}
	if (self->repo_url != NULL) {
		gs_utils_append_key_value (str, 20, "flatpak::repo-url",
					   self->repo_url);
	}
	if (self->runtime_url != NULL) {
		gs_utils_append_key_value (str, 20, "flatpak::runtime-url",
					   self->runtime_url);
	}
	if (self->main_app_ref != NULL) {
		gs_utils_append_key_value (str, 20, "flatpak::main-app-ref",
					   self->main_app_ref);
	}
	if (self->file_kind != GS_FLATPAK_APP_FILE_KIND_UNKNOWN) {
		gs_utils_append_key_value (str, 20, "flatpak::file-kind",
					   gs_flatpak_app_file_kind_to_string (self->file_kind));
	}
}

const gchar *
gs_flatpak_app_get_ref_name (GsApp *app)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	return self->ref_name;
}

const gchar *
gs_flatpak_app_get_ref_arch (GsApp *app)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	return self->ref_arch;
}

const gchar *
gs_flatpak_app_get_commit (GsApp *app)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	return self->commit;
}

GsFlatpakAppFileKind
gs_flatpak_app_get_file_kind (GsApp *app)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	return self->file_kind;
}

const gchar *
gs_flatpak_app_get_runtime_url (GsApp *app)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	return self->runtime_url;
}

FlatpakRefKind
gs_flatpak_app_get_ref_kind (GsApp *app)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	return self->ref_kind;
}

const gchar *
gs_flatpak_app_get_ref_kind_as_str (GsApp *app)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	if (self->ref_kind == FLATPAK_REF_KIND_APP)
		return "app";
	if (self->ref_kind == FLATPAK_REF_KIND_RUNTIME)
		return "runtime";
	return NULL;
}

const gchar *
gs_flatpak_app_get_object_id (GsApp *app)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	return self->object_id;
}

const gchar *
gs_flatpak_app_get_repo_gpgkey (GsApp *app)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	return self->repo_gpgkey;
}

const gchar *
gs_flatpak_app_get_repo_url (GsApp *app)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	return self->repo_url;
}

const gchar *
gs_flatpak_app_get_main_app_ref_name (GsApp *app)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	return self->main_app_ref;
}

gchar *
gs_flatpak_app_get_ref_display (GsApp *app)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	return g_strdup_printf ("%s/%s/%s/%s",
				gs_flatpak_app_get_ref_kind_as_str (app),
				self->ref_name,
				self->ref_arch,
				gs_app_get_branch (app));
}

void
gs_flatpak_app_set_ref_name (GsApp *app, const gchar *val)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	_g_set_str (&self->ref_name, val);
}

void
gs_flatpak_app_set_ref_arch (GsApp *app, const gchar *val)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	_g_set_str (&self->ref_arch, val);
}

void
gs_flatpak_app_set_commit (GsApp *app, const gchar *val)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	_g_set_str (&self->commit, val);
}

void
gs_flatpak_app_set_file_kind (GsApp *app, GsFlatpakAppFileKind file_kind)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	self->file_kind = file_kind;
}

void
gs_flatpak_app_set_runtime_url (GsApp *app, const gchar *val)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	_g_set_str (&self->runtime_url, val);
}

void
gs_flatpak_app_set_ref_kind (GsApp *app, FlatpakRefKind ref_kind)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	self->ref_kind = ref_kind;
}

void
gs_flatpak_app_set_object_id (GsApp *app, const gchar *val)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	_g_set_str (&self->object_id, val);
}

void
gs_flatpak_app_set_repo_gpgkey (GsApp *app, const gchar *val)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	_g_set_str (&self->repo_gpgkey, val);
}

void
gs_flatpak_app_set_repo_url (GsApp *app, const gchar *val)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	_g_set_str (&self->repo_url, val);
}

void
gs_flatpak_app_set_main_app_ref_name (GsApp *app, const gchar *val)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (app);
	_g_set_str (&self->main_app_ref, val);
}

static void
gs_flatpak_app_finalize (GObject *object)
{
	GsFlatpakApp *self = GS_FLATPAK_APP (object);
	g_free (self->ref_arch);
	g_free (self->ref_name);
	g_free (self->commit);
	g_free (self->object_id);
	g_free (self->runtime_url);
	g_free (self->repo_gpgkey);
	g_free (self->repo_url);
	g_free (self->main_app_ref);
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
gs_flatpak_app_init (GsFlatpakApp *self)
{
}

GsApp *
gs_flatpak_app_new (const gchar *id)
{
	return GS_APP (g_object_new (GS_TYPE_FLATPAK_APP, "id", id, NULL));
}
