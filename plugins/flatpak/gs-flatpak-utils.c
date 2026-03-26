/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2017-2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <config.h>
#include <ostree.h>

#include <glib/gi18n.h>

#include "gs-flatpak-app.h"
#include "gs-flatpak.h"
#include "gs-flatpak-utils.h"

void
gs_flatpak_error_convert (GError **perror)
{
	GError *error = perror != NULL ? *perror : NULL;

	/* not set */
	if (error == NULL)
		return;

	/* this are allowed for low-level errors */
	if (gs_utils_error_convert_gio (perror))
		return;

	/* this are allowed for low-level errors */
	if (gs_utils_error_convert_gdbus (perror))
		return;

	/* this are allowed for network ops */
	if (gs_utils_error_convert_gresolver (perror))
		return;

	/* custom to this plugin */
	if (error->domain == FLATPAK_ERROR) {
		switch (error->code) {
		case FLATPAK_ERROR_ALREADY_INSTALLED:
		case FLATPAK_ERROR_NOT_INSTALLED:
			error->code = GS_PLUGIN_ERROR_NOT_SUPPORTED;
			break;
		case FLATPAK_ERROR_OUT_OF_SPACE:
			error->code = GS_PLUGIN_ERROR_NO_SPACE;
			break;
		case FLATPAK_ERROR_INVALID_REF:
		case FLATPAK_ERROR_INVALID_DATA:
			error->code = GS_PLUGIN_ERROR_INVALID_FORMAT;
			break;
		default:
			error->code = GS_PLUGIN_ERROR_FAILED;
			break;
		}
	} else if (error->domain == OSTREE_GPG_ERROR) {
		error->code = GS_PLUGIN_ERROR_NO_SECURITY;
	} else {
		g_warning ("can't reliably fixup error from domain %s: %s",
			   g_quark_to_string (error->domain),
			   error->message);
		error->code = GS_PLUGIN_ERROR_FAILED;
	}
	error->domain = GS_PLUGIN_ERROR;
}

GsApp *
gs_flatpak_app_new_from_remote (GsPlugin *plugin,
				FlatpakRemote *xremote,
				gboolean is_user)
{
	g_autofree gchar *title = NULL;
	g_autofree gchar *url = NULL;
	g_autofree gchar *filter = NULL;
	g_autofree gchar *description = NULL;
	g_autofree gchar *comment = NULL;
	g_autoptr(GsApp) app = NULL;

	app = gs_flatpak_app_new (flatpak_remote_get_name (xremote));
	gs_app_set_kind (app, AS_COMPONENT_KIND_REPOSITORY);
	gs_app_set_state (app, flatpak_remote_get_disabled (xremote) ?
			  GS_APP_STATE_AVAILABLE : GS_APP_STATE_INSTALLED);
	gs_app_add_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);
	gs_app_set_name (app, GS_APP_QUALITY_LOWEST,
			 flatpak_remote_get_name (xremote));
	gs_app_set_size_download (app, GS_SIZE_TYPE_UNKNOWABLE, 0);
	gs_app_set_management_plugin (app, plugin);
	gs_flatpak_app_set_packaging_info (app);
	gs_app_set_scope (app, is_user ? AS_COMPONENT_SCOPE_USER : AS_COMPONENT_SCOPE_SYSTEM);

	gs_app_set_metadata (app, "GnomeSoftware::SortKey", "100");
	gs_app_set_metadata (app, "GnomeSoftware::InstallationKind",
		is_user ? _("User Installation") : _("System Installation"));

	/* title */
	title = flatpak_remote_get_title (xremote);
	if (title != NULL) {
		gs_app_set_summary (app, GS_APP_QUALITY_LOWEST, title);
		gs_app_set_name (app, GS_APP_QUALITY_NORMAL, title);
	}

	/* origin_ui on a remote is the repo dialogue section name,
	 * not the remote title */
	gs_app_set_origin_ui (app, _("Apps"));

	description = flatpak_remote_get_description (xremote);
	if (description != NULL)
		gs_app_set_description (app, GS_APP_QUALITY_NORMAL, description);

	/* url */
	url = flatpak_remote_get_url (xremote);
	if (url != NULL)
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, url);

	filter = flatpak_remote_get_filter (xremote);
	if (filter != NULL)
		gs_flatpak_app_set_repo_filter (app, filter);

	comment = flatpak_remote_get_comment (xremote);
	if (comment != NULL)
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, comment);

	/* success */
	return g_steal_pointer (&app);
}

