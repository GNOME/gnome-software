/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <locale.h>

#include "gs-common.h"
#include "gs-css.h"
#include "gs-feature-tile.h"
#include "gs-summary-tile.h"
#include "gs-upgrade-banner.h"

typedef struct {
	GCancellable		*cancellable;
	GtkApplication		*application;
	GtkBuilder		*builder;
	GtkWidget		*featured_tile1;
	GtkWidget		*upgrade_banner;
	AsStore			*store;
	AsStore			*store_global;
	AsApp			*selected_item;
	AsApp			*deleted_item;
	gboolean		 is_in_refresh;
	gboolean		 pending_changes;
	guint			 refresh_details_delayed_id;
} GsEditor;

static gchar *
gs_editor_css_download_resources (GsEditor *self, const gchar *css, GError **error)
{
	g_autoptr(GsPlugin) plugin = NULL;
	g_autoptr(SoupSession) soup_session = NULL;

	/* make remote URIs local */
	plugin = gs_plugin_new ();
	gs_plugin_set_name (plugin, "editor");
	soup_session = soup_session_new_with_options (SOUP_SESSION_USER_AGENT, gs_user_agent (),
						      SOUP_SESSION_TIMEOUT, 10,
						      NULL);
	gs_plugin_set_soup_session (plugin, soup_session);
	return gs_plugin_download_rewrite_resource (plugin, NULL, css, NULL, error);
}

static gchar *
_css_rewrite_cb (gpointer user_data, const gchar *markup, GError **error)
{
	GsEditor *self = (GsEditor *) user_data;
	return gs_editor_css_download_resources (self, markup, error);
}

static gboolean
gs_design_validate_css (GsEditor *self, const gchar *markup, GError **error)
{
	g_autoptr(GsCss) css = gs_css_new ();
	gs_css_set_rewrite_func (css, _css_rewrite_cb, self);
	if (!gs_css_parse (css, markup, error))
		return FALSE;
	return gs_css_validate (css, error);
}

static void
gs_editor_refine_app_pixbuf (GsApp *app)
{
	GPtrArray *icons;
	if (gs_app_get_pixbuf (app) != NULL)
		return;
	icons = gs_app_get_icons (app);
	for (guint i = 0; i < icons->len; i++) {
		AsIcon *ic = g_ptr_array_index (icons, i);
		g_autoptr(GError) error = NULL;
		if (as_icon_get_kind (ic) == AS_ICON_KIND_STOCK) {

			g_autoptr(GdkPixbuf) pb = NULL;
			pb = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
						       as_icon_get_name (ic),
						       64,
						       GTK_ICON_LOOKUP_FORCE_SIZE,
						       &error);
			if (pb == NULL) {
				g_warning ("failed to load icon: %s", error->message);
				continue;
			}
			gs_app_set_pixbuf (app, pb);
		} else {
			if (!as_icon_load (ic, AS_ICON_LOAD_FLAG_SEARCH_SIZE, &error)) {
				g_warning ("failed to load icon: %s", error->message);
				continue;
			}
			gs_app_set_pixbuf (app, as_icon_get_pixbuf (ic));
		}
		break;
	}
}

static GsApp *
gs_editor_convert_app (GsEditor *self, AsApp *item)
{
	AsApp *item_global;
	AsAppState item_state;
	GsApp *app;
	const gchar *keys[] = {
		"GnomeSoftware::AppTile-css",
		"GnomeSoftware::FeatureTile-css",
		"GnomeSoftware::UpgradeBanner-css",
		NULL };

	/* copy name, summary and description */
	app = gs_app_new (as_app_get_id (item));
	item_global = as_store_get_app_by_id (self->store_global, as_app_get_id (item));
	if (item_global == NULL) {
		const gchar *tmp;
		g_autoptr(AsIcon) ic = NULL;
		g_debug ("no app found for %s, using fallback", as_app_get_id (item));

		/* copy from AsApp, falling back to something sane */
		tmp = as_app_get_name (item, NULL);
		if (tmp == NULL)
			tmp = "Application";
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, tmp);
		tmp = as_app_get_comment (item, NULL);
		if (tmp == NULL)
			tmp = "Description";
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, tmp);
		tmp = as_app_get_description (item, NULL);
		if (tmp == NULL)
			tmp = "A multiline description";
		gs_app_set_description (app, GS_APP_QUALITY_NORMAL, tmp);
		ic = as_icon_new ();
		as_icon_set_kind (ic, AS_ICON_KIND_STOCK);
		as_icon_set_name (ic, "application-x-executable");
		gs_app_add_icon (app, ic);
		item_state = as_app_get_state (item);
	} else {
		GPtrArray *icons;
		g_debug ("found global app for %s", as_app_get_id (item));
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL,
				 as_app_get_name (item_global, NULL));
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
				    as_app_get_comment (item_global, NULL));
		gs_app_set_description (app, GS_APP_QUALITY_NORMAL,
					as_app_get_description (item_global, NULL));
		icons = as_app_get_icons (item_global);
		for (guint i = 0; i < icons->len; i++) {
			AsIcon *icon = g_ptr_array_index (icons, i);
			gs_app_add_icon (app, icon);
		}
		item_state = as_app_get_state (item_global);
	}

	/* copy state */
	if (item_state == AS_APP_STATE_UNKNOWN)
		item_state = AS_APP_STATE_AVAILABLE;
	gs_app_set_state (app, item_state);

	/* copy version */
	gs_app_set_version (app, "3.28");

	/* load pixbuf */
	gs_editor_refine_app_pixbuf (app);

	/* copy metadata */
	for (guint i = 0; keys[i] != NULL; i++) {
		g_autoptr(GError) error = NULL;
		const gchar *markup = as_app_get_metadata_item (item, keys[i]);
		if (markup != NULL) {
			g_autofree gchar *css_new = NULL;
			css_new = gs_editor_css_download_resources (self, markup, &error);
			if (css_new == NULL) {
				g_warning ("%s", error->message);
				gs_app_set_metadata (app, keys[i], markup);
			} else {
				gs_app_set_metadata (app, keys[i], css_new);
			}
		} else {
			gs_app_set_metadata (app, keys[i], NULL);
		}
	}
	return app;
}

