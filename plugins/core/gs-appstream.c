/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018-2019 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <gnome-software.h>

#include "gs-appstream.h"

#define	GS_APPSTREAM_MAX_SCREENSHOTS	5

GsApp *
gs_appstream_create_app (GsPlugin *plugin, XbSilo *silo, XbNode *component, GError **error)
{
	GsApp *app;
	g_autoptr(GsApp) app_new = gs_app_new (NULL);

	/* refine enough to get the unique ID */
	if (!gs_appstream_refine_app (plugin, app_new, silo, component,
				      GS_PLUGIN_REFINE_FLAGS_DEFAULT,
				      error))
		return NULL;

	/* never add wildcard apps to the plugin cache */
	if (gs_app_has_quirk (app_new, GS_APP_QUIRK_IS_WILDCARD))
		return g_steal_pointer (&app_new);

	/* no longer supported */
	if (gs_app_get_kind (app_new) == AS_APP_KIND_SHELL_EXTENSION) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "shell extensions no longer supported");
		return NULL;
	}

	/* look for existing object */
	app = gs_plugin_cache_lookup (plugin, gs_app_get_unique_id (app_new));
	if (app != NULL)
		return app;

	/* use the temp object we just created */
	gs_app_set_metadata (app_new, "GnomeSoftware::Creator",
			     gs_plugin_get_name (plugin));
	gs_plugin_cache_add (plugin, NULL, app_new);
	return g_steal_pointer (&app_new);
}

static gchar *
gs_appstream_format_description (XbNode *root, GError **error)
{
	g_autoptr(GString) str = g_string_new (NULL);
	g_autoptr(XbNode) n = xb_node_get_child (root);

	while (n != NULL) {
		g_autoptr(XbNode) n2 = NULL;

		/* support <p>, <ul>, <ol> and <li>, ignore all else */
		if (g_strcmp0 (xb_node_get_element (n), "p") == 0) {
			g_string_append_printf (str, "%s\n\n", xb_node_get_text (n));
		} else if (g_strcmp0 (xb_node_get_element (n), "ul") == 0) {
			g_autoptr(GPtrArray) children = xb_node_get_children (n);
			for (guint i = 0; i < children->len; i++) {
				XbNode *nc = g_ptr_array_index (children, i);
				if (g_strcmp0 (xb_node_get_element (nc), "li") == 0) {
					g_string_append_printf (str, " • %s\n",
								xb_node_get_text (nc));
				}
			}
			g_string_append (str, "\n");
		} else if (g_strcmp0 (xb_node_get_element (n), "ol") == 0) {
			g_autoptr(GPtrArray) children = xb_node_get_children (n);
			for (guint i = 0; i < children->len; i++) {
				XbNode *nc = g_ptr_array_index (children, i);
				if (g_strcmp0 (xb_node_get_element (nc), "li") == 0) {
					g_string_append_printf (str, " %u. %s\n",
								i + 1,
								xb_node_get_text (nc));
				}
			}
			g_string_append (str, "\n");
		}

		n2 = xb_node_get_next (n);
		g_set_object (&n, n2);
	}

	/* remove extra newlines */
	while (str->len > 0 && str->str[str->len - 1] == '\n')
		g_string_truncate (str, str->len - 1);

	/* success */
	return g_string_free (g_steal_pointer (&str), FALSE);
}

static gchar *
gs_appstream_build_icon_prefix (XbNode *component)
{
	const gchar *origin;
	const gchar *tmp;
	gint npath;
	g_auto(GStrv) path = NULL;
	g_autoptr(XbNode) components = NULL;

	/* no parent, e.g. AppData */
	components = xb_node_get_parent (component);
	if (components == NULL)
		return NULL;

	/* set explicitly */
	tmp = xb_node_query_text (components, "info/icon-prefix", NULL);
	if (tmp != NULL)
		return g_strdup (tmp);

	/* fall back to origin */
	origin = xb_node_get_attr (components, "origin");
	if (origin == NULL)
		return NULL;

	/* no metadata */
	tmp = xb_node_query_text (components, "info/filename", NULL);
	if (tmp == NULL)
		return NULL;

	/* check format */
	path = g_strsplit (tmp, "/", -1);
	npath = g_strv_length (path);
	if (npath < 3 || !(g_strcmp0 (path[npath-2], "xmls") == 0 || g_strcmp0 (path[npath-2], "yaml") == 0))
		return NULL;

	/* fix the new path */
	g_free (path[npath-1]);
	g_free (path[npath-2]);
	path[npath-1] = g_strdup (origin);
	path[npath-2] = g_strdup ("icons");
	return g_strjoinv ("/", path);
}

static AsIcon *
gs_appstream_new_icon (XbNode *component, XbNode *n, AsIconKind icon_kind, guint sz)
{
	AsIcon *icon = as_icon_new ();
	g_autofree gchar *icon_path = NULL;
	as_icon_set_kind (icon, icon_kind);
	switch (icon_kind) {
	case AS_ICON_KIND_REMOTE:
		as_icon_set_url (icon, xb_node_get_text (n));
		break;
	default:
		as_icon_set_name (icon, xb_node_get_text (n));
	}
	if (sz == 0)
		sz = xb_node_get_attr_as_uint (n, "width");
	if (sz > 0) {
		as_icon_set_width (icon, sz);
		as_icon_set_height (icon, sz);
	}
	icon_path = gs_appstream_build_icon_prefix (component);
	if (icon_path != NULL)
		as_icon_set_prefix (icon, icon_path);
	return icon;
}

static AsIcon *
gs_appstream_get_icon_by_kind (XbNode *component, AsIconKind icon_kind)
{
	g_autofree gchar *xpath = NULL;
	g_autoptr(XbNode) icon = NULL;

	xpath = g_strdup_printf ("icon[@type='%s']",
				 as_icon_kind_to_string (icon_kind));
	icon = xb_node_query_first (component, xpath, NULL);
	if (icon == NULL)
		return NULL;
	return gs_appstream_new_icon (component, icon, icon_kind, 0);
}

static AsIcon *
gs_appstream_get_icon_by_kind_and_size (XbNode *component, AsIconKind icon_kind, guint sz)
{
	g_autofree gchar *xpath = NULL;
	g_autoptr(XbNode) icon = NULL;

	xpath = g_strdup_printf ("icon[@type='%s'][@height='%u'][@width='%u']",
				 as_icon_kind_to_string (icon_kind), sz, sz);
	icon = xb_node_query_first (component, xpath, NULL);
	if (icon == NULL)
		return NULL;
	return gs_appstream_new_icon (component, icon, icon_kind, sz);
}

