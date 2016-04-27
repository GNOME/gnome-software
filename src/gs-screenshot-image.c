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

struct _GsScreenshotImage
{
	GtkBin		 parent_instance;

	AsScreenshot	*screenshot;
	GtkWidget	*stack;
	GtkWidget	*box_error;
	GtkWidget	*image1;
	GtkWidget	*image2;
	GtkWidget	*label_error;
	SoupSession	*session;
	SoupMessage	*message;
	gchar		*filename;
	const gchar	*current_image;
	gboolean	 use_desktop_background;
	guint		 width;
	guint		 height;
	gint		 scale;
};

G_DEFINE_TYPE (GsScreenshotImage, gs_screenshot_image, GTK_TYPE_BIN)

/**
 * gs_screenshot_image_get_screenshot:
 **/
AsScreenshot *
gs_screenshot_image_get_screenshot (GsScreenshotImage *ssimg)
{
	g_return_val_if_fail (GS_IS_SCREENSHOT_IMAGE (ssimg), NULL);
	return ssimg->screenshot;
}

/**
 * gs_screenshot_image_set_error:
 **/
static void
gs_screenshot_image_set_error (GsScreenshotImage *ssimg, const gchar *message)
{
	gint width, height;

	gtk_stack_set_visible_child_name (GTK_STACK (ssimg->stack), "error");
	gtk_label_set_label (GTK_LABEL (ssimg->label_error), message);
	gtk_widget_get_size_request (ssimg->stack, &width, &height);
	if (width < 200)
		gtk_widget_hide (ssimg->label_error);
	else
		gtk_widget_show (ssimg->label_error);
}

/**
 * gs_screenshot_image_get_desktop_pixbuf:
 **/
static GdkPixbuf *
gs_screenshot_image_get_desktop_pixbuf (GsScreenshotImage *ssimg)
{
	g_autoptr(GnomeBG) bg = NULL;
	g_autoptr(GnomeDesktopThumbnailFactory) factory = NULL;
	g_autoptr(GSettings) settings = NULL;

	factory = gnome_desktop_thumbnail_factory_new (GNOME_DESKTOP_THUMBNAIL_SIZE_LARGE);
	bg = gnome_bg_new ();
	settings = g_settings_new ("org.gnome.desktop.background");
	gnome_bg_load_from_preferences (bg, settings);
	return gnome_bg_create_thumbnail (bg, factory,
					  gdk_screen_get_default (),
					  ssimg->width, ssimg->height);
}

/**
 * gs_screenshot_image_use_desktop_background:
 **/
static gboolean
gs_screenshot_image_use_desktop_background (GsScreenshotImage *ssimg, GdkPixbuf *pixbuf)
{
	g_autoptr(AsImage) im = NULL;

	/* nothing to show, means no background mode */
	if (pixbuf == NULL)
		return FALSE;
	/* background mode explicitly disabled */
	if (!ssimg->use_desktop_background)
		return FALSE;

	/* use a temp AsImage */
	im = as_image_new ();
	as_image_set_pixbuf (im, pixbuf);
	return (as_image_get_alpha_flags (im) & AS_IMAGE_ALPHA_FLAG_INTERNAL) > 0;
}

/**
 * as_screenshot_show_image:
 **/
static void
as_screenshot_show_image (GsScreenshotImage *ssimg)
{
	g_autoptr(GdkPixbuf) pixbuf_bg = NULL;
	g_autoptr(GdkPixbuf) pixbuf = NULL;

	/* no need to composite */
	if (ssimg->width == G_MAXUINT || ssimg->height == G_MAXUINT) {
		pixbuf_bg = gdk_pixbuf_new_from_file (ssimg->filename, NULL);
	} else {
		/* this is always going to have alpha */
		pixbuf = gdk_pixbuf_new_from_file_at_scale (ssimg->filename,
							    ssimg->width * ssimg->scale,
							    ssimg->height * ssimg->scale,
							    FALSE, NULL);
		if (pixbuf != NULL) {
			if (gs_screenshot_image_use_desktop_background (ssimg, pixbuf)) {
				pixbuf_bg = gs_screenshot_image_get_desktop_pixbuf (ssimg);
				if (pixbuf_bg == NULL) {
					pixbuf_bg = g_object_ref (pixbuf);
				} else {
					gdk_pixbuf_composite (pixbuf, pixbuf_bg,
							      0, 0,
							      ssimg->width, ssimg->height,
							      0, 0, 1.0f, 1.0f,
							      GDK_INTERP_NEAREST, 255);
				}
			} else {
				pixbuf_bg = g_object_ref (pixbuf);
			}
		}
	}

	/* show icon */
	if (g_strcmp0 (ssimg->current_image, "image1") == 0) {
		if (pixbuf_bg != NULL) {
			gs_image_set_from_pixbuf_with_scale (GTK_IMAGE (ssimg->image2),
							     pixbuf_bg, ssimg->scale);
		}
		gtk_stack_set_visible_child_name (GTK_STACK (ssimg->stack), "image2");
		ssimg->current_image = "image2";
	} else {
		if (pixbuf_bg != NULL) {
			gs_image_set_from_pixbuf_with_scale (GTK_IMAGE (ssimg->image1),
							     pixbuf_bg, ssimg->scale);
		}
		gtk_stack_set_visible_child_name (GTK_STACK (ssimg->stack), "image1");
		ssimg->current_image = "image1";
	}

	gtk_widget_show (GTK_WIDGET (ssimg));
}

