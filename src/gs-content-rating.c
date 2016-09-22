/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2015-2016 Richard Hughes <richard@hughsie.com>
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

#include <glib/gi18n.h>

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
	_("No references or depictions of sexual nature") },
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
	_("No innappropriate humor") },
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
	_("Players are encouraged to purchase specific real-world items") },
	{ "money-gambling",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No gambling of any kind") },
	{ "money-gambling",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Gambling on random events using tokens or credits") },
	{ "money-gambling",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Gambling using \"play\" money") },
	{ "money-gambling",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Gambling using real money") },
	{ "money-purchasing",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No ability to spend money") },
	{ "money-purchasing",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Ability to spend real money in-game") },
	{ "social-chat",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No way to chat with other players") },
	{ "social-chat",	AS_CONTENT_RATING_VALUE_MILD,
	/* TRANSLATORS: content rating description */
	_("Player-to-player game interactions without chat functionality") },
	{ "social-chat",	AS_CONTENT_RATING_VALUE_MODERATE,
	/* TRANSLATORS: content rating description */
	_("Player-to-player preset interactions without chat functionality") },
	{ "social-chat",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Uncontrolled chat functionality between players") },
	{ "social-audio",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No way to talk with other players") },
	{ "social-audio",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Uncontrolled audio or video chat functionality between players") },
	{ "social-contacts",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No sharing of social network usernames or email addresses") },
	{ "social-contacts",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Sharing social network usernames or email addresses") },
	{ "social-info",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No sharing of user information with 3rd parties") },
	{ "social-info",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Sharing user information with 3rd parties") },
	{ "social-location",	AS_CONTENT_RATING_VALUE_NONE,
	/* TRANSLATORS: content rating description */
	_("No sharing of physical location to other users") },
	{ "social-location",	AS_CONTENT_RATING_VALUE_INTENSE,
	/* TRANSLATORS: content rating description */
	_("Sharing physical location to other users") },
	{ NULL, 0, NULL } };
	for (i = 0; tab[i].id != NULL; i++) {
		if (g_strcmp0 (tab[i].id, id) == 0 && tab[i].value == value)
			return tab[i].desc;
	}
	return NULL;
}

/* data obtained from https://en.wikipedia.org/wiki/Video_game_rating_system */
const gchar *
gs_utils_content_rating_age_to_str (GsContentRatingSystem system, guint age)
{
	if (system == GS_CONTENT_RATING_SYSTEM_INCAA) {
		if (age >= 18)
			return "+18";
		if (age >= 13)
			return "+13";
		return "ATP";
	}
	if (system == GS_CONTENT_RATING_SYSTEM_ACB) {
		if (age >= 18)
			return "R18+";
		if (age >= 15)
			return "MA15+";
		return "PG";
	}
	if (system == GS_CONTENT_RATING_SYSTEM_DJCTQ) {
		if (age >= 18)
			return "18";
		if (age >= 16)
			return "16";
		if (age >= 14)
			return "14";
		if (age >= 12)
			return "12";
		if (age >= 10)
			return "10";
		return "L";
	}
	if (system == GS_CONTENT_RATING_SYSTEM_GSRR) {
		if (age >= 18)
			return "限制";
		if (age >= 15)
			return "輔15";
		if (age >= 12)
			return "輔12";
		if (age >= 6)
			return "保護";
		return "普通";
	}
	if (system == GS_CONTENT_RATING_SYSTEM_PEGI) {
		if (age >= 18)
			return "18";
		if (age >= 16)
			return "16";
		if (age >= 12)
			return "12";
		if (age >= 7)
			return "7";
		if (age >= 3)
			return "3";
		return NULL;
	}
	if (system == GS_CONTENT_RATING_SYSTEM_KAVI) {
		if (age >= 18)
			return "18+";
		if (age >= 16)
			return "16+";
		if (age >= 12)
			return "12+";
		if (age >= 7)
			return "7+";
		if (age >= 3)
			return "3+";
		return NULL;
	}
	if (system == GS_CONTENT_RATING_SYSTEM_USK) {
		if (age >= 18)
			return "18";
		if (age >= 16)
			return "16";
		if (age >= 12)
			return "12";
		if (age >= 6)
			return "6";
		return "0";
	}
	if (system == GS_CONTENT_RATING_SYSTEM_ESRA) {
		if (age >= 25)
			return "+25";
		if (age >= 18)
			return "+18";
		if (age >= 12)
			return "+12";
		if (age >= 7)
			return "+7";
		if (age >= 3)
			return "+3";
		return NULL;
	}
	if (system == GS_CONTENT_RATING_SYSTEM_CERO) {
		if (age >= 18)
			return "Z";
		if (age >= 17)
			return "D";
		if (age >= 15)
			return "C";
		if (age >= 12)
			return "B";
		return "A";
	}
	if (system == GS_CONTENT_RATING_SYSTEM_OFLCNZ) {
		if (age >= 18)
			return "R18";
		if (age >= 16)
			return "R16";
		if (age >= 15)
			return "R15";
		if (age >= 13)
			return "R13";
		return "G";
	}
	if (system == GS_CONTENT_RATING_SYSTEM_RUSSIA) {
		if (age >= 18)
			return "18+";
		if (age >= 16)
			return "16+";
		if (age >= 12)
			return "12+";
		if (age >= 6)
			return "6+";
		return "0+";
	}
	if (system == GS_CONTENT_RATING_SYSTEM_MDA) {
		if (age >= 18)
			return "M18";
		if (age >= 16)
			return "ADV";
		return "General";
	}
	if (system == GS_CONTENT_RATING_SYSTEM_GRAC) {
		if (age >= 18)
			return "18";
		if (age >= 15)
			return "15";
		if (age >= 12)
			return "12";
		return "ALL";
	}
	if (system == GS_CONTENT_RATING_SYSTEM_ESRB) {
		if (age >= 18)
			return "AO";
		if (age >= 17)
			return "M";
		if (age >= 13)
			return "T";
		if (age >= 10)
			return "E10+";
		if (age >= 6)
			return "E";
		return "eC";
	}
	/* IARC = everything else */
	if (age >= 18)
		return "18+";
	if (age >= 16)
		return "16+";
	if (age >= 12)
		return "12+";
	if (age >= 7)
		return "7+";
	if (age >= 3)
		return "3+";
	return NULL;
}

/* data obtained from https://en.wikipedia.org/wiki/Video_game_rating_system */
GsContentRatingSystem
gs_utils_content_rating_system_from_locale (const gchar *locale)
{
	g_auto(GStrv) split = g_strsplit (locale, "_", -1);

	/* Argentina */
	if (g_strcmp0 (split[0], "ar") == 0)
		return GS_CONTENT_RATING_SYSTEM_INCAA;

	/* Australia */
	if (g_strcmp0 (split[0], "au") == 0)
		return GS_CONTENT_RATING_SYSTEM_ACB;

	/* Brazil */
	if (g_strcmp0 (split[0], "br") == 0)
		return GS_CONTENT_RATING_SYSTEM_DJCTQ;

	/* Taiwan */
	if (g_strcmp0 (locale, "zh_TW") == 0)
		return GS_CONTENT_RATING_SYSTEM_GSRR;

	/* Europe (but not Finland or Germany), India, Israel,
	 * Pakistan, Quebec, South Africa */
	if (g_strcmp0 (locale, "en_GB") == 0 ||
	    g_strcmp0 (split[0], "gb") == 0 ||
	    g_strcmp0 (split[0], "al") == 0 ||
	    g_strcmp0 (split[0], "ad") == 0 ||
	    g_strcmp0 (split[0], "am") == 0 ||
	    g_strcmp0 (split[0], "at") == 0 ||
	    g_strcmp0 (split[0], "az") == 0 ||
	    g_strcmp0 (split[0], "by") == 0 ||
	    g_strcmp0 (split[0], "be") == 0 ||
	    g_strcmp0 (split[0], "ba") == 0 ||
	    g_strcmp0 (split[0], "bg") == 0 ||
	    g_strcmp0 (split[0], "hr") == 0 ||
	    g_strcmp0 (split[0], "cy") == 0 ||
	    g_strcmp0 (split[0], "cz") == 0 ||
	    g_strcmp0 (split[0], "dk") == 0 ||
	    g_strcmp0 (split[0], "ee") == 0 ||
	    g_strcmp0 (split[0], "fr") == 0 ||
	    g_strcmp0 (split[0], "ge") == 0 ||
	    g_strcmp0 (split[0], "gr") == 0 ||
	    g_strcmp0 (split[0], "hu") == 0 ||
	    g_strcmp0 (split[0], "is") == 0 ||
	    g_strcmp0 (split[0], "it") == 0 ||
	    g_strcmp0 (split[0], "kz") == 0 ||
	    g_strcmp0 (split[0], "xk") == 0 ||
	    g_strcmp0 (split[0], "lv") == 0 ||
	    g_strcmp0 (split[0], "fl") == 0 ||
	    g_strcmp0 (split[0], "lu") == 0 ||
	    g_strcmp0 (split[0], "lt") == 0 ||
	    g_strcmp0 (split[0], "mk") == 0 ||
	    g_strcmp0 (split[0], "mt") == 0 ||
	    g_strcmp0 (split[0], "md") == 0 ||
	    g_strcmp0 (split[0], "mc") == 0 ||
	    g_strcmp0 (split[0], "me") == 0 ||
	    g_strcmp0 (split[0], "nl") == 0 ||
	    g_strcmp0 (split[0], "no") == 0 ||
	    g_strcmp0 (split[0], "pl") == 0 ||
	    g_strcmp0 (split[0], "pt") == 0 ||
	    g_strcmp0 (split[0], "ro") == 0 ||
	    g_strcmp0 (split[0], "sm") == 0 ||
	    g_strcmp0 (split[0], "rs") == 0 ||
	    g_strcmp0 (split[0], "sk") == 0 ||
	    g_strcmp0 (split[0], "si") == 0 ||
	    g_strcmp0 (split[0], "es") == 0 ||
	    g_strcmp0 (split[0], "se") == 0 ||
	    g_strcmp0 (split[0], "ch") == 0 ||
	    g_strcmp0 (split[0], "tr") == 0 ||
	    g_strcmp0 (split[0], "ua") == 0 ||
	    g_strcmp0 (split[0], "va") == 0 ||
	    g_strcmp0 (split[0], "in") == 0 ||
	    g_strcmp0 (split[0], "il") == 0 ||
	    g_strcmp0 (split[0], "pk") == 0 ||
	    g_strcmp0 (split[0], "za") == 0)
		return GS_CONTENT_RATING_SYSTEM_PEGI;

	/* Finland */
	if (g_strcmp0 (split[0], "fi") == 0)
		return GS_CONTENT_RATING_SYSTEM_KAVI;

	/* Germany */
	if (g_strcmp0 (split[0], "de") == 0)
		return GS_CONTENT_RATING_SYSTEM_USK;

	/* Iran */
	if (g_strcmp0 (split[0], "ir") == 0)
		return GS_CONTENT_RATING_SYSTEM_ESRA;

	/* Japan */
	if (g_strcmp0 (split[0], "jp") == 0)
		return GS_CONTENT_RATING_SYSTEM_CERO;

	/* New Zealand */
	if (g_strcmp0 (split[0], "nz") == 0)
		return GS_CONTENT_RATING_SYSTEM_OFLCNZ;

	/* Russia: Content rating law */
	if (g_strcmp0 (split[0], "ru") == 0)
		return GS_CONTENT_RATING_SYSTEM_RUSSIA;

	/* Singapore */
	if (g_strcmp0 (split[0], "sg") == 0)
		return GS_CONTENT_RATING_SYSTEM_MDA;

	/* South Korea */
	if (g_strcmp0 (split[0], "kr") == 0)
		return GS_CONTENT_RATING_SYSTEM_GRAC;

	/* USA, Canada, Mexico */
	if (g_strcmp0 (locale, "en_US") == 0 ||
	    g_strcmp0 (split[0], "us") == 0 ||
	    g_strcmp0 (split[0], "ca") == 0 ||
	    g_strcmp0 (split[0], "mx") == 0)
		return GS_CONTENT_RATING_SYSTEM_ESRB;

	/* everything else is IARC */
	return GS_CONTENT_RATING_SYSTEM_IARC;
}
