/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-bg.h>
#include <libgnome-desktop/gnome-desktop-thumbnail.h>

#include "gs-screenshot-image.h"
#include "gs-utils.h"

struct _GsScreenshotImagePrivate
{
	AsScreenshot	*screenshot;
	GtkWidget	*stack;
	GtkWidget	*box_error;
	GtkWidget	*image1;
	GtkWidget	*image2;
	GtkWidget	*label_error;
	SoupSession	*session;
	gchar		*cachedir;
	gchar		*filename;
	const gchar	*current_image;
	guint		 width;
	guint		 height;
	gint		 scale;
};

G_DEFINE_TYPE_WITH_PRIVATE (GsScreenshotImage, gs_screenshot_image, GTK_TYPE_BIN)

/**
 * gs_screenshot_image_get_screenshot:
 **/
AsScreenshot *
gs_screenshot_image_get_screenshot (GsScreenshotImage *ssimg)
{
	GsScreenshotImagePrivate *priv;
	g_return_val_if_fail (GS_IS_SCREENSHOT_IMAGE (ssimg), NULL);
	priv = gs_screenshot_image_get_instance_private (ssimg);
	return priv->screenshot;
}

/**
 * gs_screenshot_image_set_error:
 **/
static void
gs_screenshot_image_set_error (GsScreenshotImage *ssimg, const gchar *message)
{
	GsScreenshotImagePrivate *priv;
	gint width, height;

	priv = gs_screenshot_image_get_instance_private (ssimg);

	gtk_stack_set_visible_child_name (GTK_STACK (priv->stack), "error");
	gtk_label_set_label (GTK_LABEL (priv->label_error), message);
	gtk_widget_get_size_request (priv->stack, &width, &height);
	if (width < 200)
		gtk_widget_hide (priv->label_error);
	else
		gtk_widget_show (priv->label_error);
}

/**
 * gs_screenshot_image_get_desktop_pixbuf:
 **/
static GdkPixbuf *
gs_screenshot_image_get_desktop_pixbuf (GsScreenshotImage *ssimg)
{
	GdkPixbuf *pixbuf;
	GnomeBG *bg;
	GnomeDesktopThumbnailFactory *factory;
	GSettings *settings;
	GsScreenshotImagePrivate *priv;

	priv = gs_screenshot_image_get_instance_private (ssimg);

	factory = gnome_desktop_thumbnail_factory_new (GNOME_DESKTOP_THUMBNAIL_SIZE_LARGE);
	bg = gnome_bg_new ();
	settings = g_settings_new ("org.gnome.desktop.background");
	gnome_bg_load_from_preferences (bg, settings);
	pixbuf = gnome_bg_create_thumbnail (bg, factory, gdk_screen_get_default (), priv->width, priv->height);
	g_object_unref (bg);
	g_object_unref (factory);
	g_object_unref (settings);
	return pixbuf;
}

/**
 * gs_screenshot_image_use_desktop_background:
 **/
static gboolean
gs_screenshot_image_use_desktop_background (GdkPixbuf *pixbuf)
{
	AsImage *im;
	gboolean ret;

	/* nothing to show, means no background mode */
	if (pixbuf == NULL)
		return FALSE;

	/* use a temp AsImage */
	im = as_image_new ();
	as_image_set_pixbuf (im, pixbuf);
	ret = (as_image_get_alpha_flags (im) & AS_IMAGE_ALPHA_FLAG_INTERNAL) > 0;
	g_object_unref (im);
	return ret;
}

/**
 * as_screenshot_show_image:
 **/
