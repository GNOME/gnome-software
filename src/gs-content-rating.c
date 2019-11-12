/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <string.h>
#include "gs-content-rating.h"

static const gchar *rating_system_names[] = {
	[GS_CONTENT_RATING_SYSTEM_UNKNOWN] = NULL,
	[GS_CONTENT_RATING_SYSTEM_INCAA] = "INCAA",
	[GS_CONTENT_RATING_SYSTEM_ACB] = "ACB",
	[GS_CONTENT_RATING_SYSTEM_DJCTQ] = "DJCTQ",
	[GS_CONTENT_RATING_SYSTEM_GSRR] = "GSRR",
	[GS_CONTENT_RATING_SYSTEM_PEGI] = "PEGI",
	[GS_CONTENT_RATING_SYSTEM_KAVI] = "KAVI",
	[GS_CONTENT_RATING_SYSTEM_USK] = "USK",
	[GS_CONTENT_RATING_SYSTEM_ESRA] = "ESRA",
	[GS_CONTENT_RATING_SYSTEM_CERO] = "CERO",
	[GS_CONTENT_RATING_SYSTEM_OFLCNZ] = "OFLCNZ",
	[GS_CONTENT_RATING_SYSTEM_RUSSIA] = "RUSSIA",
	[GS_CONTENT_RATING_SYSTEM_MDA] = "MDA",
	[GS_CONTENT_RATING_SYSTEM_GRAC] = "GRAC",
	[GS_CONTENT_RATING_SYSTEM_ESRB] = "ESRB",
	[GS_CONTENT_RATING_SYSTEM_IARC] = "IARC",
};
G_STATIC_ASSERT (G_N_ELEMENTS (rating_system_names) == GS_CONTENT_RATING_SYSTEM_LAST);

const gchar *
gs_content_rating_system_to_str (GsContentRatingSystem system)
{
	if ((gint) system < GS_CONTENT_RATING_SYSTEM_UNKNOWN ||
	    (gint) system >= GS_CONTENT_RATING_SYSTEM_LAST)
		return NULL;

	return rating_system_names[system];
}

/* Table of the human-readable descriptions for each #AsContentRatingValue for
 * each content rating category. @desc_none must be non-%NULL, but the other
 * values may be %NULL if no description is appropriate. In that case, the next
 * non-%NULL description for a lower #AsContentRatingValue will be used. */