static void
gs_editor_refresh_details (GsEditor *self)
{
	AsAppKind app_kind = AS_APP_KIND_UNKNOWN;
	GtkWidget *widget;
	const gchar *css = NULL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app = NULL;

	/* ignore changed events */
	self->is_in_refresh = TRUE;

	/* create a GsApp for the AsApp */
	if (self->selected_item != NULL) {
		app = gs_editor_convert_app (self, self->selected_item);
		g_debug ("refreshing details for %s", gs_app_get_id (app));
	}

	/* get kind */
	if (self->selected_item != NULL)
		app_kind = as_app_get_kind (self->selected_item);

	/* feature tiles */
	if (app_kind != AS_APP_KIND_OS_UPGRADE) {
		if (self->selected_item != NULL) {
			gs_app_tile_set_app (GS_APP_TILE (self->featured_tile1), app);
			gtk_widget_set_sensitive (self->featured_tile1, TRUE);
		} else {
			gtk_widget_set_sensitive (self->featured_tile1, FALSE);
		}
		gtk_widget_set_visible (self->featured_tile1, TRUE);
	} else {
		gtk_widget_set_visible (self->featured_tile1, FALSE);
	}

	/* upgrade banner */
	if (app_kind == AS_APP_KIND_OS_UPGRADE) {
		if (self->selected_item != NULL) {
			gs_upgrade_banner_set_app (GS_UPGRADE_BANNER (self->upgrade_banner), app);
			gtk_widget_set_sensitive (self->upgrade_banner, TRUE);
		} else {
			gtk_widget_set_sensitive (self->upgrade_banner, FALSE);
		}
		gtk_widget_set_visible (self->upgrade_banner, TRUE);
	} else {
		gtk_widget_set_visible (self->upgrade_banner, FALSE);
	}

	/* name */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "box_name"));
	if (self->selected_item != NULL) {
		const gchar *tmp;
		gtk_widget_set_visible (widget, app_kind == AS_APP_KIND_OS_UPGRADE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "entry_name"));
		tmp = as_app_get_name (self->selected_item, NULL);
		if (tmp != NULL)
			gtk_entry_set_text (GTK_ENTRY (widget), tmp);
	} else {
		gtk_widget_set_visible (widget, FALSE);
	}

	/* summary */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "box_summary"));
	if (self->selected_item != NULL) {
		const gchar *tmp;
		gtk_widget_set_visible (widget, app_kind == AS_APP_KIND_OS_UPGRADE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "entry_summary"));
		tmp = as_app_get_comment (self->selected_item, NULL);
		if (tmp != NULL)
			gtk_entry_set_text (GTK_ENTRY (widget), tmp);
	} else {
		gtk_widget_set_visible (widget, FALSE);
	}

	/* kudos */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "box_kudos"));
	if (self->selected_item != NULL) {
		gtk_widget_set_visible (widget, app_kind != AS_APP_KIND_OS_UPGRADE);
	} else {
		gtk_widget_set_visible (widget, TRUE);
	}

	/* category featured */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "checkbutton_category_featured"));
	if (self->selected_item != NULL) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget),
					      as_app_has_category (self->selected_item,
								   "Featured"));
		gtk_widget_set_sensitive (widget, TRUE);
	} else {
		gtk_widget_set_sensitive (widget, FALSE);
	}

	/* kudo popular */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "checkbutton_editors_pick"));
	if (self->selected_item != NULL) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget),
					      as_app_has_kudo (self->selected_item,
							       "GnomeSoftware::popular"));
		gtk_widget_set_sensitive (widget, TRUE);
	} else {
		gtk_widget_set_sensitive (widget, FALSE);
	}

	/* featured */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "textview_css"));
	if (self->selected_item != NULL) {
		GtkTextBuffer *buffer;
		GtkTextIter iter_end;
		GtkTextIter iter_start;
		g_autofree gchar *css_existing = NULL;

		if (app_kind == AS_APP_KIND_OS_UPGRADE) {
			css = as_app_get_metadata_item (self->selected_item,
							"GnomeSoftware::UpgradeBanner-css");
		} else {
			css = as_app_get_metadata_item (self->selected_item,
							"GnomeSoftware::FeatureTile-css");
		}
		if (css == NULL)
			css = "";
		buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (widget));
		gtk_text_buffer_get_bounds (buffer, &iter_start, &iter_end);
		css_existing = gtk_text_buffer_get_text (buffer, &iter_start, &iter_end, FALSE);
		if (g_strcmp0 (css_existing, css) != 0)
			gtk_text_buffer_set_text (buffer, css, -1);
		gtk_widget_set_sensitive (widget, TRUE);
	} else {
		gtk_widget_set_sensitive (widget, FALSE);
	}

	/* desktop ID */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "entry_desktop_id"));
	if (self->selected_item != NULL) {
		const gchar *id = as_app_get_id (self->selected_item);
		if (id == NULL)
			id = "";
		gtk_entry_set_text (GTK_ENTRY (widget), id);
		gtk_widget_set_sensitive (widget, TRUE);
	} else {
		gtk_entry_set_text (GTK_ENTRY (widget), "");
		gtk_widget_set_sensitive (widget, FALSE);
	}

	/* validate CSS */
	if (css == NULL) {
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "label_infobar_css"));
		gtk_label_set_label (GTK_LABEL (widget), "");
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "infobar_css"));
		gtk_info_bar_set_message_type (GTK_INFO_BAR (widget), GTK_MESSAGE_OTHER);
	} else if (!gs_design_validate_css (self, css, &error)) {
		g_autofree gchar *msg = g_strdup (error->message);
		g_strdelimit (msg, "\n\r<>", '\0');
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "label_infobar_css"));
		gtk_label_set_label (GTK_LABEL (widget), msg);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "infobar_css"));
		gtk_info_bar_set_message_type (GTK_INFO_BAR (widget), GTK_MESSAGE_WARNING);
	} else {
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "label_infobar_css"));
		gtk_label_set_label (GTK_LABEL (widget), _("CSS validated OK!"));
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "infobar_css"));
		gtk_info_bar_set_message_type (GTK_INFO_BAR (widget), GTK_MESSAGE_OTHER);
	}

	/* do not ignore changed events */
	self->is_in_refresh = FALSE;
}

