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
#include <xmlb.h>

#include "gs-common.h"
#include "gs-css.h"
#include "gs-feature-tile.h"
#include "gs-summary-tile.h"
#include "gs-upgrade-banner.h"

#include "gnome-software-private.h"

typedef struct {
	GCancellable		*cancellable;
	GtkApplication		*application;
	GtkBuilder		*builder;
	GtkWidget		*featured_tile1;
	GtkWidget		*upgrade_banner;
	GsPluginLoader		*plugin_loader;
	GsAppList		*store;
	GsApp			*selected_app;
	GsApp			*deleted_app;
	gboolean		 is_in_refresh;
	gboolean		 pending_changes;
	guint			 refresh_details_delayed_id;
	guint			 autosave_id;
	GFile			*autosave_file;
} GsEditor;

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

static GsApp *
gs_editor_node_to_app (XbNode *component)
{
	g_autoptr(GsApp) app = gs_app_new (NULL);
	const gchar *tmp;
	const gchar *keys[] = {
		"GnomeSoftware::AppTile-css",
		"GnomeSoftware::FeatureTile-css",
		"GnomeSoftware::UpgradeBanner-css",
		NULL };

	/* <id> */
	tmp = xb_node_query_text (component, "id", NULL);
	if (tmp == NULL)
		return NULL;
	gs_app_set_id (app, tmp);

	/* <kudos><kudo>foo</kudo></kudos> */
	if (xb_node_query_text (component, "kudos/kudo[text()='GnomeSoftware::popular']", NULL) != NULL)
		gs_app_add_kudo (app, GS_APP_KUDO_POPULAR);

	/* <categories><category>Featured</category></categories> */
	if (xb_node_query_text (component, "categories/category[text()='Featured']", NULL) != NULL)
		gs_app_add_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED);

	/* <custom><value key="foo">bar</value></custom> */
	for (guint j = 0; keys[j] != NULL; j++) {
		g_autofree gchar *xpath = g_strdup_printf ("custom/value[@key='%s']", keys[j]);
		tmp = xb_node_query_text (component, xpath, NULL);
		if (tmp != NULL)
			gs_app_set_metadata (app, keys[j], tmp);
	}
	return g_steal_pointer (&app);
}

static XbBuilderNode *
gs_editor_app_to_node (GsApp *app)
{
	g_autoptr(XbBuilderNode) component =  xb_builder_node_new ("component");
	g_autoptr(XbBuilderNode) custom = NULL;
	const gchar *keys[] = {
		"GnomeSoftware::AppTile-css",
		"GnomeSoftware::FeatureTile-css",
		"GnomeSoftware::UpgradeBanner-css",
		NULL };

	/* <id> */
	xb_builder_node_insert_text (component, "id", gs_app_get_id (app), NULL);

	/* <categories><category>Featured</category></categories> */
	if (gs_app_has_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED)) {
		g_autoptr(XbBuilderNode) cats = NULL;
		cats = xb_builder_node_insert (component, "categories", NULL);
		xb_builder_node_insert_text (cats, "category", "Featured", NULL);
	}

	/* <kudos><kudo>foo</kudo></kudos> */
	if (gs_app_has_kudo (app, GS_APP_KUDO_POPULAR)) {
		g_autoptr(XbBuilderNode) kudos = NULL;
		kudos = xb_builder_node_insert (component, "kudos", NULL);
		xb_builder_node_insert_text (kudos, "category", "GnomeSoftware::popular", NULL);
	}

	/* <custom><value key="foo">bar</value></custom> */
	custom = xb_builder_node_insert (component, "custom", NULL);
	for (guint j = 0; keys[j] != NULL; j++) {
		g_autoptr(XbBuilderNode) value = xb_builder_node_new ("value");
		const gchar *tmp = gs_app_get_metadata_item (app, keys[j]);
		if (tmp == NULL)
			continue;

		/* add literal text */
		xb_builder_node_add_flag (value, XB_BUILDER_NODE_FLAG_LITERAL_TEXT);
		xb_builder_node_set_text (value, tmp, -1);
		xb_builder_node_set_attr (value, "key", keys[j]);
		xb_builder_node_add_child (custom, value);
	}

	return g_steal_pointer (&component);
}