static void
gs_appstream_refine_icon (GsPlugin *plugin, GsApp *app, XbNode *component)
{
	g_autoptr(AsIcon) icon = NULL;
	g_autoptr(XbNode) n = NULL;

	/* try a stock icon first */
	icon = gs_appstream_get_icon_by_kind (component, AS_ICON_KIND_STOCK);
	if (icon != NULL) {
		/* the stock icon referenced by the AppStream data may not be present in the current
		 * theme (usually more stock icon entries are added to permit huge themes like Papirus
		 * to style all apps in the software center). Since we can not rely on the icon's presence,
		 * we also add other icons to the list and do not return here. */
		gs_app_add_icon (app, icon);
	}

	/* if HiDPI get a 128px cached icon */
	if (gs_plugin_get_scale (plugin) == 2) {
		icon = gs_appstream_get_icon_by_kind_and_size (component,
							       AS_ICON_KIND_CACHED,
							       128);
		if (icon != NULL) {
			gs_app_add_icon (app, icon);
			return;
		}
	}

	/* non-HiDPI cached icon */
	icon = gs_appstream_get_icon_by_kind_and_size (component,
						       AS_ICON_KIND_CACHED,
						       64);
	if (icon != NULL) {
		gs_app_add_icon (app, icon);
		return;
	}

	/* prefer local */
	icon = gs_appstream_get_icon_by_kind (component, AS_ICON_KIND_LOCAL);
	if (icon != NULL) {
		/* does not exist, so try to find using the icon theme */
		if (as_icon_get_kind (icon) == AS_ICON_KIND_LOCAL &&
		    as_icon_get_filename (icon) == NULL) {
			g_debug ("converting missing LOCAL icon %s to STOCK",
				 as_icon_get_name (icon));
			as_icon_set_kind (icon, AS_ICON_KIND_STOCK);
		}
		gs_app_add_icon (app, icon);
		return;
	}

	/* remote URL */
	icon = gs_appstream_get_icon_by_kind (component, AS_ICON_KIND_REMOTE);
	if (icon != NULL) {
		gs_app_add_icon (app, icon);
		return;
	}

	/* assume a stock icon */
	n = xb_node_query_first (component, "icon", NULL);
	if (n != NULL) {
		icon = gs_appstream_new_icon (component, n, AS_ICON_KIND_STOCK, 0);
		gs_app_add_icon (app, icon);
	}
}

static gboolean
gs_appstream_refine_add_addons (GsPlugin *plugin,
				GsApp *app,
				XbSilo *silo,
				GError **error)
{
	g_autofree gchar *xpath = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) addons = NULL;

	/* get all components */
	xpath = g_strdup_printf ("components/component/extends[text()='%s']/..",
				 gs_app_get_id (app));
	addons = xb_silo_query (silo, xpath, 0, &error_local);
	if (addons == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < addons->len; i++) {
		XbNode *addon = g_ptr_array_index (addons, i);
		g_autoptr(GsApp) app2 = NULL;
		app2 = gs_appstream_create_app (plugin, silo, addon, error);
		if (app2 == NULL)
			return FALSE;
		gs_app_add_addon (app, app2);
	}
	return TRUE;
}

static gboolean
gs_appstream_refine_add_images (GsApp *app, AsScreenshot *ss, XbNode *screenshot, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) images = NULL;

	/* get all components */
	images = xb_node_query (screenshot, "image", 0, &error_local);
	if (images == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < images->len; i++) {
		XbNode *image = g_ptr_array_index (images, i);
		g_autoptr(AsImage) im = as_image_new ();
		as_image_set_height (im, xb_node_get_attr_as_uint (image, "height"));
		as_image_set_width (im, xb_node_get_attr_as_uint (image, "width"));
		as_image_set_kind (im, as_image_kind_from_string (xb_node_get_attr (image, "type")));
		as_image_set_url (im, xb_node_get_text (image));
		as_screenshot_add_image (ss, im);
	}

	/* success */
	return TRUE;
}

static gboolean
gs_appstream_refine_add_screenshots (GsApp *app, XbNode *component, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) screenshots = NULL;

	/* get all components */
	screenshots = xb_node_query (component, "screenshots/screenshot", 0, &error_local);
	if (screenshots == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < screenshots->len; i++) {
		XbNode *screenshot = g_ptr_array_index (screenshots, i);
		g_autoptr(AsScreenshot) ss = as_screenshot_new ();
		if (!gs_appstream_refine_add_images (app, ss, screenshot, error))
			return FALSE;
		gs_app_add_screenshot (app, ss);
	}

	/* FIXME: move into no refine flags section? */
	if (screenshots ->len > 0)
		gs_app_add_kudo (app, GS_APP_KUDO_HAS_SCREENSHOTS);

	/* success */
	return TRUE;
}

static gboolean
gs_appstream_refine_add_provides (GsApp *app, XbNode *component, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) provides = NULL;

	/* get all components */
	provides = xb_node_query (component, "provides/*", 0, &error_local);
	if (provides == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < provides->len; i++) {
		XbNode *provide = g_ptr_array_index (provides, i);
		g_autoptr(AsProvide) pr = as_provide_new ();
		as_provide_set_kind (pr, as_provide_kind_from_string (xb_node_get_element (provide)));
		as_provide_set_value (pr, xb_node_get_text (provide));
		gs_app_add_provide (app, pr);
	}

	/* success */
	return TRUE;
}

static gboolean
gs_appstream_is_recent_release (XbNode *component)
{
	guint64 ts;
	guint64 secs;

	/* get newest release */
	ts = xb_node_query_attr_as_uint (component, "releases/release", "timestamp", NULL);
	if (ts == G_MAXUINT64)
		return FALSE;

	/* is last build less than one year ago? */
	secs = ((guint64) g_get_real_time () / G_USEC_PER_SEC) - ts;
	return secs / (60 * 60 * 24) < 365;
}

static gboolean
gs_appstream_copy_metadata (GsApp *app, XbNode *component, GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) values = NULL;

	/* get all components */
	values = xb_node_query (component, "custom/value", 0, &error_local);
	if (values == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < values->len; i++) {
		XbNode *value = g_ptr_array_index (values, i);
		const gchar *key = xb_node_get_attr (value, "key");
		if (key == NULL)
			continue;
		if (gs_app_get_metadata_item (app, key) != NULL)
			continue;
		gs_app_set_metadata (app, key, xb_node_get_text (value));
	}
	return TRUE;
}

