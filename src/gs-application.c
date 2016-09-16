/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2013-2016 Richard Hughes <richard@hughsie.com>
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

#include "gs-application.h"

#include <stdlib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <libsoup/soup.h>

#ifdef GDK_WINDOWING_X11
#include <gtk/gtkx.h>
#endif

#include "gd-notification.h"

#ifdef HAVE_PACKAGEKIT
#include "gs-dbus-helper.h"
#endif

#include "gs-first-run-dialog.h"
#include "gs-shell.h"
#include "gs-update-monitor.h"
#include "gs-shell-search-provider.h"
#include "gs-folders.h"
#include "gs-utils.h"

#define ENABLE_SOFTWARE_SOURCES_CONF_KEY "enable-software-sources"

struct _GsApplication {
	GtkApplication	 parent;
	gboolean	 enable_profile_mode;
	GCancellable	*cancellable;
	GtkApplication	*application;
	GtkCssProvider	*provider;
	GsPluginLoader	*plugin_loader;
	gint		 pending_apps;
	GsShell		*shell;
	GsUpdateMonitor *update_monitor;
#ifdef HAVE_PACKAGEKIT
	GsDbusHelper	*dbus_helper;
#endif
	GsShellSearchProvider *search_provider;
	GNetworkMonitor *network_monitor;
	gulong		 network_changed_handler;
	GSettings       *settings;
};

G_DEFINE_TYPE (GsApplication, gs_application, GTK_TYPE_APPLICATION);

GsPluginLoader *
gs_application_get_plugin_loader (GsApplication *application)
{
	return application->plugin_loader;
}

gboolean
gs_application_has_active_window (GsApplication *application)
{
	GList *windows;
	GList *l;

	windows = gtk_application_get_windows (GTK_APPLICATION (application));
	for (l = windows; l != NULL; l = l->next) {
		if (gtk_window_is_active (GTK_WINDOW (l->data)))
			return TRUE;
	}
	return FALSE;
}

static void
gs_application_init (GsApplication *application)
{
	const GOptionEntry options[] = {
		{ "mode", '\0', 0, G_OPTION_ARG_STRING, NULL,
		  /* TRANSLATORS: this is a command line option */
		  _("Start up mode: either ‘updates’, ‘updated’, ‘installed’ or ‘overview’"), _("MODE") },
		{ "search", '\0', 0, G_OPTION_ARG_STRING, NULL,
		  _("Search for applications"), _("SEARCH") },
		{ "details", '\0', 0, G_OPTION_ARG_STRING, NULL,
		  _("Show application details (using application ID)"), _("ID") },
		{ "details-pkg", '\0', 0, G_OPTION_ARG_STRING, NULL,
		  _("Show application details (using package name)"), _("PKGNAME") },
		{ "local-filename", '\0', 0, G_OPTION_ARG_FILENAME, NULL,
		  _("Open a local package file"), _("FILENAME") },
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, NULL,
		  _("Show verbose debugging information"), NULL },
		{ "profile", 0, 0, G_OPTION_ARG_NONE, NULL,
		  _("Show profiling information for the service"), NULL },
		{ "quit", 0, 0, G_OPTION_ARG_NONE, NULL,
		  _("Quit the running instance"), NULL },
		{ "prefer-local", '\0', 0, G_OPTION_ARG_NONE, NULL,
		  _("Prefer local file sources to AppStream"), NULL },
		{ "version", 0, 0, G_OPTION_ARG_NONE, NULL,
		  _("Show version number"), NULL },
		{ NULL }
	};

	g_application_add_main_option_entries (G_APPLICATION (application), options);

	application->network_changed_handler = 0;
}

static void
download_updates_setting_changed (GSettings     *settings,
				  const gchar   *key,
				  GsApplication *app)
{
	if (!gs_update_monitor_is_managed () &&
	    g_settings_get_boolean (settings, key)) {
		g_debug ("Enabling update monitor");
		app->update_monitor = gs_update_monitor_new (app);
	} else {
		g_debug ("Disabling update monitor");
		g_clear_object (&app->update_monitor);
	}
}

static void
on_permission_changed (GPermission *permission,
                       GParamSpec  *pspec,
                       gpointer     data)
{
	GsApplication *app = data;

	if (app->settings)
		download_updates_setting_changed (app->settings, "download-updates", app);
}

