/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2012-2016 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
 * Copyright (C) 2014-2018 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>

#include "gs-app-row.h"
#include "gs-star-widget.h"
#include "gs-progress-button.h"
#include "gs-common.h"
#include "gs-folders.h"

typedef struct
{
	GsApp		*app;
	GtkWidget	*image;
	GtkWidget	*name_box;
	GtkWidget	*name_label;
	GtkWidget	*version_box;
	GtkWidget	*version_current_label;
	GtkWidget	*version_arrow_label;
	GtkWidget	*version_update_label;
	GtkWidget	*system_updates_label; /* Only for "System Updates" app */
	GtkWidget	*star;
	GtkWidget	*description_box;
	GtkWidget	*description_label;
	GtkWidget	*button_box;
	GtkWidget	*button_revealer;
	GtkWidget	*button;
	GtkWidget	*spinner;
	GtkWidget	*label;
	GtkWidget	*box_tag;
	GtkWidget	*label_warning;
	GtkWidget	*label_origin;
	GtkWidget	*label_installed;
	GtkWidget	*label_app_size;
	gboolean	 colorful;
	gboolean	 show_buttons;
	gboolean	 show_rating;
	gboolean	 show_description;
	gboolean	 show_source;
	gboolean	 show_update;
	gboolean	 show_installed_size;
	guint		 pending_refresh_id;
	gboolean	 is_narrow;
} GsAppRowPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (GsAppRow, gs_app_row, GTK_TYPE_LIST_BOX_ROW)

enum {
	SIGNAL_BUTTON_CLICKED,
	SIGNAL_UNREVEALED,
	SIGNAL_LAST
};

static guint signals [SIGNAL_LAST] = { 0 };

typedef enum {
	PROP_APP = 1,
	PROP_SHOW_DESCRIPTION,
	PROP_SHOW_SOURCE,
	PROP_SHOW_BUTTONS,
	PROP_SHOW_INSTALLED_SIZE,
	PROP_IS_NARROW,
} GsAppRowProperty;

static GParamSpec *obj_props[PROP_IS_NARROW + 1] = { NULL, };

/**
 * gs_app_row_get_description:
 *
 * Return value: PangoMarkup
 **/
static GString *
gs_app_row_get_description (GsAppRow *app_row)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);
	const gchar *tmp = NULL;

	/* convert the markdown update description into PangoMarkup */
	if (priv->show_update) {
		tmp = gs_app_get_update_details_markup (priv->app);
		if (tmp != NULL && tmp[0] != '\0')
			return g_string_new (tmp);
	}

	/* if missing summary is set, return it without escaping in order to
	 * correctly show hyperlinks */
	if (gs_app_get_state (priv->app) == GS_APP_STATE_UNAVAILABLE) {
		tmp = gs_app_get_summary_missing (priv->app);
		if (tmp != NULL && tmp[0] != '\0')
			return g_string_new (tmp);
	}

	/* try all these things in order */
	if (tmp == NULL || (tmp != NULL && tmp[0] == '\0'))
		tmp = gs_app_get_summary (priv->app);
	if (tmp == NULL || (tmp != NULL && tmp[0] == '\0'))
		tmp = gs_app_get_description (priv->app);
	if (tmp == NULL || (tmp != NULL && tmp[0] == '\0'))
		tmp = gs_app_get_name (priv->app);
	if (tmp == NULL)
		return NULL;
	return g_string_new (tmp);
}

static void
gs_app_row_update_button_reveal (GsAppRow *app_row)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);
	gboolean sensitive = gtk_widget_get_sensitive (priv->button);

	gtk_widget_set_visible (priv->button_revealer, sensitive || !priv->is_narrow);
}

