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

const gchar *
gs_content_rating_system_to_str (GsContentRatingSystem system)
{
	if (system == GS_CONTENT_RATING_SYSTEM_INCAA)
		return "INCAA";
	if (system == GS_CONTENT_RATING_SYSTEM_ACB)
		return "ACB";
	if (system == GS_CONTENT_RATING_SYSTEM_DJCTQ)
		return "DJCTQ";
	if (system == GS_CONTENT_RATING_SYSTEM_GSRR)
		return "GSRR";
	if (system == GS_CONTENT_RATING_SYSTEM_PEGI)
		return "PEGI";
	if (system == GS_CONTENT_RATING_SYSTEM_KAVI)
		return "KAVI";
	if (system == GS_CONTENT_RATING_SYSTEM_USK)
		return "USK";
	if (system == GS_CONTENT_RATING_SYSTEM_ESRA)
		return "ESRA";
	if (system == GS_CONTENT_RATING_SYSTEM_CERO)
		return "CERO";
	if (system == GS_CONTENT_RATING_SYSTEM_OFLCNZ)
		return "OFLCNZ";
	if (system == GS_CONTENT_RATING_SYSTEM_RUSSIA)
		return "RUSSIA";
	if (system == GS_CONTENT_RATING_SYSTEM_MDA)
		return "MDA";
	if (system == GS_CONTENT_RATING_SYSTEM_GRAC)
		return "GRAC";
	if (system == GS_CONTENT_RATING_SYSTEM_ESRB)
		return "ESRB";
	if (system == GS_CONTENT_RATING_SYSTEM_IARC)
		return "IARC";
	return NULL;
}