static const struct {
	const gchar *id;  /* (not nullable) */
	const gchar *desc_none;  /* (not nullable) */
	const gchar *desc_mild;  /* (nullable) */
	const gchar *desc_moderate;  /* (nullable) */
	const gchar *desc_intense;  /* (nullable) */
} oars_descriptions[] = {
	{
		"violence-cartoon",
		/* TRANSLATORS: content rating description */
		N_("No cartoon violence"),
		/* TRANSLATORS: content rating description */
		N_("Cartoon characters in unsafe situations"),
		/* TRANSLATORS: content rating description */
		N_("Cartoon characters in aggressive conflict"),
		/* TRANSLATORS: content rating description */
		N_("Graphic violence involving cartoon characters"),
	},
	{
		"violence-fantasy",
		/* TRANSLATORS: content rating description */
		N_("No fantasy violence"),
		/* TRANSLATORS: content rating description */
		N_("Characters in unsafe situations easily distinguishable from reality"),
		/* TRANSLATORS: content rating description */
		N_("Characters in aggressive conflict easily distinguishable from reality"),
		/* TRANSLATORS: content rating description */
		N_("Graphic violence easily distinguishable from reality"),
	},
	{
		"violence-realistic",
		/* TRANSLATORS: content rating description */
		N_("No realistic violence"),
		/* TRANSLATORS: content rating description */
		N_("Mildly realistic characters in unsafe situations"),
		/* TRANSLATORS: content rating description */
		N_("Depictions of realistic characters in aggressive conflict"),
		/* TRANSLATORS: content rating description */
		N_("Graphic violence involving realistic characters"),
	},
	{
		"violence-bloodshed",
		/* TRANSLATORS: content rating description */
		N_("No bloodshed"),
		/* TRANSLATORS: content rating description */
		N_("Unrealistic bloodshed"),
		/* TRANSLATORS: content rating description */
		N_("Realistic bloodshed"),
		/* TRANSLATORS: content rating description */
		N_("Depictions of bloodshed and the mutilation of body parts"),
	},
	{
		"violence-sexual",
		/* TRANSLATORS: content rating description */
		N_("No sexual violence"),
		/* TRANSLATORS: content rating description */
		N_("Rape or other violent sexual behavior"),
		NULL,
		NULL,
	},
	{
		"drugs-alcohol",
		/* TRANSLATORS: content rating description */
		N_("No references to alcohol"),
		/* TRANSLATORS: content rating description */
		N_("References to alcoholic beverages"),
		/* TRANSLATORS: content rating description */
		N_("Use of alcoholic beverages"),
		NULL,
	},
	{
		"drugs-narcotics",
		/* TRANSLATORS: content rating description */
		N_("No references to illicit drugs"),
		/* TRANSLATORS: content rating description */
		N_("References to illicit drugs"),
		/* TRANSLATORS: content rating description */
		N_("Use of illicit drugs"),
		NULL,
	},
	{
		"drugs-tobacco",
		/* TRANSLATORS: content rating description */
		N_("No references to tobacco products"),
		/* TRANSLATORS: content rating description */
		N_("References to tobacco products"),
		/* TRANSLATORS: content rating description */
		N_("Use of tobacco products"),
		NULL,
	},
	{
		"sex-nudity",
		/* TRANSLATORS: content rating description */
		N_("No nudity of any sort"),
		/* TRANSLATORS: content rating description */
		N_("Brief artistic nudity"),
		/* TRANSLATORS: content rating description */
		N_("Prolonged nudity"),
		NULL,
	},
	{
		"sex-themes",
		/* TRANSLATORS: content rating description */
		N_("No references to or depictions of sexual nature"),
		/* TRANSLATORS: content rating description */
		N_("Provocative references or depictions"),
		/* TRANSLATORS: content rating description */
		N_("Sexual references or depictions"),
		/* TRANSLATORS: content rating description */
		N_("Graphic sexual behavior"),
	},
	{
		"language-profanity",
		/* TRANSLATORS: content rating description */
		N_("No profanity of any kind"),
		/* TRANSLATORS: content rating description */
		N_("Mild or infrequent use of profanity"),
		/* TRANSLATORS: content rating description */
		N_("Moderate use of profanity"),
		/* TRANSLATORS: content rating description */
		N_("Strong or frequent use of profanity"),
	},
	{
		"language-humor",
		/* TRANSLATORS: content rating description */
		N_("No inappropriate humor"),
		/* TRANSLATORS: content rating description */
		N_("Slapstick humor"),
		/* TRANSLATORS: content rating description */
		N_("Vulgar or bathroom humor"),
		/* TRANSLATORS: content rating description */
		N_("Mature or sexual humor"),
	},
	{
		"language-discrimination",
		/* TRANSLATORS: content rating description */
		N_("No discriminatory language of any kind"),
		/* TRANSLATORS: content rating description */
		N_("Negativity towards a specific group of people"),
		/* TRANSLATORS: content rating description */
		N_("Discrimination designed to cause emotional harm"),
		/* TRANSLATORS: content rating description */
		N_("Explicit discrimination based on gender, sexuality, race or religion"),
	},
	{
		"money-advertising",
		/* TRANSLATORS: content rating description */
		N_("No advertising of any kind"),
		/* TRANSLATORS: content rating description */
		N_("Product placement"),
		/* TRANSLATORS: content rating description */
		N_("Explicit references to specific brands or trademarked products"),
		/* TRANSLATORS: content rating description */
		N_("Users are encouraged to purchase specific real-world items"),
	},
	{
		"money-gambling",
		/* TRANSLATORS: content rating description */
		N_("No gambling of any kind"),
		/* TRANSLATORS: content rating description */
		N_("Gambling on random events using tokens or credits"),
		/* TRANSLATORS: content rating description */
		N_("Gambling using “play” money"),
		/* TRANSLATORS: content rating description */
		N_("Gambling using real money"),
	},
	{
		"money-purchasing",
		/* TRANSLATORS: content rating description */
		N_("No ability to spend money"),
		/* TRANSLATORS: content rating description */
		N_("Users are encouraged to donate real money"),
		NULL,
		/* TRANSLATORS: content rating description */
		N_("Ability to spend real money in-app"),
	},
	{
		"social-chat",
		/* TRANSLATORS: content rating description */
		N_("No way to chat with other users"),
		/* TRANSLATORS: content rating description */
		N_("User-to-user interactions without chat functionality"),
		/* TRANSLATORS: content rating description */
		N_("Moderated chat functionality between users"),
		/* TRANSLATORS: content rating description */
		N_("Uncontrolled chat functionality between users"),
	},
	{
		"social-audio",
		/* TRANSLATORS: content rating description */
		N_("No way to talk with other users"),
		/* TRANSLATORS: content rating description */
		N_("Uncontrolled audio or video chat functionality between users"),
		NULL,
		NULL,
	},
	{
		"social-contacts",
		/* TRANSLATORS: content rating description */
		N_("No sharing of social network usernames or email addresses"),
		/* TRANSLATORS: content rating description */
		N_("Sharing social network usernames or email addresses"),
		NULL,
		NULL,
	},
	{
		"social-info",
		/* TRANSLATORS: content rating description */
		N_("No sharing of user information with third parties"),
		/* TRANSLATORS: content rating description */
		N_("Checking for the latest application version"),
		/* TRANSLATORS: content rating description */
		N_("Sharing diagnostic data that does not let others identify the user"),
		/* TRANSLATORS: content rating description */
		N_("Sharing information that lets others identify the user"),
	},
	{
		"social-location",
		/* TRANSLATORS: content rating description */
		N_("No sharing of physical location with other users"),
		/* TRANSLATORS: content rating description */
		N_("Sharing physical location with other users"),
		NULL,
		NULL,
	},

	/* v1.1 */
	{
		"sex-homosexuality",
		/* TRANSLATORS: content rating description */
		N_("No references to homosexuality"),
		/* TRANSLATORS: content rating description */
		N_("Indirect references to homosexuality"),
		/* TRANSLATORS: content rating description */
		N_("Kissing between people of the same gender"),
		/* TRANSLATORS: content rating description */
		N_("Graphic sexual behavior between people of the same gender"),
	},
	{
		"sex-prostitution",
		/* TRANSLATORS: content rating description */
		N_("No references to prostitution"),
		/* TRANSLATORS: content rating description */
		N_("Indirect references to prostitution"),
		/* TRANSLATORS: content rating description */
		N_("Direct references to prostitution"),
		/* TRANSLATORS: content rating description */
		N_("Graphic depictions of the act of prostitution"),
	},
	{
		"sex-adultery",
		/* TRANSLATORS: content rating description */
		N_("No references to adultery"),
		/* TRANSLATORS: content rating description */
		N_("Indirect references to adultery"),
		/* TRANSLATORS: content rating description */
		N_("Direct references to adultery"),
		/* TRANSLATORS: content rating description */
		N_("Graphic depictions of the act of adultery"),
	},
	{
		"sex-appearance",
		/* TRANSLATORS: content rating description */
		N_("No sexualized characters"),
		NULL,
		/* TRANSLATORS: content rating description */
		N_("Scantily clad human characters"),
		/* TRANSLATORS: content rating description */
		N_("Overtly sexualized human characters"),
	},
	{
		"violence-worship",
		/* TRANSLATORS: content rating description */
		N_("No references to desecration"),
		/* TRANSLATORS: content rating description */
		N_("Depictions of or references to historical desecration"),
		/* TRANSLATORS: content rating description */
		N_("Depictions of modern-day human desecration"),
		/* TRANSLATORS: content rating description */
		N_("Graphic depictions of modern-day desecration"),
	},
	{
		"violence-desecration",
		/* TRANSLATORS: content rating description */
		N_("No visible dead human remains"),
		/* TRANSLATORS: content rating description */
		N_("Visible dead human remains"),
		/* TRANSLATORS: content rating description */
		N_("Dead human remains that are exposed to the elements"),
		/* TRANSLATORS: content rating description */
		N_("Graphic depictions of desecration of human bodies"),
	},
	{
		"violence-slavery",
		/* TRANSLATORS: content rating description */
		N_("No references to slavery"),
		/* TRANSLATORS: content rating description */
		N_("Depictions of or references to historical slavery"),
		/* TRANSLATORS: content rating description */
		N_("Depictions of modern-day slavery"),
		/* TRANSLATORS: content rating description */
		N_("Graphic depictions of modern-day slavery"),
	},
};