static gboolean
gs_design_dialog_refresh_details_delayed_cb (gpointer user_data)
{
	GsEditor *self = (GsEditor *) user_data;
	gs_editor_refresh_details (self);
	self->refresh_details_delayed_id = 0;
	return FALSE;
}

static void
gs_design_dialog_refresh_details_delayed (GsEditor *self)
{
	if (self->refresh_details_delayed_id != 0)
		g_source_remove (self->refresh_details_delayed_id);
	self->refresh_details_delayed_id = g_timeout_add (500,
		gs_design_dialog_refresh_details_delayed_cb, self);
}

static void
gs_design_dialog_buffer_changed_cb (GtkTextBuffer *buffer, GsEditor *self)
{
	GtkTextIter iter_end;
	GtkTextIter iter_start;
	g_autofree gchar *css = NULL;

	/* ignore, self change */
	if (self->is_in_refresh)
		return;

	gtk_text_buffer_get_bounds (buffer, &iter_start, &iter_end);
	css = gtk_text_buffer_get_text (buffer, &iter_start, &iter_end, FALSE);
	g_debug ("CSS now '%s'", css);
	if (as_app_get_kind (self->selected_item) == AS_APP_KIND_OS_UPGRADE) {
		as_app_add_metadata (self->selected_item, "GnomeSoftware::UpgradeBanner-css", NULL);
		as_app_add_metadata (self->selected_item, "GnomeSoftware::UpgradeBanner-css", css);
	} else {
		as_app_add_metadata (self->selected_item, "GnomeSoftware::FeatureTile-css", NULL);
		as_app_add_metadata (self->selected_item, "GnomeSoftware::FeatureTile-css", css);
	}
	self->pending_changes = TRUE;
	gs_design_dialog_refresh_details_delayed (self);
}

