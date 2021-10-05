/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-screenshot-image.h"
#include "gs-common.h"

#define SPINNER_TIMEOUT_SECS 2

struct _GsScreenshotImage
{
	GtkWidget	 parent_instance;

	AsScreenshot	*screenshot;
	GtkWidget	*spinner;
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
	guint		 width;
	guint		 height;
	guint		 scale;
	guint		 load_timeout_id;
	gboolean	 showing_image;
};

G_DEFINE_TYPE (GsScreenshotImage, gs_screenshot_image, GTK_TYPE_WIDGET)

AsScreenshot *
gs_screenshot_image_get_screenshot (GsScreenshotImage *ssimg)
{
	g_return_val_if_fail (GS_IS_SCREENSHOT_IMAGE (ssimg), NULL);
	return ssimg->screenshot;
}

static void
gs_screenshot_image_start_spinner (GsScreenshotImage *ssimg)
{
	gtk_widget_show (ssimg->spinner);
	gs_start_spinner (GTK_SPINNER (ssimg->spinner));
}

static void
gs_screenshot_image_stop_spinner (GsScreenshotImage *ssimg)
{
	gs_stop_spinner (GTK_SPINNER (ssimg->spinner));
	gtk_widget_hide (ssimg->spinner);
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
	gs_screenshot_image_stop_spinner (ssimg);
}

static void
as_screenshot_show_image (GsScreenshotImage *ssimg)
{
	g_autoptr(GdkPixbuf) pixbuf = NULL;

	/* no need to composite */
	if (ssimg->width == G_MAXUINT || ssimg->height == G_MAXUINT) {
		pixbuf = gdk_pixbuf_new_from_file (ssimg->filename, NULL);
	} else {
		/* this is always going to have alpha */
		pixbuf = gdk_pixbuf_new_from_file_at_scale (ssimg->filename,
							    (gint) (ssimg->width * ssimg->scale),
							    (gint) (ssimg->height * ssimg->scale),
							    FALSE, NULL);
	}

	/* show icon */
	if (g_strcmp0 (ssimg->current_image, "image1") == 0) {
		if (pixbuf != NULL) {
			gtk_picture_set_pixbuf (GTK_PICTURE (ssimg->image2), pixbuf);
		}
		gtk_stack_set_visible_child_name (GTK_STACK (ssimg->stack), "image2");
		ssimg->current_image = "image2";
	} else {
		if (pixbuf != NULL) {
			gtk_picture_set_pixbuf (GTK_PICTURE (ssimg->image1), pixbuf);
		}
		gtk_stack_set_visible_child_name (GTK_STACK (ssimg->stack), "image1");
		ssimg->current_image = "image1";
	}

	gtk_widget_show (GTK_WIDGET (ssimg));
	ssimg->showing_image = TRUE;

	gs_screenshot_image_stop_spinner (ssimg);
}

static GdkPixbuf *
gs_pixbuf_resample (GdkPixbuf *original,
		    guint width,
		    guint height,
		    gboolean blurred)
{
	g_autoptr(GdkPixbuf) pixbuf = NULL;
	guint tmp_height;
	guint tmp_width;
	guint pixbuf_height;
	guint pixbuf_width;
	g_autoptr(GdkPixbuf) pixbuf_tmp = NULL;

	/* never set */
	if (original == NULL)
		return NULL;

	/* 0 means 'default' */
	if (width == 0)
		width = (guint) gdk_pixbuf_get_width (original);
	if (height == 0)
		height = (guint) gdk_pixbuf_get_height (original);

	/* don't do anything to an image with the correct size */
	pixbuf_width = (guint) gdk_pixbuf_get_width (original);
	pixbuf_height = (guint) gdk_pixbuf_get_height (original);
	if (width == pixbuf_width && height == pixbuf_height)
		return g_object_ref (original);

	/* is the aspect ratio of the source perfectly 16:9 */
	if ((pixbuf_width / 16) * 9 == pixbuf_height) {
		pixbuf = gdk_pixbuf_scale_simple (original,
						  (gint) width, (gint) height,
						  GDK_INTERP_HYPER);
		if (blurred)
			gs_utils_pixbuf_blur (pixbuf, 5, 3);
		return g_steal_pointer (&pixbuf);
	}

	/* create new 16:9 pixbuf with alpha padding */
	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB,
				 TRUE, 8,
				 (gint) width,
				 (gint) height);
	gdk_pixbuf_fill (pixbuf, 0x00000000);
	/* check the ratio to see which property needs to be fitted and which needs
	 * to be reduced */
	if (pixbuf_width * 9 > pixbuf_height * 16) {
		tmp_width = width;
		tmp_height = width * pixbuf_height / pixbuf_width;
	} else {
		tmp_width = height * pixbuf_width / pixbuf_height;
		tmp_height = height;
	}
	pixbuf_tmp = gdk_pixbuf_scale_simple (original,
					      (gint) tmp_width,
					      (gint) tmp_height,
					      GDK_INTERP_HYPER);
	if (blurred)
		gs_utils_pixbuf_blur (pixbuf_tmp, 5, 3);
	gdk_pixbuf_copy_area (pixbuf_tmp,
			      0, 0, /* of src */
			      (gint) tmp_width,
			      (gint) tmp_height,
			      pixbuf,
			      (gint) (width - tmp_width) / 2,
			      (gint) (height - tmp_height) / 2);
	return g_steal_pointer (&pixbuf);
}