static void
as_screenshot_show_image (GsScreenshotImage *ssimg)
{
	GdkPixbuf *pixbuf_bg = NULL;
	GdkPixbuf *pixbuf = NULL;
	GsScreenshotImagePrivate *priv;

	priv = gs_screenshot_image_get_instance_private (ssimg);

	/* no need to composite */
	if (priv->width == G_MAXUINT || priv->height == G_MAXUINT) {
		pixbuf_bg = gdk_pixbuf_new_from_file (priv->filename, NULL);
	} else {
		/* this is always going to have alpha */
		pixbuf = gdk_pixbuf_new_from_file_at_scale (priv->filename,
							    priv->width,
							    priv->height,
							    FALSE, NULL);
		if (pixbuf != NULL) {
			if (gs_screenshot_image_use_desktop_background (pixbuf)) {
				pixbuf_bg = gs_screenshot_image_get_desktop_pixbuf (ssimg);
				if (pixbuf_bg == NULL) {
					pixbuf_bg = g_object_ref (pixbuf);
				} else {
					gdk_pixbuf_composite (pixbuf, pixbuf_bg,
							      0, 0,
							      priv->width, priv->height,
							      0, 0, 1.0f, 1.0f,
							      GDK_INTERP_NEAREST, 255);
				}
			} else {
				pixbuf_bg = g_object_ref (pixbuf);
			}
		}
	}

	/* show icon */
	if (g_strcmp0 (priv->current_image, "image1") == 0) {
		if (pixbuf_bg != NULL) {
			gs_image_set_from_pixbuf_with_scale (GTK_IMAGE (priv->image2),
							     pixbuf_bg, priv->scale);
		}
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack), "image2");
		priv->current_image = "image2";
	} else {
		if (pixbuf_bg != NULL) {
			gs_image_set_from_pixbuf_with_scale (GTK_IMAGE (priv->image1),
							     pixbuf_bg, priv->scale);
		}
		gtk_stack_set_visible_child_name (GTK_STACK (priv->stack), "image1");
		priv->current_image = "image1";
	}

	if (pixbuf != NULL)
		g_object_unref (pixbuf);
	if (pixbuf_bg != NULL)
		g_object_unref (pixbuf_bg);
	gtk_widget_show (GTK_WIDGET (ssimg));
}

/**
 * gs_screenshot_image_complete_cb:
 **/
static void
gs_screenshot_image_complete_cb (SoupSession *session,
				 SoupMessage *msg,
				 gpointer user_data)
{
	AsImage *im = NULL;
	GsScreenshotImagePrivate *priv;
	GsScreenshotImage *ssimg = GS_SCREENSHOT_IMAGE (user_data);
	GError *error = NULL;
	GdkPixbuf *pixbuf = NULL;
	GInputStream *stream = NULL;
	gboolean ret;

	if (msg->status_code != SOUP_STATUS_OK) {
		/* TRANSLATORS: this is when we try to download a screenshot and
		 * we get back 404 */
		gs_screenshot_image_set_error (ssimg, _("Screenshot not found"));
		gtk_widget_hide (GTK_WIDGET (ssimg));
		goto out;
	}

	priv = gs_screenshot_image_get_instance_private (ssimg);

	/* create a buffer with the data */
	stream = g_memory_input_stream_new_from_data (msg->response_body->data,
						      msg->response_body->length,
						      NULL);
	if (stream == NULL)
		goto out;

	/* load the image */
	pixbuf = gdk_pixbuf_new_from_stream (stream, NULL, NULL);
	if (pixbuf == NULL) {
		/* TRANSLATORS: possibly image file corrupt or not an image */
		gs_screenshot_image_set_error (ssimg, _("Failed to load image"));
		goto out;
	}

	/* is image size destination size unknown or exactly the correct size */
	if (priv->width == G_MAXUINT || priv->height == G_MAXUINT ||
	    (priv->width == (guint) gdk_pixbuf_get_width (pixbuf) &&
	     priv->height == (guint) gdk_pixbuf_get_height (pixbuf))) {
		ret = g_file_set_contents (priv->filename,
					   msg->response_body->data,
					   msg->response_body->length,
					   &error);
		if (!ret) {
			gs_screenshot_image_set_error (ssimg, error->message);
			g_error_free (error);
			goto out;
		}
	} else {
		/* save to file, using the same code as the AppStream builder
		 * so the preview looks the same */
		im = as_image_new ();
		as_image_set_pixbuf (im, pixbuf);
		ret = as_image_save_filename (im, priv->filename,
					      priv->width, priv->height,
					      AS_IMAGE_SAVE_FLAG_PAD_16_9, &error);
		if (!ret) {
			gs_screenshot_image_set_error (ssimg, error->message);
			g_error_free (error);
			goto out;
		}
	}

	/* got image, so show */
	as_screenshot_show_image (ssimg);
out:
	if (stream != NULL)
		g_object_unref (stream);
	if (pixbuf != NULL)
		g_object_unref (pixbuf);
	if (im != NULL)
		g_object_unref (im);
	g_object_unref (ssimg);
}

/**
 * gs_screenshot_image_set_cachedir:
 **/
