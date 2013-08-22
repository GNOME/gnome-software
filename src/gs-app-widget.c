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
	gchar		*status;
	GsAppWidgetKind	 kind;
	GtkWidget	*widget_button;
	GtkWidget	*widget_description1;
	GtkWidget	*widget_description2;
	GtkWidget	*widget_description3;
	GtkWidget	*widget_read_more;
	GtkWidget	*widget_image;
	GtkWidget	*widget_name;
	GtkWidget	*widget_spinner;
	GtkWidget	*widget_version;
};


#define	GS_APP_WIDGET_MAX_LINES_NO_EXPANDER	3

G_DEFINE_TYPE (GsAppWidget, gs_app_widget, GTK_TYPE_BOX)

enum {
	SIGNAL_BUTTON_CLICKED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

/**
 * gs_app_widget_refresh:
 **/
static void
gs_app_widget_refresh (GsAppWidget *app_widget)
{
	GsAppWidgetPrivate *priv = app_widget->priv;
	GtkStyleContext *context;

	if (app_widget->priv->app == NULL)
		return;

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
	gtk_style_context_remove_class (context, "suggested-action");

	switch (app_widget->priv->kind) {
	case GS_APP_WIDGET_KIND_INSTALL:
		gtk_widget_set_visible (priv->widget_spinner, FALSE);
		gtk_widget_set_visible (priv->widget_button, TRUE);
		gtk_button_set_label (GTK_BUTTON (priv->widget_button), _("Install"));
		gtk_style_context_add_class (context, "suggested-action");
		break;
	case GS_APP_WIDGET_KIND_REMOVE:
		gtk_widget_set_visible (priv->widget_spinner, FALSE);
		gtk_widget_set_visible (priv->widget_button, TRUE);
		gtk_button_set_label (GTK_BUTTON (priv->widget_button), _("Remove"));
		gtk_style_context_add_class (context, "destructive-action");
		break;
	case GS_APP_WIDGET_KIND_UPDATE:
		gtk_widget_set_visible (priv->widget_spinner, FALSE);
		gtk_widget_set_visible (priv->widget_button, FALSE);
		gtk_button_set_label (GTK_BUTTON (priv->widget_button), _("Update"));
		gtk_style_context_add_class (context, "suggested-action");
		break;
	case GS_APP_WIDGET_KIND_BUSY:
		gtk_spinner_start (GTK_SPINNER (priv->widget_spinner));
		gtk_widget_set_visible (priv->widget_spinner, TRUE);
		gtk_widget_set_visible (priv->widget_button, TRUE);
		gtk_widget_set_sensitive (priv->widget_button, FALSE);
		gtk_button_set_label (GTK_BUTTON (priv->widget_button), priv->status);
		break;
	default:
		gtk_widget_set_visible (priv->widget_button, FALSE);
		break;
	}
}

/**
 * gs_app_widget_get_status:
 **/
const gchar *
gs_app_widget_get_status (GsAppWidget *app_widget)
{
	g_return_val_if_fail (GS_IS_APP_WIDGET (app_widget), NULL);
	return app_widget->priv->status;
}

/**
 * gs_app_widget_get_kind:
 **/
GsAppWidgetKind
gs_app_widget_get_kind (GsAppWidget *app_widget)
{
	g_return_val_if_fail (GS_IS_APP_WIDGET (app_widget), 0);
	return app_widget->priv->kind;
}

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
-                                       search_len - replace_len);
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
	gs_app_widget_refresh (app_widget);
}

/**
 * gs_app_widget_set_status:
 **/
void
gs_app_widget_set_status (GsAppWidget *app_widget, const gchar *status)
{
	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	g_return_if_fail (status != NULL);
	g_free (app_widget->priv->status);
	app_widget->priv->status = g_strdup (status);
	gs_app_widget_refresh (app_widget);
}

/**
 * gs_app_widget_set_kind:
 **/
