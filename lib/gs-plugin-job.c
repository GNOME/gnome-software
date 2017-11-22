/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
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

#include <glib.h>

#include "gs-plugin-private.h"
#include "gs-plugin-job-private.h"

struct _GsPluginJob
{
	GObject			 parent_instance;
	GsPluginRefineFlags	 refine_flags;
	GsPluginRefineFlags	 filter_flags;
	gboolean		 interactive;
	guint			 max_results;
	guint			 timeout;
	guint64			 age;
	GsPlugin		*plugin;
	GsPluginAction		 action;
	GsAppListSortFunc	 sort_func;
	gpointer		 sort_func_data;
	gchar			*search;
	GsAuth			*auth;
	GsApp			*app;
	GsAppList		*list;
	GFile			*file;
	GsCategory		*category;
	AsReview		*review;
	GsPrice			*price;
	GsChannel		*channel;
	gint64			 time_created;
};

enum {
	PROP_0,
	PROP_ACTION,
	PROP_AGE,
	PROP_SEARCH,
	PROP_REFINE_FLAGS,
	PROP_FILTER_FLAGS,
	PROP_INTERACTIVE,
	PROP_AUTH,
	PROP_APP,
	PROP_LIST,
	PROP_FILE,
	PROP_CATEGORY,
	PROP_REVIEW,
	PROP_MAX_RESULTS,
	PROP_PRICE,
	PROP_CHANNEL,
	PROP_TIMEOUT,
	PROP_LAST
};

G_DEFINE_TYPE (GsPluginJob, gs_plugin_job, G_TYPE_OBJECT)

gchar *
gs_plugin_job_to_string (GsPluginJob *self)
{
	GString *str = g_string_new (NULL);
	gint64 time_now = g_get_monotonic_time ();
	g_string_append_printf (str, "running %s",
				gs_plugin_action_to_string (self->action));
	if (self->filter_flags > 0) {
		g_autofree gchar *tmp = gs_plugin_refine_flags_to_string (self->filter_flags);
		g_string_append_printf (str, " with filter-flags=%s", tmp);
	}
	if (self->refine_flags > 0) {
		g_autofree gchar *tmp = gs_plugin_refine_flags_to_string (self->refine_flags);
		g_string_append_printf (str, " with refine-flags=%s", tmp);
	}
	if (self->interactive)
		g_string_append_printf (str, " with interactive=True");
	if (self->timeout > 0)
		g_string_append_printf (str, " with timeout=%u", self->timeout);
	if (self->max_results > 0)
		g_string_append_printf (str, " with max-results=%u", self->max_results);
	if (self->age != 0) {
		if (self->age == G_MAXUINT) {
			g_string_append (str, " with cache age=any");
		} else {
			g_string_append_printf (str, " with cache age=%" G_GUINT64_FORMAT,
						self->age);
		}
	}
	if (self->search != NULL) {
		g_string_append_printf (str, " with search=%s",
					self->search);
	}
	if (self->category != NULL) {
		GsCategory *parent = gs_category_get_parent (self->category);
		if (parent != NULL) {
			g_string_append_printf (str, " with category=%s/%s",
						gs_category_get_id (parent),
						gs_category_get_id (self->category));
		} else {
			g_string_append_printf (str, " with category=%s",
						gs_category_get_id (self->category));
		}
	}
	if (self->review != NULL) {
		g_string_append_printf (str, " with review=%s",
					as_review_get_id (self->review));
	}
	if (self->price != NULL) {
		g_autofree gchar *price_string = gs_price_to_string (self->price);
		g_string_append_printf (str, " with price=%s", price_string);
	}
	if (self->channel != NULL) {
		g_string_append_printf (str, " with channel=%s", gs_channel_get_name (self->channel));
	}
	if (self->auth != NULL) {
		g_string_append_printf (str, " with auth=%s",
					gs_auth_get_provider_id (self->auth));
	}
	if (self->file != NULL) {
		g_autofree gchar *path = g_file_get_path (self->file);
		g_string_append_printf (str, " with file=%s", path);
	}
	if (self->plugin != NULL) {
		g_string_append_printf (str, " on plugin=%s",
					gs_plugin_get_name (self->plugin));
	}
	if (self->list != NULL && gs_app_list_length (self->list) > 0) {
		g_autofree const gchar **unique_ids = NULL;
		g_autofree gchar *unique_ids_str = NULL;
		unique_ids = g_new0 (const gchar *, gs_app_list_length (self->list) + 1);
		for (guint i = 0; i < gs_app_list_length (self->list); i++) {
			GsApp *app = gs_app_list_index (self->list, i);
			unique_ids[i] = gs_app_get_unique_id (app);
		}
		unique_ids_str = g_strjoinv (",", (gchar**) unique_ids);
		g_string_append_printf (str, " on apps %s", unique_ids_str);
	}
	if (time_now - self->time_created > 1000) {
		g_string_append_printf (str, " took %" G_GINT64_FORMAT "ms",
					(time_now - self->time_created) / 1000);
	}
	return g_string_free (str, FALSE);
}

