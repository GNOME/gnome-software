/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2020 Alexander Mikhaylenko
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Authors:
 *  - Alexander Mikhaylenko <alexm@gnome.org>
 *  - Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

/**
 * SECTION:gs-rounded-bin
 * @short_description: A #GtkBin which can clip rounded corners into its child
 *
 * #GsRoundedBin is a basic #GtkBin subclass which supports masking the child
 * widget to apply rounded corners to it. It has no other layout functionality,
 * and will hopefully eventually be replaced by rounded corner functionality in
 * GTK 4 itself.
 *
 * To use it, set the `border-radius` property in CSS:
 * |[
 * path>to>parent>widget>rounded-bin {
 *   border-radius: 12px;
 * }
 * ]|
 *
 * Adapted from
 *  * https://gitlab.gnome.org/GNOME/libhandy/-/blob/1.0.0/src/hdy-window-mixin.c
 *  * https://gitlab.gnome.org/GNOME/fractal/-/blob/c69aacc4/fractal-gtk/src/widgets/clip_container.rs
 *
 * Since: 40
 */

#include "config.h"

#include <cairo.h>
#include <gdk/gdk.h>
#include <glib.h>
#include <glib-object.h>
#include <gtk/gtk.h>

#include "gs-rounded-bin.h"

/* Used to index #GsRoundedBin.masks, and create_masks() uses the numeric
 * values to determine mask centre points */
typedef enum {
	GS_CORNER_TOP_LEFT = 0,
	GS_CORNER_TOP_RIGHT = 1,
	GS_CORNER_BOTTOM_LEFT = 2,
	GS_CORNER_BOTTOM_RIGHT = 3,
} GsCornerType;

struct _GsRoundedBin
{
	GtkBin		 parent_instance;

	gint		 last_border_radius;
	cairo_surface_t	*masks[4];  /* (owned) (indexed-by GsCornerType) */
};

G_DEFINE_TYPE (GsRoundedBin, gs_rounded_bin, GTK_TYPE_BIN)

static void clear_masks (GsRoundedBin *self);

static void
gs_rounded_bin_init (GsRoundedBin *self)
{
	gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);
}

static void
gs_rounded_bin_finalize (GObject *object)
{
	GsRoundedBin *self = GS_ROUNDED_BIN (object);

	clear_masks (self);

	G_OBJECT_CLASS (gs_rounded_bin_parent_class)->finalize (object);
}

static gint
get_border_radius (GtkStyleContext *ctx)
{
	GtkStateFlags state = gtk_style_context_get_state (ctx);
	gint border_radius;
	gtk_style_context_get (ctx, state, "border-radius", &border_radius, NULL);
	return border_radius;
}

static void
create_masks (GsRoundedBin *self,
              cairo_t      *cr,
              gint          radius)
{
	gdouble scale_factor = gtk_widget_get_scale_factor (GTK_WIDGET (self));
	gdouble r = radius;

	clear_masks (self);

	if (radius <= 0.0)
		return;

	for (gsize i = 0; i < G_N_ELEMENTS (self->masks); i++) {
		GsCornerType corner = (GsCornerType) i;
		cairo_surface_t *surface = NULL;
		cairo_t *mask_ctx = NULL;
		gdouble mod_val, val;

		surface = cairo_surface_create_similar_image (cairo_get_target (cr),
							      CAIRO_FORMAT_A8,
							      radius * scale_factor,
							      radius * scale_factor);

		mask_ctx = cairo_create (surface);
		cairo_scale (mask_ctx, scale_factor, scale_factor);
		cairo_set_source_rgb (mask_ctx, 0.0, 0.0, 0.0);
		mod_val = (i % 2 == 0) ? r : 0.0;
		val = (i / 2 == 0) ? r : 0.0;
		cairo_arc (mask_ctx, mod_val, val, r, 0.0, G_PI * 2.0);
		cairo_fill (mask_ctx);

		self->masks[corner] = g_steal_pointer (&surface);

		cairo_destroy (mask_ctx);
	}
}

static void
clear_masks (GsRoundedBin *self)
{
	for (gsize i = 0; i < G_N_ELEMENTS (self->masks); i++)
		g_clear_pointer (&self->masks[i], cairo_surface_destroy);
}

