/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2015-2016 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <string.h>
#include <glib/gi18n.h>

#include "gs-application.h"
#include "gs-download-utils.h"
#include "gs-page.h"
#include "gs-common.h"
#include "gs-screenshot-image.h"

typedef struct
{
	GsPluginLoader		*plugin_loader;
	GsShell			*shell;
	GtkWidget		*header_start_widget;
	GtkWidget		*header_end_widget;
	gboolean		 is_active;
} GsPagePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GsPage, gs_page, GTK_TYPE_WIDGET)

typedef enum {
	PROP_TITLE = 1,
	PROP_COUNTER,
	PROP_VADJUSTMENT,
} GsPageProperty;

static GParamSpec *obj_props[PROP_VADJUSTMENT + 1] = { NULL, };

GsShell *
gs_page_get_shell (GsPage *page)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	return priv->shell;
}

typedef struct {
	GsApp		*app;
	GsPage		*page;
	GCancellable	*cancellable;
	gulong		 notify_quirk_id;
	GtkWidget	*dialog_install;
	GsPluginJob	*job;  /* (nullable) (unowned) */
	GsPluginAction	 action;
	GsShellInteraction interaction;
	gboolean	 propagate_error;
	gulong		 app_needs_user_action_id;
} GsPageHelper;

static void
gs_page_helper_free (GsPageHelper *helper)
{
	if (helper->notify_quirk_id > 0)
		g_signal_handler_disconnect (helper->app, helper->notify_quirk_id);
	if (helper->app_needs_user_action_id != 0)
		g_signal_handler_disconnect (helper->job, helper->app_needs_user_action_id);
	if (helper->app != NULL)
		g_object_unref (helper->app);
	if (helper->page != NULL)
		g_object_unref (helper->page);
	if (helper->cancellable != NULL)
		g_object_unref (helper->cancellable);
	g_slice_free (GsPageHelper, helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsPageHelper, gs_page_helper_free);

static void
gs_page_show_update_message (GsPageHelper *helper, AsScreenshot *ss)
{
	GsPagePrivate *priv = gs_page_get_instance_private (helper->page);
	GPtrArray *images;
	GtkWidget *dialog;
	g_autofree gchar *escaped = NULL;

	escaped = g_markup_escape_text (as_screenshot_get_caption (ss), -1);
	dialog = adw_message_dialog_new (GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (helper->page), GTK_TYPE_WINDOW)),
					 gs_app_get_name (helper->app), escaped);
	adw_message_dialog_add_response (ADW_MESSAGE_DIALOG (dialog),
					 /* TRANSLATORS: button text */
					 "ok", _("_OK"));

	/* image is optional */
	images = as_screenshot_get_images (ss);
	if (images->len) {
		GtkWidget *ssimg;
		g_autoptr(SoupSession) soup_session = NULL;

		/* load screenshot */
		soup_session = gs_build_soup_session ();
		ssimg = gs_screenshot_image_new (soup_session);
		gs_screenshot_image_set_screenshot (GS_SCREENSHOT_IMAGE (ssimg), ss);
		gs_screenshot_image_set_size (GS_SCREENSHOT_IMAGE (ssimg), 400, 225);
		gs_screenshot_image_load_async (GS_SCREENSHOT_IMAGE (ssimg),
						helper->cancellable);
		gtk_widget_set_margin_start (ssimg, 24);
		gtk_widget_set_margin_end (ssimg, 24);
		adw_message_dialog_set_extra_child (ADW_MESSAGE_DIALOG (dialog), ssimg);
	}

	gs_shell_modal_dialog_present (priv->shell, GTK_WINDOW (dialog));
}