static void
gs_application_monitor_permission (GsApplication *app)
{
	GPermission *permission;

	permission = gs_update_monitor_permission_get ();
	if (permission != NULL)
		g_signal_connect (permission, "notify",
				  G_CALLBACK (on_permission_changed), app);
}

static void
gs_application_monitor_updates (GsApplication *app)
{
	g_signal_connect (app->settings, "changed::download-updates",
			  G_CALLBACK (download_updates_setting_changed), app);
	download_updates_setting_changed (app->settings,
					  "download-updates",
					  app);
}

static void
network_changed_cb (GNetworkMonitor *monitor,
		    gboolean available,
		    GsApplication *app)
{
	gs_plugin_loader_set_network_status (app->plugin_loader, available);
}

static void
gs_application_monitor_network (GsApplication *app)
{
	GNetworkMonitor *network_monitor;

	network_monitor = g_network_monitor_get_default ();
	if (network_monitor == NULL || app->network_changed_handler != 0)
		return;
	app->network_monitor = g_object_ref (network_monitor);

	app->network_changed_handler = g_signal_connect (app->network_monitor, "network-changed",
							 G_CALLBACK (network_changed_cb), app);

	network_changed_cb (app->network_monitor,
			    g_network_monitor_get_network_available (app->network_monitor),
			    app);
}

static void
gs_application_initialize_plugins (GsApplication *app)
{
	static gboolean initialized = FALSE;
	g_auto(GStrv) plugin_blacklist = NULL;
	g_auto(GStrv) plugin_whitelist = NULL;
	g_autoptr(GError) error = NULL;
	const gchar *tmp;

	if (initialized)
		return;

	initialized = TRUE;

	/* allow for debugging */
	tmp = g_getenv ("GNOME_SOFTWARE_PLUGINS_BLACKLIST");
	if (tmp != NULL)
		plugin_blacklist = g_strsplit (tmp, ",", -1);
	tmp = g_getenv ("GNOME_SOFTWARE_PLUGINS_WHITELIST");
	if (tmp != NULL)
		plugin_whitelist = g_strsplit (tmp, ",", -1);

	app->plugin_loader = gs_plugin_loader_new ();
	gs_plugin_loader_set_location (app->plugin_loader, NULL);
	if (!gs_plugin_loader_setup (app->plugin_loader,
				     plugin_whitelist,
				     plugin_blacklist,
				     &error)) {
		g_warning ("Failed to setup plugins: %s", error->message);
		exit (1);
	}

	/* show the priority of each plugin */
	gs_plugin_loader_dump_state (app->plugin_loader);

}

static gboolean
gs_application_dbus_register (GApplication    *application,
                              GDBusConnection *connection,
                              const gchar     *object_path,
                              GError         **error)
{
	GsApplication *app = GS_APPLICATION (application);

	gs_application_initialize_plugins (app);
	app->search_provider = gs_shell_search_provider_new ();
	gs_shell_search_provider_setup (app->search_provider,
					app->plugin_loader);

	return gs_shell_search_provider_register (app->search_provider, connection, error);
}

static void
gs_application_dbus_unregister (GApplication    *application,
                                GDBusConnection *connection,
                                const gchar     *object_path)
{
	GsApplication *app = GS_APPLICATION (application);

	if (app->search_provider != NULL) {
		gs_shell_search_provider_unregister (app->search_provider);
		g_clear_object (&app->search_provider);
	}
}

static void
gs_application_show_first_run_dialog (GsApplication *app)
{
	GtkWidget *dialog;

	if (g_settings_get_boolean (app->settings, "first-run") == TRUE) {
		dialog = gs_first_run_dialog_new ();
		gs_shell_modal_dialog_present (app->shell, GTK_DIALOG (dialog));
		g_settings_set_boolean (app->settings, "first-run", FALSE);
		g_signal_connect_swapped (dialog, "response",
					  G_CALLBACK (gtk_widget_destroy), dialog);
	}
}

static void
theme_changed (GtkSettings *settings, GParamSpec *pspec, GsApplication *app)
{
	g_autoptr(GFile) file = NULL;
	g_autofree gchar *theme = NULL;

	g_object_get (settings, "gtk-theme-name", &theme, NULL);
	if (g_strcmp0 (theme, "HighContrast") == 0) {
		file = g_file_new_for_uri ("resource:///org/gnome/Software/gtk-style-hc.css");
	} else {
		file = g_file_new_for_uri ("resource:///org/gnome/Software/gtk-style.css");
	}
	gtk_css_provider_load_from_file (app->provider, file, NULL);
}

