/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib.h>

#include "gs-enums.h"
#include "gs-plugin-private.h"
#include "gs-plugin-job-private.h"

typedef struct
{
	GsPluginRefineFlags	 refine_flags;
	GsAppListFilterFlags	 dedupe_flags;
	gboolean		 interactive;
	gboolean		 propagate_error;
	guint			 max_results;
	guint			 timeout;
	guint64			 age;
	GsPlugin		*plugin;
	GsPluginAction		 action;
	GsAppListSortFunc	 sort_func;
	gpointer		 sort_func_data;
	gchar			*search;
	GsApp			*app;
	GsAppList		*list;
	GFile			*file;
	GsCategory		*category;
	gint64			 time_created;
} GsPluginJobPrivate;

enum {
	PROP_0,
	PROP_ACTION,
	PROP_AGE,
	PROP_SEARCH,
	PROP_REFINE_FLAGS,
	PROP_DEDUPE_FLAGS,
	PROP_INTERACTIVE,
	PROP_APP,
	PROP_LIST,
	PROP_FILE,
	PROP_CATEGORY,
	PROP_MAX_RESULTS,
	PROP_TIMEOUT,
	PROP_PROPAGATE_ERROR,
	PROP_LAST
};

G_DEFINE_TYPE_WITH_PRIVATE (GsPluginJob, gs_plugin_job, G_TYPE_OBJECT)

gchar *
gs_plugin_job_to_string (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	GString *str = g_string_new (NULL);
	gint64 time_now = g_get_monotonic_time ();
	g_string_append_printf (str, "running %s",
				gs_plugin_action_to_string (priv->action));
	if (priv->plugin != NULL) {
		g_string_append_printf (str, " on plugin=%s",
					gs_plugin_get_name (priv->plugin));
	}
	if (priv->dedupe_flags > 0)
		g_string_append_printf (str, " with dedupe-flags=%" G_GUINT64_FORMAT, priv->dedupe_flags);
	if (priv->refine_flags > 0) {
		g_autofree gchar *tmp = gs_plugin_refine_flags_to_string (priv->refine_flags);
		g_string_append_printf (str, " with refine-flags=%s", tmp);
	}
	if (priv->interactive)
		g_string_append_printf (str, " with interactive=True");
	if (priv->propagate_error)
		g_string_append_printf (str, " with propagate-error=True");
	if (priv->timeout > 0)
		g_string_append_printf (str, " with timeout=%u", priv->timeout);
	if (priv->max_results > 0)
		g_string_append_printf (str, " with max-results=%u", priv->max_results);
	if (priv->age != 0) {
		if (priv->age == G_MAXUINT) {
			g_string_append (str, " with cache age=any");
		} else {
			g_string_append_printf (str, " with cache age=%" G_GUINT64_FORMAT,
						priv->age);
		}
	}
	if (priv->search != NULL) {
		g_string_append_printf (str, " with search=%s",
					priv->search);
	}
	if (priv->category != NULL) {
		GsCategory *parent = gs_category_get_parent (priv->category);
		if (parent != NULL) {
			g_string_append_printf (str, " with category=%s/%s",
						gs_category_get_id (parent),
						gs_category_get_id (priv->category));
		} else {
			g_string_append_printf (str, " with category=%s",
						gs_category_get_id (priv->category));
		}
	}
	if (priv->file != NULL) {
		g_autofree gchar *path = g_file_get_path (priv->file);
		g_string_append_printf (str, " with file=%s", path);
	}
	if (priv->list != NULL && gs_app_list_length (priv->list) > 0) {
		g_autofree const gchar **unique_ids = NULL;
		g_autofree gchar *unique_ids_str = NULL;
		unique_ids = g_new0 (const gchar *, gs_app_list_length (priv->list) + 1);
		for (guint i = 0; i < gs_app_list_length (priv->list); i++) {
			GsApp *app = gs_app_list_index (priv->list, i);
			unique_ids[i] = gs_app_get_unique_id (app);
		}
		unique_ids_str = g_strjoinv (",", (gchar**) unique_ids);
		g_string_append_printf (str, " on apps %s", unique_ids_str);
	}
	if (time_now - priv->time_created > 1000) {
		g_string_append_printf (str, ", elapsed time since creation %" G_GINT64_FORMAT "ms",
					(time_now - priv->time_created) / 1000);
	}
	return g_string_free (str, FALSE);
}

void
gs_plugin_job_set_refine_flags (GsPluginJob *self, GsPluginRefineFlags refine_flags)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->refine_flags = refine_flags;
}

