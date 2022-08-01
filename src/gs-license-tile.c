/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Philip Withnall <pwithnall@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

/**
 * SECTION:gs-license-tile
 * @short_description: A tile for displaying license information about an app
 *
 * #GsLicenseTile is a tile which displays high-level license information about
 * an app. Broadly, whether it is FOSS or proprietary.
 *
 * It checks the license information in the provided #GsApp. If
 * #GsLicenseTile:app is %NULL, the behaviour of the widget is undefined.
 *
 * Since: 41
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gs-common.h"
#include "gs-license-tile.h"
#include "gs-lozenge.h"

struct _GsLicenseTile
{
	GtkWidget	 parent_instance;

	GsApp		*app;  /* (nullable) (owned) */
	gulong		 notify_license_handler;
	gulong		 notify_urls_handler;

	GtkWidget	*lozenges[3];
	GtkLabel	*title_label;
	GtkLabel	*description_label;
	GtkLabel	*get_involved_label;
};

G_DEFINE_TYPE (GsLicenseTile, gs_license_tile, GTK_TYPE_WIDGET)

typedef enum {
	PROP_APP = 1,
} GsLicenseTileProperty;

static GParamSpec *obj_props[PROP_APP + 1] = { NULL, };

typedef enum {
	SIGNAL_GET_INVOLVED_ACTIVATED,
} GsFeaturedCarouselSignal;

static guint obj_signals[SIGNAL_GET_INVOLVED_ACTIVATED + 1] = { 0, };