void
gs_plugin_job_set_refine_flags (GsPluginJob *self, GsPluginRefineFlags refine_flags)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	self->refine_flags = refine_flags;
}

void
gs_plugin_job_set_filter_flags (GsPluginJob *self, GsPluginRefineFlags filter_flags)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	self->filter_flags = filter_flags;
}

GsPluginRefineFlags
gs_plugin_job_get_refine_flags (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), 0);
	return self->refine_flags;
}

GsPluginRefineFlags
gs_plugin_job_get_filter_flags (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), 0);
	return self->filter_flags;
}

gboolean
gs_plugin_job_has_refine_flags (GsPluginJob *self, GsPluginRefineFlags refine_flags)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), FALSE);
	return (self->refine_flags & refine_flags) > 0;
}

void
gs_plugin_job_add_refine_flags (GsPluginJob *self, GsPluginRefineFlags refine_flags)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	self->refine_flags |= refine_flags;
}

void
gs_plugin_job_remove_refine_flags (GsPluginJob *self, GsPluginRefineFlags refine_flags)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	self->refine_flags &= ~refine_flags;
}

void
gs_plugin_job_set_interactive (GsPluginJob *self, gboolean interactive)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	self->interactive = interactive;
}

gboolean
gs_plugin_job_get_interactive (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), FALSE);
	return self->interactive;
}

void
gs_plugin_job_set_max_results (GsPluginJob *self, guint max_results)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	self->max_results = max_results;
}

guint
gs_plugin_job_get_max_results (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), 0);
	return self->max_results;
}

void
gs_plugin_job_set_timeout (GsPluginJob *self, guint timeout)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	self->timeout = timeout;
}

guint
gs_plugin_job_get_timeout (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), 0);
	return self->timeout;
}

void
gs_plugin_job_set_age (GsPluginJob *self, guint64 age)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	self->age = age;
}

guint64
gs_plugin_job_get_age (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), 0);
	return self->age;
}

void
gs_plugin_job_set_action (GsPluginJob *self, GsPluginAction action)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	self->action = action;
}

GsPluginAction
gs_plugin_job_get_action (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), 0);
	return self->action;
}

void
gs_plugin_job_set_sort_func (GsPluginJob *self, GsAppListSortFunc sort_func)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	self->sort_func = sort_func;
}

GsAppListSortFunc
gs_plugin_job_get_sort_func (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), 0);
	return self->sort_func;
}

void
gs_plugin_job_set_sort_func_data (GsPluginJob *self, gpointer sort_func_data)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	self->sort_func_data = sort_func_data;
}

gpointer
gs_plugin_job_get_sort_func_data (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return self->sort_func_data;
}

void
gs_plugin_job_set_search (GsPluginJob *self, const gchar *search)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_free (self->search);
	self->search = g_strdup (search);
}

const gchar *
gs_plugin_job_get_search (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return self->search;
}

void
gs_plugin_job_set_auth (GsPluginJob *self, GsAuth *auth)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_set_object (&self->auth, auth);
}

GsAuth *
gs_plugin_job_get_auth (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return self->auth;
}

void
gs_plugin_job_set_app (GsPluginJob *self, GsApp *app)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_set_object (&self->app, app);

	/* ensure we can always operate on a list object */
	if (self->list != NULL && app != NULL && gs_app_list_length (self->list) == 0)
		gs_app_list_add (self->list, self->app);
}