static void
gs_application_initialize_ui (GsApplication *app)
{
	static gboolean initialized = FALSE;

	if (initialized)
		return;

	initialized = TRUE;

	/* register ahead of loading the .ui file */
	gd_notification_get_type ();

	/* get CSS */
	app->provider = gtk_css_provider_new ();
	gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
						   GTK_STYLE_PROVIDER (app->provider),
						   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

	g_signal_connect (gtk_settings_get_default (), "notify::gtk-theme-name",
			  G_CALLBACK (theme_changed), app);
	theme_changed (gtk_settings_get_default (), NULL, app);

	gs_application_initialize_plugins (app);

	/* setup UI */
	app->shell = gs_shell_new ();

	/* this lets gs_shell_profile_dump() work from shells */
	gs_shell_set_profile_mode (app->shell, app->enable_profile_mode);

	app->cancellable = g_cancellable_new ();

	gs_shell_setup (app->shell, app->plugin_loader, app->cancellable);
	gtk_application_add_window (GTK_APPLICATION (app), gs_shell_get_window (app->shell));
}

static void
initialize_ui_and_present_window (GsApplication *app)
{
	GList *windows;
	GtkWindow *window;

	gs_application_initialize_ui (app);
	windows = gtk_application_get_windows (GTK_APPLICATION (app));
	if (windows) {
		window = windows->data;
		gtk_window_present (window);
	}
}

static void
sources_activated (GSimpleAction *action,
		   GVariant      *parameter,
		   gpointer       app)
{
	gs_shell_show_sources (GS_APPLICATION (app)->shell);
}

static void
about_activated (GSimpleAction *action,
		 GVariant      *parameter,
		 gpointer       user_data)
{
	GsApplication *app = GS_APPLICATION (user_data);
	const gchar *authors[] = {
		"Richard Hughes",
		"Matthias Clasen",
		"Allan Day",
		"Ryan Lerch",
		"William Jon McCann",
		NULL
	};
	const gchar *copyright = "Copyright \xc2\xa9 2016 Richard Hughes, Matthias Clasen";
	GtkAboutDialog *dialog;

	gs_application_initialize_ui (app);

	dialog = GTK_ABOUT_DIALOG (gtk_about_dialog_new ());
	gtk_about_dialog_set_authors (dialog, authors);
	gtk_about_dialog_set_copyright (dialog, copyright);
	gtk_about_dialog_set_license_type (dialog, GTK_LICENSE_GPL_2_0);
	gtk_about_dialog_set_logo_icon_name (dialog, "org.gnome.Software");
	gtk_about_dialog_set_translator_credits (dialog, _("translator-credits"));
	gtk_about_dialog_set_version (dialog, VERSION);

	/* TRANSLATORS: this is the title of the about window */
	gtk_window_set_title (GTK_WINDOW (dialog), _("About Software"));

	/* TRANSLATORS: this is the application name */
	gtk_about_dialog_set_program_name (dialog, _("Software"));

	/* TRANSLATORS: well, we seem to think so, anyway */
	gtk_about_dialog_set_comments (dialog, _("A nice way to manage the "
						 "software on your system."));

	gs_shell_modal_dialog_present (app->shell, GTK_DIALOG (dialog));

	/* just destroy */
	g_signal_connect_swapped (dialog, "response",
				  G_CALLBACK (gtk_widget_destroy), dialog);
}

static void
profile_activated (GSimpleAction *action,
		   GVariant      *parameter,
		   gpointer       data)
{
	GsApplication *app = GS_APPLICATION (data);
	app->enable_profile_mode = TRUE;

	/* dump right now as well */
	if (app->plugin_loader != NULL) {
		AsProfile *profile = gs_plugin_loader_get_profile (app->plugin_loader);
		as_profile_dump (profile);
	}
}

static void
cancel_trigger_failed_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	GsApplication *app = GS_APPLICATION (user_data);
	g_autoptr(GError) error = NULL;
	if (!gs_plugin_loader_app_action_finish (app->plugin_loader, res, &error)) {
		g_warning ("failed to cancel trigger: %s", error->message);
		return;
	}
}

