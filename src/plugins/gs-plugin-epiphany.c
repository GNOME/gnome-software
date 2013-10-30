/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
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

#define _GNU_SOURCE
#include <string.h>

#include <glib/gi18n.h>
#include <libsoup/soup.h>

#include <gs-plugin.h>
#include <gs-utils.h>

struct GsPluginPrivate {
	GList			*list;
	SoupSession		*session;
	gsize			 loaded;
};

/**
 * gs_plugin_get_name:
 */
const gchar *
gs_plugin_get_name (void)
{
	return "epiphany";
}

/**
 * gs_plugin_initialize:
 */
void
gs_plugin_initialize (GsPlugin *plugin)
{
	plugin->priv = GS_PLUGIN_GET_PRIVATE (GsPluginPrivate);

	/* we can only work with epiphany */
	if (!g_file_test ("/usr/bin/epiphany", G_FILE_TEST_EXISTS)) {
		gs_plugin_set_enabled (plugin, FALSE);
		g_warning ("disabling '%s' as epiphany does not exist",
			   plugin->name);
	}
}

/**
 * gs_plugin_get_priority:
 */
gdouble
gs_plugin_get_priority (GsPlugin *plugin)
{
	return 1.0f;
}

/**
 * gs_plugin_destroy:
 */
void
gs_plugin_destroy (GsPlugin *plugin)
{
	if (plugin->priv->session != NULL)
		g_object_unref (plugin->priv->session);
	gs_plugin_list_free (plugin->priv->list);
}

/**
 * gs_plugin_add_installed_file:
 */
static gboolean
gs_plugin_add_installed_file (GsPlugin *plugin,
			      const gchar *filename,
			      GsApp **app,
			      GError **error)
{
	GKeyFile *kf;
	gboolean no_display;
	gboolean ret;
	gchar *comment = NULL;
	gchar *icon = NULL;
	gchar *name = NULL;
	gchar *path;

	/* load keyfile */
	path = g_build_filename (g_get_user_data_dir (),
				 "applications",
				 filename,
				 NULL);
	kf = g_key_file_new ();
	ret = g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, error);
	if (!ret)
		goto out;

	/* check we're showing this */
	no_display = g_key_file_get_boolean (kf,
					     G_KEY_FILE_DESKTOP_GROUP,
					     G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY,
					     NULL);

	/* get name */
	name = g_key_file_get_locale_string (kf,
					     G_KEY_FILE_DESKTOP_GROUP,
					     G_KEY_FILE_DESKTOP_KEY_NAME,
					     NULL,
					     error);
	if (name == NULL) {
		ret = FALSE;
		goto out;
	}

	/* get icon */
	icon = g_key_file_get_locale_string (kf,
					     G_KEY_FILE_DESKTOP_GROUP,
					     G_KEY_FILE_DESKTOP_KEY_ICON,
					     NULL,
					     error);
	if (icon == NULL) {
		ret = FALSE;
		goto out;
	}

	/* get comment */
	comment = g_key_file_get_locale_string (kf,
						G_KEY_FILE_DESKTOP_GROUP,
						G_KEY_FILE_DESKTOP_KEY_COMMENT,
						NULL,
						NULL);
	if (comment == NULL) {
		/* TRANSLATORS: this is when a webapp has no comment */
		comment = g_strdup_printf (_("Web app"));
	}

	/* create application */
	*app = gs_app_new (filename);
	gs_app_set_name (*app, name);
	gs_app_set_summary (*app, comment);
	/* TRANSLATORS: this is the licence of the web-app */
	gs_app_set_licence (*app, _("Proprietary"));
	gs_app_set_state (*app, no_display ? GS_APP_STATE_AVAILABLE :
					     GS_APP_STATE_INSTALLED);
	gs_app_set_kind (*app, GS_APP_KIND_NORMAL);
	gs_app_set_id_kind (*app, GS_APP_ID_KIND_WEBAPP);
	gs_app_add_source_id (*app, path);
	gs_app_set_icon (*app, icon);
	ret = gs_app_load_icon (*app, error);
	if (!ret)
		goto out;
