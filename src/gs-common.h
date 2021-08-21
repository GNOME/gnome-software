/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <gio/gdesktopappinfo.h>
#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

typedef void (*GsRemoveFunc) (GtkWidget *container,
			      GtkWidget *child);

void	 gs_start_spinner		(GtkSpinner	*spinner);
void	 gs_stop_spinner		(GtkSpinner	*spinner);
void	 gs_widget_remove_all		(GtkWidget	*container,
					 GsRemoveFunc    remove_func);
void	 gs_grab_focus_when_mapped	(GtkWidget	*widget);

void	 gs_app_notify_installed	(GsApp		*app);
GtkResponseType
	gs_app_notify_unavailable	(GsApp		*app,
					 GtkWindow	*parent);

gboolean	 gs_utils_is_current_desktop	(const gchar	*name);
gchar		*gs_utils_set_key_colors_in_css	(const gchar	*css,
						 GsApp		*app);
void		 gs_utils_widget_set_css	(GtkWidget	*widget,
						 GtkCssProvider	**provider,
						 const gchar	*class_name,
						 const gchar	*css);
const gchar	*gs_utils_get_error_value	(const GError	*error);
void		 gs_utils_show_error_dialog	(GtkWindow	*parent,
						 const gchar	*title,
						 const gchar	*msg,
						 const gchar	*details);
gchar		*gs_utils_build_unique_id_kind	(AsComponentKind kind,
						 const gchar	*id);
gboolean	 gs_utils_list_has_component_fuzzy	(GsAppList	*list,
						 GsApp		*app);
void		 gs_utils_reboot_notify		(GsAppList	*list,
						 gboolean	 is_install);
gchar		*gs_utils_time_to_string	(gint64		 unix_time_seconds);
void		 gs_utils_invoke_reboot_async	(GCancellable	*cancellable,
						 GAsyncReadyCallback ready_callback,
						 gpointer	 user_data);
gboolean	gs_utils_split_time_difference	(gint64 unix_time_seconds,
						 gint *out_minutes_ago,
						 gint *out_hours_ago,
						 gint *out_days_ago,
						 gint *out_weeks_ago,
						 gint *out_months_ago,
						 gint *out_years_ago);

#if !GLIB_CHECK_VERSION(2, 62, 0)

#define  g_clear_signal_handler(handler_id_ptr, instance)           \
  G_STMT_START {                                                    \
    gpointer const _instance      = (instance);                     \
    gulong *const _handler_id_ptr = (handler_id_ptr);               \
    const gulong _handler_id      = *_handler_id_ptr;               \
                                                                    \
    if (_handler_id > 0)                                            \
      {                                                             \
        *_handler_id_ptr = 0;                                       \
        g_signal_handler_disconnect (_instance, _handler_id);       \
      }                                                             \
  } G_STMT_END                                                      \

#endif

G_END_DECLS
