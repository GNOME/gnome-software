/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2015-2017 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018-2019 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <gnome-software.h>
#include <locale.h>

#include "gs-appstream.h"

#define	GS_APPSTREAM_MAX_SCREENSHOTS	5

GsApp *
gs_appstream_create_app (GsPlugin *plugin,
			 XbSilo *silo,
			 XbNode *component,
			 const gchar *appstream_source_file,
			 AsComponentScope default_scope,
			 GError **error)
{
	GsApp *app;
	g_autoptr(GsApp) app_new = NULL;

	/* The 'plugin' can be NULL, when creating app for --show-metainfo */
	g_return_val_if_fail (XB_IS_SILO (silo), NULL);
	g_return_val_if_fail (XB_IS_NODE (component), NULL);

	app_new = gs_app_new (NULL);

	/* refine enough to get the unique ID */
	if (!gs_appstream_refine_app (plugin, app_new, silo, component,
				      GS_PLUGIN_REFINE_FLAGS_REQUIRE_ID,
				      NULL, appstream_source_file, default_scope, error))
		return NULL;

	/* never add wildcard apps to the plugin cache, and only add to
	 * the cache if it’s available */
	if (gs_app_has_quirk (app_new, GS_APP_QUIRK_IS_WILDCARD) || plugin == NULL)
		return g_steal_pointer (&app_new);

	if (plugin == NULL)
		return g_steal_pointer (&app_new);

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

/* Helper function to do the equivalent of
 *  *node = xb_node_get_next (*node)
 * but with correct reference counting, since xb_node_get_next() returns a new
 * ref. */
static void
node_set_to_next (XbNode **node)
{
	g_autoptr(XbNode) next_node = NULL;

	g_assert (node != NULL);
	g_assert (*node != NULL);

	next_node = xb_node_get_next (*node);
	g_object_unref (*node);
	*node = g_steal_pointer (&next_node);
}

/* Returns escaped text */
static gchar *
gs_appstream_format_description_text (XbNode *node)
{
	g_autoptr(GString) str = g_string_new (NULL);
	const gchar *node_text;

	if (node == NULL)
		return NULL;

	node_text = xb_node_get_text (node);
	if (node_text != NULL && *node_text != '\0') {
		g_autofree gchar *escaped = g_markup_escape_text (node_text, -1);
		g_string_append (str, escaped);
	}

	for (g_autoptr(XbNode) n = xb_node_get_child (node); n != NULL; node_set_to_next (&n)) {
		const gchar *start_elem = "", *end_elem = "";
		g_autofree gchar *text = NULL;
		if (g_strcmp0 (xb_node_get_element (n), "em") == 0) {
			start_elem = "<i>";
			end_elem = "</i>";
		} else if (g_strcmp0 (xb_node_get_element (n), "code") == 0) {
			start_elem = "<tt>";
			end_elem = "</tt>";
		}

		/* These can be nested */
		text = gs_appstream_format_description_text (n);
		if (text != NULL) {
			g_string_append_printf (str, "%s%s%s", start_elem, text, end_elem);
		}

		node_text = xb_node_get_tail (n);
		if (node_text != NULL && *node_text != '\0') {
			g_autofree gchar *escaped = g_markup_escape_text (node_text, -1);
			g_string_append (str, escaped);
		}
	}

	if (str->len == 0)
		return NULL;

	return g_string_free (g_steal_pointer (&str), FALSE);
}

static void
format_issue_link (GString     *str,
                   const gchar *issue_content,
                   AsIssueKind  kind,
                   const gchar *url)
{
	g_autofree gchar *escaped_text = NULL;

	if (url != NULL) {
		escaped_text = g_markup_printf_escaped ("<a href=\"%s\" title=\"%s\">%s</a>",
							url, url, issue_content);
		g_string_append (str, escaped_text);
		return;
	}

	switch (kind) {
	case AS_ISSUE_KIND_CVE:
		#define CVE_URL "https://cve.mitre.org/cgi-bin/cvename.cgi?name="
		/* @issue_content is expected to be in the form ‘CVE-2023-12345’ */
		escaped_text = g_markup_printf_escaped ("<a href=\"" CVE_URL "%s\" title=\"" CVE_URL "%s\">%s</a>",
							issue_content, issue_content, issue_content);
		#undef CVE_URL
		break;
	case AS_ISSUE_KIND_GENERIC:
	case AS_ISSUE_KIND_UNKNOWN:
	default:
		escaped_text = g_markup_escape_text (issue_content, -1);
		break;
	}

	g_string_append (str, escaped_text);
}

static gchar *
gs_appstream_format_description (XbNode *description_node,
				 XbNode *issues_node)
{
	g_autoptr(GString) str = g_string_new (NULL);

	for (g_autoptr(XbNode) n = description_node ? xb_node_get_child (description_node) : NULL; n != NULL; node_set_to_next (&n)) {
		/* support <p>, <em>, <code>, <ul>, <ol> and <li>, ignore all else */
		if (g_strcmp0 (xb_node_get_element (n), "p") == 0) {
			g_autofree gchar *escaped = gs_appstream_format_description_text (n);
			/* Treat a self-closing paragraph (`<p/>`) as
			 * nonexistent. This is consistent with Firefox. */
			if (escaped != NULL)
				g_string_append_printf (str, "%s\n\n", escaped);
		} else if (g_strcmp0 (xb_node_get_element (n), "ul") == 0) {
			g_autoptr(XbNode) child = NULL;
			g_autoptr(XbNode) next = NULL;
			for (child = xb_node_get_child (n); child != NULL; g_object_unref (child), child = g_steal_pointer (&next)) {
				next = xb_node_get_next (child);
				if (g_strcmp0 (xb_node_get_element (child), "li") == 0) {
					g_autofree gchar *escaped = gs_appstream_format_description_text (child);

					/* Treat a self-closing `<li/>` as an empty
					 * list element (equivalent to `<li></li>`).
					 * This is consistent with Firefox. */
					g_string_append_printf (str, " • %s\n",
								(escaped != NULL) ? escaped : "");
				}
			}
			g_string_append (str, "\n");
		} else if (g_strcmp0 (xb_node_get_element (n), "ol") == 0) {
			g_autoptr(XbNode) child = NULL;
			g_autoptr(XbNode) next = NULL;
			guint i = 0;
			for (child = xb_node_get_child (n); child != NULL; i++, g_object_unref (child), child = g_steal_pointer (&next)) {
				next = xb_node_get_next (child);
				if (g_strcmp0 (xb_node_get_element (child), "li") == 0) {
					g_autofree gchar *escaped = gs_appstream_format_description_text (child);

					/* Treat self-closing elements as with `<ul>` above. */
					g_string_append_printf (str, " %u. %s\n",
								i + 1,
								(escaped != NULL) ? escaped : "");
				}
			}
			g_string_append (str, "\n");
		}
	}

	/* remove extra newlines */
	while (str->len > 0 && str->str[str->len - 1] == '\n')
		g_string_truncate (str, str->len - 1);

	if (issues_node) {
		/* Add a single new line to delimit the description node's text from the issues */
		if (str->len)
			g_string_append_c (str, '\n');

		for (g_autoptr(XbNode) n = xb_node_get_child (issues_node); n != NULL; node_set_to_next (&n)) {
			if (g_strcmp0 (xb_node_get_element (n), "issue") == 0) {
				const gchar *node_text = xb_node_get_text (n);
				AsIssueKind issue_kind = as_issue_kind_from_string (xb_node_get_attr (n, "type"));
				const gchar *issue_url = xb_node_get_attr (n, "url");

				if (node_text != NULL && *node_text != '\0') {
					if (str->len > 0 && str->str[str->len - 1] != '\n')
						g_string_append_c (str, '\n');
					g_string_append (str, " • ");
					format_issue_link (str, node_text, issue_kind, issue_url);
				}
			}
		}

		/* remove extra newlines, in case there was no text for the issues */
		while (str->len > 0 && str->str[str->len - 1] == '\n')
			g_string_truncate (str, str->len - 1);
	}

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
	if (npath < 3 ||
	    !(g_strcmp0 (path[npath-2], "xmls") == 0 ||
	      g_strcmp0 (path[npath-2], "yaml") == 0 ||
	      g_strcmp0 (path[npath-2], "xml") == 0))
		return NULL;

	/* fix the new path */
	g_free (path[npath-1]);
	g_free (path[npath-2]);
	path[npath-1] = g_strdup (origin);
	path[npath-2] = g_strdup ("icons");
	return g_strjoinv ("/", path);
}

/* This function is designed to do no disk or network I/O. */
static AsIcon *
gs_appstream_new_icon (XbNode *component, XbNode *n, AsIconKind icon_kind, guint sz)
{
	AsIcon *icon = as_icon_new ();
	g_autofree gchar *icon_path = NULL;
	as_icon_set_kind (icon, icon_kind);
	switch (icon_kind) {
	case AS_ICON_KIND_LOCAL:
		as_icon_set_filename (icon, xb_node_get_text (n));
		break;
	case AS_ICON_KIND_REMOTE:
		as_icon_set_url (icon, xb_node_get_text (n));
		break;
	default:
		as_icon_set_name (icon, xb_node_get_text (n));
	}
	if (sz == 0) {
		guint64 width = xb_node_get_attr_as_uint (n, "width");
		if (width > 0 && width < G_MAXUINT)
			sz = width;
	}

	if (sz > 0) {
		as_icon_set_width (icon, sz);
		as_icon_set_height (icon, sz);
	}

	if (icon_kind != AS_ICON_KIND_LOCAL && icon_kind != AS_ICON_KIND_REMOTE) {
		/* add partial filename for now, we will compose the full one later */
		icon_path = gs_appstream_build_icon_prefix (component);
		as_icon_set_filename (icon, icon_path);
	}
	return icon;
}

static void
traverse_components_for_icons (GsApp *app,
			       GPtrArray *components)
{
	if (components == NULL)
		return;

	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);
		g_autoptr(XbNode) child = NULL;
		g_autoptr(XbNode) next = NULL;
		for (child = xb_node_get_child (component); child != NULL; g_object_unref (child), child = g_steal_pointer (&next)) {
			next = xb_node_get_next (child);
			if (g_strcmp0 (xb_node_get_element (child), "icon") == 0) {
				/* This code deliberately does *not* check that the icon files or theme
				 * icons exist, as that would mean doing disk I/O for all the apps in
				 * the appstream file, regardless of whether the calling code is
				 * actually going to use the icons. Better to add all the possible icons
				 * and let the calling code check which ones exist, if it needs to. */
				g_autoptr(AsIcon) as_icon = NULL;
				g_autoptr(GIcon) gicon = NULL;
				const gchar *icon_kind_str = xb_node_get_attr (child, "type");
				AsIconKind icon_kind = as_icon_kind_from_string (icon_kind_str);

				if (icon_kind == AS_ICON_KIND_UNKNOWN) {
					g_debug ("unknown icon kind ‘%s’", icon_kind_str);
					continue;
				}

				as_icon = gs_appstream_new_icon (component, child, icon_kind, 0);
				gicon = gs_icon_new_for_appstream_icon (as_icon);
				if (gicon != NULL)
					gs_app_add_icon (app, gicon);
			}
		}
	}
}

