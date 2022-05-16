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
	GtkBox			 parent_instance;

	GtkWidget		*button_cancel;
	GtkWidget		*button_download;
	GtkWidget		*button_stack;
	GtkWidget		*button_update;
	GtkWidget		*description;
	GtkWidget		*listbox;
	GtkWidget		*listbox_box;
	GtkWidget		*section_header;
	GtkWidget		*title;

	GsAppList		*list;
	GsUpdatesSectionKind	 kind;
	GCancellable		*cancellable;
	GsPage			*page;
	GsPluginLoader		*plugin_loader;
	GtkSizeGroup		*sizegroup_name;
	GtkSizeGroup		*sizegroup_button_label;
	GtkSizeGroup		*sizegroup_button_image;
	GtkSizeGroup		*sizegroup_header;
	gboolean		 is_narrow;
};

G_DEFINE_TYPE (GsUpdatesSection, gs_updates_section, GTK_TYPE_BOX)

typedef enum {
	PROP_IS_NARROW = 1,
} GsUpdatesSectionProperty;

static GParamSpec *obj_props[PROP_IS_NARROW + 1] = { NULL, };

GsAppList *
gs_updates_section_get_list (GsUpdatesSection *self)
{
	return self->list;
}

static gboolean
_listbox_keynav_failed_cb (GsUpdatesSection *self, GtkDirectionType direction, GtkListBox *listbox)
{
	GtkRoot *root = gtk_widget_get_root (GTK_WIDGET (listbox));

	if (!root)
		return FALSE;

	if (direction != GTK_DIR_UP && direction != GTK_DIR_DOWN)
		return FALSE;

	return gtk_widget_child_focus (GTK_WIDGET (root), direction == GTK_DIR_UP ? GTK_DIR_TAB_BACKWARD : GTK_DIR_TAB_FORWARD);
}

static void
_app_row_button_clicked_cb (GsAppRow *app_row, GsUpdatesSection *self)
{
	GsApp *app = gs_app_row_get_app (app_row);
	if (gs_app_get_state (app) != GS_APP_STATE_UPDATABLE_LIVE)
		return;
	gs_page_update_app (GS_PAGE (self->page), app, gs_app_get_cancellable (app));
}

static void
_row_unrevealed_cb (GObject *row, GParamSpec *pspec, gpointer data)
{
	GtkWidget *widget;
	GsUpdatesSection *self;

	widget = gtk_widget_get_parent (GTK_WIDGET (row));
	if (widget == NULL)
		return;

	/* Traverse the widget structure up to the GsUpdatesSection */
	while (widget != NULL) {
		if (GS_IS_UPDATES_SECTION (widget))
			break;
		widget = gtk_widget_get_parent (widget);
	}

	g_return_if_fail (GS_IS_UPDATES_SECTION (widget));
	self = GS_UPDATES_SECTION (widget);

	gs_app_list_remove (self->list, gs_app_row_get_app (GS_APP_ROW (row)));

	gtk_list_box_remove (GTK_LIST_BOX (self->listbox), GTK_WIDGET (row));

	if (!gs_app_list_length (self->list))
		gtk_widget_hide (widget);
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
	if (gs_app_get_state (app) == GS_APP_STATE_INSTALLED) {
		GsAppRow *app_row = GS_APP_ROW (user_data);
		_unreveal_row (app_row);
	}
}

void
gs_updates_section_add_app (GsUpdatesSection *self, GsApp *app)
{
	GtkWidget *app_row;
	app_row = gs_app_row_new (app);
	gs_app_row_set_show_description (GS_APP_ROW (app_row), FALSE);
	gs_app_row_set_show_update (GS_APP_ROW (app_row), TRUE);
	gs_app_row_set_show_buttons (GS_APP_ROW (app_row), TRUE);
	g_signal_connect (app_row, "button-clicked",
			  G_CALLBACK (_app_row_button_clicked_cb),
			  self);
	gtk_list_box_append (GTK_LIST_BOX (self->listbox), app_row);
	gs_app_list_add (self->list, app);

	gs_app_row_set_size_groups (GS_APP_ROW (app_row),
				    self->sizegroup_name,
				    self->sizegroup_button_label,
				    self->sizegroup_button_image);
	g_signal_connect_object (app, "notify::state",
	                         G_CALLBACK (_app_state_notify_cb),
	                         app_row, 0);
	g_object_bind_property (G_OBJECT (self), "is-narrow",
				app_row, "is-narrow",
				G_BINDING_SYNC_CREATE);
	gtk_widget_show (GTK_WIDGET (self));
}