static void
gs_editor_set_page (GsEditor *self, const gchar *name)
{
	GtkWidget *widget;

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "stack_main"));
	gtk_stack_set_visible_child_name (GTK_STACK (widget), name);

	if (g_strcmp0 (name, "none") == 0) {
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_back"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_new"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_import"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_save"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_search"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_remove"));
		gtk_widget_set_visible (widget, FALSE);

	} else if (g_strcmp0 (name, "choice") == 0) {
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_back"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_new"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_import"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_save"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_search"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_remove"));
		gtk_widget_set_visible (widget, FALSE);

	} else if (g_strcmp0 (name, "details") == 0) {
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_back"));
		gtk_widget_set_visible (widget, TRUE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_new"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_import"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_save"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_search"));
		gtk_widget_set_visible (widget, FALSE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_remove"));
		gtk_widget_set_visible (widget, TRUE);
	}
}

static void
gs_editor_app_tile_clicked_cb (GsAppTile *tile, GsEditor *self)
{
	GsApp *app = gs_app_tile_get_app (tile);
	AsApp *item = as_store_get_app_by_id (self->store, gs_app_get_id (app));
	if (item == NULL) {
		g_warning ("failed to find %s", gs_app_get_id (app));
		return;
	}
	g_set_object (&self->selected_item, item);

	gs_editor_refresh_details (self);
	gs_editor_set_page (self, "details");
}

static void
gs_editor_refresh_choice (GsEditor *self)
{
	GPtrArray *apps;
	GtkContainer *container;

	/* add all apps */
	container = GTK_CONTAINER (gtk_builder_get_object (self->builder,
							   "flowbox_main"));
	gs_container_remove_all (GTK_CONTAINER (container));
	apps = as_store_get_apps (self->store);
	for (guint i = 0; i < apps->len; i++) {
		AsApp *item = g_ptr_array_index (apps, i);
		GtkWidget *tile = NULL;
		g_autoptr(GsApp) app = NULL;

		app = gs_editor_convert_app (self, item);
		tile = gs_summary_tile_new (app);
		g_signal_connect (tile, "clicked",
				  G_CALLBACK (gs_editor_app_tile_clicked_cb),
				  self);
		gtk_widget_set_visible (tile, TRUE);
		gtk_widget_set_vexpand (tile, FALSE);
		gtk_widget_set_hexpand (tile, FALSE);
		gtk_widget_set_size_request (tile, 300, 50);
		gtk_widget_set_valign (tile, GTK_ALIGN_START);
		gtk_container_add (GTK_CONTAINER (container), tile);
	}
}

static void
gs_editor_error_message (GsEditor *self, const gchar *title, const gchar *message)
{
	GtkWidget *dialog;
	GtkWindow *window;
	window = GTK_WINDOW (gtk_builder_get_object (self->builder, "window_main"));
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL |
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_OK,
					 "%s", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  "%s", message);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

static void
gs_editor_button_back_clicked_cb (GtkWidget *widget, GsEditor *self)
{
	gs_editor_set_page (self, as_store_get_size (self->store) == 0 ? "none" : "choice");
}

static void
gs_editor_button_menu_clicked_cb (GtkWidget *widget, GsEditor *self)
{
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "popover_menu"));
	gtk_popover_popup (GTK_POPOVER (widget));
}

static void
gs_editor_refresh_file (GsEditor *self, GFile *file)
{
	GtkWidget *widget;

	/* set subtitle */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "headerbar_main"));
	if (file != NULL) {
		g_autofree gchar *basename = g_file_get_basename (file);
		gtk_header_bar_set_subtitle (GTK_HEADER_BAR (widget), basename);
	} else {
		gtk_header_bar_set_subtitle (GTK_HEADER_BAR (widget), NULL);
	}
}

static void
gs_editor_button_import_file (GsEditor *self, GFile *file)
{
	g_autoptr(GError) error = NULL;

	/* load new file */
	if (!as_store_from_file (self->store, file, NULL, NULL, &error)) {
		/* TRANSLATORS: error dialog title */
		gs_editor_error_message (self, _("Failed to load file"), error->message);
		return;
	}

	/* update listview */
	gs_editor_refresh_choice (self);
	gs_editor_refresh_file (self, file);

	/* set the appropriate page */
	gs_editor_set_page (self, as_store_get_size (self->store) == 0 ? "none" : "choice");

	/* reset */
	self->pending_changes = FALSE;
}

