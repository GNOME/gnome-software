/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-age-rating-context-dialog
 * @short_description: A dialog showing age rating information about an app
 *
 * #GsAgeRatingContextDialog is a dialog which shows detailed information
 * about the suitability of the content in an app for different ages. It gives
 * a breakdown of which content is more or less suitable for younger audiences.
 * This information is derived from the `<content_rating>` element in the app’s
 * appdata.
 *
 * It is designed to show a more detailed view of the information which the
 * app’s age rating tile in #GsAppContextBar is derived from.
 *
 * The widget has no special appearance if the app is unset, so callers will
 * typically want to hide the dialog in that case.
 *
 * Since: 41
 */

#include "config.h"

#include <adwaita.h>
#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <locale.h>

#include "gs-app.h"
#include "gs-common.h"
#include "gs-context-dialog-row.h"
#include "gs-age-rating-context-dialog.h"

struct _GsAgeRatingContextDialog
{
	GsInfoWindow		 parent_instance;

	GsApp			*app;  /* (nullable) (owned) */
	gulong			 app_notify_handler_content_rating;
	gulong			 app_notify_handler_name;

	GtkLabel		*age;
	GtkWidget		*lozenge;
	GtkLabel		*title;
	GtkListBox		*attributes_list;
};

G_DEFINE_TYPE (GsAgeRatingContextDialog, gs_age_rating_context_dialog, GS_TYPE_INFO_WINDOW)

typedef enum {
	PROP_APP = 1,
} GsAgeRatingContextDialogProperty;

static GParamSpec *obj_props[PROP_APP + 1] = { NULL, };

/* FIXME: Ideally this data would move into libappstream, to be next to the
 * other per-attribute strings and data which it already stores. */
static const struct {
	const gchar *id;  /* (not nullable) */
	const gchar *title;  /* (not nullable) */
	const gchar *icon_name;  /* (not nullable) */
	const gchar *icon_name_negative;  /* (nullable) */
} attribute_details[] = {
	/* v1.0 */
	{
		"violence-cartoon",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Cartoon Violence"),
		"violence-symbolic",
		"violence-none-symbolic",
	},
	{
		"violence-fantasy",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Fantasy Violence"),
		"violence-symbolic",
		"violence-none-symbolic",
	},
	{
		"violence-realistic",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Realistic Violence"),
		"violence-symbolic",
		"violence-none-symbolic",
	},
	{
		"violence-bloodshed",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Violence Depicting Bloodshed"),
		"violence-symbolic",
		"violence-none-symbolic",
	},
	{
		"violence-sexual",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Sexual Violence"),
		"violence-symbolic",
		"violence-none-symbolic",
	},
	{
		"drugs-alcohol",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Alcohol"),
		"pub-symbolic",
		NULL,
	},
	{
		"drugs-narcotics",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Narcotics"),
		"cigarette-symbolic",
		"cigarette-none-symbolic",
	},
	{
		"drugs-tobacco",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Tobacco"),
		"cigarette-symbolic",
		"cigarette-none-symbolic",
	},
	{
		"sex-nudity",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Nudity"),
		"nudity-symbolic",
		"nudity-none-symbolic",
	},
	{
		"sex-themes",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Sexual Themes"),
		"nudity-symbolic",
		"nudity-none-symbolic",
	},
	{
		"language-profanity",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Profanity"),
		"strong-language-symbolic",
		"strong-language-none-symbolic",
	},
	{
		"language-humor",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Inappropriate Humor"),
		"strong-language-symbolic",
		"strong-language-none-symbolic",
	},
	{
		"language-discrimination",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Discrimination"),
		"chat-symbolic",
		"chat-none-symbolic",
	},
	{
		"money-advertising",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Advertising"),
		"money-symbolic",
		"money-none-symbolic",
	},
	{
		"money-gambling",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Gambling"),
		"money-symbolic",
		"money-none-symbolic",
	},
	{
		"money-purchasing",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Purchasing"),
		"money-symbolic",
		"money-none-symbolic",
	},
	{
		"social-chat",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Chat Between Users"),
		"chat-symbolic",
		"chat-none-symbolic",
	},
	{
		"social-audio",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Audio Chat Between Users"),
		"audio-headset-symbolic",
		NULL,
	},
	{
		"social-contacts",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Contact Details"),
		"contact-new-symbolic",
		NULL,
	},
	{
		"social-info",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Identifying Information"),
		"x-office-address-book-symbolic",
		NULL,
	},
	{
		"social-location",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Location Sharing"),
		"location-services-active-symbolic",
		"location-services-disabled-symbolic",
	},

	/* v1.1 */
	{
		/* Why is there an OARS category which discriminates based on sexual orientation?
		 * It’s because there are, very unfortunately, still countries in the world in
		 * which homosexuality, or software which refers to it, is illegal. In order to be
		 * able to ship FOSS in those countries, there needs to be a mechanism for apps to
		 * describe whether they refer to anything illegal, and for ratings mechanisms in
		 * those countries to filter out any apps which describe themselves as such.
		 *
		 * As a counterpoint, it’s illegal in many more countries to discriminate on the
		 * basis of sexual orientation, so this category is treated exactly the same as
		 * sex-themes (once the intensities of the ratings levels for both categories are
		 * normalised) in those countries.
		 *
		 * The differences between countries are handled through handling #AsContentRatingSystem
		 * values differently. */
		"sex-homosexuality",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Homosexuality"),
		"nudity-symbolic",
		"nudity-none-symbolic",
	},
	{
		"sex-prostitution",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Prostitution"),
		"nudity-symbolic",
		"nudity-none-symbolic",
	},
	{
		"sex-adultery",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Adultery"),
		"nudity-symbolic",
		"nudity-none-symbolic",
	},
	{
		"sex-appearance",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Sexualized Characters"),
		"nudity-symbolic",
		"nudity-none-symbolic",
	},
	{
		"violence-worship",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Desecration"),
		"violence-symbolic",
		"violence-none-symbolic",
	},
	{
		"violence-desecration",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Human Remains"),
		"graveyard-symbolic",
		NULL,
	},
	{
		"violence-slavery",
		/* TRANSLATORS: content rating title, see https://hughsie.github.io/oars/ */
		N_("Slavery"),
		"violence-symbolic",
		"violence-none-symbolic",
	},
};