static void
reboot_failed_cb (GObject *source, GAsyncResult *res, gpointer user_data)
{
	GsApplication *app = GS_APPLICATION (user_data);
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) retval = NULL;

	/* get result */
	retval = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, &error);
	if (retval != NULL)
		return;

	if (error != NULL) {
		g_warning ("Calling org.gnome.SessionManager.Reboot failed: %s",
			   error->message);
	}

	/* cancel trigger */
	gs_plugin_loader_app_action_async (app->plugin_loader,
					   NULL, /* everything! */
					   GS_PLUGIN_ACTION_UPDATE_CANCEL,
					   app->cancellable,
					   cancel_trigger_failed_cb,
					   app);
}

static void
offline_update_cb (GsPluginLoader *plugin_loader,
		   GAsyncResult *res,
		   GsApplication *app)
{
	g_autoptr(GDBusConnection) bus = NULL;
	g_autoptr(GError) error = NULL;
	if (!gs_plugin_loader_update_finish (plugin_loader, res, &error)) {
		g_warning ("Failed to trigger offline update: %s", error->message);
		return;
	}

	/* trigger reboot */
	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	g_dbus_connection_call (bus,
				"org.gnome.SessionManager",
				"/org/gnome/SessionManager",
				"org.gnome.SessionManager",
				"Reboot",
				NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
				G_MAXINT, NULL,
				reboot_failed_cb,
				app);
}

static void
gs_application_reboot_failed_cb (GObject *source,
				 GAsyncResult *res,
				 gpointer user_data)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GVariant) retval = NULL;

	/* get result */
	retval = g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), res, &error);
	if (retval != NULL)
		return;
	if (error != NULL) {
		g_warning ("Calling org.gnome.SessionManager.Reboot failed: %s",
			   error->message);
	}
}

static void
reboot_activated (GSimpleAction *action,
		   GVariant      *parameter,
		   gpointer       data)
{
	g_autoptr(GDBusConnection) bus = NULL;
	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);
	g_dbus_connection_call (bus,
				"org.gnome.SessionManager",
				"/org/gnome/SessionManager",
				"org.gnome.SessionManager",
				"Reboot",
				NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
				G_MAXINT, NULL,
				gs_application_reboot_failed_cb,
				NULL);
}

static void
reboot_and_install (GSimpleAction *action,
		    GVariant      *parameter,
		    gpointer       data)
{
	GsApplication *app = GS_APPLICATION (data);
	gs_application_initialize_plugins (app);
	gs_plugin_loader_update_async (app->plugin_loader,
				       NULL,
				       app->cancellable,
				       (GAsyncReadyCallback) offline_update_cb,
				       app);
}

static void
quit_activated (GSimpleAction *action,
		GVariant      *parameter,
		gpointer       app)
{
	GApplicationFlags flags;
	GList *windows;
	GtkWidget *window;

	flags = g_application_get_flags (app);

	if (flags & G_APPLICATION_IS_SERVICE) {
		windows = gtk_application_get_windows (GTK_APPLICATION (app));
		if (windows) {
			window = windows->data;
			gtk_widget_hide (window);
		}

		return;
	}

	g_application_quit (G_APPLICATION (app));
}

static void
set_mode_activated (GSimpleAction *action,
		    GVariant      *parameter,
		    gpointer       data)
{
	GsApplication *app = GS_APPLICATION (data);
	const gchar *mode;

	initialize_ui_and_present_window (app);

	mode = g_variant_get_string (parameter, NULL);
	if (g_strcmp0 (mode, "updates") == 0) {
		gs_shell_set_mode (app->shell, GS_SHELL_MODE_UPDATES);
	} else if (g_strcmp0 (mode, "installed") == 0) {
		gs_shell_set_mode (app->shell, GS_SHELL_MODE_INSTALLED);
	} else if (g_strcmp0 (mode, "moderate") == 0) {
		gs_shell_set_mode (app->shell, GS_SHELL_MODE_MODERATE);
	} else if (g_strcmp0 (mode, "overview") == 0) {
		gs_shell_set_mode (app->shell, GS_SHELL_MODE_OVERVIEW);
	} else if (g_strcmp0 (mode, "updated") == 0) {
		gs_shell_set_mode (app->shell, GS_SHELL_MODE_UPDATES);
		gs_shell_show_installed_updates (app->shell);
	} else {
		g_warning ("Mode '%s' not recognised", mode);
	}
}