static void
mask_corner (GsRoundedBin *self,
             cairo_t      *cr,
             gdouble       scale_factor,
             GsCornerType  corner,
             gdouble       x,
             gdouble       y)
{
	g_assert (corner >= 0 && corner < G_N_ELEMENTS (self->masks));

	cairo_save (cr);
	cairo_scale (cr, 1.0 / scale_factor, 1.0 / scale_factor);
	cairo_mask_surface (cr, self->masks[corner], x * scale_factor, y * scale_factor);
	cairo_restore (cr);
}

static gboolean
gs_rounded_bin_draw (GtkWidget *widget,
                     cairo_t   *cr)
{
	GsRoundedBin *self = GS_ROUNDED_BIN (widget);
	GdkWindow *window;
	gboolean clip_set = FALSE;
	GdkRectangle clip = { 0, };
	GtkStyleContext *ctx;
	gint width, height, radius;
	gdouble w, h, r, xy;
	gint scale_factor;
	cairo_surface_t *surface = NULL;
	cairo_t *surface_ctx = NULL;

	window = gtk_widget_get_window (widget);
	g_assert (window != NULL);

	/* Propagate the draw further if this is an input-only window. */
	if (!gtk_cairo_should_draw_window (cr, window))
		return FALSE;

	clip_set = gdk_cairo_get_clip_rectangle (cr, &clip);

	ctx = gtk_widget_get_style_context (widget);
	width = gtk_widget_get_allocated_width (widget);
	height = gtk_widget_get_allocated_height (widget);
	w = width;
	h = height;
	radius = get_border_radius (ctx);
	r = radius;
	xy = 0.0;

	/* Don’t do any custom drawing if the radius is default. */
	if (radius <= 0.0)
		return GTK_WIDGET_CLASS (gs_rounded_bin_parent_class)->draw (widget, cr);

	if (!clip_set) {
		clip.width = width;
		clip.height = height;
	}

	cairo_save (cr);

	scale_factor = gtk_widget_get_scale_factor (widget);
	if (radius * scale_factor != self->last_border_radius) {
		create_masks (self, cr, radius);
		self->last_border_radius = radius * scale_factor;
	}

	surface = gdk_window_create_similar_surface (window,
						     CAIRO_CONTENT_COLOR_ALPHA,
						     MAX (width, 1),
						     MAX (height, 1));
	surface_ctx = cairo_create (surface);
	cairo_surface_set_device_offset (surface, -clip.x, -clip.y);

	if (!gtk_widget_get_app_paintable (widget)) {
		gtk_render_background (ctx, surface_ctx, xy, xy, w, h);
		gtk_render_frame (ctx, surface_ctx, xy, xy, w, h);
	}

	if (gtk_bin_get_child (GTK_BIN (widget)) != NULL) {
		gtk_container_propagate_draw (GTK_CONTAINER (widget),
					      gtk_bin_get_child (GTK_BIN (widget)),
					      surface_ctx);
	}

	cairo_set_source_surface (cr, surface, 0.0, 0.0);
	cairo_rectangle (cr, xy + r, xy, w - r * 2.0, r);
	cairo_rectangle (cr, xy + r, xy + h - r, w - r * 2.0, r);
	cairo_rectangle (cr, xy, xy + r, w, h - r * 2.0);
	cairo_fill (cr);

	if (clip.x < xy + r && clip.y < xy + r) {
		mask_corner (self, cr, scale_factor, GS_CORNER_TOP_LEFT, xy, xy);
	}

	if ((clip.x + clip.width) > xy + w - r && clip.y < xy + r) {
		mask_corner (self, cr, scale_factor, GS_CORNER_TOP_RIGHT, xy + w - r, xy);
	}

	if (clip.x < xy + r && (clip.y + clip.height) > xy + h - r) {
		mask_corner (self, cr, scale_factor, GS_CORNER_BOTTOM_LEFT, xy, xy + h - r);
	}

	if ((clip.x + clip.width) > xy + w - r && (clip.y + clip.height) > xy + h - r) {
		mask_corner (self, cr, scale_factor, GS_CORNER_BOTTOM_RIGHT, xy + w - r, xy + h - r);
	}

	cairo_surface_flush (surface);

	cairo_restore (cr);

	cairo_surface_destroy (surface);
	cairo_destroy (surface_ctx);

	/* Continue propagating the draw signal */
	return FALSE;
}

static void
gs_rounded_bin_class_init (GsRoundedBinClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->finalize = gs_rounded_bin_finalize;

	widget_class->draw = gs_rounded_bin_draw;

	gtk_widget_class_set_css_name (widget_class, "rounded-bin");
}
