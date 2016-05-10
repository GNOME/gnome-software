 /* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Canonical Ltd.
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#ifndef __GS_REVIEW_H
#define __GS_REVIEW_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GS_TYPE_REVIEW (gs_review_get_type ())

G_DECLARE_FINAL_TYPE (GsReview, gs_review, GS, REVIEW, GObject)

/**
 * GsReviewAction:
 * @GS_REVIEW_ACTION_SUBMIT:	Submit a new review
 * @GS_REVIEW_ACTION_UPVOTE:	Upvote an existing review
 * @GS_REVIEW_ACTION_DOWNVOTE:	Downvote an existing review
 * @GS_REVIEW_ACTION_REPORT:	Report an existing review
 * @GS_REVIEW_ACTION_REMOVE:	Remove a review written by the user
 * @GS_REVIEW_ACTION_DISMISS:	Dismiss (ignore) a review when moderating
 *
 * The review action.
 **/
typedef enum {
	GS_REVIEW_ACTION_SUBMIT,
	GS_REVIEW_ACTION_UPVOTE,
	GS_REVIEW_ACTION_DOWNVOTE,
	GS_REVIEW_ACTION_REPORT,
	GS_REVIEW_ACTION_REMOVE,
	GS_REVIEW_ACTION_DISMISS,
	/*< private >*/
	GS_REVIEW_ACTION_LAST
} GsReviewAction;

/**
 * GsReviewFlags:
 * @GS_REVIEW_FLAG_NONE:	No special flags set
 * @GS_REVIEW_FLAG_SELF:	The user wrote the review themselves
 * @GS_REVIEW_FLAG_VOTED:	The user voted on the review
 *
 * The flags for the review.
 **/
typedef enum {
	GS_REVIEW_FLAG_NONE	= 0,
	GS_REVIEW_FLAG_SELF	= 1 << 0,
	GS_REVIEW_FLAG_VOTED	= 1 << 1,
	/*< private >*/
	GS_REVIEW_FLAG_LAST
} GsReviewFlags;

GsReview	*gs_review_new				(void);

gint		 gs_review_get_karma			(GsReview	*review);
void		 gs_review_set_karma			(GsReview	*review,
							 gint		 karma);

gint		 gs_review_get_score			(GsReview	*review);
void		 gs_review_set_score			(GsReview	*review,
							 gint		 score);

const gchar	*gs_review_get_summary			(GsReview	*review);
void		 gs_review_set_summary			(GsReview	*review,
							 const gchar	*summary);

const gchar	*gs_review_get_text			(GsReview	*review);
void		 gs_review_set_text			(GsReview	*review,
							 const gchar	*text);

gint		 gs_review_get_rating			(GsReview	*review);
void		 gs_review_set_rating			(GsReview	*review,
							 gint		 rating);

const gchar	*gs_review_get_version			(GsReview	*review);
void		 gs_review_set_version			(GsReview	*review,
							 const gchar	*version);

const gchar	*gs_review_get_reviewer			(GsReview	*review);
void		 gs_review_set_reviewer			(GsReview	*review,
							 const gchar	*reviewer);

GDateTime	*gs_review_get_date			(GsReview	*review);
void		 gs_review_set_date			(GsReview	*review,
							 GDateTime	*date);

GsReviewFlags	 gs_review_get_flags			(GsReview	*review);
void		 gs_review_set_flags			(GsReview	*review,
							 GsReviewFlags	 flags);
void		 gs_review_add_flags			(GsReview	*review,
							 GsReviewFlags	 flags);

const gchar	*gs_review_get_metadata_item		(GsReview	*review,
							 const gchar	*key);
void		 gs_review_add_metadata			(GsReview	*review,
							 const gchar	*key,
							 const gchar	*value);

G_END_DECLS

#endif /* __GS_REVIEW_H */

/* vim: set noexpandtab: */
