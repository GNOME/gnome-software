/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gio/gio.h>

#include "gs-app-list-private.h"
#include "gs-app-row.h"
#include "gs-page.h"
#include "gs-common.h"
#include "gs-progress-button.h"
#include "gs-update-dialog.h"
#include "gs-updates-section.h"
#include "gs-utils.h"

struct _GsUpdatesSection
{
	GtkListBox		 parent_instance;
	GsAppList		*list;
	GsUpdatesSectionKind	 kind;
	GCancellable		*cancellable;
	GsPage			*page;
	GsPluginLoader		*plugin_loader;
	GtkSizeGroup		*sizegroup_image;
	GtkSizeGroup		*sizegroup_name;
	GtkSizeGroup		*sizegroup_desc;
	GtkSizeGroup		*sizegroup_button;
	GtkSizeGroup		*sizegroup_header;
	GtkWidget		*button_download;
	GtkWidget		*button_update;
	GtkWidget		*button_cancel;
	GtkStack		*button_stack;
	GtkWidget		*section_header;
};

G_DEFINE_TYPE (GsUpdatesSection, gs_updates_section, GTK_TYPE_LIST_BOX)

GsAppList *
gs_updates_section_get_list (GsUpdatesSection *self)
{
	return self->list;
}

static void
_app_row_button_clicked_cb (GsAppRow *app_row, GsUpdatesSection *self)
{
	GsApp *app = gs_app_row_get_app (app_row);
	if (gs_app_get_state (app) != AS_APP_STATE_UPDATABLE_LIVE)
		return;
	gs_page_update_app (GS_PAGE (self->page), app, gs_app_get_cancellable (app));
}

static void
_row_unrevealed_cb (GObject *row, GParamSpec *pspec, gpointer data)
{
	GtkWidget *list;

	list = gtk_widget_get_parent (GTK_WIDGET (row));
	if (list == NULL)
		return;
	gtk_container_remove (GTK_CONTAINER (list), GTK_WIDGET (row));
}

static void
_unreveal_row (GsAppRow *app_row)
{
	gs_app_row_unreveal (app_row);
	g_signal_connect (app_row, "unrevealed",
	                  G_CALLBACK (_row_unrevealed_cb), NULL);
}

static void
_app_state_notify_cb (GsApp *app, GParamSpec *pspec, gpointer user_data)
{
	if (gs_app_get_state (app) == AS_APP_STATE_INSTALLED) {
		GsAppRow *app_row = GS_APP_ROW (user_data);
		_unreveal_row (app_row);
	}
}

void
gs_updates_section_add_app (GsUpdatesSection *self, GsApp *app)
{
	GtkWidget *app_row;
	app_row = gs_app_row_new (app);
	gs_app_row_set_show_update (GS_APP_ROW (app_row), TRUE);
	gs_app_row_set_show_buttons (GS_APP_ROW (app_row), TRUE);
	g_signal_connect (app_row, "button-clicked",
			  G_CALLBACK (_app_row_button_clicked_cb),
			  self);
	gtk_container_add (GTK_CONTAINER (self), app_row);
	gs_app_list_add (self->list, app);

	gs_app_row_set_size_groups (GS_APP_ROW (app_row),
				    self->sizegroup_image,
				    self->sizegroup_name,
				    self->sizegroup_desc,
				    self->sizegroup_button);
	g_signal_connect_object (app, "notify::state",
	                         G_CALLBACK (_app_state_notify_cb),
	                         app_row, 0);
	gtk_widget_show (GTK_WIDGET (self));
}

void
gs_updates_section_remove_all (GsUpdatesSection *self)
{
	g_autoptr(GList) children = NULL;
	children = gtk_container_get_children (GTK_CONTAINER (self));
	for (GList *l = children; l != NULL; l = l->next) {
		GtkWidget *w = GTK_WIDGET (l->data);
		gtk_container_remove (GTK_CONTAINER (self), w);
	}
	gs_app_list_remove_all (self->list);
	gtk_widget_hide (GTK_WIDGET (self));
}

typedef struct {
	GsUpdatesSection	*self;
	gboolean		 do_reboot;
	gboolean		 do_reboot_notification;
} GsUpdatesSectionUpdateHelper;

