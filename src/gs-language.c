/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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
#include <glib.h>

#include "gs-language.h"

#define GS_LANGUAGE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GS_TYPE_LANGUAGE, GsLanguagePrivate))

struct GsLanguagePrivate
{
	GHashTable		*hash;
};

G_DEFINE_TYPE (GsLanguage, gs_language, G_TYPE_OBJECT)

/**
 * gs_language_parser_start_element:
 **/
static void
gs_language_parser_start_element (GMarkupParseContext *context, const gchar *element_name,
                                  const gchar **attribute_names, const gchar **attribute_values,
                                  gpointer user_data, GError **error)
{
	guint i, len;
	const gchar *code1 = NULL;
	const gchar *code2b = NULL;
	const gchar *name = NULL;
	GsLanguage *language = user_data;

	if (strcmp (element_name, "iso_639_entry") != 0)
		return;

	/* find data */
	len = g_strv_length ((gchar**)attribute_names);
	for (i=0; i<len; i++) {
		if (strcmp (attribute_names[i], "iso_639_1_code") == 0)
			code1 = attribute_values[i];
		if (strcmp (attribute_names[i], "iso_639_2B_code") == 0)
			code2b = attribute_values[i];
		if (strcmp (attribute_names[i], "name") == 0)
			name = attribute_values[i];
	}

	/* not valid entry */
	if (name == NULL)
		return;

	/* add both to hash */
	if (code1 != NULL)
		g_hash_table_insert (language->priv->hash, g_strdup (code1), g_strdup (name));
	if (code2b != NULL)
		g_hash_table_insert (language->priv->hash, g_strdup (code2b), g_strdup (name));
}

/* trivial parser */
static const GMarkupParser gs_language_markup_parser =
{
	gs_language_parser_start_element,
	NULL, /* end_element */
	NULL, /* characters */
	NULL, /* passthrough */
	NULL /* error */
};

/**
 * gs_language_populate:
 *
 * <iso_639_entry iso_639_2B_code="hun" iso_639_2T_code="hun" iso_639_1_code="hu" name="Hungarian" />
 **/
gboolean
gs_language_populate (GsLanguage *language, GError **error)
{
	gboolean ret = FALSE;
	gchar *contents = NULL;
	gchar *filename;
	gsize size;
	GMarkupParseContext *context = NULL;

	/* find filename */
	filename = g_build_filename (DATADIR, "xml", "iso-codes", "iso_639.xml", NULL);
	if (!g_file_test (filename, G_FILE_TEST_EXISTS)) {
		g_free (filename);
		filename = g_build_filename ("/usr", "share", "xml", "iso-codes", "iso_639.xml", NULL);
	}
	if (!g_file_test (filename, G_FILE_TEST_EXISTS)) {
		g_set_error (error, 1, 0, "cannot find source file : '%s'", filename);
		goto out;
	}

	/* get contents */
	ret = g_file_get_contents (filename, &contents, &size, error);
	if (!ret)
		goto out;

	/* create parser */
	context = g_markup_parse_context_new (&gs_language_markup_parser, G_MARKUP_PREFIX_ERROR_POSITION, language, NULL);

	/* parse data */
	ret = g_markup_parse_context_parse (context, contents, (gssize) size, error);
	if (!ret)
		goto out;
out:
	if (context != NULL)
		g_markup_parse_context_free (context);
	g_free (filename);
	g_free (contents);
	return ret;
}

/**
 * gs_language_iso639_to_language:
 **/
gchar *
gs_language_iso639_to_language (GsLanguage *language, const gchar *iso639)
{
	return g_strdup (g_hash_table_lookup (language->priv->hash, iso639));
}

/**
 * gs_language_finalize:
 * @object: The object to finalize
 **/
static void
gs_language_finalize (GObject *object)
{
	GsLanguage *language;

	g_return_if_fail (GS_IS_LANGUAGE (object));

	language = GS_LANGUAGE (object);

	g_return_if_fail (language->priv != NULL);
	g_hash_table_unref (language->priv->hash);

	G_OBJECT_CLASS (gs_language_parent_class)->finalize (object);
}

/**
 * gs_language_init:
 * @language: This class instance
 **/
static void
gs_language_init (GsLanguage *language)
{
	language->priv = GS_LANGUAGE_GET_PRIVATE (language);
	language->priv->hash = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, (GDestroyNotify) g_free);
}

/**
 * gs_language_class_init:
 * @klass: The GsLanguageClass
 **/
static void
gs_language_class_init (GsLanguageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_language_finalize;
	g_type_class_add_private (klass, sizeof (GsLanguagePrivate));
}

/**
 * gs_language_new:
 *
 * Return value: a new GsLanguage object.
 **/
GsLanguage *
gs_language_new (void)
{
	GsLanguage *language;
	language = g_object_new (GS_TYPE_LANGUAGE, NULL);
	return GS_LANGUAGE (language);
}
