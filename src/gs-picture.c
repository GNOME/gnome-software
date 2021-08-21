/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Purism SPC
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-picture
 * @title: GsPicture
 * @stability: Stable
 * @short_description: A widget displaying a picture
 *
 * This widget displays the picture stored in a #GdkPixbuf scaled to the
 * allocated size while preserving its aspect ratio.
 *
 * It uses the height-for-width size request mode.
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk-a11y.h>

#include "gs-picture.h"
#include "gs-common.h"

struct _GsPicture
{
	GtkDrawingArea	 parent_instance;

	GdkPixbuf	*pixbuf;
};

G_DEFINE_TYPE (GsPicture, gs_picture, GTK_TYPE_DRAWING_AREA)

typedef enum {
	PROP_PIXBUF = 1,
} GsPictureProperty;

static GParamSpec *obj_props[PROP_PIXBUF + 1] = { NULL, };

/**
 * gs_picture_get_pixbuf:
 * @picture: a #GsPicture
 *
 * Get the value of #GsPicture:pixbuf.
 *
 * Returns: (nullable) (transfer none): the pixbuf
 *
 * Since: 41
 */
GdkPixbuf *
gs_picture_get_pixbuf (GsPicture *picture)
{
	g_return_val_if_fail (GS_IS_PICTURE (picture), NULL);
	return picture->pixbuf;
}

/**
 * gs_picture_set_pixbuf:
 * @picture: a #GsPicture
 * @pixbuf: (transfer none) (nullable): new pixbuf
 *
 * Set the value of #GsPicture:pixbuf, and schedule the widget to
 * be resized. The new pixbuf will be scaled to fit the widget’s
 * existing size allocation.
 *
 * Since: 41
 */
void
gs_picture_set_pixbuf (GsPicture *picture, GdkPixbuf *pixbuf)
{
	g_return_if_fail (GS_IS_PICTURE (picture));

	if (picture->pixbuf == pixbuf)
		return;

	g_set_object (&picture->pixbuf, pixbuf);
	gtk_widget_queue_resize (GTK_WIDGET (picture));

	g_object_notify_by_pspec (G_OBJECT (picture), obj_props[PROP_PIXBUF]);
}

/* This is derived from the private adw_css_measure() from Libhandy. */
static void
css_measure (GtkWidget *widget, GtkOrientation orientation, gint *minimum, gint *natural)
{
	GtkStyleContext *style_context = gtk_widget_get_style_context (widget);
	GtkStateFlags state_flags = gtk_widget_get_state_flags (widget);
	GtkBorder border, margin, padding;
	gint css_width, css_height, min = 0, nat = 0;

	if (minimum)
		min = *minimum;

	if (natural)
		nat = *natural;

	/* Manually apply minimum sizes, the border, the padding and the margin as we
	 * can't use the private GtkGadget.
	 */
	gtk_style_context_get (style_context, state_flags,
			       "min-width", &css_width,
			       "min-height", &css_height,
			       NULL);
	gtk_style_context_get_border (style_context, state_flags, &border);
	gtk_style_context_get_margin (style_context, state_flags, &margin);
	gtk_style_context_get_padding (style_context, state_flags, &padding);
	if (orientation == GTK_ORIENTATION_VERTICAL) {
		min = MAX (min, css_height) +
		      border.top + margin.top + padding.top +
		      border.bottom + margin.bottom + padding.bottom;
		nat = MAX (nat, css_height) +
		      border.top + margin.top + padding.top +
		      border.bottom + margin.bottom + padding.bottom;
	} else {
		min = MAX (min, css_width) +
		      border.left + margin.left + padding.left +
		      border.right + margin.right + padding.right;
		nat = MAX (nat, css_width) +
		      border.left + margin.left + padding.left +
		      border.right + margin.right + padding.right;
	}

	if (minimum)
		*minimum = MAX (min, 0);

	if (natural)
		*natural = MAX (nat, 0);
}

/* This private method is prefixed by the call name because it will be a virtual
 * method in GTK 4.
 */