out:
	g_key_file_free (kf);
	g_free (path);
	g_free (icon);
	g_free (name);
	g_free (comment);
	return ret;
}

/**
 * gs_plugin_epiphany_load_db:
 */
static gboolean
gs_plugin_epiphany_load_db (GsPlugin *plugin, GError **error)
{
	GDir *dir;
	GsApp *app = NULL;
	const gchar *filename;
	gboolean ret = TRUE;
	gchar *path;

	/* find any web apps */
	path = g_build_filename (g_get_user_data_dir (), "applications", NULL);
	dir = g_dir_open (path, 0, error);
	if (dir == NULL) {
		ret = FALSE;
		goto out;
	}
	while ((filename = g_dir_read_name (dir)) != NULL) {
		if (!g_str_has_prefix (filename, "epiphany"))
			continue;
		if (!g_str_has_suffix (filename, ".desktop"))
			continue;
		ret = gs_plugin_add_installed_file (plugin,
						    filename,
						    &app,
						    error);
		if (!ret)
			goto out;
		if (app != NULL) {
			gs_app_set_management_plugin (app, "Epiphany");
			gs_plugin_add_app (&plugin->priv->list, app);
			g_clear_object (&app);
		}
	}
out:
	g_free (path);
	if (dir != NULL)
		g_dir_close (dir);
	return ret;
}

/**
 * gs_plugin_add_installed:
 */
gboolean
gs_plugin_add_installed (GsPlugin *plugin,
			 GList **list,
			 GCancellable *cancellable,
			 GError **error)
{
	GList *l;
	GsApp *app;
	gboolean ret = TRUE;

	/* already loaded */
	if (g_once_init_enter (&plugin->priv->loaded)) {
		ret = gs_plugin_epiphany_load_db (plugin, error);
		g_once_init_leave (&plugin->priv->loaded, TRUE);
		if (!ret)
			goto out;
	}

	/* add all installed apps */
	for (l = plugin->priv->list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_state (app) != GS_APP_STATE_INSTALLED)
			continue;
		gs_plugin_add_app (list, app);
	}
out:
	return ret;
}

/**
 * gs_plugin_epiphany_match_app_value:
 */
static gboolean
gs_plugin_epiphany_match_app_value (GsApp *app, const gchar *value)
{
	if (strcasestr (gs_app_get_name (app), value) != NULL)
		return TRUE;
	if (strcasestr (gs_app_get_summary (app), value) != NULL)
		return TRUE;
	return FALSE;
}

/**
 * gs_plugin_epiphany_match_app:
 */
static gboolean
gs_plugin_epiphany_match_app (GsApp *app, gchar **values)
{
	gboolean matches = FALSE;
	guint i;

	/* does the GsApp match *all* search keywords */
	for (i = 0; values[i] != NULL; i++) {
		matches = gs_plugin_epiphany_match_app_value (app, values[i]);
		if (!matches)
			break;
	}
	return matches;
}

/**
 * gs_plugin_add_search:
 */
gboolean
gs_plugin_add_search (GsPlugin *plugin,
		      gchar **values,
		      GList **list,
		      GCancellable *cancellable,
		      GError **error)
{
	GList *l;
	GsApp *app;
	gboolean ret = TRUE;

	/* already loaded */
	if (g_once_init_enter (&plugin->priv->loaded)) {
		ret = gs_plugin_epiphany_load_db (plugin, error);
		g_once_init_leave (&plugin->priv->loaded, TRUE);
		if (!ret)
			goto out;
	}

	/* add any matching apps */
	for (l = plugin->priv->list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_plugin_epiphany_match_app (app, values))
			gs_plugin_add_app (list, app);
	}
out:
	return ret;
}

/**
 * gs_plugin_app_set_enabled:
 */
