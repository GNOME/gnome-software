/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include <gio/gio.h>

#include "gs-repo-row.h"
#include "gs-repos-section.h"

struct _GsReposSection
{
	GtkBox		 parent_instance;
	GtkWidget	*title;
	GtkListBox	*list;
	GsPluginLoader	*plugin_loader;
	gchar		*sort_key;
};

G_DEFINE_TYPE (GsReposSection, gs_repos_section, GTK_TYPE_BOX)

enum {
	SIGNAL_REMOVE_CLICKED,
	SIGNAL_SWITCH_CLICKED,
	SIGNAL_LAST
};

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
gs_repos_section_finalize (GObject *object)
{
	GsReposSection *self = GS_REPOS_SECTION (object);

	g_clear_object (&self->plugin_loader);
	g_free (self->sort_key);

	G_OBJECT_CLASS (gs_repos_section_parent_class)->finalize (object);
}

static void
gs_repos_section_class_init (GsReposSectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gs_repos_section_finalize;

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

	gtk_widget_class_set_css_name (GTK_WIDGET_CLASS (klass), "repos-section");
}

static void
gs_repos_section_init (GsReposSection *self)
{
	PangoAttrList *attrs;

	attrs = pango_attr_list_new ();
	pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));

	self->title = gtk_label_new ("");
	g_object_set (G_OBJECT (self->title),
		      "visible", TRUE,
		      "halign", GTK_ALIGN_START,
		      "valign", GTK_ALIGN_CENTER,
		      "xalign", 0.0,
		      "yalign", 0.5,
		      "attributes", attrs,
		      NULL);

	pango_attr_list_unref (attrs);

	gtk_box_pack_start (GTK_BOX (self), self->title, FALSE, FALSE, 0);

	self->list = GTK_LIST_BOX (gtk_list_box_new ());
	g_object_set (G_OBJECT (self->list),
		      "visible", TRUE,
		      "halign", GTK_ALIGN_FILL,
		      "hexpand", TRUE,
		      "valign", GTK_ALIGN_CENTER,
		      "vexpand", TRUE,
		      "margin-top", 4,
		      "margin-bottom", 4,
		      "selection-mode", GTK_SELECTION_NONE,
		      NULL);
	gtk_list_box_set_sort_func (self->list, _list_sort_func, self, NULL);

	gtk_box_pack_start (GTK_BOX (self), GTK_WIDGET (self->list), TRUE, TRUE, 0);

	g_signal_connect (self->list, "row-activated",
			  G_CALLBACK (gs_repos_section_row_activated_cb), self);
}

GtkWidget *
gs_repos_section_new (GsPluginLoader *plugin_loader,
		      const gchar *title)
{
	GsReposSection *self;

	g_return_val_if_fail (GS_IS_PLUGIN_LOADER (plugin_loader), NULL);

	self = g_object_new (GS_TYPE_REPOS_SECTION,
			     "orientation", GTK_ORIENTATION_VERTICAL,
			     "spacing", 8,
			     NULL);

	gtk_label_set_text (GTK_LABEL (self->title), title);
	self->plugin_loader = g_object_ref (plugin_loader);

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

	row = gs_repo_row_new (self->plugin_loader, repo);

	g_signal_connect (row, "remove-clicked",
	                  G_CALLBACK (repo_remove_clicked_cb), self);
	g_signal_connect (row, "switch-clicked",
	                  G_CALLBACK (repo_switch_clicked_cb), self);

	gtk_list_box_prepend (self->list, row);
	gtk_widget_show (row);
}

const gchar *
gs_repos_section_get_title (GsReposSection *self)
{
	g_return_val_if_fail (GS_IS_REPOS_SECTION (self), NULL);

	return gtk_label_get_text (GTK_LABEL (self->title));
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