/* Get the `icon_name` (or, if @negative_version is %TRUE, the
 * `icon_name_negative`) from @attribute_details for the given @attribute.
 * If `icon_name_negative` is %NULL, fall back to returning `icon_name`. */
static const gchar *
content_rating_attribute_get_icon_name (const gchar *attribute,
                                        gboolean     negative_version)
{
	for (gsize i = 0; i < G_N_ELEMENTS (attribute_details); i++) {
		if (g_str_equal (attribute, attribute_details[i].id)) {
			if (negative_version && attribute_details[i].icon_name_negative != NULL)
				return attribute_details[i].icon_name_negative;
			return attribute_details[i].icon_name;
		}
	}

	/* Attribute not handled */
	g_assert_not_reached ();
}

/* Get the `title` from @attribute_details for the given @attribute. */
static const gchar *
content_rating_attribute_get_title (const gchar *attribute)
{
	for (gsize i = 0; i < G_N_ELEMENTS (attribute_details); i++) {
		if (g_str_equal (attribute, attribute_details[i].id)) {
			return _(attribute_details[i].title);
		}
	}

	/* Attribute not handled */
	g_assert_not_reached ();
}

static void
add_attribute_row (GtkListBox           *list_box,
                   const gchar          *attribute,
                   AsContentRatingValue  value)
{
	GtkListBoxRow *row;
	GsContextDialogRowImportance rating;
	const gchar *icon_name, *title, *description;

	switch (value) {
	case AS_CONTENT_RATING_VALUE_UNKNOWN:
		rating = GS_CONTEXT_DIALOG_ROW_IMPORTANCE_NEUTRAL;
		icon_name = content_rating_attribute_get_icon_name (attribute, FALSE);
		/* Translators: This refers to a content rating attribute which
		 * has an unknown value. For example, the amount of violence in
		 * an app is ‘Unknown’. */
		description = _("Unknown");
		break;
	case AS_CONTENT_RATING_VALUE_NONE:
		rating = GS_CONTEXT_DIALOG_ROW_IMPORTANCE_UNIMPORTANT;
		icon_name = content_rating_attribute_get_icon_name (attribute, TRUE);
		description = as_content_rating_attribute_get_description (attribute, value);
		break;
	case AS_CONTENT_RATING_VALUE_MILD:
		rating = GS_CONTEXT_DIALOG_ROW_IMPORTANCE_WARNING;
		icon_name = content_rating_attribute_get_icon_name (attribute, FALSE);
		description = as_content_rating_attribute_get_description (attribute, value);
		break;
	case AS_CONTENT_RATING_VALUE_MODERATE:
		rating = GS_CONTEXT_DIALOG_ROW_IMPORTANCE_WARNING;
		icon_name = content_rating_attribute_get_icon_name (attribute, FALSE);
		description = as_content_rating_attribute_get_description (attribute, value);
		break;
	case AS_CONTENT_RATING_VALUE_INTENSE:
		rating = GS_CONTEXT_DIALOG_ROW_IMPORTANCE_IMPORTANT;
		icon_name = content_rating_attribute_get_icon_name (attribute, FALSE);
		description = as_content_rating_attribute_get_description (attribute, value);
		break;
	default:
		g_assert_not_reached ();
	}

	title = content_rating_attribute_get_title (attribute);

	row = gs_context_dialog_row_new (icon_name, rating, title, description);
	gtk_list_box_append (list_box, GTK_WIDGET (row));
}

