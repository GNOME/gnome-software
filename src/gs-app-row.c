/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gs-app-row.h"
#include "gs-cleanup.h"
#include "gs-star-widget.h"
#include "gs-markdown.h"
#include "gs-progress-button.h"
#include "gs-utils.h"
#include "gs-folders.h"

typedef struct
{
	GsApp		*app;
	GtkWidget	*image;
	GtkWidget	*name_box;
	GtkWidget	*name_label;
	GtkWidget	*version_label;
	GtkWidget	*star;
	GtkWidget	*folder_label;
	GtkWidget	*description_label;
	GtkWidget	*button_box;
	GtkWidget	*button;
	GtkWidget	*spinner;
	GtkWidget	*label;
	GtkWidget	*checkbox;
	gboolean	 colorful;
	gboolean	 show_codec;
	gboolean	 show_update;
	gboolean	 selectable;
	guint		 pending_refresh_id;
} GsAppRowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsAppRow, gs_app_row, GTK_TYPE_LIST_BOX_ROW)

enum {
	PROP_ZERO,
	PROP_SELECTED
};

enum {
	SIGNAL_BUTTON_CLICKED,
	SIGNAL_UNREVEALED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

/**
 * gs_app_row_get_description:
 *
 * Return value: PangoMarkup
 **/
static GString *
gs_app_row_get_description (GsAppRow *app_row)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);
	const gchar *tmp = NULL;
	_cleanup_free_ gchar *escaped = NULL;

	/* convert the markdown update description into PangoMarkup */
	if (priv->show_update &&
	    gs_app_get_state (priv->app) == AS_APP_STATE_UPDATABLE) {
		tmp = gs_app_get_update_details (priv->app);
		if (tmp != NULL && tmp[0] != '\0') {
			_cleanup_object_unref_ GsMarkdown *markdown = NULL;
			markdown = gs_markdown_new (GS_MARKDOWN_OUTPUT_PANGO);
			gs_markdown_set_smart_quoting (markdown, FALSE);
			gs_markdown_set_autocode (markdown, FALSE);
			gs_markdown_set_autolinkify (markdown, FALSE);
			escaped = gs_markdown_parse (markdown, tmp);
			return g_string_new (escaped);
		}
	}

	if (gs_app_get_kind (priv->app) == GS_APP_KIND_MISSING)
		return g_string_new (gs_app_get_summary_missing (priv->app));

	/* try all these things in order */
	if (tmp == NULL || (tmp != NULL && tmp[0] == '\0'))
		tmp = gs_app_get_description (priv->app);
	if (tmp == NULL || (tmp != NULL && tmp[0] == '\0'))
		tmp = gs_app_get_summary (priv->app);
	if (tmp == NULL || (tmp != NULL && tmp[0] == '\0'))
		tmp = gs_app_get_name (priv->app);
	escaped = g_markup_escape_text (tmp, -1);
	return g_string_new (escaped);
}

/**
 * gs_app_row_refresh:
 **/