/**
 * gs_screenshot_image_show_blurred:
 **/
static void
gs_screenshot_image_show_blurred (GsScreenshotImage *ssimg,
				  const gchar *filename_thumb)
{
	g_autoptr(AsImage) im = NULL;
	g_autoptr(GdkPixbuf) pb = NULL;

	/* create an helper which can do the blurring for us */
	im = as_image_new ();
	if (!as_image_load_filename (im, filename_thumb, NULL))
		return;
	pb = as_image_save_pixbuf (im,
				   ssimg->width * ssimg->scale,
				   ssimg->height * ssimg->scale,
				   AS_IMAGE_SAVE_FLAG_BLUR);
	if (pb == NULL)
		return;

	if (g_strcmp0 (ssimg->current_image, "image1") == 0) {
		gs_image_set_from_pixbuf_with_scale (GTK_IMAGE (ssimg->image1),
						     pb, ssimg->scale);
	} else {
		gs_image_set_from_pixbuf_with_scale (GTK_IMAGE (ssimg->image2),
						     pb, ssimg->scale);
	}
}

/**
 * gs_screenshot_image_complete_cb:
 **/
static void
gs_screenshot_image_complete_cb (SoupSession *session,
				 SoupMessage *msg,
				 gpointer user_data)
{
	g_autoptr(GsScreenshotImage) ssimg = GS_SCREENSHOT_IMAGE (user_data);
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(AsImage) im = NULL;
	g_autoptr(GdkPixbuf) pixbuf = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* return immediately if the message was cancelled or if we're in destruction */
	if (msg->status_code == SOUP_STATUS_CANCELLED || ssimg->session == NULL)
		return;

	if (msg->status_code != SOUP_STATUS_OK) {
		/* TRANSLATORS: this is when we try to download a screenshot and
		 * we get back 404 */
		gs_screenshot_image_set_error (ssimg, _("Screenshot not found"));
		gtk_widget_hide (GTK_WIDGET (ssimg));
		return;
	}

	/* create a buffer with the data */
	stream = g_memory_input_stream_new_from_data (msg->response_body->data,
						      msg->response_body->length,
						      NULL);
	if (stream == NULL)
		return;

	/* load the image */
	pixbuf = gdk_pixbuf_new_from_stream (stream, NULL, NULL);
	if (pixbuf == NULL) {
		/* TRANSLATORS: possibly image file corrupt or not an image */
		gs_screenshot_image_set_error (ssimg, _("Failed to load image"));
		return;
	}

	/* is image size destination size unknown or exactly the correct size */
	if (ssimg->width == G_MAXUINT || ssimg->height == G_MAXUINT ||
	    (ssimg->width * ssimg->scale == (guint) gdk_pixbuf_get_width (pixbuf) &&
	     ssimg->height * ssimg->scale == (guint) gdk_pixbuf_get_height (pixbuf))) {
		ret = g_file_set_contents (ssimg->filename,
					   msg->response_body->data,
					   msg->response_body->length,
					   &error);
		if (!ret) {
			gs_screenshot_image_set_error (ssimg, error->message);
			return;
		}
	} else {
		/* save to file, using the same code as the AppStream builder
		 * so the preview looks the same */
		im = as_image_new ();
		as_image_set_pixbuf (im, pixbuf);
		ret = as_image_save_filename (im, ssimg->filename,
					      ssimg->width * ssimg->scale,
					      ssimg->height * ssimg->scale,
					      AS_IMAGE_SAVE_FLAG_PAD_16_9, &error);
		if (!ret) {
			gs_screenshot_image_set_error (ssimg, error->message);
			return;
		}
	}

	/* got image, so show */
	as_screenshot_show_image (ssimg);
}

