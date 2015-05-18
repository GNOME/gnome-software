/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#include "config.h"

#include "gs-application.h"

#include <stdlib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <packagekit-glib2/packagekit.h>

#ifdef GDK_WINDOWING_X11
#include <gtk/gtkx.h>
#endif

#include "gs-dbus-helper.h"
#include "gs-box.h"
#include "gs-cleanup.h"
#include "gs-first-run-dialog.h"
#include "gs-shell.h"
#include "gs-update-monitor.h"
#include "gs-proxy-settings.h"
#include "gs-plugin-loader.h"
#include "gs-profile.h"
#include "gs-shell-search-provider.h"
#include "gs-offline-updates.h"
#include "gs-folders.h"
#include "gs-utils.h"


struct _GsApplication {
	GtkApplication	 parent;
	GsProfile	*profile;
	GCancellable	*cancellable;
	GtkApplication	*application;
	GtkCssProvider	*provider;
	GsPluginLoader	*plugin_loader;
	gint		 pending_apps;
	GsShell		*shell;
	GsUpdateMonitor *update_monitor;
	GsProxySettings *proxy_settings;
	GsDbusHelper	*dbus_helper;
	GsShellSearchProvider *search_provider;
	GNetworkMonitor *network_monitor;
	GSettings       *settings;
};

struct _GsApplicationClass {
	GtkApplicationClass parent_class;
};

G_DEFINE_TYPE (GsApplication, gs_application, GTK_TYPE_APPLICATION);

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
	application->profile = gs_profile_new ();
}

static void
download_updates_setting_changed (GSettings     *settings,
				  const gchar   *key,
				  GsApplication *app)
{
	if (!gs_updates_are_managed () &&
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

	permission = gs_offline_updates_permission_get ();
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
	app->network_monitor = g_object_ref (g_network_monitor_get_default ());

	g_signal_connect (app->network_monitor, "network-changed",
			  G_CALLBACK (network_changed_cb), app);

	network_changed_cb (app->network_monitor,
			    g_network_monitor_get_network_available (app->network_monitor),
			    app);
}

static void
gs_application_initialize_plugins (GsApplication *app)
{
	static gboolean initialized = FALSE;
	_cleanup_error_free_ GError *error = NULL;

	if (initialized)
		return;

	initialized = TRUE;

	app->plugin_loader = gs_plugin_loader_new ();
	gs_plugin_loader_set_location (app->plugin_loader, NULL);
	if (!gs_plugin_loader_setup (app->plugin_loader, &error)) {
		g_warning ("Failed to setup plugins: %s", error->message);
		exit (1);
	}

	/* show the priority of each plugin */
	gs_plugin_loader_dump_state (app->plugin_loader);

}

static void
gs_application_provide_search (GsApplication *app)
{
	gs_application_initialize_plugins (app);
	app->search_provider = gs_shell_search_provider_new ();
	gs_shell_search_provider_setup (app->search_provider,
					app->plugin_loader);
}

static void
gs_application_show_first_run_dialog (GsApplication *app)
{
	GtkWidget *dialog;

	if (g_settings_get_boolean (app->settings, "first-run") == TRUE) {
		dialog = gs_first_run_dialog_new ();
		gtk_window_set_transient_for (GTK_WINDOW (dialog), gs_shell_get_window (app->shell));
		gtk_window_present (GTK_WINDOW (dialog));

		g_settings_set_boolean (app->settings, "first-run", FALSE);
	}
}

static void
theme_changed (GtkSettings *settings, GParamSpec *pspec, GsApplication *app)
{
	_cleanup_object_unref_ GFile *file = NULL;
	_cleanup_free_ gchar *theme = NULL;

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

	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   DATADIR "/gnome-software/icons/hicolor");

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

	app->cancellable = g_cancellable_new ();

	gs_shell_setup (app->shell, app->plugin_loader, app->cancellable);
	gtk_application_add_window (GTK_APPLICATION (app), gs_shell_get_window (app->shell));

	g_signal_connect_swapped (app->shell, "loaded",
				  G_CALLBACK (gtk_window_present), gs_shell_get_window (app->shell));
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
		 gpointer       app)
{
	const gchar *authors[] = {
		"Richard Hughes",
		"Matthias Clasen",
		"Allan Day",
		"Ryan Lerch",
		"William Jon McCann",
		NULL
	};
	const gchar *copyright = "Copyright \xc2\xa9 2013 Richard Hughes, Matthias Clasen";
	GtkIconTheme *icon_theme;
	GList *windows;
	GtkWindow *parent = NULL;
	_cleanup_object_unref_ GdkPixbuf *logo = NULL;

	gs_application_initialize_ui (app);

	windows = gtk_application_get_windows (GTK_APPLICATION (app));
	if (windows)
		parent = windows->data;

	icon_theme = gtk_icon_theme_get_default ();
	logo = gtk_icon_theme_load_icon (icon_theme, "gnome-software", 256, 0, NULL);

	gtk_show_about_dialog (parent,
			       /* TRANSLATORS: this is the title of the about window */
			       "title", _("About Software"),
			       /* TRANSLATORS: this is the application name */
			       "program-name", _("Software"),
			       "authors", authors,
			       /* TRANSLATORS: well, we seem to think so, anyway */
			       "comments", _("A nice way to manage the software on your system."),
			       "copyright", copyright,
			       "license-type", GTK_LICENSE_GPL_2_0,
			       "logo", logo,
			       "translator-credits", _("translator-credits"),
			       "version", VERSION,
			       NULL);
}