GsApp *
gs_flatpak_app_new_from_repo_file (GFile *file,
				   GCancellable *cancellable,
				   GError **error)
{
	gchar *tmp;
	g_autofree gchar *basename = NULL;
	g_autofree gchar *filename = NULL;
	g_autofree gchar *repo_comment = NULL;
	g_autofree gchar *repo_default_branch = NULL;
	g_autofree gchar *repo_description = NULL;
	g_autofree gchar *repo_gpgkey = NULL;
	g_autofree gchar *repo_homepage = NULL;
	g_autofree gchar *repo_icon = NULL;
	g_autofree gchar *repo_id = NULL;
	g_autofree gchar *repo_title = NULL;
	g_autofree gchar *repo_url = NULL;
	g_autofree gchar *repo_filter = NULL;
	g_autoptr(GError) error_local = NULL;
	g_autoptr(GKeyFile) kf = NULL;
	g_autoptr(GsApp) app = NULL;

	/* read the file */
	kf = g_key_file_new ();
	filename = g_file_get_path (file);
	if (!g_key_file_load_from_file (kf, filename,
					G_KEY_FILE_NONE,
					&error_local)) {
		g_set_error (error,
			     GS_PLUGIN_ERROR,
			     GS_PLUGIN_ERROR_NOT_SUPPORTED,
			     "failed to load flatpakrepo: %s",
			     error_local->message);
		return NULL;
	}

	/* get the ID from the basename */
	basename = g_file_get_basename (file);
	tmp = g_strrstr (basename, ".");
	if (tmp != NULL)
		*tmp = '\0';

	/* ensure this is valid for flatpak */
	if (ostree_validate_remote_name (basename, NULL)) {
		repo_id = g_steal_pointer (&basename);
	} else {
		repo_id = g_str_to_ascii (basename, NULL);

		for (guint i = 0; repo_id[i] != '\0'; i++) {
			if (!g_ascii_isalnum (repo_id[i]))
				repo_id[i] = '_';
		}
	}

	/* create source */
	repo_title = g_key_file_get_string (kf, "Flatpak Repo", "Title", NULL);
	repo_url = g_key_file_get_string (kf, "Flatpak Repo", "Url", NULL);
	if (repo_title == NULL || repo_url == NULL ||
	    repo_title[0] == '\0' || repo_url[0] == '\0') {
		g_set_error_literal (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "not enough data in file, "
				     "expected at least Title and Url");
		return NULL;
	}

	/* check version */
	if (g_key_file_has_key (kf, "Flatpak Repo", "Version", NULL)) {
		guint64 ver = g_key_file_get_uint64 (kf, "Flatpak Repo", "Version", NULL);
		if (ver != 1) {
			g_set_error (error,
				     GS_PLUGIN_ERROR,
				     GS_PLUGIN_ERROR_NOT_SUPPORTED,
				     "unsupported version %" G_GUINT64_FORMAT, ver);
			return NULL;
		}
	}

	/* create source */
	app = gs_flatpak_app_new (repo_id);
	gs_flatpak_app_set_file_kind (app, GS_FLATPAK_APP_FILE_KIND_REPO);
	gs_app_set_kind (app, AS_COMPONENT_KIND_REPOSITORY);
	gs_app_set_state (app, GS_APP_STATE_AVAILABLE_LOCAL);
	gs_app_add_quirk (app, GS_APP_QUIRK_NOT_LAUNCHABLE);
	gs_app_set_name (app, GS_APP_QUALITY_NORMAL, repo_title);
	gs_app_set_size_download (app, GS_SIZE_TYPE_UNKNOWABLE, 0);
	gs_flatpak_app_set_repo_url (app, repo_url);
	gs_app_set_origin_ui (app, repo_title);
	gs_app_set_origin_hostname (app, repo_url);

	/* user specified a URL */
	repo_gpgkey = g_key_file_get_string (kf, "Flatpak Repo", "GPGKey", NULL);
	if (repo_gpgkey != NULL) {
		if (g_str_has_prefix (repo_gpgkey, "http://") ||
		    g_str_has_prefix (repo_gpgkey, "https://")) {
			g_set_error_literal (error,
					     GS_PLUGIN_ERROR,
					     GS_PLUGIN_ERROR_NOT_SUPPORTED,
					     "Base64 encoded GPGKey required, not URL");
			return NULL;
		}
		gs_flatpak_app_set_repo_gpgkey (app, repo_gpgkey);
	}

	/* optional data */
	repo_homepage = g_key_file_get_string (kf, "Flatpak Repo", "Homepage", NULL);
	if (repo_homepage != NULL)
		gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, repo_homepage);
	repo_comment = g_key_file_get_string (kf, "Flatpak Repo", "Comment", NULL);
	if (repo_comment != NULL)
		gs_app_set_summary (app, GS_APP_QUALITY_NORMAL, repo_comment);
	repo_description = g_key_file_get_string (kf, "Flatpak Repo", "Description", NULL);
	if (repo_description != NULL)
		gs_app_set_description (app, GS_APP_QUALITY_NORMAL, repo_description);
	repo_default_branch = g_key_file_get_string (kf, "Flatpak Repo", "DefaultBranch", NULL);
	if (repo_default_branch != NULL)
		gs_app_set_branch (app, repo_default_branch);
	repo_icon = g_key_file_get_string (kf, "Flatpak Repo", "Icon", NULL);
	if (repo_icon != NULL &&
	    (g_str_has_prefix (repo_icon, "http:") ||
	     g_str_has_prefix (repo_icon, "https:"))) {
		/* Unfortunately the .flatpakrepo file doesn’t specify the icon
		 * size or scale out of band. */
		g_autoptr(GIcon) icon = gs_remote_icon_new (repo_icon);
		gs_app_add_icon (app, icon);
	}
	repo_filter = g_key_file_get_string (kf, "Flatpak Repo", "Filter", NULL);
	if (repo_filter != NULL && *repo_filter != '\0')
		gs_flatpak_app_set_repo_filter (app, repo_filter);

	/* success */
	return g_steal_pointer (&app);
}