static gboolean
gs_appstream_refine_add_addons (GsPlugin *plugin,
				GsApp *app,
				XbSilo *silo,
				const gchar *appstream_source_file,
				AsComponentScope default_scope,
				GError **error)
{
	g_autofree gchar *xpath = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) addons = NULL;
	g_autoptr(GsAppList) addons_list = NULL;

	/* get all components */
	xpath = g_strdup_printf ("components/component/extends[text()='%s']/..",
				 gs_app_get_id (app));
	addons = xb_silo_query (silo, xpath, 0, &error_local);
	if (addons == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}

	addons_list = gs_app_list_new ();

	for (guint i = 0; i < addons->len; i++) {
		XbNode *addon = g_ptr_array_index (addons, i);
		g_autoptr(GsApp) addon_app = NULL;

		addon_app = gs_appstream_create_app (plugin, silo, addon, appstream_source_file, default_scope, error);
		if (addon_app == NULL)
			return FALSE;

		gs_app_list_add (addons_list, addon_app);
	}

	gs_app_add_addons (app, addons_list);

	return TRUE;
}

static guint64
component_get_release_timestamp (XbNode *component)
{
	guint64 timestamp;
	const gchar *date_str;

	/* Spec says to prefer `timestamp` over `date` if both are provided:
	 * https://www.freedesktop.org/software/appstream/docs/chap-Metadata.html#tag-releases */
	timestamp = xb_node_query_attr_as_uint (component, "releases/release", "timestamp", NULL);
	date_str = xb_node_query_attr (component, "releases/release", "date", NULL);

	if (timestamp != G_MAXUINT64) {
		return timestamp;
	} else if (date_str != NULL) {
		g_autoptr(GDateTime) date = g_date_time_new_from_iso8601 (date_str, NULL);
		if (date != NULL)
			return g_date_time_to_unix (date);
	}

	/* Unknown. */
	return G_MAXUINT64;
}

static gboolean
gs_appstream_is_recent_release (XbNode *component)
{
	guint64 ts;
	gint64 secs;

	/* get newest release */
	ts = component_get_release_timestamp (component);
	if (ts == G_MAXUINT64)
		return FALSE;

	/* is last build less than one year ago? */
	secs = (g_get_real_time () / G_USEC_PER_SEC) - ts;
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

	/* Strip off the territory, codeset and modifier, if present. */
	separator = strpbrk (locale_copy, "_.@");
	if (separator != NULL)
		*separator = '\0';

	if (g_strcmp0 (locale_copy, "C") == 0)
		return FALSE;
	if (g_strcmp0 (locale_copy, "en") == 0)
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
	return as_utils_is_desktop_environment (project_group);
}

static gboolean
gs_appstream_refine_app_relation (GsApp           *app,
                                  XbNode          *relation_node,
                                  AsRelationKind   kind,
                                  GError         **error)
{
	/* Iterate over the children, which might be any combination of zero or
	 * more <id/>, <modalias/>, <kernel/>, <memory/>, <firmware/>,
	 * <control/> or <display_length/> elements. For the moment, we only
	 * support some of these. */
	for (g_autoptr(XbNode) child = xb_node_get_child (relation_node); child != NULL; node_set_to_next (&child)) {
		const gchar *item_kind = xb_node_get_element (child);
		g_autoptr(AsRelation) relation = as_relation_new ();

		as_relation_set_kind (relation, kind);

		if (g_str_equal (item_kind, "control")) {
			/* https://www.freedesktop.org/software/appstream/docs/chap-Metadata.html#tag-relations-control */
			as_relation_set_item_kind (relation, AS_RELATION_ITEM_KIND_CONTROL);
			as_relation_set_value_control_kind (relation, as_control_kind_from_string (xb_node_get_text (child)));
		} else if (g_str_equal (item_kind, "display_length")) {
			const gchar *compare;
			const gchar *side;
#if !AS_CHECK_VERSION(1, 0, 0)
			AsDisplayLengthKind display_length_kind;
#endif

			/* https://www.freedesktop.org/software/appstream/docs/chap-Metadata.html#tag-relations-display_length */
			as_relation_set_item_kind (relation, AS_RELATION_ITEM_KIND_DISPLAY_LENGTH);

			compare = xb_node_get_attr (child, "compare");
			as_relation_set_compare (relation, (compare != NULL) ? as_relation_compare_from_string (compare) : AS_RELATION_COMPARE_GE);

#if AS_CHECK_VERSION(1, 0, 0)
			side = xb_node_get_attr (child, "side");
			as_relation_set_display_side_kind (relation, (side != NULL) ? as_display_side_kind_from_string (side) : AS_DISPLAY_SIDE_KIND_SHORTEST);
			as_relation_set_value_px (relation, xb_node_get_text_as_uint (child));
#else
			display_length_kind = as_display_length_kind_from_string (xb_node_get_text (child));
			if (display_length_kind != AS_DISPLAY_LENGTH_KIND_UNKNOWN) {
				/* Ignore the `side` attribute */
				as_relation_set_value_display_length_kind (relation, display_length_kind);
			} else {
				side = xb_node_get_attr (child, "side");
				as_relation_set_display_side_kind (relation, (side != NULL) ? as_display_side_kind_from_string (side) : AS_DISPLAY_SIDE_KIND_SHORTEST);
				as_relation_set_value_px (relation, xb_node_get_text_as_uint (child));
			}
#endif
		} else if (g_str_equal (item_kind, "id")) {
			if (kind == AS_RELATION_KIND_REQUIRES &&
			    g_strcmp0 (xb_node_get_attr (child, "type"), "id") == 0 &&
			    g_strcmp0 (xb_node_get_text (child), "org.gnome.Software.desktop") == 0) {
				/* is compatible */
				gint rc = as_vercmp_simple (xb_node_get_attr (child, "version"), PACKAGE_VERSION);
				if (rc > 0) {
					g_set_error (error,
						     GS_PLUGIN_ERROR,
						     GS_PLUGIN_ERROR_NOT_SUPPORTED,
						     "not for this gnome-software");
					return FALSE;
				}
			}
		} else {
			g_debug ("Relation type ‘%s’ not currently supported for %s; ignoring",
				 item_kind, gs_app_get_id (app));
			continue;
		}

		gs_app_add_relation (app, relation);
	}

	return TRUE;
}

static void
gs_appstream_find_description_and_issues_nodes (XbNode *release_node,
						XbNode **out_description_node, /* (out) (transfer full) */
						XbNode **out_issues_node) /* (out) (transfer full) */
{
	g_autoptr(XbNode) child = NULL;
	g_autoptr(XbNode) next = NULL;
	g_autoptr(XbNode) description_node = NULL;
	g_autoptr(XbNode) issues_node = NULL;

	for (child = xb_node_get_child (release_node);
	     child != NULL && (description_node == NULL || issues_node == NULL);
	     g_object_unref (child), child = g_steal_pointer (&next)) {
		next = xb_node_get_next (child);
		if (description_node == NULL && g_strcmp0 (xb_node_get_element (child), "description") == 0) {
			description_node = g_object_ref (child);
		} else if (issues_node == NULL && g_strcmp0 (xb_node_get_element (child), "issues") == 0) {
			issues_node = g_object_ref (child);
		}
	}

	if (out_description_node)
		*out_description_node = g_steal_pointer (&description_node);
	if (out_issues_node)
		*out_issues_node = g_steal_pointer (&issues_node);
}

typedef enum {
	ELEMENT_KIND_UNKNOWN = -1,
	ELEMENT_KIND_BUNDLE,
	ELEMENT_KIND_CATEGORIES,
	ELEMENT_KIND_CONTENT_RATING,
	ELEMENT_KIND_CUSTOM,
	ELEMENT_KIND_DESCRIPTION,
	ELEMENT_KIND_DEVELOPER_NAME,
	ELEMENT_KIND_DEVELOPER,
	ELEMENT_KIND_ICON,
	ELEMENT_KIND_ID,
	ELEMENT_KIND_INFO,
	ELEMENT_KIND_KEYWORDS,
	ELEMENT_KIND_KUDOS,
	ELEMENT_KIND_LANGUAGES,
	ELEMENT_KIND_LAUNCHABLE,
	ELEMENT_KIND_METADATA_LICENSE,
	ELEMENT_KIND_NAME,
	ELEMENT_KIND_PKGNAME,
	ELEMENT_KIND_PROJECT_GROUP,
	ELEMENT_KIND_PROJECT_LICENSE,
	ELEMENT_KIND_PROVIDES,
	ELEMENT_KIND_RECOMMENDS,
	ELEMENT_KIND_RELEASES,
	ELEMENT_KIND_REQUIRES,
	ELEMENT_KIND_SCREENSHOTS,
	ELEMENT_KIND_SUMMARY,
	ELEMENT_KIND_SUPPORTS,
	ELEMENT_KIND_URL
} ElementKind;

/* This is not for speed, but to not accidentally have checked for the element name twice in the block */
static ElementKind
gs_appstream_get_element_kind (const gchar *element_name)
{
	struct {
		const gchar *name;
		ElementKind kind;
	} kinds[] = {
		{ "bundle", ELEMENT_KIND_BUNDLE },
		{ "categories", ELEMENT_KIND_CATEGORIES },
		{ "content_rating", ELEMENT_KIND_CONTENT_RATING },
		{ "custom", ELEMENT_KIND_CUSTOM },
		{ "description", ELEMENT_KIND_DESCRIPTION },
		{ "developer", ELEMENT_KIND_DEVELOPER },
		{ "developer_name", ELEMENT_KIND_DEVELOPER_NAME },
		{ "icon", ELEMENT_KIND_ICON },
		{ "id", ELEMENT_KIND_ID },
		{ "info", ELEMENT_KIND_INFO },
		{ "keywords", ELEMENT_KIND_KEYWORDS },
		{ "kudos", ELEMENT_KIND_KUDOS },
		{ "languages", ELEMENT_KIND_LANGUAGES },
		{ "launchable", ELEMENT_KIND_LAUNCHABLE },
		{ "metadata_license", ELEMENT_KIND_METADATA_LICENSE },
		{ "name", ELEMENT_KIND_NAME },
		{ "pkgname", ELEMENT_KIND_PKGNAME },
		{ "project_group", ELEMENT_KIND_PROJECT_GROUP },
		{ "project_license", ELEMENT_KIND_PROJECT_LICENSE },
		{ "provides", ELEMENT_KIND_PROVIDES },
		{ "recommends", ELEMENT_KIND_RECOMMENDS },
		{ "releases", ELEMENT_KIND_RELEASES },
		{ "requires", ELEMENT_KIND_REQUIRES },
		{ "screenshots", ELEMENT_KIND_SCREENSHOTS },
		{ "summary", ELEMENT_KIND_SUMMARY },
		{ "supports", ELEMENT_KIND_SUPPORTS },
		{ "url", ELEMENT_KIND_URL }
	};
	for (guint i = 0; i < G_N_ELEMENTS (kinds); i++) {
		if (g_strcmp0 (element_name, kinds[i].name) == 0)
			return kinds[i].kind;
	}
	return ELEMENT_KIND_UNKNOWN;
}

