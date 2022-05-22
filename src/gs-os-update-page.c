/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-os-update-page
 * @title: GsOsUpdatePage
 * @include: gnome-software.h
 * @stability: Stable
 * @short_description: A small page showing OS updates
 *
 * This is a page from #GsUpdateDialog.
 */

#include "config.h"

#include <adwaita.h>
#include <glib/gi18n.h>

#include "gs-os-update-page.h"
#include "gs-common.h"

typedef enum {
	GS_OS_UPDATE_PAGE_SECTION_ADDITIONS,
	GS_OS_UPDATE_PAGE_SECTION_REMOVALS,
	GS_OS_UPDATE_PAGE_SECTION_UPDATES,
	GS_OS_UPDATE_PAGE_SECTION_DOWNGRADES,
	GS_OS_UPDATE_PAGE_SECTION_LAST,
} GsOsUpdatePageSection;

typedef enum {
	PROP_APP = 1,
	PROP_SHOW_BACK_BUTTON,
	PROP_TITLE,
} GsOsUpdatePageProperty;

enum {
	SIGNAL_APP_ACTIVATED,
	SIGNAL_BACK_CLICKED,
	SIGNAL_LAST
};

static GParamSpec *obj_props[PROP_TITLE + 1] = { NULL, };

static guint signals[SIGNAL_LAST] = { 0 };

struct _GsOsUpdatePage
{
	GtkBox		 parent_instance;

	GtkWidget	*back_button;
	GtkWidget	*box;
	GtkWidget	*group;
	GtkWidget	*header_bar;
	AdwWindowTitle	*window_title;

	GsApp		*app;  /* (owned) (nullable) */
	GtkWidget	*list_boxes[GS_OS_UPDATE_PAGE_SECTION_LAST];
};

G_DEFINE_TYPE (GsOsUpdatePage, gs_os_update_page, GTK_TYPE_BOX)

static void
row_activated_cb (GtkListBox *list_box,
		  GtkListBoxRow *row,
		  GsOsUpdatePage *page)
{
	GsApp *app;

	app = GS_APP (g_object_get_data (G_OBJECT (gtk_list_box_row_get_child (row)), "app"));
	g_assert (app != NULL);

	g_signal_emit (page, signals[SIGNAL_APP_ACTIVATED], 0, app);
}

static gchar *
format_version_update (GsApp *app, GtkTextDirection direction)
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
		switch (direction) {
		case GTK_TEXT_DIR_RTL:
			/* ensure the arrow is the right way round for the text direction,
			 * as arrows are not bidi-mirrored automatically
			 * See section 2 of http://www.unicode.org/L2/L2017/17438-bidi-math-fdbk.html */
			return g_strdup_printf ("%s ← %s",
						version_update,
						version_current);
		case GTK_TEXT_DIR_NONE:
		case GTK_TEXT_DIR_LTR:
		default:
			return g_strdup_printf ("%s → %s",
						version_current,
						version_update);
		}
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
	gtk_box_append (GTK_BOX (row), label);
	if (gs_app_get_state (app) == GS_APP_STATE_UPDATABLE ||
	    gs_app_get_state (app) == GS_APP_STATE_UPDATABLE_LIVE) {
		g_autofree gchar *verstr = format_version_update (app, gtk_widget_get_direction (row));
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
	gtk_box_append (GTK_BOX (row), label);

	return row;
}

static gboolean
is_downgrade (const gchar *evr1,
              const gchar *evr2)
{
	gint rc;

	if (evr1 == NULL || evr2 == NULL)
		return FALSE;

	rc = as_vercmp (evr1, evr2, AS_VERCMP_FLAG_IGNORE_EPOCH);
	if (rc != 0)
		return rc > 0;

	return FALSE;
}