void
gs_updates_section_remove_all (GsUpdatesSection *self)
{
	GtkWidget *child;
	while ((child = gtk_widget_get_first_child (self->listbox)) != NULL)
		gtk_list_box_remove (GTK_LIST_BOX (self->listbox), child);
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
	case AS_COMPONENT_KIND_DESKTOP_APP:
		g_string_append (key, "2:");
		break;
	case AS_COMPONENT_KIND_WEB_APP:
		g_string_append (key, "3:");
		break;
	case AS_COMPONENT_KIND_RUNTIME:
		g_string_append (key, "4:");
		break;
	case AS_COMPONENT_KIND_ADDON:
		g_string_append (key, "5:");
		break;
	case AS_COMPONENT_KIND_CODEC:
		g_string_append (key, "6:");
		break;
	case AS_COMPONENT_KIND_FONT:
		g_string_append (key, "6:");
		break;
	case AS_COMPONENT_KIND_INPUT_METHOD:
		g_string_append (key, "7:");
		break;
	default:
		if (gs_app_get_special_kind (app) == GS_APP_SPECIAL_KIND_OS_UPDATE)
			g_string_append (key, "1:");
		else
			g_string_append (key, "8:");
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
		GsSizeType size_type;
		guint64 size_bytes;

		size_type = gs_app_get_size_download (app, &size_bytes);
		if (size_type != GS_SIZE_TYPE_VALID || size_bytes != 0)
			return FALSE;

		size_type = gs_app_get_size_download_dependencies (app, &size_bytes);
		if (size_type != GS_SIZE_TYPE_VALID || size_bytes != 0)
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
		gtk_stack_set_visible_child_name (GTK_STACK (self->button_stack), "cancel");
		gtk_widget_show (GTK_WIDGET (self->button_stack));
		return;
	}

	if (self->kind == GS_UPDATES_SECTION_KIND_OFFLINE_FIRMWARE ||
	    self->kind == GS_UPDATES_SECTION_KIND_OFFLINE) {
		if (_all_offline_updates_downloaded (self))
			gtk_stack_set_visible_child_name (GTK_STACK (self->button_stack), "update");
		else
			gtk_stack_set_visible_child_name (GTK_STACK (self->button_stack), "download");

		gtk_widget_show (GTK_WIDGET (self->button_stack));
		/* TRANSLATORS: This is the button for installing all
		 * offline updates */
		gs_progress_button_set_label (GS_PROGRESS_BUTTON (self->button_update), _("Restart & Update"));
	} else if (self->kind == GS_UPDATES_SECTION_KIND_ONLINE) {
		gtk_stack_set_visible_child_name (GTK_STACK (self->button_stack), "update");
		gtk_widget_show (GTK_WIDGET (self->button_stack));
		/* TRANSLATORS: This is the button for upgrading all
		 * online-updatable applications */
		gs_progress_button_set_label (GS_PROGRESS_BUTTON (self->button_update), _("Update All"));
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
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED) &&
		    !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			GsApp *app = NULL;

			if (gs_app_list_length (self->list) == 1)
				app = gs_app_list_index (self->list, 0);

			gs_plugin_loader_claim_error (plugin_loader,
						      NULL,
						      GS_PLUGIN_ACTION_UPDATE,
						      app,
						      TRUE,
						      error);
		}
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
		gs_utils_reboot_notify (self->list, TRUE);
	}

out:
	g_clear_object (&self->cancellable);
	_update_buttons (self);
}

static void
_button_cancel_clicked_cb (GsUpdatesSection *self)
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
		if (!g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED) &&
		    !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			g_warning ("failed to download updates: %s", error->message);
	}

	g_clear_object (&self->cancellable);
	_update_buttons (self);
}

static void
_button_download_clicked_cb (GsUpdatesSection *self)
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
_button_update_all_clicked_cb (GsUpdatesSection *self)
{
	g_autoptr(GCancellable) cancellable = g_cancellable_new ();
	g_autoptr(GsPluginJob) plugin_job = NULL;
	GsUpdatesSectionUpdateHelper *helper = g_new0 (GsUpdatesSectionUpdateHelper, 1);

	helper->self = g_object_ref (self);

	/* look at each app in turn */
	for (guint i = 0; i < gs_app_list_length (self->list); i++) {
		GsApp *app = gs_app_list_index (self->list, i);
		if (gs_app_get_state (app) == GS_APP_STATE_UPDATABLE)
			helper->do_reboot = TRUE;
		if (gs_app_has_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT))
			helper->do_reboot_notification = TRUE;
	}

	g_set_object (&self->cancellable, cancellable);
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_UPDATE,
					 "list", self->list,
					 "interactive", TRUE,
					 "propagate-error", TRUE,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job,
					    self->cancellable,
					    (GAsyncReadyCallback) _perform_update_cb,
					    helper);
	_update_buttons (self);
}