static gboolean
gs_appstream_refine_app_updates (GsPlugin *plugin,
				 GsApp *app,
				 XbSilo *silo,
				 XbNode *component,
				 GError **error)
{
	AsUrgencyKind urgency_best = AS_URGENCY_KIND_UNKNOWN;
	g_autofree gchar *xpath = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GHashTable) installed = g_hash_table_new (g_str_hash, g_str_equal);
	g_autoptr(GPtrArray) releases_inst = NULL;
	g_autoptr(GPtrArray) releases = NULL;
	g_autoptr(GPtrArray) updates_list = g_ptr_array_new ();

	/* only for UPDATABLE apps */
	if (!gs_app_is_updatable (app))
		return TRUE;

	/* find out which releases are already installed */
	xpath = g_strdup_printf ("component/id[text()='%s']/../releases/*[@version]",
				 gs_app_get_id (app));
	releases_inst = xb_silo_query (silo, xpath, 0, &error_local);
	if (releases_inst == NULL) {
		if (!g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) &&
		    !g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)) {
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
	} else {
		for (guint i = 0; i < releases_inst->len; i++) {
			XbNode *release = g_ptr_array_index (releases_inst, i);
			g_hash_table_insert (installed,
					     (gpointer) xb_node_get_attr (release, "version"),
					     (gpointer) release);
		}
	}
	g_clear_error (&error_local);

	/* get all components */
	releases = xb_node_query (component, "releases/*", 0, &error_local);
	if (releases == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < releases->len; i++) {
		XbNode *release = g_ptr_array_index (releases, i);
		const gchar *version = xb_node_get_attr (release, "version");
		g_autoptr(XbNode) description = NULL;
		AsUrgencyKind urgency_tmp;

		/* ignore releases with no version */
		if (version == NULL)
			continue;

		/* already installed */
		if (g_hash_table_lookup (installed, version) != NULL)
			continue;

		/* limit this to three versions backwards if there has never
		 * been a detected installed version */
		if (g_hash_table_size (installed) == 0 && i >= 3)
			break;

		/* use the 'worst' urgency, e.g. critical over enhancement */
		urgency_tmp = as_urgency_kind_from_string (xb_node_get_attr (release, "urgency"));
		if (urgency_tmp > urgency_best)
			urgency_best = urgency_tmp;

		/* add updates with a description */
		description = xb_node_query_first (release, "description", NULL);
		if (description == NULL)
			continue;
		g_ptr_array_add (updates_list, release);
	}

	/* only set if known */
	if (urgency_best != AS_URGENCY_KIND_UNKNOWN)
		gs_app_set_update_urgency (app, urgency_best);

	/* no prefix on each release */
	if (updates_list->len == 1) {
		XbNode *release = g_ptr_array_index (updates_list, 0);
		g_autoptr(XbNode) n = NULL;
		g_autofree gchar *desc = NULL;
		n = xb_node_query_first (release, "description", NULL);
		desc = gs_appstream_format_description (n, NULL);
		gs_app_set_update_details (app, desc);

	/* get the descriptions with a version prefix */
	} else if (updates_list->len > 1) {
		g_autoptr(GString) update_desc = g_string_new ("");
		for (guint i = 0; i < updates_list->len; i++) {
			XbNode *release = g_ptr_array_index (updates_list, i);
			g_autofree gchar *desc = NULL;
			g_autoptr(XbNode) n = NULL;

			n = xb_node_query_first (release, "description", NULL);
			desc = gs_appstream_format_description (n, NULL);
			g_string_append_printf (update_desc,
						"Version %s:\n%s\n\n",
						xb_node_get_attr (release, "version"),
						desc);
		}

		/* remove trailing newlines */
		if (update_desc->len > 2)
			g_string_truncate (update_desc, update_desc->len - 2);
		gs_app_set_update_details (app, update_desc->str);
	}

	/* if there is no already set update version use the newest */
	if (gs_app_get_update_version (app) == NULL &&
	    updates_list->len > 0) {
		XbNode *release = g_ptr_array_index (updates_list, 0);
		gs_app_set_update_version (app, xb_node_get_attr (release, "version"));
	}

	/* success */
	return TRUE;
}

/**
 * _gs_utils_locale_has_translations:
 * @locale: A locale, e.g. `en_GB` or `uz_UZ.utf8@cyrillic`
 *
 * Looks up if the locale is likely to have translations.
 *
 * Returns: %TRUE if the locale should have translations
 **/
static gboolean
_gs_utils_locale_has_translations (const gchar *locale)
{
	g_autofree gchar *locale_copy = g_strdup (locale);
	gchar *separator;

	/* Strip off the codeset and modifier, if present. */
	separator = strpbrk (locale_copy, ".@");
	if (separator != NULL)
		*separator = '\0';

	if (g_strcmp0 (locale_copy, "C") == 0)
		return FALSE;
	if (g_strcmp0 (locale_copy, "en") == 0)
		return FALSE;
	if (g_strcmp0 (locale_copy, "en_US") == 0)
		return FALSE;
	return TRUE;
}

static gboolean
gs_appstream_origin_valid (const gchar *origin)
{
	if (origin == NULL)
		return FALSE;
	if (g_strcmp0 (origin, "") == 0)
		return FALSE;
	return TRUE;
}

static gboolean
gs_appstream_is_valid_project_group (const gchar *project_group)
{
	if (project_group == NULL)
		return FALSE;
	return as_utils_is_environment_id (project_group);
}

static gboolean
gs_appstream_refine_app_content_rating (GsPlugin *plugin,
					GsApp *app,
					XbNode *content_rating,
					GError **error)
{
	g_autoptr(AsContentRating) cr = as_content_rating_new ();
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) content_attributes = NULL;
	const gchar *content_rating_kind = NULL;

	/* get kind */
	content_rating_kind = xb_node_get_attr (content_rating, "type");
	/* we only really expect/support OARS 1.0 and 1.1 */
	if (content_rating_kind == NULL ||
	    (g_strcmp0 (content_rating_kind, "oars-1.0") != 0 &&
	     g_strcmp0 (content_rating_kind, "oars-1.1") != 0)) {
		return TRUE;
	}

	as_content_rating_set_kind (cr, content_rating_kind);

	/* get attributes; no attributes being found (i.e.
	 * `<content_rating type="*"/>`) is OK: it means that all attributes have
	 * value `none`, as per the
	 * [OARS semantics](https://github.com/hughsie/oars/blob/master/specification/oars-1.1.md) */
	content_attributes = xb_node_query (content_rating, "content_attribute", 0, &error_local);
	if (content_attributes == NULL &&
	    g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
		g_clear_error (&error_local);
	} else if (content_attributes == NULL &&
		 g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT)) {
		return TRUE;
	} else if (content_attributes == NULL) {
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}

	for (guint i = 0; content_attributes != NULL && i < content_attributes->len; i++) {
		XbNode *content_attribute = g_ptr_array_index (content_attributes, i);
		as_content_rating_add_attribute (cr,
						 xb_node_get_attr (content_attribute, "id"),
						 as_content_rating_value_from_string (xb_node_get_text (content_attribute)));
	}

	gs_app_set_content_rating (app, cr);
	return TRUE;
}