static void
gs_editor_button_import_clicked_cb (GtkApplication *application, GsEditor *self)
{
	GtkFileFilter *filter;
	GtkWindow *window;
	GtkWidget *dialog;
	gint res;
	g_autoptr(GFile) file = NULL;

	/* import warning */
	window = GTK_WINDOW (gtk_builder_get_object (self->builder,
						     "window_main"));
	if (as_store_get_size (self->store) > 0) {
		dialog = gtk_message_dialog_new (window,
						 GTK_DIALOG_MODAL |
						 GTK_DIALOG_DESTROY_WITH_PARENT,
						 GTK_MESSAGE_WARNING,
						 GTK_BUTTONS_CANCEL,
						 /* TRANSLATORS: window title */
						 _("Unsaved changes"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  _("The application list is already loaded."));

		gtk_dialog_add_button (GTK_DIALOG (dialog),
				       /* TRANSLATORS: button text */
				       _("Merge documents"),
				       GTK_RESPONSE_ACCEPT);
		gtk_dialog_add_button (GTK_DIALOG (dialog),
				       /* TRANSLATORS: button text */
				       _("Throw away changes"),
				       GTK_RESPONSE_YES);
		res = gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);

		if (res == GTK_RESPONSE_CANCEL)
			return;
		if (res == GTK_RESPONSE_YES)
			as_store_remove_all (self->store);
	}

	/* import the new file */
	dialog = gtk_file_chooser_dialog_new (_("Open AppStream File"),
					      window,
					      GTK_FILE_CHOOSER_ACTION_OPEN,
					      _("_Cancel"), GTK_RESPONSE_CANCEL,
					      _("_Open"), GTK_RESPONSE_ACCEPT,
					      NULL);
	filter = gtk_file_filter_new ();
	gtk_file_filter_add_pattern (filter, "*.xml");
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter);
	res = gtk_dialog_run (GTK_DIALOG (dialog));
	if (res != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy (dialog);
		return;
	}
	file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
	gs_editor_button_import_file (self, file);
	gtk_widget_destroy (dialog);
}

static void
gs_editor_button_save_clicked_cb (GtkApplication *application, GsEditor *self)
{
	GtkFileFilter *filter;
	GtkWidget *dialog;
	GtkWindow *window;
	gint res;
	g_autoptr(GError) error = NULL;
	g_autoptr(GFile) file = NULL;

	/* export a new file */
	window = GTK_WINDOW (gtk_builder_get_object (self->builder,
						     "window_main"));
	dialog = gtk_file_chooser_dialog_new (_("Open AppStream File"),
					      window,
					      GTK_FILE_CHOOSER_ACTION_SAVE,
					      _("_Cancel"), GTK_RESPONSE_CANCEL,
					      _("_Save"), GTK_RESPONSE_ACCEPT,
					      NULL);
	filter = gtk_file_filter_new ();
	gtk_file_filter_add_pattern (filter, "*.xml");
	gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (dialog), filter);
	res = gtk_dialog_run (GTK_DIALOG (dialog));
	if (res != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy (dialog);
		return;
	}
	file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
	gtk_widget_destroy (dialog);
	if (!as_store_to_file (self->store,
			       file,
			       AS_NODE_TO_XML_FLAG_ADD_HEADER |
			       AS_NODE_TO_XML_FLAG_FORMAT_MULTILINE |
			       AS_NODE_TO_XML_FLAG_FORMAT_INDENT,
			       self->cancellable,
			       &error)) {
		/* TRANSLATORS: error dialog title */
		gs_editor_error_message (self, _("Failed to save file"), error->message);
		return;
	}
	self->pending_changes = FALSE;
	gs_editor_refresh_file (self, file);
	gs_editor_refresh_details (self);
}

static void
gs_editor_show_notification (GsEditor *self, const gchar *text)
{
	GtkWidget *widget;

	/* set text */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "label_notification"));
	gtk_label_set_markup (GTK_LABEL (widget), text);

	/* show button: FIXME, use flags? */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_notification_undo_remove"));
	gtk_widget_set_visible (widget, TRUE);

	/* show revealer */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "revealer_notification"));
	gtk_revealer_set_reveal_child (GTK_REVEALER (widget), TRUE);
}

static void
gs_editor_button_notification_dismiss_clicked_cb (GtkWidget *widget, GsEditor *self)
{
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "revealer_notification"));
	gtk_revealer_set_reveal_child (GTK_REVEALER (widget), FALSE);
}

static void
gs_editor_button_undo_remove_clicked_cb (GtkWidget *widget, GsEditor *self)
{
	if (self->deleted_item == NULL)
		return;

	/* add this back to the store and set it as current */
	as_store_add_app (self->store, self->deleted_item);
	g_set_object (&self->selected_item, self->deleted_item);
	g_clear_object (&self->deleted_item);

	/* hide notification */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "revealer_notification"));
	gtk_revealer_set_reveal_child (GTK_REVEALER (widget), FALSE);

	self->pending_changes = TRUE;
	gs_editor_refresh_choice (self);
	gs_editor_refresh_details (self);
	gs_editor_set_page (self, "details");
}