static gboolean
gs_pixbuf_save_filename (GdkPixbuf *pixbuf,
			 const gchar *filename,
			 guint width,
			 guint height,
			 GError **error)
{
	g_autoptr(GdkPixbuf) pb = NULL;

	/* resample & save pixbuf */
	pb = gs_pixbuf_resample (pixbuf, width, height, FALSE);
	return gdk_pixbuf_save (pb,
				filename,
				"png",
				error,
				NULL);
}

static void
gs_screenshot_image_show_blurred (GsScreenshotImage *ssimg,
				  const gchar *filename_thumb)
{
	g_autoptr(GdkPixbuf) pb_src = NULL;
	g_autoptr(GdkPixbuf) pb = NULL;

	pb_src = gdk_pixbuf_new_from_file (filename_thumb, NULL);
	if (pb_src == NULL)
		return;
	pb = gs_pixbuf_resample (pb_src,
				 ssimg->width * ssimg->scale,
				 ssimg->height * ssimg->scale,
				 TRUE /* blurred */);
	if (pb == NULL)
		return;

	if (g_strcmp0 (ssimg->current_image, "image1") == 0) {
		gtk_picture_set_pixbuf (GTK_PICTURE (ssimg->image1), pb);
	} else {
		gtk_picture_set_pixbuf (GTK_PICTURE (ssimg->image2), pb);
	}
}