static void
gs_editor_add_nodes_from_silo (GsEditor *self, XbSilo *silo)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) components = NULL;

	components = xb_silo_query (silo, "components/component", 0, &error);
	if (components == NULL) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return;
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return;
		/* TRANSLATORS: error dialog title */
		gs_editor_error_message (self, _("Failed to load components"), error->message);
		return;
	}
	for (guint i = 0; i < components->len; i++) {
		g_autoptr(GsApp) app = NULL;
		XbNode *component = g_ptr_array_index (components, i);
		app = gs_editor_node_to_app (component);
		if (app == NULL)
			continue;
		gs_app_list_add (self->store, app);
	}
}

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
gs_editor_copy_from_global_app (GsApp *app, GsApp *global_app)
{
	gs_app_set_state (app, AS_APP_STATE_UNKNOWN);

	/* nothing found */
	if (global_app == NULL) {
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "Application");
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "Description");
		gs_app_set_description (app, GS_APP_QUALITY_NORMAL, "A multiline description");
		gs_app_set_version (app, "3.28");
		gs_app_set_pixbuf (app, NULL);
		gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		return;
	}

	/* copy state */
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL,
			 gs_app_get_name (global_app));
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL,
			    gs_app_get_summary (global_app));
	gs_app_set_description (app, GS_APP_QUALITY_NORMAL,
				gs_app_get_description (global_app));
	gs_app_set_version (app, gs_app_get_version (global_app));
	gs_app_set_state (app, gs_app_get_state (global_app));
	gs_app_set_pixbuf (app, gs_app_get_pixbuf (global_app));
}