/**
 * gs_screenshot_image_set_screenshot:
 **/
void
gs_screenshot_image_set_screenshot (GsScreenshotImage *ssimg,
				    AsScreenshot *screenshot)
{
	g_return_if_fail (GS_IS_SCREENSHOT_IMAGE (ssimg));
	g_return_if_fail (AS_IS_SCREENSHOT (screenshot));

	if (ssimg->screenshot == screenshot)
		return;
	if (ssimg->screenshot)
		g_object_unref (ssimg->screenshot);
	ssimg->screenshot = g_object_ref (screenshot);
}

/**
 * gs_screenshot_image_set_size:
 **/
void
gs_screenshot_image_set_size (GsScreenshotImage *ssimg,
			      guint width, guint height)
{
	g_return_if_fail (GS_IS_SCREENSHOT_IMAGE (ssimg));
	g_return_if_fail (width != 0);
	g_return_if_fail (height != 0);

	ssimg->width = width;
	ssimg->height = height;
	gtk_widget_set_size_request (ssimg->stack, width, height);
}

/**
 * gs_screenshot_image_set_use_desktop_background:
 **/
void
gs_screenshot_image_set_use_desktop_background (GsScreenshotImage *ssimg,
                                                gboolean use_desktop_background)
{
	g_return_if_fail (GS_IS_SCREENSHOT_IMAGE (ssimg));
	ssimg->use_desktop_background = use_desktop_background;
}

/**
 * gs_screenshot_get_cachefn_for_url:
 **/
static gchar *
gs_screenshot_get_cachefn_for_url (const gchar *url)
{
	g_autofree gchar *basename = NULL;
	g_autofree gchar *checksum = NULL;
	checksum = g_compute_checksum_for_string (G_CHECKSUM_SHA256, url, -1);
	basename = g_path_get_basename (url);
	return g_strdup_printf ("%s-%s", checksum, basename);
}

/**
 * gs_screenshot_image_load_async:
 **/