static gboolean
gs_plugin_app_set_enabled (const gchar *filename, gboolean enabled, GError **error)
{
	GKeyFile *kf;
	gboolean ret;
	gchar *data = NULL;
	gsize length;

	/* load file */
	kf = g_key_file_new ();
	ret = g_key_file_load_from_file (kf, filename, G_KEY_FILE_NONE, error);
	if (!ret)
		goto out;

	/* change value */
	g_key_file_set_boolean (kf,
				G_KEY_FILE_DESKTOP_GROUP,
				G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY,
				!enabled);

	/* save value */
	data = g_key_file_to_data (kf, &length, error);
	if (data == NULL) {
		ret = FALSE;
		goto out;
	}
	ret = g_file_set_contents (filename, data, length, error);
	if (!ret)
		goto out;
out:
	g_free (data);
	g_key_file_free (kf);
	return ret;
}

/**
 * gs_plugin_app_install:
 */
gboolean
gs_plugin_app_install (GsPlugin *plugin,
		       GsApp *app,
		       GCancellable *cancellable,
		       GError **error)
{
	const gchar *filename;
	gboolean ret = TRUE;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "Epiphany") != 0)
		goto out;
	filename = gs_app_get_source_id_default (app);
	if (filename == NULL)
		goto out;
	gs_app_set_state (app, GS_APP_STATE_INSTALLING);
	ret = gs_plugin_app_set_enabled (filename, TRUE, error);
	if (!ret)
		goto out;
	gs_app_set_state (app, GS_APP_STATE_INSTALLED);
out:
	return ret;
}

/**
 * gs_plugin_app_remove:
 */
gboolean
gs_plugin_app_remove (GsPlugin *plugin,
		      GsApp *app,
		      GCancellable *cancellable,
		      GError **error)
{
	const gchar *filename;
	gboolean ret = TRUE;

	/* only process this app if was created by this plugin */
	if (g_strcmp0 (gs_app_get_management_plugin (app), "Epiphany") != 0)
		goto out;

	filename = gs_app_get_source_id_default (app);
	if (filename == NULL)
		goto out;
	gs_app_set_state (app, GS_APP_STATE_REMOVING);
	ret = gs_plugin_app_set_enabled (filename, FALSE, error);
	if (!ret)
		goto out;
	gs_app_set_state (app, GS_APP_STATE_AVAILABLE);
out:
	return ret;
}

/**
 * gs_plugin_write_file:
 */