static gboolean
gs_screenshot_image_save_downloaded_img (GsScreenshotImage *ssimg,
					 GdkPixbuf *pixbuf,
					 GError **error)
{
	gboolean ret;
	const GPtrArray *images;
	g_autoptr(GError) error_local = NULL;
	g_autofree char *filename = NULL;
	g_autofree char *size_dir = NULL;
	g_autofree char *cache_kind = NULL;
	g_autofree char *basename = NULL;
	guint width = ssimg->width;
	guint height = ssimg->height;

	ret = gs_pixbuf_save_filename (pixbuf, ssimg->filename,
				      ssimg->width * ssimg->scale,
				      ssimg->height * ssimg->scale,
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
						GS_UTILS_CACHE_FLAG_WRITEABLE |
						GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
						&error_local);

        if (filename == NULL) {
		/* if we cannot get a cache filename, warn about that but do not
		 * set a user's visible error because this is a complementary
		 * operation */
                g_warning ("Failed to get cache filename for counterpart "
                           "screenshot '%s' in folder '%s': %s", basename,
                           cache_kind, error_local->message);
                return TRUE;
        }

	ret = gs_pixbuf_save_filename (pixbuf, filename,
					width, height,
					&error_local);

	if (!ret) {
		/* if we cannot save this screenshot, warn about that but do not
		 * set a user's visible error because this is a complementary
		 * operation */
                g_warning ("Failed to save screenshot '%s': %s", filename,
                           error_local->message);
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

	if (ssimg->load_timeout_id) {
		g_source_remove (ssimg->load_timeout_id);
		ssimg->load_timeout_id = 0;
	}

	/* return immediately if the message was cancelled or if we're in destruction */
	if (msg->status_code == SOUP_STATUS_CANCELLED || ssimg->session == NULL)
		return;

	if (msg->status_code == SOUP_STATUS_NOT_MODIFIED) {
		g_debug ("screenshot has not been modified");
		as_screenshot_show_image (ssimg);
		gs_screenshot_image_stop_spinner (ssimg);
		return;
	}
	if (msg->status_code != SOUP_STATUS_OK) {
		/* Ignore failures due to being offline */
		if (msg->status_code != SOUP_STATUS_CANT_RESOLVE)
			g_warning ("Result of screenshot downloading attempt with "
				   "status code '%u': %s", msg->status_code,
				   msg->reason_phrase);
		gs_screenshot_image_stop_spinner (ssimg);
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
	if (stream == NULL) {
		gs_screenshot_image_stop_spinner (ssimg);
		return;
	}

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
	gtk_widget_set_size_request (ssimg->stack, -1, (gint) height);
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
#ifndef GLIB_VERSION_2_62
	GTimeVal time_val;
#endif
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
#ifdef GLIB_VERSION_2_62
	date_time = g_file_info_get_modification_date_time (info);
#else
	g_file_info_get_modification_time (info, &time_val);
	date_time = g_date_time_new_from_timeval_local (&time_val);
#endif
	mod_date = g_date_time_format (date_time, "%a, %d %b %Y %H:%M:%S %Z");
	soup_message_headers_append (msg->request_headers,
				     "If-Modified-Since",
				     mod_date);
}

static gboolean
gs_screenshot_show_spinner_cb (gpointer user_data)
{
	GsScreenshotImage *ssimg = user_data;

	ssimg->load_timeout_id = 0;
	gs_screenshot_image_start_spinner (ssimg);

	return FALSE;
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
	g_assert (ssimg->filename != NULL);

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
		g_assert (cachefn_thumb != NULL);
		if (g_file_test (cachefn_thumb, G_FILE_TEST_EXISTS))
			gs_screenshot_image_show_blurred (ssimg, cachefn_thumb);
	}

	/* re-request the cache filename, which might be different as it needs
	 * to be writable this time */
	g_free (ssimg->filename);
	ssimg->filename = gs_utils_get_cache_filename (cache_kind,
						       basename,
						       GS_UTILS_CACHE_FLAG_WRITEABLE |
						       GS_UTILS_CACHE_FLAG_CREATE_DIRECTORY,
						       NULL);
	if (ssimg->filename == NULL) {
		/* TRANSLATORS: this is when we try create the cache directory
		 * but we were out of space or permission was denied */
		gs_screenshot_image_set_error (ssimg, _("Could not create cache"));
		return;
	}

	/* download file */
	g_debug ("downloading %s to %s", url, ssimg->filename);
	base_uri = soup_uri_new (url);
	if (base_uri == NULL || !SOUP_URI_VALID_FOR_HTTP (base_uri)) {
		/* TRANSLATORS: this is when we try to download a screenshot
		 * that was not a valid URL */
		gs_screenshot_image_set_error (ssimg, _("Screenshot not valid"));
		return;
	}

	if (ssimg->load_timeout_id) {
		g_source_remove (ssimg->load_timeout_id);
		ssimg->load_timeout_id = 0;
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

	ssimg->load_timeout_id = g_timeout_add_seconds (SPINNER_TIMEOUT_SECS,
		gs_screenshot_show_spinner_cb, ssimg);

	/* send async */
	soup_session_queue_message (ssimg->session,
				    g_object_ref (ssimg->message) /* transfer full */,
				    gs_screenshot_image_complete_cb,
				    g_object_ref (ssimg));
}

gboolean
gs_screenshot_image_is_showing (GsScreenshotImage *ssimg)
{
	return ssimg->showing_image;
}

void
gs_screenshot_image_set_description (GsScreenshotImage *ssimg,
				     const gchar *description)
{
	gtk_accessible_update_property (GTK_ACCESSIBLE (ssimg->image1),
					GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, description,
					-1);
	gtk_accessible_update_property (GTK_ACCESSIBLE (ssimg->image2),
					GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, description,
					-1);
}

static void
gs_screenshot_image_dispose (GObject *object)
{
	GsScreenshotImage *ssimg = GS_SCREENSHOT_IMAGE (object);

	if (ssimg->load_timeout_id) {
		g_source_remove (ssimg->load_timeout_id);
		ssimg->load_timeout_id = 0;
	}

	if (ssimg->message != NULL) {
		soup_session_cancel_message (ssimg->session,
		                             ssimg->message,
		                             SOUP_STATUS_CANCELLED);
		g_clear_object (&ssimg->message);
	}
	gs_widget_remove_all (GTK_WIDGET (ssimg), NULL);
	g_clear_object (&ssimg->screenshot);
	g_clear_object (&ssimg->session);
	g_clear_object (&ssimg->settings);

	g_clear_pointer (&ssimg->filename, g_free);

	G_OBJECT_CLASS (gs_screenshot_image_parent_class)->dispose (object);
}

static void
gs_screenshot_image_init (GsScreenshotImage *ssimg)
{
	ssimg->settings = g_settings_new ("org.gnome.software");
	ssimg->showing_image = FALSE;

	gtk_widget_init_template (GTK_WIDGET (ssimg));
}

static void
gs_screenshot_image_snapshot (GtkWidget   *widget,
                              GtkSnapshot *snapshot)
{
	GtkStyleContext *context;

	context = gtk_widget_get_style_context (widget);
	gtk_snapshot_render_frame (snapshot,
				   context,
				   0.0, 0.0,
				   gtk_widget_get_width (widget),
				   gtk_widget_get_height (widget));

	GTK_WIDGET_CLASS (gs_screenshot_image_parent_class)->snapshot (widget, snapshot);
}

static void
gs_screenshot_image_class_init (GsScreenshotImageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->dispose = gs_screenshot_image_dispose;

	widget_class->snapshot = gs_screenshot_image_snapshot;

	gtk_widget_class_set_template_from_resource (widget_class,
						     "/org/gnome/Software/gs-screenshot-image.ui");
	gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
	gtk_widget_class_set_accessible_role (widget_class, GTK_ACCESSIBLE_ROLE_IMG);

	gtk_widget_class_bind_template_child (widget_class, GsScreenshotImage, spinner);
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
