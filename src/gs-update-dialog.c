/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-update-dialog.h"
#include "gs-app-row.h"
#include "gs-update-list.h"
#include "gs-common.h"

typedef struct {
	gchar		*title;
	gchar		*stack_page;
	GtkWidget	*focus;
} BackEntry;

typedef enum {
	GS_UPDATE_DIALOG_SECTION_ADDITIONS,
	GS_UPDATE_DIALOG_SECTION_REMOVALS,
	GS_UPDATE_DIALOG_SECTION_UPDATES,
	GS_UPDATE_DIALOG_SECTION_DOWNGRADES,
	GS_UPDATE_DIALOG_SECTION_LAST,
} GsUpdateDialogSection;

struct _GsUpdateDialog
{
	GtkDialog	 parent_instance;

	GQueue		*back_entry_stack;
	GCancellable	*cancellable;
	GsPluginLoader	*plugin_loader;
	GtkWidget	*box_header;
	GtkWidget	*button_back;
	GtkWidget	*image_icon;
	GtkWidget	*label_details;
	GtkWidget	*label_name;
	GtkWidget	*label_summary;
	GtkWidget	*list_boxes[GS_UPDATE_DIALOG_SECTION_LAST];
	GtkWidget	*list_box_installed_updates;
	GtkWidget	*os_update_description;
	GtkWidget	*os_update_box;
	GtkWidget	*scrolledwindow;
	GtkWidget	*scrolledwindow_details;
	GtkWidget	*spinner;
	GtkWidget	*stack;
	GtkWidget       *permissions_section_box;
	GtkWidget       *permissions_section_content;
};

G_DEFINE_TYPE (GsUpdateDialog, gs_update_dialog, GTK_TYPE_DIALOG)

static void
save_back_entry (GsUpdateDialog *dialog)
{
	BackEntry *entry;

	entry = g_slice_new0 (BackEntry);
	entry->stack_page = g_strdup (gtk_stack_get_visible_child_name (GTK_STACK (dialog->stack)));
	entry->title = g_strdup (gtk_window_get_title (GTK_WINDOW (dialog)));

	entry->focus = gtk_window_get_focus (GTK_WINDOW (dialog));
	if (entry->focus != NULL)
		g_object_add_weak_pointer (G_OBJECT (entry->focus),
		                           (gpointer *) &entry->focus);

	g_queue_push_head (dialog->back_entry_stack, entry);
}

static void
back_entry_free (BackEntry *entry)
{
	if (entry->focus != NULL)
		g_object_remove_weak_pointer (G_OBJECT (entry->focus),
		                              (gpointer *) &entry->focus);
	g_free (entry->stack_page);
	g_free (entry->title);
	g_slice_free (BackEntry, entry);
}

static struct {
        GsAppPermissions permission;
        const char *title;
        const char *subtitle;
} permission_display_data[] = {
  { GS_APP_PERMISSIONS_NETWORK, N_("Network"), N_("Can communicate over the network") },
  { GS_APP_PERMISSIONS_SYSTEM_BUS, N_("System Services"), N_("Can access D-Bus services on the system bus") },
  { GS_APP_PERMISSIONS_SESSION_BUS, N_("Session Services"), N_("Can access D-Bus services on the session bus") },
  { GS_APP_PERMISSIONS_DEVICES, N_("Devices"), N_("Can access system device files") },
  { GS_APP_PERMISSIONS_HOME_FULL, N_("Home folder"), N_("Can view, edit and create files") },
  { GS_APP_PERMISSIONS_HOME_READ, N_("Home folder"), N_("Can view files") },
  { GS_APP_PERMISSIONS_FILESYSTEM_FULL, N_("File system"), N_("Can view, edit and create files") },
  { GS_APP_PERMISSIONS_FILESYSTEM_READ, N_("File system"), N_("Can view files") },
  { GS_APP_PERMISSIONS_DOWNLOADS_FULL, N_("Downloads folder"), N_("Can view, edit and create files") },
  { GS_APP_PERMISSIONS_DOWNLOADS_READ, N_("Downloads folder"), N_("Can view files") },
  { GS_APP_PERMISSIONS_SETTINGS, N_("Settings"), N_("Can view and change any settings") },
  { GS_APP_PERMISSIONS_X11, N_("Legacy display system"), N_("Uses an old, insecure display system") },
  { GS_APP_PERMISSIONS_ESCAPE_SANDBOX, N_("Sandbox escape"), N_("Can escape the sandbox and circumvent any other restrictions") },
};