static void
gs_page_app_installed_cb (GObject *source,
                          GAsyncResult *res,
                          gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsPage *page = helper->page;
	gboolean ret;
	g_autoptr(GError) error = NULL;

	ret = gs_plugin_loader_job_action_finish (plugin_loader,
						   res,
						   &error);

	gs_application_emit_install_resources_done (GS_APPLICATION (g_application_get_default ()), NULL, error);

	if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED) ||
	    g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_debug ("App install cancelled with error: %s", error->message);
		return;
	}
	if (!ret) {
		if (helper->propagate_error) {
			if (helper->job != NULL)
				gs_plugin_loader_claim_job_error (plugin_loader,
								  NULL,
								  helper->job,
								  error);
			else
				gs_plugin_loader_claim_error (plugin_loader,
							      NULL,
							      helper->action,
							      helper->app,
							      helper->interaction == GS_SHELL_INTERACTION_FULL,
							      error);
		} else {
			g_warning ("failed to install %s: %s", gs_app_get_id (helper->app), error->message);
		}
		return;
	}

	/* the single update needs system reboot, e.g. for firmware */
	if (gs_app_has_quirk (helper->app, GS_APP_QUIRK_NEEDS_REBOOT)) {
		g_autoptr(GsAppList) list = gs_app_list_new ();
		gs_app_list_add (list, helper->app);
		gs_utils_reboot_notify (list, TRUE);
	}

	/* tell the user what they have to do */
	if (gs_app_get_kind (helper->app) == AS_COMPONENT_KIND_FIRMWARE &&
	    gs_app_has_quirk (helper->app, GS_APP_QUIRK_NEEDS_USER_ACTION)) {
		AsScreenshot *ss = gs_app_get_action_screenshot (helper->app);
		if (ss != NULL && as_screenshot_get_caption (ss) != NULL)
			gs_page_show_update_message (helper, ss);
	}

	/* only show this if the window is not active */
	if (gs_app_is_installed (helper->app) &&
	    helper->action == GS_PLUGIN_ACTION_INSTALL &&
	    !gtk_window_is_active (GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (helper->page), GTK_TYPE_WINDOW))) &&
	    ((helper->interaction) & GS_SHELL_INTERACTION_NOTIFY) != 0)
		gs_app_notify_installed (helper->app);

	if (gs_app_is_installed (helper->app) &&
	    GS_PAGE_GET_CLASS (page)->app_installed != NULL) {
		GS_PAGE_GET_CLASS (page)->app_installed (page, helper->app);
	}
}

static void
gs_page_app_removed_cb (GObject *source,
                        GAsyncResult *res,
                        gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	GsPage *page = helper->page;
	gboolean ret;
	g_autoptr(GError) error = NULL;

	ret = gs_plugin_loader_job_action_finish (plugin_loader,
						   res,
						   &error);
	if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED) ||
	    g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		g_debug ("%s", error->message);
		return;
	}
	if (!ret) {
		g_warning ("failed to uninstall: %s", error->message);
		return;
	}

	/* the app removal needs system reboot, e.g. for rpm-ostree */
	if (gs_app_has_quirk (helper->app, GS_APP_QUIRK_NEEDS_REBOOT)) {
		g_autoptr(GsAppList) list = gs_app_list_new ();
		gs_app_list_add (list, helper->app);
		gs_utils_reboot_notify (list, FALSE);
	}

	if (!gs_app_is_installed (helper->app) &&
	    GS_PAGE_GET_CLASS (page)->app_removed != NULL) {
		GS_PAGE_GET_CLASS (page)->app_removed (page, helper->app);
	}
}

GtkWidget *
gs_page_get_header_start_widget (GsPage *page)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);

	return priv->header_start_widget;
}

void
gs_page_set_header_start_widget (GsPage *page, GtkWidget *widget)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);

	g_set_object (&priv->header_start_widget, widget);
}

GtkWidget *
gs_page_get_header_end_widget (GsPage *page)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);

	return priv->header_end_widget;
}

void
gs_page_set_header_end_widget (GsPage *page, GtkWidget *widget)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);

	g_set_object (&priv->header_end_widget, widget);
}