static void
gs_editor_button_remove_clicked_cb (GtkWidget *widget, GsEditor *self)
{
	const gchar *name;
	g_autofree gchar *msg = NULL;

	if (self->selected_item == NULL)
		return;

	/* send notification */
	name = as_app_get_name (self->selected_item, NULL);
	if (name == NULL) {
		AsApp *item_global = as_store_get_app_by_id (self->store_global,
							     as_app_get_id (self->selected_item));
		if (item_global != NULL)
			name = as_app_get_name (item_global, NULL);
	}
	if (name != NULL) {
		g_autofree gchar *name_markup = NULL;
		name_markup = g_strdup_printf ("<b>%s</b>", name);
		/* TRANSLATORS, the %s is the app name, e.g. 'Inkscape' */
		msg = g_strdup_printf (_("%s banner design deleted."), name_markup);
	} else {
		/* TRANSLATORS, this is a notification */
		msg = g_strdup (_("Banner design deleted."));
	}
	gs_editor_show_notification (self, msg);

	/* save this so we can undo */
	g_set_object (&self->deleted_item, self->selected_item);

	as_store_remove_app_by_id (self->store, as_app_get_id (self->selected_item));
	self->pending_changes = TRUE;
	gs_editor_refresh_choice (self);

	/* set the appropriate page */
	gs_editor_set_page (self, as_store_get_size (self->store) == 0 ? "none" : "choice");
}

static void
gs_editor_checkbutton_editors_pick_cb (GtkToggleButton *widget, GsEditor *self)
{
	/* ignore, self change */
	if (self->is_in_refresh)
		return;
	if (self->selected_item == NULL)
		return;

	if (gtk_toggle_button_get_active (widget)) {
		as_app_add_kudo (self->selected_item, "GnomeSoftware::popular");
	} else {
		as_app_remove_kudo (self->selected_item, "GnomeSoftware::popular");
	}
	self->pending_changes = TRUE;
	gs_editor_refresh_details (self);
}

static void
gs_editor_checkbutton_category_featured_cb (GtkToggleButton *widget, GsEditor *self)
{
	/* ignore, self change */
	if (self->is_in_refresh)
		return;
	if (self->selected_item == NULL)
		return;

	if (gtk_toggle_button_get_active (widget)) {
		as_app_add_category (self->selected_item, "Featured");
	} else {
		as_app_remove_category (self->selected_item, "Featured");
	}
	self->pending_changes = TRUE;
	gs_editor_refresh_details (self);
}

static void
gs_editor_entry_desktop_id_notify_cb (GtkEntry *entry, GParamSpec *pspec, GsEditor *self)
{
	/* ignore, self change */
	if (self->is_in_refresh)
		return;
	if (self->selected_item == NULL)
		return;

	/* check the name does not already exist */
	//FIXME

	as_store_remove_app (self->store, self->selected_item);
	as_app_set_id (self->selected_item, gtk_entry_get_text (entry));
	as_store_add_app (self->store, self->selected_item);

	self->pending_changes = TRUE;
	gs_editor_refresh_choice (self);
	gs_editor_refresh_details (self);
}

static void
gs_editor_entry_name_notify_cb (GtkEntry *entry, GParamSpec *pspec, GsEditor *self)
{
	/* ignore, self change */
	if (self->is_in_refresh)
		return;
	if (self->selected_item == NULL)
		return;

	as_app_set_name (self->selected_item, NULL, gtk_entry_get_text (entry));

	self->pending_changes = TRUE;
	gs_editor_refresh_choice (self);
	gs_editor_refresh_details (self);
}

static void
gs_editor_entry_summary_notify_cb (GtkEntry *entry, GParamSpec *pspec, GsEditor *self)
{
	/* ignore, self change */
	if (self->is_in_refresh)
		return;
	if (self->selected_item == NULL)
		return;

	as_app_set_comment (self->selected_item, NULL, gtk_entry_get_text (entry));

	self->pending_changes = TRUE;
	gs_editor_refresh_choice (self);
	gs_editor_refresh_details (self);
}

static gboolean
gs_editor_delete_event_cb (GtkWindow *window, GdkEvent *event, GsEditor *self)
{
	GtkWidget *dialog;
	gint res;

	if (!self->pending_changes)
		return FALSE;

	/* ask for confirmation */
	dialog = gtk_message_dialog_new (window,
					 GTK_DIALOG_MODAL |
					 GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_CANCEL,
					 /* TRANSLATORS: window title */
					 _("Unsaved changes"));
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
						  _("The application list has unsaved changes."));
	gtk_dialog_add_button (GTK_DIALOG (dialog),
			       /* TRANSLATORS: button text */
			       _("Throw away changes"),
			       GTK_RESPONSE_CLOSE);
	res = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	if (res == GTK_RESPONSE_CLOSE)
		return FALSE;
	return TRUE;
}