gboolean
gs_appstream_refine_app (GsPlugin *plugin,
			 GsApp *app,
			 XbSilo *silo,
			 XbNode *component,
			 GsPluginRefineFlags refine_flags,
			 GHashTable *installed_by_desktopid,
			 const gchar *appstream_source_file,
			 AsComponentScope default_scope,
			 GError **error)
{
	GsAppQuality name_quality = GS_APP_QUALITY_HIGHEST;
	const gchar *tmp;
	const gchar *developer_name_fallback = NULL;
	gboolean has_name = FALSE, has_metadata_license = FALSE;
	gboolean had_icons, had_sources;
	gboolean locale_has_translations = FALSE;
	g_autoptr(GPtrArray) legacy_pkgnames = NULL;
	g_autoptr(XbNode) launchable_desktop_id = NULL;
	g_autoptr(XbNode) child = NULL;
	g_autoptr(XbNode) next = NULL;

	/* The 'plugin' can be NULL, when creating app for --show-metainfo */
	g_return_val_if_fail (GS_IS_APP (app), FALSE);
	g_return_val_if_fail (XB_IS_SILO (silo), FALSE);
	g_return_val_if_fail (XB_IS_NODE (component), FALSE);

	had_icons = gs_app_has_icons (app);
	had_sources = gs_app_get_sources (app)->len > 0;
	if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_KUDOS) != 0) {
		tmp = setlocale (LC_MESSAGES, NULL);
		locale_has_translations = _gs_utils_locale_has_translations (tmp);
	}

	/* set id kind */
	if (gs_app_get_kind (app) == AS_COMPONENT_KIND_UNKNOWN ||
	    gs_app_get_kind (app) == AS_COMPONENT_KIND_GENERIC) {
		AsComponentKind kind;
		tmp = xb_node_get_attr (component, "type");
		kind = as_component_kind_from_string (tmp);
		if (kind != AS_COMPONENT_KIND_UNKNOWN)
			gs_app_set_kind (app, kind);
	}

	/* types we can never launch */
	switch (gs_app_get_kind (app)) {
	case AS_COMPONENT_KIND_REPOSITORY:
		/* plugins may know better name, than what there's set in the appstream for the repos */
		name_quality = GS_APP_QUALITY_NORMAL;
		/* fall-through */
	case AS_COMPONENT_KIND_ADDON:
	case AS_COMPONENT_KIND_CODEC:
	case AS_COMPONENT_KIND_DRIVER:
	case AS_COMPONENT_KIND_FIRMWARE:
	case AS_COMPONENT_KIND_FONT:
	case AS_COMPONENT_KIND_GENERIC:
	case AS_COMPONENT_KIND_INPUT_METHOD:
	case AS_COMPONENT_KIND_LOCALIZATION:
	case AS_COMPONENT_KIND_OPERATING_SYSTEM:
	case AS_COMPONENT_KIND_RUNTIME:
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

	tmp = gs_app_get_metadata_item (app, "flathub::verification::verified");
	if (g_strcmp0 (tmp, "true") == 0)
		gs_app_add_quirk (app, GS_APP_QUIRK_DEVELOPER_VERIFIED);
	else
		gs_app_remove_quirk (app, GS_APP_QUIRK_DEVELOPER_VERIFIED);

	legacy_pkgnames = g_ptr_array_new_with_free_func (g_object_unref);

	for (child = xb_node_get_child (component); child != NULL; g_object_unref (child), child = g_steal_pointer (&next)) {
		next = xb_node_get_next (child);

		switch (gs_appstream_get_element_kind (xb_node_get_element (child))) {
		default:
		case ELEMENT_KIND_UNKNOWN:
			break;
		case ELEMENT_KIND_BUNDLE:
			if (!had_sources) {
				const gchar *kind = xb_node_get_attr (child, "type");
				const gchar *bundle_id = xb_node_get_text (child);

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
			break;
		case ELEMENT_KIND_CATEGORIES:
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_CATEGORIES) != 0) {
				g_autoptr(XbNode) cat_child = NULL;
				g_autoptr(XbNode) cat_next = NULL;
				for (cat_child = xb_node_get_child (child); cat_child != NULL; g_object_unref (cat_child), cat_child = g_steal_pointer (&cat_next)) {
					cat_next = xb_node_get_next (cat_child);
					if (g_strcmp0 (xb_node_get_element (cat_child), "category") == 0) {
						tmp = xb_node_get_text (cat_child);
						if (tmp != NULL) {
							gs_app_add_category (app, tmp);

							/* Special case: We used to use the `Blacklisted`
							 * category to hide apps from their .desktop
							 * file or appdata. We now use a quirk for that.
							 * This special case can be removed when all
							 * appstream files no longer use the `Blacklisted`
							 * category (including external-appstream files
							 * put together by distributions). */
							if (g_strcmp0 (tmp, "Blacklisted") == 0)
								gs_app_add_quirk (app, GS_APP_QUIRK_HIDE_EVERYWHERE);

							if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_KUDOS) != 0 &&
							    !gs_app_has_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED) &&
							   g_strcmp0 (tmp, "Featured") == 0)
								gs_app_add_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED);
						}
					}
				}
			}
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_KUDOS) != 0 &&
			    !gs_app_has_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED)) {
				g_autoptr(XbNode) cat_child = NULL;
				g_autoptr(XbNode) cat_next = NULL;
				for (cat_child = xb_node_get_child (child); cat_child != NULL; g_object_unref (cat_child), cat_child = g_steal_pointer (&cat_next)) {
					cat_next = xb_node_get_next (cat_child);
					if (g_strcmp0 (xb_node_get_element (cat_child), "category") == 0) {
						tmp = xb_node_get_text (cat_child);
						if (g_strcmp0 (tmp, "Featured") == 0) {
							gs_app_add_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED);
							break;
						}
					}
				}
			}
			break;
		case ELEMENT_KIND_CONTENT_RATING: {
			g_autoptr(AsContentRating) content_rating = gs_app_dup_content_rating (app);
			if (content_rating == NULL) {
				const gchar *content_rating_kind = NULL;

				/* get kind */
				content_rating_kind = xb_node_get_attr (child, "type");
				/* we only really expect/support OARS 1.0 and 1.1 */
				if (g_strcmp0 (content_rating_kind, "oars-1.0") == 0 ||
				    g_strcmp0 (content_rating_kind, "oars-1.1") == 0) {
					g_autoptr(AsContentRating) cr = NULL;
					g_autoptr(XbNode) cr_child = NULL;
					g_autoptr(XbNode) cr_next = NULL;
					for (cr_child = xb_node_get_child (child); cr_child != NULL; g_object_unref (cr_child), cr_child = g_steal_pointer (&cr_next)) {
						cr_next = xb_node_get_next (cr_child);
						if (g_strcmp0 (xb_node_get_element (cr_child), "content_attribute") == 0) {
							if (cr == NULL) {
								cr = as_content_rating_new ();
								as_content_rating_set_kind (cr, content_rating_kind);
							}
							as_content_rating_add_attribute (cr,
											 xb_node_get_attr (cr_child, "id"),
											 as_content_rating_value_from_string (xb_node_get_text (cr_child)));
						}
					}
					if (cr != NULL)
						gs_app_set_content_rating (app, cr);
				}
			}
			} break;
		case ELEMENT_KIND_CUSTOM: {
			g_autoptr(XbNode) cus_child = NULL;
			g_autoptr(XbNode) cus_next = NULL;
			for (cus_child = xb_node_get_child (child); cus_child != NULL; g_object_unref (cus_child), cus_child = g_steal_pointer (&cus_next)) {
				const gchar *key = xb_node_get_attr (cus_child, "key");
				cus_next = xb_node_get_next (cus_child);
				if (key == NULL)
					continue;
				if (gs_app_get_metadata_item (app, key) != NULL)
					continue;
				gs_app_set_metadata (app, key, xb_node_get_text (cus_child));
			}
			} break;
		case ELEMENT_KIND_DESCRIPTION:
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_DESCRIPTION) != 0) {
				g_autofree gchar *description = gs_appstream_format_description (child, NULL);
				if (description != NULL)
					gs_app_set_description (app, GS_APP_QUALITY_HIGHEST, description);
			}
			break;
		case ELEMENT_KIND_DEVELOPER:
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_DEVELOPER_NAME) > 0 &&
			    gs_app_get_developer_name (app) == NULL) {
				g_autoptr(XbNode) developer_child = NULL;
				g_autoptr(XbNode) developer_next = NULL;
				for (developer_child = xb_node_get_child (child);
				     developer_child != NULL;
				     g_object_unref (developer_child), developer_child = g_steal_pointer (&developer_next)) {
					developer_next = xb_node_get_next (developer_child);
					if (g_strcmp0 (xb_node_get_element (developer_child), "name") == 0) {
						tmp = xb_node_get_text (developer_child);
						if (tmp != NULL) {
							gs_app_set_developer_name (app, tmp);
							break;
						}
					}
				}
			}
			break;
		case ELEMENT_KIND_DEVELOPER_NAME:
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_DEVELOPER_NAME) > 0 &&
			    developer_name_fallback == NULL) {
				developer_name_fallback = xb_node_get_text (child);
			}
			break;
		case ELEMENT_KIND_ICON:
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON) != 0 &&
			    !had_icons) {
				/* This code deliberately does *not* check that the icon files or theme
				 * icons exist, as that would mean doing disk I/O for all the apps in
				 * the appstream file, regardless of whether the calling code is
				 * actually going to use the icons. Better to add all the possible icons
				 * and let the calling code check which ones exist, if it needs to. */
				const gchar *icon_kind_str = xb_node_get_attr (child, "type");
				AsIconKind icon_kind = as_icon_kind_from_string (icon_kind_str);

				if (icon_kind == AS_ICON_KIND_UNKNOWN) {
					g_debug ("unknown icon kind ‘%s’", icon_kind_str);
				} else {
					g_autoptr(GIcon) gicon = NULL;
					g_autoptr(AsIcon) as_icon = NULL;
					as_icon = gs_appstream_new_icon (component, child, icon_kind, 0);
					gicon = gs_icon_new_for_appstream_icon (as_icon);
					if (gicon != NULL)
						gs_app_add_icon (app, gicon);
				}
			}
			/* HiDPI icon */
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_KUDOS) != 0 &&
			    !gs_app_has_kudo (app, GS_APP_KUDO_HI_DPI_ICON) &&
			    xb_node_get_attr_as_uint (child, "width") == 128) {
				gs_app_add_kudo (app, GS_APP_KUDO_HI_DPI_ICON);
			}
			break;
		case ELEMENT_KIND_ID:
			if (gs_app_get_id (app) == NULL) {
				tmp = xb_node_get_text (child);
				if (tmp != NULL)
					gs_app_set_id (app, tmp);
			}
			break;
		case ELEMENT_KIND_INFO:
			if (gs_app_get_metadata_item (app, "appstream::source-file") == NULL) {
				g_autoptr(XbNode) info_child = NULL;
				g_autoptr(XbNode) info_next = NULL;
				for (info_child = xb_node_get_child (child); info_child != NULL; g_object_unref (info_child), info_child = g_steal_pointer (&info_next)) {
					info_next = xb_node_get_next (info_child);
					if (g_strcmp0 (xb_node_get_element (info_child), "filename") == 0) {
						tmp = xb_node_get_text (info_child);
						if (tmp != NULL)
							gs_app_set_metadata (app, "appstream::source-file", tmp);
						break;
					}
				}
			}
			break;
		case ELEMENT_KIND_KEYWORDS:
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_KUDOS) != 0 &&
			    !gs_app_has_kudo (app, GS_APP_KUDO_HAS_KEYWORDS)) {
				g_autoptr(XbNode) kw_child = NULL;
				g_autoptr(XbNode) kw_next = NULL;
				for (kw_child = xb_node_get_child (child); kw_child != NULL; g_object_unref (kw_child), kw_child = g_steal_pointer (&kw_next)) {
					kw_next = xb_node_get_next (kw_child);
					if (g_strcmp0 (xb_node_get_element (kw_child), "keyword") == 0) {
						gs_app_add_kudo (app, GS_APP_KUDO_HAS_KEYWORDS);
						break;
					}
				}
			}
			break;
		case ELEMENT_KIND_KUDOS:
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_KUDOS) != 0 &&
			    !gs_app_has_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED)) {
				g_autoptr(XbNode) kudos_child = NULL;
				g_autoptr(XbNode) kudos_next = NULL;
				for (kudos_child = xb_node_get_child (child); kudos_child != NULL; g_object_unref (kudos_child), kudos_child = g_steal_pointer (&kudos_next)) {
					kudos_next = xb_node_get_next (kudos_child);
					if (g_strcmp0 (xb_node_get_element (kudos_child), "GnomeSoftware::popular") == 0) {
						gs_app_add_kudo (app, GS_APP_KUDO_FEATURED_RECOMMENDED);
						break;
					}
				}
			}
			break;
		case ELEMENT_KIND_LANGUAGES:
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_KUDOS) != 0) {
				if (!locale_has_translations)
					gs_app_add_kudo (app, GS_APP_KUDO_MY_LANGUAGE);

				if (!gs_app_get_has_translations (app)) {
					g_autoptr(XbNode) langs_child = NULL;
					g_autoptr(XbNode) langs_next = NULL;
					g_auto(GStrv) variants = g_get_locale_variants (tmp);

					for (langs_child = xb_node_get_child (child); langs_child != NULL; g_object_unref (langs_child), langs_child = g_steal_pointer (&langs_next)) {
						langs_next = xb_node_get_next (langs_child);
						if (g_strcmp0 (xb_node_get_element (langs_child), "lang") == 0) {
							tmp = xb_node_get_text (langs_child);
							if (tmp != NULL) {
								gboolean is_variant = FALSE;

								/* Set this under the FLAGS_REQUIRE_KUDOS flag because it’s
								 * only useful in combination with KUDO_MY_LANGUAGE */
								gs_app_set_has_translations (app, TRUE);

								if (gs_app_has_kudo (app, GS_APP_KUDO_MY_LANGUAGE))
									break;

								for (guint j = 0; variants[j]; j++) {
									if (g_strcmp0 (tmp, variants[j]) == 0) {
										is_variant = TRUE;
										break;
									}
								}
								if (is_variant && xb_node_get_attr_as_uint (langs_child, "percentage") > 50) {
									gs_app_add_kudo (app, GS_APP_KUDO_MY_LANGUAGE);
									break;
								}
							}
						}
					}
				}
			}
			break;
		case ELEMENT_KIND_LAUNCHABLE: {
			const gchar *kind = xb_node_get_attr (child, "type");
			if (g_strcmp0 (kind, "desktop-id") == 0) {
				gs_app_set_launchable (app,
						       AS_LAUNCHABLE_KIND_DESKTOP_ID,
						       xb_node_get_text (child));
				g_set_object (&launchable_desktop_id, child);
			} else if (g_strcmp0 (kind, "url") == 0) {
				gs_app_set_launchable (app,
						       AS_LAUNCHABLE_KIND_URL,
						       xb_node_get_text (child));
			}
			} break;
		case ELEMENT_KIND_METADATA_LICENSE:
			has_metadata_license = TRUE;
			break;
		case ELEMENT_KIND_NAME:
			tmp = xb_node_get_text (child);
			if (tmp != NULL) {
				gs_app_set_name (app, name_quality, tmp);
				has_name = TRUE;
			}
			break;
		case ELEMENT_KIND_PKGNAME:
			g_ptr_array_add (legacy_pkgnames, g_object_ref (child));
			break;
		case ELEMENT_KIND_PROJECT_GROUP:
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PROJECT_GROUP) > 0 &&
			    gs_app_get_project_group (app) == NULL) {
				tmp = xb_node_get_text (child);
				if (tmp != NULL && gs_appstream_is_valid_project_group (tmp))
					gs_app_set_project_group (app, tmp);
			}
			break;
		case ELEMENT_KIND_PROJECT_LICENSE:
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_LICENSE) != 0 &&
			    gs_app_get_license (app) == NULL) {
				tmp = xb_node_get_text (child);
				if (tmp != NULL)
					gs_app_set_license (app, GS_APP_QUALITY_HIGHEST, tmp);
			}
			break;
		case ELEMENT_KIND_PROVIDES: {
			g_autoptr(XbNode) prov_child = NULL;
			g_autoptr(XbNode) prov_next = NULL;
			for (prov_child = xb_node_get_child (child); prov_child != NULL; g_object_unref (prov_child), prov_child = g_steal_pointer (&prov_next)) {
				AsProvidedKind kind;
				const gchar *element_name = xb_node_get_element (prov_child);
				prov_next = xb_node_get_next (prov_child);

				/* try the simple case */
				kind = as_provided_kind_from_string (element_name);
				if (kind == AS_PROVIDED_KIND_UNKNOWN) {
					/* try the complex cases */

					if (g_strcmp0 (element_name, "library") == 0) {
						kind = AS_PROVIDED_KIND_LIBRARY;
					} else if (g_strcmp0 (element_name, "binary") == 0) {
						kind = AS_PROVIDED_KIND_BINARY;
					} else if (g_strcmp0 (element_name, "firmware") == 0) {
						const gchar *fw_type = xb_node_get_attr (prov_child, "type");
						if (g_strcmp0 (fw_type, "runtime") == 0)
							kind = AS_PROVIDED_KIND_FIRMWARE_RUNTIME;
						else if (g_strcmp0 (fw_type, "flashed") == 0)
							kind = AS_PROVIDED_KIND_FIRMWARE_FLASHED;
					} else if (g_strcmp0 (element_name, "python3") == 0) {
						kind = AS_PROVIDED_KIND_PYTHON;
					} else if (g_strcmp0 (element_name, "dbus") == 0) {
						const gchar *dbus_type = xb_node_get_attr (prov_child, "type");
						if (g_strcmp0 (dbus_type, "system") == 0)
							kind = AS_PROVIDED_KIND_DBUS_SYSTEM;
						else if ((g_strcmp0 (dbus_type, "user") == 0) || (g_strcmp0 (dbus_type, "session") == 0))
							kind = AS_PROVIDED_KIND_DBUS_USER;
					}
				}

				if (kind == AS_PROVIDED_KIND_UNKNOWN ||
				    xb_node_get_text (prov_child) == NULL) {
					/* give up */
					g_debug ("ignoring unknown or empty provided item type:'%s' value:'%s'", element_name, xb_node_get_text (prov_child));
					continue;
				}

				gs_app_add_provided_item (app, kind, xb_node_get_text (prov_child));
			}
			} break;
		case ELEMENT_KIND_RECOMMENDS:
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS) != 0) {
				if (!gs_appstream_refine_app_relation (app, child, AS_RELATION_KIND_RECOMMENDS, error))
						return FALSE;
			}
			break;
		case ELEMENT_KIND_RELEASES: {
			g_autoptr(GPtrArray) current_version_history = gs_app_get_version_history (app);
			gboolean needs_version_history = current_version_history == NULL || current_version_history->len == 0;
			gboolean needs_update_details = (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_UPDATE_DETAILS) != 0 &&
							silo != NULL && gs_app_is_updatable (app);
			/* set the release date */
			if (gs_app_get_release_date (app) == G_MAXUINT64) {
				g_autoptr(XbNode) release = xb_node_get_child (child);
				if (release != NULL && g_strcmp0 (xb_node_get_element (release), "release") == 0) {
					guint64 timestamp;
					const gchar *date_str;

					/* Spec says to prefer `timestamp` over `date` if both are provided:
					 * https://www.freedesktop.org/software/appstream/docs/chap-Metadata.html#tag-releases */
					timestamp = xb_node_get_attr_as_uint (release, "timestamp");
					date_str = xb_node_get_attr (release, "date");

					if (timestamp != G_MAXUINT64) {
						gs_app_set_release_date (app, timestamp);
					} else if (date_str != NULL) {
						g_autoptr(GDateTime) date = g_date_time_new_from_iso8601 (date_str, NULL);
						if (date != NULL)
							gs_app_set_release_date (app, g_date_time_to_unix (date));
					}
				}
			}
			if (needs_version_history || needs_update_details) {
				g_autoptr(GPtrArray) version_history = NULL; /* (element-type AsRelease) */
				g_autoptr(GHashTable) installed = NULL;
				g_autoptr(GPtrArray) updates_list = NULL;
				g_autoptr(XbNode) rels_child = NULL;
				g_autoptr(XbNode) rels_next = NULL;
				AsUrgencyKind urgency_best = AS_URGENCY_KIND_UNKNOWN;
				guint i;

				if (needs_update_details) {
					g_autofree gchar *xpath = NULL;
					g_autoptr(GPtrArray) releases_inst = NULL;
					g_autoptr(GError) local_error = NULL;

					installed = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_object_unref);
					updates_list = g_ptr_array_new_with_free_func (g_object_unref);

					/* find out which releases are already installed */
					xpath = g_strdup_printf ("component/id[text()='%s']/../releases/*[@version]",
								 gs_app_get_id (app));
					releases_inst = xb_silo_query (silo, xpath, 0, &local_error);
					if (releases_inst == NULL) {
						if (!g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
							g_propagate_error (error, g_steal_pointer (&local_error));
							return FALSE;
						}
					} else {
						for (i = 0; i < releases_inst->len; i++) {
							XbNode *release = g_ptr_array_index (releases_inst, i);
							g_hash_table_insert (installed,
									     (gpointer) xb_node_get_attr (release, "version"),
									     g_object_ref (release));
						}
					}
					g_clear_error (&local_error);
				}

				if (needs_version_history)
					version_history = g_ptr_array_new_with_free_func (g_object_unref);

				for (i = 0, rels_child = xb_node_get_child (child); rels_child != NULL;
				     i++, g_object_unref (rels_child), rels_child = g_steal_pointer (&rels_next)) {
					g_autoptr(XbNode) description_node = NULL;
					g_autoptr(XbNode) issues_node = NULL;
					const gchar *version;

					rels_next = xb_node_get_next (rels_child);
					if (g_strcmp0 (xb_node_get_element (rels_child), "release") != 0)
						continue;

					version = xb_node_get_attr (rels_child, "version");
					/* ignore releases with no version */
					if (version == NULL)
						continue;

					gs_appstream_find_description_and_issues_nodes (rels_child, &description_node, &issues_node);

					if (version_history != NULL) {
						g_autoptr(AsRelease) release = NULL;
						g_autofree gchar *description = NULL;
						guint64 timestamp;
						const gchar *date_str;

						timestamp = xb_node_get_attr_as_uint (rels_child, "timestamp");
						date_str = xb_node_get_attr (rels_child, "date");

						/* include updates with or without a description */
						if (description_node != NULL || issues_node != NULL)
							description = gs_appstream_format_description (description_node, issues_node);

						release = as_release_new ();
						as_release_set_version (release, version);
						if (timestamp != G_MAXUINT64)
							as_release_set_timestamp (release, timestamp);
						else if (date_str != NULL)  /* timestamp takes precedence over date */
							as_release_set_date (release, date_str);
						if (description != NULL)
							as_release_set_description (release, description, NULL);

						g_ptr_array_add (version_history, g_steal_pointer (&release));
					}

					if (needs_update_details) {
						AsUrgencyKind urgency_tmp;

						/* already installed */
						if (g_hash_table_lookup (installed, version) != NULL)
							continue;

						/* limit this to three versions backwards if there has never
						 * been a detected installed version */
						if (g_hash_table_size (installed) == 0 && i >= 3)
							continue;

						/* use the 'worst' urgency, e.g. critical over enhancement */
						urgency_tmp = as_urgency_kind_from_string (xb_node_get_attr (rels_child, "urgency"));
						if (urgency_tmp > urgency_best)
							urgency_best = urgency_tmp;

						/* add updates with a description */
						if (description_node != NULL || issues_node != NULL)
							g_ptr_array_add (updates_list, g_object_ref (rels_child));
					}
				}

				if (version_history != NULL && version_history->len > 0)
					gs_app_set_version_history (app, version_history);

				if (needs_update_details) {
					/* only set if known */
					if (urgency_best != AS_URGENCY_KIND_UNKNOWN)
						gs_app_set_update_urgency (app, urgency_best);

					/* no prefix on each release */
					if (updates_list->len == 1) {
						XbNode *release = g_ptr_array_index (updates_list, 0);
						g_autoptr(XbNode) description_node = NULL;
						g_autoptr(XbNode) issues_node = NULL;
						g_autofree gchar *desc = NULL;
						gs_appstream_find_description_and_issues_nodes (release, &description_node, &issues_node);
						desc = gs_appstream_format_description (description_node, issues_node);
						gs_app_set_update_details_markup (app, desc);

					/* get the descriptions with a version prefix */
					} else if (updates_list->len > 1) {
						const gchar *version = gs_app_get_version (app);
						g_autoptr(GString) update_desc = g_string_new ("");
						for (guint j = 0; j < updates_list->len; j++) {
							XbNode *release = g_ptr_array_index (updates_list, j);
							const gchar *release_version = xb_node_get_attr (release, "version");
							g_autofree gchar *desc = NULL;
							g_autoptr(XbNode) description_node = NULL;
							g_autoptr(XbNode) issues_node = NULL;

							/* use the first release description, then skip the currently installed version and all below it */
							if (i != 0 && version != NULL && as_vercmp_simple (version, release_version) >= 0)
								continue;

							gs_appstream_find_description_and_issues_nodes (release, &description_node, &issues_node);
							desc = gs_appstream_format_description (description_node, issues_node);

							g_string_append_printf (update_desc,
										"Version %s:\n%s\n\n",
										xb_node_get_attr (release, "version"),
										desc);
						}

						/* remove trailing newlines */
						if (update_desc->len > 2)
							g_string_truncate (update_desc, update_desc->len - 2);
						if (update_desc->len > 0)
							gs_app_set_update_details_markup (app, update_desc->str);
					}

					/* if there is no already set update version use the newest */
					if (gs_app_get_update_version (app) == NULL &&
					    updates_list->len > 0) {
						XbNode *release = g_ptr_array_index (updates_list, 0);
						gs_app_set_update_version (app, xb_node_get_attr (release, "version"));
					}
				}
			}
			} break;
		case ELEMENT_KIND_REQUIRES:
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS) != 0) {
				if (!gs_appstream_refine_app_relation (app, child, AS_RELATION_KIND_REQUIRES, error))
						return FALSE;
			}
			break;
		case ELEMENT_KIND_SCREENSHOTS:
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_SCREENSHOTS) != 0 &&
			    gs_app_get_screenshots (app)->len == 0) {
				g_autoptr(XbNode) scrs_child = NULL;
				g_autoptr(XbNode) scrs_next = NULL;
				for (scrs_child = xb_node_get_child (child); scrs_child != NULL; g_object_unref (scrs_child), scrs_child = g_steal_pointer (&scrs_next)) {
					scrs_next = xb_node_get_next (scrs_child);
					if (g_strcmp0 (xb_node_get_element (scrs_child), "screenshot") == 0) {
						g_autoptr(AsScreenshot) scr = as_screenshot_new ();
						g_autoptr(XbNode) scr_child = NULL;
						g_autoptr(XbNode) scr_next = NULL;
						gboolean any_added = FALSE;
						for (scr_child = xb_node_get_child (scrs_child); scr_child != NULL; g_object_unref (scr_child), scr_child = g_steal_pointer (&scr_next)) {
							scr_next = xb_node_get_next (scr_child);
							if (g_strcmp0 (xb_node_get_element (scr_child), "image") == 0) {
								g_autoptr(AsImage) im = as_image_new ();
								as_image_set_height (im, xb_node_get_attr_as_uint (scr_child, "height"));
								as_image_set_width (im, xb_node_get_attr_as_uint (scr_child, "width"));
								as_image_set_kind (im, as_image_kind_from_string (xb_node_get_attr (scr_child, "type")));
								as_image_set_url (im, xb_node_get_text (scr_child));
								as_screenshot_add_image (scr, im);
								any_added = TRUE;
							} else if (g_strcmp0 (xb_node_get_element (scr_child), "video") == 0) {
								g_autoptr(AsVideo) vid = as_video_new ();
								as_video_set_height (vid, xb_node_get_attr_as_uint (scr_child, "height"));
								as_video_set_width (vid, xb_node_get_attr_as_uint (scr_child, "width"));
								as_video_set_codec_kind (vid, as_video_codec_kind_from_string (xb_node_get_attr (scr_child, "codec")));
								as_video_set_container_kind (vid, as_video_container_kind_from_string (xb_node_get_attr (scr_child, "container")));
								as_video_set_url (vid, xb_node_get_text (scr_child));
								as_screenshot_add_video (scr, vid);
								any_added = TRUE;
							}
						}
						if (any_added)
							gs_app_add_screenshot (app, scr);
					}
				}
				/* FIXME: move into no refine flags section? */
				if (gs_app_get_screenshots (app)->len)
					gs_app_add_kudo (app, GS_APP_KUDO_HAS_SCREENSHOTS);
			}
			break;
		case ELEMENT_KIND_SUMMARY:
			tmp = xb_node_get_text (child);
			if (tmp != NULL)
				gs_app_set_summary (app, name_quality, tmp);
			break;
		case ELEMENT_KIND_SUPPORTS:
			#if AS_CHECK_VERSION(0, 15, 0)
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_PERMISSIONS) != 0) {
				if (!gs_appstream_refine_app_relation (app, child, AS_RELATION_KIND_SUPPORTS, error))
						return FALSE;
			}
			#endif
			break;
		case ELEMENT_KIND_URL:
			if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_URL) != 0) {
				const gchar *kind = xb_node_get_attr (child, "type");
				if (kind != NULL) {
					gs_app_set_url (app,
							as_url_kind_from_string (kind),
							xb_node_get_text (child));
				}
			}
			break;
		}
	}

	if (developer_name_fallback != NULL &&
	    gs_app_get_developer_name (app) == NULL) {
		gs_app_set_developer_name (app, developer_name_fallback);
	}

	/* try to detect old-style AppStream 'override'
	 * files without the merge attribute */
	if (!has_name && !has_metadata_license)
		gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);

	if (gs_app_get_metadata_item (app, "appstream::source-file") == NULL) {
		if (appstream_source_file != NULL) {
			/* empty string means the node was not found by the caller */
			if (*appstream_source_file != '\0')
				gs_app_set_metadata (app, "appstream::source-file", appstream_source_file);
		} else {
			tmp = xb_node_query_text (component, "../info/filename", NULL);
			if (tmp != NULL)
				gs_app_set_metadata (app, "appstream::source-file", tmp);
		}
	}

	/* set scope */
	if (gs_app_get_scope (app) == AS_COMPONENT_SCOPE_UNKNOWN) {
		/* all callers should provide both appstream_source_file and default_scope, thus
		   when the appstream_source_file the "unknown" scope means "not found in the silo" */
		if (appstream_source_file != NULL) {
			if (default_scope != AS_COMPONENT_SCOPE_UNKNOWN)
				gs_app_set_scope (app, default_scope);
		} else {
			tmp = xb_node_query_text (component, "../info/scope", NULL);
			if (tmp != NULL)
				gs_app_set_scope (app, as_component_scope_from_string (tmp));
		}
	}

	if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ICON) != 0 &&
	    !had_icons && !gs_app_has_icons (app)) {
		/* If no icon found, try to inherit the icon from the .desktop file */
		g_autofree gchar *xpath = NULL;
		if (launchable_desktop_id != NULL) {
			const gchar *launchable_id = xb_node_get_text (launchable_desktop_id);
			if (launchable_id != NULL) {
				if (installed_by_desktopid != NULL) {
					GPtrArray *components = g_hash_table_lookup (installed_by_desktopid, launchable_id);
					traverse_components_for_icons (app, components);
				} else {
					g_autoptr(GPtrArray) components = NULL;
					xpath = g_strdup_printf ("/component[@type='desktop-application']/launchable[@type='desktop-id'][text()='%s']/..",
								 launchable_id);
					components = xb_silo_query (silo, xpath, 0, NULL);
					traverse_components_for_icons (app, components);
					g_clear_pointer (&xpath, g_free);
				}
			}
		}

		if (installed_by_desktopid != NULL) {
			GPtrArray *components = g_hash_table_lookup (installed_by_desktopid, gs_app_get_id (app));
			traverse_components_for_icons (app, components);
		} else {
			g_autoptr(GPtrArray) components = NULL;
			xpath = g_strdup_printf ("/component[@type='desktop-application']/launchable[@type='desktop-id'][text()='%s']/..",
						 gs_app_get_id (app));
			components = xb_silo_query (silo, xpath, 0, NULL);
			traverse_components_for_icons (app, components);
		}
	}

	/* add legacy package names */
	if (gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_UNKNOWN &&
	    legacy_pkgnames->len > 0 && gs_app_get_sources (app)->len == 0) {
		for (guint i = 0; i < legacy_pkgnames->len; i++) {
			XbNode *pkgname = g_ptr_array_index (legacy_pkgnames, i);
			tmp = xb_node_get_text (pkgname);
			if (tmp != NULL && tmp[0] != '\0')
				gs_app_add_source (app, tmp);
		}
		gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
	}

	/* set origin */
	if (gs_app_get_origin_appstream (app) == NULL || (gs_app_get_origin (app) == NULL && (
	    gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_FLATPAK ||
	    gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_PACKAGE))) {
		g_autoptr(XbNode) parent = xb_node_get_parent (component);
		tmp = NULL;
		if (parent != NULL) {
			tmp = xb_node_get_attr (parent, "origin");
			if (gs_appstream_origin_valid (tmp)) {
				if (gs_app_get_origin_appstream (app) == NULL)
					gs_app_set_origin_appstream (app, tmp);

				if (gs_app_get_origin (app) == NULL && (
				    gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_FLATPAK ||
				    gs_app_get_bundle_kind (app) == AS_BUNDLE_KIND_PACKAGE)) {
					gs_app_set_origin (app, tmp);
				}
			}
		}
	}

	/* set addons */
	if ((refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_ADDONS) != 0 &&
	    plugin != NULL && silo != NULL) {
		if (!gs_appstream_refine_add_addons (plugin, app, silo, appstream_source_file, default_scope, error))
			return FALSE;
	}

	if (refine_flags & GS_PLUGIN_REFINE_FLAGS_REQUIRE_KUDOS) {
		if (!locale_has_translations)
			gs_app_add_kudo (app, GS_APP_KUDO_MY_LANGUAGE);

		/* was this app released recently */
		if (gs_appstream_is_recent_release (component))
			gs_app_add_kudo (app, GS_APP_KUDO_RECENT_RELEASE);
	}

	return TRUE;
}