void
gs_plugin_job_set_dedupe_flags (GsPluginJob *self, GsAppListFilterFlags dedupe_flags)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->dedupe_flags = dedupe_flags;
}

GsPluginRefineFlags
gs_plugin_job_get_refine_flags (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), GS_PLUGIN_REFINE_FLAGS_NONE);
	return priv->refine_flags;
}

GsAppListFilterFlags
gs_plugin_job_get_dedupe_flags (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), GS_APP_LIST_FILTER_FLAG_NONE);
	return priv->dedupe_flags;
}

gboolean
gs_plugin_job_has_refine_flags (GsPluginJob *self, GsPluginRefineFlags refine_flags)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), FALSE);
	return (priv->refine_flags & refine_flags) > 0;
}

void
gs_plugin_job_add_refine_flags (GsPluginJob *self, GsPluginRefineFlags refine_flags)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->refine_flags |= refine_flags;
}

void
gs_plugin_job_remove_refine_flags (GsPluginJob *self, GsPluginRefineFlags refine_flags)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->refine_flags &= ~refine_flags;
}

void
gs_plugin_job_set_interactive (GsPluginJob *self, gboolean interactive)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->interactive = interactive;
}

gboolean
gs_plugin_job_get_interactive (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), FALSE);
	return priv->interactive;
}

void
gs_plugin_job_set_propagate_error (GsPluginJob *self, gboolean propagate_error)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->propagate_error = propagate_error;
}

gboolean
gs_plugin_job_get_propagate_error (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), FALSE);
	return priv->propagate_error;
}

void
gs_plugin_job_set_max_results (GsPluginJob *self, guint max_results)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->max_results = max_results;
}

guint
gs_plugin_job_get_max_results (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), 0);
	return priv->max_results;
}

void
gs_plugin_job_set_timeout (GsPluginJob *self, guint timeout)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->timeout = timeout;
}

guint
gs_plugin_job_get_timeout (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), 0);
	return priv->timeout;
}

void
gs_plugin_job_set_age (GsPluginJob *self, guint64 age)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->age = age;
}

guint64
gs_plugin_job_get_age (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), 0);
	return priv->age;
}

void
gs_plugin_job_set_action (GsPluginJob *self, GsPluginAction action)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->action = action;
}

GsPluginAction
gs_plugin_job_get_action (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), GS_PLUGIN_ACTION_UNKNOWN);
	return priv->action;
}

void
gs_plugin_job_set_sort_func (GsPluginJob *self, GsAppListSortFunc sort_func, gpointer user_data)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	priv->sort_func = sort_func;
	priv->sort_func_data = user_data;
}

GsAppListSortFunc
gs_plugin_job_get_sort_func (GsPluginJob *self, gpointer *user_data_out)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	if (user_data_out != NULL)
		*user_data_out = priv->sort_func_data;
	return priv->sort_func;
}

void
gs_plugin_job_set_search (GsPluginJob *self, const gchar *search)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_free (priv->search);
	priv->search = g_strdup (search);
}

const gchar *
gs_plugin_job_get_search (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return priv->search;
}

void
gs_plugin_job_set_app (GsPluginJob *self, GsApp *app)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_set_object (&priv->app, app);

	/* ensure we can always operate on a list object */
	if (priv->list != NULL && app != NULL && gs_app_list_length (priv->list) == 0)
		gs_app_list_add (priv->list, priv->app);
}

GsApp *
gs_plugin_job_get_app (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return priv->app;
}

void
gs_plugin_job_set_list (GsPluginJob *self, GsAppList *list)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	if (list == NULL)
		g_warning ("trying to set list to NULL, not a good idea");
	g_set_object (&priv->list, list);
}

GsAppList *
gs_plugin_job_get_list (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return priv->list;
}

void
gs_plugin_job_set_file (GsPluginJob *self, GFile *file)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_set_object (&priv->file, file);
}

GFile *
gs_plugin_job_get_file (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return priv->file;
}

void
gs_plugin_job_set_plugin (GsPluginJob *self, GsPlugin *plugin)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_set_object (&priv->plugin, plugin);
}

GsPlugin *
gs_plugin_job_get_plugin (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return priv->plugin;
}

void
gs_plugin_job_set_category (GsPluginJob *self, GsCategory *category)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_if_fail (GS_IS_PLUGIN_JOB (self));
	g_set_object (&priv->category, category);
}

GsCategory *
gs_plugin_job_get_category (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PLUGIN_JOB (self), NULL);
	return priv->category;
}