const gchar *
gs_content_rating_key_value_to_str (const gchar *id, AsContentRatingValue value)
{
	gsize i;

	if ((gint) value < AS_CONTENT_RATING_VALUE_NONE ||
	    (gint) value > AS_CONTENT_RATING_VALUE_INTENSE)
		return NULL;

	for (i = 0; i < G_N_ELEMENTS (oars_descriptions); i++) {
		if (!g_str_equal (oars_descriptions[i].id, id))
			continue;

		/* Return the most-intense non-NULL string. */
		if (oars_descriptions[i].desc_intense != NULL && value >= AS_CONTENT_RATING_VALUE_INTENSE)
			return _(oars_descriptions[i].desc_intense);
		if (oars_descriptions[i].desc_moderate != NULL && value >= AS_CONTENT_RATING_VALUE_MODERATE)
			return _(oars_descriptions[i].desc_moderate);
		if (oars_descriptions[i].desc_mild != NULL && value >= AS_CONTENT_RATING_VALUE_MILD)
			return _(oars_descriptions[i].desc_mild);
		if (oars_descriptions[i].desc_none != NULL && value >= AS_CONTENT_RATING_VALUE_NONE)
			return _(oars_descriptions[i].desc_none);
		g_assert_not_reached ();
	}

	/* This means the requested @id is missing from @oars_descriptions, so
	 * presumably the OARS spec has been updated but gnome-software hasn’t. */
	g_warn_if_reached ();

	return NULL;
}