static void
populate_permissions_section (GsUpdateDialog *dialog, GsAppPermissions permissions)
{
	GList *children;

	children = gtk_container_get_children (GTK_CONTAINER (dialog->permissions_section_content));
	for (GList *l = children; l != NULL; l = l->next)
		gtk_widget_destroy (GTK_WIDGET (l->data));
	g_list_free (children);

	for (gsize i = 0; i < G_N_ELEMENTS (permission_display_data); i++) {
		GtkWidget *row, *image, *box, *label;

		if ((permissions & permission_display_data[i].permission) == 0)
			continue;

		row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
		gtk_widget_show (row);
		if ((permission_display_data[i].permission & ~MEDIUM_PERMISSIONS) != 0) {
			gtk_style_context_add_class (gtk_widget_get_style_context (row), "permission-row-warning");
		}

		image = gtk_image_new_from_icon_name ("dialog-warning-symbolic", GTK_ICON_SIZE_MENU);
		if ((permission_display_data[i].permission & ~MEDIUM_PERMISSIONS) == 0)
			gtk_widget_set_opacity (image, 0);

		gtk_widget_show (image);
		gtk_container_add (GTK_CONTAINER (row), image);

		box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
		gtk_widget_show (box);
		gtk_container_add (GTK_CONTAINER (row), box);

		label = gtk_label_new (_(permission_display_data[i].title));
		gtk_label_set_xalign (GTK_LABEL (label), 0);
		gtk_widget_show (label);
		gtk_container_add (GTK_CONTAINER (box), label);

		label = gtk_label_new (_(permission_display_data[i].subtitle));
		gtk_label_set_xalign (GTK_LABEL (label), 0);
		gtk_style_context_add_class (gtk_widget_get_style_context (label), "dim-label");
		gtk_widget_show (label);
		gtk_container_add (GTK_CONTAINER (box), label);

		gtk_container_add (GTK_CONTAINER (dialog->permissions_section_content), row);
	}
}

static void
set_updates_description_ui (GsUpdateDialog *dialog, GsApp *app)
{
	AsAppKind kind;
	const GdkPixbuf *pixbuf;
	const gchar *update_details;

	/* set window title */
	kind = gs_app_get_kind (app);
	if (kind == AS_APP_KIND_OS_UPDATE) {
		gtk_window_set_title (GTK_WINDOW (dialog), gs_app_get_name (app));
	} else if (gs_app_get_source_default (app) != NULL &&
		   gs_app_get_update_version (app) != NULL) {
		g_autofree gchar *tmp = NULL;
		tmp = g_strdup_printf ("%s %s",
				       gs_app_get_source_default (app),
				       gs_app_get_update_version (app));
		gtk_window_set_title (GTK_WINDOW (dialog), tmp);
	} else if (gs_app_get_source_default (app) != NULL) {
		gtk_window_set_title (GTK_WINDOW (dialog),
				      gs_app_get_source_default (app));
	} else {
		gtk_window_set_title (GTK_WINDOW (dialog),
				      gs_app_get_update_version (app));
	}

	/* set update header */
	gtk_widget_set_visible (dialog->box_header, kind == AS_APP_KIND_DESKTOP);
	update_details = gs_app_get_update_details (app);
	if (update_details == NULL) {
		/* TRANSLATORS: this is where the packager did not write
		 * a description for the update */
		update_details = _("No update description available.");
	}
	gtk_label_set_label (GTK_LABEL (dialog->label_details), update_details);
	gtk_label_set_label (GTK_LABEL (dialog->label_name), gs_app_get_name (app));
	gtk_label_set_label (GTK_LABEL (dialog->label_summary), gs_app_get_summary (app));

	pixbuf = gs_app_get_pixbuf (app);
	if (pixbuf != NULL)
		gs_image_set_from_pixbuf (GTK_IMAGE (dialog->image_icon), pixbuf);

	/* show the back button if needed */
	gtk_widget_set_visible (dialog->button_back, !g_queue_is_empty (dialog->back_entry_stack));

	if (gs_app_has_quirk (app, GS_APP_QUIRK_NEW_PERMISSIONS)) {
		gtk_widget_show (dialog->permissions_section_box);
		populate_permissions_section (dialog, gs_app_get_update_permissions (app));
	} else {
		gtk_widget_hide (dialog->permissions_section_box);
	}
}