static gchar *
_get_app_sort_key (GsApp *app)
{
	GString *key;
	g_autofree gchar *sort_name = NULL;

	key = g_string_sized_new (64);

	/* sort apps by kind */
	switch (gs_app_get_kind (app)) {
	case AS_APP_KIND_OS_UPDATE:
		g_string_append (key, "1:");
		break;
	case AS_APP_KIND_DESKTOP:
		g_string_append (key, "2:");
		break;
	case AS_APP_KIND_WEB_APP:
		g_string_append (key, "3:");
		break;
	case AS_APP_KIND_RUNTIME:
		g_string_append (key, "4:");
		break;
	case AS_APP_KIND_ADDON:
		g_string_append (key, "5:");
		break;
	case AS_APP_KIND_CODEC:
		g_string_append (key, "6:");
		break;
	case AS_APP_KIND_FONT:
		g_string_append (key, "6:");
		break;
	case AS_APP_KIND_INPUT_METHOD:
		g_string_append (key, "7:");
		break;
	case AS_APP_KIND_SHELL_EXTENSION:
		g_string_append (key, "8:");
		break;
	default:
		g_string_append (key, "9:");
		break;
	}

	/* finally, sort by short name */
	if (gs_app_get_name (app) != NULL) {
		sort_name = gs_utils_sort_key (gs_app_get_name (app));
		g_string_append (key, sort_name);
	}

	return g_string_free (key, FALSE);
}

static gint
_list_sort_func (GtkListBoxRow *a, GtkListBoxRow *b, gpointer user_data)
{
	GsApp *a1 = gs_app_row_get_app (GS_APP_ROW (a));
	GsApp *a2 = gs_app_row_get_app (GS_APP_ROW (b));
	g_autofree gchar *key1 = _get_app_sort_key (a1);
	g_autofree gchar *key2 = _get_app_sort_key (a2);

	/* compare the keys according to the algorithm above */
	return g_strcmp0 (key1, key2);
}

static void
_update_helper_free (GsUpdatesSectionUpdateHelper *helper)
{
	g_object_unref (helper->self);
	g_free (helper);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsUpdatesSectionUpdateHelper, _update_helper_free);

static void
_cancel_trigger_failed_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	GsUpdatesSection *self = GS_UPDATES_SECTION (user_data);
	g_autoptr(GError) error = NULL;
	if (!gs_plugin_loader_job_action_finish (self->plugin_loader, res, &error)) {
		g_warning ("failed to cancel trigger: %s", error->message);
		return;
	}
}

static void
_reboot_failed_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	GsUpdatesSection *self = GS_UPDATES_SECTION (user_data);
	g_autoptr(GError) error = NULL;
	GsApp *app = NULL;
	g_autoptr(GsPluginJob) plugin_job = NULL;
	g_autoptr(GVariant) retval = NULL;

	/* get result */
	retval = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, &error);
	if (retval != NULL)
		return;

	if (error != NULL) {
		g_warning ("Calling org.gnome.SessionManager.Reboot failed: %s",
			   error->message);
	}

	/* cancel trigger */
	app = gs_app_list_index (self->list, 0);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_UPDATE_CANCEL,
					 "app", app,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    gs_app_get_cancellable (app),
					    _cancel_trigger_failed_cb,
					    self);
}

static gboolean
_all_offline_updates_downloaded (GsUpdatesSection *self)
{
	/* use the download size to figure out what is downloaded and what not */
	for (guint i = 0; i < gs_app_list_length (self->list); i++) {
		GsApp *app = gs_app_list_index (self->list, i);
		guint64 size = gs_app_get_size_download (app);
		if (size != 0)
			return FALSE;
	}

	return TRUE;
}

static void
_update_buttons (GsUpdatesSection *self)
{
	/* operation in progress */
	if (self->cancellable != NULL) {
		gtk_widget_set_sensitive (self->button_cancel,
					  !g_cancellable_is_cancelled (self->cancellable));
		gtk_stack_set_visible_child_name (self->button_stack, "cancel");
		gtk_widget_show (GTK_WIDGET (self->button_stack));
		return;
	}

	if (self->kind == GS_UPDATES_SECTION_KIND_OFFLINE_FIRMWARE ||
	    self->kind == GS_UPDATES_SECTION_KIND_OFFLINE) {
		if (_all_offline_updates_downloaded (self))
			gtk_stack_set_visible_child_name (self->button_stack, "update");
		else
			gtk_stack_set_visible_child_name (self->button_stack, "download");

		gtk_widget_show (GTK_WIDGET (self->button_stack));
		/* TRANSLATORS: This is the button for installing all
		 * offline updates */
		gtk_button_set_label (GTK_BUTTON (self->button_update), _("Restart & Update"));
	} else if (self->kind == GS_UPDATES_SECTION_KIND_ONLINE) {
		gtk_stack_set_visible_child_name (self->button_stack, "update");
		gtk_widget_show (GTK_WIDGET (self->button_stack));
		/* TRANSLATORS: This is the button for upgrading all
		 * online-updatable applications */
		gtk_button_set_label (GTK_BUTTON (self->button_update), _("Update All"));
	} else {
		gtk_widget_hide (GTK_WIDGET (self->button_stack));
	}

}