static char *
get_esrb_string (gchar *source, gchar *translate)
{
	if (g_strcmp0 (source, translate) == 0)
		return g_strdup (source);
	/* TRANSLATORS: This is the formatting of English and localized name
 	   of the rating e.g. "Adults Only (solo adultos)" */
	return g_strdup_printf (_("%s (%s)"), source, translate);
}

/* data obtained from https://en.wikipedia.org/wiki/Video_game_rating_system */
gchar *
gs_utils_content_rating_age_to_str (GsContentRatingSystem system, guint age)
{
	if (system == GS_CONTENT_RATING_SYSTEM_INCAA) {
		if (age >= 18)
			return g_strdup ("+18");
		if (age >= 13)
			return g_strdup ("+13");
		return g_strdup ("ATP");
	}
	if (system == GS_CONTENT_RATING_SYSTEM_ACB) {
		if (age >= 18)
			return g_strdup ("R18+");
		if (age >= 15)
			return g_strdup ("MA15+");
		return g_strdup ("PG");
	}
	if (system == GS_CONTENT_RATING_SYSTEM_DJCTQ) {
		if (age >= 18)
			return g_strdup ("18");
		if (age >= 16)
			return g_strdup ("16");
		if (age >= 14)
			return g_strdup ("14");
		if (age >= 12)
			return g_strdup ("12");
		if (age >= 10)
			return g_strdup ("10");
		return g_strdup ("L");
	}
	if (system == GS_CONTENT_RATING_SYSTEM_GSRR) {
		if (age >= 18)
			return g_strdup ("限制");
		if (age >= 15)
			return g_strdup ("輔15");
		if (age >= 12)
			return g_strdup ("輔12");
		if (age >= 6)
			return g_strdup ("保護");
		return g_strdup ("普通");
	}
	if (system == GS_CONTENT_RATING_SYSTEM_PEGI) {
		if (age >= 18)
			return g_strdup ("18");
		if (age >= 16)
			return g_strdup ("16");
		if (age >= 12)
			return g_strdup ("12");
		if (age >= 7)
			return g_strdup ("7");
		if (age >= 3)
			return g_strdup ("3");
		return NULL;
	}
	if (system == GS_CONTENT_RATING_SYSTEM_KAVI) {
		if (age >= 18)
			return g_strdup ("18+");
		if (age >= 16)
			return g_strdup ("16+");
		if (age >= 12)
			return g_strdup ("12+");
		if (age >= 7)
			return g_strdup ("7+");
		if (age >= 3)
			return g_strdup ("3+");
		return NULL;
	}
	if (system == GS_CONTENT_RATING_SYSTEM_USK) {
		if (age >= 18)
			return g_strdup ("18");
		if (age >= 16)
			return g_strdup ("16");
		if (age >= 12)
			return g_strdup ("12");
		if (age >= 6)
			return g_strdup ("6");
		return g_strdup ("0");
	}
	/* Reference: http://www.esra.org.ir/ */
	if (system == GS_CONTENT_RATING_SYSTEM_ESRA) {
		if (age >= 18)
			return g_strdup ("+18");
		if (age >= 15)
			return g_strdup ("+15");
		if (age >= 12)
			return g_strdup ("+12");
		if (age >= 7)
			return g_strdup ("+7");
		if (age >= 3)
			return g_strdup ("+3");
		return NULL;
	}
	if (system == GS_CONTENT_RATING_SYSTEM_CERO) {
		if (age >= 18)
			return g_strdup ("Z");
		if (age >= 17)
			return g_strdup ("D");
		if (age >= 15)
			return g_strdup ("C");
		if (age >= 12)
			return g_strdup ("B");
		return g_strdup ("A");
	}
	if (system == GS_CONTENT_RATING_SYSTEM_OFLCNZ) {
		if (age >= 18)
			return g_strdup ("R18");
		if (age >= 16)
			return g_strdup ("R16");
		if (age >= 15)
			return g_strdup ("R15");
		if (age >= 13)
			return g_strdup ("R13");
		return g_strdup ("G");
	}
	if (system == GS_CONTENT_RATING_SYSTEM_RUSSIA) {
		if (age >= 18)
			return g_strdup ("18+");
		if (age >= 16)
			return g_strdup ("16+");
		if (age >= 12)
			return g_strdup ("12+");
		if (age >= 6)
			return g_strdup ("6+");
		return g_strdup ("0+");
	}
	if (system == GS_CONTENT_RATING_SYSTEM_MDA) {
		if (age >= 18)
			return g_strdup ("M18");
		if (age >= 16)
			return g_strdup ("ADV");
		return get_esrb_string ("General", _("General"));
	}
	if (system == GS_CONTENT_RATING_SYSTEM_GRAC) {
		if (age >= 18)
			return g_strdup ("18");
		if (age >= 15)
			return g_strdup ("15");
		if (age >= 12)
			return g_strdup ("12");
		return get_esrb_string ("ALL", _("ALL"));
	}
	if (system == GS_CONTENT_RATING_SYSTEM_ESRB) {
		if (age >= 18)
			return get_esrb_string ("Adults Only", _("Adults Only"));
		if (age >= 17)
			return get_esrb_string ("Mature", _("Mature"));
		if (age >= 13)
			return get_esrb_string ("Teen", _("Teen"));
		if (age >= 10)
			return get_esrb_string ("Everyone 10+", _("Everyone 10+"));
		if (age >= 6)
			return get_esrb_string ("Everyone", _("Everyone"));

		return get_esrb_string ("Early Childhood", _("Early Childhood"));
	}
	/* IARC = everything else */
	if (age >= 18)
		return g_strdup ("18+");
	if (age >= 16)
		return g_strdup ("16+");
	if (age >= 12)
		return g_strdup ("12+");
	if (age >= 7)
		return g_strdup ("7+");
	if (age >= 3)
		return g_strdup ("3+");
	return NULL;
}

