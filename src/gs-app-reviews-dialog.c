/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) Adrien Plazas <adrien.plazas@puri.sm>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include "gs-app-reviews-dialog.h"

#include "gnome-software-private.h"
#include "gs-common.h"
#include "gs-review-row.h"
#include <glib/gi18n.h>

struct _GsAppReviewsDialog
{
	AdwDialog	 parent_instance;
	GtkWidget       *toast_overlay;
	GtkWidget	*listbox;
	GtkWidget	*stack;

	GsPluginLoader	*plugin_loader;  /* (owned) (nullable) */
	GsApp		*app;  /* (owned) (nullable) */
	GCancellable	*cancellable;  /* (owned) */
	GCancellable	*refine_cancellable;  /* (owned) (nullable) */
	GsOdrsProvider	*odrs_provider;  /* (nullable) (owned), NULL if reviews are disabled */
};

G_DEFINE_TYPE (GsAppReviewsDialog, gs_app_reviews_dialog, ADW_TYPE_DIALOG)

typedef enum {
	PROP_APP = 1,
	PROP_ODRS_PROVIDER,
	PROP_PLUGIN_LOADER,
} GsAppReviewsDialogProperty;

typedef struct {
	GsReviewRow        *row; /* (not nullable) (unowned) */
	GsAppReviewsDialog *dialog; /* (not nullable) (unowned) */
	GsReviewAction      action;
} AsyncReviewData;

G_DEFINE_AUTOPTR_CLEANUP_FUNC (AsyncReviewData, g_free)

static GParamSpec *obj_props[PROP_PLUGIN_LOADER + 1] = { NULL, };

enum {
	SIGNAL_REVIEWS_UPDATED,
	SIGNAL_LAST
};

static guint signals[SIGNAL_LAST] = { 0 };

static void refresh_reviews (GsAppReviewsDialog *self);

static void
display_error_toast (GsAppReviewsDialog *dialog,
                     const gchar *error_text)
{
	AdwToast *toast;

	g_return_if_fail (GS_IS_APP_REVIEWS_DIALOG (dialog));
	g_return_if_fail (error_text != NULL);

	toast = adw_toast_new (error_text);

	adw_toast_overlay_add_toast (ADW_TOAST_OVERLAY (dialog->toast_overlay), toast);
}

static gint
sort_reviews (AsReview **a, AsReview **b)
{
	/* User's review of the app should be displayed first */
	if (as_review_get_flags (*a) & AS_REVIEW_FLAG_SELF)
		return -1;
	if (as_review_get_flags (*b) & AS_REVIEW_FLAG_SELF)
		return 1;

	return -g_date_time_compare (as_review_get_date (*a), as_review_get_date (*b));
}

static void
review_action_completed_cb (GObject      *source_object,
                            GAsyncResult *result,
                            gpointer      user_data)
{
	GsOdrsProvider *odrs_provider = GS_ODRS_PROVIDER (source_object);
	g_autoptr(GError) local_error = NULL;
	g_autoptr(AsyncReviewData) data = g_steal_pointer (&user_data);
	gboolean success;

	/* enable review actions after action completion */
	gs_review_row_actions_set_sensitive (data->row, TRUE);

	if (g_cancellable_is_cancelled (g_task_get_cancellable (G_TASK (result))))
		return;

	switch (data->action) {
	case GS_REVIEW_ACTION_UPVOTE:
		success = gs_odrs_provider_upvote_review_finish (odrs_provider, result, &local_error);
		break;
	case GS_REVIEW_ACTION_DOWNVOTE:
		success = gs_odrs_provider_downvote_review_finish (odrs_provider, result, &local_error);
		break;
	case GS_REVIEW_ACTION_REPORT:
		success = gs_odrs_provider_report_review_finish (odrs_provider, result, &local_error);
		break;
	case GS_REVIEW_ACTION_REMOVE:
		success = gs_odrs_provider_remove_review_finish (odrs_provider, result, &local_error);
		/* update the local app */
		if (success) {
			gs_app_remove_review (data->dialog->app, gs_review_row_get_review (data->row));
			refresh_reviews (data->dialog);
		}
		break;
	default:
		g_assert_not_reached ();
	}

	if (!success) {
		const char *translatable_message;

		g_assert (local_error != NULL);

		g_warning ("failed to %s review on %s: %s",
			   gs_review_row_action_to_string (data->action),
			   gs_app_get_id (data->dialog->app),
			   local_error->message);

		if (g_error_matches (local_error, GS_ODRS_PROVIDER_ERROR,
				     GS_ODRS_PROVIDER_ERROR_PARSING_DATA)) {
			translatable_message = _("Invalid ratings data received from server");
		} else if (g_error_matches (local_error, GS_ODRS_PROVIDER_ERROR,
					    GS_ODRS_PROVIDER_ERROR_SERVER_ERROR)) {
			translatable_message = _("Could not communicate with ratings server");
		} else {
			/* likely a programming error in gnome-software, so don’t
			 * waste a translatable string on it */
			translatable_message = local_error->message;
		}

		display_error_toast (data->dialog, translatable_message);
		return;
	}
}