void
gs_app_row_refresh (GsAppRow *app_row)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);
	GtkStyleContext *context;
	GString *str = NULL;

	if (priv->app == NULL)
		return;

	/* do a fill bar for the current progress */
	switch (gs_app_get_state (priv->app)) {
	case AS_APP_STATE_INSTALLING:
		gs_progress_button_set_progress (GS_PROGRESS_BUTTON (priv->button),
		                                 gs_app_get_progress (priv->app));
		gs_progress_button_set_show_progress (GS_PROGRESS_BUTTON (priv->button), TRUE);
		break;
	default:
		gs_progress_button_set_show_progress (GS_PROGRESS_BUTTON (priv->button), FALSE);
		break;
	}

	/* join the lines*/
	str = gs_app_row_get_description (app_row);
	gs_string_replace (str, "\n", " ");
	gtk_label_set_markup (GTK_LABEL (priv->description_label), str->str);
	g_string_free (str, TRUE);

	gtk_label_set_label (GTK_LABEL (priv->name_label),
			     gs_app_get_name (priv->app));
	if (priv->show_update &&
	    gs_app_get_state (priv->app) == AS_APP_STATE_UPDATABLE) {
		gtk_widget_show (priv->version_label);
		gtk_widget_hide (priv->star);
		gtk_label_set_label (GTK_LABEL (priv->version_label),
				     gs_app_get_update_version_ui (priv->app));
	} else {
		gtk_widget_hide (priv->version_label);
		if (gs_app_get_kind (priv->app) == GS_APP_KIND_MISSING ||
		    (gs_app_get_state (priv->app) == AS_APP_STATE_AVAILABLE_LOCAL && gs_app_get_rating (priv->app) < 0))
			gtk_widget_hide (priv->star);
		else
			gtk_widget_show (priv->star);
		gtk_widget_set_sensitive (priv->star, FALSE);
		if (gs_app_get_rating_kind (priv->app) == GS_APP_RATING_KIND_USER) {
			gs_star_widget_set_rating (GS_STAR_WIDGET (priv->star),
						   GS_APP_RATING_KIND_USER,
						   gs_app_get_rating (priv->app));
		} else {
			gs_star_widget_set_rating (GS_STAR_WIDGET (priv->star),
						   GS_APP_RATING_KIND_KUDOS,
						   gs_app_get_kudos_percentage (priv->app));
		}
		gtk_label_set_label (GTK_LABEL (priv->version_label),
				     gs_app_get_version_ui (priv->app));
	}

	if (priv->show_update || priv->show_codec) {
		gtk_widget_hide (priv->folder_label);
	} else {
		_cleanup_object_unref_ GsFolders *folders = NULL;
		const gchar *folder;
		folders = gs_folders_get ();
		folder = gs_folders_get_app_folder (folders, gs_app_get_id (priv->app), gs_app_get_categories (priv->app));
		if (folder)
			folder = gs_folders_get_folder_name (folders, folder);
		gtk_label_set_label (GTK_LABEL (priv->folder_label), folder);
		gtk_widget_set_visible (priv->folder_label, folder != NULL);
	}

	if (gs_app_get_pixbuf (priv->app))
		gs_image_set_from_pixbuf (GTK_IMAGE (priv->image),
					  gs_app_get_pixbuf (priv->app));

	context = gtk_widget_get_style_context (priv->image);
	if (gs_app_get_kind (priv->app) == GS_APP_KIND_MISSING)
		gtk_style_context_add_class (context, "dimmer-label");
	else
		gtk_style_context_remove_class (context, "dimmer-label");

	gtk_widget_set_visible (priv->button, FALSE);
	gtk_widget_set_sensitive (priv->button, TRUE);
	gtk_widget_set_visible (priv->spinner, FALSE);
	gtk_widget_set_visible (priv->label, FALSE);

	context = gtk_widget_get_style_context (priv->button);
	gtk_style_context_remove_class (context, "destructive-action");

	switch (gs_app_get_state (priv->app)) {
	case AS_APP_STATE_UNAVAILABLE:
		gtk_widget_set_visible (priv->button, TRUE);
		if (gs_app_get_url (priv->app, AS_URL_KIND_MISSING) != NULL) {
			/* TRANSLATORS: this is a button next to the search results that
			 * allows the application to be easily installed */
			gtk_button_set_label (GTK_BUTTON (priv->button), _("Visit website"));
		} else {
			/* TRANSLATORS: this is a button next to the search results that
			 * allows the application to be easily installed.
			 * The ellipsis indicates that further steps are required */
			gtk_button_set_label (GTK_BUTTON (priv->button), _("Install…"));
		}
		break;
	case AS_APP_STATE_QUEUED_FOR_INSTALL:
		gtk_widget_set_visible (priv->label, TRUE);
		gtk_widget_set_visible (priv->button, TRUE);
		/* TRANSLATORS: this is a button next to the search results that
		 * allows to cancel a queued install of the application */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("Cancel"));
		/* TRANSLATORS: this is a label that describes an application
		 * that has been queued for installation */
		gtk_label_set_label (GTK_LABEL (priv->label), _("Pending"));
		break;
	case AS_APP_STATE_AVAILABLE:
	case AS_APP_STATE_AVAILABLE_LOCAL:
		gtk_widget_set_visible (priv->button, TRUE);
		/* TRANSLATORS: this is a button next to the search results that
		 * allows the application to be easily installed */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("Install"));
		break;
	case AS_APP_STATE_UPDATABLE:
	case AS_APP_STATE_INSTALLED:
		if (gs_app_get_kind (priv->app) != GS_APP_KIND_SYSTEM)
			gtk_widget_set_visible (priv->button, TRUE);
		/* TRANSLATORS: this is a button next to the search results that
		 * allows the application to be easily removed */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("Remove"));
		if (priv->colorful)
			gtk_style_context_add_class (context, "destructive-action");
		break;
	case AS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (priv->button, TRUE);
		gtk_widget_set_sensitive (priv->button, FALSE);
		/* TRANSLATORS: this is a button next to the search results that
		 * shows the status of an application being installed */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("Installing"));
		break;
	case AS_APP_STATE_REMOVING:
		gtk_spinner_start (GTK_SPINNER (priv->spinner));
		gtk_widget_set_visible (priv->spinner, TRUE);
		gtk_widget_set_visible (priv->button, TRUE);
		gtk_widget_set_sensitive (priv->button, FALSE);
		/* TRANSLATORS: this is a button next to the search results that
		 * shows the status of an application being erased */
		gtk_button_set_label (GTK_BUTTON (priv->button), _("Removing"));
		break;
	default:
		break;
	}

	gtk_widget_set_visible (priv->button_box, !priv->show_update);

	if (priv->selectable) {
		if (gs_app_get_id_kind (priv->app) == AS_ID_KIND_DESKTOP ||
		    gs_app_get_id_kind (priv->app) == AS_ID_KIND_WEB_APP)
			gtk_widget_set_visible (priv->checkbox, TRUE);
		gtk_widget_set_sensitive (priv->button, FALSE);
	} else {
		gtk_widget_set_visible (priv->checkbox, FALSE);
	}
}

