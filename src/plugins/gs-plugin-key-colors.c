/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
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

#include <config.h>

#include <gnome-software.h>

void
gs_plugin_initialize (GsPlugin *plugin)
{
	/* need icon */
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "icons");
}

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

static void
gs_plugin_key_colors_set_for_pixbuf (GsApp *app, GdkPixbuf *pb, guint number)
{
	GList *l;
	gint rowstride, n_channels;
	gint x, y;
	guchar *pixels, *p;
	guint bin_size = 200;
	guint i;
	guint number_of_bins;
	g_autoptr(AsImage) im = NULL;

	/* go through each pixel */
	n_channels = gdk_pixbuf_get_n_channels (pb);
	rowstride = gdk_pixbuf_get_rowstride (pb);
	pixels = gdk_pixbuf_get_pixels (pb);
	for (bin_size = 250; bin_size > 0; bin_size -= 2) {
		g_autoptr(GHashTable) hash = NULL;
		hash = g_hash_table_new_full (g_direct_hash,  g_direct_equal,
					      NULL, g_free);
		for (y = 0; y < gdk_pixbuf_get_height (pb); y++) {
			for (x = 0; x < gdk_pixbuf_get_width (pb); x++) {
				CdColorRGB8 tmp;
				GsColorBin *s;
				gpointer key;

				/* disregard any with alpha */
				p = pixels + y * rowstride + x * n_channels;
				if (p[3] != 255)
					continue;

				/* find in cache */
				tmp.R = p[0] / bin_size;
				tmp.G = p[1] / bin_size;
				tmp.B = p[2] / bin_size;
				key = GUINT_TO_POINTER (cd_color_rgb8_to_uint32 (&tmp));
				s = g_hash_table_lookup (hash, key);
				if (s != NULL) {
					s->color.red += p[0];
					s->color.green += p[1];
					s->color.blue += p[2];
					s->cnt++;
					continue;
				}

				/* add to hash table */
				s = g_new0 (GsColorBin, 1);
				s->color.red = p[0];
				s->color.green = p[1];
				s->color.blue = p[2];
				s->color.alpha = 1.0;
				s->cnt = 1;
				g_hash_table_insert (hash, key, s);
			}
		}

		number_of_bins = g_hash_table_size (hash);
//		g_debug ("number of colors: %i", number_of_bins);
		if (number_of_bins >= number) {
			g_autoptr(GList) values = NULL;

			/* order by most popular */
			values = g_hash_table_get_values (hash);
			values = g_list_sort (values, gs_color_bin_sort_cb);
			for (l = values; l != NULL; l = l->next) {
				GsColorBin *s = l->data;
				g_autofree GdkRGBA *color = g_new0 (GdkRGBA, 1);
				color->red = s->color.red / s->cnt;
				color->green = s->color.green / s->cnt;
				color->blue = s->color.blue / s->cnt;
				gs_app_add_key_color (app, color);
			}
			return;
		}
	}

	/* the algorithm failed, so just return a monochrome ramp */
	for (i = 0; i < 3; i++) {
		g_autofree GdkRGBA *color = g_new0 (GdkRGBA, 1);
		color->red = 255 * i / 3;
		color->green = color->red;
		color->blue = color->red;
		color->alpha = 1.0f;
		gs_app_add_key_color (app, color);
	}
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
		      GsApp *app,
		      GsPluginRefineFlags flags,
		      GCancellable *cancellable,
		      GError **error)
{
	GdkPixbuf *pb;
	g_autoptr(GdkPixbuf) pb_small = NULL;

	/* add a rating */
	if ((flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_KEY_COLORS) == 0)
		return TRUE;

	/* already set */
	if (gs_app_get_key_colors(app)->len > 0)
		return TRUE;

	/* no pixbuf */
	pb = gs_app_get_pixbuf (app);
	if (pb == NULL)
		return TRUE;

	/* get a list of key colors */
	pb_small = gdk_pixbuf_scale_simple (pb, 32, 32, GDK_INTERP_BILINEAR);
	gs_plugin_key_colors_set_for_pixbuf (app, pb_small, 10);
	return TRUE;
}
