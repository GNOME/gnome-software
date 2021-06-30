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
 * SECTION:gs-app-context-bar
 * @short_description: A bar containing context tiles describing an app
 *
 * #GsAppContextBar is a bar which contains ‘context tiles’ to describe some of
 * the key features of an app. Each tile describes one aspect of the app, such
 * as its download/installed size, hardware requirements, or content rating.
 * Tiles are intended to convey the most pertinent information about aspects of
 * the app, leaving further detail to be shown in a more detailed dialog.
 *
 * The widget has no special appearance if the app is unset, so callers will
 * typically want to hide the bar in that case.
 *
 * Since: 41
 */

#include "config.h"

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <handy.h>
#include <locale.h>

#include "gs-app.h"
#include "gs-app-context-bar.h"

typedef struct
{
	GtkWidget	*lozenge;
	GtkWidget	*lozenge_content;
	GtkLabel	*title;
	GtkLabel	*description;
} GsAppContextTile;

typedef enum
{
	STORAGE_TILE,
	SAFETY_TILE,
	HARDWARE_SUPPORT_TILE,
	AGE_RATING_TILE,
} GsAppContextTileType;
#define N_TILE_TYPES (AGE_RATING_TILE + 1)

struct _GsAppContextBar
{
	GtkGrid			 parent_instance;

	GsApp			*app;  /* (nullable) (owned) */
	gulong			 app_notify_handler;

	GsAppContextTile	tiles[N_TILE_TYPES];
};

G_DEFINE_TYPE (GsAppContextBar, gs_app_context_bar, GTK_TYPE_GRID)

typedef enum {
	PROP_APP = 1,
} GsAppContextBarProperty;

static GParamSpec *obj_props[PROP_APP + 1] = { NULL, };

static void
update_storage_tile (GsAppContextBar *self)
{
	g_autofree gchar *lozenge_text = NULL;
	const gchar *title;
	const gchar *description;
	guint64 size_bytes;

	g_assert (self->app != NULL);

	if (gs_app_is_installed (self->app)) {
		size_bytes = gs_app_get_size_installed (self->app);
		/* Translators: The disk usage of an application when installed.
		 * This is displayed in a context tile, so the string should be short. */
		title = _("Installed Size");
		/* FIXME: Calculate data and cache usage so we can set the text
		 * as per https://gitlab.gnome.org/Teams/Design/software-mockups/-/raw/master/adaptive/context-tiles.png
		description = "Includes 230 MB of data and 1.8 GB of cache"; */
		description = "";
	} else {
		size_bytes = gs_app_get_size_download (self->app);
		/* Translators: The download size of an application.
		 * This is displayed in a context tile, so the string should be short. */
		title = _("Download Size");
		/* FIXME: Calculate data and cache usage so we can set the text
		 * as per https://gitlab.gnome.org/Teams/Design/software-mockups/-/raw/master/adaptive/context-tiles.png
		description = "Needs 150 MB of additional system downloads"; */
		description = "";
	}

	if (size_bytes == 0 || size_bytes == GS_APP_SIZE_UNKNOWABLE) {
		/* Translators: This is displayed for the download size in an
		 * app’s context tile if the size is unknown. It should be short
		 * (at most a couple of characters wide). */
		lozenge_text = g_strdup (_("?"));
		/* Translators: Displayed if the download or installed size of
		 * an app could not be determined.
		 * This is displayed in a context tile, so the string should be short. */
		description = _("Size is unknown");
	} else {
		lozenge_text = g_format_size (size_bytes);
	}

	gtk_label_set_text (GTK_LABEL (self->tiles[STORAGE_TILE].lozenge_content), lozenge_text);
	gtk_label_set_text (self->tiles[STORAGE_TILE].title, title);
	gtk_label_set_text (self->tiles[STORAGE_TILE].description, description);
}

typedef enum
{
	/* The code in this file relies on the fact that these enum values
	 * numerically increase as they get more unsafe. */
	SAFETY_SAFE,
	SAFETY_POTENTIALLY_UNSAFE,
	SAFETY_UNSAFE
} SafetyRating;

static void
add_to_safety_rating (SafetyRating *chosen_rating,
                      GPtrArray    *descriptions,
                      SafetyRating  item_rating,
                      const gchar  *item_description)
{
	/* Clear the existing descriptions and replace with @item_description if
	 * this item increases the @chosen_rating. This means the final list of
	 * @descriptions will only be the items which caused @chosen_rating to
	 * be so high. */
	if (item_rating > *chosen_rating) {
		g_ptr_array_set_size (descriptions, 0);
		*chosen_rating = item_rating;
	}

	if (item_rating == *chosen_rating)
		g_ptr_array_add (descriptions, (gpointer) item_description);
}