static void
child_unrevealed (GObject *revealer, GParamSpec *pspec, gpointer user_data)
{
	GsAppRow *app_row = user_data;

	g_signal_emit (app_row, signals[SIGNAL_UNREVEALED], 0);
}

void
gs_app_row_unreveal (GsAppRow *app_row)
{
	GtkWidget *child;
	GtkWidget *revealer;

	g_return_if_fail (GS_IS_APP_ROW (app_row));

	child = gtk_bin_get_child (GTK_BIN (app_row));
	gtk_widget_set_sensitive (child, FALSE);

	revealer = gtk_revealer_new ();
	gtk_revealer_set_reveal_child (GTK_REVEALER (revealer), TRUE);
	gtk_widget_show (revealer);

	g_object_ref (child);
	gtk_container_remove (GTK_CONTAINER (app_row), child);
	gtk_container_add (GTK_CONTAINER (revealer), child);
	g_object_unref (child);

	gtk_container_add (GTK_CONTAINER (app_row), revealer);
	g_signal_connect (revealer, "notify::child-revealed",
			  G_CALLBACK (child_unrevealed), app_row);
	gtk_revealer_set_reveal_child (GTK_REVEALER (revealer), FALSE);
}

/**
 * gs_app_row_get_app:
 **/
GsApp *
gs_app_row_get_app (GsAppRow *app_row)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);
	g_return_val_if_fail (GS_IS_APP_ROW (app_row), NULL);
	return priv->app;
}

/**
 * gs_app_row_refresh_idle_cb:
 **/
static gboolean
gs_app_row_refresh_idle_cb (gpointer user_data)
{
	GsAppRow *app_row = GS_APP_ROW (user_data);
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);
	priv->pending_refresh_id = 0;
	gs_app_row_refresh (app_row);
	return FALSE;
}

/**
 * gs_app_row_notify_props_changed_cb:
 **/
static void
gs_app_row_notify_props_changed_cb (GsApp *app,
				    GParamSpec *pspec,
				    GsAppRow *app_row)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);
	if (priv->pending_refresh_id > 0)
		return;
	priv->pending_refresh_id = g_idle_add (gs_app_row_refresh_idle_cb, app_row);
}

/**
 * gs_app_row_set_app:
 **/
void
gs_app_row_set_app (GsAppRow *app_row, GsApp *app)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);
	g_return_if_fail (GS_IS_APP_ROW (app_row));
	g_return_if_fail (GS_IS_APP (app));
	priv->app = g_object_ref (app);
	g_signal_connect_object (priv->app, "notify::state",
				 G_CALLBACK (gs_app_row_notify_props_changed_cb),
				 app_row, 0);
	g_signal_connect_object (priv->app, "notify::rating",
				 G_CALLBACK (gs_app_row_notify_props_changed_cb),
				 app_row, 0);
	g_signal_connect_object (priv->app, "notify::progress",
				 G_CALLBACK (gs_app_row_notify_props_changed_cb),
				 app_row, 0);
	gs_app_row_refresh (app_row);
}

