/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 * Copyright (C) 2021 Endless OS Foundation, Inc
 *
 * Authors:
 *  - Richard Hughes <richard@hughsie.com>
 *  - Kalev Lember <klember@redhat.com>
 *  - Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-key-colors
 * @short_description: Helper functions for calculating key colors
 *
 * Key colors are RGB colors which represent an app, and they are derived from
 * the app’s icon, or manually specified as an override.
 *
 * Use gs_calculate_key_colors() to calculate the key colors from an app’s icon.
 *
 * Since: 40
 */

#include "config.h"

#include <glib.h>
#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "gs-key-colors.h"

typedef struct {
	guint8		 R;
	guint8		 G;
	guint8		 B;
} CdColorRGB8;

static guint32
cd_color_rgb8_to_uint32 (CdColorRGB8 *rgb)
{
	return (guint32) rgb->R |
		(guint32) rgb->G << 8 |
		(guint32) rgb->B << 16;
}

typedef struct {
	GdkRGBA		color;
	guint		cnt;
} GsColorBin;

static gint
gs_color_bin_sort_cb (gconstpointer a, gconstpointer b)
{
	GsColorBin *s1 = (GsColorBin *) a;
	GsColorBin *s2 = (GsColorBin *) b;
	if (s1->cnt < s2->cnt)
		return 1;
	if (s1->cnt > s2->cnt)
		return -1;
	return 0;
}

/* convert range of 0..255 to 0..1 */
static inline gdouble
_convert_from_rgb8 (guchar val)
{
	return (gdouble) val / 255.f;
}

static void
key_colors_set_for_pixbuf (GArray *colors, GdkPixbuf *pb, guint number)
{
	gint rowstride, n_channels;
	gint x, y, width, height;
	guchar *pixels, *p;
	guint bin_size = 200;
	guint i;
	guint number_of_bins;

	/* go through each pixel */
	n_channels = gdk_pixbuf_get_n_channels (pb);
	rowstride = gdk_pixbuf_get_rowstride (pb);
	pixels = gdk_pixbuf_get_pixels (pb);
	width = gdk_pixbuf_get_width (pb);
	height = gdk_pixbuf_get_height (pb);

	for (bin_size = 250; bin_size > 0; bin_size -= 2) {
		g_autoptr(GHashTable) hash = NULL;
		hash = g_hash_table_new_full (g_direct_hash,  g_direct_equal,
					      NULL, g_free);
		for (y = 0; y < height; y++) {
			for (x = 0; x < width; x++) {
				CdColorRGB8 tmp;
				GsColorBin *s;
				gpointer key;

				/* disregard any with alpha */
				p = pixels + y * rowstride + x * n_channels;
				if (p[3] != 255)
					continue;

				/* find in cache */
				tmp.R = (guint8) (p[0] / bin_size);
				tmp.G = (guint8) (p[1] / bin_size);
				tmp.B = (guint8) (p[2] / bin_size);
				key = GUINT_TO_POINTER (cd_color_rgb8_to_uint32 (&tmp));
				s = g_hash_table_lookup (hash, key);
				if (s != NULL) {
					s->color.red += _convert_from_rgb8 (p[0]);
					s->color.green += _convert_from_rgb8 (p[1]);
					s->color.blue += _convert_from_rgb8 (p[2]);
					s->cnt++;
					continue;
				}

				/* add to hash table */
				s = g_new0 (GsColorBin, 1);
				s->color.red = _convert_from_rgb8 (p[0]);
				s->color.green = _convert_from_rgb8 (p[1]);
				s->color.blue = _convert_from_rgb8 (p[2]);
				s->color.alpha = 1.0;
				s->cnt = 1;
				g_hash_table_insert (hash, key, s);
			}
		}

		number_of_bins = g_hash_table_size (hash);
		if (number_of_bins >= number) {
			g_autoptr(GList) values = NULL;

			/* order by most popular */
			values = g_hash_table_get_values (hash);
			values = g_list_sort (values, gs_color_bin_sort_cb);
			for (GList *l = values; l != NULL; l = l->next) {
				GsColorBin *s = l->data;
				GdkRGBA color;
				color.red = s->color.red / s->cnt;
				color.green = s->color.green / s->cnt;
				color.blue = s->color.blue / s->cnt;
				g_array_append_val (colors, color);
			}
			return;
		}
	}

	/* the algorithm failed, so just return a monochrome ramp */
	for (i = 0; i < 3; i++) {
		GdkRGBA color;
		color.red = (gdouble) i / 3.f;
		color.green = color.red;
		color.blue = color.red;
		color.alpha = 1.0f;
		g_array_append_val (colors, color);
	}
}

/**
 * gs_calculate_key_colors:
 * @pixbuf: an app icon to calculate key colors from
 *
 * Calculate the set of key colors present in @pixbuf. These are the colors
 * which stand out the most, and they are subjective. This function does not
 * guarantee to return perfect results, but should return workable results for
 * most icons.
 *
 * @pixbuf will be scaled down to 32×32 pixels, so if it can be provided at
 * that resolution by the caller, this function will return faster.
 *
 * Returns: (transfer full) (element-type GdkRGBA): key colors for @pixbuf
 * Since: 40
 */
GArray *
gs_calculate_key_colors (GdkPixbuf *pixbuf)
{
	g_autoptr(GdkPixbuf) pb_small = NULL;
	g_autoptr(GArray) colors = g_array_new (FALSE, FALSE, sizeof (GdkRGBA));

	/* scale the icon down to simplify calculations */
	pb_small = gdk_pixbuf_scale_simple (pixbuf, 32, 32, GDK_INTERP_BILINEAR);

	/* get a list of key colors */
	key_colors_set_for_pixbuf (colors, pb_small, 10);

	return g_steal_pointer (&colors);
}