static void
update_safety_tile (GsAppContextBar *self)
{
	const gchar *icon_name, *title, *css_class;
	g_autoptr(GPtrArray) descriptions = g_ptr_array_new_with_free_func (NULL);
	g_autofree gchar *description = NULL;
	GsAppPermissions permissions;
	GtkStyleContext *context;

	/* Treat everything as safe to begin with, and downgrade its safety
	 * based on app properties. */
	SafetyRating chosen_rating = SAFETY_SAFE;

	g_assert (self->app != NULL);

	permissions = gs_app_get_permissions (self->app);
	for (GsAppPermissions i = GS_APP_PERMISSIONS_NONE; i < GS_APP_PERMISSIONS_LAST; i <<= 1) {
		if (!(permissions & i))
			continue;

		switch (i) {
		case GS_APP_PERMISSIONS_NONE:
			add_to_safety_rating (&chosen_rating, descriptions,
					      SAFETY_SAFE,
					      /* Translators: This indicates an app requires no permissions to run.
					       * It’s used in a context tile, so should be short. */
					      _("No permissions"));
			break;
		case GS_APP_PERMISSIONS_NETWORK:
			add_to_safety_rating (&chosen_rating, descriptions,
					      SAFETY_POTENTIALLY_UNSAFE,
					      /* Translators: This indicates an app uses the network.
					       * It’s used in a context tile, so should be short. */
					      _("Has network access"));
			break;
		case GS_APP_PERMISSIONS_SYSTEM_BUS:
			add_to_safety_rating (&chosen_rating, descriptions,
					      SAFETY_POTENTIALLY_UNSAFE,
					      /* Translators: This indicates an app uses D-Bus system services.
					       * It’s used in a context tile, so should be short. */
					      _("Uses system services"));
			break;
		case GS_APP_PERMISSIONS_SESSION_BUS:
			add_to_safety_rating (&chosen_rating, descriptions,
					      SAFETY_UNSAFE,
					      /* Translators: This indicates an app uses D-Bus session services.
					       * It’s used in a context tile, so should be short. */
					      _("Uses session services"));
			break;
		case GS_APP_PERMISSIONS_DEVICES:
			add_to_safety_rating (&chosen_rating, descriptions,
					      SAFETY_POTENTIALLY_UNSAFE,
					      /* Translators: This indicates an app can access arbitrary hardware devices.
					       * It’s used in a context tile, so should be short. */
					      _("Can access hardware devices"));
			break;
		case GS_APP_PERMISSIONS_HOME_FULL:
		case GS_APP_PERMISSIONS_FILESYSTEM_FULL:
			add_to_safety_rating (&chosen_rating, descriptions,
					      SAFETY_UNSAFE,
					      /* Translators: This indicates an app can read/write to the user’s home or the entire filesystem.
					       * It’s used in a context tile, so should be short. */
					      _("Can read/write all your data"));
			break;
		case GS_APP_PERMISSIONS_HOME_READ:
		case GS_APP_PERMISSIONS_FILESYSTEM_READ:
			add_to_safety_rating (&chosen_rating, descriptions,
					      SAFETY_UNSAFE,
					      /* Translators: This indicates an app can read (but not write) from the user’s home or the entire filesystem.
					       * It’s used in a context tile, so should be short. */
					      _("Can read all your data"));
			break;
		case GS_APP_PERMISSIONS_DOWNLOADS_FULL:
			add_to_safety_rating (&chosen_rating, descriptions,
					      SAFETY_POTENTIALLY_UNSAFE,
					      /* Translators: This indicates an app can read/write to the user’s Downloads directory.
					       * It’s used in a context tile, so should be short. */
					      _("Can read/write your downloads"));
			break;
		case GS_APP_PERMISSIONS_DOWNLOADS_READ:
			add_to_safety_rating (&chosen_rating, descriptions,
					      SAFETY_POTENTIALLY_UNSAFE,
					      /* Translators: This indicates an app can read (but not write) from the user’s Downloads directory.
					       * It’s used in a context tile, so should be short. */
					      _("Can read your downloads"));
			break;
		case GS_APP_PERMISSIONS_SETTINGS:
			add_to_safety_rating (&chosen_rating, descriptions,
					      SAFETY_POTENTIALLY_UNSAFE,
					      /* Translators: This indicates an app can access or change user settings.
					       * It’s used in a context tile, so should be short. */
					      _("Can access and change user settings"));
			break;
		case GS_APP_PERMISSIONS_X11:
			add_to_safety_rating (&chosen_rating, descriptions,
					      SAFETY_UNSAFE,
					      /* Translators: This indicates an app uses the X11 windowing system.
					       * It’s used in a context tile, so should be short. */
					      _("Uses a legacy windowing system"));
			break;
		case GS_APP_PERMISSIONS_ESCAPE_SANDBOX:
			add_to_safety_rating (&chosen_rating, descriptions,
					      SAFETY_UNSAFE,
					      /* Translators: This indicates an app can escape its sandbox.
					       * It’s used in a context tile, so should be short. */
					      _("Can acquire arbitrary permissions"));
			break;
		default:
			break;
		}
	}

	if (permissions == GS_APP_PERMISSIONS_UNKNOWN)
		add_to_safety_rating (&chosen_rating, descriptions,
				      SAFETY_POTENTIALLY_UNSAFE,
				      /* Translators: This indicates that we don’t know what permissions an app requires to run.
				       * It’s used in a context tile, so should be short. */
				      _("App has unknown permissions"));

	/* Is the code FOSS and hence inspectable? This doesn’t distinguish
	 * between closed source and open-source-but-not-FOSS software, even
	 * though the code of the latter is technically publicly auditable. This
	 * is because I don’t want to get into the business of maintaining lists
	 * of ‘auditable’ source code licenses. */
	if (!gs_app_get_license_is_free (self->app))
		add_to_safety_rating (&chosen_rating, descriptions,
				      SAFETY_POTENTIALLY_UNSAFE,
				      /* Translators: This indicates an app is not licensed under a free software license.
				       * It’s used in a context tile, so should be short. */
				      _("Proprietary code"));
	else
		add_to_safety_rating (&chosen_rating, descriptions,
				      SAFETY_SAFE,
				      /* Translators: This indicates an app’s source code is freely available, so can be audited for security.
				       * It’s used in a context tile, so should be short. */
				      _("Auditable code"));

	/* Does the app come from official sources, such as this distro’s main
	 * repos? */
	if (gs_app_has_quirk (self->app, GS_APP_QUIRK_PROVENANCE))
		add_to_safety_rating (&chosen_rating, descriptions,
				      SAFETY_SAFE,
				      /* Translators: This indicates an app comes from the distribution’s main repositories, so can be trusted.
				       * It’s used in a context tile, so should be short. */
				      _("App comes from a trusted source"));

	if (gs_app_has_quirk (self->app, GS_APP_QUIRK_DEVELOPER_VERIFIED))
		add_to_safety_rating (&chosen_rating, descriptions,
				      SAFETY_SAFE,
				      /* Translators: This indicates an app was written and released by a developer who has been verified.
				       * It’s used in a context tile, so should be short. */
				      _("App developer is verified"));

	g_assert (descriptions->len > 0);

	g_ptr_array_add (descriptions, NULL);
	/* Translators: This string is used to join various other translated
	 * strings into an inline list of reasons why an app has been marked as
	 * ‘safe’, ‘potentially safe’ or ‘unsafe’. For example:
	 * “App comes from a trusted source; Auditable code; No permissions”
	 * If concatenating strings as a list using a separator like this is not
	 * possible in your language, please file an issue against gnome-software:
	 * https://gitlab.gnome.org/GNOME/gnome-software/-/issues/new */
	description = g_strjoinv (_("; "), (gchar **) descriptions->pdata);

	/* Update the UI. */
	switch (chosen_rating) {
	case SAFETY_SAFE:
		icon_name = "safety-symbolic";
		/* Translators: The app is considered safe to install and run.
		 * This is displayed in a context tile, so the string should be short. */
		title = _("Safe");
		css_class = "green";
		break;
	case SAFETY_POTENTIALLY_UNSAFE:
		icon_name = "dialog-question-symbolic";
		/* Translators: The app is considered potentially unsafe to install and run.
		 * This is displayed in a context tile, so the string should be short. */
		title = _("Potentially Unsafe");
		css_class = "yellow";
		break;
	case SAFETY_UNSAFE:
		icon_name = "dialog-warning-symbolic";
		/* Translators: The app is considered unsafe to install and run.
		 * This is displayed in a context tile, so the string should be short. */
		title = _("Unsafe");
		css_class = "red";
		break;
	default:
		g_assert_not_reached ();
	}

	gtk_image_set_from_icon_name (GTK_IMAGE (self->tiles[SAFETY_TILE].lozenge_content), icon_name, GTK_ICON_SIZE_BUTTON);
	gtk_label_set_text (self->tiles[SAFETY_TILE].title, title);
	gtk_label_set_text (self->tiles[SAFETY_TILE].description, description);

	context = gtk_widget_get_style_context (self->tiles[SAFETY_TILE].lozenge);

	gtk_style_context_remove_class (context, "green");
	gtk_style_context_remove_class (context, "yellow");
	gtk_style_context_remove_class (context, "red");

	gtk_style_context_add_class (context, css_class);
}