static GsOsUpdatePageSection
get_app_section (GsApp *app)
{
	GsOsUpdatePageSection section;

	/* Sections:
	 * 1. additions
	 * 2. removals
	 * 3. updates
	 * 4. downgrades */
	switch (gs_app_get_state (app)) {
	case GS_APP_STATE_AVAILABLE:
		section = GS_OS_UPDATE_PAGE_SECTION_ADDITIONS;
		break;
	case GS_APP_STATE_UNAVAILABLE:
	case GS_APP_STATE_INSTALLED:
		section = GS_OS_UPDATE_PAGE_SECTION_REMOVALS;
		break;
	case GS_APP_STATE_UPDATABLE:
	case GS_APP_STATE_UPDATABLE_LIVE:
		if (is_downgrade (gs_app_get_version (app),
		                  gs_app_get_update_version (app)))
			section = GS_OS_UPDATE_PAGE_SECTION_DOWNGRADES;
		else
			section = GS_OS_UPDATE_PAGE_SECTION_UPDATES;
		break;
	default:
		g_warning ("get_app_section: unhandled state %s for %s",
		           gs_app_state_to_string (gs_app_get_state (app)),
		           gs_app_get_unique_id (app));
		section = GS_OS_UPDATE_PAGE_SECTION_UPDATES;
		break;
	}

	return section;
}

static gint
os_updates_sort_func (GtkListBoxRow *a,
		      GtkListBoxRow *b,
		      gpointer user_data)
{
	GObject *o1 = G_OBJECT (gtk_list_box_row_get_child (a));
	GObject *o2 = G_OBJECT (gtk_list_box_row_get_child (b));
	GsApp *a1 = g_object_get_data (o1, "app");
	GsApp *a2 = g_object_get_data (o2, "app");
	const gchar *key1 = gs_app_get_source_default (a1);
	const gchar *key2 = gs_app_get_source_default (a2);

	return g_strcmp0 (key1, key2);
}

static GtkWidget *
get_section_header (GsOsUpdatePage *page, GsOsUpdatePageSection section)
{
	GtkWidget *header;
	GtkWidget *label;

	/* get labels and buttons for everything */
	if (section == GS_OS_UPDATE_PAGE_SECTION_ADDITIONS) {
		/* TRANSLATORS: This is the header for package additions during
		 * a system update */
		label = gtk_label_new (_("Additions"));
	} else if (section == GS_OS_UPDATE_PAGE_SECTION_REMOVALS) {
		/* TRANSLATORS: This is the header for package removals during
		 * a system update */
		label = gtk_label_new (_("Removals"));
	} else if (section == GS_OS_UPDATE_PAGE_SECTION_UPDATES) {
		/* TRANSLATORS: This is the header for package updates during
		 * a system update */
		label = gtk_label_new (C_("Packages to be updated during a system upgrade", "Updates"));
	} else if (section == GS_OS_UPDATE_PAGE_SECTION_DOWNGRADES) {
		/* TRANSLATORS: This is the header for package downgrades during
		 * a system update */
		label = gtk_label_new (_("Downgrades"));
	} else {
		g_assert_not_reached ();
	}

	/* create header */
	header = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_widget_add_css_class (header, "app-listbox-header");

	/* put label into the header */
	gtk_widget_set_hexpand (label, TRUE);
	gtk_box_append (GTK_BOX (header), label);
	gtk_widget_set_margin_start (label, 16);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_widget_add_css_class (label, "heading");

	/* success */
	return header;
}

static void
list_header_func (GtkListBoxRow *row,
		  GtkListBoxRow *before,
		  gpointer user_data)
{
	GsOsUpdatePage *page = (GsOsUpdatePage *) user_data;
	GObject *o = G_OBJECT (gtk_list_box_row_get_child (row));
	GsApp *app = g_object_get_data (o, "app");
	GtkWidget *header = NULL;

	if (before == NULL)
		header = get_section_header (page, get_app_section (app));
	else
		header = gtk_separator_new (GTK_ORIENTATION_HORIZONTAL);
	gtk_list_box_row_set_header (row, header);
}

static void
create_section (GsOsUpdatePage *page, GsOsUpdatePageSection section)
{
	GtkWidget *previous = NULL;

	page->list_boxes[section] = gtk_list_box_new ();
	gtk_list_box_set_selection_mode (GTK_LIST_BOX (page->list_boxes[section]),
	                                 GTK_SELECTION_NONE);
	gtk_list_box_set_sort_func (GTK_LIST_BOX (page->list_boxes[section]),
				    os_updates_sort_func,
				    page, NULL);
	gtk_list_box_set_header_func (GTK_LIST_BOX (page->list_boxes[section]),
				      list_header_func,
				      page, NULL);
	g_signal_connect (GTK_LIST_BOX (page->list_boxes[section]), "row-activated",
			  G_CALLBACK (row_activated_cb), page);
	gtk_box_append (GTK_BOX (page->box), page->list_boxes[section]);
	gtk_widget_set_margin_top (page->list_boxes[section], 24);

	/* reorder the children */
	for (guint i = 0; i < GS_OS_UPDATE_PAGE_SECTION_LAST; i++) {
		if (page->list_boxes[i] == NULL)
			continue;
		gtk_box_reorder_child_after (GTK_BOX (page->box),
					     page->list_boxes[i],
					     previous);
		previous = page->list_boxes[i];
	}

	/* make rounded edges */
	gtk_widget_set_overflow (page->list_boxes[section], GTK_OVERFLOW_HIDDEN);
	gtk_widget_add_css_class (page->list_boxes[section], "card");
}

