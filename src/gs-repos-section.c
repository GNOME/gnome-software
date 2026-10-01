/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <gio/gio.h>

#include "gs-repo-row.h"
#include "gs-repos-section.h"

struct _GsReposSection
{
	GtkBox			 parent_instance;
	AdwPreferencesPage	*page;
	AdwPreferencesGroup	*group;
	GtkListBox		*list;
	GSettings		*settings;
	gchar			*sort_key;
	gchar			*title;
	gchar			*icon_name;
	gboolean		 always_allow_enable_disable;
	gboolean		 related_loaded;
	gboolean		 options_visible;
};

G_DEFINE_TYPE (GsReposSection, gs_repos_section, GTK_TYPE_BOX)

typedef enum {
	PROP_RELATED_LOADED = 1,
	PROP_TITLE,
	PROP_ICON_NAME,
} GsReposSectionProperty;

enum {
	SIGNAL_REMOVE_CLICKED,
	SIGNAL_SWITCH_CLICKED,
	SIGNAL_LAST
};

static GParamSpec *obj_props[PROP_ICON_NAME + 1] = { NULL, };
static guint signals [SIGNAL_LAST] = { 0 };

static void
repo_remove_clicked_cb (GsRepoRow *row,
			GsReposSection *section)
{
	g_signal_emit (section, signals[SIGNAL_REMOVE_CLICKED], 0, row);
}

static void
repo_switch_clicked_cb (GsRepoRow *row,
			GsReposSection *section)
{
	g_signal_emit (section, signals[SIGNAL_SWITCH_CLICKED], 0, row);
}


static void
repo_default_source_clicked_cb (GsRepoRow *row,
				gboolean is_active,
				gpointer user_data)
{
	GsReposSection *section = user_data;
	GsApp *repo;
	const char *packaging_format;

	g_return_if_fail (GS_IS_REPOS_SECTION (section));

	repo = gs_repo_row_get_repo (row);
	packaging_format = gs_app_get_packaging_format_raw (repo);

	if (is_active) {
		char *value[3];

		value[0] = g_strconcat (packaging_format, ":", gs_app_get_id (repo), NULL);
		value[1] = (char *) packaging_format;
		value[2] = NULL;

		g_settings_set_strv (section->settings, "packaging-format-preference", (const char * const *) value);

		g_free (value[0]);
	} else {
		char *value[2];

		value[0] = (char *) packaging_format;
		value[1] = NULL;

		g_settings_set_strv (section->settings, "packaging-format-preference", (const char * const *) value);
	}
}

static void
gs_repos_section_row_activated_cb (GtkListBox *box,
				   GtkListBoxRow *row,
				   gpointer user_data)
{
	GsReposSection *section = user_data;
	g_return_if_fail (GS_IS_REPOS_SECTION (section));
	gs_repo_row_emit_switch_clicked (GS_REPO_ROW (row));
}

static gchar *
_get_app_sort_key (GsApp *app)
{
	if (gs_app_get_name (app) != NULL)
		return gs_utils_sort_key (gs_app_get_name (app));

	return NULL;
}

static gint
_list_sort_func (GtkListBoxRow *a, GtkListBoxRow *b, gpointer user_data)
{
	GsApp *a1 = gs_repo_row_get_repo (GS_REPO_ROW (a));
	GsApp *a2 = gs_repo_row_get_repo (GS_REPO_ROW (b));
	g_autofree gchar *key1 = _get_app_sort_key (a1);
	g_autofree gchar *key2 = _get_app_sort_key (a2);

	return g_strcmp0 (key1, key2);
}