typedef struct {
	guint min;
	guint max;
} Range;

static void
update_hardware_support_tile (GsAppContextBar *self)
{
	g_autoptr(GPtrArray) relations = NULL;
	AsRelationKind control_relations[AS_CONTROL_KIND_LAST] = { AS_RELATION_KIND_UNKNOWN, };
	GdkDisplay *display;
	GdkMonitor *monitor = NULL;
	GdkRectangle current_screen_size;
	gboolean any_control_relations_set;
	const gchar *icon_name = NULL, *title = NULL, *description = NULL, *css_class = NULL;
	gboolean has_touchscreen = FALSE, has_keyboard = FALSE, has_mouse = FALSE;
	GtkStyleContext *context;

	g_assert (self->app != NULL);

	relations = gs_app_get_relations (self->app);

	/* Extract the %AS_RELATION_ITEM_KIND_CONTROL relations and summarise
	 * them. */
	any_control_relations_set = FALSE;

	for (gsize i = 0; relations != NULL && i < relations->len; i++) {
		AsRelation *relation = AS_RELATION (g_ptr_array_index (relations, i));
		AsRelationKind kind = as_relation_get_kind (relation);

		if (as_relation_get_item_kind (relation) == AS_RELATION_ITEM_KIND_CONTROL) {
			AsControlKind control_kind = as_relation_get_value_control_kind (relation);
			control_relations[control_kind] = MAX (control_relations[control_kind], kind);

			if (kind == AS_RELATION_KIND_REQUIRES ||
			    kind == AS_RELATION_KIND_RECOMMENDS)
				any_control_relations_set = TRUE;
		}
	}

	display = gtk_widget_get_display (GTK_WIDGET (self));

	/* Work out what input devices are available. */
	if (display != NULL) {
		GdkSeat *seat = gdk_display_get_default_seat (display);
		GdkSeatCapabilities seat_capabilities = gdk_seat_get_capabilities (seat);

		has_touchscreen = (seat_capabilities & GDK_SEAT_CAPABILITY_TOUCH);
		has_keyboard = (seat_capabilities & GDK_SEAT_CAPABILITY_KEYBOARD);
		has_mouse = (seat_capabilities & GDK_SEAT_CAPABILITY_POINTER);
	}

	/* Warn about screen size mismatches. Compare against the largest
	 * monitor associated with this widget’s #GdkDisplay, defaulting to
	 * the primary monitor.
	 *
	 * See https://www.freedesktop.org/software/appstream/docs/chap-Metadata.html#tag-requires-recommends-display_length
	 * for the semantics of the display length relations.*/
	if (display != NULL) {
		int n_monitors = gdk_display_get_n_monitors (display);
		int monitor_max_dimension;

		monitor = NULL;
		monitor_max_dimension = 0;

		for (int i = 0; i < n_monitors; i++) {
			GdkMonitor *monitor2 = gdk_display_get_monitor (display, i);
			GdkRectangle monitor_geometry;
			int monitor2_max_dimension;

			if (monitor2 == NULL)
				continue;

			gdk_monitor_get_geometry (monitor2, &monitor_geometry);
			monitor2_max_dimension = MAX (monitor_geometry.width, monitor_geometry.height);

			if (monitor2_max_dimension > monitor_max_dimension ||
			    (gdk_monitor_is_primary (monitor2) &&
			     monitor2_max_dimension == monitor_max_dimension)) {
				monitor = monitor2;
				monitor_max_dimension = monitor2_max_dimension;
				continue;
			}
		}
	}

	if (monitor != NULL) {
		gdk_monitor_get_geometry (monitor, &current_screen_size);

		for (gsize i = 0; relations != NULL && i < relations->len; i++) {
			AsRelation *relation = AS_RELATION (g_ptr_array_index (relations, i));
			guint comparand1;
			Range comparand2;
			gboolean match = TRUE;

			/* All lengths here are in logical/application pixels,
			 * not device pixels. */
			if (as_relation_get_item_kind (relation) == AS_RELATION_ITEM_KIND_DISPLAY_LENGTH &&
			    as_relation_get_kind (relation) == AS_RELATION_KIND_REQUIRES) {
				AsRelationCompare comparator = as_relation_get_compare (relation);

				/* From https://www.freedesktop.org/software/appstream/docs/chap-Metadata.html#tag-requires-recommends-display_length */
				Range display_lengths[] = {
					[AS_DISPLAY_LENGTH_KIND_XSMALL] = { 0, 360 },
					[AS_DISPLAY_LENGTH_KIND_SMALL] = { 360, 768 },
					[AS_DISPLAY_LENGTH_KIND_MEDIUM] = { 768, 1024 },
					[AS_DISPLAY_LENGTH_KIND_LARGE] = { 1024, 3840 },
					[AS_DISPLAY_LENGTH_KIND_XLARGE] = { 3840, G_MAXUINT },
				};

				switch (as_relation_get_display_side_kind (relation)) {
				case AS_DISPLAY_SIDE_KIND_SHORTEST:
					comparand1 = MIN (current_screen_size.width, current_screen_size.height);
					comparand2.min = comparand2.max = as_relation_get_value_px (relation);
					break;
				case AS_DISPLAY_SIDE_KIND_LONGEST:
					comparand1 = MAX (current_screen_size.width, current_screen_size.height);
					comparand2.min = comparand2.max = as_relation_get_value_px (relation);
					break;
				case AS_DISPLAY_SIDE_KIND_UNKNOWN:
				case AS_DISPLAY_SIDE_KIND_LAST:
				default:
					comparand1 = MAX (current_screen_size.width, current_screen_size.height);
					comparand2.min = display_lengths[as_relation_get_value_display_length_kind (relation)].min;
					comparand2.max = display_lengths[as_relation_get_value_display_length_kind (relation)].max;
					break;
				}

				switch (comparator) {
				case AS_RELATION_COMPARE_EQ:
					match = (comparand1 >= comparand2.min &&
						 comparand1 <= comparand2.max);
					break;
				case AS_RELATION_COMPARE_NE:
					match = (comparand1 < comparand2.min ||
						 comparand1 > comparand2.max);
					break;
				case AS_RELATION_COMPARE_LT:
					match = (comparand1 < comparand2.min);
					break;
				case AS_RELATION_COMPARE_GT:
					match = (comparand1 > comparand2.max);
					break;
				case AS_RELATION_COMPARE_LE:
					match = (comparand1 <= comparand2.max);
					break;
				case AS_RELATION_COMPARE_GE:
					match = (comparand1 >= comparand2.min);
					break;
				case AS_RELATION_COMPARE_UNKNOWN:
				case AS_RELATION_COMPARE_LAST:
				default:
					g_assert_not_reached ();
				}

				/* If the current screen size is not supported,
				 * try and summarise the restrictions into a
				 * single context tile. */
				if (!match) {
					if ((comparator == AS_RELATION_COMPARE_LE ||
					     comparator == AS_RELATION_COMPARE_LT) &&
					     comparand1 >= display_lengths[AS_DISPLAY_LENGTH_KIND_MEDIUM].min) {
						icon_name = "phone-symbolic";
						title = _("Mobile Only");
						description = _("Only works on a small screen");
						css_class = "red";
					} else if ((comparator == AS_RELATION_COMPARE_GE ||
						    comparator == AS_RELATION_COMPARE_GT) &&
						   comparand1 <= display_lengths[AS_DISPLAY_LENGTH_KIND_MEDIUM].min) {
						icon_name = "desktop-symbolic";
						title = _("Desktop Only");
						description = _("Only works on a large screen");
						css_class = "red";
					} else {
						icon_name = "desktop-symbolic";
						title = _("Screen Size Mismatch");
						description = _("Requires a specific screen size");
						css_class = "red";
					}
				}
			}
		}
	}

	/* Warn about missing touchscreen or keyboard support. There are some
	 * assumptions here that certain input devices are only available on
	 * certain platforms; they can change in future.
	 *
	 * As with the rest of the tile contents in this function, tile contents
	 * which are checked lower down in the function are only used if nothing
	 * more important has already been set earlier.
	 *
	 * The available information is being summarised to quite an extreme
	 * degree here, and it’s likely this code will have to evolve for
	 * corner cases in future. */
	if (icon_name == NULL &&
	    control_relations[AS_CONTROL_KIND_TOUCH] == AS_RELATION_KIND_REQUIRES &&
	    !has_touchscreen) {
		icon_name = "phone-symbolic";
		title = _("Mobile Only");
		description = _("Requires a touchscreen");
		css_class = "red";
	} else if (icon_name == NULL &&
		   control_relations[AS_CONTROL_KIND_KEYBOARD] == AS_RELATION_KIND_REQUIRES &&
		   !has_keyboard) {
		icon_name = "input-keyboard-symbolic";
		title = _("Desktop Only");
		description = _("Requires a keyboard");
		css_class = "red";
	} else if (icon_name == NULL &&
		   control_relations[AS_CONTROL_KIND_POINTING] == AS_RELATION_KIND_REQUIRES &&
		   !has_mouse) {
		icon_name = "input-mouse-symbolic";
		title = _("Desktop Only");
		description = _("Requires a mouse");
		css_class = "red";
	} else if (icon_name == NULL && !any_control_relations_set) {
		icon_name = "desktop-symbolic";
		title = _("Desktop Only");
		description = _("Not optimized for touch devices or phones");
		css_class = "grey";
	}

	/* Say if the app requires a gamepad. We can’t reliably detect whether
	 * the computer has a gamepad, as it might be unplugged unless the user
	 * is currently playing a game. So this might be shown even if the user
	 * has a gamepad available. */
	if (icon_name == NULL &&
	    control_relations[AS_CONTROL_KIND_GAMEPAD] == AS_RELATION_KIND_REQUIRES) {
		icon_name = "input-gaming-symbolic";
		title = _("Gamepad Needed");
		description = _("Requires a gamepad to play");
		css_class = "yellow";
	}

	/* Otherwise, is it adaptive? Note that %AS_RELATION_KIND_RECOMMENDS
	 * means more like ‘supports’ than ‘recommends’. */
	if (icon_name == NULL &&
	    control_relations[AS_CONTROL_KIND_TOUCH] == AS_RELATION_KIND_RECOMMENDS &&
	    control_relations[AS_CONTROL_KIND_KEYBOARD] == AS_RELATION_KIND_RECOMMENDS &&
	    control_relations[AS_CONTROL_KIND_POINTING] == AS_RELATION_KIND_RECOMMENDS) {
		icon_name = "adaptive-symbolic";
		/* Translators: This is used in a context tile to indicate that
		 * an app works on phones, tablets *and* desktops. It should be
		 * short and in title case. */
		title = _("Adaptive");
		description = _("Works on phones, tablets and desktops");
		css_class = "green";
	}

	/* Fallback. At the moment (June 2021) almost no apps have any metadata
	 * about hardware support, so this case will be hit most of the time.
	 *
	 * So in the absence of any other information, assume that all apps
	 * support desktop, and none support mobile. */
	if (icon_name == NULL) {
		if (!has_keyboard || !has_mouse) {
			icon_name = "desktop-symbolic";
			title = _("Desktop Only");
			description = _("Probably requires a keyboard or mouse");
			css_class = "yellow";
		} else {
			icon_name = "desktop-symbolic";
			title = _("Desktop Only");
			description = _("Not optimized for touch devices or phones");
			css_class = "grey";
		}
	}

	/* Update the UI. The `adaptive-symbolic` icon needs a special size to
	 * be set, as it is wider than it is tall. Setting the size ensures it’s
	 * rendered at the right height. */
	gtk_image_set_from_icon_name (GTK_IMAGE (self->tiles[HARDWARE_SUPPORT_TILE].lozenge_content), icon_name, GTK_ICON_SIZE_BUTTON);
	gtk_image_set_pixel_size (GTK_IMAGE (self->tiles[HARDWARE_SUPPORT_TILE].lozenge_content), g_str_equal (icon_name, "adaptive-symbolic") ? 56 : -1);

	gtk_label_set_text (self->tiles[HARDWARE_SUPPORT_TILE].title, title);
	gtk_label_set_text (self->tiles[HARDWARE_SUPPORT_TILE].description, description);

	context = gtk_widget_get_style_context (self->tiles[HARDWARE_SUPPORT_TILE].lozenge);

	gtk_style_context_remove_class (context, "green");
	gtk_style_context_remove_class (context, "yellow");
	gtk_style_context_remove_class (context, "red");

	gtk_style_context_add_class (context, css_class);

	if (g_str_equal (icon_name, "adaptive-symbolic"))
		gtk_style_context_add_class (context, "wide-image");
	else
		gtk_style_context_remove_class (context, "wide-image");
}

