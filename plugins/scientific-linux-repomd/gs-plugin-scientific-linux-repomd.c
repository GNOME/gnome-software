/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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

#include <glib/gi18n.h>
#include <gnome-software.h>
#include <xmlb.h>

#include <sys/utsname.h>

#define SCIENTIFIC_LINUX_BASEURL "http://ftp.scientificlinux.org/linux/scientific"

struct GsPluginData {
        gchar           *cachefn;
        gchar           *cachefn_b;
        GFileMonitor    *cachefn_monitor;
        gchar           *os_name;
        gchar           *os_version;
        gchar           *os_arch;
        gchar           *url;
        GsApp           *cached_origin;
        GSettings       *settings;
        guint64          major_v;
        guint64          current_minor_v;
        guint64          published_minor_v;
        gboolean         is_valid;
        GMutex           mutex;
};

void
gs_plugin_initialize (GsPlugin *plugin)
{
        GsPluginData *priv = gs_plugin_alloc_data (plugin, sizeof(GsPluginData));

        g_mutex_init (&priv->mutex);

        /* check that we are running on SL */
        if (!gs_plugin_check_distro_id (plugin, "scientific")) {
                gs_plugin_set_enabled (plugin, FALSE);
                g_debug ("disabling '%s' as we're not Scientfic Linux", gs_plugin_get_name (plugin));
                return;
        }

        priv->settings = g_settings_new ("org.gnome.software");

        /* require the GnomeSoftware::CpeName metadata */
        gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "os-release");
}

void
gs_plugin_destroy (GsPlugin *plugin)
{
        GsPluginData *priv = gs_plugin_get_data (plugin);
        if (priv->cachefn_monitor != NULL)
                g_object_unref (priv->cachefn_monitor);
        if (priv->cached_origin != NULL)
                g_object_unref (priv->cached_origin);
        if (priv->settings != NULL)
                g_object_unref (priv->settings);
        g_free (priv->cachefn);
        g_free (priv->cachefn_b);
        g_free (priv->os_name);
        g_free (priv->os_version);
        g_free (priv->os_arch);
        g_free (priv->url);
        g_mutex_clear (&priv->mutex);
}

static void
_file_changed_cb (GFileMonitor *monitor,
                  GFile *file, GFile *other_file,
                  GFileMonitorEvent event_type,
                  gpointer user_data)
{
        GsPlugin *plugin = GS_PLUGIN (user_data);
        GsPluginData *priv = gs_plugin_get_data (plugin);

        g_debug ("SL cache file changed, so reloading upgrades list");
        gs_plugin_updates_changed (plugin);
        priv->is_valid = FALSE;
}


gboolean
gs_plugin_setup (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
        GsPluginData *priv = gs_plugin_get_data (plugin);
        g_autoptr(GFile) file = NULL;
        g_autoptr(GsOsRelease) os_release = NULL;
        g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);
        utsname priv_uname;
        g_autofree gchar **tokens = NULL;

        /* set the filename for cache */
        priv->cachefn = gs_utils_get_cache_filename ("scientific-repomd",
                                                     "latestrepomd.xml",
                                                     GS_UTILS_CACHE_FLAG_WRITEABLE,
                                                     error);

        if (priv->cachefn == NULL)
                return FALSE;

        priv->cachefn_b = gs_utils_get_cache_filename ("scientific-repomd",
                                                       "latestrepomd.xmlb",
                                                       GS_UTILS_CACHE_FLAG_WRITEABLE,
                                                       error);

        if (priv->cachefn_b == NULL)
                return FALSE;

        g_debug ("SL cache file xml : %s", priv->cachefn);
        g_debug ("SL cache file xmlb: %s", priv->cachefn_b);

        /* watch this in case it is changed by the user */
        file = g_file_new_for_path (priv->cachefn);
        priv->cachefn_monitor = g_file_monitor (file,
                                                G_FILE_MONITOR_NONE,
                                                cancellable,
                                                error);
        if (priv->cachefn_monitor == NULL)
                return FALSE;

        /* get system arch */
        if (uname (&priv_uname) != 0)
                return FALSE;
        g_stpcpy (priv->os_arch, *unameData.machine);
        g_debug ("Running Arch detected: %s", priv->os_arch);

        /* read os-release for the current versions */
        os_release = gs_os_release_new (error);
        if (os_release == NULL)
                return FALSE;
        priv->os_name = g_strdup (gs_os_release_get_name (os_release));
        if (priv->os_name == NULL)
                return FALSE;
        priv->os_version = gs_os_release_get_version_id (os_release);
        if (priv->os_version == NULL)
                return FALSE;
        tokens = g_strsplit (priv->os_version, '.', 2);
        priv->major_v = g_ascii_strtoull (tokens[0], NULL, 10);
        priv->current_minor_v = g_ascii_strtoull (tokens[1], NULL, 10);

        g_debug ("SL major version detected: %u", priv->major_v);
        g_debug ("SL minor version detected: %u", priv->minor_v);

        priv->url = g_strjoin ('/', SCIENTIFIC_LINUX_BASEURL,
                               tokens[0], priv->os_arch,
                               'os/repodata/repomd.xml');

        /* watch this in case it is changed by the user */
        g_signal_connect (priv->cachefn_monitor, "changed",
                          G_CALLBACK (_file_changed_cb), plugin);

        /* add source */
        priv->cached_origin = gs_app_new (gs_plugin_get_name (plugin));
        gs_app_set_kind (priv->cached_origin, AS_APP_KIND_SOURCE);
        gs_app_set_origin_hostname (priv->cached_origin, priv->url);

        /* add the source to the plugin cache which allows us to match the
         * unique ID to a GsApp when creating an event */
        gs_plugin_cache_add (plugin,
                             gs_app_get_unique_id (priv->cached_origin),
                             priv->cached_origin);

        /* success */
        return TRUE;
}