static void
gs_app_row_refresh_button (GsAppRow *app_row, gboolean missing_search_result)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);
	GtkStyleContext *context;

	/* disabled */
	if (!priv->show_buttons) {
		gs_app_row_update_button_reveal (app_row);
		gtk_widget_set_visible (priv->button, FALSE);
		return;
	}

	/* label */
	switch (gs_app_get_state (priv->app)) {
	case GS_APP_STATE_UNAVAILABLE:
		gtk_widget_set_visible (priv->button, TRUE);
		if (missing_search_result) {
			/* TRANSLATORS: this is a button next to the search results that
			 * allows the application to be easily installed */
			gs_progress_button_set_label (GS_PROGRESS_BUTTON (priv->button), _("Visit Website"));
			gs_progress_button_set_icon_name (GS_PROGRESS_BUTTON (priv->button), NULL);
		} else {
			/* TRANSLATORS: this is a button next to the search results that
			 * allows the application to be easily installed.
			 * The ellipsis indicates that further steps are required */
			gs_progress_button_set_label (GS_PROGRESS_BUTTON (priv->button), _("Install…"));
			gs_progress_button_set_icon_name (GS_PROGRESS_BUTTON (priv->button), NULL);
		}
		break;
	case GS_APP_STATE_QUEUED_FOR_INSTALL:
		gtk_widget_set_visible (priv->button, TRUE);
		/* TRANSLATORS: this is a button next to the search results that
		 * allows to cancel a queued install of the application */
		gs_progress_button_set_label (GS_PROGRESS_BUTTON (priv->button), _("Cancel"));
		gs_progress_button_set_icon_name (GS_PROGRESS_BUTTON (priv->button), "edit-delete-symbolic");
		break;
	case GS_APP_STATE_AVAILABLE:
	case GS_APP_STATE_AVAILABLE_LOCAL:
		gtk_widget_set_visible (priv->button, TRUE);
		/* TRANSLATORS: this is a button next to the search results that
		 * allows the application to be easily installed */
		gs_progress_button_set_label (GS_PROGRESS_BUTTON (priv->button), _("Install"));
		gs_progress_button_set_icon_name (GS_PROGRESS_BUTTON (priv->button), "list-add-symbolic");
		break;
	case GS_APP_STATE_UPDATABLE_LIVE:
		gtk_widget_set_visible (priv->button, TRUE);
		if (priv->show_update) {
			/* TRANSLATORS: this is a button in the updates panel
			 * that allows the app to be easily updated live */
			gs_progress_button_set_label (GS_PROGRESS_BUTTON (priv->button), _("Update"));
			gs_progress_button_set_icon_name (GS_PROGRESS_BUTTON (priv->button), "software-update-available-symbolic");
		} else {
			/* TRANSLATORS: this is a button next to the search results that
			 * allows the application to be easily removed */
			gs_progress_button_set_label (GS_PROGRESS_BUTTON (priv->button), _("Uninstall"));
			gs_progress_button_set_icon_name (GS_PROGRESS_BUTTON (priv->button), "app-remove-symbolic");
		}
		break;
	case GS_APP_STATE_UPDATABLE:
	case GS_APP_STATE_INSTALLED:
		if (!gs_app_has_quirk (priv->app, GS_APP_QUIRK_COMPULSORY))
			gtk_widget_set_visible (priv->button, TRUE);
		/* TRANSLATORS: this is a button next to the search results that
		 * allows the application to be easily removed */
		gs_progress_button_set_label (GS_PROGRESS_BUTTON (priv->button), _("Uninstall"));
		gs_progress_button_set_icon_name (GS_PROGRESS_BUTTON (priv->button), "app-remove-symbolic");
		break;
	case GS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (priv->button, TRUE);
		/* TRANSLATORS: this is a button next to the search results that
		 * shows the status of an application being installed */
		gs_progress_button_set_label (GS_PROGRESS_BUTTON (priv->button), _("Installing"));
		gs_progress_button_set_icon_name (GS_PROGRESS_BUTTON (priv->button), NULL);
		break;
	case GS_APP_STATE_REMOVING:
		gtk_widget_set_visible (priv->button, TRUE);
		/* TRANSLATORS: this is a button next to the search results that
		 * shows the status of an application being erased */
		gs_progress_button_set_label (GS_PROGRESS_BUTTON (priv->button), _("Uninstalling"));
		gs_progress_button_set_icon_name (GS_PROGRESS_BUTTON (priv->button), NULL);
		break;
	default:
		break;
	}

	/* visible */
	switch (gs_app_get_state (priv->app)) {
	case GS_APP_STATE_UNAVAILABLE:
	case GS_APP_STATE_QUEUED_FOR_INSTALL:
	case GS_APP_STATE_AVAILABLE:
	case GS_APP_STATE_AVAILABLE_LOCAL:
	case GS_APP_STATE_UPDATABLE_LIVE:
	case GS_APP_STATE_INSTALLING:
	case GS_APP_STATE_REMOVING:
		gtk_widget_set_visible (priv->button, TRUE);
		break;
	case GS_APP_STATE_UPDATABLE:
	case GS_APP_STATE_INSTALLED:
		gtk_widget_set_visible (priv->button,
					!gs_app_has_quirk (priv->app,
							   GS_APP_QUIRK_COMPULSORY));
		break;
	default:
		gtk_widget_set_visible (priv->button, FALSE);
		break;
	}

	/* colorful */
	context = gtk_widget_get_style_context (priv->button);
	if (!priv->colorful) {
		gtk_style_context_remove_class (context, "destructive-action");
	} else {
		switch (gs_app_get_state (priv->app)) {
		case GS_APP_STATE_UPDATABLE:
		case GS_APP_STATE_INSTALLED:
			gtk_style_context_add_class (context, "destructive-action");
			break;
		case GS_APP_STATE_UPDATABLE_LIVE:
			if (priv->show_update)
				gtk_style_context_remove_class (context, "destructive-action");
			else
				gtk_style_context_add_class (context, "destructive-action");
			break;
		default:
			gtk_style_context_remove_class (context, "destructive-action");
			break;
		}
	}

	/* always insensitive when in selection mode */
	switch (gs_app_get_state (priv->app)) {
	case GS_APP_STATE_INSTALLING:
	case GS_APP_STATE_REMOVING:
		gtk_widget_set_sensitive (priv->button, FALSE);
		break;
	default:
		gtk_widget_set_sensitive (priv->button, TRUE);
		break;
	}

	gs_app_row_update_button_reveal (app_row);
}