static gchar *
build_age_rating_description (AsContentRating *content_rating)
{
	g_autofree const gchar **rating_ids = as_content_rating_get_all_rating_ids ();
	g_autoptr(GPtrArray) descriptions = g_ptr_array_new_with_free_func (NULL);
	AsContentRatingValue value_bad = AS_CONTENT_RATING_VALUE_NONE;
	guint age_bad = 0;

	/* Ordered from worst to best, these are all OARS 1.0/1.1 categories */
	const gchar * const violence_group[] = {
		"violence-bloodshed",
		"violence-realistic",
		"violence-fantasy",
		"violence-cartoon",
		NULL
	};
	const gchar * const social_group[] = {
		"social-audio",
		"social-chat",
		"social-contacts",
		"social-info",
		NULL
	};
	const gchar * const coalesce_groups[] = {
		"sex-themes",
		"sex-homosexuality",
		NULL
	};

	/* Get the worst category. */
	for (gsize i = 0; rating_ids[i] != NULL; i++) {
		guint rating_age;
		AsContentRatingValue rating_value;

		rating_value = as_content_rating_get_value (content_rating, rating_ids[i]);
		rating_age = as_content_rating_attribute_to_csm_age (rating_ids[i], rating_value);

		if (rating_age > age_bad)
			age_bad = rating_age;
		if (rating_value > value_bad)
			value_bad = rating_value;
	}

	/* If the worst category is nothing, great! Show a more specific message
	 * than a big listing of all the groups. */
	if (value_bad == AS_CONTENT_RATING_VALUE_NONE || age_bad == 0)
		/* Translators: This indicates that the content rating for an
		 * app says it can be used by all ages of people, as it contains
		 * no objectionable content. */
		return g_strdup (_("Contains no age-inappropriate content"));

	/* Add a description for each rating category which contributes to the
	 * @age_bad being as it is. Handle the groups separately.
	 * Intentionally coalesce some categories if they have the same values,
	 * to avoid confusion */
	for (gsize i = 0; rating_ids[i] != NULL; i++) {
		guint rating_age;
		AsContentRatingValue rating_value;

		if (g_strv_contains (violence_group, rating_ids[i]) ||
		    g_strv_contains (social_group, rating_ids[i]))
			continue;

		rating_value = as_content_rating_get_value (content_rating, rating_ids[i]);
		rating_age = as_content_rating_attribute_to_csm_age (rating_ids[i], rating_value);

		if (rating_age < age_bad)
			continue;

		/* Coalesce down to the first element in @coalesce_groups,
		 * unless this group’s value differs. Currently only one
		 * coalesce group is supported. */
		if (g_strv_contains (coalesce_groups + 1, rating_ids[i]) &&
		    as_content_rating_attribute_to_csm_age (coalesce_groups[0],
							    as_content_rating_get_value (content_rating,
											 coalesce_groups[0])) == rating_age)
			continue;

		g_ptr_array_add (descriptions,
				 (gpointer) as_content_rating_attribute_get_description (rating_ids[i], rating_value));
	}

	for (gsize i = 0; violence_group[i] != NULL; i++) {
		guint rating_age;
		AsContentRatingValue rating_value;

		rating_value = as_content_rating_get_value (content_rating, violence_group[i]);
		rating_age = as_content_rating_attribute_to_csm_age (violence_group[i], rating_value);

		if (rating_age < age_bad)
			continue;

		g_ptr_array_add (descriptions,
				 (gpointer) as_content_rating_attribute_get_description (violence_group[i], rating_value));
		break;
	}

	for (gsize i = 0; social_group[i] != NULL; i++) {
		guint rating_age;
		AsContentRatingValue rating_value;

		rating_value = as_content_rating_get_value (content_rating, social_group[i]);
		rating_age = as_content_rating_attribute_to_csm_age (social_group[i], rating_value);

		if (rating_age < age_bad)
			continue;

		g_ptr_array_add (descriptions,
				 (gpointer) as_content_rating_attribute_get_description (social_group[i], rating_value));
		break;
	}

	g_ptr_array_add (descriptions, NULL);
	/* Translators: This string is used to join various other translated
	 * strings into an inline list of reasons why an app has been given a
	 * certain content rating. For example:
	 * “References to alcoholic beverages; Moderated chat functionality between users”
	 * If concatenating strings as a list using a separator like this is not
	 * possible in your language, please file an issue against gnome-software:
	 * https://gitlab.gnome.org/GNOME/gnome-software/-/issues/new */
	return g_strjoinv (_("; "), (gchar **) descriptions->pdata);
}