/**
 * gs_age_rating_context_dialog_process_attributes:
 * @content_rating: content rating data from an app, retrieved using
 *     gs_app_dup_content_rating()
 * @show_worst_only: %TRUE to only process the worst content rating attributes,
 *     %FALSE to process all of them
 * @callback: callback to call for each attribute being processed
 * @user_data: data to pass to @callback
 *
 * Loop through all the defined content rating attributes, and decide which ones
 * are relevant to show to the user. For each of the relevant attributes, call
 * @callback with the attribute name and value.
 *
 * If @show_worst_only is %TRUE, only the attributes which cause the overall
 * rating of the app to be as high as it is are considered relevant. If it is
 * %FALSE, all attributes are relevant.
 *
 * If the app has an overall age rating of 0, @callback is called exactly once,
 * with the attribute name set to %NULL, to indicate that the app is suitable
 * for all in every attribute.
 *
 * Since: 41
 */
void
gs_age_rating_context_dialog_process_attributes (AsContentRating                       *content_rating,
                                                 gboolean                               show_worst_only,
                                                 GsAgeRatingContextDialogAttributeFunc  callback,
                                                 gpointer                               user_data)
{
	g_autofree const gchar **rating_ids = as_content_rating_get_all_rating_ids ();
	AsContentRatingValue value_bad = AS_CONTENT_RATING_VALUE_NONE;
	guint age_bad = 0;

	/* Ordered from worst to best, these are all OARS 1.0/1.1 categories */
	const gchar * const violence_group[] = {
		"violence-bloodshed",
		"violence-realistic",
		"violence-fantasy",
		"violence-cartoon",
		NULL
	};
	const gchar * const social_group[] = {
		"social-audio",
		"social-chat",
		"social-contacts",
		"social-info",
		NULL
	};
	const gchar * const coalesce_groups[] = {
		"sex-themes",
		"sex-homosexuality",
		NULL
	};

	/* Get the worst category. */
	for (gsize i = 0; rating_ids[i] != NULL; i++) {
		guint rating_age;
		AsContentRatingValue rating_value;

		rating_value = as_content_rating_get_value (content_rating, rating_ids[i]);
		rating_age = as_content_rating_attribute_to_csm_age (rating_ids[i], rating_value);

		if (rating_age > age_bad)
			age_bad = rating_age;
		if (rating_value > value_bad)
			value_bad = rating_value;
	}

	/* If the worst category is nothing, great! Show a more specific message
	 * than a big listing of all the groups. */
	if (show_worst_only && (value_bad == AS_CONTENT_RATING_VALUE_NONE || age_bad == 0)) {
		callback (NULL, AS_CONTENT_RATING_VALUE_UNKNOWN, user_data);
		return;
	}

	/* Add a description for each rating category which contributes to the
	 * @age_bad being as it is. Handle the groups separately.
	 * Intentionally coalesce some categories if they have the same values,
	 * to avoid confusion */
	for (gsize i = 0; rating_ids[i] != NULL; i++) {
		guint rating_age;
		AsContentRatingValue rating_value;

		if (g_strv_contains (violence_group, rating_ids[i]) ||
		    g_strv_contains (social_group, rating_ids[i]))
			continue;

		rating_value = as_content_rating_get_value (content_rating, rating_ids[i]);
		rating_age = as_content_rating_attribute_to_csm_age (rating_ids[i], rating_value);

		if (show_worst_only && rating_age < age_bad)
			continue;

		/* Coalesce down to the first element in @coalesce_groups,
		 * unless this group’s value differs. Currently only one
		 * coalesce group is supported. */
		if (g_strv_contains (coalesce_groups + 1, rating_ids[i]) &&
		    as_content_rating_attribute_to_csm_age (coalesce_groups[0],
							    as_content_rating_get_value (content_rating,
											 coalesce_groups[0])) >= rating_age)
			continue;

		callback (rating_ids[i], rating_value, user_data);
	}

	for (gsize i = 0; violence_group[i] != NULL; i++) {
		guint rating_age;
		AsContentRatingValue rating_value;

		rating_value = as_content_rating_get_value (content_rating, violence_group[i]);
		rating_age = as_content_rating_attribute_to_csm_age (violence_group[i], rating_value);

		if (show_worst_only && rating_age < age_bad)
			continue;

		callback (violence_group[i], rating_value, user_data);
		break;
	}

	for (gsize i = 0; social_group[i] != NULL; i++) {
		guint rating_age;
		AsContentRatingValue rating_value;

		rating_value = as_content_rating_get_value (content_rating, social_group[i]);
		rating_age = as_content_rating_attribute_to_csm_age (social_group[i], rating_value);

		if (show_worst_only && rating_age < age_bad)
			continue;

		callback (social_group[i], rating_value, user_data);
		break;
	}
}