void
gs_page_install_app (GsPage *page,
		     GsApp *app,
		     GsShellInteraction interaction,
		     GCancellable *cancellable)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	GsPageHelper *helper;
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* probably non-free */
	if (gs_app_get_state (app) == GS_APP_STATE_UNAVAILABLE) {
		GtkResponseType response;

		response = gs_app_notify_unavailable (app, GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (page), GTK_TYPE_WINDOW)));
		if (response != GTK_RESPONSE_OK) {
			g_autoptr(GError) error_local = NULL;
			g_set_error_literal (&error_local, G_IO_ERROR, G_IO_ERROR_CANCELLED, _("User declined installation"));
			gs_application_emit_install_resources_done (GS_APPLICATION (g_application_get_default ()), NULL, error_local);
			return;
		}
	}

	helper = g_slice_new0 (GsPageHelper);
	helper->app = g_object_ref (app);
	helper->page = g_object_ref (page);
	helper->cancellable = g_object_ref (cancellable != NULL ? cancellable : gs_app_get_cancellable (app));
	helper->interaction = interaction;
	helper->propagate_error = TRUE;

	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_REPOSITORY) {
		helper->action = GS_PLUGIN_ACTION_INSTALL_REPO;
		plugin_job = gs_plugin_job_manage_repository_new (helper->app,
								  GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INSTALL |
								  ((interaction == GS_SHELL_INTERACTION_FULL) ?
								   GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE : 0));
		gs_plugin_job_set_propagate_error (plugin_job, helper->propagate_error);
	} else {
		helper->action = GS_PLUGIN_ACTION_INSTALL;
		plugin_job = gs_plugin_job_newv (helper->action,
						 "interactive", (interaction == GS_SHELL_INTERACTION_FULL),
						 "propagate-error", helper->propagate_error,
						 "app", helper->app,
						 NULL);
	}

	gs_plugin_loader_job_process_async (priv->plugin_loader,
					    plugin_job,
					    helper->cancellable,
					    gs_page_app_installed_cb,
					    helper);
}

static void
gs_page_update_app_response_cb (AdwMessageDialog *dialog,
				const gchar *response,
				gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GsPagePrivate *priv = gs_page_get_instance_private (helper->page);
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* not agreed */
	if (g_strcmp0 (response, "install") != 0)
		return;

	g_debug ("update %s", gs_app_get_id (helper->app));

	list = gs_app_list_new ();
	gs_app_list_add (list, helper->app);

	plugin_job = gs_plugin_job_update_apps_new (list,
						    GS_PLUGIN_UPDATE_APPS_FLAGS_NO_DOWNLOAD | GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE);
	gs_plugin_job_set_propagate_error (plugin_job, helper->propagate_error);

	gs_plugin_loader_job_process_async (priv->plugin_loader,
					    plugin_job,
					    helper->cancellable,
					    gs_page_app_installed_cb,
					    helper);
	g_steal_pointer (&helper);
}

static void
gs_page_notify_quirk_cb (GsApp *app, GParamSpec *pspec, GsPageHelper *helper)
{
	adw_message_dialog_set_response_enabled (ADW_MESSAGE_DIALOG (helper->dialog_install),
						 "install",
						 !gs_app_has_quirk (helper->app,
								    GS_APP_QUIRK_NEEDS_USER_ACTION));
}

static gboolean update_app_needs_user_action_cb (GsPluginJobUpdateApps *plugin_job,
                                                 GsApp                 *app,
                                                 AsScreenshot          *action_screenshot,
                                                 gpointer               user_data);

void
gs_page_update_app (GsPage *page, GsApp *app, GCancellable *cancellable)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	GsPageHelper *helper;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* non-firmware apps do not have to be prepared */
	helper = g_slice_new0 (GsPageHelper);
	helper->app = g_object_ref (app);
	helper->page = g_object_ref (page);
	helper->cancellable = g_object_ref (cancellable != NULL ? cancellable : gs_app_get_cancellable (app));
	helper->propagate_error = TRUE;

	/* generic fallback */
	list = gs_app_list_new ();
	gs_app_list_add (list, app);

	plugin_job = gs_plugin_job_update_apps_new (list,
						    GS_PLUGIN_UPDATE_APPS_FLAGS_NO_DOWNLOAD | GS_PLUGIN_UPDATE_APPS_FLAGS_INTERACTIVE);
	gs_plugin_job_set_propagate_error (plugin_job, helper->propagate_error);
	helper->job = plugin_job;

	helper->app_needs_user_action_id =
		g_signal_connect (plugin_job, "app-needs-user-action",
				  G_CALLBACK (update_app_needs_user_action_cb), helper);

	gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
					    helper->cancellable,
					    gs_page_app_installed_cb,
					    helper);
}