/* Wrapper around as_content_rating_system_format_age() which returns the short
 * form of the content rating. This doesn’t make a difference for most ratings
 * systems, but it does for ESRB which normally produces quite long strings.
 *
 * FIXME: This should probably be upstreamed into libappstream once it’s been in
 * the GNOME 41 release and stabilised. */
static gchar *
content_rating_system_format_age_short (AsContentRatingSystem system,
                                        guint                 age)
{
	if (system == AS_CONTENT_RATING_SYSTEM_ESRB) {
		if (age >= 18)
			return g_strdup ("AO");
		if (age >= 17)
			return g_strdup ("M");
		if (age >= 13)
			return g_strdup ("T");
		if (age >= 10)
			return g_strdup ("E10+");
		if (age >= 6)
			return g_strdup ("E");

		return g_strdup ("EC");
	}

	return as_content_rating_system_format_age (system, age);
}

static void
update_age_rating_tile (GsAppContextBar *self)
{
	AsContentRating *content_rating;
	AsContentRatingSystem system;
	guint age = G_MAXUINT;  /* unknown */
	g_autofree gchar *age_text = NULL;
	g_autofree gchar *description = NULL;
	const gchar *locale;
	GtkStyleContext *context;
	const gchar *css_age_classes[] = {
		"details-rating-18",
		"details-rating-15",
		"details-rating-12",
		"details-rating-5",
		"details-rating-0",
	};
	gsize age_index;

	g_assert (self->app != NULL);

	/* get the content rating system from the locale */
	locale = setlocale (LC_MESSAGES, NULL);
	system = as_content_rating_system_from_locale (locale);
	g_debug ("content rating system is guessed as %s from %s",
		 as_content_rating_system_to_string (system),
		 locale);

	content_rating = gs_app_get_content_rating (self->app);
	if (content_rating != NULL)
		age = as_content_rating_get_minimum_age (content_rating);

	if (age != G_MAXUINT)
		age_text = content_rating_system_format_age_short (system, age);

	/* Some ratings systems (PEGI) don’t start at age 0 */
	if (content_rating != NULL && age_text == NULL && age == 0)
		/* Translators: The app is considered suitable to be run by all ages of people.
		 * This is displayed in a context tile, so the string should be short. */
		age_text = g_strdup (_("All"));

	context = gtk_widget_get_style_context (self->tiles[AGE_RATING_TILE].lozenge);

	/* We currently only support OARS-1.0 and OARS-1.1 */
	if (age_text == NULL ||
	    (content_rating != NULL &&
	     g_strcmp0 (as_content_rating_get_kind (content_rating), "oars-1.0") != 0 &&
	     g_strcmp0 (as_content_rating_get_kind (content_rating), "oars-1.1") != 0)) {
		for (gsize i = 0; i < G_N_ELEMENTS (css_age_classes); i++)
			gtk_style_context_remove_class (context, css_age_classes[i]);
		gtk_style_context_add_class (context, "grey");

		/* Translators: This app has no age rating information available.
		 * This string is displayed like an icon. Please use any
		 * similarly short punctuation character, word or acronym which
		 * will be widely understood in your region, in this context.
		 * This is displayed in a context tile, so the string should be short. */
		age_text = g_strdup (_("?"));
		description = g_strdup (_("No age rating information available"));
	} else {
		/* Update the CSS */
		if (age >= 18)
			age_index = 0;
		else if (age >= 15)
			age_index = 1;
		else if (age >= 12)
			age_index = 2;
		else if (age >= 5)
			age_index = 3;
		else
			age_index = 4;

		for (gsize i = 0; i < G_N_ELEMENTS (css_age_classes); i++) {
			if (i == age_index)
				gtk_style_context_add_class (context, css_age_classes[i]);
			else
				gtk_style_context_remove_class (context, css_age_classes[i]);
		}
		gtk_style_context_remove_class (context, "grey");

		description = build_age_rating_description (content_rating);
	}

	/* Update the label texts */
	gtk_label_set_text (GTK_LABEL (self->tiles[AGE_RATING_TILE].lozenge_content), age_text);
	gtk_label_set_text (self->tiles[AGE_RATING_TILE].description, description);
}