static void
search_activated (GSimpleAction *action,
		  GVariant      *parameter,
		  gpointer       data)
{
	GsApplication *app = GS_APPLICATION (data);
	const gchar *search;

	initialize_ui_and_present_window (app);

	search = g_variant_get_string (parameter, NULL);
	gs_shell_show_search (app->shell, search);
}

static void
details_activated (GSimpleAction *action,
		   GVariant      *parameter,
		   gpointer       data)
{
	GsApplication *app = GS_APPLICATION (data);
	const gchar *id;
	const gchar *search;

	initialize_ui_and_present_window (app);

	g_variant_get (parameter, "(&s&s)", &id, &search);
	if (search != NULL && search[0] != '\0')
		gs_shell_show_search_result (app->shell, id, search);
	else {
		g_autoptr (GsApp) a = NULL;

		if (as_utils_unique_id_valid (id))
			a = gs_app_new_from_unique_id (id);
		else
			a = gs_app_new (id);

		gs_shell_show_app (app->shell, a);
	}
}

static void
details_pkg_activated (GSimpleAction *action,
		       GVariant      *parameter,
		       gpointer       data)
{
	GsApplication *app = GS_APPLICATION (data);
	g_autoptr (GsApp) a = NULL;

	initialize_ui_and_present_window (app);

	a = gs_app_new (NULL);
	gs_app_add_source (a, g_variant_get_string (parameter, NULL));
	gs_shell_show_app (app->shell, a);
}

static void
filename_activated (GSimpleAction *action,
		    GVariant      *parameter,
		    gpointer       data)
{
	GsApplication *app = GS_APPLICATION (data);
	const gchar *filename;

	gs_application_initialize_ui (app);

	g_variant_get (parameter, "(&s)", &filename);
	gs_shell_show_filename (app->shell, filename);
}

static void
launch_activated (GSimpleAction *action,
		  GVariant      *parameter,
		  gpointer       data)
{
	const gchar *desktop_id;
	GdkDisplay *display;
	g_autoptr(GError) error = NULL;
	g_autoptr(GAppInfo) appinfo = NULL;
	g_autoptr(GAppLaunchContext) context = NULL;

	desktop_id = g_variant_get_string (parameter, NULL);
	display = gdk_display_get_default ();
	appinfo = G_APP_INFO (gs_utils_get_desktop_app_info (desktop_id));
	if (appinfo == NULL) {
		g_warning ("no such desktop file: %s", desktop_id);
		return;
	}

	context = G_APP_LAUNCH_CONTEXT (gdk_display_get_app_launch_context (display));
	if (!g_app_info_launch (appinfo, NULL, context, &error)) {
		g_warning ("launching %s failed: %s", desktop_id, error->message);
	}
}

static void
show_offline_updates_error (GSimpleAction *action,
			    GVariant      *parameter,
			    gpointer       data)
{
	GsApplication *app = GS_APPLICATION (data);

	initialize_ui_and_present_window (app);

	gs_shell_set_mode (app->shell, GS_SHELL_MODE_UPDATES);
	gs_update_monitor_show_error (app->update_monitor, app->shell);
}

static void
install_resources_activated (GSimpleAction *action,
                             GVariant      *parameter,
                             gpointer       data)
{
	GsApplication *app = GS_APPLICATION (data);
#ifdef GDK_WINDOWING_X11
	GdkDisplay *display;
#endif
	const gchar *mode;
	const gchar *startup_id;
	g_autofree gchar **resources = NULL;

	g_variant_get (parameter, "(&s^a&s&s)", &mode, &resources, &startup_id);

#ifdef GDK_WINDOWING_X11
	display = gdk_display_get_default ();

	if (GDK_IS_X11_DISPLAY (display)) {
		if (startup_id != NULL && startup_id[0] != '\0')
			gdk_x11_display_set_startup_notification_id (display,
			                                             startup_id);
	}
#endif

	initialize_ui_and_present_window (app);

	gs_shell_show_extras_search (app->shell, mode, resources);
}

