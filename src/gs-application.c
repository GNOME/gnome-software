/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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
#include <packagekit-glib2/packagekit.h>

#include "gs-shell.h"
#include "gs-plugin-loader.h"

struct _GsApplication {
        GtkApplication parent;

        GCancellable            *cancellable;
        GtkApplication          *application;
        PkTask                  *task;
        GtkCssProvider          *provider;
        GsPluginLoader          *plugin_loader;
        gint                     pending_apps;
        GsShell                 *shell;
};

struct _GsApplicationClass {
        GtkApplicationClass parent_class;
};

G_DEFINE_TYPE (GsApplication, gs_application, GTK_TYPE_APPLICATION);

static void
gs_application_init (GsApplication *application)
{
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
        GdkPixbuf *logo;
        GList *windows;
        GtkWindow *parent = NULL;

        windows = gtk_application_get_windows (GTK_APPLICATION (app));
        if (windows)
                parent = windows->data;

        icon_theme = gtk_icon_theme_get_default ();
        logo = gtk_icon_theme_load_icon (icon_theme, "system-software-install", 256, 0, NULL);

        gtk_show_about_dialog (parent,
                               "title", _("About GNOME Software"),
                               "program-name", _("GNOME Software"),
                               "authors", authors,
                               "comments", _("A nice way to manager the software on your system."),
                               "copyright", copyright,
                               "license-type", GTK_LICENSE_GPL_2_0,
                               "logo", logo,
                               "translator-credits", _("translator-credits"),
                               "version", VERSION,
                               NULL);

        g_object_unref (logo);
}

static void
quit_activated (GSimpleAction *action,
                GVariant      *parameter,
                gpointer       app)
{
        g_application_quit (G_APPLICATION (app));
}

static GActionEntry actions[] = {
        { "about", about_activated, NULL, NULL, NULL },
        { "quit", quit_activated, NULL, NULL, NULL }
};

static void
gs_application_startup (GApplication *application)
{
        GsApplication *app = GS_APPLICATION (application);
        GtkBuilder *builder;
        GMenuModel *app_menu;
        GtkWindow *window;
        GFile *file;
        GError *error = NULL;

        G_APPLICATION_CLASS (gs_application_parent_class)->startup (application);

        /* set up the app menu */
        g_action_map_add_action_entries (G_ACTION_MAP (app),
                                         actions, G_N_ELEMENTS (actions),
                                         application);
        builder = gtk_builder_new_from_resource ("/org/gnome/software/app-menu.ui");
        app_menu = G_MENU_MODEL (gtk_builder_get_object (builder, "appmenu"));
        gtk_application_set_app_menu (GTK_APPLICATION (app), app_menu);
        g_object_unref (builder);

        /* get CSS */
        app->provider = gtk_css_provider_new ();
        gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
                                                   GTK_STYLE_PROVIDER (app->provider),
                                                   GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        file = g_file_new_for_uri ("resource:///org/gnome/software/gtk-style.css");
        gtk_css_provider_load_from_file (app->provider, file, NULL);
        g_object_unref (file);

        /* setup pk */
        app->task = pk_task_new ();
        g_object_set (app->task, "background", FALSE, NULL);

        /* setup plugins */
        app->plugin_loader = gs_plugin_loader_new ();
        gs_plugin_loader_set_location (app->plugin_loader, NULL);
        if (!gs_plugin_loader_setup (app->plugin_loader, &error)) {
                g_warning ("Failed to setup plugins: %s", error->message);
                exit (1);
        }
        gs_plugin_loader_set_enabled (app->plugin_loader, "hardcoded-descriptions", TRUE);
        gs_plugin_loader_set_enabled (app->plugin_loader, "hardcoded-featured", TRUE);
        gs_plugin_loader_set_enabled (app->plugin_loader, "hardcoded-kind", TRUE);
        gs_plugin_loader_set_enabled (app->plugin_loader, "hardcoded-popular", TRUE);
        gs_plugin_loader_set_enabled (app->plugin_loader, "hardcoded-ratings", TRUE);
        gs_plugin_loader_set_enabled (app->plugin_loader, "hardcoded-screenshots", TRUE);
        gs_plugin_loader_set_enabled (app->plugin_loader, "hardcoded-menu-spec", TRUE);
        gs_plugin_loader_set_enabled (app->plugin_loader, "local-ratings", TRUE);
        gs_plugin_loader_set_enabled (app->plugin_loader, "packagekit", TRUE);
        gs_plugin_loader_set_enabled (app->plugin_loader, "packagekit-refine", TRUE);
        gs_plugin_loader_set_enabled (app->plugin_loader, "desktopdb", TRUE);
        gs_plugin_loader_set_enabled (app->plugin_loader, "datadir-apps", TRUE);
        gs_plugin_loader_set_enabled (app->plugin_loader, "datadir-filename", TRUE);

        /* setup UI */
        app->shell = gs_shell_new ();

        app->cancellable = g_cancellable_new ();

        window = gs_shell_setup (app->shell, app->plugin_loader, app->cancellable);
        gtk_application_add_window (GTK_APPLICATION (app), window);

        gtk_widget_show (GTK_WIDGET (window));
}