static void
_perform_update_cb (GsPluginLoader *plugin_loader, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GsUpdatesSectionUpdateHelper) helper = (GsUpdatesSectionUpdateHelper *) user_data;
	GsUpdatesSection *self = helper->self;

	/* get the results */
	if (!gs_plugin_loader_job_action_finish (plugin_loader, res, &error)) {
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("failed to perform update: %s", error->message);
		goto out;
	}

	/* trigger reboot if any application was not updatable live */
	if (helper->do_reboot) {
		g_autoptr(GDBusConnection) bus = NULL;
		bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
		g_dbus_connection_call (bus,
					"org.gnome.SessionManager",
					"/org/gnome/SessionManager",
					"org.gnome.SessionManager",
					"Reboot",
					NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
					G_MAXINT, NULL,
					_reboot_failed_cb,
					self);

	/* when we are not doing an offline update, show a notification
	 * if any application requires a reboot */
	} else if (helper->do_reboot_notification) {
		gs_utils_reboot_notify (self->list);
	}

out:
	g_clear_object (&self->cancellable);
	_update_buttons (self);
}

static void
_button_cancel_clicked_cb (GtkButton *button, GsUpdatesSection *self)
{
	g_cancellable_cancel (self->cancellable);
	_update_buttons (self);
}

static void
_download_finished_cb (GObject *object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GsUpdatesSection) self = (GsUpdatesSection *) user_data;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get result */
	list = gs_plugin_loader_job_process_finish (GS_PLUGIN_LOADER (object), res, &error);
	if (list == NULL) {
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			g_warning ("failed to download updates: %s", error->message);
	}

	g_clear_object (&self->cancellable);
	_update_buttons (self);
}

static void
_button_download_clicked_cb (GtkButton *button, GsUpdatesSection *self)
{
	g_autoptr(GCancellable) cancellable = g_cancellable_new ();
	g_autoptr(GsPluginJob) plugin_job = NULL;

	g_set_object (&self->cancellable, cancellable);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_DOWNLOAD,
					 "list", self->list,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_SIZE,
					 "interactive", TRUE,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable,
					    (GAsyncReadyCallback) _download_finished_cb,
					    g_object_ref (self));
	_update_buttons (self);
}

static void
_button_update_all_clicked_cb (GtkButton *button, GsUpdatesSection *self)
{
	g_autoptr(GCancellable) cancellable = g_cancellable_new ();
	g_autoptr(GsPluginJob) plugin_job = NULL;
	GsUpdatesSectionUpdateHelper *helper = g_new0 (GsUpdatesSectionUpdateHelper, 1);

	helper->self = g_object_ref (self);

	/* look at each app in turn */
	for (guint i = 0; i < gs_app_list_length (self->list); i++) {
		GsApp *app = gs_app_list_index (self->list, i);
		if (gs_app_get_state (app) == AS_APP_STATE_UPDATABLE)
			helper->do_reboot = TRUE;
		if (gs_app_has_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT))
			helper->do_reboot_notification = TRUE;
	}

	g_set_object (&self->cancellable, cancellable);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_UPDATE,
					 "list", self->list,
					 "interactive", TRUE,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable,
					    (GAsyncReadyCallback) _perform_update_cb,
					    helper);
	_update_buttons (self);
}

