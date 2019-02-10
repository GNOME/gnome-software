/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>

#ifdef HAVE_GNOME_DESKTOP
#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-bg.h>
#include <libgnome-desktop/gnome-desktop-thumbnail.h>
#endif

#include "gs-screenshot-image.h"
#include "gs-common.h"

struct _GsScreenshotImage
{
	GtkBin		 parent_instance;

	AsScreenshot	*screenshot;
	GtkWidget	*stack;
	GtkWidget	*box_error;
	GtkWidget	*image1;
	GtkWidget	*image2;
	GtkWidget	*label_error;
	GSettings	*settings;
	SoupSession	*session;
	SoupMessage	*message;
	gchar		*filename;
	const gchar	*current_image;
	gboolean	 use_desktop_background;
	guint		 width;
	guint		 height;
	guint		 scale;
	gboolean	 showing_image;
};

G_DEFINE_TYPE (GsScreenshotImage, gs_screenshot_image, GTK_TYPE_BIN)

AsScreenshot *
gs_screenshot_image_get_screenshot (GsScreenshotImage *ssimg)
{
	g_return_val_if_fail (GS_IS_SCREENSHOT_IMAGE (ssimg), NULL);
	return ssimg->screenshot;
}

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
	ssimg->showing_image = FALSE;
}

static GdkPixbuf *
gs_screenshot_image_get_desktop_pixbuf (GsScreenshotImage *ssimg)
{
#ifdef HAVE_GNOME_DESKTOP
	g_autoptr(GnomeBG) bg = NULL;
	g_autoptr(GnomeDesktopThumbnailFactory) factory = NULL;
	g_autoptr(GSettings) settings = NULL;

	factory = gnome_desktop_thumbnail_factory_new (GNOME_DESKTOP_THUMBNAIL_SIZE_LARGE);
	bg = gnome_bg_new ();
	settings = g_settings_new ("org.gnome.desktop.background");
	gnome_bg_load_from_preferences (bg, settings);
	return gnome_bg_create_thumbnail (bg, factory,
					  gdk_screen_get_default (),
					  (gint) ssimg->width,
					  (gint) ssimg->height);
#else
	return NULL;
#endif
}

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
							    (gint) (ssimg->width * ssimg->scale),
							    (gint) (ssimg->height * ssimg->scale),
							    FALSE, NULL);
		if (pixbuf != NULL) {
			if (gs_screenshot_image_use_desktop_background (ssimg, pixbuf)) {
				pixbuf_bg = gs_screenshot_image_get_desktop_pixbuf (ssimg);
				if (pixbuf_bg == NULL) {
					pixbuf_bg = g_object_ref (pixbuf);
				} else {
					gdk_pixbuf_composite (pixbuf, pixbuf_bg,
							      0, 0,
							      (gint) ssimg->width,
							      (gint) ssimg->height,
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
							     pixbuf_bg, (gint) ssimg->scale);
		}
		gtk_stack_set_visible_child_name (GTK_STACK (ssimg->stack), "image2");
		ssimg->current_image = "image2";
	} else {
		if (pixbuf_bg != NULL) {
			gs_image_set_from_pixbuf_with_scale (GTK_IMAGE (ssimg->image1),
							     pixbuf_bg, (gint) ssimg->scale);
		}
		gtk_stack_set_visible_child_name (GTK_STACK (ssimg->stack), "image1");
		ssimg->current_image = "image1";
	}

	gtk_widget_show (GTK_WIDGET (ssimg));
	ssimg->showing_image = TRUE;
}

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
						     pb, (gint) ssimg->scale);
	} else {
		gs_image_set_from_pixbuf_with_scale (GTK_IMAGE (ssimg->image2),
						     pb, (gint) ssimg->scale);
	}
}