static void
gs_application_activate (GApplication *application)
{
        gs_shell_activate (GS_APPLICATION (application)->shell);
}

static int
gs_application_command_line (GApplication            *application,
                             GApplicationCommandLine *cmdline)
{
        GsApplication *app = GS_APPLICATION (application);
        GOptionContext *context;
        gchar *mode = NULL;
        gboolean help = FALSE;
        gboolean verbose = FALSE;
        const GOptionEntry options[] = {
                { "mode", '\0', 0, G_OPTION_ARG_STRING, &mode,
                  _("Start up mode, either 'updates', 'installed' or 'overview'"), _("MODE") },
                { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, NULL, NULL },
                { "help", '?', 0, G_OPTION_ARG_NONE, &help, NULL, NULL },

                { NULL}
        };
        gchar **args, **argv;
        gint argc;
        gint i;
        GError *error = NULL;

        args = g_application_command_line_get_arguments (cmdline, &argc);
        /* We have to make an extra copy of the array, since g_option_context_parse()
         * assumes that it can remove strings from the array without freeing them.
         */
        argv = g_new (gchar*, argc + 1);
        for (i = 0; i <= argc; i++)
                argv[i] = args[i];

        context = g_option_context_new ("");
        g_option_context_set_help_enabled (context, FALSE);
        g_option_context_add_main_entries (context, options, NULL);
        if (!g_option_context_parse (context, &argc, &argv, &error)) {
                g_application_command_line_printerr (cmdline, "%s\n", error->message);
                g_error_free (error);
                g_application_command_line_set_exit_status (cmdline, 1);
        }
        else if (help) {
                gchar *text;
                text = g_option_context_get_help (context, FALSE, NULL);
                g_application_command_line_print (cmdline, "%s",  text);
                g_free (text);
        }
        if (verbose)
                g_setenv ("G_MESSAGES_DEBUG", "all", FALSE);
        if (mode) {
                if (g_strcmp0 (mode, "updates") == 0) {
                        gs_shell_set_mode (app->shell, GS_SHELL_MODE_UPDATES);
                } else if (g_strcmp0 (mode, "installed") == 0) {
                        gs_shell_set_mode (app->shell, GS_SHELL_MODE_INSTALLED);
                } else if (g_strcmp0 (mode, "overview") == 0) {
                        /* this is the default */
                } else {
                        g_warning ("Mode '%s' not recognised", mode);
                }
        }

        g_free (argv);
        g_strfreev (args);

        g_option_context_free (context);

        return 0;
}

static void
gs_application_finalize (GObject *object)
{
        GsApplication *app = GS_APPLICATION (object);

        g_clear_object (&app->plugin_loader);
        g_clear_object (&app->task);
        g_clear_object (&app->cancellable);
        g_clear_object (&app->shell);
        g_clear_object (&app->provider);
        G_OBJECT_CLASS (gs_application_parent_class)->finalize (object);
}

static void
gs_application_class_init (GsApplicationClass *class)
{
        G_OBJECT_CLASS (class)->finalize = gs_application_finalize;
        G_APPLICATION_CLASS (class)->startup = gs_application_startup;
        G_APPLICATION_CLASS (class)->activate = gs_application_activate;
        G_APPLICATION_CLASS (class)->command_line = gs_application_command_line;
}

GsApplication *
gs_application_new (void)
{
        return g_object_new (GS_APPLICATION_TYPE,
                            "application-id", "org.gnome.Software",
                            "flags", G_APPLICATION_HANDLES_COMMAND_LINE,
                            NULL);
}