static gint
gs_editor_flow_box_sort_cb (GtkFlowBoxChild *row1, GtkFlowBoxChild *row2, gpointer user_data)
{
	GsAppTile *tile1 = GS_APP_TILE (gtk_bin_get_child (GTK_BIN (row1)));
	GsAppTile *tile2 = GS_APP_TILE (gtk_bin_get_child (GTK_BIN (row2)));
	return g_strcmp0 (gs_app_get_name (gs_app_tile_get_app (tile1)),
			  gs_app_get_name (gs_app_tile_get_app (tile2)));
}

static void
gs_editor_load_completion_model (GsEditor *self)
{
	GPtrArray *apps;
	GtkListStore *store;
	GtkTreeIter iter;

	store = GTK_LIST_STORE (gtk_builder_get_object (self->builder, "liststore_ids"));
	apps = as_store_get_apps (self->store_global);
	for (guint i = 0; i < apps->len; i++) {
		AsApp *item = g_ptr_array_index (apps, i);
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, 0, as_app_get_id (item), -1);
	}
}

static void
gs_editor_button_new_feature_clicked_cb (GtkApplication *application, GsEditor *self)
{
	g_autofree gchar *id = NULL;
	g_autoptr(AsApp) item = as_app_new ();
	const gchar *css = "border: 1px solid #808080;\nbackground: #eee;\ncolor: #000;";

	/* add new app */
	as_app_set_kind (item, AS_APP_KIND_DESKTOP);
	id = g_strdup_printf ("example-%04x.desktop",
			      (guint) g_random_int_range (0x0000, 0xffff));
	as_app_set_id (item, id);
	as_app_add_metadata (item, "GnomeSoftware::FeatureTile-css", css);
	as_app_add_kudo (item, "GnomeSoftware::popular");
	as_app_add_category (item, "Featured");
	as_store_add_app (self->store, item);
	g_set_object (&self->selected_item, item);

	self->pending_changes = TRUE;
	gs_editor_refresh_choice (self);
	gs_editor_refresh_details (self);
	gs_editor_set_page (self, "details");
}

static void
gs_editor_button_new_os_upgrade_clicked_cb (GtkApplication *application, GsEditor *self)
{
	g_autofree gchar *id = NULL;
	g_autoptr(AsApp) item = as_app_new ();
	const gchar *css = "border: 1px solid #808080;\nbackground: #fffeee;\ncolor: #000;";

	/* add new app */
	as_app_set_kind (item, AS_APP_KIND_OS_UPGRADE);
	as_app_set_state (item, AS_APP_STATE_AVAILABLE);
	as_app_set_id (item, "org.gnome.release");
	as_app_set_name (item, NULL, "GNOME");
	as_app_set_comment (item, NULL, "A major upgrade, with new features and added polish.");
	as_app_add_metadata (item, "GnomeSoftware::UpgradeBanner-css", css);
	as_store_add_app (self->store, item);
	g_set_object (&self->selected_item, item);

	self->pending_changes = TRUE;
	gs_editor_refresh_choice (self);
	gs_editor_refresh_details (self);
	gs_editor_set_page (self, "details");
}

static void
gs_editor_button_new_clicked_cb (GtkWidget *widget, GsEditor *self)
{
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "popover_new"));
	gtk_popover_popup (GTK_POPOVER (widget));
}