void
gs_screenshot_image_set_cachedir (GsScreenshotImage *ssimg, const gchar *cachedir)
{
	GsScreenshotImagePrivate *priv;
	priv = gs_screenshot_image_get_instance_private (ssimg);
	g_free (priv->cachedir);
	priv->cachedir = g_strdup (cachedir);
}

/**
 * gs_screenshot_image_set_screenshot:
 **/
void
gs_screenshot_image_set_screenshot (GsScreenshotImage *ssimg,
				    AsScreenshot *screenshot)
{
	GsScreenshotImagePrivate *priv;

	g_return_if_fail (GS_IS_SCREENSHOT_IMAGE (ssimg));
	g_return_if_fail (AS_IS_SCREENSHOT (screenshot));

	priv = gs_screenshot_image_get_instance_private (ssimg);

	if (priv->screenshot == screenshot)
		return;
	if (priv->screenshot)
		g_object_unref (priv->screenshot);
	priv->screenshot = g_object_ref (screenshot);
}

/**
 * gs_screenshot_image_set_size:
 **/
void
gs_screenshot_image_set_size (GsScreenshotImage *ssimg,
			      guint width, guint height)
{
	GsScreenshotImagePrivate *priv;

	g_return_if_fail (GS_IS_SCREENSHOT_IMAGE (ssimg));
	g_return_if_fail (width != 0);
	g_return_if_fail (height != 0);

	priv = gs_screenshot_image_get_instance_private (ssimg);
	priv->width = width;
	priv->height = height;
	gtk_widget_set_size_request (priv->stack, width, height);
}

/**
 * gs_screenshot_image_load_async:
 **/
void
gs_screenshot_image_load_async (GsScreenshotImage *ssimg,
				GCancellable *cancellable)
{
	AsImage *im = NULL;
	GsScreenshotImagePrivate *priv;
	SoupMessage *msg = NULL;
	SoupURI *base_uri = NULL;
	const gchar *url;
	gchar *basename = NULL;
	gchar *cachedir = NULL;
	gchar *sizedir = NULL;
	gint rc;

	g_return_if_fail (GS_IS_SCREENSHOT_IMAGE (ssimg));

	priv = gs_screenshot_image_get_instance_private (ssimg);

	g_return_if_fail (AS_IS_SCREENSHOT (priv->screenshot));
	g_return_if_fail (priv->width != 0);
	g_return_if_fail (priv->height != 0);

	/* load the HiDPI image if it exists */
	priv->scale = 1;
	if (gtk_widget_get_scale_factor (GTK_WIDGET (ssimg)) > 1) {
		im = as_screenshot_get_image (priv->screenshot,
					      priv->width * 2,
					      priv->height * 2);
		if (im != NULL)
			priv->scale *= 2;
	}

	/* fallback to LoDPI if it exists */
	if (im == NULL) {
		im = as_screenshot_get_image (priv->screenshot,
					      priv->width,
					      priv->height);
	}
	if (im == NULL) {
		/* TRANSLATORS: this is when we request a screenshot size that
		 * the generator did not create or the parser did not add */
		gs_screenshot_image_set_error (ssimg, _("Screenshot size not found"));
		goto out;
	}
	url = as_image_get_url (im);
	basename = g_path_get_basename (url);
	if (priv->width == G_MAXUINT || priv->height == G_MAXUINT) {
		sizedir = g_strdup ("unknown");
	} else {
		sizedir = g_strdup_printf ("%ux%u", priv->width, priv->height);
	}
	cachedir = g_build_filename (priv->cachedir,
				     "gnome-software",
				     "screenshots",
				     sizedir,
				     NULL);
	rc = g_mkdir_with_parents (cachedir, 0700);
	if (rc != 0) {
		/* TRANSLATORS: this is when we try create the cache directory
		 * but we were out of space or permission was denied */
		gs_screenshot_image_set_error (ssimg, _("Could not create cache"));
		goto out;
	}

	/* does local file already exist */
	priv->filename = g_build_filename (cachedir, basename, NULL);
	if (g_file_test (priv->filename, G_FILE_TEST_EXISTS)) {
		as_screenshot_show_image (ssimg);
		goto out;
	}

	/* download file */
	g_debug ("downloading %s to %s", url, priv->filename);
	base_uri = soup_uri_new (url);
	if (base_uri == NULL) {
		/* TRANSLATORS: this is when we try to download a screenshot
		 * that was not a valid URL */
		gs_screenshot_image_set_error (ssimg, _("Screenshot not valid"));
		goto out;
	}
	msg = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
	if (msg == NULL) {
		/* TRANSLATORS: this is when networking is not available */
		gs_screenshot_image_set_error (ssimg, _("Screenshot not available"));
		goto out;
	}

	/* send async */
	soup_session_queue_message (priv->session, msg,
				    gs_screenshot_image_complete_cb,
				    g_object_ref (ssimg));
out:
	g_free (basename);
	g_free (sizedir);
	g_free (cachedir);
	if (base_uri != NULL)
		soup_uri_free (base_uri);
}

