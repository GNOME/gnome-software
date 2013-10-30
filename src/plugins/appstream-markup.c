/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#include "appstream-common.h"
#include "appstream-markup.h"

struct AppstreamMarkup
{
	AppstreamMarkupMode	 mode;
	GString			*string;
	gboolean		 enabled;
	gchar			*lang;
	guint			 locale_value;
};

/**
 * appstream_markup_free:
 */
void
appstream_markup_free (AppstreamMarkup *app)
{
	g_free (app->lang);
	g_string_free (app->string, TRUE);
	g_slice_free (AppstreamMarkup, app);
}

/**
 * appstream_markup_new:
 */
AppstreamMarkup *
appstream_markup_new (void)
{
	AppstreamMarkup *markup;
	markup = g_slice_new0 (AppstreamMarkup);
	markup->enabled = TRUE;
	markup->locale_value = G_MAXUINT;
	markup->string = g_string_new("");
	return markup;
}

/**
 * appstream_markup_process_locale:
 */
static gboolean
appstream_markup_process_locale (AppstreamMarkup *markup)
{
	guint locale_value;

	/* is this worse than the locale we're already showing */
	locale_value = appstream_get_locale_value (markup->lang);
	if (locale_value > markup->locale_value)
		return FALSE;

	/* is this better than the previous locale */
	if (locale_value < markup->locale_value) {
		g_string_set_size (markup->string, 0);
		markup->locale_value = locale_value;
	}
	return TRUE;
}

/**
 * appstream_markup_set_mode:
 */
void
appstream_markup_set_mode (AppstreamMarkup *markup, AppstreamMarkupMode mode)
{
	if (!markup->enabled)
		return;

	/* format markup in the same way as the distro pre-processor */
	switch (mode) {
	case APPSTREAM_MARKUP_MODE_P_START:
		if (appstream_markup_process_locale (markup) &&
		    markup->string->len > 0)
			g_string_append (markup->string, "\n");
		markup->mode = APPSTREAM_MARKUP_MODE_P_CONTENT;
		break;
	case APPSTREAM_MARKUP_MODE_UL_START:
		markup->mode = APPSTREAM_MARKUP_MODE_UL_CONTENT;
		break;
	case APPSTREAM_MARKUP_MODE_LI_START:
		markup->mode = APPSTREAM_MARKUP_MODE_LI_CONTENT;
		break;
	case APPSTREAM_MARKUP_MODE_START:
		markup->locale_value = G_MAXUINT;
		g_string_truncate (markup->string, 0);
		markup->mode = mode;
		break;
	case APPSTREAM_MARKUP_MODE_END:
		/* remove trailing newline if not distro-formatted */
		if (markup->mode != APPSTREAM_MARKUP_MODE_START) {
			g_string_truncate (markup->string,
					   markup->string->len - 1);
		}
		markup->mode = mode;
		break;
	default:
		markup->mode = mode;
		break;
	}
}

/**
 * appstream_text_is_whitespace:
 */
static gboolean
appstream_text_is_whitespace (const gchar *text)
{
	gboolean ret = TRUE;
	guint i;
	for (i = 0; text[i] != '\0'; i++) {
		if (!g_ascii_isspace (text[i])) {
			ret = FALSE;
			break;
		}
	}
	return ret;
}

/**
 * appstream_markup_add_content:
 */
void
appstream_markup_add_content (AppstreamMarkup *markup,
			      const gchar *text,
			      gssize length)
{
	gchar *tmp = NULL;

	if (!markup->enabled)
		return;

	/* lang not good enough */
	if (!appstream_markup_process_locale (markup))
		return;

	switch (markup->mode) {
	case APPSTREAM_MARKUP_MODE_START:
		/* this is for pre-formatted text */
		tmp = appstream_xml_unmunge_safe (text, length);
		if (tmp == NULL)
			break;
		if (!appstream_text_is_whitespace (tmp))
			g_string_append (markup->string, tmp);
		break;
	case APPSTREAM_MARKUP_MODE_P_CONTENT:
		tmp = appstream_xml_unmunge (text, length);
		if (tmp == NULL)
			break;
		g_string_append_printf (markup->string, "%s\n", tmp);
		break;
	case APPSTREAM_MARKUP_MODE_LI_CONTENT:
		tmp = appstream_xml_unmunge (text, length);
		if (tmp == NULL)
			break;
		g_string_append_printf (markup->string, " • %s\n", tmp);
		break;
	default:
		break;
	}
	g_free (tmp);
}

/**
 * appstream_markup_set_lang:
 */
void
appstream_markup_set_lang (AppstreamMarkup *markup, const gchar *lang)
{
	if (!markup->enabled)
		return;
	if (lang == NULL)
		lang = "C";
	if (g_strcmp0 (lang, markup->lang) == 0)
		return;
	g_free (markup->lang);
	markup->lang = g_strdup (lang);
}

/**
 * appstream_markup_get_text:
 */
const gchar *
appstream_markup_get_text (AppstreamMarkup *markup)
{
	if (markup->string->len == 0)
		return NULL;
	return markup->string->str;
}

/**
 * appstream_markup_get_lang:
 */
const gchar *
appstream_markup_get_lang (AppstreamMarkup *markup)
{
	return markup->lang;
}

/**
 * appstream_markup_set_enabled:
 */
void
appstream_markup_set_enabled (AppstreamMarkup *markup, gboolean enabled)
{
	markup->enabled = enabled;
}