static void
gs_app_row_actually_refresh (GsAppRow *app_row)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);
	GtkStyleContext *context;
	GString *str = NULL;
	const gchar *tmp;
	gboolean missing_search_result;
	guint64 size = 0;
	g_autoptr(GIcon) icon = NULL;

	if (priv->app == NULL)
		return;

	/* is this a missing search result from the extras page? */
	missing_search_result = (gs_app_get_state (priv->app) == GS_APP_STATE_UNAVAILABLE &&
	                         gs_app_get_url_missing (priv->app) != NULL);

	/* do a fill bar for the current progress */
	switch (gs_app_get_state (priv->app)) {
	case GS_APP_STATE_INSTALLING:
		gs_progress_button_set_progress (GS_PROGRESS_BUTTON (priv->button),
		                                 gs_app_get_progress (priv->app));
		gs_progress_button_set_show_progress (GS_PROGRESS_BUTTON (priv->button), TRUE);
		break;
	default:
		gs_progress_button_set_show_progress (GS_PROGRESS_BUTTON (priv->button), FALSE);
		break;
	}

	/* join the description lines */
	str = gs_app_row_get_description (app_row);
	if (str != NULL) {
		as_gstring_replace (str, "\n", " ");
		gtk_label_set_label (GTK_LABEL (priv->description_label), str->str);
		g_string_free (str, TRUE);
	} else {
		gtk_label_set_text (GTK_LABEL (priv->description_label), NULL);
	}

	/* add warning */
	if (gs_app_has_quirk (priv->app, GS_APP_QUIRK_REMOVABLE_HARDWARE)) {
		gtk_label_set_text (GTK_LABEL (priv->label_warning),
				    /* TRANSLATORS: during the update the device
				     * will restart into a special update-only mode */
				    _("Device cannot be used during update."));
		gtk_widget_show (priv->label_warning);
	}

	/* where did this app come from */
	if (priv->show_source) {
		tmp = gs_app_get_origin_hostname (priv->app);
		if (tmp != NULL) {
			g_autofree gchar *origin_tmp = NULL;
			/* TRANSLATORS: this refers to where the app came from */
			origin_tmp = g_strdup_printf (_("Source: %s"), tmp);
			gtk_label_set_label (GTK_LABEL (priv->label_origin), origin_tmp);
		}
		gtk_widget_set_visible (priv->label_origin, tmp != NULL);
	} else {
		gtk_widget_set_visible (priv->label_origin, FALSE);
	}

	/* installed tag */
	if (!priv->show_buttons) {
		switch (gs_app_get_state (priv->app)) {
		case GS_APP_STATE_UPDATABLE:
		case GS_APP_STATE_UPDATABLE_LIVE:
		case GS_APP_STATE_INSTALLED:
			gtk_widget_set_visible (priv->label_installed, TRUE);
			break;
		default:
			gtk_widget_set_visible (priv->label_installed, FALSE);
			break;
		}
	} else {
		gtk_widget_set_visible (priv->label_installed, FALSE);
	}

	/* name */
	gtk_label_set_label (GTK_LABEL (priv->name_label),
	                     gs_app_get_name (priv->app));

	if (priv->show_update) {
		const gchar *version_current = NULL;
		const gchar *version_update = NULL;

		/* current version */
		tmp = gs_app_get_version_ui (priv->app);
		if (tmp != NULL && tmp[0] != '\0') {
			version_current = tmp;
			gtk_label_set_label (GTK_LABEL (priv->version_current_label),
			                     version_current);
			gtk_widget_show (priv->version_current_label);
		} else {
			gtk_widget_hide (priv->version_current_label);
		}

		/* update version */
		tmp = gs_app_get_update_version_ui (priv->app);
		if (tmp != NULL && tmp[0] != '\0' &&
		    g_strcmp0 (tmp, version_current) != 0) {
			version_update = tmp;
			gtk_label_set_label (GTK_LABEL (priv->version_update_label),
			                     version_update);
			gtk_widget_show (priv->version_update_label);
		} else {
			gtk_widget_hide (priv->version_update_label);
		}

		/* have both: show arrow */
		if (version_current != NULL && version_update != NULL &&
		    g_strcmp0 (version_current, version_update) != 0) {
			gtk_widget_show (priv->version_arrow_label);
		} else {
			gtk_widget_hide (priv->version_arrow_label);
		}

		/* ensure the arrow is the right way round for the text direction,
		 * as arrows are not bidi-mirrored automatically
		 * See section 2 of http://www.unicode.org/L2/L2017/17438-bidi-math-fdbk.html */
		switch (gtk_widget_get_direction (priv->version_box)) {
		case GTK_TEXT_DIR_RTL:
			gtk_label_set_label (GTK_LABEL (priv->version_arrow_label), "←");
			break;
		case GTK_TEXT_DIR_NONE:
		case GTK_TEXT_DIR_LTR:
		default:
			gtk_label_set_label (GTK_LABEL (priv->version_arrow_label), "→");
			break;
		}

		/* show the box if we have either of the versions */
		if (version_current != NULL || version_update != NULL)
			gtk_widget_show (priv->version_box);
		else
			gtk_widget_hide (priv->version_box);

		gtk_widget_hide (priv->star);
	} else {
		gtk_widget_hide (priv->version_box);
		if (missing_search_result || gs_app_get_rating (priv->app) <= 0 || !priv->show_rating) {
			gtk_widget_hide (priv->star);
		} else {
			gtk_widget_show (priv->star);
			gtk_widget_set_sensitive (priv->star, FALSE);
			gs_star_widget_set_rating (GS_STAR_WIDGET (priv->star),
						   gs_app_get_rating (priv->app));
		}
	}

	if (priv->show_update && gs_app_get_special_kind (priv->app) == GS_APP_SPECIAL_KIND_OS_UPDATE) {
		gtk_label_set_label (GTK_LABEL (priv->system_updates_label), gs_app_get_summary (priv->app));
		gtk_widget_show (priv->system_updates_label);
	} else {
		gtk_widget_hide (priv->system_updates_label);
	}

	/* pixbuf */
	icon = gs_app_get_icon_for_size (priv->app,
					 gtk_image_get_pixel_size (GTK_IMAGE (priv->image)),
					 gtk_widget_get_scale_factor (priv->image),
					 "system-component-application");
	gtk_image_set_from_gicon (GTK_IMAGE (priv->image), icon);

	context = gtk_widget_get_style_context (priv->image);
	if (missing_search_result)
		gtk_style_context_add_class (context, "dimmer-label");
	else
		gtk_style_context_remove_class (context, "dimmer-label");

	/* pending label */
	switch (gs_app_get_state (priv->app)) {
	case GS_APP_STATE_QUEUED_FOR_INSTALL:
		gtk_widget_set_visible (priv->label, TRUE);
		gtk_label_set_label (GTK_LABEL (priv->label), _("Pending"));
		break;
	case GS_APP_STATE_PENDING_INSTALL:
		gtk_widget_set_visible (priv->label, TRUE);
		gtk_label_set_label (GTK_LABEL (priv->label), _("Pending install"));
		break;
	case GS_APP_STATE_PENDING_REMOVE:
		gtk_widget_set_visible (priv->label, TRUE);
		gtk_label_set_label (GTK_LABEL (priv->label), _("Pending remove"));
		break;
	default:
		gtk_widget_set_visible (priv->label, FALSE);
		break;
	}

	/* spinner */
	switch (gs_app_get_state (priv->app)) {
	case GS_APP_STATE_REMOVING:
		gtk_spinner_start (GTK_SPINNER (priv->spinner));
		gtk_widget_set_visible (priv->spinner, TRUE);
		break;
	default:
		gtk_widget_set_visible (priv->spinner, FALSE);
		break;
	}

	/* button */
	gs_app_row_refresh_button (app_row, missing_search_result);

	/* hide buttons in the update list, unless the app is live updatable */
	switch (gs_app_get_state (priv->app)) {
	case GS_APP_STATE_UPDATABLE_LIVE:
	case GS_APP_STATE_INSTALLING:
		gtk_widget_set_visible (priv->button_box, TRUE);
		break;
	default:
		gtk_widget_set_visible (priv->button_box, !priv->show_update);
		break;
	}

	/* show the right size */
	if (priv->show_installed_size) {
		size = gs_app_get_size_installed (priv->app);
	}
	if (size != GS_APP_SIZE_UNKNOWABLE && size != 0) {
		g_autofree gchar *sizestr = NULL;
		sizestr = g_format_size (size);
		gtk_label_set_label (GTK_LABEL (priv->label_app_size), sizestr);
		gtk_widget_show (priv->label_app_size);
	} else {
		gtk_widget_hide (priv->label_app_size);
	}

	/* add warning */
	if (priv->show_update) {
		g_autoptr(GString) warning = g_string_new (NULL);
		const gchar *renamed_from;

		if (gs_app_has_quirk (priv->app, GS_APP_QUIRK_NEW_PERMISSIONS))
			g_string_append (warning, _("Requires additional permissions"));

		renamed_from = gs_app_get_renamed_from (priv->app);
		if (renamed_from && g_strcmp0 (renamed_from, gs_app_get_name (priv->app)) != 0) {
			if (warning->len > 0)
				g_string_append (warning, "\n");
			/* Translators: A message to indicate that an app has been renamed. The placeholder is the old human-readable name. */
			g_string_append_printf (warning, _("Renamed from %s"), renamed_from);
		}

		if (warning->len > 0) {
			gtk_label_set_text (GTK_LABEL (priv->label_warning), warning->str);
			gtk_widget_show (priv->label_warning);
		}
	}

	gtk_widget_set_visible (priv->box_tag,
				gtk_widget_get_visible (priv->label_origin) ||
				gtk_widget_get_visible (priv->label_installed) ||
				gtk_widget_get_visible (priv->label_warning));

	gtk_widget_set_visible (priv->description_box,
				gtk_widget_get_visible (priv->box_tag) ||
				gtk_widget_get_visible (priv->description_label));

	gtk_label_set_max_width_chars (GTK_LABEL (priv->name_label),
				       gtk_widget_get_visible (priv->description_label) ? 20 : -1);
}