/*
 * parse_locale:
 * @locale: (transfer full): a locale to parse
 * @language_out: (out) (optional) (nullable): return location for the parsed
 *    language, or %NULL to ignore
 * @territory_out: (out) (optional) (nullable): return location for the parsed
 *    territory, or %NULL to ignore
 * @codeset_out: (out) (optional) (nullable): return location for the parsed
 *    codeset, or %NULL to ignore
 * @modifier_out: (out) (optional) (nullable): return location for the parsed
 *    modifier, or %NULL to ignore
 *
 * Parse @locale as a locale string of the form
 * `language[_territory][.codeset][@modifier]` — see `man 3 setlocale` for
 * details.
 *
 * On success, %TRUE will be returned, and the components of the locale will be
 * returned in the given addresses, with each component not including any
 * separators. Otherwise, %FALSE will be returned and the components will be set
 * to %NULL.
 *
 * @locale is modified, and any returned non-%NULL pointers will point inside
 * it.
 *
 * Returns: %TRUE on success, %FALSE otherwise
 */
static gboolean
parse_locale (gchar *locale  /* (transfer full) */,
	      const gchar **language_out,
	      const gchar **territory_out,
	      const gchar **codeset_out,
	      const gchar **modifier_out)
{
	gchar *separator;
	const gchar *language = NULL, *territory = NULL, *codeset = NULL, *modifier = NULL;

	separator = strrchr (locale, '@');
	if (separator != NULL) {
		modifier = separator + 1;
		*separator = '\0';
	}

	separator = strrchr (locale, '.');
	if (separator != NULL) {
		codeset = separator + 1;
		*separator = '\0';
	}

	separator = strrchr (locale, '_');
	if (separator != NULL) {
		territory = separator + 1;
		*separator = '\0';
	}

	language = locale;

	/* Parse failure? */
	if (*language == '\0') {
		language = NULL;
		territory = NULL;
		codeset = NULL;
		modifier = NULL;
	}

	if (language_out != NULL)
		*language_out = language;
	if (territory_out != NULL)
		*territory_out = territory;
	if (codeset_out != NULL)
		*codeset_out = codeset;
	if (modifier_out != NULL)
		*modifier_out = modifier;

	return (language != NULL);
}