static void
profile_activated (GSimpleAction *action,
		   GVariant      *parameter,
		   gpointer       data)
{
	GsApplication *app = GS_APPLICATION (data);
	gs_profile_dump (app->profile);
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
	else
		gs_shell_show_details (app->shell, id);
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
	_cleanup_error_free_ GError *error = NULL;
	_cleanup_object_unref_ GAppInfo *appinfo = NULL;
	_cleanup_object_unref_ GAppLaunchContext *context = NULL;

	desktop_id = g_variant_get_string (parameter, NULL);
	display = gdk_display_get_default ();
	appinfo = G_APP_INFO (g_desktop_app_info_new (desktop_id));
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
	gs_offline_updates_show_error ();
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
	gchar **resources;

	g_variant_get (parameter, "(&s^as&s)", &mode, &resources, &startup_id);

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
	{ "set-mode", set_mode_activated, "s", NULL, NULL },
	{ "search", search_activated, "s", NULL, NULL },
	{ "details", details_activated, "(ss)", NULL, NULL },
	{ "filename", filename_activated, "(s)", NULL, NULL },
	{ "launch", launch_activated, "s", NULL, NULL },
	{ "show-offline-update-error", show_offline_updates_error, NULL, NULL, NULL },
	{ "install-resources", install_resources_activated, "(sass)", NULL, NULL },
	{ "nop", NULL, NULL, NULL }
};

static void
gs_application_startup (GApplication *application)
{
	G_APPLICATION_CLASS (gs_application_parent_class)->startup (application);

	g_type_ensure (GS_TYPE_BOX);

	g_action_map_add_action_entries (G_ACTION_MAP (application),
					 actions, G_N_ELEMENTS (actions),
					 application);

	GS_APPLICATION (application)->proxy_settings = gs_proxy_settings_new ();
	GS_APPLICATION (application)->dbus_helper = gs_dbus_helper_new ();
	GS_APPLICATION (application)->settings = g_settings_new ("org.gnome.software");
	gs_application_monitor_permission (GS_APPLICATION (application));
	gs_application_monitor_updates (GS_APPLICATION (application));
	gs_application_provide_search (GS_APPLICATION (application));
	gs_application_monitor_network (GS_APPLICATION (application));
	gs_folders_convert ();
}

static void
gs_application_activate (GApplication *application)
{
	gs_application_initialize_ui (GS_APPLICATION (application));
	gs_shell_set_mode (GS_APPLICATION (application)->shell, GS_SHELL_MODE_OVERVIEW);
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
	g_clear_object (&app->proxy_settings);
	g_clear_object (&app->profile);
	g_clear_object (&app->search_provider);
	g_clear_object (&app->network_monitor);
	g_clear_object (&app->dbus_helper);
	g_clear_object (&app->settings);

	G_OBJECT_CLASS (gs_application_parent_class)->dispose (object);
}

