/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Matthew Leeds <mwleeds@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-app-version-history-row.h"

#include "gs-common.h"

struct _GsAppVersionHistoryRow
{
	GtkListBoxRow	 parent_instance;

	GtkWidget	*version_number_label;
	GtkWidget	*version_date_label;
	GtkWidget	*version_description_label;
};

G_DEFINE_TYPE (GsAppVersionHistoryRow, gs_app_version_history_row, GTK_TYPE_LIST_BOX_ROW)

static void
gs_app_version_history_row_class_init (GsAppVersionHistoryRowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-app-version-history-row.ui");

	gtk_widget_class_bind_template_child (widget_class, GsAppVersionHistoryRow, version_number_label);
	gtk_widget_class_bind_template_child (widget_class, GsAppVersionHistoryRow, version_date_label);
	gtk_widget_class_bind_template_child (widget_class, GsAppVersionHistoryRow, version_description_label);
}

static void
gs_app_version_history_row_init (GsAppVersionHistoryRow *row)
{
	gtk_widget_set_has_window (GTK_WIDGET (row), FALSE);
	gtk_widget_init_template (GTK_WIDGET (row));
}

void
gs_app_version_history_row_set_info (GsAppVersionHistoryRow *row,
				     const char *version_number,
				     guint64     version_date,
				     const char *version_description)
{
	g_autofree char *version_date_string = NULL;
	g_autofree char *version_date_string_tooltip = NULL;

	if (version_number == NULL || *version_number == '\0')
		return;

	if (version_description != NULL && *version_description != '\0') {
		g_autofree char *version_tmp = NULL;
		version_tmp = g_strdup_printf (_("New in Version %s"), version_number);
		gtk_label_set_label (GTK_LABEL (row->version_number_label), version_tmp);
		gtk_label_set_label (GTK_LABEL (row->version_description_label), version_description);
	} else {
		g_autofree char *version_tmp = NULL;
		const gchar *version_description_fallback;
		version_tmp = g_strdup_printf (_("Version %s"), version_number);
		gtk_label_set_label (GTK_LABEL (row->version_number_label), version_tmp);
		version_description_fallback = _("No details for this release");
		gtk_label_set_label (GTK_LABEL (row->version_description_label), version_description_fallback);
		gtk_style_context_add_class (gtk_widget_get_style_context (row->version_description_label), "dim-label");
	}

	if (version_date != 0) {
		g_autoptr(GDateTime) date_time = NULL;
		const gchar *format_string;

		/* this is the date in the form of "x weeks ago" or "y months ago" */
		version_date_string = gs_utils_time_to_string ((gint64) version_date);

		/* TRANSLATORS: This is the date string with: day number, month name, year.
		   i.e. "25 May 2012" */
		format_string = _("%e %B %Y");
		date_time = g_date_time_new_from_unix_local (version_date);
		version_date_string_tooltip = g_date_time_format (date_time, format_string);
	}

	if (version_date_string == NULL)
		gtk_widget_set_visible (row->version_date_label, FALSE);
	else
		gtk_label_set_label (GTK_LABEL (row->version_date_label), version_date_string);

	if (version_date_string_tooltip != NULL)
		gtk_widget_set_tooltip_text (row->version_date_label, version_date_string_tooltip);
}

GtkWidget *
gs_app_version_history_row_new (void)
{
	GsAppVersionHistoryRow *row;

	row = g_object_new (GS_TYPE_APP_VERSION_HISTORY_ROW, NULL);
	return GTK_WIDGET (row);
}
