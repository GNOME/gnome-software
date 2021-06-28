/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2013-2015 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2016-2019 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <glib/gi18n.h>
#include <gio/gdesktopappinfo.h>

#include "gs-common.h"

#ifdef HAVE_GSETTINGS_DESKTOP_SCHEMAS
#include <gdesktop-enums.h>
#endif

#include <langinfo.h>

#define SPINNER_DELAY 500

static gboolean
fade_in (gpointer data)
{
	GtkWidget *spinner = data;
	gdouble opacity;

	opacity = gtk_widget_get_opacity (spinner);
	opacity = opacity + 0.1;
	gtk_widget_set_opacity (spinner, opacity);

	if (opacity >= 1.0) {
		g_object_steal_data (G_OBJECT (spinner), "fade-timeout");
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static void
remove_source (gpointer data)
{
	g_source_remove (GPOINTER_TO_UINT (data));
}

static gboolean
start_spinning (gpointer data)
{
	GtkWidget *spinner = data;
	guint id;

	gtk_widget_set_opacity (spinner, 0);
	gtk_spinner_start (GTK_SPINNER (spinner));
	id = g_timeout_add (100, fade_in, spinner);
	g_object_set_data_full (G_OBJECT (spinner), "fade-timeout",
				GUINT_TO_POINTER (id), remove_source);

	/* don't try to remove this source in the future */
	g_object_steal_data (G_OBJECT (spinner), "start-timeout");
	return G_SOURCE_REMOVE;
}

void
gs_stop_spinner (GtkSpinner *spinner)
{
	g_object_set_data (G_OBJECT (spinner), "start-timeout", NULL);
	gtk_spinner_stop (spinner);
}

void
gs_start_spinner (GtkSpinner *spinner)
{
	gboolean active;
	guint id;

	/* Don't do anything if it's already spinning */
	g_object_get (spinner, "active", &active, NULL);
	if (active || g_object_get_data (G_OBJECT (spinner), "start-timeout") != NULL)
		return;

	gtk_widget_set_opacity (GTK_WIDGET (spinner), 0);
	id = g_timeout_add (SPINNER_DELAY, start_spinning, spinner);
	g_object_set_data_full (G_OBJECT (spinner), "start-timeout",
				GUINT_TO_POINTER (id), remove_source);
}

static void
remove_all_cb (GtkWidget *widget, gpointer user_data)
{
	GtkContainer *container = GTK_CONTAINER (user_data);
	gtk_container_remove (container, widget);
}

void
gs_container_remove_all (GtkContainer *container)
{
	gtk_container_foreach (container, remove_all_cb, container);
}

static void
grab_focus (GtkWidget *widget)
{
	g_signal_handlers_disconnect_by_func (widget, grab_focus, NULL);
	gtk_widget_grab_focus (widget);
}

void
gs_grab_focus_when_mapped (GtkWidget *widget)
{
	if (gtk_widget_get_mapped (widget))
		gtk_widget_grab_focus (widget);
	else
		g_signal_connect_after (widget, "map",
					G_CALLBACK (grab_focus), NULL);
}

void
gs_app_notify_installed (GsApp *app)
{
	g_autofree gchar *summary = NULL;
	const gchar *body = NULL;
	g_autoptr(GNotification) n = NULL;

	switch (gs_app_get_kind (app)) {
	case AS_COMPONENT_KIND_DESKTOP_APP:
		/* TRANSLATORS: this is the summary of a notification that an application
		 * has been successfully installed */
		summary = g_strdup_printf (_("%s is now installed"), gs_app_get_name (app));
		if (gs_app_has_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT)) {
			/* TRANSLATORS: an application has been installed, but
			 * needs a reboot to complete the installation */
			body = _("A restart is required for the changes to take effect.");
		} else {
			/* TRANSLATORS: this is the body of a notification that an application
			 * has been successfully installed */
			body = _("Application is ready to be used.");
		}
		break;
	default:
		if (gs_app_get_kind (app) == AS_COMPONENT_KIND_GENERIC &&
		    gs_app_get_special_kind (app) == GS_APP_SPECIAL_KIND_OS_UPDATE) {
			/* TRANSLATORS: this is the summary of a notification that OS updates
			* have been successfully installed */
			summary = g_strdup (_("OS updates are now installed"));
			/* TRANSLATORS: this is the body of a notification that OS updates
			* have been successfully installed */
			body = _("Recently installed updates are available to review");
		} else {
			/* TRANSLATORS: this is the summary of a notification that a component
			* has been successfully installed */
			summary = g_strdup_printf (_("%s is now installed"), gs_app_get_name (app));
			if (gs_app_has_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT)) {
				/* TRANSLATORS: an application has been installed, but
				* needs a reboot to complete the installation */
				body = _("A restart is required for the changes to take effect.");
			}
		}
		break;
	}
	n = g_notification_new (summary);
	if (body != NULL)
		g_notification_set_body (n, body);

	if (gs_app_has_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT)) {
		/* TRANSLATORS: button text */
		g_notification_add_button_with_target (n, _("Restart"),
						       "app.reboot", NULL);
	} else if (gs_app_get_kind (app) == AS_COMPONENT_KIND_DESKTOP_APP) {
		/* TRANSLATORS: this is button that opens the newly installed application */
		g_notification_add_button_with_target (n, _("Launch"),
						       "app.launch", "(ss)",
						       gs_app_get_id (app),
						       gs_app_get_management_plugin (app));
	}
	g_notification_set_default_action_and_target  (n, "app.details", "(ss)",
						       gs_app_get_unique_id (app), "");
	g_application_send_notification (g_application_get_default (), "installed", n);
}