static void
row_activated_cb (GtkListBox *list_box,
		  GtkListBoxRow *row,
		  GsUpdateDialog *dialog)
{
	GsApp *app;

	app = GS_APP (g_object_get_data (G_OBJECT (gtk_bin_get_child (GTK_BIN (row))), "app"));

	/* save the current stack state for the back button */
	save_back_entry (dialog);

	/* setup package view */
	gs_update_dialog_show_update_details (dialog, app);
}

static void
installed_updates_row_activated_cb (GtkListBox *list_box,
				    GtkListBoxRow *row,
				    GsUpdateDialog *dialog)
{
	GsApp *app;

	app = gs_app_row_get_app (GS_APP_ROW (row));

	/* save the current stack state for the back button */
	save_back_entry (dialog);

	gs_update_dialog_show_update_details (dialog, app);
}

static void
get_installed_updates_cb (GsPluginLoader *plugin_loader,
                          GAsyncResult *res,
                          GsUpdateDialog *dialog)
{
	guint i;
	guint64 install_date;
	g_autoptr(GsAppList) list = NULL;
	g_autoptr(GError) error = NULL;

	/* get the results */
	list = gs_plugin_loader_job_process_finish (plugin_loader, res, &error);

	/* if we're in teardown, short-circuit and return immediately without
	 * dereferencing priv variables */
	if (g_error_matches (error, GS_PLUGIN_ERROR, GS_PLUGIN_ERROR_CANCELLED) ||
	    dialog->spinner == NULL) {
		g_debug ("get installed updates cancelled");
		return;
	}

	gs_stop_spinner (GTK_SPINNER (dialog->spinner));

	/* error */
	if (list == NULL) {
		g_warning ("failed to get installed updates: %s", error->message);
		gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "empty");
		return;
	}

	/* no results */
	if (gs_app_list_length (list) == 0) {
		g_debug ("no installed updates to show");
		gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "empty");
		return;
	}

	/* set the header title using any one of the applications */
	install_date = gs_app_get_install_date (gs_app_list_index (list, 0));
	if (install_date > 0) {
		GtkWidget *header;
		g_autoptr(GDateTime) date = NULL;
		g_autofree gchar *date_str = NULL;
		g_autofree gchar *subtitle = NULL;

		date = g_date_time_new_from_unix_utc ((gint64) install_date);
		date_str = g_date_time_format (date, "%x");

		/* TRANSLATORS: this is the subtitle of the installed updates dialog window.
		   %s will be replaced by the date when the updates were installed.
		   The date format is defined by the locale's preferred date representation
		   ("%x" in strftime.) */
		subtitle = g_strdup_printf (_("Installed on %s"), date_str);
		header = gtk_dialog_get_header_bar (GTK_DIALOG (dialog));
		gtk_header_bar_set_subtitle (GTK_HEADER_BAR (header), subtitle);
	}

	gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "installed-updates-list");

	gs_container_remove_all (GTK_CONTAINER (dialog->list_box_installed_updates));
	for (i = 0; i < gs_app_list_length (list); i++) {
		gs_update_list_add_app (GS_UPDATE_LIST (dialog->list_box_installed_updates),
					gs_app_list_index (list, i));
	}
}

void
gs_update_dialog_show_installed_updates (GsUpdateDialog *dialog)
{
	g_autoptr(GsPluginJob) plugin_job = NULL;

	/* TRANSLATORS: this is the title of the installed updates dialog window */
	gtk_window_set_title (GTK_WINDOW (dialog), _("Installed Updates"));

	gtk_widget_set_visible (dialog->button_back, !g_queue_is_empty (dialog->back_entry_stack));
	gs_start_spinner (GTK_SPINNER (dialog->spinner));
	gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "spinner");

	plugin_job = gs_plugin_job_newv (GS_PLUGIN_ACTION_GET_UPDATES_HISTORICAL,
					 "refine-flags", GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON |
							 GS_PLUGIN_REFINE_FLAGS_REQUIRE_VERSION,
					 NULL);
	gs_plugin_loader_job_process_async (dialog->plugin_loader, plugin_job,
	                                    dialog->cancellable,
	                                    (GAsyncReadyCallback) get_installed_updates_cb,
	                                    dialog);
}