static void
review_button_clicked_cb (GsReviewRow        *row,
                          GsReviewAction      action,
                          GsAppReviewsDialog *self)
{
	AsReview *review = gs_review_row_get_review (row);
	g_autoptr(GError) local_error = NULL;
	g_autoptr(AsyncReviewData) data = g_new0 (AsyncReviewData, 1);

	g_assert (self->odrs_provider != NULL);

	data->row = row;
	data->dialog = self;
	data->action = action;

	/* avoid submitting duplicate requests */
	gs_review_row_actions_set_sensitive (row, FALSE);

	switch (action) {
	case GS_REVIEW_ACTION_UPVOTE:
		gs_odrs_provider_upvote_review_async (self->odrs_provider, self->app,
						      review, self->cancellable,
						      review_action_completed_cb,
						      g_steal_pointer (&data));

		return;
	case GS_REVIEW_ACTION_DOWNVOTE:
		gs_odrs_provider_downvote_review_async (self->odrs_provider, self->app,
							review, self->cancellable,
							review_action_completed_cb,
							g_steal_pointer (&data));

		return;
	case GS_REVIEW_ACTION_REPORT:
		gs_odrs_provider_report_review_async (self->odrs_provider, self->app,
						      review, self->cancellable,
						      review_action_completed_cb,
						      g_steal_pointer (&data));

		return;
	case GS_REVIEW_ACTION_REMOVE:
		gs_odrs_provider_remove_review_async (self->odrs_provider, self->app,
						      review, self->cancellable,
						      review_action_completed_cb,
						      g_steal_pointer (&data));

		return;
	default:
		g_assert_not_reached ();
	}
}

static GSList * /* (transfer container) */
gather_listbox_rows (GtkWidget *listbox)
{
	GSList *rows = NULL;
	GtkWidget *widget;

	widget = gtk_widget_get_first_child (listbox);
	while (widget) {
		rows = g_slist_prepend (rows, widget);
		widget = gtk_widget_get_next_sibling (widget);
	}

	return g_slist_reverse (rows);
}

static void
populate_reviews (GsAppReviewsDialog *self)
{
	GPtrArray *reviews;
	GSList *rows, *link;
	gboolean show_reviews = FALSE;
	guint64 possible_actions = 0;
	guint i;
	GsReviewAction all_actions[] = {
		GS_REVIEW_ACTION_UPVOTE,
		GS_REVIEW_ACTION_DOWNVOTE,
		GS_REVIEW_ACTION_REPORT,
		GS_REVIEW_ACTION_REMOVE,
	};

	/* nothing to show */
	if (self->app == NULL) {
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "empty");

		return;
	}

	/* show or hide the entire reviews section */
	switch (gs_app_get_kind (self->app)) {
	case AS_COMPONENT_KIND_DESKTOP_APP:
	case AS_COMPONENT_KIND_FONT:
	case AS_COMPONENT_KIND_INPUT_METHOD:
	case AS_COMPONENT_KIND_WEB_APP:
		/* don't show a missing rating on a local file */
		if (gs_app_get_state (self->app) != GS_APP_STATE_AVAILABLE_LOCAL &&
		    self->odrs_provider != NULL)
			show_reviews = TRUE;
		break;
	default:
		break;
	}

	/* some apps are unreviewable */
	if (gs_app_has_quirk (self->app, GS_APP_QUIRK_NOT_REVIEWABLE))
		show_reviews = FALSE;

	/* check that reviews are available */
	reviews = gs_app_get_reviews (self->app);
	if (reviews->len == 0)
		show_reviews = FALSE;

	if (!show_reviews) {
		gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "empty");

		return;
	}

	/* find what the plugins support */
	for (i = 0; i < G_N_ELEMENTS (all_actions); i++) {
		if (self->odrs_provider != NULL)
			possible_actions |= (1u << all_actions[i]);
	}

	/* add all the reviews */
	rows = gather_listbox_rows (self->listbox);
	g_ptr_array_sort (reviews, (GCompareFunc) sort_reviews);
	for (i = 0, link = rows; i < reviews->len; i++, link = g_slist_next (link)) {
		AsReview *review = g_ptr_array_index (reviews, i);
		GtkWidget *row = NULL;
		guint64 actions;

		/* Try to merge with existing rows, to preserve cursor (focused) row
		   and window scroll position. */
		if (link != NULL) {
			GtkWidget *existing_row = link->data;
			if (gs_review_row_get_review (GS_REVIEW_ROW (existing_row)) == review)
				row = existing_row;
			else
				gtk_list_box_remove (GTK_LIST_BOX (self->listbox), existing_row);
		}

		if (row == NULL) {
			row = gs_review_row_new (review);
			gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (row), FALSE);
			gtk_list_box_append (GTK_LIST_BOX (self->listbox), row);

			g_signal_connect (row, "button-clicked",
					  G_CALLBACK (review_button_clicked_cb), self);
		}

		if (as_review_get_flags (review) & AS_REVIEW_FLAG_SELF)
			actions = possible_actions & (1 << GS_REVIEW_ACTION_REMOVE);
		else
			actions = possible_actions & ~(1u << GS_REVIEW_ACTION_REMOVE);
		gs_review_row_set_actions (GS_REVIEW_ROW (row), actions);
		gs_review_row_actions_set_sensitive (GS_REVIEW_ROW (row),
						     GS_IS_PLUGIN_LOADER (self->plugin_loader) && gs_plugin_loader_get_network_available (self->plugin_loader));
	}

	while (link != NULL) {
		GtkWidget *existing_row = link->data;
		gtk_list_box_remove (GTK_LIST_BOX (self->listbox), existing_row);
		link = g_slist_next (link);
	}

	g_slist_free (rows);

	gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "reviews");
}