static void
_setup_section_header (GsUpdatesSection *self)
{
	/* get labels and buttons for everything */
	switch (self->kind) {
	case GS_UPDATES_SECTION_KIND_OFFLINE_FIRMWARE:
		/* TRANSLATORS: This is the header for system firmware that
		 * requires a reboot to apply */
		gtk_label_set_label (GTK_LABEL (self->title), _("Integrated Firmware"));
		break;
	case GS_UPDATES_SECTION_KIND_OFFLINE:
		/* TRANSLATORS: This is the header for offline OS and offline
		 * app updates that require a reboot to apply */
		gtk_label_set_label (GTK_LABEL (self->title), _("Requires Restart"));
		break;
	case GS_UPDATES_SECTION_KIND_ONLINE:
		/* TRANSLATORS: This is the header for online runtime and
		 * app updates, typically flatpaks or snaps */
		gtk_label_set_label (GTK_LABEL (self->title), _("Application Updates"));
		break;
	case GS_UPDATES_SECTION_KIND_ONLINE_FIRMWARE:
		/* TRANSLATORS: This is the header for device firmware that can
		 * be installed online */
		gtk_label_set_label (GTK_LABEL (self->title), _("Device Firmware"));
		break;
	default:
		g_assert_not_reached ();
	}
}

static void
_app_row_activated_cb (GsUpdatesSection *self, GtkListBoxRow *row)
{
	GsApp *app = gs_app_row_get_app (GS_APP_ROW (row));
	GtkWidget *dialog;
	g_autofree gchar *str = NULL;

	/* debug */
	str = gs_app_to_string (app);
	g_debug ("%s", str);

	dialog = gs_update_dialog_new_for_app (self->plugin_loader, app);
	gs_shell_modal_dialog_present (gs_page_get_shell (self->page), GTK_WINDOW (dialog));
}

static void
gs_updates_section_show (GtkWidget *widget)
{
	_update_buttons (GS_UPDATES_SECTION (widget));

	GTK_WIDGET_CLASS (gs_updates_section_parent_class)->show (widget);
}

static void
gs_updates_section_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	GsUpdatesSection *self = GS_UPDATES_SECTION (object);

	switch ((GsUpdatesSectionProperty) prop_id) {
	case PROP_IS_NARROW:
		g_value_set_boolean (value, gs_updates_section_get_is_narrow (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_updates_section_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	GsUpdatesSection *self = GS_UPDATES_SECTION (object);

	switch ((GsUpdatesSectionProperty) prop_id) {
	case PROP_IS_NARROW:
		gs_updates_section_set_is_narrow (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_updates_section_dispose (GObject *object)
{
	GsUpdatesSection *self = GS_UPDATES_SECTION (object);

	g_clear_object (&self->cancellable);
	g_clear_object (&self->list);
	g_clear_object (&self->plugin_loader);
	g_clear_object (&self->page);
	g_clear_object (&self->sizegroup_name);
	g_clear_object (&self->sizegroup_button_label);
	g_clear_object (&self->sizegroup_button_image);
	g_clear_object (&self->sizegroup_header);
	self->button_download = NULL;
	self->button_update = NULL;
	self->button_cancel = NULL;
	self->button_stack = NULL;

	G_OBJECT_CLASS (gs_updates_section_parent_class)->dispose (object);
}

static void
gs_updates_section_class_init (GsUpdatesSectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_updates_section_get_property;
	object_class->set_property = gs_updates_section_set_property;
	object_class->dispose = gs_updates_section_dispose;
	widget_class->show = gs_updates_section_show;

	/**
	 * GsUpdatesSection:is-narrow:
	 *
	 * Whether the section is in narrow mode.
	 *
	 * In narrow mode, the section will take up less horizontal space, doing
	 * so by e.g. using icons rather than labels in buttons. This is needed
	 * to keep the UI useable on small form-factors like smartphones.
	 *
	 * Since: 41
	 */
	obj_props[PROP_IS_NARROW] =
		g_param_spec_boolean ("is-narrow", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-updates-section.ui");

	gtk_widget_class_bind_template_child (widget_class, GsUpdatesSection, button_cancel);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesSection, button_download);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesSection, button_stack);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesSection, button_update);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesSection, description);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesSection, listbox);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesSection, listbox_box);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesSection, section_header);
	gtk_widget_class_bind_template_child (widget_class, GsUpdatesSection, title);
	gtk_widget_class_bind_template_callback (widget_class, _app_row_activated_cb);
	gtk_widget_class_bind_template_callback (widget_class, _button_cancel_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, _button_download_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, _button_update_all_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, _listbox_keynav_failed_cb);
}