typedef struct {
	guint16			 match_value;
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
#if LIBXMLB_CHECK_VERSION(0, 3, 0)
		g_auto(XbQueryContext) context = XB_QUERY_CONTEXT_INIT ();
		xb_value_bindings_bind_str (xb_query_context_get_bindings (&context), 0, search, NULL);
		n = xb_node_query_with_context (component, helper->query, &context, NULL);
#else
		xb_query_bind_str (helper->query, 0, search, NULL);
		n = xb_node_query_full (component, helper->query, NULL);
#endif
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

typedef struct {
	guint16			match_value;
	const gchar		*xpath;
} Query;

static gboolean
gs_appstream_do_search (GsPlugin *plugin,
			XbSilo *silo,
			const gchar * const *values,
			const Query queries[],
			GsAppList *list,
			GCancellable *cancellable,
			GError **error)
{
	AsComponentScope default_scope = AS_COMPONENT_SCOPE_UNKNOWN;
	g_autofree gchar *silo_filename = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) array = g_ptr_array_new_with_free_func ((GDestroyNotify) gs_appstream_search_helper_free);
	g_autoptr(GPtrArray) components = NULL;
	g_autoptr(GTimer) timer = g_timer_new ();
#if AS_CHECK_VERSION(1, 0, 0)
	const guint16 component_id_weight = as_utils_get_tag_search_weight ("id");
#else
	const guint16 component_id_weight = AS_SEARCH_TOKEN_MATCH_ID;
#endif

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), FALSE);
	g_return_val_if_fail (XB_IS_SILO (silo), FALSE);
	g_return_val_if_fail (values != NULL, FALSE);
	g_return_val_if_fail (GS_IS_APP_LIST (list), FALSE);

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
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	if (components->len > 0) {
		g_autoptr(XbNode) filename_node = NULL;
		g_autoptr(XbNode) scope_node = NULL;
		filename_node = xb_silo_query_first (silo, "info/filename", NULL);
		if (filename_node != NULL)
			silo_filename = g_strdup (xb_node_get_text (filename_node));
		scope_node = xb_silo_query_first (silo, "info/scope", NULL);
		if (scope_node != NULL && xb_node_get_text (scope_node) != NULL)
			default_scope = as_component_scope_from_string (xb_node_get_text (scope_node));
	}
	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);
		guint16 match_value = gs_appstream_silo_search_component (array, component, values);
		if (match_value != 0) {
			g_autoptr(GsApp) app = gs_appstream_create_app (plugin, silo, component, silo_filename ? silo_filename : "", default_scope, error);
			if (app == NULL)
				return FALSE;
			if (gs_app_has_quirk (app, GS_APP_QUIRK_IS_WILDCARD)) {
				g_debug ("not returning wildcard %s",
					 gs_app_get_unique_id (app));
				continue;
			}
			g_debug ("add %s", gs_app_get_unique_id (app));

			/* The match value is used for prioritising results.
			 * Drop the ID token from it as it’s the highest
			 * numeric value but isn’t visible to the user in the
			 * UI, which leads to confusing results ordering. */
			gs_app_set_match_value (app, match_value & (~component_id_weight));
			gs_app_list_add (list, app);

			if (gs_app_get_kind (app) == AS_COMPONENT_KIND_ADDON) {
				g_autoptr(GPtrArray) extends = NULL;

				/* add the parent app as a wildcard, to be refined later */
				extends = xb_node_query (component, "extends", 0, NULL);
				for (guint jj = 0; extends && jj < extends->len; jj++) {
					XbNode *extend = g_ptr_array_index (extends, jj);
					g_autoptr(GsApp) app2 = NULL;
					const gchar *tmp;
					app2 = gs_app_new (xb_node_get_text (extend));
					gs_app_add_quirk (app2, GS_APP_QUIRK_IS_WILDCARD);
					tmp = xb_node_query_attr (extend, "../..", "origin", NULL);
					if (gs_appstream_origin_valid (tmp))
						gs_app_set_origin_appstream (app2, tmp);
					gs_app_list_add (list, app2);
				}
			}
		}

		if (g_cancellable_set_error_if_cancelled (cancellable, error))
			return FALSE;
	}
	g_debug ("search took %fms", g_timer_elapsed (timer, NULL) * 1000);
	return TRUE;
}