static void
update_tiles (GsAppContextBar *self)
{
	if (self->app == NULL)
		return;

	update_storage_tile (self);
	update_safety_tile (self);
	update_hardware_support_tile (self);
	update_age_rating_tile (self);
}

static void
app_notify_cb (GObject    *obj,
               GParamSpec *pspec,
               gpointer    user_data)
{
	GsAppContextBar *self = GS_APP_CONTEXT_BAR (user_data);

	update_tiles (self);
}

static void
gs_app_context_bar_init (GsAppContextBar *self)
{
	gtk_widget_set_has_window (GTK_WIDGET (self), FALSE);
	gtk_widget_init_template (GTK_WIDGET (self));
}

static void
gs_app_context_bar_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
	GsAppContextBar *self = GS_APP_CONTEXT_BAR (object);

	switch ((GsAppContextBarProperty) prop_id) {
	case PROP_APP:
		g_value_set_object (value, gs_app_context_bar_get_app (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_context_bar_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
	GsAppContextBar *self = GS_APP_CONTEXT_BAR (object);

	switch ((GsAppContextBarProperty) prop_id) {
	case PROP_APP:
		gs_app_context_bar_set_app (self, g_value_get_object (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_app_context_bar_dispose (GObject *object)
{
	GsAppContextBar *self = GS_APP_CONTEXT_BAR (object);

	if (self->app_notify_handler != 0) {
		g_signal_handler_disconnect (self->app, self->app_notify_handler);
		self->app_notify_handler = 0;
	}
	g_clear_object (&self->app);

	G_OBJECT_CLASS (gs_app_context_bar_parent_class)->dispose (object);
}

static void
gs_app_context_bar_class_init (GsAppContextBarClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_app_context_bar_get_property;
	object_class->set_property = gs_app_context_bar_set_property;
	object_class->dispose = gs_app_context_bar_dispose;

	/**
	 * GsAppContextBar:app: (nullable)
	 *
	 * The app to display the context details for.
	 *
	 * This may be %NULL; if so, the content of the widget will be
	 * undefined.
	 *
	 * Since: 41
	 */
	obj_props[PROP_APP] =
		g_param_spec_object ("app", NULL, NULL,
				     GS_TYPE_APP,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	gtk_widget_class_set_css_name (widget_class, "app-context-bar");
	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-app-context-bar.ui");

	gtk_widget_class_bind_template_child_full (widget_class, "storage_tile_lozenge", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[STORAGE_TILE].lozenge));
	gtk_widget_class_bind_template_child_full (widget_class, "storage_tile_lozenge_content", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[STORAGE_TILE].lozenge_content));
	gtk_widget_class_bind_template_child_full (widget_class, "storage_tile_title", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[STORAGE_TILE].title));
	gtk_widget_class_bind_template_child_full (widget_class, "storage_tile_description", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[STORAGE_TILE].description));
	gtk_widget_class_bind_template_child_full (widget_class, "safety_tile_lozenge", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[SAFETY_TILE].lozenge));
	gtk_widget_class_bind_template_child_full (widget_class, "safety_tile_lozenge_content", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[SAFETY_TILE].lozenge_content));
	gtk_widget_class_bind_template_child_full (widget_class, "safety_tile_title", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[SAFETY_TILE].title));
	gtk_widget_class_bind_template_child_full (widget_class, "safety_tile_description", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[SAFETY_TILE].description));
	gtk_widget_class_bind_template_child_full (widget_class, "hardware_support_tile_lozenge", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[HARDWARE_SUPPORT_TILE].lozenge));
	gtk_widget_class_bind_template_child_full (widget_class, "hardware_support_tile_lozenge_content", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[HARDWARE_SUPPORT_TILE].lozenge_content));
	gtk_widget_class_bind_template_child_full (widget_class, "hardware_support_tile_title", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[HARDWARE_SUPPORT_TILE].title));
	gtk_widget_class_bind_template_child_full (widget_class, "hardware_support_tile_description", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[HARDWARE_SUPPORT_TILE].description));
	gtk_widget_class_bind_template_child_full (widget_class, "age_rating_tile_lozenge", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[AGE_RATING_TILE].lozenge));
	gtk_widget_class_bind_template_child_full (widget_class, "age_rating_tile_lozenge_content", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[AGE_RATING_TILE].lozenge_content));
	gtk_widget_class_bind_template_child_full (widget_class, "age_rating_tile_title", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[AGE_RATING_TILE].title));
	gtk_widget_class_bind_template_child_full (widget_class, "age_rating_tile_description", FALSE, G_STRUCT_OFFSET (GsAppContextBar, tiles[AGE_RATING_TILE].description));
}