static void
refresh_reviews (GsAppReviewsDialog *self)
{
	if (!gtk_widget_get_realized (GTK_WIDGET (self)))
		return;

	populate_reviews (self);

	g_signal_emit (self, signals[SIGNAL_REVIEWS_UPDATED], 0);
}

static void
gs_app_reviews_dialog_app_refine_cb (GObject      *source,
                                     GAsyncResult *res,
                                     gpointer      user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsAppReviewsDialog *self = user_data;
	g_autoptr(GError) error = NULL;

	if (!gs_plugin_loader_job_process_finish (plugin_loader, res, NULL, &error)) {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) &&
		    !g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED)) {
			g_warning ("failed to refine %s: %s",
				   gs_app_get_id (self->app),
				   error->message);
		}
		return;
	}

	refresh_reviews (self);
}

static void
gs_app_reviews_dialog_app_refine (GsAppReviewsDialog *self)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;

	if (self->refine_cancellable != NULL) {
		g_cancellable_cancel (self->refine_cancellable);
		g_clear_object (&self->refine_cancellable);
	}

	if (self->plugin_loader == NULL || self->app == NULL)
		return;

	self->refine_cancellable = g_cancellable_new ();

	/* If this task fails (e.g. because we have no networking) then
	 * it's of no huge importance if we don't get the required data.
	 */
	plugin_job = gs_plugin_job_refine_new_for_app (self->app,
						       GS_PLUGIN_REFINE_FLAGS_INTERACTIVE,
						       GS_PLUGIN_REFINE_REQUIRE_FLAGS_RATING |
						       GS_PLUGIN_REFINE_REQUIRE_FLAGS_REVIEW_RATINGS |
						       GS_PLUGIN_REFINE_REQUIRE_FLAGS_REVIEWS |
						       GS_PLUGIN_REFINE_REQUIRE_FLAGS_SIZE);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->refine_cancellable,
					    gs_app_reviews_dialog_app_refine_cb,
					    self);
}