typedef enum {
	GS_APP_LICENSE_FREE		= 0,
	GS_APP_LICENSE_NONFREE		= 1,
	GS_APP_LICENSE_PATENT_CONCERN	= 2
} GsAppLicenseHint;

GtkResponseType
gs_app_notify_unavailable (GsApp *app, GtkWindow *parent)
{
	GsAppLicenseHint hint = GS_APP_LICENSE_FREE;
	GtkResponseType response;
	GtkWidget *dialog;
	const gchar *license;
	gboolean already_enabled = FALSE;	/* FIXME */
	guint i;
	struct {
		const gchar	*str;
		GsAppLicenseHint hint;
	} keywords[] = {
		{ "NonFree",		GS_APP_LICENSE_NONFREE },
		{ "PatentConcern",	GS_APP_LICENSE_PATENT_CONCERN },
		{ "Proprietary",	GS_APP_LICENSE_NONFREE },
		{ NULL, 0 }
	};
	g_autoptr(GSettings) settings = NULL;
	g_autoptr(GString) body = NULL;
	g_autoptr(GString) title = NULL;

	/* this is very crude */
	license = gs_app_get_license (app);
	if (license != NULL) {
		for (i = 0; keywords[i].str != NULL; i++) {
			if (g_strstr_len (license, -1, keywords[i].str) != NULL)
				hint |= keywords[i].hint;
		}
	} else {
		/* use the worst-case assumption */
		hint = GS_APP_LICENSE_NONFREE | GS_APP_LICENSE_PATENT_CONCERN;
	}

	/* check if the user has already dismissed */
	settings = g_settings_new ("org.gnome.software");
	if (!g_settings_get_boolean (settings, "prompt-for-nonfree"))
		return GTK_RESPONSE_OK;

	title = g_string_new ("");
	if (already_enabled) {
		g_string_append_printf (title, "<b>%s</b>",
					/* TRANSLATORS: window title */
					_("Install Third-Party Software?"));
	} else {
		g_string_append_printf (title, "<b>%s</b>",
					/* TRANSLATORS: window title */
					_("Enable Third-Party Software Repository?"));
	}
	dialog = gtk_message_dialog_new (parent,
					 GTK_DIALOG_MODAL,
					 GTK_MESSAGE_QUESTION,
					 GTK_BUTTONS_CANCEL,
					 NULL);
	gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), title->str);

	body = g_string_new ("");
	if (hint & GS_APP_LICENSE_NONFREE) {
		g_string_append_printf (body,
					/* TRANSLATORS: the replacements are as follows:
					 * 1. Application name, e.g. "Firefox"
					 * 2. Software repository name, e.g. fedora-optional
					 */
					_("%s is not <a href=\"https://en.wikipedia.org/wiki/Free_and_open-source_software\">"
					  "free and open source software</a>, "
					  "and is provided by “%s”."),
					gs_app_get_name (app),
					gs_app_get_origin (app));
	} else {
		g_string_append_printf (body,
					/* TRANSLATORS: the replacements are as follows:
					 * 1. Application name, e.g. "Firefox"
					 * 2. Software repository name, e.g. fedora-optional */
					_("%s is provided by “%s”."),
					gs_app_get_name (app),
					gs_app_get_origin (app));
	}

	/* tell the use what needs to be done */
	if (!already_enabled) {
		g_string_append (body, " ");
		g_string_append (body,
				_("This software repository must be "
				  "enabled to continue installation."));
	}

	/* be aware of patent clauses */
	if (hint & GS_APP_LICENSE_PATENT_CONCERN) {
		g_string_append (body, "\n\n");
		if (gs_app_get_kind (app) != AS_COMPONENT_KIND_CODEC) {
			g_string_append_printf (body,
						/* TRANSLATORS: Laws are geographical, urgh... */
						_("It may be illegal to install "
						  "or use %s in some countries."),
						gs_app_get_name (app));
		} else {
			g_string_append (body,
					/* TRANSLATORS: Laws are geographical, urgh... */
					_("It may be illegal to install or use "
					  "this codec in some countries."));
		}
	}

	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", body->str);
	/* TRANSLATORS: this is button text to not ask about non-free content again */
	if (0) gtk_dialog_add_button (GTK_DIALOG (dialog), _("Don’t Warn Again"), GTK_RESPONSE_YES);
	if (already_enabled) {
		gtk_dialog_add_button (GTK_DIALOG (dialog),
				       /* TRANSLATORS: button text */
				       _("Install"),
				       GTK_RESPONSE_OK);
	} else {
		gtk_dialog_add_button (GTK_DIALOG (dialog),
				       /* TRANSLATORS: button text */
				       _("Enable and Install"),
				       GTK_RESPONSE_OK);
	}
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	if (response == GTK_RESPONSE_YES) {
		response = GTK_RESPONSE_OK;
		g_settings_set_boolean (settings, "prompt-for-nonfree", FALSE);
	}
	gtk_widget_destroy (dialog);
	return response;
}