static gboolean
update_app_needs_user_action_cb (GsPluginJobUpdateApps *plugin_job,
                                 GsApp                 *app,
                                 AsScreenshot          *action_screenshot,
                                 gpointer               user_data)
{
	GsPageHelper *helper = user_data;
	GtkWidget *dialog;
	g_autoptr(SoupSession) soup_session = NULL;
	GtkWidget *ssimg;
	g_autofree gchar *heading = NULL;
	g_autofree gchar *escaped = NULL;
	GsPagePrivate *priv = gs_page_get_instance_private (helper->page);

	g_assert (helper->app == app);

	/* TRANSLATORS: this is a prompt message, and
	 * '%s' is an app summary, e.g. 'GNOME Clocks' */
	heading = g_strdup_printf (_("Prepare %s"), gs_app_get_name (helper->app));
	escaped = g_markup_escape_text (as_screenshot_get_caption (action_screenshot), -1);
	dialog = adw_message_dialog_new (GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (helper->page), GTK_TYPE_WINDOW)),
					 heading, escaped);
	adw_message_dialog_set_body_use_markup (ADW_MESSAGE_DIALOG (dialog), TRUE);
	adw_message_dialog_add_responses (ADW_MESSAGE_DIALOG (dialog),
					  "cancel",  _("_Cancel"),
					  /* TRANSLATORS: update the fw */
					  "install",  _("_Install"),
					  NULL);

	/* this will be enabled when the device is in the right mode */
	helper->dialog_install = dialog;
	helper->notify_quirk_id =
		g_signal_connect (helper->app, "notify::quirk",
				  G_CALLBACK (gs_page_notify_quirk_cb),
				  helper);
	adw_message_dialog_set_response_enabled (ADW_MESSAGE_DIALOG (dialog),
						 "install", FALSE);

	/* load screenshot */
	soup_session = gs_build_soup_session ();
	ssimg = gs_screenshot_image_new (soup_session);
	gs_screenshot_image_set_screenshot (GS_SCREENSHOT_IMAGE (ssimg), action_screenshot);
	gs_screenshot_image_set_size (GS_SCREENSHOT_IMAGE (ssimg), 400, 225);
	gs_screenshot_image_load_async (GS_SCREENSHOT_IMAGE (ssimg),
					helper->cancellable);
	gtk_widget_set_margin_start (ssimg, 24);
	gtk_widget_set_margin_end (ssimg, 24);
	adw_message_dialog_set_extra_child (ADW_MESSAGE_DIALOG (dialog), ssimg);

	/* handle this async */
	g_signal_connect (dialog, "response",
			  G_CALLBACK (gs_page_update_app_response_cb), helper);
	gs_shell_modal_dialog_present (priv->shell, GTK_WINDOW (dialog));

	/* Cancel the job to allow us to wait for the user to interact with the
	 * dialogue. It’ll be rescheduled in gs_page_update_app_response_cb() */
	return FALSE;
}

static void
gs_page_remove_app_response_cb (AdwMessageDialog *dialog,
				const gchar *response,
				gpointer user_data)
{
	g_autoptr(GsPageHelper) helper = (GsPageHelper *) user_data;
	GsPagePrivate *priv = gs_page_get_instance_private (helper->page);
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* not agreed */
	if (g_strcmp0 (response, "uninstall") != 0)
		return;

	g_debug ("uninstall %s", gs_app_get_id (helper->app));
	if (gs_app_get_kind (helper->app) == AS_COMPONENT_KIND_REPOSITORY) {
		helper->action = GS_PLUGIN_ACTION_REMOVE_REPO;
		plugin_job = gs_plugin_job_manage_repository_new (helper->app,
								  GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_REMOVE |
								  GS_PLUGIN_MANAGE_REPOSITORY_FLAGS_INTERACTIVE);
	} else {
		plugin_job = gs_plugin_job_newv (helper->action,
						 "interactive", TRUE,
						 "app", helper->app,
						 NULL);
	}
	gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
					    helper->cancellable,
					    gs_page_app_removed_cb,
					    helper);
	g_steal_pointer (&helper);
}