static gboolean
gs_plugin_write_file (GsApp *app, const gchar *filename, GError **error)
{
	GKeyFile *kf;
	const gchar *url;
	gboolean ret;
	gchar *data;
	gchar *exec;
	gchar *profile;
	gchar *wmclass;
	gsize length;

	kf = g_key_file_new ();
	g_key_file_set_string (kf,
			       G_KEY_FILE_DESKTOP_GROUP,
			       G_KEY_FILE_DESKTOP_KEY_NAME,
			       gs_app_get_name (app));
	g_key_file_set_string (kf,
			       G_KEY_FILE_DESKTOP_GROUP,
			       G_KEY_FILE_DESKTOP_KEY_COMMENT,
			       gs_app_get_summary (app));

	url = gs_app_get_url (app, GS_APP_URL_KIND_HOMEPAGE);
	wmclass = g_strdup_printf ("%s-%s",
				   gs_app_get_id (app),
				   gs_app_get_metadata_item (app, "Epiphany::hash"));
	profile = g_strdup_printf ("%s/epiphany/app-%s",
				   g_get_user_config_dir (), wmclass);
	exec = g_strdup_printf ("epiphany --application-mode "
				"--profile=\"%s\" %s", profile, url);
	g_key_file_set_string (kf,
			       G_KEY_FILE_DESKTOP_GROUP,
			       G_KEY_FILE_DESKTOP_KEY_EXEC,
			       exec);
	g_key_file_set_boolean (kf,
				G_KEY_FILE_DESKTOP_GROUP,
				G_KEY_FILE_DESKTOP_KEY_STARTUP_NOTIFY,
				TRUE);
	g_key_file_set_boolean (kf,
				G_KEY_FILE_DESKTOP_GROUP,
				G_KEY_FILE_DESKTOP_KEY_TERMINAL,
				FALSE);
	g_key_file_set_boolean (kf,
				G_KEY_FILE_DESKTOP_GROUP,
				G_KEY_FILE_DESKTOP_KEY_NO_DISPLAY,
				gs_app_get_state (app) == GS_APP_STATE_INSTALLED ? FALSE : TRUE);
	g_key_file_set_string (kf,
			       G_KEY_FILE_DESKTOP_GROUP,
			       G_KEY_FILE_DESKTOP_KEY_TYPE,
			       G_KEY_FILE_DESKTOP_TYPE_APPLICATION);
	g_key_file_set_string (kf,
			       G_KEY_FILE_DESKTOP_GROUP,
			       G_KEY_FILE_DESKTOP_KEY_ICON,
			       gs_app_get_icon (app));
	g_key_file_set_string (kf,
			       G_KEY_FILE_DESKTOP_GROUP,
			       G_KEY_FILE_DESKTOP_KEY_STARTUP_WM_CLASS,
			       wmclass);

	/* save keyfile */
	data = g_key_file_to_data (kf, &length, error);
	if (data == NULL) {
		ret = FALSE;
		goto out;
	}
	ret = g_file_set_contents (filename, data, length, error);
	if (!ret)
		goto out;
out:
	g_free (profile);
	g_free (exec);
	g_free (wmclass);
	g_key_file_free (kf);
	return ret;
}

/**
 * gs_plugin_setup_networking:
 */
static gboolean
gs_plugin_setup_networking (GsPlugin *plugin, GError **error)
{
	gboolean ret = TRUE;

	/* already set up */
	if (plugin->priv->session != NULL)
		goto out;

	/* set up a session */
	plugin->priv->session = soup_session_sync_new_with_options (SOUP_SESSION_USER_AGENT,
								    "gnome-software",
								    SOUP_SESSION_TIMEOUT, 5000,
								    NULL);
	if (plugin->priv->session == NULL) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "%s: failed to setup networking",
			     plugin->name);
		goto out;
	}
	soup_session_add_feature_by_type (plugin->priv->session,
					  SOUP_TYPE_PROXY_RESOLVER_DEFAULT);
out:
	return ret;
}

/**
 * gs_plugin_epiphany_download:
 */
static gboolean
gs_plugin_epiphany_download (GsPlugin *plugin, const gchar *uri, const gchar *filename, GError **error)
{
	GInputStream *stream = NULL;
	GdkPixbuf *pixbuf = NULL;
	GdkPixbuf *pixbuf_new = NULL;
	SoupMessage *msg = NULL;
	gboolean ret = TRUE;
	guint status_code;

	/* create the GET data */
	msg = soup_message_new (SOUP_METHOD_GET, uri);

	/* ensure networking is set up */
	ret = gs_plugin_setup_networking (plugin, error);
	if (!ret)
		goto out;

	/* set sync request */
	status_code = soup_session_send_message (plugin->priv->session, msg);
	if (status_code != SOUP_STATUS_OK) {
		ret = FALSE;
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_FAILED,
			     "Failed to set rating on fedora-tagger: %s",
			     soup_status_get_phrase (status_code));
		goto out;
	}

	/* we're assuming this is a 64x64 png file, resize if not */
	stream = g_memory_input_stream_new_from_data (msg->response_body->data,
						      msg->response_body->length,
						      NULL);
	pixbuf = gdk_pixbuf_new_from_stream (stream, NULL, error);
	if (pixbuf == NULL) {
		ret = FALSE;
		goto out;
	}
	if (gdk_pixbuf_get_height (pixbuf) == 64 &&
	    gdk_pixbuf_get_width (pixbuf) == 64) {
		pixbuf_new = g_object_ref (pixbuf);
	} else {
		pixbuf_new = gdk_pixbuf_scale_simple (pixbuf, 64, 64,
						      GDK_INTERP_BILINEAR);
	}

	/* write file */
	ret = gdk_pixbuf_save (pixbuf_new, filename, "png", error, NULL);
	if (!ret)
		goto out;