static void
add_attribute_rows_cb (const gchar          *attribute,
                       AsContentRatingValue  value,
                       gpointer              user_data)
{
	GsAgeRatingContextDialog *self = GS_AGE_RATING_CONTEXT_DIALOG (user_data);

	add_attribute_row (self->attributes_list, attribute, value);
}

/* Wrapper around as_content_rating_system_format_age() which returns the short
 * form of the content rating. This doesn’t make a difference for most ratings
 * systems, but it does for ESRB which normally produces quite long strings.
 *
 * FIXME: This should probably be upstreamed into libappstream once it’s been in
 * the GNOME 41 release and stabilised. */
gchar *
gs_age_rating_context_dialog_format_age_short (AsContentRatingSystem system,
                                               guint                 age)
{
	if (system == AS_CONTENT_RATING_SYSTEM_ESRB) {
		if (age >= 18)
			return g_strdup ("AO");
		if (age >= 17)
			return g_strdup ("M");
		if (age >= 13)
			return g_strdup ("T");
		if (age >= 10)
			return g_strdup ("E10+");
		if (age >= 6)
			return g_strdup ("E");

		return g_strdup ("EC");
	}

	return as_content_rating_system_format_age (system, age);
}

/**
 * gs_age_rating_context_dialog_update_lozenge:
 * @app: the #GsApp to rate
 * @lozenge: lozenge widget
 * @lozenge_content: label within the lozenge widget
 * @is_unknown_out: (out caller-allocates) (not optional): return location for
 *     a boolean indicating whether the age rating is unknown, rather than a
 *     specific age
 *
 * Update the @lozenge and @lozenge_content widgets to indicate the overall
 * age rating for @app. This involves changing their CSS class and label
 * content.
 *
 * If the overall age rating for @app is unknown (because the app doesn’t
 * provide a complete `<content_rating>` element in its appdata), the lozenge is
 * set to show a question mark, and @is_unknown_out is set to %TRUE.
 *
 * Since: 41
 */