static void
unset_focus (GtkWidget *widget)
{
	GtkWidget *focus;

	focus = gtk_window_get_focus (GTK_WINDOW (widget));
	if (GTK_IS_LABEL (focus))
		gtk_label_select_region (GTK_LABEL (focus), 0, 0);
	gtk_window_set_focus (GTK_WINDOW (widget), NULL);
}

static gchar *
format_version_update (GsApp *app)
{
	const gchar *tmp;
	const gchar *version_current = NULL;
	const gchar *version_update = NULL;

	/* current version */
	tmp = gs_app_get_version (app);
	if (tmp != NULL && tmp[0] != '\0')
		version_current = tmp;

	/* update version */
	tmp = gs_app_get_update_version (app);
	if (tmp != NULL && tmp[0] != '\0')
		version_update = tmp;

	/* have both */
	if (version_current != NULL && version_update != NULL &&
	    g_strcmp0 (version_current, version_update) != 0) {
		return g_strdup_printf ("%s → %s",
					version_current,
					version_update);
	}

	/* just update */
	if (version_update)
		return g_strdup (version_update);

	/* we have nothing, nada, zilch */
	return NULL;
}

static GtkWidget *
create_app_row (GsApp *app)
{
	GtkWidget *row, *label;

	row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	g_object_set_data_full (G_OBJECT (row),
	                        "app",
	                        g_object_ref (app),
	                        g_object_unref);
	label = gtk_label_new (gs_app_get_source_default (app));
	g_object_set (label,
	              "margin-start", 20,
	              "margin-end", 0,
	              "margin-top", 6,
	              "margin-bottom", 6,
	              "xalign", 0.0,
	              "ellipsize", PANGO_ELLIPSIZE_END,
	              NULL);
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_hexpand (label, TRUE);
	gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
	gtk_container_add (GTK_CONTAINER (row), label);
	if (gs_app_get_state (app) == AS_APP_STATE_UPDATABLE ||
	    gs_app_get_state (app) == AS_APP_STATE_UPDATABLE_LIVE) {
		g_autofree gchar *verstr = format_version_update (app);
		label = gtk_label_new (verstr);
	} else {
		label = gtk_label_new (gs_app_get_version (app));
	}
	g_object_set (label,
	              "margin-start", 0,
	              "margin-end", 20,
	              "margin-top", 6,
	              "margin-bottom", 6,
	              "xalign", 1.0,
	              "ellipsize", PANGO_ELLIPSIZE_END,
	              NULL);
	gtk_widget_set_halign (label, GTK_ALIGN_END);
	gtk_widget_set_valign (label, GTK_ALIGN_CENTER);
	gtk_container_add (GTK_CONTAINER (row), label);
	gtk_widget_show_all (row);

	return row;
}

static gboolean
is_downgrade (const gchar *evr1,
              const gchar *evr2)
{
	gint rc;
	g_autofree gchar *epoch1 = NULL;
	g_autofree gchar *epoch2 = NULL;
	g_autofree gchar *version1 = NULL;
	g_autofree gchar *version2 = NULL;
	g_autofree gchar *release1 = NULL;
	g_autofree gchar *release2 = NULL;

	if (evr1 == NULL || evr2 == NULL)
		return FALSE;

	/* split into epoch-version-release */
	if (!gs_utils_parse_evr (evr1, &epoch1, &version1, &release1))
		return FALSE;
	if (!gs_utils_parse_evr (evr2, &epoch2, &version2, &release2))
		return FALSE;

	/* ignore epoch here as it's a way to make downgrades happen and not
	 * part of the semantic version */

	/* check version */
#if AS_CHECK_VERSION(0,7,15)
	rc = as_utils_vercmp_full (version1, version2,
	                           AS_VERSION_COMPARE_FLAG_NONE);
#else
	rc = as_utils_vercmp (version1, version2);
#endif
	if (rc != 0)
		return rc > 0;

	/* check release */
#if AS_CHECK_VERSION(0,7,15)
	rc = as_utils_vercmp_full (version1, version2,
	                           AS_VERSION_COMPARE_FLAG_NONE);
#else
	rc = as_utils_vercmp (release1, release2);
#endif
	if (rc != 0)
		return rc > 0;

	return FALSE;
}