static void
gs_picture_measure (GtkWidget		*widget,
		    GtkOrientation	 orientation,
		    int			 for_size,
		    int			*minimum,
		    int			*natural,
		    int			*minimum_baseline,
		    int			*natural_baseline)
{
	GsPicture *picture = GS_PICTURE (widget);

	if (minimum)
		*minimum = 0;

	if (natural) {
		if (picture->pixbuf == NULL) {
			*natural = 0;
		} else if (orientation == GTK_ORIENTATION_HORIZONTAL) {
			gdouble width = gdk_pixbuf_get_width (picture->pixbuf);
			gdouble height = gdk_pixbuf_get_height (picture->pixbuf);

			if (for_size < 0)
				*natural = width;
			else
				*natural = height <= 0 ? 0 : (width * for_size) / height;
		} else {
			*natural = gdk_pixbuf_get_height (picture->pixbuf);
		}
	}

	if (minimum_baseline)
		*minimum_baseline = -1;

	if (natural_baseline)
		*natural_baseline = -1;

	css_measure (widget, orientation, minimum, natural);
}

static GtkSizeRequestMode
gs_picture_get_request_mode (GtkWidget *widget)
{
	return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void
gs_picture_get_preferred_width_for_height (GtkWidget	*widget,
					   gint		 height,
					   gint		*minimum,
					   gint		*natural)
{
	gs_picture_measure (widget, GTK_ORIENTATION_HORIZONTAL, height,
			    minimum, natural, NULL, NULL);
}

static void
gs_picture_get_preferred_width (GtkWidget	*widget,
				gint		*minimum,
				gint		*natural)
{
	gs_picture_measure (widget, GTK_ORIENTATION_HORIZONTAL, -1,
			    minimum, natural, NULL, NULL);
}

static void
gs_picture_get_preferred_height_and_baseline_for_width (GtkWidget	*widget,
							gint		 width,
							gint		*minimum,
							gint		*natural,
							gint		*minimum_baseline,
							gint		*natural_baseline)
{
	gs_picture_measure (widget, GTK_ORIENTATION_VERTICAL, width,
			    minimum, natural, minimum_baseline, natural_baseline);
}

static void
gs_picture_get_preferred_height_for_width (GtkWidget	*widget,
					   gint		 width,
					   gint		*minimum,
					   gint		*natural)
{
	gs_picture_measure (widget, GTK_ORIENTATION_VERTICAL, width,
			    minimum, natural, NULL, NULL);
}

static void
gs_picture_get_preferred_height (GtkWidget	*widget,
				 gint		*minimum,
				 gint		*natural)
{
	gs_picture_measure (widget, GTK_ORIENTATION_VERTICAL, -1,
			    minimum, natural, NULL, NULL);
}

/* This is derived from the private adw_css_size_allocate_self() from Libhandy. */
static void
css_size_allocate_self (GtkWidget *widget, GtkAllocation *allocation)
{
	GtkStyleContext *style_context;
	GtkStateFlags state_flags;
	GtkBorder margin;

	/* Manually apply the border, the padding and the margin as we can't use the
	 * private GtkGadget.
	 */
	style_context = gtk_widget_get_style_context (widget);
	state_flags = gtk_widget_get_state_flags (widget);

	gtk_style_context_get_margin (style_context, state_flags, &margin);

	allocation->width -= margin.left + margin.right;
	allocation->height -= margin.top + margin.bottom;
	allocation->x += margin.left;
	allocation->y += margin.top;
}

static void
gs_picture_size_allocate (GtkWidget	*widget,
			  GtkAllocation	*allocation)
{
	css_size_allocate_self (widget, allocation);
	gtk_widget_set_allocation (widget, allocation);
}

/* This is derived from the private get_video_box() from RetroGTK.
 * I (Adrien Plazas) wrote that original code and the initial GsPicture, I
 * accept to license it as GPL-2.0+ so it can be used here. */
static gboolean
get_picture_box (GsPicture	*picture,
		 gdouble	*width,
		 gdouble	*height,
		 gdouble	*ratio,
		 gdouble	*x,
		 gdouble	*y)
{
	gdouble picture_width;
	gdouble picture_height;
	gdouble picture_ratio;
	gdouble allocated_width;
	gdouble allocated_height;
	gdouble allocated_ratio;

	g_assert (width != NULL);
	g_assert (height != NULL);
	g_assert (ratio != NULL);
	g_assert (x != NULL);
	g_assert (y != NULL);

	picture_width = (gdouble) gdk_pixbuf_get_width (picture->pixbuf);
	picture_height = (gdouble) gdk_pixbuf_get_height (picture->pixbuf);
	allocated_width = (gdouble) gtk_widget_get_allocated_width (GTK_WIDGET (picture));
	allocated_height = (gdouble) gtk_widget_get_allocated_height (GTK_WIDGET (picture));

	if (picture_width <= 0 || picture_height <= 0 ||
	    allocated_width <= 0 || allocated_height <= 0)
		return FALSE;

	// Set the size of the display.
	picture_ratio = picture_width / picture_height;
	allocated_ratio = allocated_width / allocated_height;

	// If the widget is wider than the picture…
	if (allocated_ratio > picture_ratio) {
		*height = allocated_height;
		*width = (gdouble) (allocated_height * picture_ratio);
		*ratio = picture_height / allocated_height;
	} else {
		*width = allocated_width;
		*height = (gdouble) (allocated_width / picture_ratio);
		*ratio = picture_width / allocated_width;
	}

	// Set the position of the display.
	*x = (allocated_width - *width) / 2;
	*y = (allocated_height - *height) / 2;

	return TRUE;
}

static gboolean
gs_picture_draw (GtkWidget *widget, cairo_t *cr)
{
	GsPicture *picture = GS_PICTURE (widget);
	gdouble width, height, ratio, x, y;

	if (picture->pixbuf == NULL)
		return FALSE;

	if (!get_picture_box (picture, &width, &height, &ratio, &x, &y))
		return FALSE;

	/* We need to translate before scaling because we don't want the scale
	 * to be taken into account when doing so. */
	cairo_translate (cr, x, y);
	cairo_scale (cr, 1.0 / ratio, 1.0 / ratio);

	gdk_cairo_set_source_pixbuf (cr, picture->pixbuf, 0, 0);
	cairo_paint (cr);

	return FALSE;
}

static void
gs_picture_finalize (GObject *object)
{
	GsPicture *picture = GS_PICTURE (object);

	g_clear_object (&picture->pixbuf);

	G_OBJECT_CLASS (gs_picture_parent_class)->finalize (object);
}

static void
gs_picture_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsPicture *picture = GS_PICTURE (object);

	switch ((GsPictureProperty) prop_id) {
	case PROP_PIXBUF:
		g_value_set_object (value, gs_picture_get_pixbuf (picture));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_picture_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsPicture *picture = GS_PICTURE (object);

	switch ((GsPictureProperty) prop_id) {
	case PROP_PIXBUF:
		gs_picture_set_pixbuf (picture, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_picture_init (GsPicture *picture)
{
	AtkObject *accessible;

	accessible = gtk_widget_get_accessible (GTK_WIDGET (picture));
	if (accessible != NULL) {
		atk_object_set_role (accessible, ATK_ROLE_IMAGE);
		/* Translators: This is the accessibility label for a screenshot. */
		atk_object_set_name (accessible, _("Picture"));
	}
}

static void
gs_picture_class_init (GsPictureClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->finalize = gs_picture_finalize;
	object_class->get_property = gs_picture_get_property;
	object_class->set_property = gs_picture_set_property;

	widget_class->get_request_mode = gs_picture_get_request_mode;
	widget_class->get_preferred_width = gs_picture_get_preferred_width;
	widget_class->get_preferred_width_for_height = gs_picture_get_preferred_width_for_height;
	widget_class->get_preferred_height = gs_picture_get_preferred_height;
	widget_class->get_preferred_height_for_width = gs_picture_get_preferred_height_for_width;
	widget_class->get_preferred_height_and_baseline_for_width = gs_picture_get_preferred_height_and_baseline_for_width;
	widget_class->size_allocate = gs_picture_size_allocate;
	widget_class->draw = gs_picture_draw;

	/**
	 * GsPicture:pixbuf: (nullable)
	 *
	 * The pixbuf to display.
	 *
	 * If this is %NULL, the widget will be zero sized.
	 *
	 * Since: 41
	 */
	obj_props[PROP_PIXBUF] =
		g_param_spec_object ("pixbuf", NULL, NULL,
				     GDK_TYPE_PIXBUF,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	gtk_widget_class_set_accessible_type (widget_class, GTK_TYPE_IMAGE_ACCESSIBLE);
	gtk_widget_class_set_css_name (widget_class, "picture");
}

/**
 * gs_picture_new:
 *
 * Create a new #GsPicture.
 *
 * Returns: (transfer full) (type GsPicture): a new #GsPicture
 *
 * Since: 41
 */
GtkWidget *
gs_picture_new (void)
{
	return g_object_new (GS_TYPE_PICTURE, NULL);
}