void
gs_age_rating_context_dialog_update_lozenge (GsApp     *app,
                                             GtkWidget *lozenge,
                                             GtkLabel  *lozenge_content,
                                             gboolean  *is_unknown_out)
{
	const gchar *css_class;
	const gchar *locale;
	AsContentRatingSystem system;
	g_autoptr(AsContentRating) content_rating = NULL;
	GtkStyleContext *context;
	const gchar *css_age_classes[] = {
		"details-rating-18",
		"details-rating-15",
		"details-rating-12",
		"details-rating-5",
		"details-rating-0",
	};
	guint age = G_MAXUINT;
	g_autofree gchar *age_text = NULL;

	g_return_if_fail (GS_IS_APP (app));
	g_return_if_fail (GTK_IS_WIDGET (lozenge));
	g_return_if_fail (GTK_IS_LABEL (lozenge_content));
	g_return_if_fail (is_unknown_out != NULL);

	/* get the content rating system from the locale */
	locale = setlocale (LC_MESSAGES, NULL);
	system = as_content_rating_system_from_locale (locale);
	g_debug ("content rating system is guessed as %s from %s",
		 as_content_rating_system_to_string (system),
		 locale);

	content_rating = gs_app_dup_content_rating (app);
	if (content_rating != NULL)
		age = as_content_rating_get_minimum_age (content_rating);

	if (age != G_MAXUINT)
		age_text = gs_age_rating_context_dialog_format_age_short (system, age);

	/* Some ratings systems (PEGI) don’t start at age 0 */
	if (content_rating != NULL && age_text == NULL && age == 0)
		/* Translators: The app is considered suitable to be run by all ages of people.
		 * This is displayed in a context tile, so the string should be short. */
		age_text = g_strdup (_("All"));

	/* We currently only support OARS-1.0 and OARS-1.1 */
	if (age_text == NULL ||
	    (content_rating != NULL &&
	     g_strcmp0 (as_content_rating_get_kind (content_rating), "oars-1.0") != 0 &&
	     g_strcmp0 (as_content_rating_get_kind (content_rating), "oars-1.1") != 0)) {
		/* Translators: This app has no age rating information available.
		 * This string is displayed like an icon. Please use any
		 * similarly short punctuation character, word or acronym which
		 * will be widely understood in your region, in this context.
		 * This is displayed in a context tile, so the string should be short. */
		g_free (age_text);
		age_text = g_strdup (_("?"));
		css_class = "grey";
		*is_unknown_out = TRUE;
	} else {
		/* Update the CSS */
		if (age >= 18)
			css_class = css_age_classes[0];
		else if (age >= 15)
			css_class = css_age_classes[1];
		else if (age >= 12)
			css_class = css_age_classes[2];
		else if (age >= 5)
			css_class = css_age_classes[3];
		else
			css_class = css_age_classes[4];

		*is_unknown_out = FALSE;
	}

	/* Update the UI. */
	gtk_label_set_text (lozenge_content, age_text);

	context = gtk_widget_get_style_context (lozenge);

	for (gsize i = 0; i < G_N_ELEMENTS (css_age_classes); i++)
		gtk_style_context_remove_class (context, css_age_classes[i]);
	gtk_style_context_remove_class (context, "grey");

	gtk_style_context_add_class (context, css_class);
}