/**
 * gs_screenshot_image_destroy:
 **/
static void
gs_screenshot_image_destroy (GtkWidget *widget)
{
	GsScreenshotImage *ssimg = GS_SCREENSHOT_IMAGE (widget);
	GsScreenshotImagePrivate *priv;

	priv = gs_screenshot_image_get_instance_private (ssimg);

	g_clear_object (&priv->screenshot);
	g_free (priv->cachedir);
	priv->cachedir = NULL;
	g_free (priv->filename);
	priv->filename = NULL;
	g_clear_object (&priv->session);

	GTK_WIDGET_CLASS (gs_screenshot_image_parent_class)->destroy (widget);
}

/**
 * gs_screenshot_image_init:
 **/
static void
gs_screenshot_image_init (GsScreenshotImage *ssimg)
{
	AtkObject *accessible;

	ssimg->priv = gs_screenshot_image_get_instance_private (ssimg);

	gtk_widget_set_has_window (GTK_WIDGET (ssimg), FALSE);
	gtk_widget_init_template (GTK_WIDGET (ssimg));
	gtk_widget_add_events (ssimg->priv->stack, GDK_BUTTON_PRESS_MASK |
	                                           GDK_BUTTON_RELEASE_MASK |
	                                           GDK_TOUCH_MASK |
	                                           GDK_ENTER_NOTIFY_MASK |
	                                           GDK_LEAVE_NOTIFY_MASK);

	accessible = gtk_widget_get_accessible (GTK_WIDGET (ssimg));
	if (accessible != 0) {
		atk_object_set_role (accessible, ATK_ROLE_IMAGE);
		atk_object_set_name (accessible, _("Screenshot"));
	}
}

/**
 * gs_screenshot_image_draw:
 **/
static gboolean
gs_screenshot_image_draw (GtkWidget *widget, cairo_t *cr)
{
	GtkStyleContext *context;

	context = gtk_widget_get_style_context (widget);
	gtk_render_background (context, cr,
			       0, 0,
			       gtk_widget_get_allocated_width (widget),
			       gtk_widget_get_allocated_height (widget));
	gtk_render_frame (context, cr,
			  0, 0,
			  gtk_widget_get_allocated_width (widget),
			  gtk_widget_get_allocated_height (widget));

	return GTK_WIDGET_CLASS (gs_screenshot_image_parent_class)->draw (widget, cr);
}

/**
 * gs_screenshot_image_class_init:
 **/
static void
gs_screenshot_image_class_init (GsScreenshotImageClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	widget_class->destroy = gs_screenshot_image_destroy;
	widget_class->draw = gs_screenshot_image_draw;

	gtk_widget_class_set_template_from_resource (widget_class,
						     "/org/gnome/Software/screenshot-image.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsScreenshotImage, stack);
	gtk_widget_class_bind_template_child_private (widget_class, GsScreenshotImage, image1);
	gtk_widget_class_bind_template_child_private (widget_class, GsScreenshotImage, image2);
	gtk_widget_class_bind_template_child_private (widget_class, GsScreenshotImage, box_error);
	gtk_widget_class_bind_template_child_private (widget_class, GsScreenshotImage, label_error);
}

/**
 * gs_screenshot_image_new:
 **/
GtkWidget *
gs_screenshot_image_new (SoupSession *session)
{
	GsScreenshotImage *ssimg;
	ssimg = g_object_new (GS_TYPE_SCREENSHOT_IMAGE, NULL);
	ssimg->priv->session = g_object_ref (session);
	return GTK_WIDGET (ssimg);
}

/* vim: set noexpandtab: */