static gboolean
_refresh_cache (GsPlugin *plugin,
                guint cache_age,
                GCancellable *cancellable,
                GError **error)
{
        GsPluginData *priv = gs_plugin_get_data (plugin);
        g_autoptr(GsApp) app_dl = gs_app_new (gs_plugin_get_name (plugin));

        /* check cache age */
        if (cache_age > 0) {
                g_autoptr(GFile) file = g_file_new_for_path (priv->cachefn);
                guint tmp = gs_utils_get_file_age (file);
                if (tmp < cache_age) {
                        g_debug ("%s is only %u seconds old",
                                 priv->cachefn, tmp);
                        return TRUE;
                }
        }

        /* download new file */
        gs_app_set_summary_missing (app_dl,
                                    /* TRANSLATORS: status text when downloading */
                                    _("Downloading upgrade information…"));
        if (!gs_plugin_download_file (plugin, app_dl,
                                      priv->url,
                                      priv->cachefn,
                                      cancellable,
                                      error)) {
                gs_utils_error_add_unique_id (error, priv->cached_origin);
                return FALSE;
        }

        /* success */
        priv->is_valid = FALSE;
        return TRUE;
}

gboolean
gs_plugin_refresh (GsPlugin *plugin,
                   guint cache_age,
                   GCancellable *cancellable,
                   GError **error)
{
        GsPluginData *priv = gs_plugin_get_data (plugin);
        g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);
        return _refresh_cache (plugin, cache_age, cancellable, error);
}

static gboolean
_ensure_cache (GsPlugin *plugin, GCancellable *cancellable, GError **error)
{
        GsPluginData *priv = gs_plugin_get_data (plugin);
        gsize len;
        guint64  tmp_int;
        autofree gchar *tmp = NULL;
        g_autofree gchar *data = NULL;
        g_autofree gchar **tokens = NULL;
        g_autoptr(XbNode) revision = NULL;
        g_autoptr(XbSilo) silo = xb_silo_new ();
        g_autoptr(XbBuilder) builder = xb_builder_new ();
        g_autoptr(XbBuilderSource) source = xb_builder_source_new ();

        /* already verified cache */
        if (priv->is_valid)
                return TRUE;

        /* just ensure there is any data, no matter how old */
        if (!_refresh_cache (plugin, G_MAXUINT, cancellable, error))
                return FALSE;

        /* get cached file */
        if (!g_file_get_contents (priv->cachefn, &data, &len, error)) {
                gs_utils_error_convert_gio (error);
                return FALSE;
        }

        if (!xb_builder_source_load_xml (source, data,
                                         XB_BUILDER_SOURCE_FLAG_WATCH_FILE |
                                         XB_BUILDER_SOURCE_FLAG_LITERAL_TEXT,
                                         error)) {
                gs_utils_error_convert_gio (error);
                return FALSE;
        }

        xb_builder_import_source (builder, source);
        priv->silo = xb_builder_ensure (builder, priv->cachefn_b,
                                        XB_BUILDER_COMPILE_FLAG_WATCH_BLOB,
                                        NULL, error);
        if (silo == NULL) {
                gs_utils_error_convert_gio (error);
                return FALSE;
        }

        revision = xb_silo_query_first (silo, '/repomd/revision', error)
        if (revision == NULL)
                return FALSE;

        tmp = xb_node_get_text (revision);

        tokens = g_strsplit (tmp, '.', 2);
        tmp_int = g_ascii_strtoull (tokens[0], NULL, 10);
        if (priv->major_v != tmp_int) {
            g_debug ("SL cache file, wrong major version: %u != %u",
                     priv->major_v, tmp_int);
            return FALSE;
        }

        priv->published_minor_v = g_ascii_strtoull (tokens[1], NULL, 10);

        priv->is_valid = TRUE;
        return TRUE;
}