static void
update_attributes_list (GsAgeRatingContextDialog *self)
{
	g_autoptr(AsContentRating) content_rating = NULL;
	gboolean is_unknown;
	g_autofree gchar *title = NULL;

	gs_widget_remove_all (GTK_WIDGET (self->attributes_list), (GsRemoveFunc) gtk_list_box_remove);

	/* UI state is undefined if app is not set. */
	if (self->app == NULL)
		return;

	/* Update lozenge and title */
	content_rating = gs_app_dup_content_rating (self->app);
	gs_age_rating_context_dialog_update_lozenge (self->app,
						     self->lozenge,
						     self->age,
						     &is_unknown);

	/* Title */
	if (is_unknown) {
		/* Translators: It’s unknown what age rating this app has. The
		 * placeholder is the app name. */
		title = g_strdup_printf (("%s has an unknown age rating"), gs_app_get_name (self->app));
	} else {
		guint age;

		/* if content_rating is NULL, is_unknown should be TRUE */
		g_assert (content_rating != NULL);
		age = as_content_rating_get_minimum_age (content_rating);

		if (age == 0)
			/* Translators: This is a dialogue title which indicates that an app is suitable
			 * for all ages. The placeholder is the app name. */
			title = g_strdup_printf (_("%s is suitable for everyone"), gs_app_get_name (self->app));
		else if (age <= 3)
			/* Translators: This is a dialogue title which indicates that an app is suitable
			 * for children up to around age 3. The placeholder is the app name. */
			title = g_strdup_printf (_("%s is suitable for toddlers"), gs_app_get_name (self->app));
		else if (age <= 5)
			/* Translators: This is a dialogue title which indicates that an app is suitable
			 * for children up to around age 5. The placeholder is the app name. */
			title = g_strdup_printf (_("%s is suitable for young children"), gs_app_get_name (self->app));
		else if (age <= 12)
			/* Translators: This is a dialogue title which indicates that an app is suitable
			 * for children up to around age 12. The placeholder is the app name. */
			title = g_strdup_printf (("%s is suitable for children"), gs_app_get_name (self->app));
		else if (age <= 18)
			/* Translators: This is a dialogue title which indicates that an app is suitable
			 * for people up to around age 18. The placeholder is the app name. */
			title = g_strdup_printf (_("%s is suitable for teenagers"), gs_app_get_name (self->app));
		else if (age < G_MAXUINT)
			/* Translators: This is a dialogue title which indicates that an app is suitable
			 * for people aged up to and over 18. The placeholder is the app name. */
			title = g_strdup_printf (_("%s is suitable for adults"), gs_app_get_name (self->app));
		else
			/* Translators: This is a dialogue title which indicates that an app is suitable
			 * for a specified age group. The first placeholder is the app name, the second
			 * is the age group. */
			title = g_strdup_printf (_("%s is suitable for %s"), gs_app_get_name (self->app),
						 gtk_label_get_text (self->age));
	}

	gtk_label_set_text (self->title, title);

	/* Update the rows */
	gs_age_rating_context_dialog_process_attributes (content_rating,
							 FALSE,
							 add_attribute_rows_cb,
							 self);
}

static void
app_notify_cb (GObject    *obj,
               GParamSpec *pspec,
               gpointer    user_data)
{
	GsAgeRatingContextDialog *self = GS_AGE_RATING_CONTEXT_DIALOG (user_data);

	update_attributes_list (self);
}

static gint
sort_cb (GtkListBoxRow *row1,
         GtkListBoxRow *row2,
         gpointer       user_data)
{
	GsContextDialogRow *_row1 = GS_CONTEXT_DIALOG_ROW (row1);
	GsContextDialogRow *_row2 = GS_CONTEXT_DIALOG_ROW (row2);
	GsContextDialogRowImportance importance1, importance2;
	const gchar *title1, *title2;

	importance1 = gs_context_dialog_row_get_importance (_row1);
	importance2 = gs_context_dialog_row_get_importance (_row2);

	if (importance1 != importance2)
		return importance2 - importance1;

	title1 = gs_context_dialog_row_get_title (_row1);
	title2 = gs_context_dialog_row_get_title (_row2);

	return g_strcmp0 (title1, title2);
}

static void
gs_age_rating_context_dialog_init (GsAgeRatingContextDialog *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	/* Sort the list so the most important rows are at the top. */
	gtk_list_box_set_sort_func (self->attributes_list, sort_cb, NULL, NULL);
}