void
gs_image_set_from_pixbuf_with_scale (GtkImage *image, const GdkPixbuf *pixbuf, gint scale)
{
	cairo_surface_t *surface;
	surface = gdk_cairo_surface_create_from_pixbuf (pixbuf, scale, NULL);
	if (surface == NULL)
		return;
	gtk_image_set_from_surface (image, surface);
	cairo_surface_destroy (surface);
}

gboolean
gs_utils_is_current_desktop (const gchar *name)
{
	const gchar *tmp;
	g_auto(GStrv) names = NULL;
	tmp = g_getenv ("XDG_CURRENT_DESKTOP");
	if (tmp == NULL)
		return FALSE;
	names = g_strsplit (tmp, ":", -1);
	return g_strv_contains ((const gchar * const *) names, name);
}

static void
gs_utils_widget_css_parsing_error_cb (GtkCssProvider *provider,
				      GtkCssSection *section,
				      GError *error,
				      gpointer user_data)
{
	g_warning ("CSS parse error %u:%u: %s",
		   gtk_css_section_get_start_line (section),
		   gtk_css_section_get_start_position (section),
		   error->message);
}

/**
 * gs_utils_set_key_colors_in_css:
 * @css: some CSS
 * @app: a #GsApp to get the key colors from
 *
 * Replace placeholders in @css with the key colors from @app, returning a copy
 * of the CSS with the key colors inlined as `rgb()` literals.
 *
 * The key color placeholders are of the form `@keycolor-XX@`, where `XX` is a
 * two digit counter. The first counter (`00`) will be replaced with the first
 * key color in @app, the second counter (`01`) with the second, etc.
 *
 * CSS may be %NULL, in which case %NULL is returned.
 *
 * Returns: (transfer full): a copy of @css with the key color placeholders
 *     replaced, free with g_free()
 * Since: 40
 */
gchar *
gs_utils_set_key_colors_in_css (const gchar *css,
                                GsApp       *app)
{
	GArray *key_colors;
	g_autoptr(GString) css_new = NULL;

	if (css == NULL)
		return NULL;

	key_colors = gs_app_get_key_colors (app);

	/* Do we not need to do any replacements? */
	if (key_colors->len == 0 ||
	    g_strstr_len (css, -1, "@keycolor") == NULL)
		return g_strdup (css);

	/* replace key color values */
	css_new = g_string_new (css);
	for (guint j = 0; j < key_colors->len; j++) {
		const GdkRGBA *color = &g_array_index (key_colors, GdkRGBA, j);
		g_autofree gchar *key = NULL;
		g_autofree gchar *value = NULL;
		key = g_strdup_printf ("@keycolor-%02u@", j);
		value = g_strdup_printf ("rgb(%.0f,%.0f,%.0f)",
					 color->red * 255.f,
					 color->green * 255.f,
					 color->blue * 255.f);
		as_gstring_replace (css_new, key, value);
	}

	return g_string_free (g_steal_pointer (&css_new), FALSE);
}