static GActionEntry actions[] = {
	{ "about", about_activated, NULL, NULL, NULL },
	{ "sources", sources_activated, NULL, NULL, NULL },
	{ "quit", quit_activated, NULL, NULL, NULL },
	{ "profile", profile_activated, NULL, NULL, NULL },
	{ "reboot-and-install", reboot_and_install, NULL, NULL, NULL },
	{ "reboot", reboot_activated, NULL, NULL, NULL },
	{ "set-mode", set_mode_activated, "s", NULL, NULL },
	{ "search", search_activated, "s", NULL, NULL },
	{ "details", details_activated, "(ss)", NULL, NULL },
	{ "details-pkg", details_pkg_activated, "s", NULL, NULL },
	{ "filename", filename_activated, "(s)", NULL, NULL },
	{ "launch", launch_activated, "s", NULL, NULL },
	{ "show-offline-update-error", show_offline_updates_error, NULL, NULL, NULL },
	{ "install-resources", install_resources_activated, "(sass)", NULL, NULL },
	{ "nop", NULL, NULL, NULL }
};

static void
gs_application_update_software_sources_presence (GApplication *self)
{
	GSimpleAction *action;
	gboolean enable_sources;

	action = G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self),
							      "sources"));
	enable_sources = g_settings_get_boolean (GS_APPLICATION (self)->settings,
						 ENABLE_SOFTWARE_SOURCES_CONF_KEY);
	g_simple_action_set_enabled (action, enable_sources);
}

static void
gs_application_settings_changed_cb (GApplication *self,
				    const gchar *key,
				    gpointer data)
{
	if (g_strcmp0 (key, ENABLE_SOFTWARE_SOURCES_CONF_KEY) == 0) {
		gs_application_update_software_sources_presence (self);
	}
}

static void
gs_application_startup (GApplication *application)
{
	GSettings *settings;
	G_APPLICATION_CLASS (gs_application_parent_class)->startup (application);

	g_action_map_add_action_entries (G_ACTION_MAP (application),
					 actions, G_N_ELEMENTS (actions),
					 application);

#ifdef HAVE_PACKAGEKIT
	GS_APPLICATION (application)->dbus_helper = gs_dbus_helper_new ();
#endif
	settings = g_settings_new ("org.gnome.software");
	GS_APPLICATION (application)->settings = settings;
	g_signal_connect_swapped (settings, "changed",
				  G_CALLBACK (gs_application_settings_changed_cb),
				  application);

	gs_application_monitor_permission (GS_APPLICATION (application));
	gs_application_monitor_updates (GS_APPLICATION (application));
	gs_folders_convert ();

	gs_application_update_software_sources_presence (application);
}

static void
gs_application_shell_loaded_cb (GsShell *shell, GsApplication *app)
{
	gs_shell_set_mode (app->shell, GS_SHELL_MODE_OVERVIEW);
}

static void
gs_application_activate (GApplication *application)
{
	GsApplication *app = GS_APPLICATION (application);

	gs_application_initialize_ui (GS_APPLICATION (application));
	gs_application_monitor_network (GS_APPLICATION (application));

	/* start metadata loading screen */
	if (gs_shell_get_mode (app->shell) == GS_SHELL_MODE_UNKNOWN) {
		g_signal_connect (app->shell, "loaded",
				  G_CALLBACK (gs_application_shell_loaded_cb),
				  app);
		gs_shell_set_mode (app->shell, GS_SHELL_MODE_LOADING);
	} else {
		gs_shell_set_mode (app->shell, GS_SHELL_MODE_OVERVIEW);
	}

	gs_shell_activate (GS_APPLICATION (application)->shell);

	gs_application_show_first_run_dialog (GS_APPLICATION (application));
}

static void
gs_application_dispose (GObject *object)
{
	GsApplication *app = GS_APPLICATION (object);

	if (app->cancellable != NULL) {
		g_cancellable_cancel (app->cancellable);
		g_clear_object (&app->cancellable);
	}

	g_clear_object (&app->plugin_loader);
	g_clear_object (&app->shell);
	g_clear_object (&app->provider);
	g_clear_object (&app->update_monitor);
	if (app->network_changed_handler != 0) {
		g_signal_handler_disconnect (app->network_monitor, app->network_changed_handler);
		app->network_changed_handler = 0;
	}
	g_clear_object (&app->network_monitor);
#ifdef HAVE_PACKAGEKIT
	g_clear_object (&app->dbus_helper);
#endif
	g_clear_object (&app->settings);

	G_OBJECT_CLASS (gs_application_parent_class)->dispose (object);
}