static void
gs_app_reviews_dialog_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
	GsAppReviewsDialog *self = GS_APP_REVIEWS_DIALOG (object);

	switch ((GsAppReviewsDialogProperty) prop_id) {
	case PROP_APP:
		g_value_set_object (value, gs_app_reviews_dialog_get_app (self));
		break;
	case PROP_ODRS_PROVIDER:
		g_value_set_object (value, gs_app_reviews_dialog_get_odrs_provider (self));
		break;
	case PROP_PLUGIN_LOADER:
		g_value_set_object (value, gs_app_reviews_dialog_get_plugin_loader (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_reviews_dialog_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
	GsAppReviewsDialog *self = GS_APP_REVIEWS_DIALOG (object);

	switch ((GsAppReviewsDialogProperty) prop_id) {
	case PROP_APP:
		gs_app_reviews_dialog_set_app (self, g_value_get_object (value));
		break;
	case PROP_ODRS_PROVIDER:
		gs_app_reviews_dialog_set_odrs_provider (self, g_value_get_object (value));
		break;
	case PROP_PLUGIN_LOADER:
		gs_app_reviews_dialog_set_plugin_loader (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_reviews_dialog_dispose (GObject *object)
{
	GsAppReviewsDialog *self = GS_APP_REVIEWS_DIALOG (object);

	g_cancellable_cancel (self->cancellable);
	g_clear_object (&self->cancellable);

	if (self->refine_cancellable != NULL) {
		g_cancellable_cancel (self->refine_cancellable);
		g_clear_object (&self->refine_cancellable);
	}

	if (self->plugin_loader)
		g_signal_handlers_disconnect_by_func (self->plugin_loader,
						      refresh_reviews, self);
	g_clear_object (&self->plugin_loader);

	g_clear_object (&self->app);
	g_clear_object (&self->odrs_provider);

	G_OBJECT_CLASS (gs_app_reviews_dialog_parent_class)->dispose (object);
}

static void
gs_app_reviews_dialog_class_init (GsAppReviewsDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_app_reviews_dialog_get_property;
	object_class->set_property = gs_app_reviews_dialog_set_property;
	object_class->dispose = gs_app_reviews_dialog_dispose;

	/**
	 * GsAppReviewsDialog:app: (nullable)
	 *
	 * An app whose reviews should be displayed.
	 *
	 * If this is %NULL, ratings and reviews will be disabled.
	 *
	 * Since: 42
	 */
	obj_props[PROP_APP] =
		g_param_spec_object ("app", NULL, NULL,
				     GS_TYPE_APP,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * GsAppReviewsDialog:odrs-provider: (nullable)
	 *
	 * An ODRS provider to give access to ratings and reviews information
	 * for the app being displayed.
	 *
	 * If this is %NULL, ratings and reviews will be disabled.
	 *
	 * Since: 42
	 */
	obj_props[PROP_ODRS_PROVIDER] =
		g_param_spec_object ("odrs-provider", NULL, NULL,
				     GS_TYPE_ODRS_PROVIDER,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	/**
	 * GsAppReviewsDialog:plugin-loader: (nullable)
	 *
	 * A plugin loader to provide network availability.
	 *
	 * If this is %NULL, ratings and reviews will be disabled.
	 *
	 * Since: 42
	 */
	obj_props[PROP_PLUGIN_LOADER] =
		g_param_spec_object ("plugin-loader", NULL, NULL,
				     GS_TYPE_PLUGIN_LOADER,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	/**
	 * GsAppReviewsDialog::reviews-updated:
	 *
	 * Emitted when reviews are updated.
	 *
	 * Since: 42
	 */
	signals[SIGNAL_REVIEWS_UPDATED] =
		g_signal_new ("reviews-updated",
		              G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
		              0, NULL, NULL, g_cclosure_marshal_generic,
		              G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-app-reviews-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsAppReviewsDialog, toast_overlay);
	gtk_widget_class_bind_template_child (widget_class, GsAppReviewsDialog, listbox);
	gtk_widget_class_bind_template_child (widget_class, GsAppReviewsDialog, stack);
}

static void
gs_app_reviews_dialog_init (GsAppReviewsDialog *self)
{
	self->cancellable = g_cancellable_new ();

	gtk_widget_init_template (GTK_WIDGET (self));

	g_signal_connect_swapped (self, "realize",
				  G_CALLBACK (refresh_reviews), self);
}

/**
 * gs_app_reviews_dialog_new:
 * @app: (nullable): a #GsApp, or %NULL
 * @odrs_provider: (nullable): a #GsOdrsProvider, or %NULL
 * @plugin_loader: (nullable): a #GsPluginLoader, or %NULL
 *
 * Create a new #GsAppReviewsDialog and set its initial app, ODRS
 * provider and plugin loader to @app, @odrs_provider and
 * @plugin_loader respectively.
 *
 * Returns: (transfer full): a new #GsAppReviewsDialog
 * Since: 42
 */
GtkWidget *
gs_app_reviews_dialog_new (GsApp *app, GsOdrsProvider *odrs_provider, GsPluginLoader *plugin_loader)
{
	GsAppReviewsDialog *self;

	g_return_val_if_fail (app == NULL || GS_IS_APP (app), NULL);
	g_return_val_if_fail (odrs_provider == NULL || GS_IS_ODRS_PROVIDER (odrs_provider), NULL);
	g_return_val_if_fail (plugin_loader == NULL ||GS_IS_PLUGIN_LOADER (plugin_loader), NULL);

	self = g_object_new (GS_TYPE_APP_REVIEWS_DIALOG,
			     "app", app,
			     "odrs-provider", odrs_provider,
			     "plugin-loader", plugin_loader,
			     NULL);

	return GTK_WIDGET (self);
}

/**
 * gs_app_reviews_dialog_get_app:
 * @self: a #GsAppReviewsDialog
 *
 * Get the value of #GsAppReviewsDialog:app.
 *
 * Returns: (nullable) (transfer none): a #GsApp, or %NULL if unset
 * Since: 42
 */
GsApp *
gs_app_reviews_dialog_get_app (GsAppReviewsDialog *self)
{
	g_return_val_if_fail (GS_IS_APP_REVIEWS_DIALOG (self), NULL);

	return self->app;
}

/**
 * gs_app_reviews_dialog_set_app:
 * @self: a #GsAppReviewsDialog
 * @app: (nullable) (transfer none): new #GsApp or %NULL
 *
 * Set the value of #GsAppReviewsDialog:app.
 *
 * Since: 42
 */
void
gs_app_reviews_dialog_set_app (GsAppReviewsDialog *self,
                               GsApp              *app)
{
	g_return_if_fail (GS_IS_APP_REVIEWS_DIALOG (self));
	g_return_if_fail (app == NULL || GS_IS_APP (app));

	if (g_set_object (&self->app, app)) {
		gs_app_reviews_dialog_app_refine (self);
		refresh_reviews (self);
		g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_APP]);
	}
}

/**
 * gs_app_reviews_dialog_get_odrs_provider:
 * @self: a #GsAppReviewsDialog
 *
 * Get the value of #GsAppReviewsDialog:odrs-provider.
 *
 * Returns: (nullable) (transfer none): a #GsOdrsProvider, or %NULL if unset
 * Since: 42
 */
GsOdrsProvider *
gs_app_reviews_dialog_get_odrs_provider (GsAppReviewsDialog *self)
{
	g_return_val_if_fail (GS_IS_APP_REVIEWS_DIALOG (self), NULL);

	return self->odrs_provider;
}

/**
 * gs_app_reviews_dialog_set_odrs_provider:
 * @self: a #GsAppReviewsDialog
 * @odrs_provider: (nullable) (transfer none): new #GsOdrsProvider or %NULL
 *
 * Set the value of #GsAppReviewsDialog:odrs-provider.
 *
 * Since: 42
 */
void
gs_app_reviews_dialog_set_odrs_provider (GsAppReviewsDialog *self,
                                         GsOdrsProvider     *odrs_provider)
{
	g_return_if_fail (GS_IS_APP_REVIEWS_DIALOG (self));
	g_return_if_fail (odrs_provider == NULL || GS_IS_ODRS_PROVIDER (odrs_provider));

	if (g_set_object (&self->odrs_provider, odrs_provider)) {
		refresh_reviews (self);
		g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ODRS_PROVIDER]);
	}
}

/**
 * gs_app_reviews_dialog_get_plugin_loader:
 * @self: a #GsAppReviewsDialog
 *
 * Get the value of #GsAppReviewsDialog:plugin-loader.
 *
 * Returns: (nullable) (transfer none): a #GsPluginLoader, or %NULL if unset
 * Since: 42
 */
GsPluginLoader *
gs_app_reviews_dialog_get_plugin_loader (GsAppReviewsDialog *self)
{
	g_return_val_if_fail (GS_IS_APP_REVIEWS_DIALOG (self), NULL);

	return self->plugin_loader;
}

/**
 * gs_app_reviews_dialog_set_plugin_loader:
 * @self: a #GsAppReviewsDialog
 * @plugin_loader: (nullable) (transfer none): new #GsPluginLoader or %NULL
 *
 * Set the value of #GsAppReviewsDialog:plugin-loader.
 *
 * Since: 42
 */
void
gs_app_reviews_dialog_set_plugin_loader (GsAppReviewsDialog *self,
                                         GsPluginLoader     *plugin_loader)
{
	g_return_if_fail (GS_IS_APP_REVIEWS_DIALOG (self));
	g_return_if_fail (plugin_loader == NULL || GS_IS_PLUGIN_LOADER (plugin_loader));

	if (self->plugin_loader)
		g_signal_handlers_disconnect_by_func (self->plugin_loader,
						      refresh_reviews, self);

	if (g_set_object (&self->plugin_loader, plugin_loader)) {
		gs_app_reviews_dialog_app_refine (self);
		g_signal_connect_swapped (self->plugin_loader, "notify::network-available",
					  G_CALLBACK (refresh_reviews), self);
		g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_PLUGIN_LOADER]);
	}
}