/**
 * gs_os_update_page_get_app:
 * @page: a #GsOsUpdatePage
 *
 * Get the value of #GsOsUpdatePage:app.
 *
 * Returns: (nullable) (transfer none): the app
 *
 * Since: 41
 */
GsApp *
gs_os_update_page_get_app (GsOsUpdatePage *page)
{
	g_return_val_if_fail (GS_IS_OS_UPDATE_PAGE (page), NULL);
	return page->app;
}

/**
 * gs_os_update_page_set_app:
 * @page: a #GsOsUpdatePage
 * @app: (transfer none) (nullable): new app
 *
 * Set the value of #GsOsUpdatePage:app.
 *
 * Since: 41
 */
void
gs_os_update_page_set_app (GsOsUpdatePage *page, GsApp *app)
{
	GsAppList *related;
	GsApp *app_related;
	GsOsUpdatePageSection section;
	GtkWidget *row;

	g_return_if_fail (GS_IS_OS_UPDATE_PAGE (page));
	g_return_if_fail (!app || GS_IS_APP (app));

	if (page->app == app)
		return;

	g_set_object (&page->app, app);

	/* clear existing data */
	for (guint i = 0; i < GS_OS_UPDATE_PAGE_SECTION_LAST; i++) {
		if (page->list_boxes[i] == NULL)
			continue;
		gs_widget_remove_all (page->list_boxes[i], (GsRemoveFunc) gtk_list_box_remove);
	}

	if (app) {
		adw_window_title_set_title (page->window_title, gs_app_get_name (app));
		adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (page->group),
						       gs_app_get_description (app));

		/* add new apps */
		related = gs_app_get_related (app);
		for (guint i = 0; i < gs_app_list_length (related); i++) {
			app_related = gs_app_list_index (related, i);

			section = get_app_section (app_related);
			if (page->list_boxes[section] == NULL)
				create_section (page, section);

			row = create_app_row (app_related);
			gtk_list_box_append (GTK_LIST_BOX (page->list_boxes[section]), row);
		}
	} else {
		adw_window_title_set_title (page->window_title, NULL);
		adw_preferences_group_set_description (ADW_PREFERENCES_GROUP (page->group), NULL);
	}

	g_object_notify_by_pspec (G_OBJECT (page), obj_props[PROP_APP]);
	g_object_notify_by_pspec (G_OBJECT (page), obj_props[PROP_TITLE]);
}

/**
 * gs_os_update_page_get_show_back_button:
 * @page: a #GsOsUpdatePage
 *
 * Get the value of #GsOsUpdatePage:show-back-button.
 *
 * Returns: whether to show the back button
 *
 * Since: 42.1
 */
gboolean
gs_os_update_page_get_show_back_button (GsOsUpdatePage *page)
{
	g_return_val_if_fail (GS_IS_OS_UPDATE_PAGE (page), FALSE);
	return gtk_widget_get_visible (page->back_button);
}

/**
 * gs_os_update_page_set_show_back_button:
 * @page: a #GsOsUpdatePage
 * @show_back_button: whether to show the back button
 *
 * Set the value of #GsOsUpdatePage:show-back-button.
 *
 * Since: 42.1
 */
void
gs_os_update_page_set_show_back_button (GsOsUpdatePage *page,
					gboolean show_back_button)
{
	g_return_if_fail (GS_IS_OS_UPDATE_PAGE (page));

	show_back_button = !!show_back_button;

	if (gtk_widget_get_visible (page->back_button) == show_back_button)
		return;

	gtk_widget_set_visible (page->back_button, show_back_button);

	g_object_notify_by_pspec (G_OBJECT (page), obj_props[PROP_SHOW_BACK_BUTTON]);
}