void
gs_screenshot_image_load_async (GsScreenshotImage *ssimg,
				GCancellable *cancellable)
{
	AsImage *im = NULL;
	SoupURI *base_uri = NULL;
	const gchar *url;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *cache_kind = NULL;
	g_autofree gchar *cachefn_thumb = NULL;
	g_autofree gchar *sizedir = NULL;

	g_return_if_fail (GS_IS_SCREENSHOT_IMAGE (ssimg));

	g_return_if_fail (AS_IS_SCREENSHOT (ssimg->screenshot));
	g_return_if_fail (ssimg->width != 0);
	g_return_if_fail (ssimg->height != 0);

	/* load an image according to the scale factor */
	ssimg->scale = gtk_widget_get_scale_factor (GTK_WIDGET (ssimg));
	im = as_screenshot_get_image (ssimg->screenshot,
				      ssimg->width * ssimg->scale,
				      ssimg->height * ssimg->scale);

	/* if we've failed to load a HiDPI image, fallback to LoDPI */
	if (im == NULL && ssimg->scale > 1) {
		ssimg->scale = 1;
		im = as_screenshot_get_image (ssimg->screenshot,
					      ssimg->width,
					      ssimg->height);
	}
	if (im == NULL) {
		/* TRANSLATORS: this is when we request a screenshot size that
		 * the generator did not create or the parser did not add */
		gs_screenshot_image_set_error (ssimg, _("Screenshot size not found"));
		return;
	}

	/* check if the URL points to a local file */
	url = as_image_get_url (im);
	if (g_str_has_prefix (url, "file://")) {
		ssimg->filename = g_strdup (url + 7);
		if (g_file_test (ssimg->filename, G_FILE_TEST_EXISTS)) {
			as_screenshot_show_image (ssimg);
			return;
		}
	}

	basename = gs_screenshot_get_cachefn_for_url (url);
	if (ssimg->width == G_MAXUINT || ssimg->height == G_MAXUINT) {
		sizedir = g_strdup ("unknown");
	} else {
		sizedir = g_strdup_printf ("%ux%u", ssimg->width * ssimg->scale, ssimg->height * ssimg->scale);
	}
	cache_kind = g_build_filename ("screenshots", sizedir, NULL);
	ssimg->filename = gs_utils_get_cache_filename (cache_kind,
						       basename,
						       GS_UTILS_CACHE_FLAG_NONE,
						       NULL);
	if (ssimg->filename == NULL) {
		/* TRANSLATORS: this is when we try create the cache directory
		 * but we were out of space or permission was denied */
		gs_screenshot_image_set_error (ssimg, _("Could not create cache"));
		return;
	}

	/* does local file already exist */
	if (g_file_test (ssimg->filename, G_FILE_TEST_EXISTS)) {
		as_screenshot_show_image (ssimg);
		return;
	}

	/* can we load a blurred smaller version of this straight away */
	if (ssimg->width > AS_IMAGE_THUMBNAIL_WIDTH &&
	    ssimg->height > AS_IMAGE_THUMBNAIL_HEIGHT) {
		g_autofree gchar *cache_kind_thumb = NULL;
		cache_kind_thumb = g_build_filename ("screenshots", "112x63", NULL);
		cachefn_thumb = gs_utils_get_cache_filename (cache_kind_thumb,
							     basename,
							     GS_UTILS_CACHE_FLAG_NONE,
							     NULL);
		if (cachefn_thumb == NULL)
			return;
		if (g_file_test (cachefn_thumb, G_FILE_TEST_EXISTS))
			gs_screenshot_image_show_blurred (ssimg, cachefn_thumb);
	}

	/* re-request the cache filename, which might be different as it needs
	 * to be writable this time */
	g_free (ssimg->filename);
	ssimg->filename = gs_utils_get_cache_filename (cache_kind,
						       basename,
						       GS_UTILS_CACHE_FLAG_WRITEABLE,
						       NULL);

	/* download file */
	g_debug ("downloading %s to %s", url, ssimg->filename);
	base_uri = soup_uri_new (url);
	if (base_uri == NULL || !SOUP_URI_VALID_FOR_HTTP (base_uri)) {
		/* TRANSLATORS: this is when we try to download a screenshot
		 * that was not a valid URL */
		gs_screenshot_image_set_error (ssimg, _("Screenshot not valid"));
		soup_uri_free (base_uri);
		return;
	}

	/* cancel any previous messages */
	if (ssimg->message != NULL) {
		soup_session_cancel_message (ssimg->session,
		                             ssimg->message,
		                             SOUP_STATUS_CANCELLED);
		g_clear_object (&ssimg->message);
	}

	ssimg->message = soup_message_new_from_uri (SOUP_METHOD_GET, base_uri);
	if (ssimg->message == NULL) {
		/* TRANSLATORS: this is when networking is not available */
		gs_screenshot_image_set_error (ssimg, _("Screenshot not available"));
		soup_uri_free (base_uri);
		return;
	}

	/* send async */
	soup_session_queue_message (ssimg->session,
				    g_object_ref (ssimg->message) /* transfer full */,
				    gs_screenshot_image_complete_cb,
				    g_object_ref (ssimg));
	soup_uri_free (base_uri);
}

/**
 * gs_screenshot_image_destroy:
 **/
static void
gs_screenshot_image_destroy (GtkWidget *widget)
{
	GsScreenshotImage *ssimg = GS_SCREENSHOT_IMAGE (widget);

	if (ssimg->message != NULL) {
		soup_session_cancel_message (ssimg->session,
		                             ssimg->message,
		                             SOUP_STATUS_CANCELLED);
		g_clear_object (&ssimg->message);
	}
	g_clear_object (&ssimg->screenshot);
	g_clear_object (&ssimg->session);

	g_clear_pointer (&ssimg->filename, g_free);

	GTK_WIDGET_CLASS (gs_screenshot_image_parent_class)->destroy (widget);
}

/**
 * gs_screenshot_image_init:
 **/
static void
gs_screenshot_image_init (GsScreenshotImage *ssimg)
{
	AtkObject *accessible;

	ssimg->use_desktop_background = TRUE;

	gtk_widget_set_has_window (GTK_WIDGET (ssimg), FALSE);
	gtk_widget_init_template (GTK_WIDGET (ssimg));

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
						     "/org/gnome/Software/gs-screenshot-image.ui");

	gtk_widget_class_bind_template_child (widget_class, GsScreenshotImage, stack);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotImage, image1);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotImage, image2);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotImage, box_error);
	gtk_widget_class_bind_template_child (widget_class, GsScreenshotImage, label_error);
}

/**
 * gs_screenshot_image_new:
 **/
GtkWidget *
gs_screenshot_image_new (SoupSession *session)
{
	GsScreenshotImage *ssimg;
	ssimg = g_object_new (GS_TYPE_SCREENSHOT_IMAGE, NULL);
	ssimg->session = g_object_ref (session);
	return GTK_WIDGET (ssimg);
}

/* vim: set noexpandtab: */