static void
child_unrevealed (GObject *revealer, GParamSpec *pspec, gpointer user_data)
{
	GsAppRow *app_row = user_data;
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	/* return immediately if we are in destruction (this doesn't, however,
	 * catch the case where we are being removed from a container without
	 * having been destroyed first.)
	 */
	if (priv->app == NULL || !gtk_widget_get_mapped (GTK_WIDGET (app_row)))
		return;

	g_signal_emit (app_row, signals[SIGNAL_UNREVEALED], 0);
}

void
gs_app_row_unreveal (GsAppRow *app_row)
{
	GtkWidget *child;
	GtkWidget *revealer;

	g_return_if_fail (GS_IS_APP_ROW (app_row));

	child = gtk_list_box_row_get_child (GTK_LIST_BOX_ROW (app_row));
	gtk_widget_set_sensitive (child, FALSE);

	revealer = gtk_revealer_new ();
	gtk_revealer_set_reveal_child (GTK_REVEALER (revealer), TRUE);
	gtk_widget_show (revealer);

	g_object_ref (child);
	gtk_list_box_row_set_child (GTK_LIST_BOX_ROW (app_row), revealer);
	gtk_revealer_set_child (GTK_REVEALER (revealer), child);
	g_object_unref (child);

	g_signal_connect (revealer, "notify::child-revealed",
			  G_CALLBACK (child_unrevealed), app_row);
	gtk_revealer_set_reveal_child (GTK_REVEALER (revealer), FALSE);
}