GsApp *
gs_plugin_job_get_app (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return self->app;
}

void
gs_plugin_job_set_list (GsPluginJob *self, GsAppList *list)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	if (list == NULL)
		g_warning ("trying to set list to NULL, not a good idea");
	g_set_object (&self->list, list);
}

GsAppList *
gs_plugin_job_get_list (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return self->list;
}

void
gs_plugin_job_set_file (GsPluginJob *self, GFile *file)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_set_object (&self->file, file);
}

GFile *
gs_plugin_job_get_file (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return self->file;
}

void
gs_plugin_job_set_plugin (GsPluginJob *self, GsPlugin *plugin)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_set_object (&self->plugin, plugin);
}

GsPlugin *
gs_plugin_job_get_plugin (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return self->plugin;
}

void
gs_plugin_job_set_category (GsPluginJob *self, GsCategory *category)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_set_object (&self->category, category);
}

GsCategory *
gs_plugin_job_get_category (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return self->category;
}

void
gs_plugin_job_set_review (GsPluginJob *self, AsReview *review)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_set_object (&self->review, review);
}

AsReview *
gs_plugin_job_get_review (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return self->review;
}

void
gs_plugin_job_set_price (GsPluginJob *self, GsPrice *price)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_set_object (&self->price, price);
}

GsPrice *
gs_plugin_job_get_price (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return self->price;
}

void
gs_plugin_job_set_channel (GsPluginJob *self, GsChannel *channel)
{
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_set_object (&self->channel, channel);
}

GsChannel *
gs_plugin_job_get_channel (GsPluginJob *self)
{
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return self->channel;
}