static gboolean
gs_appstream_refine_app_content_ratings (GsPlugin *plugin,
					 GsApp *app,
					 XbNode *component,
					 GError **error)
{
	g_autoptr(GPtrArray) content_ratings = NULL;
	g_autoptr(GError) error_local = NULL;

	/* find any content ratings */
	content_ratings = xb_node_query (component, "content_rating", 0, &error_local);
	if (content_ratings == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < content_ratings->len; i++) {
		XbNode *content_rating = g_ptr_array_index (content_ratings, i);
		if (!gs_appstream_refine_app_content_rating (plugin, app, content_rating, error))
			return FALSE;
	}
	return TRUE;
}

gboolean
gs_appstream_refine_app (GsPlugin *plugin,
			 GsApp *app,
			 XbSilo *silo,
			 XbNode *component,
			 GsPluginRefineFlags refine_flags,
			 GError **error)
{
	const gchar *tmp;
	g_autoptr(GPtrArray) bundles = NULL;
	g_autoptr(GPtrArray) launchables = NULL;
	g_autoptr(XbNode) req = NULL;

	/* is compatible */
	req = xb_node_query_first (component,
				   "requires/id[@type='id']"
				   "[text()='org.gnome.Software.desktop']", NULL);
	if (req != NULL) {
#if AS_CHECK_VERSION(0,7,15)
		gint rc = as_utils_vercmp_full (xb_node_get_attr (req, "version"),
		                                PACKAGE_VERSION,
		                                AS_VERSION_COMPARE_FLAG_NONE);
#else
		gint rc = as_utils_vercmp (xb_node_get_attr (req, "version"),
		                           PACKAGE_VERSION);
#endif
		if (rc > 0) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "not for this gnome-software");
			return FALSE;
		}
	}

	/* types we can never launch */
	switch (gs_app_get_kind (app)) {
	case AS_APP_KIND_ADDON:
	case AS_APP_KIND_CODEC:
	case AS_APP_KIND_DRIVER:
	case AS_APP_KIND_FIRMWARE:
	case AS_APP_KIND_FONT:
	case AS_APP_KIND_GENERIC:
	case AS_APP_KIND_INPUT_METHOD:
	case AS_APP_KIND_LOCALIZATION:
	case AS_APP_KIND_OS_UPDATE:
	case AS_APP_KIND_OS_UPGRADE:
	case AS_APP_KIND_RUNTIME:
	case AS_APP_KIND_SOURCE:
		gs_app_add_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);
		break;
	default:
		break;
	}

	/* check if the special metadata affects the not-launchable quirk */
	tmp = gs_app_get_metadata_item (app, "GnomeSoftware::quirks::not-launchable");
	if (tmp != NULL) {
		if (g_strcmp0 (tmp, "true") == 0)
			gs_app_add_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);
		else if (g_strcmp0 (tmp, "false") == 0)
			gs_app_remove_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);
	}

	tmp = gs_app_get_metadata_item (app, "GnomeSoftware::quirks::hide-everywhere");
	if (tmp != NULL) {
		if (g_strcmp0 (tmp, "true") == 0)
			gs_app_add_quirk (app, GS_APP_QUIRK_HIDE_EVERYWHERE);
		else if (g_strcmp0 (tmp, "false") == 0)
			gs_app_remove_quirk (app, GS_APP_QUIRK_HIDE_EVERYWHERE);
	}

	/* try to detect old-style AppStream 'override'
	 * files without the merge attribute */
	if (xb_node_query_text (component, "name", NULL) == NULL &&
	    xb_node_query_text (component, "metadata_license", NULL) == NULL) {
		gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
	}

	/* set id */
	tmp = xb_node_query_text (component, "id", NULL);
	if (tmp != NULL && gs_app_get_id (app) == NULL)
		gs_app_set_id (app, tmp);

	/* set source */
	tmp = xb_node_query_text (component, "../info/filename", NULL);
	if (tmp != NULL && gs_app_get_metadata_item (app, "appstream::source-file") == NULL) {
		gs_app_set_metadata (app, "appstream::source-file", tmp);
	}

	/* set scope */
	tmp = xb_node_query_text (component, "../info/scope", NULL);
	if (tmp != NULL)
		gs_app_set_scope (app, as_app_scope_from_string (tmp));

	/* set content rating */
	if (TRUE) {
		if (!gs_appstream_refine_app_content_ratings (plugin, app, component, error))
			return FALSE;
	}

	/* set name */
	tmp = xb_node_query_text (component, "name", NULL);
	if (tmp != NULL)
		gs_app_set_name (app, GS_APP_QUALITY_HIGHEST, tmp);

	/* set summary */
	tmp = xb_node_query_text (component, "summary", NULL);
	if (tmp != NULL)
		gs_app_set_summary (app, GS_APP_QUALITY_HIGHEST, tmp);

	/* add urls */
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL) {
		g_autoptr(GPtrArray) urls = NULL;
		urls = xb_node_query (component, "url", 0, NULL);
		if (urls != NULL) {
			for (guint i = 0; i < urls->len; i++) {
				XbNode *url = g_ptr_array_index (urls, i);
				const gchar *kind = xb_node_get_attr (url, "type");
				if (kind == NULL)
					continue;
				gs_app_set_url (app,
						as_url_kind_from_string (kind),
						xb_node_get_text (url));
			}
		}
	}

	/* add launchables */
	launchables = xb_node_query (component, "launchable", 0, NULL);
	if (launchables != NULL) {
		for (guint i = 0; i < launchables->len; i++) {
			XbNode *launchable = g_ptr_array_index (launchables, i);
			const gchar *kind = xb_node_get_attr (launchable, "type");
			if (g_strcmp0 (kind, "desktop-id") == 0) {
				gs_app_set_launchable (app,
						       AS_LAUNCHABLE_KIND_DESKTOP_ID,
						       xb_node_get_text (launchable));
				break;
			} else if (g_strcmp0 (kind, "url") == 0) {
				gs_app_set_launchable (app,
						       AS_LAUNCHABLE_KIND_URL,
						       xb_node_get_text (launchable));
			}
		}
	}

	/* set license */
	if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE) > 0 &&
	    gs_app_get_license (app) == NULL) {
		tmp = xb_node_query_text (component, "project_license", NULL);
		if (tmp != NULL)
			gs_app_set_license (app, GS_APP_QUALITY_HIGHEST, tmp);
	}

	/* set description */
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION) {
		g_autofree gchar *description = NULL;
		g_autoptr(XbNode) n = xb_node_query_first (component, "description", NULL);
		if (n != NULL)
			description = gs_appstream_format_description (n, NULL);
		if (description != NULL)
			gs_app_set_description (app, GS_APP_QUALITY_HIGHEST, description);
	}

	/* set icon */
	if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON) > 0 &&
	    gs_app_get_icons(app)->len == 0)
		gs_appstream_refine_icon (plugin, app, component);

	/* set categories */
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_CATEGORIES) {
		g_autoptr(GPtrArray) categories = NULL;
		categories = xb_node_query (component, "categories/category", 0, NULL);
		if (categories != NULL) {
			for (guint i = 0; i < categories->len; i++) {
				XbNode *category = g_ptr_array_index (categories, i);
				gs_app_add_category (app, xb_node_get_text (category));

				/* Special case: We used to use the `Blacklisted`
				 * category to hide apps from their .desktop
				 * file or appdata. We now use a quirk for that.
				 * This special case can be removed when all
				 * appstream files no longer use the `Blacklisted`
				 * category (including external-appstream files
				 * put together by distributions). */
				if (g_strcmp0 (xb_node_get_text (category), "Blacklisted") == 0)
					gs_app_add_quirk (app, GS_APP_QUIRK_HIDE_EVERYWHERE);
			}
		}
	}

	/* set project group */
	if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROJECT_GROUP) > 0 &&
	    gs_app_get_project_group (app) == NULL) {
		tmp = xb_node_query_text (component, "project_group", NULL);
		if (tmp != NULL && gs_appstream_is_valid_project_group (tmp))
			gs_app_set_project_group (app, tmp);
	}

	/* set developer name */
	if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_DEVELOPER_NAME) > 0 &&
	    gs_app_get_developer_name (app) == NULL) {
		tmp = xb_node_query_text (component, "developer_name", NULL);
		if (tmp != NULL)
			gs_app_set_developer_name (app, tmp);
	}

	/* set id kind */
	if (gs_app_get_kind (app) == AS_APP_KIND_UNKNOWN ||
	    gs_app_get_kind (app) == AS_APP_KIND_GENERIC) {
		tmp = xb_node_get_attr (component, "type");
		gs_app_set_kind (app, as_app_kind_from_string (tmp));
	}

	/* copy all the metadata */
	if (!gs_appstream_copy_metadata (app, component, error))
		return FALSE;

	/* add bundles */
	bundles = xb_node_query (component, "bundle", 0, NULL);
	if (bundles != NULL && gs_app_get_sources(app)->len == 0) {
		for (guint i = 0; i < bundles->len; i++) {
			XbNode *bundle = g_ptr_array_index (bundles, i);
			const gchar *kind = xb_node_get_attr (bundle, "type");
			const gchar *bundle_id = xb_node_get_text (bundle);

			if (bundle_id == NULL || kind == NULL)
				continue;

			gs_app_add_source (app, bundle_id);
			gs_app_set_bundle_kind (app, as_bundle_kind_from_string (kind));

			/* get the type/name/arch/branch */
			if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_FLATPAK) {
				g_auto(GStrv) split = g_strsplit (bundle_id, "/", -1);
				if (g_strv_length (split) != 4) {
					g_set_error (error,
						     GS_PLUGIN_ERROR,
						     GS_PLUGIN_ERROR_NOT_SUPPORTED,
						     "invalid ID %s for a flatpak ref",
						     bundle_id);
					return FALSE;
				}

				/* we only need the branch for the unique ID */
				gs_app_set_branch (app, split[3]);
			}
		}
	}

	/* add legacy package names */
	if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_UNKNOWN) {
		g_autoptr(GPtrArray) pkgnames = NULL;
		pkgnames = xb_node_query (component, "pkgname", 0, NULL);
		if (pkgnames != NULL && gs_app_get_sources(app)->len == 0) {
			for (guint i = 0; i < pkgnames->len; i++) {
				XbNode *pkgname = g_ptr_array_index (pkgnames, i);
				tmp = xb_node_get_text (pkgname);
				if (tmp != NULL && tmp[0] != '\0')
					gs_app_add_source (app, tmp);
			}
			gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
		}
	}

	/* set origin for flatpaks */
	if (gs_app_get_origin (app) == NULL &&
	    gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_FLATPAK) {
		g_autoptr(XbNode) parent = xb_node_get_parent (component);
		if (parent != NULL) {
			tmp = xb_node_get_attr (parent, "origin");
			gs_app_set_origin (app, tmp);
		}
	}

	/* set addons */
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS) {
		if (!gs_appstream_refine_add_addons (plugin, app, silo, error))
			return FALSE;
	}

	/* set screenshots */
	if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SCREENSHOTS) > 0 &&
	    gs_app_get_screenshots(app)->len == 0) {
		if (!gs_appstream_refine_add_screenshots (app, component, error))
			return FALSE;
	}

	/* set provides */
	if (!gs_appstream_refine_add_provides (app, component, error))
		return FALSE;

	/* add kudos */
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_KUDOS) {
		g_autoptr(GPtrArray) kudos = NULL;
		tmp = gs_plugin_get_locale (plugin);
		if (!_gs_utils_locale_has_translations (tmp)) {
			gs_app_add_kudo (app, GS_APP_KUDO_MY_LANGUAGE);
		} else {

			g_autoptr(GString) xpath = g_string_new (NULL);
			g_auto(GStrv) variants = g_get_locale_variants (tmp);

			/* @variants includes @tmp */
			for (gsize i = 0; variants[i] != NULL; i++)
				xb_string_append_union (xpath, "languages/lang[text()='%s'][@percentage>50]", variants[i]);

			if (xb_node_query_text (component, xpath->str, NULL) != NULL)
				gs_app_add_kudo (app, GS_APP_KUDO_MY_LANGUAGE);
		}

		/* any keywords */
		if (xb_node_query_text (component, "keywords/keyword", NULL) != NULL)
			gs_app_add_kudo (app, GS_APP_KUDO_HAS_KEYWORDS);

		/* HiDPI icon */
		if (xb_node_query_text (component, "icon[@width='128']", NULL) != NULL)
			gs_app_add_kudo (app, GS_APP_KUDO_HI_DPI_ICON);

		/* was this application released recently */
		if (gs_appstream_is_recent_release (component))
			gs_app_add_kudo (app, GS_APP_KUDO_RECENT_RELEASE);

		/* add a kudo to featured and popular apps */
		if (xb_node_query_text (component, "kudos/kudo[text()='GnomeSoftware::popular']", NULL) != NULL)
			gs_app_add_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED);
		if (xb_node_query_text (component, "categories/category[text()='Featured']", NULL) != NULL)
			gs_app_add_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED);

		/* add new-style kudos */
		kudos = xb_node_query (component, "kudos/kudo", 0, NULL);
		for (guint i = 0; kudos != NULL && i < kudos->len; i++) {
			XbNode *kudo = g_ptr_array_index (kudos, i);
			switch (as_kudo_kind_from_string (xb_node_get_text (kudo))) {
			case AS_KUDO_KIND_SEARCH_PROVIDER:
				gs_app_add_kudo (app, GS_APP_KUDO_SEARCH_PROVIDER);
				break;
			case AS_KUDO_KIND_USER_DOCS:
				gs_app_add_kudo (app, GS_APP_KUDO_INSTALLS_USER_DOCS);
				break;
			case AS_KUDO_KIND_MODERN_TOOLKIT:
				gs_app_add_kudo (app, GS_APP_KUDO_MODERN_TOOLKIT);
				break;
			case AS_KUDO_KIND_NOTIFICATIONS:
				gs_app_add_kudo (app, GS_APP_KUDO_USES_NOTIFICATIONS);
				break;
			case AS_KUDO_KIND_HIGH_CONTRAST:
				gs_app_add_kudo (app, GS_APP_KUDO_HIGH_CONTRAST);
				break;
			case AS_KUDO_KIND_HI_DPI_ICON:
				gs_app_add_kudo (app, GS_APP_KUDO_HI_DPI_ICON);
				break;
			default:
				break;
			}
		}
	}

	/* we have an origin in the XML */
	if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ORIGIN) > 0 &&
	    gs_app_get_origin_appstream (app) == NULL) {
		g_autoptr(XbNode) parent = xb_node_get_parent (component);
		if (parent != NULL) {
			tmp = xb_node_get_attr (parent, "origin");
			if (gs_appstream_origin_valid (tmp))
				gs_app_set_origin_appstream (app, tmp);
		}
	}

	/* is there any update information */
	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS) {
		if (!gs_appstream_refine_app_updates (plugin,
						      app,
						      silo,
						      component,
						      error))
			return FALSE;
	}

	return TRUE;
}