/**
 * gs_app_context_bar_new:
 * @app: (nullable): the app to display context tiles for, or %NULL
 *
 * Create a new #GsAppContextBar and set its initial app to @app.
 *
 * Returns: (transfer full): a new #GsAppContextBar
 * Since: 41
 */
GtkWidget *
gs_app_context_bar_new (GsApp *app)
{
	g_return_val_if_fail (app == NULL || GS_IS_APP (app), NULL);

	return g_object_new (GS_TYPE_APP_CONTEXT_BAR,
			     "app", app,
			     NULL);
}

/**
 * gs_app_context_bar_get_app:
 * @self: a #GsAppContextBar
 *
 * Gets the value of #GsAppContextBar:app.
 *
 * Returns: (nullable) (transfer none): app whose context tiles are being
 *     displayed, or %NULL if none is set
 * Since: 41
 */
GsApp *
gs_app_context_bar_get_app (GsAppContextBar *self)
{
	g_return_val_if_fail (GS_IS_APP_CONTEXT_BAR (self), NULL);

	return self->app;
}

/**
 * gs_app_context_bar_set_app:
 * @self: a #GsAppContextBar
 * @app: (nullable) (transfer none): the app to display context tiles for,
 *     or %NULL for none
 *
 * Set the value of #GsAppContextBar:app.
 *
 * Since: 41
 */
void
gs_app_context_bar_set_app (GsAppContextBar *self,
                            GsApp           *app)
{
	g_return_if_fail (GS_IS_APP_CONTEXT_BAR (self));
	g_return_if_fail (app == NULL || GS_IS_APP (app));

	if (app == self->app)
		return;

	if (self->app_notify_handler != 0) {
		g_signal_handler_disconnect (self->app, self->app_notify_handler);
		self->app_notify_handler = 0;
	}

	g_set_object (&self->app, app);

	if (self->app != NULL)
		self->app_notify_handler = g_signal_connect (self->app, "notify", G_CALLBACK (app_notify_cb), self);

	/* Update the tiles. */
	update_tiles (self);

	g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_APP]);
}