static GtkWidget *
_build_section_header (GsUpdatesSection *self)
{
	GtkStyleContext *context;
	GtkWidget *header;
	GtkWidget *label;

	/* get labels and buttons for everything */
	if (self->kind == GS_UPDATES_SECTION_KIND_OFFLINE_FIRMWARE) {
		/* TRANSLATORS: This is the header for system firmware that
		 * requires a reboot to apply */
		label = gtk_label_new (_("Integrated Firmware"));
	} else if (self->kind == GS_UPDATES_SECTION_KIND_OFFLINE) {
		/* TRANSLATORS: This is the header for offline OS and offline
		 * app updates that require a reboot to apply */
		label = gtk_label_new (_("Requires Restart"));
	} else if (self->kind == GS_UPDATES_SECTION_KIND_ONLINE) {
		/* TRANSLATORS: This is the header for online runtime and
		 * app updates, typically flatpaks or snaps */
		label = gtk_label_new (_("Application Updates"));
	} else if (self->kind == GS_UPDATES_SECTION_KIND_ONLINE_FIRMWARE) {
		/* TRANSLATORS: This is the header for device firmware that can
		 * be installed online */
		label = gtk_label_new (_("Device Firmware"));
	} else {
		g_assert_not_reached ();
	}

	/* create header */
	header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
	context = gtk_widget_get_style_context (header);
	gtk_style_context_add_class (context, "app-listbox-header");

	/* put label into the header */
	gtk_widget_set_hexpand (label, TRUE);
	gtk_container_add (GTK_CONTAINER (header), label);
	gtk_widget_set_visible (label, TRUE);
	gtk_widget_set_margin_start (label, 6);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	context = gtk_widget_get_style_context (label);
	gtk_style_context_add_class (context, "app-listbox-header-title");

	/* use a stack so we can switch which buttons are showing without the
	 * sizegroup resizing */
	self->button_stack = GTK_STACK (gtk_stack_new ());
	gtk_container_add (GTK_CONTAINER (header), GTK_WIDGET (self->button_stack));
	gtk_container_child_set (GTK_CONTAINER (header), GTK_WIDGET (self->button_stack), "pack-type", GTK_PACK_END, NULL);

	/* add download button */
	self->button_download = gs_progress_button_new ();
	gtk_button_set_use_underline (GTK_BUTTON (self->button_download), TRUE);
	gtk_button_set_label (GTK_BUTTON (self->button_download), _("_Download"));
	context = gtk_widget_get_style_context (self->button_download);
	gtk_style_context_add_class (context, GTK_STYLE_CLASS_SUGGESTED_ACTION);
	g_signal_connect (self->button_download, "clicked",
			  G_CALLBACK (_button_download_clicked_cb),
			  self);
	gtk_stack_add_named (self->button_stack, self->button_download, "download");
	gtk_widget_set_visible (self->button_download, TRUE);

	/* add update button */
	self->button_update = gs_progress_button_new ();
	context = gtk_widget_get_style_context (self->button_update);
	gtk_style_context_add_class (context, GTK_STYLE_CLASS_SUGGESTED_ACTION);
	g_signal_connect (self->button_update, "clicked",
			  G_CALLBACK (_button_update_all_clicked_cb),
			  self);
	gtk_stack_add_named (self->button_stack, self->button_update, "update");
	gtk_widget_set_visible (self->button_update, TRUE);

	/* add cancel button */
	self->button_cancel = gs_progress_button_new ();
	gtk_button_set_label (GTK_BUTTON (self->button_cancel), _("Cancel"));
	gs_progress_button_set_show_progress (GS_PROGRESS_BUTTON (self->button_cancel), TRUE);
	g_signal_connect (self->button_cancel, "clicked",
			  G_CALLBACK (_button_cancel_clicked_cb),
			  self);
	gtk_stack_add_named (self->button_stack, self->button_cancel, "cancel");
	gtk_widget_set_visible (self->button_cancel, TRUE);

	/* success */
	return header;
}

static void
_list_header_func (GtkListBoxRow *row, GtkListBoxRow *before, gpointer user_data)
{
	GsUpdatesSection *self = GS_UPDATES_SECTION (user_data);
	GtkWidget *header;

	/* section changed */
	if (before == NULL) {
		if (gtk_list_box_row_get_header (row) != self->section_header) {
			gtk_widget_unparent (self->section_header);
		}
		header = self->section_header;
	} else {
		header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	}
	gtk_list_box_row_set_header (row, header);
}