static GsUpdateDialogSection
get_app_section (GsApp *app)
{
	GsUpdateDialogSection section;

	/* Sections:
	 * 1. additions
	 * 2. removals
	 * 3. updates
	 * 4. downgrades */
	switch (gs_app_get_state (app)) {
	case AS_APP_STATE_AVAILABLE:
		section = GS_UPDATE_DIALOG_SECTION_ADDITIONS;
		break;
	case AS_APP_STATE_UNAVAILABLE:
		section = GS_UPDATE_DIALOG_SECTION_REMOVALS;
		break;
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_UPDATABLE_LIVE:
		if (is_downgrade (gs_app_get_version (app),
		                  gs_app_get_update_version (app)))
			section = GS_UPDATE_DIALOG_SECTION_DOWNGRADES;
		else
			section = GS_UPDATE_DIALOG_SECTION_UPDATES;
		break;
	default:
		g_warning ("get_app_section: unhandled state %s for %s",
		           as_app_state_to_string (gs_app_get_state (app)),
		           gs_app_get_unique_id (app));
		section = GS_UPDATE_DIALOG_SECTION_UPDATES;
		break;
	}

	return section;
}

static gint
os_updates_sort_func (GtkListBoxRow *a,
		      GtkListBoxRow *b,
		      gpointer user_data)
{
	GObject *o1 = G_OBJECT (gtk_bin_get_child (GTK_BIN (a)));
	GObject *o2 = G_OBJECT (gtk_bin_get_child (GTK_BIN (b)));
	GsApp *a1 = g_object_get_data (o1, "app");
	GsApp *a2 = g_object_get_data (o2, "app");
	const gchar *key1 = gs_app_get_source_default (a1);
	const gchar *key2 = gs_app_get_source_default (a2);

	return g_strcmp0 (key1, key2);
}

static GtkWidget *
get_section_header (GsUpdateDialog *dialog, GsUpdateDialogSection section)
{
	GtkStyleContext *context;
	GtkWidget *header;
	GtkWidget *label;

	/* get labels and buttons for everything */
	if (section == GS_UPDATE_DIALOG_SECTION_ADDITIONS) {
		/* TRANSLATORS: This is the header for package additions during
		 * a system update */
		label = gtk_label_new (_("Additions"));
	} else if (section == GS_UPDATE_DIALOG_SECTION_REMOVALS) {
		/* TRANSLATORS: This is the header for package removals during
		 * a system update */
		label = gtk_label_new (_("Removals"));
	} else if (section == GS_UPDATE_DIALOG_SECTION_UPDATES) {
		/* TRANSLATORS: This is the header for package updates during
		 * a system update */
		label = gtk_label_new (_("Updates"));
	} else if (section == GS_UPDATE_DIALOG_SECTION_DOWNGRADES) {
		/* TRANSLATORS: This is the header for package downgrades during
		 * a system update */
		label = gtk_label_new (_("Downgrades"));
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

	/* success */
	return header;
}

static void
list_header_func (GtkListBoxRow *row,
		  GtkListBoxRow *before,
		  gpointer user_data)
{
	GsUpdateDialog *dialog = (GsUpdateDialog *) user_data;
	GObject *o = G_OBJECT (gtk_bin_get_child (GTK_BIN (row)));
	GsApp *app = g_object_get_data (o, "app");
	GtkWidget *header = NULL;

	if (before == NULL)
		header = get_section_header (dialog, get_app_section (app));
	else
		header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_list_box_row_set_header (row, header);
}

static void
create_section (GsUpdateDialog *dialog, GsUpdateDialogSection section)
{
	GtkStyleContext *context;

	dialog->list_boxes[section] = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (dialog->list_boxes[section]),
	                                 GTK_SELECTION_NONE);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (dialog->list_boxes[section]),
				    os_updates_sort_func,
				    dialog, NULL);
	gtk_list_box_set_header_func (GTK_LIST_BOX (dialog->list_boxes[section]),
				      list_header_func,
				      dialog, NULL);
	g_signal_connect (GTK_LIST_BOX (dialog->list_boxes[section]), "row-activated",
			  G_CALLBACK (row_activated_cb), dialog);
	gtk_widget_set_visible (dialog->list_boxes[section], TRUE);
	gtk_widget_set_vexpand (dialog->list_boxes[section], TRUE);
	gtk_container_add (GTK_CONTAINER (dialog->os_update_box), dialog->list_boxes[section]);
	gtk_widget_set_margin_top (dialog->list_boxes[section], 24);

	/* reorder the children */
	for (guint i = 0; i < GS_UPDATE_DIALOG_SECTION_LAST; i++) {
		if (dialog->list_boxes[i] == NULL)
			continue;
		gtk_box_reorder_child (GTK_BOX (dialog->os_update_box),
				       dialog->list_boxes[i], i);
	}

	/* make rounded edges */
	context = gtk_widget_get_style_context (dialog->list_boxes[section]);
	gtk_style_context_add_class (context, "app-updates-section");
}