static void
gs_license_tile_init (GsLicenseTile *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gs_license_tile_row_activated_cb (GtkListBox    *box,
                                  GtkListBoxRow *row,
                                  gpointer       user_data)
{
	GsLicenseTile *self = GS_LICENSE_TILE (user_data);

	/* The ‘Get Involved’ row is the only activatable one */
	g_signal_emit (self, obj_signals[SIGNAL_GET_INVOLVED_ACTIVATED], 0);
}

static void
gs_license_tile_refresh (GsLicenseTile *self)
{
	const gchar *title, *css_class;
	const gchar *lozenge_icon_names[3];
	g_autofree gchar *description = NULL;
	gboolean get_involved_visible;
	const gchar *get_involved_label;

	/* Widget behaviour is undefined if the app is unspecified. */
	if (self->app == NULL)
		return;

	if (gs_app_get_license_is_free (self->app)) {
		const gchar *license_spdx, *license_url;

		title = _("Community Built");
		css_class = "green";
		lozenge_icon_names[0] = "heart-filled-symbolic";
		lozenge_icon_names[1] = "community-symbolic";
		lozenge_icon_names[2] = "sign-language-symbolic";
#if AS_CHECK_VERSION(0, 15, 3)
		get_involved_visible = (gs_app_get_url (self->app, AS_URL_KIND_HOMEPAGE) != NULL ||
					gs_app_get_url (self->app, AS_URL_KIND_CONTRIBUTE) != NULL);
#else
		get_involved_visible = (gs_app_get_url (self->app, AS_URL_KIND_HOMEPAGE) != NULL);
#endif
		get_involved_label = _("_Get Involved");

		license_spdx = gs_app_get_license (self->app);
		license_url = as_get_license_url (license_spdx);

		if (license_url != NULL) {
			/* Translators: The first placeholder here is a link to information about the license, and the second placeholder here is the name of a software license. */
			description = g_strdup_printf (_("This software is developed in the open by a community of volunteers, and released under the <a href=\"%s\">%s license</a>."
							 "\n\n"
							 "You can contribute and help make it even better."),
							 license_url,
							 license_spdx);
		} else {
			/* Translators: The placeholder here is the name of a software license. */
			description = g_strdup_printf (_("This software is developed in the open by a community of volunteers, and released under the %s license."
							 "\n\n"
							 "You can contribute and help make it even better."),
							 license_spdx);
		}
	} else {
		title = _("Proprietary");
		css_class = "grey";
		lozenge_icon_names[0] = "dialog-warning-symbolic";
		lozenge_icon_names[1] = "face-sad-symbolic";
		lozenge_icon_names[2] = "padlock-open-symbolic";
		get_involved_visible = TRUE;
		get_involved_label = _("_Learn More");

		description = g_strdup (_("This software is not developed in the open, so only its developers know how it works. It may be insecure in ways that are hard to detect, and it may change without oversight."
					  "\n\n"
					  "You may not be able to contribute to this software."));
	}

	for (gsize i = 0; i < G_N_ELEMENTS (self->lozenges); i++) {
		GtkStyleContext *context = gtk_widget_get_style_context (self->lozenges[i]);
		gtk_style_context_remove_class (context, "green");
		gtk_style_context_remove_class (context, "grey");
		gtk_style_context_add_class (context, css_class);
		gs_lozenge_set_icon_name (GS_LOZENGE (self->lozenges[i]), lozenge_icon_names[i]);
	}

	gtk_label_set_label (self->title_label, title);
	gtk_label_set_label (self->description_label, description);
	gtk_widget_set_visible (GTK_WIDGET (self->get_involved_label), get_involved_visible);
	gtk_label_set_label (self->get_involved_label, get_involved_label);
}

static void
notify_license_or_urls_cb (GObject    *object,
                           GParamSpec *pspec,
                           gpointer    user_data)
{
	gs_license_tile_refresh (GS_LICENSE_TILE (user_data));
}

static void
gs_license_tile_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
	GsLicenseTile *self = GS_LICENSE_TILE (object);

	switch ((GsLicenseTileProperty) prop_id) {
	case PROP_APP:
		g_value_set_object (value, gs_license_tile_get_app (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_license_tile_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
	GsLicenseTile *self = GS_LICENSE_TILE (object);

	switch ((GsLicenseTileProperty) prop_id) {
	case PROP_APP:
		gs_license_tile_set_app (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_license_tile_dispose (GObject *object)
{
	GsLicenseTile *self = GS_LICENSE_TILE (object);

	gs_license_tile_set_app (self, NULL);
	gs_widget_remove_all (GTK_WIDGET (self), NULL);

	G_OBJECT_CLASS (gs_license_tile_parent_class)->dispose (object);
}

static void
gs_license_tile_class_init (GsLicenseTileClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_license_tile_get_property;
	object_class->set_property = gs_license_tile_set_property;
	object_class->dispose = gs_license_tile_dispose;

	/**
	 * GsLicenseTile:app: (nullable)
	 *
	 * Application to display license information for.
	 *
	 * If this is %NULL, the state of the widget is undefined.
	 *
	 * Since: 41
	 */
	obj_props[PROP_APP] =
		g_param_spec_object ("app", NULL, NULL,
				     GS_TYPE_APP,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	/**
	 * GsLicenseTile::get-involved-activated:
	 *
	 * Emitted when the ‘Get Involved’ button is clicked, for a #GsApp which
	 * is FOSS licensed.
	 *
	 * Typically the caller should open the app’s ‘get involved’ link or
	 * homepage when this signal is emitted.
	 *
	 * Since: 41
	 */
	obj_signals[SIGNAL_GET_INVOLVED_ACTIVATED] =
		g_signal_new ("get-involved-activated",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-license-tile.ui");

	gtk_widget_class_bind_template_child_full (widget_class, "lozenge0", FALSE, G_STRUCT_OFFSET (GsLicenseTile, lozenges[0]));
	gtk_widget_class_bind_template_child_full (widget_class, "lozenge1", FALSE, G_STRUCT_OFFSET (GsLicenseTile, lozenges[1]));
	gtk_widget_class_bind_template_child_full (widget_class, "lozenge2", FALSE, G_STRUCT_OFFSET (GsLicenseTile, lozenges[2]));
	gtk_widget_class_bind_template_child (widget_class, GsLicenseTile, title_label);
	gtk_widget_class_bind_template_child (widget_class, GsLicenseTile, description_label);
	gtk_widget_class_bind_template_child (widget_class, GsLicenseTile, get_involved_label);

	gtk_widget_class_bind_template_callback (widget_class, gs_license_tile_row_activated_cb);

	gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

/**
 * gs_license_tile_new:
 * @app: (nullable) (transfer none): app to display the license information for
 *
 * Create a new #GsLicenseTile.
 *
 * Returns: (transfer full) (type GsLicenseTile): a new #GsLicenseTile
 * Since: 41
 */
GtkWidget *
gs_license_tile_new (GsApp *app)
{
	g_return_val_if_fail (app == NULL || GS_IS_APP (app), NULL);

	return g_object_new (GS_TYPE_LICENSE_TILE,
			     "app", app,
			     NULL);
}

/**
 * gs_license_tile_get_app:
 * @self: a #GsLicenseTile
 *
 * Get the value of #GsLicenseTile:app.
 *
 * Returns: (transfer none) (nullable): the app being displayed in the tile
 * Since: 41
 */
GsApp *
gs_license_tile_get_app (GsLicenseTile *self)
{
	g_return_val_if_fail (GS_IS_LICENSE_TILE (self), NULL);

	return self->app;
}

/**
 * gs_license_tile_set_app:
 * @self: a #GsLicenseTile
 * @app: (nullable) (transfer none): new app to display in the tile
 *
 * Set the value of #GsLicenseTile:app to @app.
 *
 * Since: 41
 */
void
gs_license_tile_set_app (GsLicenseTile *self,
                         GsApp         *app)
{
	g_return_if_fail (GS_IS_LICENSE_TILE (self));
	g_return_if_fail (app == NULL || GS_IS_APP (app));

	if (self->app == app)
		return;

	g_clear_signal_handler (&self->notify_license_handler, self->app);
	g_clear_signal_handler (&self->notify_urls_handler, self->app);

	g_set_object (&self->app, app);

	if (self->app != NULL) {
		self->notify_license_handler = g_signal_connect (self->app, "notify::license", G_CALLBACK (notify_license_or_urls_cb), self);
		self->notify_urls_handler = g_signal_connect (self->app, "notify::urls", G_CALLBACK (notify_license_or_urls_cb), self);
	}

	gs_license_tile_refresh (self);

	g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_APP]);
}