static void
gs_editor_refresh_details (GsEditor *self)
{
	AsAppKind app_kind = AS_APP_KIND_UNKNOWN;
	GtkWidget *widget;
	const gchar *css = NULL;
	g_autoptr(GError) error = NULL;

	/* ignore changed events */
	self->is_in_refresh = TRUE;

	/* get kind */
	if (self->selected_app != NULL)
		app_kind = gs_app_get_kind (self->selected_app);

	/* feature tiles */
	if (app_kind != AS_APP_KIND_OS_UPGRADE) {
		if (self->selected_app != NULL) {
			gs_app_tile_set_app (GS_APP_TILE (self->featured_tile1), self->selected_app);
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
		if (self->selected_app != NULL) {
			gs_upgrade_banner_set_app (GS_UPGRADE_BANNER (self->upgrade_banner), self->selected_app);
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
	if (self->selected_app != NULL) {
		const gchar *tmp;
		gtk_widget_set_visible (widget, app_kind == AS_APP_KIND_OS_UPGRADE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "entry_name"));
		tmp = gs_app_get_name (self->selected_app);
		if (tmp != NULL)
			gtk_entry_set_text (GTK_ENTRY (widget), tmp);
	} else {
		gtk_widget_set_visible (widget, FALSE);
	}

	/* summary */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "box_summary"));
	if (self->selected_app != NULL) {
		const gchar *tmp;
		gtk_widget_set_visible (widget, app_kind == AS_APP_KIND_OS_UPGRADE);
		widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "entry_summary"));
		tmp = gs_app_get_summary (self->selected_app);
		if (tmp != NULL)
			gtk_entry_set_text (GTK_ENTRY (widget), tmp);
	} else {
		gtk_widget_set_visible (widget, FALSE);
	}

	/* kudos */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "box_kudos"));
	if (self->selected_app != NULL) {
		gtk_widget_set_visible (widget, app_kind != AS_APP_KIND_OS_UPGRADE);
	} else {
		gtk_widget_set_visible (widget, TRUE);
	}

	/* category featured */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "checkbutton_category_featured"));
	if (self->selected_app != NULL) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget),
					      gs_app_has_kudo (self->selected_app,
							       GS_APP_KUDO_FEATURED_RECOMMENDED));
		gtk_widget_set_sensitive (widget, TRUE);
	} else {
		gtk_widget_set_sensitive (widget, FALSE);
	}

	/* kudo popular */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "checkbutton_editors_pick"));
	if (self->selected_app != NULL) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget),
					      gs_app_has_kudo (self->selected_app,
							       GS_APP_KUDO_POPULAR));
		gtk_widget_set_sensitive (widget, TRUE);
	} else {
		gtk_widget_set_sensitive (widget, FALSE);
	}

	/* featured */
	widget = GTK_WIDGET (gtk_builder_get_object (self->builder, "textview_css"));
	if (self->selected_app != NULL) {
		GtkTextBuffer *buffer;
		GtkTextIter iter_end;
		GtkTextIter iter_start;
		g_autofree gchar *css_existing = NULL;

		if (app_kind == AS_APP_KIND_OS_UPGRADE) {
			css = gs_app_get_metadata_item (self->selected_app,
							"GnomeSoftware::UpgradeBanner-css");
		} else {
			css = gs_app_get_metadata_item (self->selected_app,
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
	if (self->selected_app != NULL) {
		const gchar *id = gs_app_get_id (self->selected_app);
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

static XbSilo *
gs_editor_build_silo_from_apps (GsEditor *self)
{
	g_autoptr(XbBuilder) builder = xb_builder_new ();
	g_autoptr(XbBuilderNode) components = xb_builder_node_new ("components");

	/* add all apps */
	for (guint i = 0; i < gs_app_list_length (self->store); i++) {
		GsApp *app = gs_app_list_index (self->store, i);
		g_autoptr(XbBuilderNode) component = gs_editor_app_to_node (app);
		xb_builder_node_add_child (components, component);
	}
	xb_builder_import_node (builder, components);

	return xb_builder_compile (builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, NULL);
}

static void
gs_editor_autosave (GsEditor *self)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(XbSilo) silo = gs_editor_build_silo_from_apps (self);
	g_debug ("autosaving silo");
	if (!xb_silo_save_to_file (silo, self->autosave_file, NULL, &error))
		g_warning ("failed to autosave: %s", error->message);
}

static gboolean
gs_editor_autosave_cb (gpointer user_data)
{
	GsEditor *self = (GsEditor *) user_data;
	gs_editor_autosave (self);
	return TRUE;
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
	if (gs_app_get_kind (self->selected_app) == AS_APP_KIND_OS_UPGRADE) {
		gs_app_set_metadata (self->selected_app, "GnomeSoftware::UpgradeBanner-css", NULL);
		gs_app_set_metadata (self->selected_app, "GnomeSoftware::UpgradeBanner-css", css);
	} else {
		gs_app_set_metadata (self->selected_app, "GnomeSoftware::FeatureTile-css", NULL);
		gs_app_set_metadata (self->selected_app, "GnomeSoftware::FeatureTile-css", css);
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
	g_set_object (&self->selected_app, app);
	gs_editor_refresh_details (self);
	gs_editor_set_page (self, "details");
}

static void
gs_editor_refresh_choice (GsEditor *self)
{
	GtkContainer *container;

	/* add all apps */
	container = GTK_CONTAINER (gtk_builder_get_object (self->builder,
							   "flowbox_main"));
	gs_container_remove_all (GTK_CONTAINER (container));
	for (guint i = 0; i < gs_app_list_length (self->store); i++) {
		GsApp *app = gs_app_list_index (self->store, i);
		GtkWidget *tile = gs_summary_tile_new (app);
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
gs_editor_button_back_clicked_cb (GtkWidget *widget, GsEditor *self)
{
	gs_editor_set_page (self, gs_app_list_length (self->store) == 0 ? "none" : "choice");
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
gs_search_page_app_search_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GsApp) app = GS_APP (user_data);
	g_autoptr(GsAppList) list = NULL;

	list = gs_plugin_loader_job_process_finish (GS_PLUGIN_LOADER (source_object), res, &error);
	if (list == NULL) {
		if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			return;
		g_warning ("failed to get search app: %s", error->message);
		return;
	}
	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app_tmp = gs_app_list_index (list, i);
		if (g_strcmp0 (gs_app_get_id (app), gs_app_get_id (app_tmp)) == 0) {
			gs_editor_copy_from_global_app (app, app_tmp);
			return;
		}
	}
	if (gs_app_list_length (list) > 0) {
		GsApp *app_tmp = gs_app_list_index (list, 0);
		gs_editor_copy_from_global_app (app, app_tmp);
	}
}

static void
gs_editor_plugin_app_search (GsEditor *self, GsApp *app)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;
	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
					 "search", gs_app_get_id (app),
					 "max-results", 20,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
					 NULL);
	gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job, NULL,
					    gs_search_page_app_search_cb,
					    g_object_ref (app));
}

static gboolean
gs_editor_appstream_upgrade_cb (XbBuilderFixup *self,
				XbBuilderNode *bn,
				gpointer user_data,
				GError **error)
{
	if (g_strcmp0 (xb_builder_node_get_element (bn), "metadata") == 0)
		xb_builder_node_set_element (bn, "custom");
	return TRUE;
}

static void
gs_editor_button_import_file (GsEditor *self, GFile *file)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(XbSilo) silo = NULL;
	g_autoptr(XbBuilder) builder = xb_builder_new ();
	g_autoptr(XbBuilderFixup) fixup = NULL;
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();

	/* load new file */
	if (!xb_builder_source_load_file (source, file,
					  XB_BUILDER_SOURCE_FLAG_LITERAL_TEXT,
					  NULL, &error)) {
		/* TRANSLATORS: error dialog title */
		gs_editor_error_message (self, _("Failed to load file"), error->message);
		return;
	}

	/* fix up any legacy installed files */
	fixup = xb_builder_fixup_new ("AppStreamUpgrade",
				      gs_editor_appstream_upgrade_cb,
				      self, NULL);
	xb_builder_fixup_set_max_depth (fixup, 3);
	xb_builder_source_add_fixup (source, fixup);

	xb_builder_import_source (builder, source);
	silo = xb_builder_compile (builder, XB_BUILDER_COMPILE_FLAG_NONE, NULL, &error);
	if (silo == NULL) {
		/* TRANSLATORS: error dialog title */
		gs_editor_error_message (self, _("Failed to load file"), error->message);
		return;
	}

	/* add applications */
	gs_editor_add_nodes_from_silo (self, silo);

	/* update listview */
	gs_editor_refresh_choice (self);
	gs_editor_refresh_file (self, file);

	/* set the global app state */
	for (guint i = 0; i < gs_app_list_length (self->store); i++) {
		GsApp *app = gs_app_list_index (self->store, i);
		gs_editor_plugin_app_search (self, app);
	}

	/* set the appropriate page */
	gs_editor_set_page (self, gs_app_list_length (self->store) == 0 ? "none" : "choice");

	/* reset */
	gs_editor_autosave (self);
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
	if (gs_app_list_length (self->store) > 0) {
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
			gs_app_list_remove_all (self->store);
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
	g_autoptr(XbSilo) silo = NULL;

	/* export a new file */
	window = GTK_WINDOW (gtk_builder_get_object (self->builder,
						     "window_main"));
	dialog = gtk_file_chooser_dialog_new (_("Save AppStream File"),
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

	/* export as XML */
	silo = gs_editor_build_silo_from_apps (self);
	if (!xb_silo_export_file (silo, file,
				  XB_NODE_EXPORT_FLAG_ADD_HEADER |
				  XB_NODE_EXPORT_FLAG_FORMAT_MULTILINE |
				  XB_NODE_EXPORT_FLAG_FORMAT_INDENT,
				  NULL, &error)) {
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
	if (self->deleted_app == NULL)
		return;

	/* add this back to the store and set it as current */
	gs_app_list_add (self->store, self->deleted_app);
	g_set_object (&self->selected_app, self->deleted_app);
	g_clear_object (&self->deleted_app);

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

	if (self->selected_app == NULL)
		return;

	/* send notification */
	name = gs_app_get_name (self->selected_app);
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
	g_set_object (&self->deleted_app, self->selected_app);

	gs_app_list_remove (self->store, self->selected_app);
	self->pending_changes = TRUE;
	gs_editor_refresh_choice (self);

	/* set the appropriate page */
	gs_editor_set_page (self, gs_app_list_length (self->store) == 0 ? "none" : "choice");
}

static void
gs_editor_checkbutton_editors_pick_cb (GtkToggleButton *widget, GsEditor *self)
{
	/* ignore, self change */
	if (self->is_in_refresh)
		return;
	if (self->selected_app == NULL)
		return;

	if (gtk_toggle_button_get_active (widget)) {
		gs_app_add_kudo (self->selected_app, GS_APP_KUDO_POPULAR);
	} else {
		gs_app_remove_kudo (self->selected_app, GS_APP_KUDO_POPULAR);
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
	if (self->selected_app == NULL)
		return;

	if (gtk_toggle_button_get_active (widget)) {
		gs_app_add_kudo (self->selected_app, GS_APP_KUDO_FEATURED_RECOMMENDED);
	} else {
		gs_app_remove_kudo (self->selected_app, GS_APP_KUDO_FEATURED_RECOMMENDED);
	}
	self->pending_changes = TRUE;
	gs_editor_refresh_details (self);
}

static gboolean
gs_editor_get_search_filter_id_prefix_cb (GsApp *app, gpointer user_data)
{
	const gchar *id = (const gchar *) user_data;
	return g_str_has_prefix (gs_app_get_id (app), id);
}

static void
gs_editor_get_search_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GsEditor *self = (GsEditor *) user_data;
	GtkListStore *store;
	g_autoptr(GError) error = NULL;
	g_autoptr(GsAppList) list = NULL;

	/* get apps */
	list = gs_plugin_loader_job_process_finish (self->plugin_loader, res, &error);
	if (list == NULL) {
		if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED))
			return;
		g_warning ("failed to get search apps: %s", error->message);
		return;
	}

	/* only include the apps with the right ID prefix */
	gs_app_list_filter (list,
			    gs_editor_get_search_filter_id_prefix_cb,
			    gs_app_get_id (self->selected_app));

	/* load all the IDs into the completion model */
	store = GTK_LIST_STORE (gtk_builder_get_object (self->builder, "liststore_ids"));
	gtk_list_store_clear (store);
	if (gs_app_list_length (list) == 1) {
		GsApp *app = gs_app_list_index (list, 0);
		GtkWidget *w = GTK_WIDGET (gtk_builder_get_object (self->builder, "entry_desktop_id"));
		g_debug ("forcing completion: %s", gs_app_get_id (app));
		gtk_entry_set_text (GTK_ENTRY (w), gs_app_get_id (app));
	} else if (gs_app_list_length (list) > 0) {
		for (guint i = 0; i < gs_app_list_length (list); i++) {
			GsApp *app = gs_app_list_index (list, i);
			GtkTreeIter iter;
			gtk_list_store_append (store, &iter);
			g_debug ("adding completion: %s", gs_app_get_id (app));
			gtk_list_store_set (store, &iter, 0, gs_app_get_id (app), -1);
		}
	} else {
		g_debug ("nothing found");
	}

	/* get the "best" application for the icon and description */
	if (gs_app_list_length (list) > 0) {
		GsApp *app = gs_app_list_index (list, 0);
		g_debug ("setting global app %s", gs_app_get_unique_id (app));
		gs_editor_copy_from_global_app (self->selected_app, app);
	} else {
		gs_editor_copy_from_global_app (self->selected_app, NULL);
	}
	gs_editor_refresh_details (self);
}

static void
gs_editor_entry_desktop_id_notify_cb (GtkEntry *entry, GParamSpec *pspec, GsEditor *self)
{
	const gchar *tmp;

	/* ignore, self change */
	if (self->is_in_refresh)
		return;
	if (self->selected_app == NULL)
		return;

	/* get the new list of possible apps */
	tmp = gtk_entry_get_text (entry);
	if (tmp != NULL && strlen (tmp) > 3) {
		g_autoptr(GsPluginJob) plugin_job = NULL;
		plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_SEARCH,
						 "search", tmp,
						 "max-results", 20,
						 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_SETUP_ACTION |
								 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON,
						 NULL);
		gs_plugin_loader_job_process_async (self->plugin_loader, plugin_job, NULL,
						    gs_editor_get_search_cb,
						    self);
	}

	/* check the ID does not already exist */
	//FIXME

	gs_app_set_id (self->selected_app, tmp);

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
	if (self->selected_app == NULL)
		return;

	gs_app_set_name (self->selected_app, GS_APP_QUALITY_NORMAL, gtk_entry_get_text (entry));

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
	if (self->selected_app == NULL)
		return;

	gs_app_set_summary (self->selected_app, GS_APP_QUALITY_NORMAL, gtk_entry_get_text (entry));

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
gs_editor_button_new_feature_clicked_cb (GtkApplication *application, GsEditor *self)
{
	g_autofree gchar *id = NULL;
	g_autoptr(GsApp) app = gs_app_new (NULL);
	const gchar *css = "border: 1px solid #808080;\nbackground: #eee;\ncolor: #000;";

	/* add new app */
	gs_app_set_kind (app, AS_APP_KIND_DESKTOP);
	id = g_strdup_printf ("example-%04x.desktop",
			      (guint) g_random_int_range (0x0000, 0xffff));
	gs_app_set_id (app, id);
	gs_app_set_metadata (app, "GnomeSoftware::FeatureTile-css", css);
	gs_app_add_kudo (app, GS_APP_KUDO_POPULAR);
	gs_app_add_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED);
	gs_app_list_add (self->store, app);
	g_set_object (&self->selected_app, app);

	self->pending_changes = TRUE;
	gs_editor_refresh_choice (self);
	gs_editor_refresh_details (self);
	gs_editor_set_page (self, "details");
}