static void
gs_age_rating_context_dialog_get_property (GObject    *object,
                                           guint       prop_id,
                                           GValue     *value,
                                           GParamSpec *pspec)
{
	GsAgeRatingContextDialog *self = GS_AGE_RATING_CONTEXT_DIALOG (object);

	switch ((GsAgeRatingContextDialogProperty) prop_id) {
	case PROP_APP:
		g_value_set_object (value, gs_age_rating_context_dialog_get_app (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_age_rating_context_dialog_set_property (GObject      *object,
                                           guint         prop_id,
                                           const GValue *value,
                                           GParamSpec   *pspec)
{
	GsAgeRatingContextDialog *self = GS_AGE_RATING_CONTEXT_DIALOG (object);

	switch ((GsAgeRatingContextDialogProperty) prop_id) {
	case PROP_APP:
		gs_age_rating_context_dialog_set_app (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_age_rating_context_dialog_dispose (GObject *object)
{
	GsAgeRatingContextDialog *self = GS_AGE_RATING_CONTEXT_DIALOG (object);

	gs_age_rating_context_dialog_set_app (self, NULL);

	G_OBJECT_CLASS (gs_age_rating_context_dialog_parent_class)->dispose (object);
}

static void
gs_age_rating_context_dialog_class_init (GsAgeRatingContextDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_age_rating_context_dialog_get_property;
	object_class->set_property = gs_age_rating_context_dialog_set_property;
	object_class->dispose = gs_age_rating_context_dialog_dispose;

	/**
	 * GsAgeRatingContextDialog:app: (nullable)
	 *
	 * The app to display the age_rating context details for.
	 *
	 * This may be %NULL; if so, the content of the widget will be
	 * undefined.
	 *
	 * Since: 41
	 */
	obj_props[PROP_APP] =
		g_param_spec_object ("app", NULL, NULL,
				     GS_TYPE_APP,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-age-rating-context-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsAgeRatingContextDialog, age);
	gtk_widget_class_bind_template_child (widget_class, GsAgeRatingContextDialog, lozenge);
	gtk_widget_class_bind_template_child (widget_class, GsAgeRatingContextDialog, title);
	gtk_widget_class_bind_template_child (widget_class, GsAgeRatingContextDialog, attributes_list);
}

/**
 * gs_age_rating_context_dialog_new:
 * @app: (nullable): the app to display age_rating context information for, or %NULL
 *
 * Create a new #GsAgeRatingContextDialog and set its initial app to @app.
 *
 * Returns: (transfer full): a new #GsAgeRatingContextDialog
 * Since: 41
 */
GsAgeRatingContextDialog *
gs_age_rating_context_dialog_new (GsApp *app)
{
	g_return_val_if_fail (app == NULL || GS_IS_APP (app), NULL);

	return g_object_new (GS_TYPE_AGE_RATING_CONTEXT_DIALOG,
			     "app", app,
			     NULL);
}

/**
 * gs_age_rating_context_dialog_get_app:
 * @self: a #GsAgeRatingContextDialog
 *
 * Gets the value of #GsAgeRatingContextDialog:app.
 *
 * Returns: (nullable) (transfer none): app whose age_rating context information is
 *     being displayed, or %NULL if none is set
 * Since: 41
 */
GsApp *
gs_age_rating_context_dialog_get_app (GsAgeRatingContextDialog *self)
{
	g_return_val_if_fail (GS_IS_AGE_RATING_CONTEXT_DIALOG (self), NULL);

	return self->app;
}

/**
 * gs_age_rating_context_dialog_set_app:
 * @self: a #GsAgeRatingContextDialog
 * @app: (nullable) (transfer none): the app to display age_rating context
 *     information for, or %NULL for none
 *
 * Set the value of #GsAgeRatingContextDialog:app.
 *
 * Since: 41
 */
void
gs_age_rating_context_dialog_set_app (GsAgeRatingContextDialog *self,
                                            GsApp                          *app)
{
	g_return_if_fail (GS_IS_AGE_RATING_CONTEXT_DIALOG (self));
	g_return_if_fail (app == NULL || GS_IS_APP (app));

	if (app == self->app)
		return;

	g_clear_signal_handler (&self->app_notify_handler_content_rating, self->app);
	g_clear_signal_handler (&self->app_notify_handler_name, self->app);

	g_set_object (&self->app, app);

	if (self->app != NULL) {
		self->app_notify_handler_content_rating = g_signal_connect (self->app, "notify::content-rating", G_CALLBACK (app_notify_cb), self);
		self->app_notify_handler_name = g_signal_connect (self->app, "notify::name", G_CALLBACK (app_notify_cb), self);
	}

	/* Update the UI. */
	update_attributes_list (self);

	g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_APP]);
}
