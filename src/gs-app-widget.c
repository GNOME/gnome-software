/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2013 Richard Hughes <richard@hughsie.com>
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

#include "gs-app-widget.h"
#include "ch-markdown.h"

struct _GsAppWidgetPrivate
{
	ChMarkdown	*markdown;
	GsApp		*app;
	GtkWidget	*widget_button;
	GtkWidget	*widget_description;
	GtkWidget	*widget_image;
	GtkWidget	*widget_name;
	GtkWidget	*widget_spinner;
	GtkWidget	*widget_version;
        gboolean         colorful;
};


#define	GS_APP_WIDGET_MAX_LINES_NO_EXPANDER	3

G_DEFINE_TYPE (GsAppWidget, gs_app_widget, GTK_TYPE_BOX)

enum {
	SIGNAL_BUTTON_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

static guint
_g_string_replace (GString *string, const gchar *search, const gchar *replace)
{
       gchar *tmp;
       guint cnt = 0;
       guint replace_len;
       guint search_len;

       search_len = strlen (search);
       replace_len = strlen (replace);

       do {
	       tmp = g_strstr_len (string->str, -1, search);
	       if (tmp == NULL)
		       goto out;

	       /* reallocate the string if required */
	       if (search_len > replace_len) {
		       g_string_erase (string,
				       tmp - string->str,
-				       search_len - replace_len);
	       }
	       if (search_len < replace_len) {
		       g_string_insert_len (string,
					    tmp - string->str,
					    search,
					    replace_len - search_len);
	       }

	       /* just memcmp in the new string */
	       memcpy (tmp, replace, replace_len);
	       cnt++;
       } while (TRUE);
out:
       return cnt;
}

/**
 * gs_app_widget_refresh:
 **/
static void
gs_app_widget_refresh (GsAppWidget *app_widget)
{
	GsAppWidgetPrivate *priv = app_widget->priv;
	GtkStyleContext *context;
	GtkWidget *box;
        const gchar *tmp;
        GString *s = NULL;

	if (app_widget->priv->app == NULL)
		return;

	tmp = (gchar *)gs_app_get_description (app_widget->priv->app);
	if (tmp) {
		s = g_string_new (tmp);
		_g_string_replace (s, "\n", " ");
		tmp = s->str;
	}
        else {
                tmp = (gchar *)gs_app_get_summary (app_widget->priv->app);
        }
	gtk_label_set_label (GTK_LABEL (priv->widget_description), tmp);
        if (s)
                g_string_free (s, TRUE);
	gtk_label_set_label (GTK_LABEL (priv->widget_name),
			     gs_app_get_name (priv->app));
	gtk_label_set_label (GTK_LABEL (priv->widget_version),
			     gs_app_get_version (priv->app));
	if (gs_app_get_pixbuf (priv->app))
		gtk_image_set_from_pixbuf (GTK_IMAGE (priv->widget_image),
					   gs_app_get_pixbuf (priv->app));
	gtk_widget_set_visible (priv->widget_name, TRUE);
	gtk_widget_set_visible (priv->widget_version, TRUE);
	gtk_widget_set_visible (priv->widget_image, TRUE);
	gtk_widget_set_visible (priv->widget_button, TRUE);
	gtk_widget_set_sensitive (priv->widget_button, TRUE);

	/* show / hide widgets depending on kind */
	context = gtk_widget_get_style_context (priv->widget_button);
	gtk_style_context_remove_class (context, "destructive-action");

	switch (gs_app_get_state (app_widget->priv->app)) {
	case GS_APP_STATE_AVAILABLE:
		gtk_widget_set_visible (priv->widget_spinner, FALSE);
		gtk_widget_set_visible (priv->widget_button, TRUE);
		gtk_button_set_label (GTK_BUTTON (priv->widget_button), _("Install"));
		break;
	case GS_APP_STATE_INSTALLED:
		gtk_widget_set_visible (priv->widget_spinner, FALSE);
		gtk_widget_set_visible (priv->widget_button, TRUE);
		gtk_button_set_label (GTK_BUTTON (priv->widget_button), _("Remove"));
                if (priv->colorful)
        		gtk_style_context_add_class (context, "destructive-action");
		break;
	case GS_APP_STATE_UPDATABLE:
		gtk_widget_set_visible (priv->widget_spinner, FALSE);
		gtk_widget_set_visible (priv->widget_button, FALSE);
		gtk_button_set_label (GTK_BUTTON (priv->widget_button), _("Update"));
		break;
	case GS_APP_STATE_INSTALLING:
		gtk_spinner_start (GTK_SPINNER (priv->widget_spinner));
		gtk_widget_set_visible (priv->widget_spinner, TRUE);
		gtk_widget_set_visible (priv->widget_button, TRUE);
		gtk_widget_set_sensitive (priv->widget_button, FALSE);
		gtk_button_set_label (GTK_BUTTON (priv->widget_button), _("Installing"));
		break;
	case GS_APP_STATE_REMOVING:
		gtk_spinner_start (GTK_SPINNER (priv->widget_spinner));
		gtk_widget_set_visible (priv->widget_spinner, TRUE);
		gtk_widget_set_visible (priv->widget_button, TRUE);
		gtk_widget_set_sensitive (priv->widget_button, FALSE);
		gtk_button_set_label (GTK_BUTTON (priv->widget_button), _("Removing"));
		break;
	default:
		gtk_widget_set_visible (priv->widget_button, FALSE);
		break;
	}
	box = gtk_widget_get_parent (priv->widget_button);
	gtk_widget_set_visible (box, gtk_widget_get_visible (priv->widget_spinner) ||
				     gtk_widget_get_visible (priv->widget_button));
}

/**
 * gs_app_widget_get_app:
 **/
GsApp *
gs_app_widget_get_app (GsAppWidget *app_widget)
{
	g_return_val_if_fail (GS_IS_APP_WIDGET (app_widget), NULL);
	return app_widget->priv->app;
}


/**
 * gs_app_widget_set_app:
 **/
void
gs_app_widget_set_app (GsAppWidget *app_widget, GsApp *app)
{
	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	g_return_if_fail (GS_IS_APP (app));
	app_widget->priv->app = g_object_ref (app);
	g_signal_connect_object (app_widget->priv->app, "state-changed",
			         G_CALLBACK (gs_app_widget_refresh),
			         app_widget, G_CONNECT_SWAPPED);
	gs_app_widget_refresh (app_widget);
}

/**
 * gs_app_widget_destroy:
 **/
static void
gs_app_widget_destroy (GtkWidget *object)
{
	GsAppWidget *app_widget = GS_APP_WIDGET (object);
	GsAppWidgetPrivate *priv = app_widget->priv;

	if (priv->markdown != NULL)
		g_clear_object (&priv->markdown);
	if (priv->app != NULL)
		g_clear_object (&priv->app);

	GTK_WIDGET_CLASS (gs_app_widget_parent_class)->destroy (object);
}

static void
gs_app_widget_class_init (GsAppWidgetClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->destroy = gs_app_widget_destroy;

	signals [SIGNAL_BUTTON_CLICKED] =
		g_signal_new ("button-clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsAppWidgetClass, button_clicked),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (GsAppWidgetPrivate));
}

/**
 * gs_app_widget_button_clicked_cb:
 **/
static void
gs_app_widget_button_clicked_cb (GtkWidget *widget, GsAppWidget *app_widget)
{
	g_signal_emit (app_widget, signals[SIGNAL_BUTTON_CLICKED], 0);
}

/**
 * gs_app_widget_init:
 **/
static void
gs_app_widget_init (GsAppWidget *app_widget)
{
	GsAppWidgetPrivate *priv;
	GtkWidget *box;
	PangoAttrList *attr_list;

	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	app_widget->priv = G_TYPE_INSTANCE_GET_PRIVATE (app_widget,
							GS_TYPE_APP_WIDGET,
							GsAppWidgetPrivate);
	priv = app_widget->priv;
	priv->markdown = ch_markdown_new ();

        priv->colorful = TRUE;

	/* set defaults */
	gtk_box_set_spacing (GTK_BOX (app_widget), 3);
	g_object_set (app_widget, "margin", 9, NULL);

	/* pixbuf */
	priv->widget_image = gtk_image_new ();
	gtk_image_set_pixel_size (GTK_IMAGE (priv->widget_image), 64);

	gtk_widget_set_margin_right (priv->widget_image, 9);
	gtk_widget_set_valign (priv->widget_image, GTK_ALIGN_START);
	gtk_box_pack_start (GTK_BOX (app_widget), priv->widget_image, FALSE, FALSE, 0);

	/* name > version */
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_set_visible (box, TRUE);
	priv->widget_name = gtk_label_new ("name");
	gtk_label_set_ellipsize (GTK_LABEL (priv->widget_name),
				 PANGO_ELLIPSIZE_NONE);
	gtk_label_set_line_wrap (GTK_LABEL (priv->widget_name), TRUE);
	gtk_label_set_max_width_chars (GTK_LABEL (priv->widget_name), 20);
	gtk_misc_set_alignment (GTK_MISC (priv->widget_name), 0.0, 0.5);
	gtk_widget_set_size_request (priv->widget_name, 200, -1);
	attr_list = pango_attr_list_new ();
	pango_attr_list_insert (attr_list,
				pango_attr_weight_new (PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes (GTK_LABEL (priv->widget_name), attr_list);
	pango_attr_list_unref (attr_list);
	priv->widget_version = gtk_label_new ("version");
	gtk_misc_set_alignment (GTK_MISC (priv->widget_version), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (box), priv->widget_name, FALSE, FALSE, 0);
	gtk_box_pack_start (GTK_BOX (box), priv->widget_version, FALSE, FALSE, 6);
	gtk_box_pack_start (GTK_BOX (app_widget), box, FALSE, TRUE, 0);

	/* description */
	priv->widget_description = gtk_label_new (NULL);
        gtk_widget_show (priv->widget_description);
        gtk_label_set_line_wrap (GTK_LABEL (priv->widget_description), TRUE);
#if GTK_CHECK_VERSION (3, 9, 13)
        gtk_label_set_lines (GTK_LABEL (priv->widget_description), 3);
#endif
	gtk_widget_set_hexpand (priv->widget_description, TRUE);
	gtk_widget_set_halign (priv->widget_description, GTK_ALIGN_FILL);
	gtk_misc_set_alignment (GTK_MISC (priv->widget_description), 0.0, 0.5);
	gtk_label_set_ellipsize (GTK_LABEL (priv->widget_description), PANGO_ELLIPSIZE_END);

	gtk_box_pack_start (GTK_BOX (app_widget), priv->widget_description, TRUE, TRUE, 0);

	/* button */
	priv->widget_button = gtk_button_new_with_label ("button");
	gtk_widget_set_margin_right (priv->widget_button, 9);
	gtk_widget_set_size_request (priv->widget_button, 100, -1);
	gtk_widget_set_vexpand (priv->widget_button, FALSE);
	gtk_widget_set_hexpand (priv->widget_button, FALSE);
	gtk_widget_set_halign (priv->widget_button, GTK_ALIGN_END);
	g_signal_connect (priv->widget_button, "clicked",
			  G_CALLBACK (gs_app_widget_button_clicked_cb), app_widget);

	/* spinner */
	priv->widget_spinner = gtk_spinner_new ();
	gtk_widget_set_halign (priv->widget_spinner, GTK_ALIGN_END);
	gtk_widget_set_valign (priv->widget_spinner, GTK_ALIGN_CENTER);
	gtk_widget_set_margin_left (priv->widget_spinner, 6);
	gtk_widget_set_margin_right (priv->widget_spinner, 6);

	box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
	gtk_widget_set_size_request (box, 200, -1);
	gtk_widget_set_halign (box, GTK_ALIGN_END);
	gtk_widget_set_valign (box, GTK_ALIGN_CENTER);
	gtk_widget_set_visible (box, TRUE);
	gtk_box_pack_end (GTK_BOX (box), priv->widget_button, FALSE, FALSE, 0);
	gtk_box_pack_end (GTK_BOX (box), priv->widget_spinner, FALSE, FALSE, 0);
	gtk_box_pack_end (GTK_BOX (app_widget), box, FALSE, FALSE, 0);

	/* refresh */
	gs_app_widget_refresh (app_widget);
}

void
gs_app_widget_set_size_groups (GsAppWidget  *app_widget,
			       GtkSizeGroup *image,
			       GtkSizeGroup *name)
{
	GtkWidget *box;

	gtk_size_group_add_widget (image, app_widget->priv->widget_image);

	box = gtk_widget_get_parent (app_widget->priv->widget_name);
	gtk_size_group_add_widget (name, box);
}

void
gs_app_widget_set_colorful (GsAppWidget *app_widget,
                            gboolean     colorful)
{
        app_widget->priv->colorful = colorful;
}

/**
 * gs_app_widget_new:
 **/
GtkWidget *
gs_app_widget_new (void)
{
	return g_object_new (GS_TYPE_APP_WIDGET, NULL);
}

