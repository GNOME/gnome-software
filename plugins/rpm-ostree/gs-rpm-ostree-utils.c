/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * SPDX-FileCopyrightText: (C) 2025 Red Hat <www.redhat.com>
 */

#include <config.h>

#include <gnome-software.h>

#include "gs-rpm-ostree-utils.h"

static const gchar *
find_char_on_line (const gchar *txt,
		   gchar chr,
		   guint nth)
{
	g_assert (nth >= 1);
	while (*txt != '\n' && *txt != '\0') {
		if (*txt == chr) {
			nth--;
			if (nth == 0)
				break;
		}
		txt++;
	}
	return (*txt == chr && nth == 0) ? txt : NULL;
}

/* Extract date from "* Thu Aug 14 2025 ...." */
static void
extract_latest_date (const gchar *read_pos,
		     GDate *latest_date)
{
	const gchar *start, *end;

	start = find_char_on_line (read_pos, ' ', 2);
	if (start != NULL) {
		start++;
		end = find_char_on_line (start, ' ', 3);
		if (end != NULL) {
			GDate date;
			g_autofree gchar *str = g_strndup (start, end - start);
			g_date_clear (&date, 1);
			g_date_set_parse (&date, str);
			if (g_date_valid (&date)) {
				if (!g_date_valid (latest_date) || g_date_compare (latest_date, &date) < 0) {
					*latest_date = date;
				}
			}
		}
	}
}

static void
sanitize_update_history_text (gchar *text,
			      guint64 *out_latest_date)
{
	GDate latest_date;
	gchar *read_pos = text, *write_pos = text;

	g_date_clear (&latest_date, 1);

	#define skip_after(_chr) G_STMT_START { \
		while (*read_pos != '\0' && *read_pos != '\n' && *read_pos != (_chr)) { \
			if (read_pos != write_pos) \
				*write_pos = *read_pos; \
			read_pos++; \
			write_pos++; \
		} \
		if (*read_pos == (_chr)) { \
			if (read_pos != write_pos) \
				*write_pos = *read_pos; \
			read_pos++; \
			write_pos++; \
		} \
	} G_STMT_END
	#define skip_whitespace() G_STMT_START { \
		while (*read_pos != '\0' && *read_pos != '\n' && g_ascii_isspace (*read_pos)) { \
			if (read_pos != write_pos) \
				*write_pos = *read_pos; \
			read_pos++; \
			write_pos++; \
		} \
	} G_STMT_END

	/* The first two lines begin with "ostree diff commit from/to:" - skip them. */
	if (g_ascii_strncasecmp (read_pos, "ostree diff", strlen ("ostree diff")) == 0)
		skip_after ('\n');
	if (g_ascii_strncasecmp (read_pos, "ostree diff", strlen ("ostree diff")) == 0)
		skip_after ('\n');
	write_pos = text;

	while (*read_pos != '\0') {
		skip_whitespace ();

		if (*read_pos == '*') {
			const gchar *start, *end;

			extract_latest_date (read_pos, &latest_date);

			/* Hide email addresses */
			start = find_char_on_line (read_pos, '<', 1);
			if (start != NULL) {
				end = find_char_on_line (start, '>', 1);
				if (end != NULL) {
					while (read_pos < start) {
						if (read_pos != write_pos)
							*write_pos = *read_pos;
						read_pos++;
						write_pos++;
					}
					read_pos += end - read_pos;
					if (*read_pos == '>' && g_ascii_isspace (read_pos[1]))
						read_pos += 2;
				}
			}
		}

		skip_after ('\n');
	}

	#undef skip_until
	#undef skip_whitespace

	if (read_pos != write_pos)
		*write_pos = '\0';

	if (g_date_valid (&latest_date)) {
		g_autoptr(GDateTime) date_time = g_date_time_new_utc (g_date_get_year (&latest_date),
								      g_date_get_month (&latest_date),
								      g_date_get_day (&latest_date),
								      0, 0, 0.0);
		*out_latest_date = g_date_time_to_unix (date_time);
	}
}

/* returns whether could find next line start, which is a pointer
   to either '\n' or '\0' character in *out_next_line_start */
static gboolean
get_next_changelog_line (gchar *from,
			 gchar **out_next_line_start)
{
	gchar *end_ptr;

	if (from == NULL || *from == '\0') {
		*out_next_line_start = from;
		return FALSE;
	}

	for (end_ptr = from; *end_ptr != '\0' && *end_ptr != '\n'; end_ptr++) {
		/* just move until end-of-line or end-of-buffer is reached */
	}

	*out_next_line_start = end_ptr;

	return TRUE;
}