GsApp *
gs_app_row_get_app (GsAppRow *app_row)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);
	g_return_val_if_fail (GS_IS_APP_ROW (app_row), NULL);
	return priv->app;
}

static gboolean
gs_app_row_refresh_idle_cb (gpointer user_data)
{
	GsAppRow *app_row = GS_APP_ROW (user_data);
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);
	priv->pending_refresh_id = 0;
	gs_app_row_actually_refresh (app_row);
	return G_SOURCE_REMOVE;
}

/* Schedule an idle call to gs_app_row_actually_refresh() unless one’s already pending. */
static void
gs_app_row_schedule_refresh (GsAppRow *app_row)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	if (priv->pending_refresh_id > 0)
		return;
	priv->pending_refresh_id = g_idle_add (gs_app_row_refresh_idle_cb, app_row);
}

static void
gs_app_row_notify_props_changed_cb (GsApp *app,
				    GParamSpec *pspec,
				    GsAppRow *app_row)
{
	gs_app_row_schedule_refresh (app_row);
}

static void
gs_app_row_set_app (GsAppRow *app_row, GsApp *app)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	priv->app = g_object_ref (app);

	g_signal_connect_object (priv->app, "notify::state",
				 G_CALLBACK (gs_app_row_notify_props_changed_cb),
				 app_row, 0);
	g_signal_connect_object (priv->app, "notify::rating",
				 G_CALLBACK (gs_app_row_notify_props_changed_cb),
				 app_row, 0);
	g_signal_connect_object (priv->app, "notify::progress",
				 G_CALLBACK (gs_app_row_notify_props_changed_cb),
				 app_row, 0);
	g_signal_connect_object (priv->app, "notify::allow-cancel",
				 G_CALLBACK (gs_app_row_notify_props_changed_cb),
				 app_row, 0);

	g_object_notify (G_OBJECT (app_row), "app");

	gs_app_row_schedule_refresh (app_row);
}