static gboolean
gs_screenshot_image_save_downloaded_img (GsScreenshotImage *ssimg,
					 GdkPixbuf *pixbuf,
					 GError **error)
{
	g_autoptr(AsImage) im = NULL;
	gboolean ret;
	const GPtrArray *images;
	g_autoptr(GError) local_error = NULL;
	g_autofree char *filename = NULL;
	g_autofree char *size_dir = NULL;
	g_autofree char *cache_kind = NULL;
	g_autofree char *basename = NULL;
	guint width = ssimg->width;
	guint height = ssimg->height;

	/* save to file, using the same code as the AppStream builder
	 * so the preview looks the same */
	im = as_image_new ();
	as_image_set_pixbuf (im, pixbuf);
	ret = as_image_save_filename (im, ssimg->filename,
				      ssimg->width * ssimg->scale,
				      ssimg->height * ssimg->scale,
				      AS_IMAGE_SAVE_FLAG_PAD_16_9,
				      error);

	if (!ret)
		return FALSE;

	if (ssimg->screenshot == NULL)
		return TRUE;

	images = as_screenshot_get_images (ssimg->screenshot);
	if (images->len > 1)
		return TRUE;

	if (width == AS_IMAGE_THUMBNAIL_WIDTH &&
	    height == AS_IMAGE_THUMBNAIL_HEIGHT) {
		width = AS_IMAGE_NORMAL_WIDTH;
		height = AS_IMAGE_NORMAL_HEIGHT;
	} else {
		width = AS_IMAGE_THUMBNAIL_WIDTH;
		height = AS_IMAGE_THUMBNAIL_HEIGHT;
	}

	width *= ssimg->scale;
	height *= ssimg->scale;
	basename = g_path_get_basename (ssimg->filename);
	size_dir = g_strdup_printf ("%ux%u", width, height);
	cache_kind = g_build_filename ("screenshots", size_dir, NULL);
	filename = gs_utils_get_cache_filename (cache_kind, basename,
						GS_UTILS_CACHE_FLAG_WRITEABLE,
						&local_error);

        if (filename == NULL) {
		/* if we cannot get a cache filename, warn about that but do not
		 * set a user's visible error because this is a complementary
		 * operation */
                g_warning ("Failed to get cache filename for counterpart "
                           "screenshot '%s' in folder '%s': %s", basename,
                           cache_kind, local_error->message);
                return TRUE;
        }

	ret = as_image_save_filename (im, filename, width, height,
				      AS_IMAGE_SAVE_FLAG_PAD_16_9,
				      &local_error);

	if (!ret) {
		/* if we cannot save this screenshot, warn about that but do not
		 * set a user's visible error because this is a complementary
		 * operation */
                g_warning ("Failed to save screenshot '%s': %s", filename,
                           local_error->message);
        }

	return TRUE;
}

static void
gs_screenshot_image_complete_cb (SoupSession *session,
				 SoupMessage *msg,
				 gpointer user_data)
{
	g_autoptr(GsScreenshotImage) ssimg = GS_SCREENSHOT_IMAGE (user_data);
	gboolean ret;
	g_autoptr(GError) error = NULL;
	g_autoptr(GdkPixbuf) pixbuf = NULL;
	g_autoptr(GInputStream) stream = NULL;

	/* return immediately if the message was cancelled or if we're in destruction */
	if (msg->status_code == SOUP_STATUS_CANCELLED || ssimg->session == NULL)
		return;

	if (msg->status_code == SOUP_STATUS_NOT_MODIFIED) {
		g_debug ("screenshot has not been modified");
		as_screenshot_show_image (ssimg);
		return;
	}
	if (msg->status_code != SOUP_STATUS_OK) {
                g_warning ("Result of screenshot downloading attempt with "
			   "status code '%u': %s", msg->status_code,
			   msg->reason_phrase);
		/* if we're already showing an image, then don't set the error
		 * as having an image (even if outdated) is better */
		if (ssimg->showing_image)
			return;
		/* TRANSLATORS: this is when we try to download a screenshot and
		 * we get back 404 */
		gs_screenshot_image_set_error (ssimg, _("Screenshot not found"));
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
	} else if (!gs_screenshot_image_save_downloaded_img (ssimg, pixbuf,
							     &error)) {
		gs_screenshot_image_set_error (ssimg, error->message);
		return;
	}

	/* got image, so show */
	as_screenshot_show_image (ssimg);
}

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

	/* we reset this flag here too because it referred to the previous
	 * screenshot, and thus avoids potentially assuming that the new
	 * screenshot is shown when it is the previous one instead */
	ssimg->showing_image = FALSE;
}

void
gs_screenshot_image_set_size (GsScreenshotImage *ssimg,
			      guint width, guint height)
{
	g_return_if_fail (GS_IS_SCREENSHOT_IMAGE (ssimg));
	g_return_if_fail (width != 0);
	g_return_if_fail (height != 0);

	ssimg->width = width;
	ssimg->height = height;
	gtk_widget_set_size_request (ssimg->stack, (gint) width, (gint) height);
}

void
gs_screenshot_image_set_use_desktop_background (GsScreenshotImage *ssimg,
                                                gboolean use_desktop_background)
{
	g_return_if_fail (GS_IS_SCREENSHOT_IMAGE (ssimg));
	ssimg->use_desktop_background = use_desktop_background;
}

static gchar *
gs_screenshot_get_cachefn_for_url (const gchar *url)
{
	g_autofree gchar *basename = NULL;
	g_autofree gchar *checksum = NULL;
	checksum = g_compute_checksum_for_string (G_CHECKSUM_SHA256, url, -1);
	basename = g_path_get_basename (url);
	return g_strdup_printf ("%s-%s", checksum, basename);
}