/* data obtained from https://en.wikipedia.org/wiki/Video_game_rating_system */
GsContentRatingSystem
gs_utils_content_rating_system_from_locale (const gchar *locale)
{
	g_autofree gchar *locale_copy = g_strdup (locale);
	const gchar *territory;

	/* Default to IARC for locales which can’t be parsed. */
	if (!parse_locale (locale_copy, NULL, &territory, NULL, NULL))
		return GS_CONTENT_RATING_SYSTEM_IARC;

	/* Argentina */
	if (g_strcmp0 (territory, "AR") == 0)
		return GS_CONTENT_RATING_SYSTEM_INCAA;

	/* Australia */
	if (g_strcmp0 (territory, "AU") == 0)
		return GS_CONTENT_RATING_SYSTEM_ACB;

	/* Brazil */
	if (g_strcmp0 (territory, "BR") == 0)
		return GS_CONTENT_RATING_SYSTEM_DJCTQ;

	/* Taiwan */
	if (g_strcmp0 (territory, "TW") == 0)
		return GS_CONTENT_RATING_SYSTEM_GSRR;

	/* Europe (but not Finland or Germany), India, Israel,
	 * Pakistan, Quebec, South Africa */
	if ((g_strcmp0 (territory, "GB") == 0) ||
	    g_strcmp0 (territory, "AL") == 0 ||
	    g_strcmp0 (territory, "AD") == 0 ||
	    g_strcmp0 (territory, "AM") == 0 ||
	    g_strcmp0 (territory, "AT") == 0 ||
	    g_strcmp0 (territory, "AZ") == 0 ||
	    g_strcmp0 (territory, "BY") == 0 ||
	    g_strcmp0 (territory, "BE") == 0 ||
	    g_strcmp0 (territory, "BA") == 0 ||
	    g_strcmp0 (territory, "BG") == 0 ||
	    g_strcmp0 (territory, "HR") == 0 ||
	    g_strcmp0 (territory, "CY") == 0 ||
	    g_strcmp0 (territory, "CZ") == 0 ||
	    g_strcmp0 (territory, "DK") == 0 ||
	    g_strcmp0 (territory, "EE") == 0 ||
	    g_strcmp0 (territory, "FR") == 0 ||
	    g_strcmp0 (territory, "GE") == 0 ||
	    g_strcmp0 (territory, "GR") == 0 ||
	    g_strcmp0 (territory, "HU") == 0 ||
	    g_strcmp0 (territory, "IS") == 0 ||
	    g_strcmp0 (territory, "IT") == 0 ||
	    g_strcmp0 (territory, "LZ") == 0 ||
	    g_strcmp0 (territory, "XK") == 0 ||
	    g_strcmp0 (territory, "LV") == 0 ||
	    g_strcmp0 (territory, "FL") == 0 ||
	    g_strcmp0 (territory, "LU") == 0 ||
	    g_strcmp0 (territory, "LT") == 0 ||
	    g_strcmp0 (territory, "MK") == 0 ||
	    g_strcmp0 (territory, "MT") == 0 ||
	    g_strcmp0 (territory, "MD") == 0 ||
	    g_strcmp0 (territory, "MC") == 0 ||
	    g_strcmp0 (territory, "ME") == 0 ||
	    g_strcmp0 (territory, "NL") == 0 ||
	    g_strcmp0 (territory, "NO") == 0 ||
	    g_strcmp0 (territory, "PL") == 0 ||
	    g_strcmp0 (territory, "PT") == 0 ||
	    g_strcmp0 (territory, "RO") == 0 ||
	    g_strcmp0 (territory, "SM") == 0 ||
	    g_strcmp0 (territory, "RS") == 0 ||
	    g_strcmp0 (territory, "SK") == 0 ||
	    g_strcmp0 (territory, "SI") == 0 ||
	    g_strcmp0 (territory, "ES") == 0 ||
	    g_strcmp0 (territory, "SE") == 0 ||
	    g_strcmp0 (territory, "CH") == 0 ||
	    g_strcmp0 (territory, "TR") == 0 ||
	    g_strcmp0 (territory, "UA") == 0 ||
	    g_strcmp0 (territory, "VA") == 0 ||
	    g_strcmp0 (territory, "IN") == 0 ||
	    g_strcmp0 (territory, "IL") == 0 ||
	    g_strcmp0 (territory, "PK") == 0 ||
	    g_strcmp0 (territory, "ZA") == 0)
		return GS_CONTENT_RATING_SYSTEM_PEGI;

	/* Finland */
	if (g_strcmp0 (territory, "FI") == 0)
		return GS_CONTENT_RATING_SYSTEM_KAVI;

	/* Germany */
	if (g_strcmp0 (territory, "DE") == 0)
		return GS_CONTENT_RATING_SYSTEM_USK;

	/* Iran */
	if (g_strcmp0 (territory, "IR") == 0)
		return GS_CONTENT_RATING_SYSTEM_ESRA;

	/* Japan */
	if (g_strcmp0 (territory, "JP") == 0)
		return GS_CONTENT_RATING_SYSTEM_CERO;

	/* New Zealand */
	if (g_strcmp0 (territory, "NZ") == 0)
		return GS_CONTENT_RATING_SYSTEM_OFLCNZ;

	/* Russia: Content rating law */
	if (g_strcmp0 (territory, "RU") == 0)
		return GS_CONTENT_RATING_SYSTEM_RUSSIA;

	/* Singapore */
	if (g_strcmp0 (territory, "SQ") == 0)
		return GS_CONTENT_RATING_SYSTEM_MDA;

	/* South Korea */
	if (g_strcmp0 (territory, "KR") == 0)
		return GS_CONTENT_RATING_SYSTEM_GRAC;

	/* USA, Canada, Mexico */
	if ((g_strcmp0 (territory, "US") == 0) ||
	    g_strcmp0 (territory, "CA") == 0 ||
	    g_strcmp0 (territory, "MX") == 0)
		return GS_CONTENT_RATING_SYSTEM_ESRB;

	/* everything else is IARC */
	return GS_CONTENT_RATING_SYSTEM_IARC;
}