static void
gs_app_row_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GsAppRow *app_row = GS_APP_ROW (object);
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	switch ((GsAppRowProperty) prop_id) {
	case PROP_APP:
		g_value_set_object (value, priv->app);
		break;
	case PROP_SHOW_DESCRIPTION:
		g_value_set_boolean (value, gs_app_row_get_show_description (app_row));
		break;
	case PROP_SHOW_SOURCE:
		g_value_set_boolean (value, priv->show_source);
		break;
	case PROP_SHOW_BUTTONS:
		g_value_set_boolean (value, priv->show_buttons);
		break;
	case PROP_SHOW_INSTALLED_SIZE:
		g_value_set_boolean (value, priv->show_installed_size);
		break;
	case PROP_IS_NARROW:
		g_value_set_boolean (value, gs_app_row_get_is_narrow (app_row));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_row_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GsAppRow *app_row = GS_APP_ROW (object);

	switch ((GsAppRowProperty) prop_id) {
	case PROP_APP:
		gs_app_row_set_app (app_row, g_value_get_object (value));
		break;
	case PROP_SHOW_DESCRIPTION:
		gs_app_row_set_show_description (app_row, g_value_get_boolean (value));
		break;
	case PROP_SHOW_SOURCE:
		gs_app_row_set_show_source (app_row, g_value_get_boolean (value));
		break;
	case PROP_SHOW_BUTTONS:
		gs_app_row_set_show_buttons (app_row, g_value_get_boolean (value));
		break;
	case PROP_SHOW_INSTALLED_SIZE:
		gs_app_row_set_show_installed_size (app_row, g_value_get_boolean (value));
		break;
	case PROP_IS_NARROW:
		gs_app_row_set_is_narrow (app_row, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_row_dispose (GObject *object)
{
	GsAppRow *app_row = GS_APP_ROW (object);
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	if (priv->app)
		g_signal_handlers_disconnect_by_func (priv->app, gs_app_row_notify_props_changed_cb, app_row);

	g_clear_object (&priv->app);
	if (priv->pending_refresh_id != 0) {
		g_source_remove (priv->pending_refresh_id);
		priv->pending_refresh_id = 0;
	}

	G_OBJECT_CLASS (gs_app_row_parent_class)->dispose (object);
}

static void
gs_app_row_class_init (GsAppRowClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_app_row_get_property;
	object_class->set_property = gs_app_row_set_property;
	object_class->dispose = gs_app_row_dispose;

	/**
	 * GsAppRow:app:
	 *
	 * The #GsApp to show in this row.
	 *
	 * Since: 3.38
	 */
	obj_props[PROP_APP] =
		g_param_spec_object ("app", NULL, NULL,
				     GS_TYPE_APP,
				     G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * GsAppRow:show-description:
	 *
	 * Show the description of the app in the row.
	 *
	 * Since: 41
	 */
	obj_props[PROP_SHOW_DESCRIPTION] =
		g_param_spec_boolean ("show-description", NULL, NULL,
				      TRUE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsAppRow:show-source:
	 *
	 * Show the source of the app in the row.
	 *
	 * Since: 3.38
	 */
	obj_props[PROP_SHOW_SOURCE] =
		g_param_spec_boolean ("show-source", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * GsAppRow:show-buttons:
	 *
	 * Show buttons (such as Install, Cancel or Update) in the app row.
	 *
	 * Since: 3.38
	 */
	obj_props[PROP_SHOW_BUTTONS] =
		g_param_spec_boolean ("show-buttons", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * GsAppRow:show-installed-size:
	 *
	 * Show the installed size of the app in the row.
	 *
	 * Since: 3.38
	 */
	obj_props[PROP_SHOW_INSTALLED_SIZE] =
		g_param_spec_boolean ("show-installed-size", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

	/**
	 * GsAppRow:is-narrow:
	 *
	 * Whether the row is in narrow mode.
	 *
	 * In narrow mode, the row will take up less horizontal space, doing so
	 * by e.g. using icons rather than labels in buttons. This is needed to
	 * keep the UI useable on small form-factors like smartphones.
	 *
	 * Since: 41
	 */
	obj_props[PROP_IS_NARROW] =
		g_param_spec_boolean ("is-narrow", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	signals [SIGNAL_BUTTON_CLICKED] =
		g_signal_new ("button-clicked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsAppRowClass, button_clicked),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals [SIGNAL_UNREVEALED] =
		g_signal_new ("unrevealed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GsAppRowClass, unrevealed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-app-row.ui");

	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, image);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, name_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, name_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, version_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, version_current_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, version_arrow_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, version_update_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, system_updates_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, star);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, description_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, description_label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, button_box);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, button_revealer);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, button);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, spinner);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, label);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, box_tag);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, label_warning);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, label_origin);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, label_installed);
	gtk_widget_class_bind_template_child_private (widget_class, GsAppRow, label_app_size);
}