static void
gs_screenshot_soup_msg_set_modified_request (SoupMessage *msg, GFile *file)
{
	GTimeVal time_val;
	g_autoptr(GDateTime) date_time = NULL;
	g_autoptr(GFileInfo) info = NULL;
	g_autofree gchar *mod_date = NULL;

	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_TIME_MODIFIED,
				  G_FILE_QUERY_INFO_NONE,
				  NULL,
				  NULL);
	if (info == NULL)
		return;
	g_file_info_get_modification_time (info, &time_val);
	date_time = g_date_time_new_from_timeval_local (&time_val);
	mod_date = g_date_time_format (date_time, "%a, %d %b %Y %H:%M:%S %Z");
	soup_message_headers_append (msg->request_headers,
				     "If-Modified-Since",
				     mod_date);
}

void
gs_screenshot_image_load_async (GsScreenshotImage *ssimg,
				GCancellable *cancellable)
{
	AsImage *im = NULL;
	const gchar *url;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *cache_kind = NULL;
	g_autofree gchar *cachefn_thumb = NULL;
	g_autofree gchar *sizedir = NULL;
	g_autoptr(SoupURI) base_uri = NULL;

	g_return_if_fail (GS_IS_SCREENSHOT_IMAGE (ssimg));

	g_return_if_fail (AS_IS_SCREENSHOT (ssimg->screenshot));
	g_return_if_fail (ssimg->width != 0);
	g_return_if_fail (ssimg->height != 0);

	/* load an image according to the scale factor */
	ssimg->scale = (guint) gtk_widget_get_scale_factor (GTK_WIDGET (ssimg));
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
		g_free (ssimg->filename);
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
	g_free (ssimg->filename);
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

	/* does local file already exist and has recently been downloaded */
	if (g_file_test (ssimg->filename, G_FILE_TEST_EXISTS)) {
		guint64 age_max;
		g_autoptr(GFile) file = NULL;

		/* show the image we have in cache while we're checking for the
		 * new screenshot (which probably won't have changed) */
		as_screenshot_show_image (ssimg);

		/* verify the cache age against the maximum allowed */
		age_max = g_settings_get_uint (ssimg->settings,
					       "screenshot-cache-age-maximum");
		file = g_file_new_for_path (ssimg->filename);
		/* image new enough, not re-requesting from server */
		if (age_max > 0 && gs_utils_get_file_age (file) < age_max)
			return;
	}

	/* if we're not showing a full-size image, we try loading a blurred
	 * smaller version of it straight away */
	if (!ssimg->showing_image &&
	    ssimg->width > AS_IMAGE_THUMBNAIL_WIDTH &&
	    ssimg->height > AS_IMAGE_THUMBNAIL_HEIGHT) {
		const gchar *url_thumb;
		g_autofree gchar *basename_thumb = NULL;
		g_autofree gchar *cache_kind_thumb = NULL;
		im = as_screenshot_get_image (ssimg->screenshot,
					      AS_IMAGE_THUMBNAIL_WIDTH * ssimg->scale,
					      AS_IMAGE_THUMBNAIL_HEIGHT * ssimg->scale);
		url_thumb = as_image_get_url (im);
		basename_thumb = gs_screenshot_get_cachefn_for_url (url_thumb);
		cache_kind_thumb = g_build_filename ("screenshots", "112x63", NULL);
		cachefn_thumb = gs_utils_get_cache_filename (cache_kind_thumb,
							     basename_thumb,
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
		return;
	}

	/* not all servers support If-Modified-Since, but worst case we just
	 * re-download the entire file again every 30 days */
	if (g_file_test (ssimg->filename, G_FILE_TEST_EXISTS)) {
		g_autoptr(GFile) file = g_file_new_for_path (ssimg->filename);
		gs_screenshot_soup_msg_set_modified_request (ssimg->message, file);
	}

	/* send async */
	soup_session_queue_message (ssimg->session,
				    g_object_ref (ssimg->message) /* transfer full */,
				    gs_screenshot_image_complete_cb,
				    g_object_ref (ssimg));
}

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
	g_clear_object (&ssimg->settings);

	g_clear_pointer (&ssimg->filename, g_free);

	GTK_WIDGET_CLASS (gs_screenshot_image_parent_class)->destroy (widget);
}

static void
gs_screenshot_image_init (GsScreenshotImage *ssimg)
{
	AtkObject *accessible;

	ssimg->use_desktop_background = TRUE;
	ssimg->settings = g_settings_new ("org.gnome.software");
	ssimg->showing_image = FALSE;

	gtk_widget_set_has_window (GTK_WIDGET (ssimg), FALSE);
	gtk_widget_init_template (GTK_WIDGET (ssimg));

	accessible = gtk_widget_get_accessible (GTK_WIDGET (ssimg));
	if (accessible != 0) {
		atk_object_set_role (accessible, ATK_ROLE_IMAGE);
		atk_object_set_name (accessible, _("Screenshot"));
	}
}

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

GtkWidget *
gs_screenshot_image_new (SoupSession *session)
{
	GsScreenshotImage *ssimg;
	ssimg = g_object_new (GS_TYPE_SCREENSHOT_IMAGE, NULL);
	ssimg->session = g_object_ref (session);
	return GTK_WIDGET (ssimg);
}