static void
gs_plugin_job_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsPluginJob *self = GS_PLUGIN_JOB (obj);

	switch (prop_id) {
	case PROP_ACTION:
		g_value_set_uint (value, self->action);
		break;
	case PROP_AGE:
		g_value_set_uint64 (value, self->age);
		break;
	case PROP_REFINE_FLAGS:
		g_value_set_uint64 (value, self->refine_flags);
		break;
	case PROP_FILTER_FLAGS:
		g_value_set_uint64 (value, self->filter_flags);
		break;
	case PROP_INTERACTIVE:
		g_value_set_boolean (value, self->interactive);
		break;
	case PROP_SEARCH:
		g_value_set_string (value, self->search);
		break;
	case PROP_AUTH:
		g_value_set_object (value, self->auth);
		break;
	case PROP_APP:
		g_value_set_object (value, self->app);
		break;
	case PROP_LIST:
		g_value_set_object (value, self->list);
		break;
	case PROP_FILE:
		g_value_set_object (value, self->file);
		break;
	case PROP_CATEGORY:
		g_value_set_object (value, self->category);
		break;
	case PROP_REVIEW:
		g_value_set_object (value, self->review);
		break;
	case PROP_PRICE:
		g_value_set_object (value, self->price);
		break;
	case PROP_CHANNEL:
		g_value_set_object (value, self->channel);
		break;
	case PROP_MAX_RESULTS:
		g_value_set_uint (value, self->max_results);
		break;
	case PROP_TIMEOUT:
		g_value_set_uint (value, self->timeout);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_job_set_property (GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsPluginJob *self = GS_PLUGIN_JOB (obj);

	switch (prop_id) {
	case PROP_ACTION:
		gs_plugin_job_set_action (self, g_value_get_uint (value));
		break;
	case PROP_AGE:
		gs_plugin_job_set_age (self, g_value_get_uint64 (value));
		break;
	case PROP_REFINE_FLAGS:
		gs_plugin_job_set_refine_flags (self, g_value_get_uint64 (value));
		break;
	case PROP_FILTER_FLAGS:
		gs_plugin_job_set_filter_flags (self, g_value_get_uint64 (value));
		break;
	case PROP_INTERACTIVE:
		gs_plugin_job_set_interactive (self, g_value_get_boolean (value));
		break;
	case PROP_SEARCH:
		gs_plugin_job_set_search (self, g_value_get_string (value));
		break;
	case PROP_AUTH:
		gs_plugin_job_set_auth (self, g_value_get_object (value));
		break;
	case PROP_APP:
		gs_plugin_job_set_app (self, g_value_get_object (value));
		break;
	case PROP_LIST:
		gs_plugin_job_set_list (self, g_value_get_object (value));
		break;
	case PROP_FILE:
		gs_plugin_job_set_file (self, g_value_get_object (value));
		break;
	case PROP_CATEGORY:
		gs_plugin_job_set_category (self, g_value_get_object (value));
		break;
	case PROP_REVIEW:
		gs_plugin_job_set_review (self, g_value_get_object (value));
		break;
	case PROP_MAX_RESULTS:
		gs_plugin_job_set_max_results (self, g_value_get_uint (value));
		break;
	case PROP_TIMEOUT:
		gs_plugin_job_set_timeout (self, g_value_get_uint (value));
		break;
	case PROP_PRICE:
		gs_plugin_job_set_price (self, g_value_get_object (value));
		break;
	case PROP_CHANNEL:
		gs_plugin_job_set_channel (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
		break;
	}
}

static void
gs_plugin_job_finalize (GObject *obj)
{
	GsPluginJob *self = GS_PLUGIN_JOB (obj);
	g_free (self->search);
	g_clear_object (&self->auth);
	g_clear_object (&self->app);
	g_clear_object (&self->list);
	g_clear_object (&self->file);
	g_clear_object (&self->plugin);
	g_clear_object (&self->category);
	g_clear_object (&self->review);
	g_clear_object (&self->price);
	g_clear_object (&self->channel);
	G_OBJECT_CLASS (gs_plugin_job_parent_class)->finalize (obj);
}

static void
gs_plugin_job_class_init (GsPluginJobClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_plugin_job_finalize;
	object_class->get_property = gs_plugin_job_get_property;
	object_class->set_property = gs_plugin_job_set_property;

	pspec = g_param_spec_uint ("action", NULL, NULL,
				   GS_PLUGIN_ACTION_UNKNOWN,
				   GS_PLUGIN_ACTION_LAST,
				   GS_PLUGIN_ACTION_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ACTION, pspec);

	pspec = g_param_spec_uint64 ("age", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_AGE, pspec);

	pspec = g_param_spec_uint64 ("refine-flags", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_REFINE_FLAGS, pspec);

	pspec = g_param_spec_uint64 ("filter-flags", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_FILTER_FLAGS, pspec);

	pspec = g_param_spec_boolean ("interactive", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);

	g_object_class_install_property (object_class, PROP_INTERACTIVE, pspec);

	pspec = g_param_spec_string ("search", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SEARCH, pspec);

	pspec = g_param_spec_object ("auth", NULL, NULL,
				     GS_TYPE_AUTH,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_AUTH, pspec);

	pspec = g_param_spec_object ("app", NULL, NULL,
				     GS_TYPE_APP,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_APP, pspec);

	pspec = g_param_spec_object ("list", NULL, NULL,
				     GS_TYPE_APP_LIST,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_LIST, pspec);

	pspec = g_param_spec_object ("file", NULL, NULL,
				     G_TYPE_FILE,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_FILE, pspec);

	pspec = g_param_spec_object ("category", NULL, NULL,
				     GS_TYPE_CATEGORY,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_CATEGORY, pspec);

	pspec = g_param_spec_object ("review", NULL, NULL,
				     AS_TYPE_REVIEW,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_REVIEW, pspec);

	pspec = g_param_spec_uint ("max-results", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_MAX_RESULTS, pspec);

	pspec = g_param_spec_uint ("timeout", NULL, NULL,
				   0, G_MAXUINT, 60,
				   G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_TIMEOUT, pspec);

	pspec = g_param_spec_object ("price", NULL, NULL,
				     GS_TYPE_PRICE,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PRICE, pspec);

	pspec = g_param_spec_object ("channel", NULL, NULL,
				     GS_TYPE_CHANNEL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_CHANNEL, pspec);
}

static void
gs_plugin_job_init (GsPluginJob *self)
{
	self->refine_flags = GS_PLUGIN_REFINE_FLAGS_DEFAULT;
	self->filter_flags = GS_PLUGIN_REFINE_FLAGS_DEFAULT;
	self->list = gs_app_list_new ();
	self->time_created = g_get_monotonic_time ();
}

/* vim: set noexpandtab: */