out:
	if (stream != NULL)
		g_object_unref (stream);
	if (pixbuf_new != NULL)
		g_object_unref (pixbuf_new);
	if (pixbuf != NULL)
		g_object_unref (pixbuf);
	if (msg != NULL)
		g_object_unref (msg);
	return ret;
}

/**
 * gs_plugin_refine_app:
 */
static gboolean
gs_plugin_refine_app (GsPlugin *plugin, GsApp *app, GError **error)
{
	gboolean ret;
	gchar *filename = NULL;
	gchar *path = NULL;
	gchar *filename_icon = NULL;
	gchar *hash;
	GError *error_local = NULL;

	/* this is not yet installed */
	gs_app_set_state (app, GS_APP_STATE_AVAILABLE);

	/* calculate SHA1 hash of name */
	hash = g_compute_checksum_for_string (G_CHECKSUM_SHA1, gs_app_get_name (app), -1);
	gs_app_set_metadata (app, "Epiphany::hash", hash);

	/* download icon if it does not exist */
	filename_icon = g_strdup_printf ("%s/epiphany/app-%s-%s/app-icon.png",
					 g_get_user_config_dir (),
					 gs_app_get_id (app),
					 hash);
	if (!g_file_test (filename_icon, G_FILE_TEST_EXISTS)) {
		ret = gs_mkdir_parent (filename_icon, error);
		if (!ret)
			goto out;
		ret = gs_plugin_epiphany_download (plugin,
						   gs_app_get_icon (app),
						   filename_icon,
						   &error_local);
		if (!ret) {
			/* this isn't a fatal error */
			gs_app_set_state (app, GS_APP_STATE_UNKNOWN);
			gs_app_set_state (app, GS_APP_STATE_UNAVAILABLE);
			g_debug ("Failed to download %s: %s",
				 gs_app_get_icon (app), error_local->message);
			g_error_free (error_local);
			ret = TRUE;
			goto out;
		}
	}

	/* set local icon name */
	gs_app_set_icon (app, filename_icon);
	ret = gs_app_load_icon (app, error);
	if (!ret)
		goto out;

	/* save file */
	filename = g_strdup_printf ("%s.desktop", gs_app_get_id (app));
	path = g_build_filename (g_get_user_data_dir (),
				 "applications",
				 filename,
				 NULL);
	ret = gs_plugin_write_file (app, path, error);
	if (!ret)
		goto out;
	gs_app_add_source_id (app, path);

	/* we now know about this */
	gs_plugin_add_app (&plugin->priv->list, app);
	gs_app_set_management_plugin (app, "Epiphany");
out:
	g_free (hash);
	g_free (path);
	g_free (filename);
	g_free (filename_icon);
	return ret;
}

/**
 * gs_plugin_refine:
 */
gboolean
gs_plugin_refine (GsPlugin *plugin,
		  GList *list,
		  GsPluginRefineFlags flags,
		  GCancellable *cancellable,
		  GError **error)
{
	GList *l;
	GsApp *app;
	const gchar *tmp;
	gboolean ret = TRUE;

	for (l = list; l != NULL; l = l->next) {
		app = GS_APP (l->data);
		if (gs_app_get_id_kind (app) != GS_APP_ID_KIND_WEBAPP)
			continue;
		gs_app_set_size (app, 4096);
		tmp = gs_app_get_source_id_default (app);
		if (tmp != NULL)
			continue;
		ret = gs_plugin_refine_app (plugin, app, error);
		if (!ret)
			goto out;
	}
out:
	return ret;
}