void
gs_flatpak_app_set_packaging_info (GsApp *app)
{
	g_return_if_fail (GS_IS_APP (app));

	gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_FLATPAK);
	gs_app_set_metadata (app, "GnomeSoftware::PackagingBaseCssColor", "flatpak_packaging_color");
	gs_app_set_metadata (app, "GnomeSoftware::PackagingIcon", "package-flatpak-symbolic");
	gs_app_set_metadata (app, "GnomeSoftware::packagename-title", _("App ID"));
}

static guint
gs_get_strv_index (const gchar * const *strv,
		   const gchar *value)
{
	guint ii;

	for (ii = 0; strv[ii]; ii++) {
		if (g_str_equal (strv[ii], value))
			break;
	}

	return ii;
}

static GsBusPolicyPermission
bus_policy_permission_from_string (const char *str)
{
	if (str == NULL || g_str_equal (str, "none"))
		return GS_BUS_POLICY_PERMISSION_NONE;
	else if (g_str_equal (str, "see"))
		return GS_BUS_POLICY_PERMISSION_SEE;
	else if (g_str_equal (str, "talk"))
		return GS_BUS_POLICY_PERMISSION_TALK;
	else if (g_str_equal (str, "own"))
		return GS_BUS_POLICY_PERMISSION_OWN;
	else
		return GS_BUS_POLICY_PERMISSION_UNKNOWN;
}

/**
 * gs_flatpak_app_build_permissions_from_metadata:
 * @keyfile: app’s metadata file
 *
 * Build the app permissions data structure from a flatpak app’s metadata file.
 *
 * Returns: (transfer full): sealed permissions data structure
 * Since: 51
 */
