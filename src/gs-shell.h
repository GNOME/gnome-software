/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2014-2017 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_SHELL (gs_shell_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsShell, gs_shell, GS, SHELL, GObject)

struct _GsShellClass
{
	GObjectClass			 parent_class;

	void (* loaded)		 (GsShell *shell);
};

typedef enum {
	GS_SHELL_MODE_UNKNOWN,
	GS_SHELL_MODE_OVERVIEW,
	GS_SHELL_MODE_INSTALLED,
	GS_SHELL_MODE_SEARCH,
	GS_SHELL_MODE_UPDATES,
	GS_SHELL_MODE_DETAILS,
	GS_SHELL_MODE_CATEGORY,
	GS_SHELL_MODE_EXTRAS,
	GS_SHELL_MODE_MODERATE,
	GS_SHELL_MODE_LOADING,
	GS_SHELL_MODE_LAST
} GsShellMode;

typedef enum {
	GS_SHELL_INTERACTION_NONE	= (0u),
	GS_SHELL_INTERACTION_NOTIFY	= (1u << 0),
	GS_SHELL_INTERACTION_FULL	= (1u << 1) | GS_SHELL_INTERACTION_NOTIFY,
	GS_SHELL_INTERACTION_LAST
} GsShellInteraction;

GsShell		*gs_shell_new			(void);
void		 gs_shell_activate		(GsShell	*shell);
void		 gs_shell_refresh		(GsShell	*shell,
						 GCancellable	*cancellable);
void		 gs_shell_change_mode		(GsShell	*shell,
						 GsShellMode	 mode,
						 gpointer	 data,
						 gboolean	 scroll_up);
void		 gs_shell_reset_state		(GsShell	*shell);
void		 gs_shell_set_mode		(GsShell	*shell,
						 GsShellMode	 mode);
void		 gs_shell_modal_dialog_present	(GsShell	*shell,
						 GtkDialog	*dialog);
GsShellMode	 gs_shell_get_mode		(GsShell	*shell);
const gchar	*gs_shell_get_mode_string	(GsShell	*shell);
void		 gs_shell_install		(GsShell		*shell,
						 GsApp			*app,
						 GsShellInteraction	interaction);
void		 gs_shell_show_installed_updates(GsShell	*shell);
void		 gs_shell_show_sources		(GsShell	*shell);
void		 gs_shell_show_prefs		(GsShell	*shell);
void		 gs_shell_show_app		(GsShell	*shell,
						 GsApp		*app);
void		 gs_shell_show_category		(GsShell	*shell,
						 GsCategory	*category);
void		 gs_shell_show_search		(GsShell	*shell,
						 const gchar	*search);
void		 gs_shell_show_local_file	(GsShell	*shell,
						 GFile		*file);
void		 gs_shell_show_search_result	(GsShell	*shell,
						 const gchar	*id,
						 const gchar    *search);
void		 gs_shell_show_extras_search	(GsShell	*shell,
						 const gchar	*mode,
						 gchar		**resources);
void		 gs_shell_show_uri		(GsShell	*shell,
						 const gchar	*url);
void		 gs_shell_setup			(GsShell	*shell,
						 GsPluginLoader	*plugin_loader,
						 GCancellable	*cancellable);
gboolean	 gs_shell_is_active		(GsShell	*shell);
GtkWindow	*gs_shell_get_window		(GsShell	*shell);
void		 gs_shell_show_notification	(GsShell	*shell,
						 const gchar	*title);

G_END_DECLS