typedef struct {
	AsAppSearchMatch	 match_value;
	XbQuery			*query;
} GsAppstreamSearchHelper;

static void
gs_appstream_search_helper_free (GsAppstreamSearchHelper *helper)
{
	g_object_unref (helper->query);
	g_free (helper);
}

static guint16
gs_appstream_silo_search_component2 (GPtrArray *array, XbNode *component, const gchar *search)
{
	guint16 match_value = 0;

	/* do searches */
	for (guint i = 0; i < array->len; i++) {
		g_autoptr(GPtrArray) n = NULL;
		GsAppstreamSearchHelper *helper = g_ptr_array_index (array, i);
		xb_query_bind_str (helper->query, 0, search, NULL);
		n = xb_node_query_full (component, helper->query, NULL);
		if (n != NULL)
			match_value |= helper->match_value;
	}
	return match_value;
}

static guint16
gs_appstream_silo_search_component (GPtrArray *array, XbNode *component, const gchar * const *search)
{
	guint16 matches_sum = 0;

	/* do *all* search keywords match */
	for (guint i = 0; search[i] != NULL; i++) {
		guint tmp = gs_appstream_silo_search_component2 (array, component, search[i]);
		if (tmp == 0)
			return 0;
		matches_sum |= tmp;
	}
	return matches_sum;
}

