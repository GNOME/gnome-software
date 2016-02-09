/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Canonical Ltd.
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

#include "gs-review.h"

struct _GsReview
{
	GObject			 parent_instance;

	GsReviewState		 state;
	gchar			*summary;
	gchar			*text;
	gint			 karma;
	gint			 score;
	gint			 rating;
	gchar			*version;
	gchar			*reviewer;
	GDateTime		*date;
};

enum {
	PROP_0,
	PROP_KARMA,
	PROP_SUMMARY,
	PROP_TEXT,
	PROP_RATING,
	PROP_VERSION,
	PROP_REVIEWER,
	PROP_DATE,
	PROP_STATE,
	PROP_LAST
};

G_DEFINE_TYPE (GsReview, gs_review, G_TYPE_OBJECT)

/**
 * gs_review_get_karma:
 */
gint
gs_review_get_karma (GsReview *review)
{
	g_return_val_if_fail (GS_IS_REVIEW (review), 0);
	return review->karma;
}

/**
 * gs_review_set_karma:
 */
void
gs_review_set_karma (GsReview *review, gint karma)
{
	g_return_if_fail (GS_IS_REVIEW (review));
	review->karma = karma;
}

/**
 * gs_review_get_score:
 *
 * This allows the UI to sort reviews into the correct order.
 * Higher numbers indicate a more important or relevant review.
 */
gint
gs_review_get_score (GsReview *review)
{
	g_return_val_if_fail (GS_IS_REVIEW (review), 0);
	return review->score;
}

/**
 * gs_review_set_score:
 */
void
gs_review_set_score (GsReview *review, gint score)
{
	g_return_if_fail (GS_IS_REVIEW (review));
	review->score = score;
}

/**
 * gs_review_get_summary:
 */
const gchar *
gs_review_get_summary (GsReview *review)
{
	g_return_val_if_fail (GS_IS_REVIEW (review), NULL);
	return review->summary;
}

/**
 * gs_review_set_summary:
 */
void
gs_review_set_summary (GsReview *review, const gchar *summary)
{
	g_return_if_fail (GS_IS_REVIEW (review));
	g_free (review->summary);
	review->summary = g_strdup (summary);
}

/**
 * gs_review_get_text:
 **/
const gchar *
gs_review_get_text (GsReview *review)
{
	g_return_val_if_fail (GS_IS_REVIEW (review), NULL);
	return review->text;
}

/**
 * gs_review_set_text:
 */
void
gs_review_set_text (GsReview *review, const gchar *text)
{
	g_return_if_fail (GS_IS_REVIEW (review));
	g_free (review->text);
	review->text = g_strdup (text);
}

/**
 * gs_review_get_rating:
 */
gint
gs_review_get_rating (GsReview *review)
{
	g_return_val_if_fail (GS_IS_REVIEW (review), 0);
	return review->rating;
}

/**
 * gs_review_set_rating:
 */
void
gs_review_set_rating (GsReview *review, gint rating)
{
	g_return_if_fail (GS_IS_REVIEW (review));
	review->rating = rating;
}

/**
 * gs_review_get_state:
 */
GsReviewState
gs_review_get_state (GsReview *review)
{
	g_return_val_if_fail (GS_IS_REVIEW (review), 0);
	return review->state;
}

/**
 * gs_review_set_state:
 */
void
gs_review_set_state (GsReview *review, GsReviewState state)
{
	g_return_if_fail (GS_IS_REVIEW (review));
	review->state = state;
}

/**
 * gs_review_get_reviewer:
 **/
const gchar *
gs_review_get_reviewer (GsReview *review)
{
	g_return_val_if_fail (GS_IS_REVIEW (review), NULL);
	return review->reviewer;
}

/**
 * gs_review_set_version:
 */
void
gs_review_set_version (GsReview *review, const gchar *version)
{
	g_return_if_fail (GS_IS_REVIEW (review));
	g_free (review->version);
	review->version = g_strdup (version);
}

/**
 * gs_review_get_version:
 **/
const gchar *
gs_review_get_version (GsReview *review)
{
	g_return_val_if_fail (GS_IS_REVIEW (review), NULL);
	return review->version;
}

/**
 * gs_review_set_reviewer:
 */
void
gs_review_set_reviewer (GsReview *review, const gchar *reviewer)
{
	g_return_if_fail (GS_IS_REVIEW (review));
	g_free (review->reviewer);
	review->reviewer = g_strdup (reviewer);
}

/**
 * gs_review_get_date:
 **/
GDateTime *
gs_review_get_date (GsReview *review)
{
	g_return_val_if_fail (GS_IS_REVIEW (review), NULL);
	return review->date;
}