/**
 * gs_utils_widget_set_css:
 * @widget: a widget
 * @provider: (inout) (transfer full) (not optional) (nullable): pointer to a
 *    #GtkCssProvider to use
 * @class_name: class name to use, without the leading `.`
 * @css: (nullable): CSS to set on the widget, or %NULL to clear custom CSS
 *
 * Set custom CSS on the given @widget instance. This doesn’t affect any other
 * instances of the same widget. The @class_name must be a static string to be
 * used as a name for the @css. It doesn’t need to vary with @widget, but
 * multiple values of @class_name can be used with the same @widget to control
 * several independent snippets of custom CSS.
 *
 * @provider must be a pointer to a #GtkCssProvider pointer, typically within
 * your widget’s private data struct. This function will return a
 * #GtkCssProvider in the provided pointer, reusing any old @provider if
 * possible. When your widget is destroyed, you must destroy the returned
 * @provider. If @css is %NULL, this function will destroy the @provider.
 */
void
gs_utils_widget_set_css (GtkWidget *widget, GtkCssProvider **provider, const gchar *class_name, const gchar *css)
{
	GtkStyleContext *context;
	g_autoptr(GString) str = NULL;

	g_return_if_fail (GTK_IS_WIDGET (widget));
	g_return_if_fail (provider != NULL);
	g_return_if_fail (provider == NULL || *provider == NULL || GTK_IS_STYLE_PROVIDER (*provider));
	g_return_if_fail (class_name != NULL);

	context = gtk_widget_get_style_context (widget);

	/* remove custom class if NULL */
	if (css == NULL) {
		if (*provider != NULL)
			gtk_style_context_remove_provider (context, GTK_STYLE_PROVIDER (*provider));
		g_clear_object (provider);
		gtk_style_context_remove_class (context, class_name);
		return;
	}

	str = g_string_sized_new (1024);
	g_string_append_printf (str, ".%s {\n", class_name);
	g_string_append_printf (str, "%s\n", css);
	g_string_append (str, "}");

	/* create a new provider if needed */
	if (*provider == NULL) {
		*provider = gtk_css_provider_new ();
		g_signal_connect (*provider, "parsing-error",
				  G_CALLBACK (gs_utils_widget_css_parsing_error_cb), NULL);
	}

	/* set the custom CSS class */
	gtk_style_context_add_class (context, class_name);

	/* set up custom provider and store on the widget */
	gtk_css_provider_load_from_data (*provider, str->str, -1, NULL);
	gtk_style_context_add_provider (context, GTK_STYLE_PROVIDER (*provider),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static void
do_not_expand (GtkWidget *child, gpointer data)
{
	gtk_container_child_set (GTK_CONTAINER (gtk_widget_get_parent (child)),
				 child, "expand", FALSE, "fill", FALSE, NULL);
}

static gboolean
unset_focus (GtkWidget *widget, GdkEvent *event, gpointer data)
{
	if (GTK_IS_WINDOW (widget))
		gtk_window_set_focus (GTK_WINDOW (widget), NULL);
	return FALSE;
}

/**
 * insert_details_widget:
 * @dialog: the message dialog where the widget will be inserted
 * @details: the detailed message text to display
 *
 * Inserts a widget displaying the detailed message into the message dialog.
 */
static void
insert_details_widget (GtkMessageDialog *dialog, const gchar *details)
{
	GtkWidget *message_area, *sw, *label;
	GtkWidget *box, *tv;
	GtkTextBuffer *buffer;
	GList *children;
	g_autoptr(GString) msg = NULL;

	g_assert (GTK_IS_MESSAGE_DIALOG (dialog));
	g_assert (details != NULL);

	gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);

	msg = g_string_new ("");
	g_string_append_printf (msg, "%s\n\n%s",
	                        /* TRANSLATORS: these are show_detailed_error messages from the
	                         * package manager no mortal is supposed to understand,
	                         * but google might know what they mean */
	                        _("Detailed errors from the package manager follow:"),
	                        details);

	message_area = gtk_message_dialog_get_message_area (dialog);
	g_assert (GTK_IS_BOX (message_area));
	/* make the hbox expand */
	box = gtk_widget_get_parent (message_area);
	gtk_container_child_set (GTK_CONTAINER (gtk_widget_get_parent (box)), box,
	                         "expand", TRUE, "fill", TRUE, NULL);
	/* make the labels not expand */
	gtk_container_foreach (GTK_CONTAINER (message_area), do_not_expand, NULL);

	/* Find the secondary label and set its width_chars.   */
	/* Otherwise the label will tend to expand vertically. */
	children = gtk_container_get_children (GTK_CONTAINER (message_area));
	if (children && children->next && GTK_IS_LABEL (children->next->data)) {
		gtk_label_set_width_chars (GTK_LABEL (children->next->data), 40);
	}

	label = gtk_label_new (_("Details"));
	gtk_widget_set_halign (label, GTK_ALIGN_START);
	gtk_widget_set_visible (label, TRUE);
	gtk_container_add (GTK_CONTAINER (message_area), label);

	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
	                                     GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
	                                GTK_POLICY_NEVER,
	                                GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_min_content_height (GTK_SCROLLED_WINDOW (sw), 150);
	gtk_widget_set_visible (sw, TRUE);

	tv = gtk_text_view_new ();
	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (tv));
	gtk_text_view_set_editable (GTK_TEXT_VIEW (tv), FALSE);
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (tv), GTK_WRAP_WORD);
	gtk_style_context_add_class (gtk_widget_get_style_context (tv),
	                             "update-failed-details");
	gtk_text_buffer_set_text (buffer, msg->str, -1);
	gtk_widget_set_visible (tv, TRUE);

	gtk_container_add (GTK_CONTAINER (sw), tv);
	gtk_widget_set_vexpand (sw, TRUE);
	gtk_container_add (GTK_CONTAINER (message_area), sw);
	gtk_container_child_set (GTK_CONTAINER (message_area), sw, "pack-type", GTK_PACK_END, NULL);

	g_signal_connect (dialog, "map-event", G_CALLBACK (unset_focus), NULL);
}