gboolean
gs_appstream_search (GsPlugin *plugin,
		     XbSilo *silo,
		     const gchar * const *values,
		     GsAppList *list,
		     GCancellable *cancellable,
		     GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func ((GDestroyNotify) gs_appstream_search_helper_free);
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GTimer) timer = g_timer_new ();
	const struct {
		AsAppSearchMatch	 match_value;
		const gchar		*xpath;
	} queries[] = {
		{ AS_APP_SEARCH_MATCH_MIMETYPE,	"mimetypes/mimetype[text()~=stem(?)]" },
		{ AS_APP_SEARCH_MATCH_PKGNAME,	"pkgname[text()~=stem(?)]" },
		{ AS_APP_SEARCH_MATCH_COMMENT,	"summary[text()~=stem(?)]" },
		{ AS_APP_SEARCH_MATCH_NAME,	"name[text()~=stem(?)]" },
		{ AS_APP_SEARCH_MATCH_KEYWORD,	"keywords/keyword[text()~=stem(?)]" },
		{ AS_APP_SEARCH_MATCH_ID,	"id[text()~=stem(?)]" },
		{ AS_APP_SEARCH_MATCH_ID,	"launchable[text()~=stem(?)]" },
		{ AS_APP_SEARCH_MATCH_ORIGIN,	"../components[@origin~=stem(?)]" },
		{ AS_APP_SEARCH_MATCH_NONE,	NULL }
	};

	/* add some weighted queries */
	for (guint i = 0; queries[i].xpath != NULL; i++) {
		g_autoptr(GError) error_query = NULL;
		g_autoptr(XbQuery) query = xb_query_new (silo, queries[i].xpath, &error_query);
		if (query != NULL) {
			GsAppstreamSearchHelper *helper = g_new0 (GsAppstreamSearchHelper, 1);
			helper->match_value = queries[i].match_value;
			helper->query = g_steal_pointer (&query);
			g_ptr_array_add (array, helper);
		} else {
			g_debug ("ignoring: %s", error_query->message);
		}
	}

	/* get all components */
	components = xb_silo_query (silo, "components/component", 0, &error_local);
	if (components == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);
		guint16 match_value = gs_appstream_silo_search_component (array, component, values);
		if (match_value != 0) {
			g_autoptr(GsApp) app = gs_appstream_create_app (plugin, silo, component, error);
			if (app == NULL)
				return FALSE;
			if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD)) {
				g_debug ("not returning wildcard %s",
					 gs_app_get_unique_id (app));
				continue;
			}
			g_debug ("add %s", gs_app_get_unique_id (app));
			gs_app_set_match_value (app, match_value);
			gs_app_list_add (list, app);
		}
	}
	g_debug ("search took %fms", g_timer_elapsed (timer, NULL) * 1000);
	return TRUE;
}