static void
gs_editor_button_new_os_upgrade_clicked_cb (GtkApplication *application, GsEditor *self)
{
	g_autoptr(GsApp) app = gs_app_new (NULL);
	const gchar *css = "border: 1px solid #808080;\nbackground: #fffeee;\ncolor: #000;";

	/* add new app */
	gs_app_set_kind (app, AS_APP_KIND_OS_UPGRADE);
	gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
	gs_app_set_id (app, "org.gnome.release");
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, "GNOME");
	gs_app_set_version (app, "3.40");
	gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, "A major upgrade, with new features and added polish.");
	gs_app_set_metadata (app, "GnomeSoftware::UpgradeBanner-css", css);
	gs_app_list_add (self->store, app);
	g_set_object (&self->selected_app, app);

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
	guint retval;
	g_autofree gchar *fn = NULL;
	g_autoptr(GError) error = NULL;

	/* get UI */
	retval = gtk_builder_add_from_resource (self->builder,
						"/org/gnome/Software/Editor/gs-editor.ui",
						&error);
	if (retval == 0) {
		g_warning ("failed to load ui: %s", error->message);
		return;
	}

	self->plugin_loader = gs_plugin_loader_new ();
	if (g_file_test (LOCALPLUGINDIR, G_FILE_TEST_EXISTS))
		gs_plugin_loader_add_location (self->plugin_loader, LOCALPLUGINDIR);
	if (!gs_plugin_loader_setup (self->plugin_loader, NULL, NULL, NULL, &error)) {
		g_warning ("Failed to setup plugins: %s", error->message);
		return;
	}

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

	/* load last saved state */
	fn = gs_utils_get_cache_filename ("editor", "autosave.xmlb",
					  GS_UTILS_CACHE_FLAG_WRITEABLE, &error);
	if (fn == NULL) {
		g_warning ("Failed to get cache location: %s", error->message);
		return;
	}
	self->autosave_file = g_file_new_for_path (fn);
	if (g_file_query_exists (self->autosave_file, NULL)) {
		g_autoptr(XbSilo) silo = xb_silo_new ();
		g_debug ("loading apps from %s", fn);
		if (!xb_silo_load_from_file (silo, self->autosave_file,
					     XB_SILO_LOAD_FLAG_NONE, NULL, &error))
			g_warning ("Failed to load silo: %s", error->message);
		gs_editor_add_nodes_from_silo (self, silo);
		for (guint i = 0; i < gs_app_list_length (self->store); i++) {
			GsApp *app = gs_app_list_index (self->store, i);
			gs_editor_plugin_app_search (self, app);
		}
	}

	/* clear entries */
	gs_editor_refresh_choice (self);
	gs_editor_refresh_details (self);
	gs_editor_refresh_file (self, NULL);

	/* set the appropriate page */
	gs_editor_set_page (self, gs_app_list_length (self->store) == 0 ? "none" : "choice");

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
	if (self->autosave_file != NULL)
		g_object_unref (self->autosave_file);
	if (self->selected_app != NULL)
		g_object_unref (self->selected_app);
	if (self->deleted_app != NULL)
		g_object_unref (self->deleted_app);
	if (self->refresh_details_delayed_id != 0)
		g_source_remove (self->refresh_details_delayed_id);
	if (self->autosave_id != 0)
		g_source_remove (self->autosave_id);
	g_object_unref (self->plugin_loader);
	g_object_unref (self->cancellable);
	g_object_unref (self->store);
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
	self->store = gs_app_list_new ();
	self->autosave_id = g_timeout_add_seconds (5, gs_editor_autosave_cb, self);

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