/* This tokenises and stems @values internally for comparison against the
 * already-stemmed tokens in the libxmlb silo */
gboolean
gs_appstream_search (GsPlugin *plugin,
		     XbSilo *silo,
		     const gchar * const *values,
		     GsAppList *list,
		     GCancellable *cancellable,
		     GError **error)
{
#if AS_CHECK_VERSION(1, 0, 0)
	guint16 pkgname_weight = as_utils_get_tag_search_weight ("pkgname");
	guint16 name_weight = as_utils_get_tag_search_weight ("name");
	guint16 id_weight = as_utils_get_tag_search_weight ("id");
	const Query queries[] = {
		{ as_utils_get_tag_search_weight ("mediatype"),	"provides/mediatype[text()~=stem(?)]" },
		/* Search once with a tokenize-and-casefold operator (`~=`) to support casefolded
		 * full-text search, then again using substring matching (`contains()`), to
		 * support prefix matching. Only do the prefix matches on a few fields, and at a
		 * lower priority, otherwise things will get confusing.
		 *
		 * See https://gitlab.gnome.org/GNOME/gnome-software/-/issues/2277 */
		{ pkgname_weight,				"pkgname[text()~=stem(?)]" },
		{ pkgname_weight / 2,				"pkgname[contains(text(),stem(?))]" },
		{ as_utils_get_tag_search_weight ("summary"),	"summary[text()~=stem(?)]" },
		{ name_weight,					"name[text()~=stem(?)]" },
		{ name_weight / 2,				"name[contains(text(),stem(?))]" },
		{ as_utils_get_tag_search_weight ("keyword"),	"keywords/keyword[text()~=stem(?)]" },
		{ id_weight,					"id[text()~=stem(?)]" },
		{ id_weight,					"launchable[text()~=stem(?)]" },
		{ as_utils_get_tag_search_weight ("origin"),	"../components[@origin~=stem(?)]" },
		{ 0,						NULL }
	};
#else
	const Query queries[] = {
		{ AS_SEARCH_TOKEN_MATCH_MEDIATYPE,	"mimetypes/mimetype[text()~=stem(?)]" },
		{ AS_SEARCH_TOKEN_MATCH_PKGNAME,	"pkgname[text()~=stem(?)]" },
		{ AS_SEARCH_TOKEN_MATCH_PKGNAME / 2,	"pkgname[contains(text(),stem(?))]" },
		{ AS_SEARCH_TOKEN_MATCH_SUMMARY,	"summary[text()~=stem(?)]" },
		{ AS_SEARCH_TOKEN_MATCH_NAME,		"name[text()~=stem(?)]" },
		{ AS_SEARCH_TOKEN_MATCH_NAME / 2,	"name[contains(text(),stem(?))]" },
		{ AS_SEARCH_TOKEN_MATCH_KEYWORD,	"keywords/keyword[text()~=stem(?)]" },
		{ AS_SEARCH_TOKEN_MATCH_ID,		"id[text()~=stem(?)]" },
		{ AS_SEARCH_TOKEN_MATCH_ID,		"launchable[text()~=stem(?)]" },
		{ AS_SEARCH_TOKEN_MATCH_ORIGIN,		"../components[@origin~=stem(?)]" },
		{ AS_SEARCH_TOKEN_MATCH_NONE,		NULL }
	};
#endif

	return gs_appstream_do_search (plugin, silo, values, queries, list, cancellable, error);
}