static gboolean
gs_application_local_command_line (GApplication *app, gchar ***args, gint *status)
{
	GOptionContext *context;
	gboolean gapplication_service = FALSE;
	gchar *mode = NULL;
	gchar *search = NULL;
	gchar *id = NULL;
	gboolean activate_ui = TRUE;
	gboolean prefer_local = FALSE;
	gboolean version = FALSE;
	gboolean profile = FALSE;
	gboolean verbose = FALSE;
	gint argc;
	_cleanup_free_ gchar *local_filename = NULL;
	const GOptionEntry options[] = {
		{ "gapplication-service", '\0', 0, G_OPTION_ARG_NONE, &gapplication_service,
		   _("Enter GApplication service mode"), NULL }, 
		{ "mode", '\0', 0, G_OPTION_ARG_STRING, &mode,
		  /* TRANSLATORS: this is a command line option */
		  _("Start up mode: either ‘updates’, ‘updated’, ‘installed’ or ‘overview’"), _("MODE") },
		{ "search", '\0', 0, G_OPTION_ARG_STRING, &search,
		  _("Search for applications"), _("SEARCH") },
		{ "details", '\0', 0, G_OPTION_ARG_STRING, &id,
		  _("Show application details"), _("ID") },
		{ "local-filename", '\0', 0, G_OPTION_ARG_FILENAME, &local_filename,
		  _("Open a local package file"), _("FILENAME") },
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose,
		  _("Show verbose debugging information"), NULL },
		{ "profile", 0, 0, G_OPTION_ARG_NONE, &profile,
		  _("Show profiling information for the service"), NULL },
		{ "prefer-local", '\0', 0, G_OPTION_ARG_NONE, &prefer_local,
		  _("Prefer local file sources to AppStream"), NULL },
		{ "version", 0, 0, G_OPTION_ARG_NONE, &version, NULL, NULL },
		{ NULL}
	};
	_cleanup_error_free_ GError *error = NULL;

	context = g_option_context_new ("");
	g_option_context_add_main_entries (context, options, NULL);

	argc = g_strv_length (*args);
	if (!g_option_context_parse (context, &argc, args, &error)) {
		g_printerr ("%s\n", error->message);
		*status = 1;
		goto out;
	}

	if (verbose)
		g_setenv ("G_MESSAGES_DEBUG", "all", TRUE);

	/* prefer local sources */
	if (prefer_local)
		g_setenv ("GNOME_SOFTWARE_PREFER_LOCAL", "true", TRUE);

	if (version) {
		g_print ("gnome-software " VERSION "\n");
		*status = 0;
		goto out;
	}

	if (gapplication_service) {
		GApplicationFlags flags;

		flags = g_application_get_flags (app);
		g_application_set_flags (app, flags | G_APPLICATION_IS_SERVICE);
		activate_ui = FALSE;
	}

	if (!g_application_register (app, NULL, &error)) {
		g_printerr ("%s\n", error->message);
		*status = 1;
		goto out;
	}

	if (profile) {
		activate_ui = FALSE;
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"profile",
						NULL);
	}

	if (mode != NULL) {
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"set-mode",
						g_variant_new_string (mode));
	} else if (search != NULL) {
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"search",
						g_variant_new_string (search));
	} else if (id != NULL) {
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"details",
						g_variant_new ("(ss)", id, ""));
	} else if (local_filename != NULL) {
		g_action_group_activate_action (G_ACTION_GROUP (app),
						"filename",
						g_variant_new ("(s)", local_filename));
	} else if (activate_ui) {
		g_application_activate (app);
	}

	*status = 0;

out:
	g_option_context_free (context);
	return TRUE;
}

static void
gs_application_class_init (GsApplicationClass *class)
{
	G_OBJECT_CLASS (class)->dispose = gs_application_dispose;
	G_APPLICATION_CLASS (class)->startup = gs_application_startup;
	G_APPLICATION_CLASS (class)->activate = gs_application_activate;
	G_APPLICATION_CLASS (class)->local_command_line = gs_application_local_command_line;
}

GsApplication *
gs_application_new (void)
{
	g_set_prgname("org.gnome.Software");
	return g_object_new (GS_APPLICATION_TYPE,
			     "application-id", "org.gnome.Software",
			     NULL);
}

/* vim: set noexpandtab: */