void
gs_page_remove_app (GsPage *page, GsApp *app, GCancellable *cancellable)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	GsPageHelper *helper;
	GtkWidget *dialog;
	g_autofree gchar *message = NULL;
	g_autofree gchar *title = NULL;

	/* Is the app actually removable? */
	if (gs_app_has_quirk (app, GS_APP_QUIRK_COMPULSORY))
		return;

	/* pending install */
	helper = g_slice_new0 (GsPageHelper);
	helper->action = GS_PLUGIN_ACTION_REMOVE;
	helper->app = g_object_ref (app);
	helper->page = g_object_ref (page);
	helper->cancellable = g_object_ref (cancellable != NULL ? cancellable : gs_app_get_cancellable (app));
	if (gs_app_get_state (app) == GS_APP_STATE_QUEUED_FOR_INSTALL) {
		g_autoptr(GsPluginJob) plugin_job = NULL;

		if (helper->cancellable != gs_app_get_cancellable (app)) {
			/* cancel any ongoing job, this allows to e.g. cancel pending
			 * installations, updates, or other ops that may have been queued
			 * in the plugin loader (due to reaching the max parallel ops allowed) */
			g_cancellable_cancel (gs_app_get_cancellable (app));
		}

		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_REMOVE,
						 "interactive", TRUE,
						 "app", app,
							 NULL);
		g_debug ("uninstall %s", gs_app_get_id (app));
		gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
						    helper->cancellable,
						    gs_page_app_removed_cb,
						    helper);
		return;
	}

	/* use different name and summary */
	switch (gs_app_get_kind (app)) {
	case AS_COMPONENT_KIND_REPOSITORY:
		/* TRANSLATORS: this is a prompt message, and '%s' is an
		 * repository name, e.g. 'GNOME Nightly' */
		title = g_strdup_printf (_("Are you sure you want to remove "
					   "the %s repository?"),
					 gs_app_get_name (app));
		/* TRANSLATORS: longer dialog text */
		message = g_strdup_printf (_("All apps from %s will be "
					     "uninstalled, and you will have to "
					     "re-install the repository to use them again."),
					   gs_app_get_name (app));
		break;
	default:
		/* TRANSLATORS: this is a prompt message, and '%s' is an
		 * app summary, e.g. 'GNOME Clocks' */
		title = g_strdup_printf (_("Are you sure you want to uninstall %s?"),
					 gs_app_get_name (app));
		/* TRANSLATORS: longer dialog text */
		message = g_strdup_printf (_("%s will be uninstalled, and you will "
					     "have to install it to use it again."),
					   gs_app_get_name (app));
		break;
	}

	/* ask for confirmation */
	dialog = adw_message_dialog_new (GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (page), GTK_TYPE_WINDOW)),
					 title, message);
	adw_message_dialog_set_body_use_markup (ADW_MESSAGE_DIALOG (dialog), TRUE);
	adw_message_dialog_add_responses (ADW_MESSAGE_DIALOG (dialog),
					  "cancel",  _("_Cancel"),
					  /* TRANSLATORS: this is button text to remove the app */
					  "uninstall",  _("_Uninstall"),
					  NULL);
	adw_message_dialog_set_response_appearance (ADW_MESSAGE_DIALOG (dialog),
						    "uninstall", ADW_RESPONSE_DESTRUCTIVE);

	/* handle this async */
	g_signal_connect (dialog, "response",
			  G_CALLBACK (gs_page_remove_app_response_cb), helper);
	gs_shell_modal_dialog_present (priv->shell, GTK_WINDOW (dialog));
}

