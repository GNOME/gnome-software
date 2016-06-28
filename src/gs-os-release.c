/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 */

/**
 * SECTION:gs-os-release
 * @title: GsOsRelease
 * @include: gnome-software.h
 * @stability: Unstable
 * @short_description: Data from os-release
 *
 * This object allows plugins to parse /etc/os-release for distribution
 * metadata information.
 */

#include "config.h"

#include <glib.h>

#include "gs-os-release.h"

struct _GsOsRelease
{
	GObject			 parent_instance;
	gchar			*name;
	gchar			*version;
	gchar			*id;
	gchar			*version_id;
	gchar			*pretty_name;
};

static void gs_os_release_initable_iface_init (GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (GsOsRelease, gs_os_release, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE(G_TYPE_INITABLE, gs_os_release_initable_iface_init))

static gboolean
gs_os_release_initable_init (GInitable *initable,
			     GCancellable *cancellable,
			     GError **error)
{
	GsOsRelease *os_release = GS_OS_RELEASE (initable);
	const gchar *filename;
	g_autofree gchar *data = NULL;
	g_auto(GStrv) lines = NULL;
	guint i;

	g_return_val_if_fail (GS_IS_OS_RELEASE (os_release), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* get contents */
	filename = g_getenv ("GS_SELF_TEST_OS_RELEASE_FILENAME");
	if (filename == NULL) {
		filename = "/etc/os-release";
		if (!g_file_test (filename, G_FILE_TEST_EXISTS))
			filename = "/usr/lib/os-release";
	}
	if (!g_file_get_contents (filename, &data, NULL, error))
		return FALSE;

	/* parse */
	lines = g_strsplit (data, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		gchar *tmp;

		/* split the line up into two halves */
		tmp = g_strstr_len (lines[i], -1, "=");
		if (tmp == NULL)
			continue;
		*tmp = '\0';
		tmp++;

		/* ignore trailing quote */
		if (tmp[0] == '\"')
			tmp++;

		/* ignore trailing quote */
		g_strdelimit (tmp, "\"", '\0');

		/* match fields we're interested in */
		if (g_strcmp0 (lines[i], "NAME") == 0) {
			os_release->name = g_strdup (tmp);
			continue;
		}
		if (g_strcmp0 (lines[i], "VERSION") == 0) {
			os_release->version = g_strdup (tmp);
			continue;
		}
		if (g_strcmp0 (lines[i], "ID") == 0) {
			os_release->id = g_strdup (tmp);
			continue;
		}
		if (g_strcmp0 (lines[i], "VERSION_ID") == 0) {
			os_release->version_id = g_strdup (tmp);
			continue;
		}
		if (g_strcmp0 (lines[i], "PRETTY_NAME") == 0) {
			os_release->pretty_name = g_strdup (tmp);
			continue;
		}
	}
	return TRUE;
}

/**
 * gs_os_release_get_name:
 * @os_release: A #GsOsRelease
 *
 * Gets the name from the os-release parser.
 *
 * Returns: a string, or %NULL
 **/
const gchar *
gs_os_release_get_name (GsOsRelease *os_release)
{
	g_return_val_if_fail (GS_IS_OS_RELEASE (os_release), NULL);
	return os_release->name;
}

/**
 * gs_os_release_get_version:
 * @os_release: A #GsOsRelease
 *
 * Gets the version from the os-release parser.
 *
 * Returns: a string, or %NULL
 **/
const gchar *
gs_os_release_get_version (GsOsRelease *os_release)
{
	g_return_val_if_fail (GS_IS_OS_RELEASE (os_release), NULL);
	return os_release->version;
}

/**
 * gs_os_release_get_id:
 * @os_release: A #GsOsRelease
 *
 * Gets the ID from the os-release parser.
 *
 * Returns: a string, or %NULL
 **/
const gchar *
gs_os_release_get_id (GsOsRelease *os_release)
{
	g_return_val_if_fail (GS_IS_OS_RELEASE (os_release), NULL);
	return os_release->id;
}

/**
 * gs_os_release_get_version_id:
 * @os_release: A #GsOsRelease
 *
 * Gets the version ID from the os-release parser.
 *
 * Returns: a string, or %NULL
 **/
const gchar *
gs_os_release_get_version_id (GsOsRelease *os_release)
{
	g_return_val_if_fail (GS_IS_OS_RELEASE (os_release), NULL);
	return os_release->version_id;
}

/**
 * gs_os_release_get_pretty_name:
 * @os_release: A #GsOsRelease
 *
 * Gets the pretty name from the os-release parser.
 *
 * Returns: a string, or %NULL
 **/
const gchar *
gs_os_release_get_pretty_name (GsOsRelease *os_release)
{
	g_return_val_if_fail (GS_IS_OS_RELEASE (os_release), NULL);
	return os_release->pretty_name;
}

static void
gs_os_release_finalize (GObject *object)
{
	GsOsRelease *os_release = GS_OS_RELEASE (object);
	g_free (os_release->name);
	g_free (os_release->version);
	g_free (os_release->id);
	g_free (os_release->version_id);
	g_free (os_release->pretty_name);
	G_OBJECT_CLASS (gs_os_release_parent_class)->finalize (object);
}

static void
gs_os_release_class_init (GsOsReleaseClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_os_release_finalize;
}

static void
gs_os_release_initable_iface_init (GInitableIface *iface)
{
	iface->init = gs_os_release_initable_init;
}

static void
gs_os_release_init (GsOsRelease *os_release)
{
}

/**
 * gs_os_release_new:
 * @error: a #GError, or %NULL
 *
 * Creates a new os_release.
 *
 * Returns: (transfer full): A newly allocated #GsOsRelease, or %NULL for error
 **/
GsOsRelease *
gs_os_release_new (GError **error)
{
	GsOsRelease *os_release;
	os_release = g_initable_new (GS_TYPE_OS_RELEASE, NULL, error, NULL);
	return GS_OS_RELEASE (os_release);
}

/* vim: set noexpandtab: */