/**
 * gs_review_set_date:
 */
void
gs_review_set_date (GsReview *review, GDateTime *date)
{
	g_return_if_fail (GS_IS_REVIEW (review));
	g_clear_pointer (&review->date, g_date_time_unref);
	if (date != NULL)
		review->date = g_date_time_ref (date);
}

static void
gs_review_get_property (GObject *object, guint prop_id,
			GValue *value, GParamSpec *pspec)
{
	GsReview *review = GS_REVIEW (object);

	switch (prop_id) {
	case PROP_KARMA:
		g_value_set_int (value, review->karma);
		break;
	case PROP_SUMMARY:
		g_value_set_string (value, review->summary);
		break;
	case PROP_TEXT:
		g_value_set_string (value, review->text);
		break;
	case PROP_RATING:
		g_value_set_int (value, review->rating);
		break;
	case PROP_STATE:
		g_value_set_uint64 (value, review->state);
		break;
	case PROP_VERSION:
		g_value_set_string (value, review->version);
		break;
	case PROP_REVIEWER:
		g_value_set_string (value, review->reviewer);
		break;
	case PROP_DATE:
		g_value_set_object (value, review->date);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_review_set_property (GObject *object, guint prop_id,
			const GValue *value, GParamSpec *pspec)
{
	GsReview *review = GS_REVIEW (object);

	switch (prop_id) {
	case PROP_KARMA:
		gs_review_set_karma (review, g_value_get_int (value));
		break;
	case PROP_SUMMARY:
		gs_review_set_summary (review, g_value_get_string (value));
		break;
	case PROP_TEXT:
		gs_review_set_text (review, g_value_get_string (value));
		break;
	case PROP_RATING:
		gs_review_set_rating (review, g_value_get_int (value));
		break;
	case PROP_STATE:
		gs_review_set_state (review, g_value_get_uint64 (value));
		break;
	case PROP_VERSION:
		gs_review_set_version (review, g_value_get_string (value));
		break;
	case PROP_REVIEWER:
		gs_review_set_reviewer (review, g_value_get_string (value));
		break;
	case PROP_DATE:
		gs_review_set_date (review, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_review_dispose (GObject *object)
{
	GsReview *review = GS_REVIEW (object);

	g_clear_pointer (&review->date, g_date_time_unref);

	G_OBJECT_CLASS (gs_review_parent_class)->dispose (object);
}

static void
gs_review_finalize (GObject *object)
{
	GsReview *review = GS_REVIEW (object);

	g_free (review->summary);
	g_free (review->text);
	g_free (review->reviewer);

	G_OBJECT_CLASS (gs_review_parent_class)->finalize (object);
}

static void
gs_review_class_init (GsReviewClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = gs_review_dispose;
	object_class->finalize = gs_review_finalize;
	object_class->get_property = gs_review_get_property;
	object_class->set_property = gs_review_set_property;

	/**
	 * GsApp:karma:
	 */
	pspec = g_param_spec_int ("karma", NULL, NULL,
				  G_MININT, G_MAXINT, 0,
				  G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_KARMA, pspec);

	/**
	 * GsApp:summary:
	 */
	pspec = g_param_spec_string ("summary", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_SUMMARY, pspec);

	/**
	 * GsApp:text:
	 */
	pspec = g_param_spec_string ("text", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_TEXT, pspec);

	/**
	 * GsApp:rating:
	 */
	pspec = g_param_spec_int ("rating", NULL, NULL,
				  -1, 100, -1,
				  G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_RATING, pspec);

	/**
	 * GsApp:state:
	 */
	pspec = g_param_spec_uint64 ("state", NULL, NULL,
				     GS_REVIEW_STATE_NONE,
				     GS_REVIEW_STATE_LAST,
				     GS_REVIEW_STATE_NONE,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_STATE, pspec);

	/**
	 * GsApp:version:
	 */
	pspec = g_param_spec_string ("version", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_VERSION, pspec);

	/**
	 * GsApp:reviewer:
	 */
	pspec = g_param_spec_string ("reviewer", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_REVIEWER, pspec);

	/**
	 * GsApp:date:
	 */
	pspec = g_param_spec_object ("date", NULL, NULL,
				     GS_TYPE_REVIEW,
				     G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_DATE, pspec);
}

static void
gs_review_init (GsReview *review)
{
	review->rating = -1;
}

/**
 * gs_review_new:
 *
 * Return value: a new #GsReview object.
 **/
GsReview *
gs_review_new (void)
{
	GsReview *review;
	review = g_object_new (GS_TYPE_REVIEW, NULL);
	return GS_REVIEW (review);
}

/* vim: set noexpandtab: */
