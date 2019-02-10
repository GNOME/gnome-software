/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012-2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2015 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>
#include <locale.h>

#include "gs-application.h"
#include "gs-debug.h"

int
main (int argc, char **argv)
{
	int status = 0;
	g_autoptr(GDesktopAppInfo) appinfo = NULL;
	g_autoptr(GsApplication) application = NULL;
	g_autoptr(GsDebug) debug = gs_debug_new ();

	setlocale (LC_ALL, "");

	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* redirect logs */
	application = gs_application_new ();
	appinfo = g_desktop_app_info_new ("org.gnome.Software.desktop");
	g_set_application_name (g_app_info_get_name (G_APP_INFO (appinfo)));
	status = g_application_run (G_APPLICATION (application), argc, argv);
	return status;
}

/* vim: set noexpandtab: */