gboolean
gs_appstream_search_developer_apps (GsPlugin *plugin,
				    XbSilo *silo,
				    const gchar * const *values,
				    GsAppList *list,
				    GCancellable *cancellable,
				    GError **error)
{
#if AS_CHECK_VERSION(1, 0, 0)
	const Query queries[] = {
		{ as_utils_get_tag_search_weight ("pkgname"), "developer/name[text()~=stem(?)]" },
		{ as_utils_get_tag_search_weight ("summary"), "project_group[text()~=stem(?)]" },
		/* for legacy support */
		{ as_utils_get_tag_search_weight ("pkgname"), "developer_name[text()~=stem(?)]" },
		{ 0,					      NULL }
	};
#else
	const Query queries[] = {
		{ AS_SEARCH_TOKEN_MATCH_PKGNAME,	"developer_name[text()~=stem(?)]" },
		{ AS_SEARCH_TOKEN_MATCH_SUMMARY,	"project_group[text()~=stem(?)]" },
		{ AS_SEARCH_TOKEN_MATCH_NONE,		NULL }
	};
#endif

	return gs_appstream_do_search (plugin, silo, values, queries, list, cancellable, error);
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

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), FALSE);
	g_return_val_if_fail (XB_IS_SILO (silo), FALSE);
	g_return_val_if_fail (GS_IS_CATEGORY (category), FALSE);
	g_return_val_if_fail (GS_IS_APP_LIST (list), FALSE);

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
		g_autoptr(GError) error_local = NULL;

		/* generate query */
		if (g_strv_length (split) == 1) {
			xpath = g_strdup_printf ("components/component[not(@merge)]/categories/"
						 "category[text()='%s']/../..",
						 split[0]);
		} else if (g_strv_length (split) == 2) {
			xpath = g_strdup_printf ("components/component[not(@merge)]/categories/"
						 "category[text()='%s']/../"
						 "category[text()='%s']/../..",
						 split[0], split[1]);
		}
		components = xb_silo_query (silo, xpath, 0, &error_local);
		if (components == NULL) {
			if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
				continue;
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
			gs_app_set_metadata (app, "GnomeSoftware::Creator",
					     gs_plugin_get_name (plugin));
			gs_app_add_quirk (app, GS_APP_QUIRK_IS_WILDCARD);
			gs_app_list_add (list, app);
		}

	}
	return TRUE;
}