void
gs_updates_section_set_size_groups (GsUpdatesSection *self,
				    GtkSizeGroup *name,
				    GtkSizeGroup *button_label,
				    GtkSizeGroup *button_image,
				    GtkSizeGroup *header)
{
	g_return_if_fail (GS_IS_UPDATES_SECTION (self));

	g_set_object (&self->sizegroup_name, name);
	g_set_object (&self->sizegroup_button_label, button_label);
	g_set_object (&self->sizegroup_button_image, button_image);
	g_set_object (&self->sizegroup_header, header);

	gs_progress_button_set_size_groups (GS_PROGRESS_BUTTON (self->button_cancel), button_label, button_image);
	gs_progress_button_set_size_groups (GS_PROGRESS_BUTTON (self->button_download), button_label, button_image);
	gs_progress_button_set_size_groups (GS_PROGRESS_BUTTON (self->button_update), button_label, button_image);
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
gs_updates_section_app_state_changed_cb (GsAppList *list,
					 GsApp *in_app,
					 GsUpdatesSection *self)
{
	guint ii, len, busy = 0;

	len = gs_app_list_length (list);

	for (ii = 0; ii < len; ii++) {
		GsApp *app = gs_app_list_index (list, ii);
		GsAppState state = gs_app_get_state (app);

		if (state == GS_APP_STATE_INSTALLING ||
		    state == GS_APP_STATE_REMOVING) {
			busy++;
		}
	}

	gtk_widget_set_sensitive (self->button_update, busy < len);
}

static void
gs_updates_section_init (GsUpdatesSection *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	self->list = gs_app_list_new ();
	gs_app_list_add_flag (self->list,
			      GS_APP_LIST_FLAG_WATCH_APPS |
			      GS_APP_LIST_FLAG_WATCH_APPS_ADDONS |
			      GS_APP_LIST_FLAG_WATCH_APPS_RELATED);
	g_signal_connect_object (self->list, "notify::progress",
				 G_CALLBACK (gs_updates_section_progress_notify_cb),
				 self, 0);
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (self->listbox),
					 GTK_SELECTION_NONE);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (self->listbox),
				    _list_sort_func,
				    self, NULL);
}

/**
 * gs_updates_section_get_is_narrow:
 * @self: a #GsUpdatesSection
 *
 * Get the value of #GsUpdatesSection:is-narrow.
 *
 * Returns: %TRUE if the section is in narrow mode, %FALSE otherwise
 *
 * Since: 41
 */
gboolean
gs_updates_section_get_is_narrow (GsUpdatesSection *self)
{
	g_return_val_if_fail (GS_IS_UPDATES_SECTION (self), FALSE);

	return self->is_narrow;
}

/**
 * gs_updates_section_set_is_narrow:
 * @self: a #GsUpdatesSection
 * @is_narrow: %TRUE to set the section in narrow mode, %FALSE otherwise
 *
 * Set the value of #GsUpdatesSection:is-narrow.
 *
 * Since: 41
 */
void
gs_updates_section_set_is_narrow (GsUpdatesSection *self, gboolean is_narrow)
{
	g_return_if_fail (GS_IS_UPDATES_SECTION (self));

	is_narrow = !!is_narrow;

	if (self->is_narrow == is_narrow)
		return;

	self->is_narrow = is_narrow;
	g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_IS_NARROW]);
}

GsUpdatesSection *
gs_updates_section_new (GsUpdatesSectionKind kind,
			GsPluginLoader *plugin_loader,
			GsPage *page)
{
	GsUpdatesSection *self;
	self = g_object_new (GS_TYPE_UPDATES_SECTION, NULL);
	self->kind = kind;
	self->plugin_loader = g_object_ref (plugin_loader);
	self->page = g_object_ref (page);
	_setup_section_header (self);

	if (self->kind == GS_UPDATES_SECTION_KIND_ONLINE) {
		g_signal_connect_object (self->list, "app-state-changed",
					 G_CALLBACK (gs_updates_section_app_state_changed_cb),
					 self, 0);
	}

	return self;
}