static void
gs_page_app_launched_cb (GObject *source,
			 GAsyncResult *res,
			 gpointer user_data)
{
	GsPluginLoader *plugin_loader = GS_PLUGIN_LOADER (source);
	g_autoptr(GError) error = NULL;
	if (!gs_plugin_loader_job_action_finish (plugin_loader, res, &error)) {
		g_warning ("failed to launch GsApp: %s", error->message);
		return;
	}
}

void
gs_page_launch_app (GsPage *page, GsApp *app, GCancellable *cancellable)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	g_autoptr(GsPluginJob) plugin_job = NULL;
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_LAUNCH,
					 "interactive", TRUE,
					 "app", app,
					 NULL);
	gs_plugin_loader_job_process_async (priv->plugin_loader, plugin_job,
					    cancellable,
					    gs_page_app_launched_cb,
					    NULL);
}

gboolean
gs_page_is_active (GsPage *page)
{
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	g_return_val_if_fail (GS_IS_PAGE (page), FALSE);
	return priv->is_active;
}

/**
 * gs_page_get_title:
 * @page: a #GsPage
 *
 * Get the value of #GsPage:title.
 *
 * Returns: (nullable): human readable title for the page, or %NULL if one isn’t set
 *
 * Since: 40
 */
const gchar *
gs_page_get_title (GsPage *page)
{
	g_auto(GValue) value = G_VALUE_INIT;

	g_return_val_if_fail (GS_IS_PAGE (page), NULL);

	/* The property is typically overridden by subclasses; the
	 * implementation in #GsPage itself is just a placeholder. */
	g_object_get_property (G_OBJECT (page), "title", &value);

	return g_value_get_string (&value);
}

/**
 * gs_page_get_counter:
 * @page: a #GsPage
 *
 * Get the value of #GsPage:counter.
 *
 * Returns: a counter of the number of available updates, installed packages,
 *     etc. on this page
 *
 * Since: 40
 */
guint
gs_page_get_counter (GsPage *page)
{
	g_auto(GValue) value = G_VALUE_INIT;

	g_return_val_if_fail (GS_IS_PAGE (page), 0);

	/* The property is typically overridden by subclasses; the
	 * implementation in #GsPage itself is just a placeholder. */
	g_object_get_property (G_OBJECT (page), "counter", &value);

	return g_value_get_uint (&value);
}

/**
 * gs_page_get_vadjustment:
 * @page: a #GsPage
 *
 * Get the #GtkAdjustment used for vertical scrolling.
 *
 * Returns: (nullable) (transfer none): the #GtkAdjustment used for vertical scrolling
 *
 * Since: 41
 */
GtkAdjustment *
gs_page_get_vadjustment (GsPage *page)
{
	g_auto(GValue) value = G_VALUE_INIT;

	g_return_val_if_fail (GS_IS_PAGE (page), NULL);

	/* The property is typically overridden by subclasses; the
	 * implementation in #GsPage itself is just a placeholder. */
	g_object_get_property (G_OBJECT (page), "vadjustment", &value);

	return g_value_get_object (&value);
}

/**
 * gs_page_switch_to:
 *
 * Pure virtual method that subclasses have to override to show page specific
 * widgets.
 */
void
gs_page_switch_to (GsPage *page)
{
	GsPageClass *klass = GS_PAGE_GET_CLASS (page);
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	priv->is_active = TRUE;
	if (klass->switch_to != NULL)
		klass->switch_to (page);
}

/**
 * gs_page_switch_from:
 *
 * Pure virtual method that subclasses have to override to show page specific
 * widgets.
 */
void
gs_page_switch_from (GsPage *page)
{
	GsPageClass *klass = GS_PAGE_GET_CLASS (page);
	GsPagePrivate *priv = gs_page_get_instance_private (page);
	priv->is_active = FALSE;
	if (klass->switch_from != NULL)
		klass->switch_from (page);
}

/**
 * gs_page_scroll_up:
 * @page: a #GsPage
 *
 * Scroll the page to the top of its content, if it supports scrolling.
 *
 * If it doesn’t support scrolling, this is a no-op.
 *
 * Since: 40
 */
