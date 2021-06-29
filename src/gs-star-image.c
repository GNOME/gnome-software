/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-star-image
 * @title: GsStarImage
 * @stability: Unstable
 * @short_description: Draw a star image, which can be partially filled
 *
 * Depending on the #GsStarImage:fraction property, the star image can be
 * drawn as filled only partially or fully or not at all. This is accomplished
 * by using a `color` style property for the filled part and a
 * `star-bg` style property for the unfilled part of the star.
 * The `background` style property controls the area outside the star.
 *
 * Since: 41
 */

#include "config.h"

#include "gs-star-image.h"

struct _GsStarImage
{
	GtkWidget parent_instance;

	gdouble fraction;
};

G_DEFINE_TYPE (GsStarImage, gs_star_image, GTK_TYPE_WIDGET)

enum {
	PROP_FRACTION = 1
};

static void
gs_star_image_outline_star (cairo_t *cr,
			    gint x,
			    gint y,
			    gint radius,
			    gint *out_min_x,
			    gint *out_max_x)
{
	/* Coordinates of the vertices of the star,
	 * where (0, 0) is the centre of the star.
	 * These range from -1 to +1 in both dimensions,
	 * and will be scaled to @radius when drawn. */
	const struct _points {
		gdouble x, y;
	} small_points[] = {
		{  0.000000, -1.000000 },
		{ -1.000035, -0.424931 },
		{ -0.668055,  0.850680 },
		{  0.668055,  0.850680 },
		{  1.000035, -0.424931 }
	}, large_points[] = {
		{  0.000000, -1.000000 },
		{ -1.000035, -0.325033 },
		{ -0.618249,  0.850948 },
		{  0.618249,  0.850948 },
		{  1.000035, -0.325033 }
	}, *points;
	gint ii, nn = G_N_ELEMENTS (small_points), xx, yy;

	/* Safety check */
	G_STATIC_ASSERT (G_N_ELEMENTS (small_points) == G_N_ELEMENTS (large_points));

	if (radius <= 0)
		return;

	/* An arbitrary number, since which the math-precise star looks fine,
	 * while it looks odd for lower sizes. */
	if (radius * 2 > 20)
		points = large_points;
	else
		points = small_points;

	cairo_translate (cr, radius, radius);

	xx = points[0].x * radius;
	yy = points[0].y * radius;

	if (out_min_x)
		*out_min_x = xx;

	if (out_max_x)
		*out_max_x = xx;

	cairo_move_to (cr, xx, yy);

	for (ii = 2; ii <= 2 * nn; ii += 2) {
		xx = points[ii % nn].x * radius;
		yy = points[ii % nn].y * radius;

		if (out_min_x && *out_min_x > xx)
			*out_min_x = xx;

		if (out_max_x && *out_max_x < xx)
			*out_max_x = xx;

		cairo_line_to (cr, xx, yy);
	}
}

static void
gs_star_image_get_property (GObject *object,
			    guint param_id,
			    GValue *value,
			    GParamSpec *pspec)
{
	switch (param_id) {
	case PROP_FRACTION:
		g_value_set_double (value, gs_star_image_get_fraction (GS_STAR_IMAGE (object)));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
gs_star_image_set_property (GObject *object,
			    guint param_id,
			    const GValue *value,
			    GParamSpec *pspec)
{
	switch (param_id) {
	case PROP_FRACTION:
		gs_star_image_set_fraction (GS_STAR_IMAGE (object), g_value_get_double (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static gboolean
gs_star_image_draw (GtkWidget *widget,
		    cairo_t *cr)
{
	GtkAllocation allocation;
	gdouble fraction;
	gint radius;

	fraction = gs_star_image_get_fraction (GS_STAR_IMAGE (widget));

	gtk_widget_get_allocation (widget, &allocation);

	radius = MIN (allocation.width, allocation.height) / 2;

	if (radius > 0) {
		GtkStyleContext *style_context;
		GdkRGBA *star_bg = NULL;
		GdkRGBA star_fg;
		gint min_x = -radius, max_x = radius;

		gtk_widget_style_get (widget,
			"star-bg", &star_bg,
			NULL);

		style_context = gtk_widget_get_style_context (widget);
		gtk_style_context_get_color (style_context,
					     gtk_style_context_get_state (style_context),
					     &star_fg);

		cairo_save (cr);
		gs_star_image_outline_star (cr, allocation.x, allocation.y, radius, &min_x, &max_x);
		cairo_clip (cr);
		if (star_bg)
			gdk_cairo_set_source_rgba (cr, star_bg);
		else
			cairo_set_source_rgb (cr, 0xde / 255.0, 0xdd / 255.0, 0xda / 255.0);
		cairo_rectangle (cr, -radius, -radius, 2 * radius, 2 * radius);
		cairo_fill (cr);

		gdk_cairo_set_source_rgba (cr, &star_fg);
		if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL)
			cairo_rectangle (cr, max_x - (max_x - min_x) * fraction, -radius, (max_x - min_x) * fraction, 2 * radius);
		else
			cairo_rectangle (cr, min_x, -radius, (max_x - min_x) * fraction, 2 * radius);
		cairo_fill (cr);
		cairo_restore (cr);

		g_clear_pointer (&star_bg, gdk_rgba_free);
	}

	return FALSE;
}

static void
gs_star_image_class_init (GsStarImageClass *klass)
{
	GObjectClass *object_class;
	GtkWidgetClass *widget_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = gs_star_image_get_property;
	object_class->set_property = gs_star_image_set_property;

	widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->draw = gs_star_image_draw;

	g_object_class_install_property (object_class,
					 PROP_FRACTION,
					 g_param_spec_double ("fraction", NULL, NULL,
							      0.0, 1.0, 1.0,
							      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY));

	gtk_widget_class_install_style_property (widget_class,
					 g_param_spec_boxed ("star-bg", NULL, NULL,
							     GDK_TYPE_RGBA,
							     G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

	gtk_widget_class_set_css_name (widget_class, "star-image");
}

static void
gs_star_image_init (GsStarImage *self)
{
	self->fraction = 1.0;

	gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);
	gtk_widget_set_size_request (GTK_WIDGET (self), 16, 16);
}

GtkWidget *
gs_star_image_new (void)
{
	return g_object_new (GS_TYPE_STAR_IMAGE, NULL);
}

void
gs_star_image_set_fraction (GsStarImage *self,
			    gdouble fraction)
{
	g_return_if_fail (GS_IS_STAR_IMAGE (self));

	if (self->fraction == fraction)
		return;

	self->fraction = fraction;

	g_object_notify (G_OBJECT (self), "fraction");

	gtk_widget_queue_draw (GTK_WIDGET (self));
}

gdouble
gs_star_image_get_fraction (GsStarImage *self)
{
	g_return_val_if_fail (GS_IS_STAR_IMAGE (self), -1.0);

	return self->fraction;
}