static void
_app_row_activated_cb (GtkListBox *list_box, GtkListBoxRow *row, GsUpdatesSection *self)
{
	GsApp *app = gs_app_row_get_app (GS_APP_ROW (row));
	GtkWidget *dialog;
	g_autofree gchar *str = NULL;

	/* debug */
	str = gs_app_to_string (app);
	g_debug ("%s", str);

	dialog = gs_update_dialog_new (self->plugin_loader);
	gs_update_dialog_show_update_details (GS_UPDATE_DIALOG (dialog), app);
	gs_shell_modal_dialog_present (gs_page_get_shell (self->page), GTK_DIALOG (dialog));

	/* just destroy */
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy), dialog);
}

static void
gs_updates_section_show (GtkWidget *widget)
{
	_update_buttons (GS_UPDATES_SECTION (widget));

	GTK_WIDGET_CLASS (gs_updates_section_parent_class)->show (widget);
}

static void
gs_updates_section_dispose (GObject *object)
{
	GsUpdatesSection *self = GS_UPDATES_SECTION (object);

	g_clear_object (&self->cancellable);
	g_clear_object (&self->list);
	g_clear_object (&self->plugin_loader);
	g_clear_object (&self->page);
	g_clear_object (&self->sizegroup_image);
	g_clear_object (&self->sizegroup_name);
	g_clear_object (&self->sizegroup_desc);
	g_clear_object (&self->sizegroup_button);
	g_clear_object (&self->sizegroup_header);
	self->button_download = NULL;
	self->button_update = NULL;
	self->button_cancel = NULL;
	self->button_stack = NULL;
	g_clear_object (&self->section_header);

	G_OBJECT_CLASS (gs_updates_section_parent_class)->dispose (object);
}

static void
gs_updates_section_class_init (GsUpdatesSectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_updates_section_dispose;
	widget_class->show = gs_updates_section_show;
}

void
gs_updates_section_set_size_groups (GsUpdatesSection *self,
				    GtkSizeGroup *image,
				    GtkSizeGroup *name,
				    GtkSizeGroup *desc,
				    GtkSizeGroup *button,
				    GtkSizeGroup *header)
{
	g_set_object (&self->sizegroup_image, image);
	g_set_object (&self->sizegroup_name, name);
	g_set_object (&self->sizegroup_desc, desc);
	g_set_object (&self->sizegroup_button, button);
	g_set_object (&self->sizegroup_header, header);

	gtk_size_group_add_widget (self->sizegroup_button, GTK_WIDGET (self->button_stack));
	gtk_size_group_add_widget (self->sizegroup_header, self->section_header);
}

static void
gs_updates_section_progress_notify_cb (GsAppList *list,
				       GParamSpec *pspec,
				       GsUpdatesSection *self)
{
	if (self->button_cancel == NULL)
		return;

	gs_progress_button_set_progress (GS_PROGRESS_BUTTON (self->button_cancel),
					 gs_app_list_get_progress (list));
}

static void
gs_updates_section_init (GsUpdatesSection *self)
{
	GtkStyleContext *context;

	self->list = gs_app_list_new ();
	gs_app_list_add_flag (self->list,
			      GS_APP_LIST_FLAG_WATCH_APPS |
			      GS_APP_LIST_FLAG_WATCH_APPS_ADDONS |
			      GS_APP_LIST_FLAG_WATCH_APPS_RELATED);
	g_signal_connect_object (self->list, "notify::progress",
				 G_CALLBACK (gs_updates_section_progress_notify_cb),
				 self, 0);
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (self),
					 GTK_SELECTION_NONE);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (self),
				    _list_sort_func,
				    self, NULL);
	gtk_list_box_set_header_func (GTK_LIST_BOX (self),
				      _list_header_func,
				      self, NULL);
	g_signal_connect (self, "row-activated",
			  G_CALLBACK (_app_row_activated_cb), self);
	gtk_widget_set_margin_top (GTK_WIDGET (self), 24);

	/* make rounded edges */
	context = gtk_widget_get_style_context (GTK_WIDGET (self));
	gtk_style_context_add_class (context, "app-updates-section");
}

GtkListBox *
gs_updates_section_new (GsUpdatesSectionKind kind,
			GsPluginLoader *plugin_loader,
			GsPage *page)
{
	GsUpdatesSection *self;
	self = g_object_new (GS_TYPE_UPDATES_SECTION, NULL);
	self->kind = kind;
	self->plugin_loader = g_object_ref (plugin_loader);
	self->page = g_object_ref (page);
	self->section_header = g_object_ref_sink (_build_section_header (self));
	return GTK_LIST_BOX (self);
}