/**
 * gs_utils_show_error_dialog:
 * @parent: transient parent, or NULL for none
 * @title: the title for the dialog
 * @msg: the message for the dialog
 * @details: (allow-none): the detailed error message, or NULL for none
 *
 * Shows a message dialog for displaying error messages.
 */
void
gs_utils_show_error_dialog (GtkWindow *parent,
                            const gchar *title,
                            const gchar *msg,
                            const gchar *details)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new_with_markup (parent,
	                                             0,
	                                             GTK_MESSAGE_INFO,
	                                             GTK_BUTTONS_CLOSE,
	                                             "<big><b>%s</b></big>", title);
	gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
	                                          "%s", msg);
	if (details != NULL)
		insert_details_widget (GTK_MESSAGE_DIALOG (dialog), details);

	g_signal_connect_swapped (dialog, "response",
	                          G_CALLBACK (gtk_widget_destroy),
	                          dialog);
	gtk_widget_show (dialog);
}

/**
 * gs_utils_get_error_value:
 * @error: A GError
 *
 * Gets the machine-readable value stored in the error message.
 * The machine readable string is after the first "@", e.g.
 * message = "Requires authentication with @aaa"
 *
 * Returns: a string, or %NULL
 */
const gchar *
gs_utils_get_error_value (const GError *error)
{
	gchar *str;
	if (error == NULL)
		return NULL;
	str = g_strstr_len (error->message, -1, "@");
	if (str == NULL)
		return NULL;
	return (const gchar *) str + 1;
}

/**
 * gs_utils_build_unique_id_kind:
 * @kind: A #AsComponentKind
 * @id: An application ID
 *
 * Converts the ID valid into a wildcard unique ID of a specific kind.
 * If @id is already a unique ID, then it is returned unchanged.
 *
 * Returns: (transfer full): a unique ID, or %NULL
 */
gchar *
gs_utils_build_unique_id_kind (AsComponentKind kind, const gchar *id)
{
	if (as_utils_data_id_valid (id))
		return g_strdup (id);
	return gs_utils_build_unique_id (AS_COMPONENT_SCOPE_UNKNOWN,
					 AS_BUNDLE_KIND_UNKNOWN,
					 NULL,
					 id,
					 NULL);
}