void
gs_page_scroll_up (GsPage *page)
{
	GtkAdjustment *adj;

	g_return_if_fail (GS_IS_PAGE (page));

	adj = gs_page_get_vadjustment (page);
	if (adj)
		gtk_adjustment_set_value (adj, gtk_adjustment_get_lower (adj));
}

void
gs_page_reload (GsPage *page)
{
	GsPageClass *klass;
	g_return_if_fail (GS_IS_PAGE (page));
	klass = GS_PAGE_GET_CLASS (page);
	if (klass->reload != NULL)
		klass->reload (page);
}

gboolean
gs_page_setup (GsPage *page,
               GsShell *shell,
               GsPluginLoader *plugin_loader,
               GCancellable *cancellable,
               GError **error)
{
	GsPageClass *klass;
	GsPagePrivate *priv = gs_page_get_instance_private (page);

	g_return_val_if_fail (GS_IS_PAGE (page), FALSE);

	klass = GS_PAGE_GET_CLASS (page);
	g_assert (klass->setup != NULL);

	priv->plugin_loader = g_object_ref (plugin_loader);
	priv->shell = shell;

	return klass->setup (page, shell, plugin_loader, cancellable, error);
}

static void
gs_page_get_property (GObject    *object,
                      guint       prop_id,
                      GValue     *value,
                      GParamSpec *pspec)
{
	switch ((GsPageProperty) prop_id) {
	case PROP_TITLE:
		/* Should be overridden by subclasses. */
		g_value_set_string (value, NULL);
		break;
	case PROP_COUNTER:
		/* Should be overridden by subclasses. */
		g_value_set_uint (value, 0);
		break;
	case PROP_VADJUSTMENT:
		/* Should be overridden by subclasses. */
		g_value_set_object (value, NULL);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_page_dispose (GObject *object)
{
	GsPage *page = GS_PAGE (object);
	GsPagePrivate *priv = gs_page_get_instance_private (page);

	gs_widget_remove_all (GTK_WIDGET (page), NULL);

	g_clear_object (&priv->plugin_loader);
	g_clear_object (&priv->header_start_widget);
	g_clear_object (&priv->header_end_widget);

	G_OBJECT_CLASS (gs_page_parent_class)->dispose (object);
}

static void
gs_page_init (GsPage *page)
{
	gtk_widget_set_hexpand (GTK_WIDGET (page), TRUE);
	gtk_widget_set_vexpand (GTK_WIDGET (page), TRUE);
}

static void
gs_page_class_init (GsPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_page_get_property;
	object_class->dispose = gs_page_dispose;

	/**
	 * GsPage:title: (nullable)
	 *
	 * A human readable title for this page, or %NULL if one isn’t set or
	 * doesn’t make sense.
	 *
	 * Since: 40
	 */
	obj_props[PROP_TITLE] =
		g_param_spec_string ("title", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * GsPage:counter:
	 *
	 * A counter indicating the number of installed packages, available
	 * updates, etc. on this page.
	 *
	 * Since: 40
	 */
	obj_props[PROP_COUNTER] =
		g_param_spec_uint ("counter", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	/**
	 * GsPage:vadjustment: (nullable)
	 *
	 * The #GtkAdjustment used for vertical scrolling.
	 * This will be %NULL if the page is not vertically scrollable.
	 *
	 * Since: 41
	 */
	obj_props[PROP_VADJUSTMENT] =
		g_param_spec_object ("vadjustment", NULL, NULL,
				     GTK_TYPE_ADJUSTMENT,
				     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

GsPage *
gs_page_new (void)
{
	return GS_PAGE (g_object_new (GS_TYPE_PAGE, NULL));
}

GsAppQueryLicenseType
gs_page_get_query_license_type (GsPage *self)
{
	GsPagePrivate *priv = gs_page_get_instance_private (self);
	g_return_val_if_fail (GS_IS_PAGE (self), GS_APP_QUERY_LICENSE_ANY);
	g_return_val_if_fail (priv->shell != NULL, GS_APP_QUERY_LICENSE_ANY);
	return gs_shell_get_query_license_type (priv->shell);
}