static void
gs_editor_startup_cb (GtkApplication *application, GsEditor *self)
{
	GtkTextBuffer *buffer;
	GtkWidget *main_window;
	GtkWidget *widget;
	gboolean ret;
	guint retval;
	g_autoptr(GError) error = NULL;

	/* get UI */
	retval = gtk_builder_add_from_resource (self->builder,
						"/org/gnome/Software/Editor/gs-editor.ui",
						&error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		return;
	}

	/* load all system appstream */
	as_store_set_add_flags (self->store_global, AS_STORE_ADD_FLAG_USE_MERGE_HEURISTIC);
	ret = as_store_load (self->store_global,
			     AS_STORE_LOAD_FLAG_IGNORE_INVALID |
			     AS_STORE_LOAD_FLAG_APP_INFO_SYSTEM |
			     AS_STORE_LOAD_FLAG_APPDATA |
			     AS_STORE_LOAD_FLAG_DESKTOP,
			     self->cancellable,
			     &error);
	if (!ret) {
		g_warning ("failed to load global store: %s", error->message);
		return;
	}

	/* load all the IDs into the completion model */
	gs_editor_load_completion_model (self);

	self->featured_tile1 = gs_feature_tile_new (NULL);
	self->upgrade_banner = gs_upgrade_banner_new ();
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "box_featured"));
	gtk_container_add (GTK_CONTAINER (widget), self->featured_tile1);
	gtk_container_add (GTK_CONTAINER (widget), self->upgrade_banner);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "textview_css"));
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (widget));
	g_signal_connect (buffer, "changed",
			  G_CALLBACK (gs_design_dialog_buffer_changed_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "flowbox_main"));
	gtk_flow_box_set_sort_func (GTK_FLOW_BOX (widget),
				    gs_editor_flow_box_sort_cb,
				    self, NULL);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_save"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_editor_button_save_clicked_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_new_feature"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_editor_button_new_feature_clicked_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_new_os_upgrade"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_editor_button_new_os_upgrade_clicked_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_new"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_editor_button_new_clicked_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_remove"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_editor_button_remove_clicked_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_import"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_editor_button_import_clicked_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_back"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_editor_button_back_clicked_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_menu"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_editor_button_menu_clicked_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_notification_dismiss"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_editor_button_notification_dismiss_clicked_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "button_notification_undo_remove"));
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gs_editor_button_undo_remove_clicked_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder,
						     "checkbutton_editors_pick"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gs_editor_checkbutton_editors_pick_cb), self);
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder,
						     "checkbutton_category_featured"));
	g_signal_connect (widget, "toggled",
			  G_CALLBACK (gs_editor_checkbutton_category_featured_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "entry_desktop_id"));
	g_signal_connect (widget, "notify::text",
			  G_CALLBACK (gs_editor_entry_desktop_id_notify_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "entry_name"));
	g_signal_connect (widget, "notify::text",
			  G_CALLBACK (gs_editor_entry_name_notify_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "entry_summary"));
	g_signal_connect (widget, "notify::text",
			  G_CALLBACK (gs_editor_entry_summary_notify_cb), self);

	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "window_main"));
	g_signal_connect (widget, "delete_event",
			  G_CALLBACK (gs_editor_delete_event_cb), self);

	/* clear entries */
	gs_editor_refresh_choice (self);
	gs_editor_refresh_details (self);
	gs_editor_refresh_file (self, NULL);

	/* set the appropriate page */
	gs_editor_set_page (self, "none");

	main_window = GTK_WIDGET (gtk_builder_get_object (self->builder, "window_main"));
	gtk_application_add_window (application, GTK_WINDOW (main_window));
	gtk_widget_show (main_window);
}


static int
gs_editor_commandline_cb (GApplication *application,
			  GApplicationCommandLine *cmdline,
			  GsEditor *self)
{
	GtkWindow *window;
	gint argc;
	gboolean verbose = FALSE;
	g_auto(GStrv) argv = NULL;
	g_autoptr(GOptionContext) context = NULL;
	const GOptionEntry options[] = {
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose,
		  /* TRANSLATORS: show the program version */
		  _("Use verbose logging"), NULL },
		{ NULL}
	};

	/* get arguments */
	argv = g_application_command_line_get_arguments (cmdline, &argc);
	context = g_option_context_new (NULL);
	/* TRANSLATORS: program name, an application to add and remove software repositories */
	g_option_context_set_summary(context, _("GNOME Software Banner Designer"));
	g_option_context_add_main_entries (context, options, NULL);
	if (!g_option_context_parse (context, &argc, &argv, NULL))
		return FALSE;

	/* simple logging... */
	if (verbose)
		g_setenv ("G_MESSAGES_DEBUG", "Gs", TRUE);

	/* make sure the window is raised */
	window = GTK_WINDOW (gtk_builder_get_object (self->builder, "window_main"));
	gtk_window_present (window);

	return TRUE;
}

static void
gs_editor_self_free (GsEditor *self)
{
	if (self->selected_item != NULL)
		g_object_unref (self->selected_item);
	if (self->deleted_item != NULL)
		g_object_unref (self->deleted_item);
	if (self->refresh_details_delayed_id != 0)
		g_source_remove (self->refresh_details_delayed_id);
	g_object_unref (self->cancellable);
	g_object_unref (self->store);
	g_object_unref (self->store_global);
	g_object_unref (self->builder);
	g_free (self);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GsEditor, gs_editor_self_free)

int
main (int argc, char *argv[])
{
	g_autoptr(GsEditor) self = NULL;

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	gtk_init (&argc, &argv);

	self = g_new0 (GsEditor, 1);
	self->cancellable = g_cancellable_new ();
	self->builder = gtk_builder_new ();
	self->store = as_store_new ();
	as_store_set_add_flags (self->store, AS_STORE_ADD_FLAG_USE_UNIQUE_ID);
	self->store_global = as_store_new ();
	as_store_set_add_flags (self->store_global, AS_STORE_ADD_FLAG_USE_UNIQUE_ID);

	/* are we already activated? */
	self->application = gtk_application_new ("org.gnome.Software.Editor",
						 G_APPLICATION_HANDLES_COMMAND_LINE);
	g_signal_connect (self->application, "startup",
			  G_CALLBACK (gs_editor_startup_cb), self);
	g_signal_connect (self->application, "command-line",
			  G_CALLBACK (gs_editor_commandline_cb), self);

	/* run */
	return g_application_run (G_APPLICATION (self->application), argc, argv);
}