void
gs_update_dialog_show_update_details (GsUpdateDialog *dialog, GsApp *app)
{
	AsAppKind kind;
	g_autofree gchar *str = NULL;

	/* debug */
	str = gs_app_to_string (app);
	g_debug ("%s", str);

	/* set update header */
	set_updates_description_ui (dialog, app);

	/* workaround a gtk+ issue where the dialog comes up with a label selected,
	 * https://bugzilla.gnome.org/show_bug.cgi?id=734033 */
	unset_focus (GTK_WIDGET (dialog));

	/* set update description */
	kind = gs_app_get_kind (app);
	if (kind == AS_APP_KIND_OS_UPDATE) {
		GsAppList *related;
		GsApp *app_related;
		GsUpdateDialogSection section;
		GtkWidget *row;

		gtk_label_set_text (GTK_LABEL (dialog->os_update_description),
		                    gs_app_get_description (app));

		/* clear existing data */
		for (guint i = 0; i < GS_UPDATE_DIALOG_SECTION_LAST; i++) {
			if (dialog->list_boxes[i] == NULL)
				continue;
			gs_container_remove_all (GTK_CONTAINER (dialog->list_boxes[i]));
		}

		/* add new apps */
		related = gs_app_get_related (app);
		for (guint i = 0; i < gs_app_list_length (related); i++) {
			app_related = gs_app_list_index (related, i);

			section = get_app_section (app_related);
			if (dialog->list_boxes[section] == NULL)
				create_section (dialog, section);

			row = create_app_row (app_related);
			gtk_list_box_insert (GTK_LIST_BOX (dialog->list_boxes[section]), row, -1);
		}
		gtk_stack_set_transition_type (GTK_STACK (dialog->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT);
		gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "os-update-list");
		gtk_stack_set_transition_type (GTK_STACK (dialog->stack), GTK_STACK_TRANSITION_TYPE_NONE);
	} else {
		gtk_stack_set_transition_type (GTK_STACK (dialog->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT);
		gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), "package-details");
		gtk_stack_set_transition_type (GTK_STACK (dialog->stack), GTK_STACK_TRANSITION_TYPE_NONE);
	}
}

static void
button_back_cb (GtkWidget *widget, GsUpdateDialog *dialog)
{
	BackEntry *entry;

	/* return to the previous view */
	entry = g_queue_pop_head (dialog->back_entry_stack);

	gtk_stack_set_transition_type (GTK_STACK (dialog->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_RIGHT);
	gtk_stack_set_visible_child_name (GTK_STACK (dialog->stack), entry->stack_page);
	gtk_stack_set_transition_type (GTK_STACK (dialog->stack), GTK_STACK_TRANSITION_TYPE_NONE);

	gtk_window_set_title (GTK_WINDOW (dialog), entry->title);
	if (entry->focus)
		gtk_widget_grab_focus (entry->focus);
	back_entry_free (entry);

	gtk_widget_set_visible (dialog->button_back, !g_queue_is_empty (dialog->back_entry_stack));
}

static void
scrollbar_mapped_cb (GtkWidget *sb, GtkScrolledWindow *swin)
{
	GtkWidget *frame;

	frame = gtk_bin_get_child (GTK_BIN (gtk_bin_get_child (GTK_BIN (swin))));

	if (gtk_widget_get_mapped (GTK_WIDGET (sb))) {
		gtk_scrolled_window_set_shadow_type (swin, GTK_SHADOW_IN);
		if (GTK_IS_FRAME (frame))
			gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_NONE);
	} else {
		if (GTK_IS_FRAME (frame))
			gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
		gtk_scrolled_window_set_shadow_type (swin, GTK_SHADOW_NONE);
	}
}