gboolean
gs_appstream_add_category_apps (GsPlugin *plugin,
				XbSilo *silo,
				GsCategory *category,
				GsAppList *list,
				GCancellable *cancellable,
				GError **error)
{
	GPtrArray *desktop_groups;
	g_autoptr(GError) error_local = NULL;

	desktop_groups = gs_category_get_desktop_groups (category);
	if (desktop_groups->len == 0) {
		g_warning ("no desktop_groups for %s", gs_category_get_id (category));
		return TRUE;
	}
	for (guint j = 0; j < desktop_groups->len; j++) {
		const gchar *desktop_group = g_ptr_array_index (desktop_groups, j);
		g_autofree gchar *xpath = NULL;
		g_auto(GStrv) split = g_strsplit (desktop_group, "::", -1);
		g_autoptr(GPtrArray) components = NULL;

		/* generate query */
		if (g_strv_length (split) == 1) {
			xpath = g_strdup_printf ("components/component/categories/"
						 "category[text()='%s']/../..",
						 split[0]);
		} else if (g_strv_length (split) == 2) {
			xpath = g_strdup_printf ("components/component/categories/"
						 "category[text()='%s']/../"
						 "category[text()='%s']/../..",
						 split[0], split[1]);
		}
		components = xb_silo_query (silo, xpath, 0, &error_local);
		if (components == NULL) {
			if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
				return TRUE;
			if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
				return TRUE;
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}

		/* create app */
		for (guint i = 0; i < components->len; i++) {
			XbNode *component = g_ptr_array_index (components, i);
			g_autoptr(GsApp) app = NULL;
			const gchar *id = xb_node_query_text (component, "id", NULL);
			if (id == NULL)
				continue;
			app = gs_app_new (id);
			gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
			gs_app_list_add (list, app);
		}

	}
	return TRUE;
}

static guint
gs_appstream_count_component_for_groups (GsPlugin *plugin, XbSilo *silo, const gchar *desktop_group)
{
	guint limit = 10;
	g_autofree gchar *xpath = NULL;
	g_auto(GStrv) split = g_strsplit (desktop_group, "::", -1);
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GError) error_local = NULL;

	if (g_strv_length (split) == 1) { /* "all" group for a parent category */
		xpath = g_strdup_printf ("components/component/categories/"
					 "category[text()='%s']/../..",
					 split[0]);
	} else if (g_strv_length (split) == 2) {
		xpath = g_strdup_printf ("components/component/categories/"
					 "category[text()='%s']/../"
					 "category[text()='%s']/../..",
					 split[0], split[1]);
	} else {
		return 0;
	}

	array = xb_silo_query (silo, xpath, limit, &error_local);
	if (array == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return 0;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return 0;
		g_warning ("%s", error_local->message);
		return 0;
	}
	return array->len;
}

/* we're not actually adding categories here, we're just setting the number of
 * applications available in each category */
gboolean
gs_appstream_add_categories (GsPlugin *plugin,
			     XbSilo *silo,
			     GPtrArray *list,
			     GCancellable *cancellable,
			     GError **error)
{
	for (guint j = 0; j < list->len; j++) {
		GsCategory *parent = GS_CATEGORY (g_ptr_array_index (list, j));
		GPtrArray *children = gs_category_get_children (parent);

		for (guint i = 0; i < children->len; i++) {
			GsCategory *cat = g_ptr_array_index (children, i);
			GPtrArray *groups = gs_category_get_desktop_groups (cat);
			for (guint k = 0; k < groups->len; k++) {
				const gchar *group = g_ptr_array_index (groups, k);
				guint cnt = gs_appstream_count_component_for_groups (plugin, silo, group);
				for (guint l = 0; l < cnt; l++) {
					gs_category_increment_size (parent);
					if (children->len > 1) {
						/* Parent category has multiple groups, so increment
						 * each group's size too */
						gs_category_increment_size (cat);
					}
				}
			}
		}
		continue;
	}
	return TRUE;
}

gboolean
gs_appstream_add_popular (GsPlugin *plugin,
			  XbSilo *silo,
			  GsAppList *list,
			  GCancellable *cancellable,
			  GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) array = NULL;

	/* find out how many packages are in each category */
	array = xb_silo_query (silo,
			       "components/component/kudos/"
			       "kudo[text()='GnomeSoftware::popular']/../..",
			       0, &error_local);
	if (array == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < array->len; i++) {
		g_autoptr(GsApp) app = NULL;
		XbNode *component = g_ptr_array_index (array, i);
		const gchar *component_id = xb_node_query_text (component, "id", NULL);
		if (component_id == NULL)
			continue;
		app = gs_app_new (component_id);
		gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
		gs_app_list_add (list, app);
	}
	return TRUE;
}

gboolean
gs_appstream_add_recent (GsPlugin *plugin,
			 XbSilo *silo,
			 GsAppList *list,
			 guint64 age,
			 GCancellable *cancellable,
			 GError **error)
{
	guint64 now = (guint64) g_get_real_time () / G_USEC_PER_SEC;
	g_autofree gchar *xpath = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) array = NULL;

	/* use predicate conditions to the max */
	xpath = g_strdup_printf ("components/component/releases/"
				 "release[@timestamp>%" G_GUINT64_FORMAT "]/../..",
				 now - (30 * 24 * 60 * 60));
	array = xb_silo_query (silo, xpath, 0, &error_local);
	if (array == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < array->len; i++) {
		XbNode *component = g_ptr_array_index (array, i);
		g_autoptr(GsApp) app = gs_appstream_create_app (plugin, silo, component, error);
		if (app == NULL)
			return FALSE;
		gs_app_list_add (list, app);
	}
	return TRUE;
}