static void
button_clicked (GtkWidget *widget, GsAppRow *app_row)
{
	g_signal_emit (app_row, signals[SIGNAL_BUTTON_CLICKED], 0);
}

static void
gs_app_row_init (GsAppRow *app_row)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	priv->show_description = TRUE;

	gtk_widget_init_template (GTK_WIDGET (app_row));

	g_signal_connect (priv->button, "clicked",
			  G_CALLBACK (button_clicked), app_row);
}

void
gs_app_row_set_size_groups (GsAppRow *app_row,
			    GtkSizeGroup *name,
			    GtkSizeGroup *desc,
			    GtkSizeGroup *button_label,
			    GtkSizeGroup *button_image)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	if (name != NULL)
		gtk_size_group_add_widget (name, priv->name_box);
	if (desc != NULL)
		gtk_size_group_add_widget (desc, priv->description_box);
	gs_progress_button_set_size_groups (GS_PROGRESS_BUTTON (priv->button), button_label, button_image);
}

void
gs_app_row_set_colorful (GsAppRow *app_row, gboolean colorful)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	priv->colorful = colorful;
	gs_app_row_schedule_refresh (app_row);
}

void
gs_app_row_set_show_buttons (GsAppRow *app_row, gboolean show_buttons)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	priv->show_buttons = show_buttons;
	g_object_notify (G_OBJECT (app_row), "show-buttons");
	gs_app_row_schedule_refresh (app_row);
}

