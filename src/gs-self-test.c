/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gnome-software-private.h"

#include "gs-content-rating.h"
#include "gs-css.h"
#include "gs-test.h"

static void
gs_css_func (void)
{
	const gchar *tmp;
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsCss) css = gs_css_new ();

	/* no IDs */
	ret = gs_css_parse (css, "border: 0;", &error);
	g_assert_no_error (error);
	g_assert (ret);
	tmp = gs_css_get_markup_for_id (css, "tile");
	g_assert_cmpstr (tmp, ==, "border: 0;");

	/* with IDs */
	ret = gs_css_parse (css, "#tile2{\nborder: 0;}\n#name {color: white;\n}", &error);
	g_assert_no_error (error);
	g_assert (ret);
	tmp = gs_css_get_markup_for_id (css, "NotGoingToExist");
	g_assert_cmpstr (tmp, ==, NULL);
	tmp = gs_css_get_markup_for_id (css, "tile2");
	g_assert_cmpstr (tmp, ==, "border: 0;");
	tmp = gs_css_get_markup_for_id (css, "name");
	g_assert_cmpstr (tmp, ==, "color: white;");
}

/* Test that gs_utils_content_rating_system_from_locale() returns the correct
 * rating system for various standard locales and various forms of locale name.
 * See `locale -a` for the list of all available locales which some of these
 * test vectors were derived from. */
static void
gs_content_rating_from_locale (void)
{
	const struct {
		const gchar *locale;
		GsContentRatingSystem expected_system;
	} vectors[] = {
		/* Simple tests to get coverage of each rating system: */
		{ "es_AR", GS_CONTENT_RATING_SYSTEM_INCAA },
		{ "en_AU", GS_CONTENT_RATING_SYSTEM_ACB },
		{ "pt_BR", GS_CONTENT_RATING_SYSTEM_DJCTQ },
		{ "zh_TW", GS_CONTENT_RATING_SYSTEM_GSRR },
		{ "en_GB", GS_CONTENT_RATING_SYSTEM_PEGI },
		{ "hy_AM", GS_CONTENT_RATING_SYSTEM_PEGI },
		{ "bg_BG", GS_CONTENT_RATING_SYSTEM_PEGI },
		{ "fi_FI", GS_CONTENT_RATING_SYSTEM_KAVI },
		{ "de_DE", GS_CONTENT_RATING_SYSTEM_USK },
		{ "az_IR", GS_CONTENT_RATING_SYSTEM_ESRA },
		{ "jp_JP", GS_CONTENT_RATING_SYSTEM_CERO },
		{ "en_NZ", GS_CONTENT_RATING_SYSTEM_OFLCNZ },
		{ "ru_RU", GS_CONTENT_RATING_SYSTEM_RUSSIA },
		{ "en_SQ", GS_CONTENT_RATING_SYSTEM_MDA },
		{ "ko_KR", GS_CONTENT_RATING_SYSTEM_GRAC },
		{ "en_US", GS_CONTENT_RATING_SYSTEM_ESRB },
		{ "en_US", GS_CONTENT_RATING_SYSTEM_ESRB },
		{ "en_CA", GS_CONTENT_RATING_SYSTEM_ESRB },
		{ "es_MX", GS_CONTENT_RATING_SYSTEM_ESRB },
		/* Fallback (arbitrarily chosen Venezuela since it seems to use IARC): */
		{ "es_VE", GS_CONTENT_RATING_SYSTEM_IARC },
		/* Locale with a codeset: */
		{ "nl_NL.iso88591", GS_CONTENT_RATING_SYSTEM_PEGI },
		/* Locale with a codeset and modifier: */
		{ "nl_NL.iso885915@euro", GS_CONTENT_RATING_SYSTEM_PEGI },
		/* Locale with a less esoteric codeset: */
		{ "en_GB.UTF-8", GS_CONTENT_RATING_SYSTEM_PEGI },
		/* Locale with a modifier but no codeset: */
		{ "fi_FI@euro", GS_CONTENT_RATING_SYSTEM_KAVI },
		/* Invalid locale: */
		{ "_invalid", GS_CONTENT_RATING_SYSTEM_IARC },
	};

	for (gsize i = 0; i < G_N_ELEMENTS (vectors); i++) {
		g_test_message ("Test %" G_GSIZE_FORMAT ": %s", i, vectors[i].locale);
		g_assert_cmpint (gs_utils_content_rating_system_from_locale (vectors[i].locale), ==, vectors[i].expected_system);
	}
}

int
main (int argc, char **argv)
{
	g_test_init (&argc, &argv,
#if GLIB_CHECK_VERSION(2, 60, 0)
		     G_TEST_OPTION_ISOLATE_DIRS,
#endif
		     NULL);
	g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* only critical and error are fatal */
	g_log_set_fatal_mask (NULL, G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL);

	/* tests go here */
	g_test_add_func ("/gnome-software/src/css", gs_css_func);
	g_test_add_func ("/gnome-software/src/content-rating/from-locale",
			 gs_content_rating_from_locale);

	return g_test_run ();
}