GsAppPermissions *
gs_flatpak_app_build_permissions_from_metadata (GKeyFile *keyfile)
{
	char **strv;
	GsAppPermissions *permissions = gs_app_permissions_new ();
	GsAppPermissionsFlags flags = GS_APP_PERMISSIONS_FLAGS_NONE;
	g_autofree char *app_id = g_key_file_get_value (keyfile, "Application", "name", NULL);
	g_autofree char *mpris_id = NULL;
	g_autofree char *app_id_non_devel = NULL;
	g_autofree char *mpris_id_non_devel = NULL;

	strv = g_key_file_get_string_list (keyfile, "Context", "sockets", NULL, NULL);
	for (size_t i = 0; strv != NULL && strv[i] != NULL; i++) {
		if (g_str_equal (strv[i], "system-bus"))
			flags |= GS_APP_PERMISSIONS_FLAGS_SYSTEM_BUS | GS_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX;
		else if (g_str_equal (strv[i], "session-bus"))
			flags |= GS_APP_PERMISSIONS_FLAGS_SESSION_BUS | GS_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX;
		else if (g_str_equal (strv[i], "x11") &&
			 !g_strv_contains ((const gchar * const*)strv, "fallback-x11"))
			flags |= GS_APP_PERMISSIONS_FLAGS_X11;
		/* "fallback-x11" without "wayland" means X11 */
		else if (g_str_equal (strv[i], "fallback-x11") &&
		         !g_strv_contains ((const gchar * const*)strv, "wayland"))
			flags |= GS_APP_PERMISSIONS_FLAGS_X11;
		else if (g_str_equal (strv[i], "x11") ||
			 g_str_equal (strv[i], "fallback-x11") ||
			 g_str_equal (strv[i], "wayland"))
			/* with the above cases handled, these are all safe */;
		else if (g_str_equal (strv[i], "inherit-wayland-socket"))
			/* used by input methods like fcitx, gives them access to the compositor’s Wayland socket */
			flags |= GS_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX;
		else if (g_str_equal (strv[i], "pulseaudio"))
			flags |= GS_APP_PERMISSIONS_FLAGS_AUDIO_DEVICES;
		else if (g_str_equal (strv[i], "gpg-agent"))
			flags |= GS_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX;
		else if (g_str_equal (strv[i], "cups"))
			flags |= GS_APP_PERMISSIONS_FLAGS_DEVICES;
		else if (g_str_equal (strv[i], "pcsc"))
			flags |= GS_APP_PERMISSIONS_FLAGS_DEVICES;  /* smartcard devices */
		else if (g_str_equal (strv[i], "ssh-auth"))
			/* could use ssh-agent to authenticate on localhost or another host */
			flags |= GS_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX;
		else {
			/* Unknown socket, so we have to assume it’s unsafe,
			 * since session/system services which allow access via
			 * a plain socket are typically not written to protect
			 * against malicious clients. */
			g_debug ("Unrecognised Context.sockets value ‘%s’ for app %s",
				 strv[i], app_id);
			flags |= GS_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX;
		}
	}
	g_strfreev (strv);

	strv = g_key_file_get_string_list (keyfile, "Context", "devices", NULL, NULL);
	if (strv != NULL && g_strv_contains ((const gchar * const*)strv, "all"))
		flags |= GS_APP_PERMISSIONS_FLAGS_DEVICES;
	if (strv != NULL && g_strv_contains ((const gchar * const*)strv, "input"))
		flags |= GS_APP_PERMISSIONS_FLAGS_INPUT_DEVICES;
	if (strv != NULL && (g_strv_contains ((const gchar * const*)strv, "shm") ||
			     g_strv_contains ((const gchar * const*)strv, "kvm")))
		flags |= GS_APP_PERMISSIONS_FLAGS_SYSTEM_DEVICES;
	g_strfreev (strv);

	strv = g_key_file_get_string_list (keyfile, "Context", "shared", NULL, NULL);
	if (strv != NULL && g_strv_contains ((const gchar * const*)strv, "network"))
		flags |= GS_APP_PERMISSIONS_FLAGS_NETWORK;
	g_strfreev (strv);

	strv = g_key_file_get_string_list (keyfile, "Context", "filesystems", NULL, NULL);
	if (strv != NULL) {
		const struct {
			const gchar *key;
			GsAppPermissionsFlags perm;
		} filesystems_access[] = {
			/* Reference: https://docs.flatpak.org/en/latest/flatpak-command-reference.html#idm45858571325264 */
			{ "home", GS_APP_PERMISSIONS_FLAGS_HOME_FULL },
			{ "home:rw", GS_APP_PERMISSIONS_FLAGS_HOME_FULL },
			{ "home:ro", GS_APP_PERMISSIONS_FLAGS_HOME_READ },
			{ "~", GS_APP_PERMISSIONS_FLAGS_HOME_FULL },
			{ "~:rw", GS_APP_PERMISSIONS_FLAGS_HOME_FULL },
			{ "~:ro", GS_APP_PERMISSIONS_FLAGS_HOME_READ },
			{ "host", GS_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL },
			{ "host:rw", GS_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL },
			{ "host:ro", GS_APP_PERMISSIONS_FLAGS_FILESYSTEM_READ },
			{ "xdg-config/kdeglobals:ro", GS_APP_PERMISSIONS_FLAGS_NONE },  /* ignore this; all KDE apps need it */
			{ "xdg-download", GS_APP_PERMISSIONS_FLAGS_DOWNLOADS_FULL },
			{ "xdg-download:rw", GS_APP_PERMISSIONS_FLAGS_DOWNLOADS_FULL },
			{ "xdg-download:ro", GS_APP_PERMISSIONS_FLAGS_DOWNLOADS_READ },
			{ "xdg-data/flatpak/overrides:create", GS_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX },
			{ "xdg-run/pipewire-0", GS_APP_PERMISSIONS_FLAGS_SCREEN | GS_APP_PERMISSIONS_FLAGS_AUDIO_DEVICES },  /* see https://gitlab.gnome.org/GNOME/gnome-software/-/issues/2329 */
			{ "xdg-run/pipewire-0:rw", GS_APP_PERMISSIONS_FLAGS_SCREEN | GS_APP_PERMISSIONS_FLAGS_AUDIO_DEVICES },
			{ "xdg-run/pipewire-0:ro", GS_APP_PERMISSIONS_FLAGS_SCREEN | GS_APP_PERMISSIONS_FLAGS_AUDIO_DEVICES },
			{ "xdg-run/gvfsd", GS_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL },  /* see https://gitlab.gnome.org/GNOME/gnome-software/-/issues/2760 */
		};
		guint filesystems_hits = 0;
		guint strv_len = g_strv_length (strv);

		for (guint i = 0; i < G_N_ELEMENTS (filesystems_access); i++) {
			guint index = gs_get_strv_index ((const gchar * const *) strv, filesystems_access[i].key);
			if (index < strv_len) {
				flags |= filesystems_access[i].perm;
				filesystems_hits++;
				/* Mark it as used */
				strv[index][0] = '\0';
			}
		}

		if ((flags & GS_APP_PERMISSIONS_FLAGS_HOME_FULL) != 0)
			flags = flags & ~GS_APP_PERMISSIONS_FLAGS_HOME_READ;
		if ((flags & GS_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL) != 0)
			flags = flags & ~GS_APP_PERMISSIONS_FLAGS_FILESYSTEM_READ;
		if ((flags & GS_APP_PERMISSIONS_FLAGS_DOWNLOADS_FULL) != 0)
			flags = flags & ~GS_APP_PERMISSIONS_FLAGS_DOWNLOADS_READ;

		if (strv_len > filesystems_hits) {
			/* Cover those not being part of the above filesystems_access array */
			const struct {
				const gchar *prefix;
				const gchar *title;
				const gchar *title_subdir;
			} filesystems_other[] = {
				/* Reference: https://docs.flatpak.org/en/latest/flatpak-command-reference.html#idm45858571325264 */
				{ "/",			NULL,					   N_("System folder %s") },
				{ "home/",		NULL,					   N_("Home subfolder %s") },
				{ "~/",			NULL,					   N_("Home subfolder %s") },
				{ "host-os",		N_("Host system folders"),		   NULL },
				{ "host-etc",		N_("Host system configuration from /etc"), NULL },
				{ "xdg-desktop",	N_("Desktop folder"),			   N_("Desktop subfolder %s") },
				{ "xdg-documents",	N_("Documents folder"),			   N_("Documents subfolder %s") },
				/* note: xdg-download is listed in filesystems_access, but we list it here again to catch subdirectories */
				{ "xdg-download",	N_("Downloads folder"),			   N_("Downloads subfolder %s") },
				{ "xdg-music",		N_("Music folder"),			   N_("Music subfolder %s") },
				{ "xdg-pictures",	N_("Pictures folder"),			   N_("Pictures subfolder %s") },
				{ "xdg-public-share",	N_("Public Share folder"),		   N_("Public Share subfolder %s") },
				{ "xdg-videos",		N_("Videos folder"),			   N_("Videos subfolder %s") },
				{ "xdg-templates",	N_("Templates folder"),			   N_("Templates subfolder %s") },
				{ "xdg-cache",		N_("User cache folder"),		   N_("User cache subfolder %s") },
				{ "xdg-config",		N_("User configuration folder"),	   N_("User configuration subfolder %s") },
				{ "xdg-data",		N_("User data folder"),			   N_("User data subfolder %s") },
				{ "xdg-run",		N_("User runtime folder"),		   N_("User runtime subfolder %s") }
			};

			flags |= GS_APP_PERMISSIONS_FLAGS_FILESYSTEM_OTHER;

			for (guint j = 0; strv[j]; j++) {
				gchar *perm = strv[j];
				gboolean is_readonly;
				gchar *colon;
				guint i;

				/* Already handled by the flags */
				if (!perm[0])
					continue;

				is_readonly = g_str_has_suffix (perm, ":ro");
				colon = strrchr (perm, ':');
				/* modifiers are ":ro", ":rw", ":create", where ":create" is ":rw" + create
				   and ":rw" is default; treat ":create" as ":rw" */
				if (colon) {
					/* Completeness check */
					if (!g_str_equal (colon, ":ro") &&
					    !g_str_equal (colon, ":rw") &&
					    !g_str_equal (colon, ":create"))
						g_debug ("Unknown filesystem permission modifier '%s' from '%s'", colon, perm);
					/* cut it off */
					*colon = '\0';
				}

				for (i = 0; i < G_N_ELEMENTS (filesystems_other); i++) {
					if (g_str_has_prefix (perm, filesystems_other[i].prefix)) {
						g_autofree gchar *title_tmp = NULL;
						const gchar *slash, *title = NULL;
						slash = strchr (perm, '/');
						/* Catch and ignore invalid permission definitions */
						if (slash && filesystems_other[i].title_subdir != NULL) {
							#pragma GCC diagnostic push
							#pragma GCC diagnostic ignored "-Wformat-nonliteral"
							title_tmp = g_strdup_printf (
								_(filesystems_other[i].title_subdir),
								slash + (slash == perm ? 0 : 1));
							#pragma GCC diagnostic pop
							title = title_tmp;
						} else if (!slash && filesystems_other[i].title != NULL) {
							title = _(filesystems_other[i].title);
						}
						if (title != NULL) {
							if (is_readonly)
								gs_app_permissions_add_filesystem_read (permissions, title);
							else
								gs_app_permissions_add_filesystem_full (permissions, title);
						}
						break;
					}
				}

				/* Nothing matched, use a generic entry */
				if (i == G_N_ELEMENTS (filesystems_other)) {
					g_autofree gchar *title = g_strdup_printf (_("Filesystem access to %s"), perm);
					if (is_readonly)
						gs_app_permissions_add_filesystem_read (permissions, title);
					else
						gs_app_permissions_add_filesystem_full (permissions, title);
				}
			}
		}
	}
	g_strfreev (strv);

	/* Iterate over all the D-Bus permissions, working out the consequences of each permission
	 * and either adding to the GsAppPermissions’ flags, or adding more detailed
	 * service information to it. */
	if (!(flags & (GS_APP_PERMISSIONS_FLAGS_SYSTEM_BUS | GS_APP_PERMISSIONS_FLAGS_SESSION_BUS))) {
		const struct {
			GBusType bus_type;
			const char *keyfile_group;
			GsAppPermissionsFlags unfiltered_flag;
		} bus_policy_types[] = {
			{ G_BUS_TYPE_SESSION, "Session Bus Policy", GS_APP_PERMISSIONS_FLAGS_SESSION_BUS },
			{ G_BUS_TYPE_SYSTEM, "System Bus Policy", GS_APP_PERMISSIONS_FLAGS_SYSTEM_BUS },
		};

		/* Build various IDs for the default bus policies.
		 * MPRIS (https://specifications.freedesktop.org/mpris-spec/latest/)
		 * is a spec for remotely controlling media players, and requires
		 * apps to be able to own a sub-name in the MRPIS namespace on
		 * the bus. */
		if (app_id != NULL) {
			mpris_id = g_strconcat ("org.mpris.MediaPlayer2.", app_id, NULL);
			app_id_non_devel = g_str_has_suffix (app_id, ".Devel") ? g_strndup (app_id, strlen (app_id) - strlen (".Devel")) : NULL;
			mpris_id_non_devel = (app_id_non_devel != NULL) ? g_strconcat ("org.mpris.MediaPlayer2.", app_id_non_devel, NULL) : NULL;
		}

		for (size_t h = 0; h < G_N_ELEMENTS (bus_policy_types); h++) {
			g_auto(GStrv) bus_policies = NULL;

			/* If the app already has unfiltered access to this bus, skip it. */
			if (flags & bus_policy_types[h].unfiltered_flag)
				continue;

			bus_policies = g_key_file_get_keys (keyfile, bus_policy_types[h].keyfile_group, NULL, NULL);

			for (size_t i = 0; bus_policies != NULL && bus_policies[i] != NULL; i++) {
				const struct {
					GBusType bus_type;
					const char *bus_name;
					gboolean is_prefix;
					GsBusPolicyPermission permission_is_at_least;
					GsAppPermissionsFlags flags;
				} bus_policy_permissions[] = {
					/* Being able to talk to dconf means the app can read and write all settings: */
					{ G_BUS_TYPE_SESSION, "ca.desrt.dconf", FALSE, GS_BUS_POLICY_PERMISSION_TALK, GS_APP_PERMISSIONS_FLAGS_SETTINGS },

					/* There are various services on the session bus which are known to give sandbox escapes: */
					{ G_BUS_TYPE_SESSION, "org.freedesktop.Flatpak", FALSE, GS_BUS_POLICY_PERMISSION_TALK, GS_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX },
					{ G_BUS_TYPE_SESSION, "org.freedesktop.impl.portal.PermissionStore", FALSE, GS_BUS_POLICY_PERMISSION_TALK, GS_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX },

					/* org.gtk.vfs.* is known to allow file system access: */
					{ G_BUS_TYPE_SESSION, "org.gtk.vfs.", TRUE, GS_BUS_POLICY_PERMISSION_TALK, GS_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL },
				};
				const char *bus_name_pattern = bus_policies[i];
				g_autofree char *bus_policy_str = g_key_file_get_string (keyfile, bus_policy_types[h].keyfile_group, bus_name_pattern, NULL);
				GsBusPolicyPermission bus_policy;
				size_t j;

				/* This can’t fail because we’re iterating over the keys */
				g_assert (bus_policy_str != NULL);
				bus_policy = bus_policy_permission_from_string (bus_policy_str);

				/* Ignore the default policies (see man:flatpak-metadata(5))*/
				if (app_id != NULL &&
				    bus_policy_types[h].bus_type == G_BUS_TYPE_SESSION &&
				    (g_str_equal (bus_name_pattern, app_id) ||
				     (g_str_has_prefix (bus_name_pattern, app_id) && bus_name_pattern[strlen (app_id)] == '.') ||
				     g_str_equal (bus_name_pattern, mpris_id) ||
				     (g_str_equal (bus_name_pattern, "org.freedesktop.DBus") &&
				      bus_policy <= GS_BUS_POLICY_PERMISSION_TALK) ||
				     (g_str_has_prefix (bus_name_pattern, "org.freedesktop.portal.") &&
				      bus_policy <= GS_BUS_POLICY_PERMISSION_TALK)))
					continue;

				/* Allow .Devel apps to own their non-devel names on the session bus. */
				if (app_id_non_devel != NULL &&
				    bus_policy_types[h].bus_type == G_BUS_TYPE_SESSION &&
				    (g_str_equal (bus_name_pattern, app_id_non_devel) ||
				     (g_str_has_prefix (bus_name_pattern, app_id_non_devel) && bus_name_pattern[strlen (app_id_non_devel)] == '.') ||
				     g_str_equal (bus_name_pattern, mpris_id_non_devel)))
					continue;

				/* Search for flags to apply to the GsAppPermissions in response to the app’s policies */
				for (j = 0; j < G_N_ELEMENTS (bus_policy_permissions); j++) {
					if (bus_policy_permissions[j].bus_type == bus_policy_types[h].bus_type &&
					    ((!bus_policy_permissions[j].is_prefix && g_str_equal (bus_name_pattern, bus_policy_permissions[j].bus_name)) ||
					     (bus_policy_permissions[j].is_prefix && g_str_has_prefix (bus_name_pattern, bus_policy_permissions[j].bus_name))) &&
					    bus_policy >= bus_policy_permissions[j].permission_is_at_least) {
						flags |= bus_policy_permissions[j].flags;
						break;
					}
				}

				/* This entry in the keyfile hasn’t matched any of the specific
				 * `bus_policy_permissions` we know about, so list it generally
				 * for the user. */
				if (j == G_N_ELEMENTS (bus_policy_permissions)) {
					flags |= GS_APP_PERMISSIONS_FLAGS_BUS_POLICY_OTHER;
					gs_app_permissions_add_bus_policy (permissions,
									   bus_policy_types[h].bus_type,
									   bus_name_pattern,
									   bus_policy);
				}
			}
		}
	}

	gs_app_permissions_set_flags (permissions, flags);
	gs_app_permissions_seal (permissions);

	return permissions;
}