gboolean
gs_plugin_add_distro_upgrades (GsPlugin *plugin,
                               GsAppList *list,
                               GCancellable *cancellable,
                               GError **error)
{
        GsPluginData *priv = gs_plugin_get_data (plugin);
        g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);

        /* ensure valid data is loaded */
        if (!_ensure_cache (plugin, cancellable, error))
                return FALSE;

        if (priv->published_minor_v >  priv->current_minor_v) {
                g_autoptr(GsApp) app = NULL;
                app = _create_upgrade_from_info (plugin, priv);
                gs_app_list_add (list, app);
        }

        return TRUE;
}

static GsApp *
_create_upgrade_from_info (GsPlugin *plugin, GsPluginData *priv)
{
        GsApp *app;
        g_autofree gchar *cache_key = NULL;
        g_autofree gchar *app_id = NULL;
        g_autofree gchar *app_version = NULL;
        g_autofree gchar *url = NULL;
        g_autoptr(AsIcon) ic = NULL;

        /* search in the cache */
        cache_key = g_strdup_printf ("release-%u.%u", priv->major_v,
                                     priv->published_minor_v);
        app = gs_plugin_cache_lookup (plugin, cache_key);
        if (app != NULL)
                return app;

        app_id = g_strdup_printf ("org.scientificlinux.SL%u.%u-update",
                                  priv->major_v, priv->published_minor_v);
        app_version = g_strdup_printf ("%u.%u", 
                                  priv->major_v, priv->published_minor_v);


        /* icon from disk, RHEL uses this path so does SL */
        ic = as_icon_new ();
        as_icon_set_kind (ic, AS_ICON_KIND_LOCAL);
        as_icon_set_filename (ic, "/usr/share/pixmaps/fedora-logo-sprite.png");

        /* create */
        app = gs_app_new (app_id);
        gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
        gs_app_set_kind (app, AS_APP_KIND_OS_UPGRADE);
        gs_app_set_bundle_kind (app, AS_BUNDLE_KIND_PACKAGE);
        gs_app_set_name (app, GS_APP_QUALITY_LOWEST, item->name);
        gs_app_set_summary (app, GS_APP_QUALITY_LOWEST,
                            /* TRANSLATORS: this is a genral reason to perform distro upgrades */
                            _("Upgrade for the latest features, performance and stability improvements."));
        gs_app_set_version (app, app_version);
        gs_app_set_size_installed (app, 1024 * 1024 * 1024); /* estimate */
        gs_app_set_size_download (app, 256 * 1024 * 1024); /* estimate */
        gs_app_set_license (app, GS_APP_QUALITY_LOWEST, "LicenseRef-free");
        gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
        gs_app_add_quirk (app, GS_APP_QUIRK_PROVENANCE);
        gs_app_add_quirk (app, GS_APP_QUIRK_NOT_REVIEWABLE);
        gs_app_add_icon (app, ic);

        /* show release notes if requested*/
        url = g_strdup_printf ("http://ftp.scientificlinux.org/linux/scientific/%u/%s/release-notes/",
                               priv->major_v, priv->os_arch);
        gs_app_set_url (app, AS_URL_KIND_HOMEPAGE, url);


        /* save in the cache */
        gs_plugin_cache_add (plugin, cache_key, app);

        /* success */
        return app;
}

gboolean
gs_plugin_refine_app (GsPlugin *plugin,
                      GsApp *app,
                      GsPluginRefineFlags flags,
                      GCancellable *cancellable,
                      GError **error)
{
        GsPluginData *priv = gs_plugin_get_data (plugin);
        g_autoptr(GMutexLocker) locker = g_mutex_locker_new (&priv->mutex);

        /* not for us */
        if (gs_app_get_kind (app) != AS_APP_KIND_OS_UPGRADE)
                return TRUE;

        /* not enough metadata */
        cpe_name = gs_app_get_metadata_item (app, "GnomeSoftware::CpeName");
        if (cpe_name == NULL)
                return TRUE;

        /* ensure valid data is loaded */
        if (!_ensure_cache (plugin, cancellable, error))
                return FALSE;

        return TRUE;
}