/**
 * gs_app_row_destroy:
 **/
static void
gs_app_row_destroy (GtkWidget *object)
{
	GsAppRow *app_row = GS_APP_ROW (object);
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	if (priv->app)
		g_signal_handlers_disconnect_by_func (priv->app, gs_app_row_notify_props_changed_cb, app_row);

	g_clear_object (&priv->app);
	if (priv->pending_refresh_id != 0) {
		g_source_remove (priv->pending_refresh_id);
		priv->pending_refresh_id = 0;
	}

	GTK_WIDGET_CLASS (gs_app_row_parent_class)->destroy (object);
}

static void
gs_app_row_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsAppRow *app_row = GS_APP_ROW (object);

	switch (prop_id) {
	case PROP_SELECTED:
		gs_app_row_set_selected (app_row, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_row_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsAppRow *app_row = GS_APP_ROW (object);

	switch (prop_id) {
	case PROP_SELECTED:
		g_value_set_boolean (value, gs_app_row_get_selected (app_row));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
       		break;
	}
}

static void
gs_app_row_class_init (GsAppRowClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->set_property = gs_app_row_set_property;
	object_class->get_property = gs_app_row_get_property;

	widget_class->destroy = gs_app_row_destroy;

	pspec = g_param_spec_boolean ("selected", NULL, NULL,
				      FALSE, G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_SELECTED, pspec);

	signals [SIGNAL_BUTTON_CLICKED] =
		g_signal_new ("button-clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsAppRowClass, button_clicked),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals [SIGNAL_UNREVEALED] =
		g_signal_new ("unrevealed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsAppRowClass, unrevealed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-app-row.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, image);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, name_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, name_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, version_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, star);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, folder_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, description_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, button_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, button);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, spinner);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, checkbox);
}

static void
button_clicked (GtkWidget *widget, GsAppRow *app_row)
{
	g_signal_emit (app_row, signals[SIGNAL_BUTTON_CLICKED], 0);
}

static void
checkbox_toggled (GtkWidget *widget, GsAppRow *app_row)
{
	g_object_notify (G_OBJECT (app_row), "selected");
}

static void
gs_app_row_init (GsAppRow *app_row)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	gtk_widget_set_has_window (GTK_WIDGET (app_row), FALSE);
	gtk_widget_init_template (GTK_WIDGET (app_row));

	priv->colorful = TRUE;

	g_signal_connect (priv->button, "clicked",
			  G_CALLBACK (button_clicked), app_row);
	g_signal_connect (priv->checkbox, "toggled",
			  G_CALLBACK (checkbox_toggled), app_row);
}

void
gs_app_row_set_size_groups (GsAppRow *app_row,
			    GtkSizeGroup *image,
			    GtkSizeGroup *name)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	gtk_size_group_add_widget (image, priv->image);
	gtk_size_group_add_widget (name, priv->name_box);
}

void
gs_app_row_set_colorful (GsAppRow *app_row,
			    gboolean     colorful)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	priv->colorful = colorful;
}

void
gs_app_row_set_show_codec (GsAppRow *app_row, gboolean show_codec)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	priv->show_codec = show_codec;
}

/**
 * gs_app_row_set_show_update:
 *
 * Only really useful for the update panel to call
 **/
void
gs_app_row_set_show_update (GsAppRow *app_row, gboolean show_update)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	priv->show_update = show_update;
}

void
gs_app_row_set_selectable (GsAppRow *app_row, gboolean selectable)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	priv->selectable = selectable;
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->checkbox), FALSE);
	gs_app_row_refresh (app_row);
}

void
gs_app_row_set_selected (GsAppRow *app_row, gboolean selected)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	if (!priv->selectable)
		return;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->checkbox)) != selected) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->checkbox), selected);
		g_object_notify (G_OBJECT (app_row), "selected");
	}
}

gboolean
gs_app_row_get_selected (GsAppRow *app_row)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	if (!priv->selectable)
		return FALSE;

	return gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->checkbox));
}

GtkWidget *
gs_app_row_new (void)
{
	return g_object_new (GS_TYPE_APP_ROW, NULL);
}

/* vim: set noexpandtab: */