gboolean
gs_appstream_add_alternates (GsPlugin *plugin,
			     XbSilo *silo,
			     GsApp *app,
			     GsAppList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	GPtrArray *sources = gs_app_get_sources (app);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) ids = NULL;
	g_autoptr(GString) xpath = g_string_new (NULL);

	/* probably a package we know nothing about */
	if (gs_app_get_id (app) == NULL)
		return TRUE;

	/* actual ID */
	xb_string_append_union (xpath, "components/component/id[text()='%s']",
				gs_app_get_id (app));

	/* new ID -> old ID */
	xb_string_append_union (xpath, "components/component/id[text()='%s']/../provides/id",
				gs_app_get_id (app));

	/* old ID -> new ID */
	xb_string_append_union (xpath, "components/component/provides/id[text()='%s']/../../id",
				gs_app_get_id (app));

	/* find apps that use the same pkgname */
	for (guint j = 0; j < sources->len; j++) {
		const gchar *source = g_ptr_array_index (sources, j);
		g_autofree gchar *source_safe = xb_string_escape (source);
		xb_string_append_union (xpath,
					"components/component/pkgname[text()='%s']/../id",
					source_safe);
	}

	/* do a big query, and return all the unique results */
	ids = xb_silo_query (silo, xpath->str, 0, &error_local);
	if (ids == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < ids->len; i++) {
		XbNode *n = g_ptr_array_index (ids, i);
		g_autoptr(GsApp) app2 = NULL;
		app2 = gs_app_new (xb_node_get_text (n));
		gs_app_add_quirk (app2, GS_APP_QUIRK_IS_WILDCARD);
		gs_app_list_add (list, app2);
	}
	return TRUE;
}

gboolean
gs_appstream_add_featured (GsPlugin *plugin,
			   XbSilo *silo,
			   GsAppList *list,
			   GCancellable *cancellable,
			   GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) array = NULL;

	/* find out how many packages are in each category */
	array = xb_silo_query (silo,
			       "components/component/custom/"
			       "value[@key='GnomeSoftware::FeatureTile-css']/../..",
			       0, &error_local);
	if (array == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < array->len; i++) {
		g_autoptr(GsApp) app = NULL;
		XbNode *component = g_ptr_array_index (array, i);
		const gchar *component_id = xb_node_query_text (component, "id", NULL);
		if (component_id == NULL)
			continue;
		app = gs_app_new (component_id);
		gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
		if (!gs_appstream_copy_metadata (app, component, error))
			return FALSE;
		gs_app_list_add (list, app);
	}
	return TRUE;
}

void
gs_appstream_component_add_keyword (XbBuilderNode *component, const gchar *str)
{
	g_autoptr(XbBuilderNode) keyword = NULL;
	g_autoptr(XbBuilderNode) keywords = NULL;

	/* create <keywords> if it does not already exist */
	keywords = xb_builder_node_get_child (component, "keywords", NULL);
	if (keywords == NULL)
		keywords = xb_builder_node_insert (component, "keywords", NULL);

	/* create <keyword>str</keyword> if it does not already exist */
	keyword = xb_builder_node_get_child (keywords, "keyword", str);
	if (keyword == NULL) {
		keyword = xb_builder_node_insert (keywords, "keyword", NULL);
		xb_builder_node_set_text (keyword, str, -1);
	}
}

void
gs_appstream_component_add_provide (XbBuilderNode *component, const gchar *str)
{
	g_autoptr(XbBuilderNode) provide = NULL;
	g_autoptr(XbBuilderNode) provides = NULL;

	/* create <provides> if it does not already exist */
	provides = xb_builder_node_get_child (component, "provides", NULL);
	if (provides == NULL)
		provides = xb_builder_node_insert (component, "provides", NULL);

	/* create <id>str</id> if it does not already exist */
	provide = xb_builder_node_get_child (provides, "id", str);
	if (provide == NULL) {
		provide = xb_builder_node_insert (provides, "id", NULL);
		xb_builder_node_set_text (provide, str, -1);
	}
}

void
gs_appstream_component_add_category (XbBuilderNode *component, const gchar *str)
{
	g_autoptr(XbBuilderNode) category = NULL;
	g_autoptr(XbBuilderNode) categories = NULL;

	/* create <categories> if it does not already exist */
	categories = xb_builder_node_get_child (component, "categories", NULL);
	if (categories == NULL)
		categories = xb_builder_node_insert (component, "categories", NULL);

	/* create <category>str</category> if it does not already exist */
	category = xb_builder_node_get_child (categories, "category", str);
	if (category == NULL) {
		category = xb_builder_node_insert (categories, "category", NULL);
		xb_builder_node_set_text (category, str, -1);
	}
}

void
gs_appstream_component_add_icon (XbBuilderNode *component, const gchar *str)
{
	g_autoptr(XbBuilderNode) icon = NULL;

	/* create <icon>str</icon> if it does not already exist */
	icon = xb_builder_node_get_child (component, "icon", NULL);
	if (icon == NULL) {
		icon = xb_builder_node_insert (component, "icon",
					       "type", "stock",
					       NULL);
		xb_builder_node_set_text (icon, str, -1);
	}
}

void
gs_appstream_component_add_extra_info (GsPlugin *plugin, XbBuilderNode *component)
{
	const gchar *kind = xb_builder_node_get_attr (component, "type");

	/* add the gnome-software-specific 'Addon' group and ensure they
	 * all have an icon set */
	switch (as_app_kind_from_string (kind)) {
	case AS_APP_KIND_WEB_APP:
		gs_appstream_component_add_keyword (component, kind);
		break;
	case AS_APP_KIND_FONT:
		gs_appstream_component_add_category (component, "Addon");
		gs_appstream_component_add_category (component, "Font");
		break;
	case AS_APP_KIND_DRIVER:
		gs_appstream_component_add_category (component, "Addon");
		gs_appstream_component_add_category (component, "Driver");
		gs_appstream_component_add_icon (component, "application-x-firmware-symbolic");
		break;
	case AS_APP_KIND_LOCALIZATION:
		gs_appstream_component_add_category (component, "Addon");
		gs_appstream_component_add_category (component, "Localization");
		gs_appstream_component_add_icon (component, "accessories-dictionary-symbolic");
		break;
	case AS_APP_KIND_CODEC:
		gs_appstream_component_add_category (component, "Addon");
		gs_appstream_component_add_category (component, "Codec");
		gs_appstream_component_add_icon (component, "application-x-addon");
		break;
	case AS_APP_KIND_INPUT_METHOD:
		gs_appstream_component_add_keyword (component, kind);
		gs_appstream_component_add_category (component, "Addon");
		gs_appstream_component_add_category (component, "InputSource");
		gs_appstream_component_add_icon (component, "system-run-symbolic");
		break;
	case AS_APP_KIND_FIRMWARE:
		gs_appstream_component_add_icon (component, "system-run-symbolic");
		break;
	default:
		break;
	}
}