void
gs_app_widget_set_kind (GsAppWidget *app_widget, GsAppWidgetKind kind)
{
	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	app_widget->priv->kind = kind;
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

	g_free (priv->status);
	priv->status = NULL;
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

static void
break_lines (PangoContext *context,
             const gchar *text, const gchar *end,
             gint width,
             gchar **line1, gchar **line2, gchar **line3)
{
        PangoLayout *layout;
        PangoLayoutIter *iter;
        gchar *tmp;
        gchar *p, *p2, *r;
        gint i2 = 0, i3 = 0, i4 = 0;
        gint lines;

        layout = pango_layout_new (context);
        pango_layout_set_width (layout, width * PANGO_SCALE);
        pango_layout_set_wrap (layout, PANGO_WRAP_WORD);

        tmp = g_strconcat (text, end, NULL);
        pango_layout_set_text (layout, tmp, -1);
        g_free (tmp);

        lines = pango_layout_get_line_count (layout);

        iter = pango_layout_get_iter (layout);
        if (pango_layout_iter_next_line (iter))
                i2 = pango_layout_iter_get_index (iter);
        else
                goto out;
        if (pango_layout_iter_next_line (iter))
                i3 = pango_layout_iter_get_index (iter);
        else
                goto out;
        if (pango_layout_iter_next_line (iter))
                i4 = pango_layout_iter_get_index (iter);
        else
                goto out;
        pango_layout_iter_free (iter);

        p = (gchar *)text + i4;
        while (text <= p && pango_layout_get_line_count (layout) > 3) {
                p = g_utf8_prev_char (p);
                r = g_strndup (text, p - text);
                p2 = g_strconcat (r, "...", end, NULL);
                pango_layout_set_text (layout, p2, -1);
                g_free (p2);
                g_free (r);
        }

out:
        g_object_unref (layout);

        if (lines == 1) {
                *line1 = g_strdup (text);
                *line2 = NULL;
                *line3 = NULL;
        }
        else if (lines == 2) {
                i2 = MIN (i2, (gint)strlen (text));
                *line1 = g_strndup (text, i2);
                *line2 = g_strdup (text + i2);
                *line3 = NULL;
        }
        else {
                i2 = MIN (i2, (gint)strlen (text));
                i3 = MIN (i3, (gint)strlen (text));
                *line1 = g_strndup (text, i2);
                *line2 = g_strndup (text + i2, i3 - i2);
                *line3 = g_strdup (text + i3);
        }
}

static void
size_allocate_cb (GtkWidget     *box,
                  GtkAllocation *allocation,
                  GsAppWidget   *app_widget)
{
        gchar *tmp;
        gchar *line1, *line2, *line3;
        GString *s = NULL;

	tmp = (gchar *)gs_app_get_description (app_widget->priv->app);
	if (tmp == NULL) {
		tmp = _("The author of this software has not included a long description.");
        }
        else {
                s = g_string_new (tmp);
                _g_string_replace (s, "\n", " ");
                tmp = s->str;
        }

        break_lines (gtk_widget_get_pango_context (box),
                     tmp, _("Read More"), allocation->width,
                     &line1, &line2, &line3);

        gtk_label_set_label (GTK_LABEL (app_widget->priv->widget_description1), line1);
        gtk_label_set_label (GTK_LABEL (app_widget->priv->widget_description2), line2);
        gtk_label_set_label (GTK_LABEL (app_widget->priv->widget_description3), line3);

        if (s)
                g_string_free (s, TRUE);
}

/**
 * gs_app_widget_init:
 **/
static void
gs_app_widget_init (GsAppWidget *app_widget)
{
	GsAppWidgetPrivate *priv;
	GtkWidget *box, *box2;
        gchar *tmp;
	PangoAttrList *attr_list;

	g_return_if_fail (GS_IS_APP_WIDGET (app_widget));
	app_widget->priv = G_TYPE_INSTANCE_GET_PRIVATE (app_widget,
							GS_TYPE_APP_WIDGET,
							GsAppWidgetPrivate);
	priv = app_widget->priv;
	priv->markdown = ch_markdown_new ();

	/* set defaults */
	gtk_box_set_spacing (GTK_BOX (app_widget), 3);
	gtk_widget_set_margin_left (GTK_WIDGET (app_widget), 9);
	gtk_widget_set_margin_top (GTK_WIDGET (app_widget), 9);
	gtk_widget_set_margin_bottom (GTK_WIDGET (app_widget), 9);

	/* pixbuf */
	priv->widget_image = gtk_image_new_from_icon_name ("missing-image",
							   GTK_ICON_SIZE_DIALOG);
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
	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_hexpand (box, TRUE);
        gtk_widget_set_halign (box, GTK_ALIGN_FILL);
	priv->widget_description1 = gtk_label_new (NULL);
	gtk_misc_set_alignment (GTK_MISC (priv->widget_description1), 0.0, 0.5);
	gtk_label_set_ellipsize (GTK_LABEL (priv->widget_description1), PANGO_ELLIPSIZE_END);
        gtk_container_add (GTK_CONTAINER (box), priv->widget_description1);
	priv->widget_description2 = gtk_label_new (NULL);
	gtk_misc_set_alignment (GTK_MISC (priv->widget_description2), 0.0, 0.5);
	gtk_label_set_ellipsize (GTK_LABEL (priv->widget_description2), PANGO_ELLIPSIZE_END);
        gtk_container_add (GTK_CONTAINER (box), priv->widget_description2);
        box2 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_box_pack_start (GTK_BOX (box), box2, TRUE, TRUE, 0);
	priv->widget_description3 = gtk_label_new (NULL);
	gtk_misc_set_alignment (GTK_MISC (priv->widget_description3), 0.0, 0.5);
	gtk_label_set_ellipsize (GTK_LABEL (priv->widget_description3), PANGO_ELLIPSIZE_END);
        gtk_widget_set_size_request (priv->widget_description3, 100, -1);
        gtk_container_add (GTK_CONTAINER (box2), priv->widget_description3);
        priv->widget_read_more = gtk_label_new (NULL);
        tmp = g_markup_printf_escaped ("<a href=''>%s</a>", _("Read More"));
        gtk_label_set_markup (GTK_LABEL (priv->widget_read_more), tmp);
        g_free (tmp);
        gtk_misc_set_alignment (GTK_MISC (priv->widget_read_more), 1, 0.5);
        gtk_widget_set_halign (priv->widget_read_more, GTK_ALIGN_END);
        gtk_box_pack_start (GTK_BOX (box2), priv->widget_read_more, TRUE, TRUE, 0);

	gtk_box_pack_start (GTK_BOX (app_widget), box, TRUE, TRUE, 0);
        g_signal_connect (box, "size-allocate", G_CALLBACK (size_allocate_cb), app_widget);
        gtk_widget_show_all (box);


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

/**
 * gs_app_widget_new:
 **/
GtkWidget *
gs_app_widget_new (void)
{
	return g_object_new (GS_TYPE_APP_WIDGET, NULL);
}