/* Expected text structure is below. The pipe is not part of the text,
   it's the beginning of the line, like a cursor, to highlight significant
   left spaces of the expected input.

   |Upgraded:
   |  package_name version_from -> version_to
   |  package_name version_from -> version_to
   |    changelog entries
   |
   |    changelog entries
   |
   |  package_name ....
   |
   |Downgraded:
   |  package_name version_from -> version_to
   |  package_name version_from -> version_to
   |
   |Removed:
   |   package_nevra
   |   package_nevra
   |
   |Added:
   |   package_nevra
   |   package_nevra
*/
static void
split_changelogs (GsApp *owner_app,
		  gchar *changelogs)
{
	g_autoptr(GsApp) app = NULL;
	g_autoptr(GString) text = NULL;
	g_autoptr(GsPlugin) plugin = gs_app_dup_management_plugin (owner_app);
	GsAppState state = GS_APP_STATE_UNKNOWN;
	GDate latest_date;
	gchar *from = changelogs, *next_line = NULL;
	gboolean has_empty_line = FALSE;

	g_date_clear (&latest_date, 1);

	#define	finish_app() { \
		if (app != NULL && text != NULL) { \
			gs_app_set_update_details_text (app, text->str); \
			if (g_date_valid (&latest_date)) { \
				g_autoptr(GDateTime) date_time = g_date_time_new_utc (g_date_get_year (&latest_date), \
										      g_date_get_month (&latest_date), \
										      g_date_get_day (&latest_date), \
										      0, 0, 0.0); \
				gs_app_set_install_date (app, g_date_time_to_unix (date_time)); \
			} \
		} \
		if (text != NULL) \
			g_string_free (g_steal_pointer (&text), TRUE); \
		g_date_clear (&latest_date, 1); \
		g_clear_object (&app); \
		has_empty_line = FALSE; \
	}

	while (get_next_changelog_line (from, &next_line)) {
		gboolean restore_eol = *next_line == '\n';

		*next_line = '\0';

		if (g_ascii_strcasecmp (from, "Added:") == 0) {
			finish_app ();
			state = GS_APP_STATE_AVAILABLE;
		} else if (g_ascii_strcasecmp (from, "Removed:") == 0) {
			finish_app ();
			state = GS_APP_STATE_UNAVAILABLE;
		} else if (g_ascii_strcasecmp (from, "Upgraded:") == 0) {
			finish_app ();
			state = GS_APP_STATE_UPDATABLE;
		} else if (g_ascii_strcasecmp (from, "Downgraded:") == 0) {
			finish_app ();
			/* use this for downgrades, which are recognized by version compare, not by the state here */
			state = GS_APP_STATE_UPDATABLE_LIVE;
		} else if (*from == '\0') {
			/* maybe continuation or divider */
			has_empty_line = TRUE;
		} else if (from[0] == ' ' && from[1] == ' ' && from[2] == ' ' && from[3] == ' ') {
			/* continuation */
			if (text != NULL) {
				if (has_empty_line)
					g_string_append_c (text, '\n');
				if (text->len > 0)
					g_string_append_c (text, '\n');
				g_string_append (text, from + 4);

				if (from[4] == '*')
					extract_latest_date (from + 4, &latest_date);
			}
			has_empty_line = FALSE;
		} else if (from[0] == ' ' && from[1] == ' ') {
			/* next package */
			const gchar *package_line = from + 2;
			if (state == GS_APP_STATE_UPDATABLE) {
				if (has_empty_line)
					finish_app ();
				if (app == NULL) {
					/* it should look like 'name version_from -> version_to'*/
					gchar *space = strchr (package_line, ' ');
					if (space != NULL)
						*space = '\0';
					app = gs_app_new (package_line);
					gs_app_set_management_plugin (app, plugin);
					gs_app_set_name (app, GS_APP_QUALITY_NORMAL, package_line);
					gs_app_add_source (app, package_line);
					if (space != NULL) {
						gchar *tmp;
						tmp = strstr (space + 1, " -> ");
						if (tmp) {
							*tmp = '\0';
							gs_app_set_version (app, space + 1);
							*tmp = ' ';
							gs_app_set_update_version (app, tmp + 4);
						}
						*space = ' ';
					}
					gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
					gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
					gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
					gs_app_set_state (app, state);
					/* it's known whether the details are available or not; this will not
					   cause load of the details when the app is selected */
					gs_app_set_update_details_text (app, NULL);
					gs_app_add_related (owner_app, app);
					text = g_string_new (NULL);
				} else {
					/* ignore subpackages; uncommenting the below text will add them into the details */

					/*gchar *space = strchr (package_line, ' ');
					g_assert (text != NULL);
					/ * the section covers more than one package, add the initial too * /
					if (text->len == 0)
						g_string_append (text, gs_app_get_source_default (app));
					g_string_append_c (text, '\n');
					if (space != NULL)
						*space = '\0';
					g_string_append (text, package_line);
					if (space != NULL)
						*space = ' ';*/
				}
			} else if (state == GS_APP_STATE_UPDATABLE_LIVE) {
				/* it should look like 'name version_from -> version_to'*/
				gchar *space = strchr (package_line, ' ');
				if (space != NULL) {
					gchar *tmp;

					*space = '\0';

					tmp = strstr (space + 1, " -> ");
					if (tmp) {
						app = gs_app_new (package_line);
						gs_app_set_management_plugin (app, plugin);
						gs_app_set_name (app, GS_APP_QUALITY_NORMAL, package_line);
						gs_app_add_source (app, package_line);
						gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
						gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
						gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
						gs_app_set_state (app, GS_APP_STATE_UPDATABLE);

						*tmp = '\0';
						gs_app_set_version (app, space + 1);
						*tmp = ' ';
						gs_app_set_update_version (app, tmp + 4);
						/* it's known whether the details are available or not; this will not
						   cause load of the details when the app is selected */
						gs_app_set_update_details_text (app, NULL);
						gs_app_add_related (owner_app, app);
						g_clear_object (&app);
					}
					*space = ' ';
				}
			} else if (state != GS_APP_STATE_UNKNOWN) {
				app = gs_app_new (package_line);
				gs_app_set_management_plugin (app, plugin);
				gs_app_set_kind (app, AS_COMPONENT_KIND_GENERIC);
				gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
				gs_app_set_scope (app, AS_COMPONENT_SCOPE_SYSTEM);
				gs_app_set_state (app, state);
				gs_app_set_name (app, GS_APP_QUALITY_NORMAL, package_line);
				gs_app_add_source (app, package_line);
				/* it's known whether the details are available or not; this will not
				   cause load of the details when the app is selected */
				gs_app_set_update_details_text (app, NULL);
				gs_app_add_related (owner_app, app);
				g_clear_object (&app);
			}
			has_empty_line = FALSE;
		} else {
			/* something else */
			g_debug ("%s: unknown line '%s'", G_STRFUNC, from);
		}

		if (restore_eol) {
			*next_line = '\n';
			from = next_line + 1;
		} else {
			from = next_line;
		}
	}

	finish_app ();

	#undef finish_app

	if (gs_app_get_related (owner_app) != NULL &&
	    gs_app_list_length (gs_app_get_related (owner_app)) > 0) {
		gs_app_add_quirk (owner_app, GS_APP_QUIRK_IS_PROXY);
		gs_app_set_special_kind (owner_app, GS_APP_SPECIAL_KIND_OS_UPDATE);
	}
}

/**
 * gs_rpm_ostree_refine_app_from_changelogs:
 * @owner_app: a #GsApp
 * @in_changelogs: (in) (nullable) (transfer full): raw changelogs returned by rpm-ostree
 *
 * Splits @in_changelogs text into respective apps and adds them
 * into the @owner_app as related apps.
 *
 * The @in_changelogs is an output of `rpm-ostree db diff --changelogs --format=block` command.
 *
 * Since: 50
 **/
void
gs_rpm_ostree_refine_app_from_changelogs (GsApp *owner_app,
					  gchar *in_changelogs)
{
	g_autofree gchar *changelogs = g_steal_pointer (&in_changelogs);
	guint64 latest_date = 0;

	g_return_if_fail (GS_IS_APP (owner_app));

	if (changelogs == NULL || *changelogs == '\0')
		return;

	sanitize_update_history_text (changelogs, &latest_date);
	if (latest_date != 0)
		gs_app_set_install_date (owner_app, latest_date);

	split_changelogs (owner_app, changelogs);
}