static int
gs_application_handle_local_options (GApplication *app, GVariantDict *options)
{
	const gchar *id;
	const gchar *pkgname;
	const gchar *local_filename;
	const gchar *mode;
	const gchar *search;
	gint rc = -1;
	g_autoptr(GError) error = NULL;

	if (g_variant_dict_contains (options, "verbose"))
		g_setenv ("GS_DEBUG", "1", TRUE);

	/* prefer local sources */
	if (g_variant_dict_contains (options, "prefer-local"))
		g_setenv ("GNOME_SOFTWARE_PREFER_LOCAL", "true", TRUE);

	if (g_variant_dict_contains (options, "version")) {
		g_print ("gnome-software " VERSION "\n");
		return 0;
	}

	if (!g_application_register (app, NULL, &error)) {
		g_printerr ("%s\n", error->message);
		return 1;
	}

	if (g_variant_dict_contains (options, "profile")) {
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"profile",
						NULL);
	}
	if (g_variant_dict_contains (options, "quit")) {
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"quit",
						NULL);
		return 0;
	}

	if (g_variant_dict_lookup (options, "mode", "&s", &mode)) {
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"set-mode",
						g_variant_new_string (mode));
		rc = 0;
	} else if (g_variant_dict_lookup (options, "search", "&s", &search)) {
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"search",
						g_variant_new_string (search));
		rc = 0;
	} else if (g_variant_dict_lookup (options, "details", "&s", &id)) {
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"details",
						g_variant_new ("(ss)", id, ""));
		rc = 0;
	} else if (g_variant_dict_lookup (options, "details-pkg", "&s", &pkgname)) {
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"details-pkg",
						g_variant_new_string (pkgname));
		rc = 0;
	} else if (g_variant_dict_lookup (options, "local-filename", "^&ay", &local_filename)) {
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"filename",
						g_variant_new ("(s)", local_filename));
		rc = 0;
	}

	return rc;
}

static void
gs_application_open (GApplication  *application,
                     GFile        **files,
                     gint           n_files,
                     const gchar   *hint)
{
	GsApplication *app = GS_APPLICATION (application);
	gint i;

	for (i = 0; i < n_files; i++) {
		g_autofree gchar *str = g_file_get_uri (files[i]);
		g_autoptr(SoupURI) uri = NULL;

		uri = soup_uri_new (str);
		if (!SOUP_URI_IS_VALID (uri))
			continue;

		if (g_strcmp0 (soup_uri_get_scheme (uri), "appstream") == 0) {
			const gchar *host = soup_uri_get_host (uri);
			const gchar *path = soup_uri_get_path (uri);

			/* appstream://foo -> scheme: appstream, host: foo, path: / */
			/* appstream:foo -> scheme: appstream, host: (empty string), path: /foo */
			if (host != NULL && (strlen (host) > 0))
				path = host;

			/* trim any leading slashes */
			while (*path == '/')
				path++;

			g_action_group_activate_action (G_ACTION_GROUP (app),
			                                "details",
			                                g_variant_new ("(ss)", path, ""));
		}
		if (g_strcmp0 (soup_uri_get_scheme (uri), "apt") == 0) {
			const gchar *path = soup_uri_get_path (uri);

			/* trim any leading slashes */
			while (*path == '/')
				path++;

			g_action_group_activate_action (G_ACTION_GROUP (app),
			                                "details-pkg",
			                                g_variant_new_string (path));
		}
	}
}

static void
gs_application_class_init (GsApplicationClass *class)
{
	G_OBJECT_CLASS (class)->dispose = gs_application_dispose;
	G_APPLICATION_CLASS (class)->startup = gs_application_startup;
	G_APPLICATION_CLASS (class)->activate = gs_application_activate;
	G_APPLICATION_CLASS (class)->handle_local_options = gs_application_handle_local_options;
	G_APPLICATION_CLASS (class)->open = gs_application_open;
	G_APPLICATION_CLASS (class)->dbus_register = gs_application_dbus_register;
	G_APPLICATION_CLASS (class)->dbus_unregister = gs_application_dbus_unregister;
}

GsApplication *
gs_application_new (void)
{
	return g_object_new (GS_APPLICATION_TYPE,
			     "application-id", "org.gnome.Software",
			     "flags", G_APPLICATION_HANDLES_OPEN,
			     "inactivity-timeout", 12000,
			     NULL);
}

/* vim: set noexpandtab: */