const gchar *
gs_content_rating_key_value_to_str (const gchar *id, AsContentRatingValue value)
{
	guint i;
	struct {
		const gchar		*id;
		AsContentRatingValue	 value;
		const gchar		*desc;
	} tab[] =  {
	{ "violence-cartoon",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No cartoon violence") },
	{ "violence-cartoon",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Cartoon characters in unsafe situations") },
	{ "violence-cartoon",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Cartoon characters in aggressive conflict") },
	{ "violence-cartoon",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Graphic violence involving cartoon characters") },
	{ "violence-fantasy",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No fantasy violence") },
	{ "violence-fantasy",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Characters in unsafe situations easily distinguishable from reality") },
	{ "violence-fantasy",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Characters in aggressive conflict easily distinguishable from reality") },
	{ "violence-fantasy",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Graphic violence easily distinguishable from reality") },
	{ "violence-realistic",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No realistic violence") },
	{ "violence-realistic",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Mildly realistic characters in unsafe situations") },
	{ "violence-realistic",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Depictions of realistic characters in aggressive conflict") },
	{ "violence-realistic",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Graphic violence involving realistic characters") },
	{ "violence-bloodshed",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No bloodshed") },
	{ "violence-bloodshed",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Unrealistic bloodshed") },
	{ "violence-bloodshed",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Realistic bloodshed") },
	{ "violence-bloodshed",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Depictions of bloodshed and the mutilation of body parts") },
	{ "violence-sexual",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No sexual violence") },
	{ "violence-sexual",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Rape or other violent sexual behavior") },
	{ "drugs-alcohol",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No references to alcohol") },
	{ "drugs-alcohol",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("References to alcoholic beverages") },
	{ "drugs-alcohol",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Use of alcoholic beverages") },
	{ "drugs-narcotics",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No references to illicit drugs") },
	{ "drugs-narcotics",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("References to illicit drugs") },
	{ "drugs-narcotics",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Use of illicit drugs") },
	{ "drugs-tobacco",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("References to tobacco products") },
	{ "drugs-tobacco",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Use of tobacco products") },
	{ "sex-nudity",		AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No nudity of any sort") },
	{ "sex-nudity",		AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Brief artistic nudity") },
	{ "sex-nudity",		AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Prolonged nudity") },
	{ "sex-themes",		AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No references to or depictions of sexual nature") },
	{ "sex-themes",		AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Provocative references or depictions") },
	{ "sex-themes",		AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Sexual references or depictions") },
	{ "sex-themes",		AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Graphic sexual behavior") },
	{ "language-profanity",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No profanity of any kind") },
	{ "language-profanity",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Mild or infrequent use of profanity") },
	{ "language-profanity",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Moderate use of profanity") },
	{ "language-profanity",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Strong or frequent use of profanity") },
	{ "language-humor",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No inappropriate humor") },
	{ "language-humor",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Slapstick humor") },
	{ "language-humor",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Vulgar or bathroom humor") },
	{ "language-humor",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Mature or sexual humor") },
	{ "language-discrimination", AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No discriminatory language of any kind") },
	{ "language-discrimination", AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Negativity towards a specific group of people") },
	{ "language-discrimination", AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Discrimination designed to cause emotional harm") },
	{ "language-discrimination", AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Explicit discrimination based on gender, sexuality, race or religion") },
	{ "money-advertising", AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No advertising of any kind") },
	{ "money-advertising", AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Product placement") },
	{ "money-advertising", AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Explicit references to specific brands or trademarked products") },
	{ "money-advertising", AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Users are encouraged to purchase specific real-world items") },
	{ "money-gambling",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No gambling of any kind") },
	{ "money-gambling",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Gambling on random events using tokens or credits") },
	{ "money-gambling",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Gambling using “play” money") },
	{ "money-gambling",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Gambling using real money") },
	{ "money-purchasing",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No ability to spend money") },
	{ "money-purchasing",	AS_CONTENT_RATING_VALUE_MILD,		/* v1.1 */
	/* TRANSLATORS: content rating description */
	_("Users are encouraged to donate real money") },
	{ "money-purchasing",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Ability to spend real money in-app") },
	{ "social-chat",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No way to chat with other users") },
	{ "social-chat",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("User-to-user interactions without chat functionality") },
	{ "social-chat",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Moderated chat functionality between users") },
	{ "social-chat",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Uncontrolled chat functionality between users") },
	{ "social-audio",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No way to talk with other users") },
	{ "social-audio",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Uncontrolled audio or video chat functionality between users") },
	{ "social-contacts",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No sharing of social network usernames or email addresses") },
	{ "social-contacts",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Sharing social network usernames or email addresses") },
	{ "social-info",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No sharing of user information with third parties") },
	{ "social-info",	AS_CONTENT_RATING_VALUE_MILD,		/* v1.1 */
	/* TRANSLATORS: content rating description */
	_("Checking for the latest application version") },
	{ "social-info",	AS_CONTENT_RATING_VALUE_MODERATE,	/* v1.1 */
	/* TRANSLATORS: content rating description */
	_("Sharing diagnostic data that does not let others identify the user") },
	{ "social-info",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Sharing information that lets others identify the user") },
	{ "social-location",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No sharing of physical location with other users") },
	{ "social-location",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Sharing physical location with other users") },

	/* v1.1 */
	{ "sex-homosexuality",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No references to homosexuality") },
	{ "sex-homosexuality",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Indirect references to homosexuality") },
	{ "sex-homosexuality",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Kissing between people of the same gender") },
	{ "sex-homosexuality",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Graphic sexual behavior between people of the same gender") },
	{ "sex-prostitution",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No references to prostitution") },
	{ "sex-prostitution",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Indirect references to prostitution") },
	{ "sex-prostitution",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Direct references to prostitution") },
	{ "sex-prostitution",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Graphic depictions of the act of prostitution") },
	{ "sex-adultery",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No references to adultery") },
	{ "sex-adultery",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Indirect references to adultery") },
	{ "sex-adultery",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Direct references to adultery") },
	{ "sex-adultery",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Graphic depictions of the act of adultery") },
	{ "sex-appearance",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No sexualized characters") },
	{ "sex-appearance",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Scantily clad human characters") },
	{ "sex-appearance",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Overtly sexualized human characters") },
	{ "violence-worship",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No references to desecration") },
	{ "violence-worship",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Depictions of or references to historical desecration") },
	{ "violence-worship",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Depictions of modern-day human desecration") },
	{ "violence-worship",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Graphic depictions of modern-day desecration") },
	{ "violence-desecration", AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No visible dead human remains") },
	{ "violence-desecration", AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Visible dead human remains") },
	{ "violence-desecration", AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Dead human remains that are exposed to the elements") },
	{ "violence-desecration", AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Graphic depictions of desecration of human bodies") },
	{ "violence-slavery",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No references to slavery") },
	{ "violence-slavery",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Depictions of or references to historical slavery") },
	{ "violence-slavery",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Depictions of modern-day slavery") },
	{ "violence-slavery",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Graphic depictions of modern-day slavery") },
	{ NULL, 0, NULL } };
	for (i = 0; tab[i].id != NULL; i++) {
		if (g_strcmp0 (tab[i].id, id) == 0 && tab[i].value == value)
			return tab[i].desc;
	}
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