static void
gs_plugin_job_get_property (GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsPluginJob *self = GS_PLUGIN_JOB (obj);
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);

	switch (prop_id) {
	case PROP_ACTION:
		g_value_set_enum (value, priv->action);
		break;
	case PROP_AGE:
		g_value_set_uint64 (value, priv->age);
		break;
	case PROP_REFINE_FLAGS:
		g_value_set_flags (value, priv->refine_flags);
		break;
	case PROP_DEDUPE_FLAGS:
		g_value_set_flags (value, priv->dedupe_flags);
		break;
	case PROP_INTERACTIVE:
		g_value_set_boolean (value, priv->interactive);
		break;
	case PROP_SEARCH:
		g_value_set_string (value, priv->search);
		break;
	case PROP_APP:
		g_value_set_object (value, priv->app);
		break;
	case PROP_LIST:
		g_value_set_object (value, priv->list);
		break;
	case PROP_FILE:
		g_value_set_object (value, priv->file);
		break;
	case PROP_CATEGORY:
		g_value_set_object (value, priv->category);
		break;
	case PROP_MAX_RESULTS:
		g_value_set_uint (value, priv->max_results);
		break;
	case PROP_TIMEOUT:
		g_value_set_uint (value, priv->timeout);
		break;
	case PROP_PROPAGATE_ERROR:
		g_value_set_boolean (value, priv->propagate_error);
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
		gs_plugin_job_set_action (self, g_value_get_enum (value));
		break;
	case PROP_AGE:
		gs_plugin_job_set_age (self, g_value_get_uint64 (value));
		break;
	case PROP_REFINE_FLAGS:
		gs_plugin_job_set_refine_flags (self, g_value_get_flags (value));
		break;
	case PROP_DEDUPE_FLAGS:
		gs_plugin_job_set_dedupe_flags (self, g_value_get_flags (value));
		break;
	case PROP_INTERACTIVE:
		gs_plugin_job_set_interactive (self, g_value_get_boolean (value));
		break;
	case PROP_SEARCH:
		gs_plugin_job_set_search (self, g_value_get_string (value));
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
	case PROP_MAX_RESULTS:
		gs_plugin_job_set_max_results (self, g_value_get_uint (value));
		break;
	case PROP_TIMEOUT:
		gs_plugin_job_set_timeout (self, g_value_get_uint (value));
		break;
	case PROP_PROPAGATE_ERROR:
		gs_plugin_job_set_propagate_error (self, g_value_get_boolean (value));
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
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);

	g_free (priv->search);
	g_clear_object (&priv->app);
	g_clear_object (&priv->list);
	g_clear_object (&priv->file);
	g_clear_object (&priv->plugin);
	g_clear_object (&priv->category);

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

	pspec = g_param_spec_enum ("action", NULL, NULL,
				   GS_TYPE_PLUGIN_ACTION, GS_PLUGIN_ACTION_UNKNOWN,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ACTION, pspec);

	pspec = g_param_spec_uint64 ("age", NULL, NULL,
				     0, G_MAXUINT64, 0,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_AGE, pspec);

	pspec = g_param_spec_flags ("refine-flags", NULL, NULL,
				    GS_TYPE_PLUGIN_REFINE_FLAGS, GS_PLUGIN_REFINE_FLAGS_NONE,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_REFINE_FLAGS, pspec);

	pspec = g_param_spec_flags ("dedupe-flags", NULL, NULL,
				    GS_TYPE_APP_LIST_FILTER_FLAGS, GS_APP_LIST_FILTER_FLAG_NONE,
				    G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_DEDUPE_FLAGS, pspec);

	pspec = g_param_spec_boolean ("interactive", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);

	g_object_class_install_property (object_class, PROP_INTERACTIVE, pspec);

	pspec = g_param_spec_string ("search", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SEARCH, pspec);

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

	pspec = g_param_spec_uint ("max-results", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_MAX_RESULTS, pspec);

	pspec = g_param_spec_uint ("timeout", NULL, NULL,
				   0, G_MAXUINT, 60,
				   G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
	g_object_class_install_property (object_class, PROP_TIMEOUT, pspec);

	pspec = g_param_spec_boolean ("propagate-error", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_PROPAGATE_ERROR, pspec);
}

static void
gs_plugin_job_init (GsPluginJob *self)
{
	GsPluginJobPrivate *priv = gs_plugin_job_get_instance_private (self);

	priv->refine_flags = GS_PLUGIN_REFINE_FLAGS_NONE;
	priv->dedupe_flags = GS_APP_LIST_FILTER_FLAG_KEY_ID |
			     GS_APP_LIST_FILTER_FLAG_KEY_SOURCE |
			     GS_APP_LIST_FILTER_FLAG_KEY_VERSION;
	priv->list = gs_app_list_new ();
	priv->time_created = g_get_monotonic_time ();
}