static guint
gs_appstream_count_component_for_groups (XbSilo      *silo,
                                         const gchar *desktop_group)
{
	guint limit = 10;
	g_autofree gchar *xpath = NULL;
	g_auto(GStrv) split = g_strsplit (desktop_group, "::", -1);
	g_autoptr(GPtrArray) array = NULL;
	g_autoptr(GError) error_local = NULL;

	if (g_strv_length (split) == 1) { /* "all" group for a parent category */
		xpath = g_strdup_printf ("components/component[not(@merge)]/categories/"
					 "category[text()='%s']/../..",
					 split[0]);
	} else if (g_strv_length (split) == 2) {
		xpath = g_strdup_printf ("components/component[not(@merge)]/categories/"
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
		g_warning ("%s", error_local->message);
		return 0;
	}
	return array->len;
}

/* we're not actually adding categories here, we're just setting the number of
 * apps available in each category */
gboolean
gs_appstream_refine_category_sizes (XbSilo        *silo,
                                    GPtrArray     *list,
                                    GCancellable  *cancellable,
                                    GError       **error)
{
	g_return_val_if_fail (XB_IS_SILO (silo), FALSE);
	g_return_val_if_fail (list != NULL, FALSE);

	for (guint j = 0; j < list->len; j++) {
		GsCategory *parent = GS_CATEGORY (g_ptr_array_index (list, j));
		GPtrArray *children = gs_category_get_children (parent);

		for (guint i = 0; i < children->len; i++) {
			GsCategory *cat = g_ptr_array_index (children, i);
			GPtrArray *groups = gs_category_get_desktop_groups (cat);
			for (guint k = 0; k < groups->len; k++) {
				const gchar *group = g_ptr_array_index (groups, k);
				guint cnt = gs_appstream_count_component_for_groups (silo, group);
				if (cnt > 0) {
					gs_category_increment_size (parent, cnt);
					if (children->len > 1) {
						/* Parent category has multiple groups, so increment
						 * each group's size too */
						gs_category_increment_size (cat, cnt);
					}
				}
			}
		}
		continue;
	}
	return TRUE;
}

gboolean
gs_appstream_add_installed (GsPlugin      *plugin,
                            XbSilo        *silo,
                            GsAppList     *list,
                            GCancellable  *cancellable,
                            GError       **error)
{
	g_autoptr(GPtrArray) components = NULL;

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), FALSE);
	g_return_val_if_fail (XB_IS_SILO (silo), FALSE);
	g_return_val_if_fail (GS_IS_APP_LIST (list), FALSE);

	/* get all installed appdata files (notice no 'components/' prefix...) */
	components = xb_silo_query (silo, "component/description/..", 0, NULL);
	if (components == NULL)
		return TRUE;

	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);
		g_autoptr(GsApp) app = gs_appstream_create_app (plugin, silo, component, NULL, AS_COMPONENT_SCOPE_UNKNOWN, error);
		if (app == NULL)
			return FALSE;

		/* Can get cached GsApp, which has the state already updated */
		if (gs_app_get_state (app) != GS_APP_STATE_UPDATABLE &&
		    gs_app_get_state (app) != GS_APP_STATE_UPDATABLE_LIVE)
			gs_app_set_state (app, GS_APP_STATE_INSTALLED);
		gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
		gs_app_list_add (list, app);
	}

	return TRUE;
}

gboolean
gs_appstream_add_popular (XbSilo *silo,
			  GsAppList *list,
			  GCancellable *cancellable,
			  GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) array = NULL;

	g_return_val_if_fail (XB_IS_SILO (silo), FALSE);
	g_return_val_if_fail (GS_IS_APP_LIST (list), FALSE);

	/* find out how many packages are in each category */
	array = xb_silo_query (silo,
			       "components/component/kudos/"
			       "kudo[text()='GnomeSoftware::popular']/../..",
			       0, &error_local);
	if (array == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
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
	AsComponentScope default_scope = AS_COMPONENT_SCOPE_UNKNOWN;
	guint64 now = (guint64) g_get_real_time () / G_USEC_PER_SEC, max_future_timestamp;
	g_autofree gchar *xpath = NULL;
	g_autofree gchar *silo_filename = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) array = NULL;

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), FALSE);
	g_return_val_if_fail (XB_IS_SILO (silo), FALSE);
	g_return_val_if_fail (GS_IS_APP_LIST (list), FALSE);

	/* use predicate conditions to the max */
	xpath = g_strdup_printf ("components/component/releases/"
				 "release[@timestamp>%" G_GUINT64_FORMAT "]/../..",
				 now - age);
	array = xb_silo_query (silo, xpath, 0, &error_local);
	if (array == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
			return TRUE;
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	if (array->len > 0) {
		g_autoptr(XbNode) filename_node = NULL;
		g_autoptr(XbNode) scope_node = NULL;
		filename_node = xb_silo_query_first (silo, "info/filename", NULL);
		if (filename_node != NULL)
			silo_filename = g_strdup (xb_node_get_text (filename_node));
		scope_node = xb_silo_query_first (silo, "info/scope", NULL);
		if (scope_node != NULL && xb_node_get_text (scope_node) != NULL)
			default_scope = as_component_scope_from_string (xb_node_get_text (scope_node));
	}
	/* This is to cover mistakes when the release date is set in the future,
	   to not have it picked for too long. */
	max_future_timestamp = now + (3 * 24 * 60 * 60);
	for (guint i = 0; i < array->len; i++) {
		XbNode *component = g_ptr_array_index (array, i);
		g_autoptr(GsApp) app = NULL;
		guint64 timestamp = component_get_release_timestamp (component);
		/* set the release date */
		if (timestamp != G_MAXUINT64 && timestamp < max_future_timestamp) {
			app = gs_appstream_create_app (plugin, silo, component, silo_filename ? silo_filename : "", default_scope, error);
			if (app == NULL)
				return FALSE;

			gs_app_set_release_date (app, timestamp);
			gs_app_list_add (list, app);
		}
	}
	return TRUE;
}

gboolean
gs_appstream_add_alternates (XbSilo *silo,
			     GsApp *app,
			     GsAppList *list,
			     GCancellable *cancellable,
			     GError **error)
{
	GPtrArray *sources = gs_app_get_sources (app);
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) ids = NULL;
	g_autoptr(GString) xpath = g_string_new (NULL);

	g_return_val_if_fail (XB_IS_SILO (silo), FALSE);
	g_return_val_if_fail (GS_IS_APP (app), FALSE);
	g_return_val_if_fail (GS_IS_APP_LIST (list), FALSE);

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
		g_propagate_error (error, g_steal_pointer (&error_local));
		return FALSE;
	}
	for (guint i = 0; i < ids->len; i++) {
		XbNode *n = g_ptr_array_index (ids, i);
		g_autoptr(GsApp) app2 = NULL;
		const gchar *tmp;
		app2 = gs_app_new (xb_node_get_text (n));
		gs_app_add_quirk (app2, GS_APP_QUIRK_IS_WILDCARD);

		tmp = xb_node_query_attr (n, "../..", "origin", NULL);
		if (gs_appstream_origin_valid (tmp))
			gs_app_set_origin_appstream (app2, tmp);
		gs_app_list_add (list, app2);
	}
	return TRUE;
}