static void
back_clicked_cb (GtkWidget *widget,
		 GsOsUpdatePage *page)
{
	g_signal_emit (page, signals[SIGNAL_BACK_CLICKED], 0);
}

static void
gs_os_update_page_dispose (GObject *object)
{
	GsOsUpdatePage *page = GS_OS_UPDATE_PAGE (object);

	g_clear_object (&page->app);

	G_OBJECT_CLASS (gs_os_update_page_parent_class)->dispose (object);
}

static void
gs_os_update_page_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsOsUpdatePage *page = GS_OS_UPDATE_PAGE (object);

	switch ((GsOsUpdatePageProperty) prop_id) {
	case PROP_APP:
		g_value_set_object (value, gs_os_update_page_get_app (page));
		break;
	case PROP_SHOW_BACK_BUTTON:
		g_value_set_boolean (value, gs_os_update_page_get_show_back_button (page));
		break;
	case PROP_TITLE:
		g_value_set_string (value, adw_window_title_get_title (page->window_title));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_os_update_page_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsOsUpdatePage *page = GS_OS_UPDATE_PAGE (object);

	switch ((GsOsUpdatePageProperty) prop_id) {
	case PROP_APP:
		gs_os_update_page_set_app (page, g_value_get_object (value));
		break;
	case PROP_SHOW_BACK_BUTTON:
		gs_os_update_page_set_show_back_button (page, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_os_update_page_init (GsOsUpdatePage *page)
{
	gtk_widget_init_template (GTK_WIDGET (page));
}

static void
gs_os_update_page_class_init (GsOsUpdatePageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_os_update_page_dispose;
	object_class->get_property = gs_os_update_page_get_property;
	object_class->set_property = gs_os_update_page_set_property;

	/**
	 * GsOsUpdatePage:app: (nullable)
	 *
	 * The app to present.
	 *
	 * Since: 41
	 */
	obj_props[PROP_APP] =
		g_param_spec_object ("app", NULL, NULL,
				     GS_TYPE_APP,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsOsUpdatePage:show-back-button
	 *
	 * Whether to show the back button.
	 *
	 * Since: 42.1
	 */
	obj_props[PROP_SHOW_BACK_BUTTON] =
		g_param_spec_boolean ("show-back-button", NULL, NULL,
				     FALSE,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsOsUpdatePage:title
	 *
	 * Read-only window title.
	 *
	 * Since: 42
	 */
	obj_props[PROP_TITLE] =
		g_param_spec_string ("title", NULL, NULL,
				     NULL,
				     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	/**
	 * GsOsUpdatePage:app-activated:
	 * @app: a #GsApp
	 *
	 * Emitted when an app listed in this page got activated and the
	 * #GsUpdateDialog containing this page is expected to present its
	 * details via a #GsAppDetailsPage.
	 *
	 * Since: 41
	 */
	signals[SIGNAL_APP_ACTIVATED] =
		g_signal_new ("app-activated",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 1, GS_TYPE_APP);

	/**
	 * GsOsUpdatePage::back-clicked:
	 * @self: a #GsOsUpdatePage
	 *
	 * Emitted when the back button got activated and the #GsUpdateDialog
	 * containing this page is expected to go back.
	 *
	 * Since: 42.1
	 */
	signals[SIGNAL_BACK_CLICKED] =
		g_signal_new ("back-clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_generic,
			      G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-os-update-page.ui");

	gtk_widget_class_bind_template_child (widget_class, GsOsUpdatePage, back_button);
	gtk_widget_class_bind_template_child (widget_class, GsOsUpdatePage, box);
	gtk_widget_class_bind_template_child (widget_class, GsOsUpdatePage, group);
	gtk_widget_class_bind_template_child (widget_class, GsOsUpdatePage, header_bar);
	gtk_widget_class_bind_template_child (widget_class, GsOsUpdatePage, window_title);
	gtk_widget_class_bind_template_callback (widget_class, back_clicked_cb);
}

/**
 * gs_os_update_page_new:
 *
 * Create a new #GsOsUpdatePage.
 *
 * Returns: (transfer full): a new #GsOsUpdatePage
 * Since: 41
 */
GtkWidget *
gs_os_update_page_new (void)
{
	return GTK_WIDGET (g_object_new (GS_TYPE_OS_UPDATE_PAGE, NULL));
}