void
gs_app_row_set_show_rating (GsAppRow *app_row, gboolean show_rating)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	priv->show_rating = show_rating;
	gs_app_row_schedule_refresh (app_row);
}

/**
 * gs_app_row_get_show_description:
 * @app_row: a #GsAppRow
 *
 * Get the value of #GsAppRow:show-description.
 *
 * Returns: %TRUE if the description is shown, %FALSE otherwise
 *
 * Since: 41
 */
gboolean
gs_app_row_get_show_description (GsAppRow *app_row)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	g_return_val_if_fail (GS_IS_APP_ROW (app_row), FALSE);

	return priv->show_description;
}

/**
 * gs_app_row_set_show_description:
 * @app_row: a #GsAppRow
 * @show_description: %TRUE to show the description, %FALSE otherwise
 *
 * Set the value of #GsAppRow:show-description.
 *
 * Since: 41
 */
void
gs_app_row_set_show_description (GsAppRow *app_row, gboolean show_description)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	g_return_if_fail (GS_IS_APP_ROW (app_row));

	show_description = !!show_description;

	if (priv->show_description == show_description)
		return;

	priv->show_description = show_description;
	g_object_notify_by_pspec (G_OBJECT (app_row), obj_props[PROP_SHOW_DESCRIPTION]);
	gs_app_row_schedule_refresh (app_row);
}

void
gs_app_row_set_show_source (GsAppRow *app_row, gboolean show_source)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	priv->show_source = show_source;
	g_object_notify (G_OBJECT (app_row), "show-source");
	gs_app_row_schedule_refresh (app_row);
}

void
gs_app_row_set_show_installed_size (GsAppRow *app_row, gboolean show_size)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);
	priv->show_installed_size = show_size;
	g_object_notify (G_OBJECT (app_row), "show-installed-size");
	gs_app_row_schedule_refresh (app_row);
}

/**
 * gs_app_row_get_is_narrow:
 * @app_row: a #GsAppRow
 *
 * Get the value of #GsAppRow:is-narrow.
 *
 * Retruns: %TRUE if the row is in narrow mode, %FALSE otherwise
 *
 * Since: 41
 */
gboolean
gs_app_row_get_is_narrow (GsAppRow *app_row)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	g_return_val_if_fail (GS_IS_APP_ROW (app_row), FALSE);

	return priv->is_narrow;
}

/**
 * gs_app_row_set_is_narrow:
 * @app_row: a #GsAppRow
 * @is_narrow: %TRUE to set the row in narrow mode, %FALSE otherwise
 *
 * Set the value of #GsAppRow:is-narrow.
 *
 * Since: 41
 */
void
gs_app_row_set_is_narrow (GsAppRow *app_row, gboolean is_narrow)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	g_return_if_fail (GS_IS_APP_ROW (app_row));

	is_narrow = !!is_narrow;

	if (priv->is_narrow == is_narrow)
		return;

	priv->is_narrow = is_narrow;
	gs_app_row_update_button_reveal (app_row);
	g_object_notify_by_pspec (G_OBJECT (app_row), obj_props[PROP_IS_NARROW]);
}

/**
 * gs_app_row_set_show_update:
 *
 * Only really useful for the update panel to call
 **/
void
gs_app_row_set_show_update (GsAppRow *app_row, gboolean show_update)
{
	GsAppRowPrivate *priv = gs_app_row_get_instance_private (app_row);

	priv->show_update = show_update;
	gs_app_row_schedule_refresh (app_row);
}

GtkWidget *
gs_app_row_new (GsApp *app)
{
	g_return_val_if_fail (GS_IS_APP (app), NULL);

	return g_object_new (GS_TYPE_APP_ROW,
			     "app", app,
			     NULL);
}