static gboolean
gs_appstream_add_featured_with_query (XbSilo *silo,
				      const gchar *query,
				      GsAppList *list,
				      GCancellable *cancellable,
				      GError **error)
{
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GPtrArray) array = NULL;

	g_return_val_if_fail (XB_IS_SILO (silo), FALSE);
	g_return_val_if_fail (GS_IS_APP_LIST (list), FALSE);

	/* find out how many packages are in each category */
	array = xb_silo_query (silo, query, 0, &error_local);
	if (array == NULL) {
		if (g_error_matches (error_local, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
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

gboolean
gs_appstream_add_featured (XbSilo *silo,
			   GsAppList *list,
			   GCancellable *cancellable,
			   GError **error)
{
	const gchar *query = "components/component/custom/value[@key='GnomeSoftware::FeatureTile']/../..|"
			     "components/component/custom/value[@key='GnomeSoftware::FeatureTile-css']/../..";
	return gs_appstream_add_featured_with_query (silo, query, list, cancellable, error);
}

gboolean
gs_appstream_add_deployment_featured (XbSilo *silo,
				      const gchar * const *deployments,
				      GsAppList *list,
				      GCancellable *cancellable,
				      GError **error)
{
	g_autoptr(GString) query = g_string_new (NULL);
	g_return_val_if_fail (XB_IS_SILO (silo), FALSE);
	g_return_val_if_fail (deployments != NULL, FALSE);
	g_return_val_if_fail (GS_IS_APP_LIST (list), FALSE);
	for (guint ii = 0; deployments[ii] != NULL; ii++) {
		g_autofree gchar *escaped = xb_string_escape (deployments[ii]);
		if (escaped != NULL && *escaped != '\0') {
			xb_string_append_union (query,
				"components/component/custom/value[@key='GnomeSoftware::DeploymentFeatured'][text()='%s']/../..",
				escaped);
		}
	}
	if (!query->len)
		return TRUE;
	return gs_appstream_add_featured_with_query (silo, query->str, list, cancellable, error);
}

gboolean
gs_appstream_url_to_app (GsPlugin *plugin,
			 XbSilo *silo,
			 GsAppList *list,
			 const gchar *url,
			 GCancellable *cancellable,
			 GError **error)
{
	g_autofree gchar *path = NULL;
	g_autofree gchar *scheme = NULL;
	g_autofree gchar *xpath = NULL;
	g_autoptr(GPtrArray) components = NULL;

	g_return_val_if_fail (GS_IS_PLUGIN (plugin), FALSE);
	g_return_val_if_fail (XB_IS_SILO (silo), FALSE);
	g_return_val_if_fail (GS_IS_APP_LIST (list), FALSE);
	g_return_val_if_fail (url != NULL, FALSE);

	/* not us */
	scheme = gs_utils_get_url_scheme (url);
	if (g_strcmp0 (scheme, "appstream") != 0)
		return TRUE;

	path = gs_utils_get_url_path (url);
	xpath = g_strdup_printf ("components/component/id[text()='%s']/..", path);
	components = xb_silo_query (silo, xpath, 0, NULL);
	if (components == NULL)
		return TRUE;

	for (guint i = 0; i < components->len; i++) {
		XbNode *component = g_ptr_array_index (components, i);
		g_autoptr(GsApp) app = NULL;
		app = gs_appstream_create_app (plugin, silo, component, NULL, AS_COMPONENT_SCOPE_UNKNOWN, error);
		if (app == NULL)
			return FALSE;
		gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
		gs_app_list_add (list, app);
	}

	return TRUE;
}

static GInputStream *
gs_appstream_load_desktop_cb (XbBuilderSource *self,
			      XbBuilderSourceCtx *ctx,
			      gpointer user_data,
			      GCancellable *cancellable,
			      GError **error)
{
	g_autofree gchar *xml = NULL;
	g_autoptr(AsComponent) cpt = as_component_new ();
	g_autoptr(AsContext) actx = as_context_new ();
	g_autoptr(GBytes) bytes = NULL;
	gboolean ret;

	bytes = xb_builder_source_ctx_get_bytes (ctx, cancellable, error);
	if (bytes == NULL)
		return NULL;

	as_component_set_id (cpt, xb_builder_source_ctx_get_filename (ctx));
	ret = as_component_load_from_bytes (cpt,
					   actx,
					   AS_FORMAT_KIND_DESKTOP_ENTRY,
					   bytes,
					   error);
	if (!ret)
		return NULL;
	xml = as_component_to_xml_data (cpt, actx, error);
	if (xml == NULL)
		return NULL;
	return g_memory_input_stream_new_from_data (g_steal_pointer (&xml), (gssize) -1, g_free);
}

static gboolean
gs_appstream_load_desktop_fn (XbBuilder     *builder,
			      const gchar   *filename,
			      GCancellable  *cancellable,
			      GError        **error)
{
	g_autoptr(GFile) file = g_file_new_for_path (filename);
	g_autoptr(XbBuilderNode) info = NULL;
	g_autoptr(XbBuilderSource) source = xb_builder_source_new ();

	/* add support for desktop files */
	xb_builder_source_add_adapter (source, "application/x-desktop",
				       gs_appstream_load_desktop_cb, NULL, NULL);

	/* add source */
	if (!xb_builder_source_load_file (source, file, 0, cancellable, error))
		return FALSE;

	/* add metadata */
	info = xb_builder_node_insert (NULL, "info", NULL);
	xb_builder_node_insert_text (info, "filename", filename, NULL);
	xb_builder_source_set_info (source, info);

	/* success */
	xb_builder_import_source (builder, source);
	return TRUE;
}

gboolean
gs_appstream_load_desktop_files (XbBuilder      *builder,
				 const gchar    *path,
				 gboolean	*out_any_loaded,
				 GFileMonitor  **out_file_monitor,
				 GCancellable   *cancellable,
				 GError         **error)
{
	const gchar *fn;
	g_autoptr(GDir) dir = NULL;
	g_autoptr(GFile) parent = g_file_new_for_path (path);
	if (out_any_loaded)
		*out_any_loaded = FALSE;
	if (!g_file_query_exists (parent, cancellable)) {
		g_debug ("appstream: Skipping desktop path '%s' as %s", path, g_cancellable_is_cancelled (cancellable) ? "cancelled" : "does not exist");
		return TRUE;
	}

	g_debug ("appstream: Loading desktop path '%s'", path);

	dir = g_dir_open (path, 0, error);
	if (dir == NULL)
		return FALSE;

	if (out_file_monitor != NULL) {
		g_autoptr(GError) error_local = NULL;
		*out_file_monitor = g_file_monitor (parent, G_FILE_MONITOR_NONE, cancellable, &error_local);
		if (error_local)
			g_debug ("appstream: Failed to create file monitor for '%s': %s", path, error_local->message);
	}

	while ((fn = g_dir_read_name (dir)) != NULL) {
		if (g_str_has_suffix (fn, ".desktop")) {
			g_autofree gchar *filename = g_build_filename (path, fn, NULL);
			g_autoptr(GError) error_local = NULL;
			if (g_strcmp0 (fn, "mimeinfo.cache") == 0)
				continue;
			if (!gs_appstream_load_desktop_fn (builder,
							   filename,
							   cancellable,
							   &error_local)) {
				g_debug ("ignoring %s: %s", filename, error_local->message);
				continue;
			}
			if (out_any_loaded)
				*out_any_loaded = TRUE;
		}
	}

	/* success */
	return TRUE;
}

void
gs_appstream_component_add_keyword (XbBuilderNode *component, const gchar *str)
{
	g_autoptr(XbBuilderNode) keyword = NULL;
	g_autoptr(XbBuilderNode) keywords = NULL;

	g_return_if_fail (XB_IS_BUILDER_NODE (component));
	g_return_if_fail (str != NULL);

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

	g_return_if_fail (XB_IS_BUILDER_NODE (component));
	g_return_if_fail (str != NULL);

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

	g_return_if_fail (XB_IS_BUILDER_NODE (component));
	g_return_if_fail (str != NULL);

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

	g_return_if_fail (XB_IS_BUILDER_NODE (component));
	g_return_if_fail (str != NULL);

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
gs_appstream_component_add_extra_info (XbBuilderNode *component)
{
	const gchar *kind;

	g_return_if_fail (XB_IS_BUILDER_NODE (component));

	kind = xb_builder_node_get_attr (component, "type");

	/* add the gnome-software-specific 'Addon' group and ensure they
	 * all have an icon set */
	switch (as_component_kind_from_string (kind)) {
	case AS_COMPONENT_KIND_WEB_APP:
		gs_appstream_component_add_keyword (component, kind);
		break;
	case AS_COMPONENT_KIND_FONT:
		gs_appstream_component_add_category (component, "Addon");
		gs_appstream_component_add_category (component, "Font");
		break;
	case AS_COMPONENT_KIND_DRIVER:
		gs_appstream_component_add_category (component, "Addon");
		gs_appstream_component_add_category (component, "Driver");
		gs_appstream_component_add_icon (component, "system-component-driver");
		break;
	case AS_COMPONENT_KIND_LOCALIZATION:
		gs_appstream_component_add_category (component, "Addon");
		gs_appstream_component_add_category (component, "Localization");
		gs_appstream_component_add_icon (component, "system-component-language");
		break;
	case AS_COMPONENT_KIND_CODEC:
		gs_appstream_component_add_category (component, "Addon");
		gs_appstream_component_add_category (component, "Codec");
		gs_appstream_component_add_icon (component, "system-component-codecs");
		break;
	case AS_COMPONENT_KIND_INPUT_METHOD:
		gs_appstream_component_add_keyword (component, kind);
		gs_appstream_component_add_category (component, "Addon");
		gs_appstream_component_add_category (component, "InputSource");
		gs_appstream_component_add_icon (component, "system-component-input-sources");
		break;
	case AS_COMPONENT_KIND_FIRMWARE:
		gs_appstream_component_add_icon (component, "system-component-firmware");
		break;
	default:
		break;
	}
}

/* Resolve any media URIs which are actually relative
 * paths against the media_baseurl property */
void
gs_appstream_component_fix_url (XbBuilderNode *component, const gchar *baseurl)
{
	const gchar *text;
	g_autofree gchar *url = NULL;

	g_return_if_fail (XB_IS_BUILDER_NODE (component));
	g_return_if_fail (baseurl != NULL);

	text = xb_builder_node_get_text (component);

	if (text == NULL)
		return;

	if (g_str_has_prefix (text, "http:") ||
	    g_str_has_prefix (text, "https:"))
		return;

	url = g_strconcat (baseurl, "/", text, NULL);
	xb_builder_node_set_text (component, url , -1);
}