static gboolean
key_press_event (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	GsUpdateDialog *dialog = (GsUpdateDialog *) widget;
	GdkKeymap *keymap;
	GdkModifierType state;
	gboolean is_rtl;

	if (!gtk_widget_is_visible (dialog->button_back) || !gtk_widget_is_sensitive (dialog->button_back))
		return GDK_EVENT_PROPAGATE;

	state = event->state;
	keymap = gdk_keymap_get_for_display (gtk_widget_get_display (widget));
	gdk_keymap_add_virtual_modifiers (keymap, &state);
	state = state & gtk_accelerator_get_default_mod_mask ();
	is_rtl = gtk_widget_get_direction (dialog->button_back) == GTK_TEXT_DIR_RTL;

	if ((!is_rtl && state == GDK_MOD1_MASK && event->keyval == GDK_KEY_Left) ||
	    (is_rtl && state == GDK_MOD1_MASK && event->keyval == GDK_KEY_Right) ||
	    event->keyval == GDK_KEY_Back) {
		gtk_widget_activate (dialog->button_back);
		return GDK_EVENT_STOP;
	}

	return GDK_EVENT_PROPAGATE;
}

static gboolean
button_press_event (GsUpdateDialog *dialog, GdkEventButton *event)
{
	/* Mouse hardware back button is 8 */
	if (event->button != 8)
		return GDK_EVENT_PROPAGATE;

	if (!gtk_widget_is_visible (dialog->button_back) || !gtk_widget_is_sensitive (dialog->button_back))
		return GDK_EVENT_PROPAGATE;

	gtk_widget_activate (dialog->button_back);
	return GDK_EVENT_STOP;
}

static void
set_plugin_loader (GsUpdateDialog *dialog, GsPluginLoader *plugin_loader)
{
	dialog->plugin_loader = g_object_ref (plugin_loader);
}

static void
gs_update_dialog_dispose (GObject *object)
{
	GsUpdateDialog *dialog = GS_UPDATE_DIALOG (object);

	if (dialog->back_entry_stack != NULL) {
		g_queue_free_full (dialog->back_entry_stack, (GDestroyNotify) back_entry_free);
		dialog->back_entry_stack = NULL;
	}

	g_cancellable_cancel (dialog->cancellable);
	g_clear_object (&dialog->cancellable);

	g_clear_object (&dialog->plugin_loader);

	G_OBJECT_CLASS (gs_update_dialog_parent_class)->dispose (object);
}

static void
gs_update_dialog_init (GsUpdateDialog *dialog)
{
	GtkWidget *scrollbar;

	gtk_widget_init_template (GTK_WIDGET (dialog));

	dialog->back_entry_stack = g_queue_new ();
	dialog->cancellable = g_cancellable_new ();

	g_signal_connect (GTK_LIST_BOX (dialog->list_box_installed_updates), "row-activated",
			  G_CALLBACK (installed_updates_row_activated_cb), dialog);

	g_signal_connect (dialog->button_back, "clicked",
			  G_CALLBACK (button_back_cb),
			  dialog);

	g_signal_connect_after (dialog, "show", G_CALLBACK (unset_focus), NULL);

	scrollbar = gtk_scrolled_window_get_vscrollbar (GTK_SCROLLED_WINDOW (dialog->scrolledwindow_details));
	g_signal_connect (scrollbar, "map", G_CALLBACK (scrollbar_mapped_cb), dialog->scrolledwindow_details);
	g_signal_connect (scrollbar, "unmap", G_CALLBACK (scrollbar_mapped_cb), dialog->scrolledwindow_details);

	/* global keynav and mouse back button */
	g_signal_connect (dialog, "key-press-event",
			  G_CALLBACK (key_press_event), NULL);
	g_signal_connect (dialog, "button-press-event",
			  G_CALLBACK (button_press_event), NULL);
}

static void
gs_update_dialog_class_init (GsUpdateDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_update_dialog_dispose;

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-update-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, box_header);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, button_back);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, image_icon);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, label_details);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, label_name);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, label_summary);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, list_box_installed_updates);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, os_update_description);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, os_update_box);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, scrolledwindow);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, scrolledwindow_details);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, spinner);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, stack);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, permissions_section_box);
	gtk_widget_class_bind_template_child (widget_class, GsUpdateDialog, permissions_section_content);
}

GtkWidget *
gs_update_dialog_new (GsPluginLoader *plugin_loader)
{
	GsUpdateDialog *dialog;

	dialog = g_object_new (GS_TYPE_UPDATE_DIALOG,
	                       "use-header-bar", TRUE,
	                       NULL);
	set_plugin_loader (dialog, plugin_loader);

	return GTK_WIDGET (dialog);
}