/**
 * gs_utils_list_has_component_fuzzy:
 * @list: A #GsAppList
 * @app: A #GsApp
 *
 * Finds out if any application in the list would match a given application,
 * where the match is valid for a matching D-Bus bus name,
 * the label in the UI or the same icon.
 *
 * This function is normally used to work out if the source should be shown
 * in a GsAppRow.
 *
 * Returns: %TRUE if the app is visually the "same"
 */
gboolean
gs_utils_list_has_component_fuzzy (GsAppList *list, GsApp *app)
{
	guint i;
	GsApp *tmp;

	for (i = 0; i < gs_app_list_length (list); i++) {
		tmp = gs_app_list_index (list, i);

		/* ignore if the same object */
		if (app == tmp)
			continue;

		/* ignore with the same source */
		if (g_strcmp0 (gs_app_get_origin_hostname (tmp),
			       gs_app_get_origin_hostname (app)) == 0) {
			continue;
		}

		/* same D-Bus ID */
		if (g_strcmp0 (gs_app_get_id (tmp),
			       gs_app_get_id (app)) == 0) {
			return TRUE;
		}

		/* same name */
		if (g_strcmp0 (gs_app_get_name (tmp),
			       gs_app_get_name (app)) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

void
gs_utils_reboot_notify (GsAppList *list,
			gboolean is_install)
{
	g_autoptr(GNotification) n = NULL;
	g_autofree gchar *tmp = NULL;
	const gchar *app_name = NULL;
	const gchar *title;
	const gchar *body;

	if (gs_app_list_length (list) == 1) {
		GsApp *app = gs_app_list_index (list, 0);
		if (gs_app_get_kind (app) == AS_COMPONENT_KIND_DESKTOP_APP) {
			app_name = gs_app_get_name (app);
			if (!*app_name)
				app_name = NULL;
		}
	}

	if (is_install) {
		if (app_name) {
			/* TRANSLATORS: The '%s' is replaced with the application name */
			tmp = g_strdup_printf ("An application “%s” has been installed", app_name);
			title = tmp;
		} else {
			/* TRANSLATORS: we've just live-updated some apps */
			title = ngettext ("An update has been installed",
					  "Updates have been installed",
					  gs_app_list_length (list));
		}
	} else if (app_name) {
		/* TRANSLATORS: The '%s' is replaced with the application name */
		tmp = g_strdup_printf ("An application “%s” has been removed", app_name);
		title = tmp;
	} else {
		/* TRANSLATORS: we've just removed some apps */
		title = ngettext ("An application has been removed",
				  "Applications have been removed",
				  gs_app_list_length (list));
	}

	/* TRANSLATORS: the new apps will not be run until we restart */
	body = ngettext ("A restart is required for it to take effect.",
	                 "A restart is required for them to take effect.",
	                 gs_app_list_length (list));

	n = g_notification_new (title);
	g_notification_set_body (n, body);
	/* TRANSLATORS: button text */
	g_notification_add_button (n, _("Not Now"), "app.nop");
	/* TRANSLATORS: button text */
	g_notification_add_button_with_target (n, _("Restart"), "app.reboot", NULL);
	g_notification_set_default_action_and_target (n, "app.set-mode", "s", "updates");
	g_notification_set_priority (n, G_NOTIFICATION_PRIORITY_URGENT);
	g_application_send_notification (g_application_get_default (), "restart-required", n);
}

/**
 * gs_utils_split_time_difference:
 * @unix_time_seconds: Time since the epoch in seconds
 * @out_minutes_ago: (out) (nullable): how many minutes elapsed
 * @out_hours_ago: (out) (nullable): how many hours elapsed
 * @out_days_ago: (out) (nullable): how many days elapsed
 * @out_weeks_ago: (out) (nullable): how many weeks elapsed
 * @out_months_ago: (out) (nullable): how many months elapsed
 * @out_years_ago: (out) (nullable): how many years elapsed
 *
 * Calculates the difference between the @unix_time_seconds and the current time
 * and splits it into separate values.
 *
 * Returns: whether the out parameters had been set
 *
 * Since: 41
 **/
gboolean
gs_utils_split_time_difference (gint64 unix_time_seconds,
				gint *out_minutes_ago,
				gint *out_hours_ago,
				gint *out_days_ago,
				gint *out_weeks_ago,
				gint *out_months_ago,
				gint *out_years_ago)
{
	gint minutes_ago, hours_ago, days_ago;
	gint weeks_ago, months_ago, years_ago;
	g_autoptr(GDateTime) date_time = NULL;
	g_autoptr(GDateTime) now = NULL;
	GTimeSpan timespan;

	if (unix_time_seconds <= 0)
		return FALSE;

	date_time = g_date_time_new_from_unix_local (unix_time_seconds);
	now = g_date_time_new_now_local ();
	timespan = g_date_time_difference (now, date_time);

	minutes_ago = (gint) (timespan / G_TIME_SPAN_MINUTE);
	hours_ago = (gint) (timespan / G_TIME_SPAN_HOUR);
	days_ago = (gint) (timespan / G_TIME_SPAN_DAY);
	weeks_ago = days_ago / 7;
	months_ago = days_ago / 30;
	years_ago = weeks_ago / 52;

	if (out_minutes_ago)
		*out_minutes_ago = minutes_ago;
	if (out_hours_ago)
		*out_hours_ago = hours_ago;
	if (out_days_ago)
		*out_days_ago = days_ago;
	if (out_weeks_ago)
		*out_weeks_ago = weeks_ago;
	if (out_months_ago)
		*out_months_ago = months_ago;
	if (out_years_ago)
		*out_years_ago = years_ago;

	return TRUE;
}

/**
 * gs_utils_time_to_string:
 * @unix_time_seconds: Time since the epoch in seconds
 *
 * Converts a time to a string such as "5 minutes ago" or "2 weeks ago"
 *
 * Returns: (transfer full): the time string, or %NULL if @unix_time_seconds is
 *   not valid
 */
gchar *
gs_utils_time_to_string (gint64 unix_time_seconds)
{
	gint minutes_ago, hours_ago, days_ago;
	gint weeks_ago, months_ago, years_ago;

	if (!gs_utils_split_time_difference (unix_time_seconds,
		&minutes_ago, &hours_ago, &days_ago,
		&weeks_ago, &months_ago, &years_ago))
		return NULL;

	if (minutes_ago < 5) {
		/* TRANSLATORS: something happened less than 5 minutes ago */
		return g_strdup (_("Just now"));
	} else if (hours_ago < 1)
		return g_strdup_printf (ngettext ("%d minute ago",
						  "%d minutes ago", minutes_ago),
					minutes_ago);
	else if (days_ago < 1)
		return g_strdup_printf (ngettext ("%d hour ago",
						  "%d hours ago", hours_ago),
					hours_ago);
	else if (days_ago < 15)
		return g_strdup_printf (ngettext ("%d day ago",
						  "%d days ago", days_ago),
					days_ago);
	else if (weeks_ago < 8)
		return g_strdup_printf (ngettext ("%d week ago",
						  "%d weeks ago", weeks_ago),
					weeks_ago);
	else if (years_ago < 1)
		return g_strdup_printf (ngettext ("%d month ago",
						  "%d months ago", months_ago),
					months_ago);
	else
		return g_strdup_printf (ngettext ("%d year ago",
						  "%d years ago", years_ago),
					years_ago);
}

static void
gs_utils_reboot_call_done_cb (GObject *source,
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

/**
 * gs_utils_invoke_reboot_async:
 * @cancellable: (nullable): a %GCancellable for the call, or %NULL
 * @ready_callback: (nullable): a callback to be called after the call is finished, or %NULL
 * @user_data: user data for the @ready_callback
 *
 * Asynchronously invokes a reboot request using D-Bus. The @ready_callback should
 * use g_dbus_connection_call_finish (G_DBUS_CONNECTION (source), result, &error);
 * to get the result of the operation.
 *
 * When the @ready_callback is %NULL, a default callback is used, which shows
 * a runtime warning (using g_warning) on the console when the call fails.
 *
 * Since: 41
 **/
void
gs_utils_invoke_reboot_async (GCancellable *cancellable,
			      GAsyncReadyCallback ready_callback,
			      gpointer user_data)
{
	g_autoptr(GDBusConnection) bus = NULL;
	bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, NULL);

	if (!ready_callback)
		ready_callback = gs_utils_reboot_call_done_cb;

	g_dbus_connection_call (bus,
				"org.gnome.SessionManager",
				"/org/gnome/SessionManager",
				"org.gnome.SessionManager",
				"Reboot",
				NULL, NULL, G_DBUS_CALL_FLAGS_NONE,
				G_MAXINT, cancellable,
				ready_callback,
				user_data);
}