static void
gs_repos_section_get_property (GObject *object,
			       guint prop_id,
			       GValue *value,
			       GParamSpec *pspec)
{
	GsReposSection *self = GS_REPOS_SECTION (object);

	switch ((GsReposSectionProperty) prop_id) {
	case PROP_RELATED_LOADED:
		g_value_set_boolean (value, gs_repos_section_get_related_loaded (self));
		break;
	case PROP_TITLE:
		g_value_set_string (value, gs_repos_section_get_title (self));
		break;
	case PROP_ICON_NAME:
		g_value_set_string (value, gs_repos_section_get_icon_name (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_repos_section_set_property (GObject *object,
			       guint prop_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	GsReposSection *self = GS_REPOS_SECTION (object);

	switch ((GsReposSectionProperty) prop_id) {
	case PROP_RELATED_LOADED:
		gs_repos_section_set_related_loaded (self, g_value_get_boolean (value));
		break;
	case PROP_TITLE:
		gs_repos_section_set_title (self, g_value_get_string (value));
		break;
	case PROP_ICON_NAME:
		gs_repos_section_set_icon_name (self, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_repos_section_finalize (GObject *object)
{
	GsReposSection *self = GS_REPOS_SECTION (object);

	g_clear_object (&self->settings);
	g_free (self->sort_key);
	g_free (self->title);
	g_free (self->icon_name);

	G_OBJECT_CLASS (gs_repos_section_parent_class)->finalize (object);
}

static void
gs_repos_section_class_init (GsReposSectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->get_property = gs_repos_section_get_property;
	object_class->set_property = gs_repos_section_set_property;
	object_class->finalize = gs_repos_section_finalize;

	/**
	 * GsReposSection:related-loaded:
	 *
	 * Whether the related apps for this repo section have been
	 * successfully loaded. If so, the number of apps/installed
	 * apps is shown in each row.
	 *
	 * Since: 45
	 */
	obj_props[PROP_RELATED_LOADED] =
		g_param_spec_boolean ("related-loaded", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsReposSection:title:
	 *
	 * Localized title of the section.
	 *
	 * Since: 52
	 */
	obj_props[PROP_TITLE] =
		g_param_spec_string ("title", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	/**
	 * GsReposSection:icon-name:
	 *
	 * Icon name of the section.
	 *
	 * Since: 52
	 */
	obj_props[PROP_ICON_NAME] =
		g_param_spec_string ("icon-name", NULL, NULL,
				     NULL,
				     G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	signals [SIGNAL_REMOVE_CLICKED] =
		g_signal_new ("remove-clicked",
		              G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
		              G_TYPE_NONE, 1, GS_TYPE_REPO_ROW);

	signals [SIGNAL_SWITCH_CLICKED] =
		g_signal_new ("switch-clicked",
		              G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL, g_cclosure_marshal_VOID__OBJECT,
		              G_TYPE_NONE, 1, GS_TYPE_REPO_ROW);
}

static void
gs_repos_section_init (GsReposSection *self)
{
	self->settings = g_settings_new ("org.gnome.software");
	self->list = GTK_LIST_BOX (gtk_list_box_new ());
	g_object_set (G_OBJECT (self->list),
		      "visible", TRUE,
		      "selection-mode", GTK_SELECTION_NONE,
		      NULL);
	gtk_list_box_set_sort_func (self->list, _list_sort_func, self, NULL);

	gtk_widget_add_css_class (GTK_WIDGET (self->list), "boxed-list");

	self->page = ADW_PREFERENCES_PAGE (adw_preferences_page_new ());
	gtk_box_append (GTK_BOX (self), GTK_WIDGET (self->page));

	self->group = ADW_PREFERENCES_GROUP (adw_preferences_group_new ());
	adw_preferences_group_add (self->group, GTK_WIDGET (self->list));

	adw_preferences_page_add (self->page, self->group);

	g_signal_connect (self->list, "row-activated",
			  G_CALLBACK (gs_repos_section_row_activated_cb), self);
}

/*
 * gs_repos_section_new:
 * @always_allow_enable_disable: always allow enable/disable of the repos in this section
 * @options_visible: whether to show "Options" button for the repo rows
 *
 * Creates a new #GsReposSection. @always_allow_enable_disable is passed to each
 * #GsRepoRow.
 *
 * The @always_allow_enable_disable, when %TRUE, means that every repo in this section
 * can be enabled/disabled by the user, if supported by the related plugin, regardless
 * of the other heuristics, which can avoid the repo enable/disable.
 *
 * Returns: (transfer full): a newly created #GsReposSection
 */
GtkWidget *
gs_repos_section_new (gboolean always_allow_enable_disable,
		      gboolean options_visible)
{
	GsReposSection *self;

	self = g_object_new (GS_TYPE_REPOS_SECTION,
			     "orientation", GTK_ORIENTATION_VERTICAL,
			     "homogeneous", FALSE,
			     NULL);

	self->always_allow_enable_disable = always_allow_enable_disable;
	self->options_visible = options_visible;

	return GTK_WIDGET (self);
}

void
gs_repos_section_add_repo (GsReposSection *self,
			   GsApp *repo)
{
	GtkWidget *row;

	g_return_if_fail (GS_IS_REPOS_SECTION (self));
	g_return_if_fail (GS_IS_APP (repo));

	/* Derive the sort key from the repository. All repositories of the same kind
	   should have set the same sort key. It's because there's no other way to provide
	   the section sort key by the plugin without breaking the abstraction. */
	if (!self->sort_key)
		self->sort_key = g_strdup (gs_app_get_metadata_item (repo, "GnomeSoftware::SortKey"));

	row = gs_repo_row_new (repo, self->always_allow_enable_disable, self->options_visible);
	g_object_bind_property (self, "related-loaded",
				row, "related-loaded",
				G_BINDING_SYNC_CREATE);
	g_signal_connect (row, "remove-clicked",
	                  G_CALLBACK (repo_remove_clicked_cb), self);
	g_signal_connect (row, "switch-clicked",
	                  G_CALLBACK (repo_switch_clicked_cb), self);
	g_signal_connect (row, "default-source-clicked",
	                  G_CALLBACK (repo_default_source_clicked_cb), self);
	g_settings_bind (self->settings, "packaging-format-preference",
			 row, "packaging-format-preference",
			 G_SETTINGS_BIND_GET | G_SETTINGS_BIND_NO_SENSITIVITY);

	gtk_list_box_prepend (self->list, row);
	gtk_widget_set_visible (row, TRUE);
}

AdwPreferencesPage *
gs_repos_section_get_prefs_page (GsReposSection *self)
{
	g_return_val_if_fail (GS_IS_REPOS_SECTION (self), NULL);

	return self->page;
}

const gchar *
gs_repos_section_get_title (GsReposSection *self)
{
	g_return_val_if_fail (GS_IS_REPOS_SECTION (self), NULL);

	return self->title;
}

void
gs_repos_section_set_title (GsReposSection *self,
			    const gchar *value)
{
	g_return_if_fail (GS_IS_REPOS_SECTION (self));

	if (g_strcmp0 (self->title, value) == 0)
		return;

	g_free (self->title);
	self->title = g_strdup (value);

	g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_TITLE]);
}

const gchar *
gs_repos_section_get_icon_name (GsReposSection *self)
{
	g_return_val_if_fail (GS_IS_REPOS_SECTION (self), NULL);

	if (self->icon_name == NULL)
		return "package-generic-symbolic";

	return self->icon_name;
}

void
gs_repos_section_set_icon_name (GsReposSection *self,
				const gchar *value)
{
	g_return_if_fail (GS_IS_REPOS_SECTION (self));

	if (g_strcmp0 (self->icon_name, value) == 0)
		return;

	g_free (self->icon_name);
	self->icon_name = g_strdup (value);

	g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_ICON_NAME]);
}

const gchar *
gs_repos_section_get_sort_key (GsReposSection *self)
{
	g_return_val_if_fail (GS_IS_REPOS_SECTION (self), NULL);

	return self->sort_key;
}

void
gs_repos_section_set_sort_key (GsReposSection *self,
			       const gchar *sort_key)
{
	g_return_if_fail (GS_IS_REPOS_SECTION (self));

	if (g_strcmp0 (sort_key, self->sort_key) != 0) {
		g_free (self->sort_key);
		self->sort_key = g_strdup (sort_key);
	}
}

gboolean
gs_repos_section_get_related_loaded (GsReposSection *self)
{
	g_return_val_if_fail (GS_IS_REPOS_SECTION (self), FALSE);

	return self->related_loaded;
}

void
gs_repos_section_set_related_loaded (GsReposSection *self,
				     gboolean value)
{
	g_return_if_fail (GS_IS_REPOS_SECTION (self));

	if (!self->related_loaded == !value)
		return;

	self->related_loaded = value;

	g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_RELATED_LOADED]);
}
