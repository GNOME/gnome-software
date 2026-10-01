/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>

#include "gnome-software-private.h"
#include "gs-app.h"

G_BEGIN_DECLS

#define GS_TYPE_REPOS_SECTION (gs_repos_section_get_type ())

G_DECLARE_FINAL_TYPE (GsReposSection, gs_repos_section, GS, REPOS_SECTION, GtkBox)

GtkWidget	*gs_repos_section_new			(gboolean		 always_allow_enable_disable,
							 gboolean		 options_visible);
void		 gs_repos_section_add_repo		(GsReposSection		*self,
							 GsApp			*repo);
AdwPreferencesPage *
		 gs_repos_section_get_prefs_page	(GsReposSection		*self);
const gchar	*gs_repos_section_get_title		(GsReposSection		*self);
void		 gs_repos_section_set_title		(GsReposSection		*self,
							 const gchar		*value);
const gchar	*gs_repos_section_get_icon_name		(GsReposSection		*self);
void		 gs_repos_section_set_icon_name		(GsReposSection		*self,
							 const gchar		*value);
const gchar	*gs_repos_section_get_sort_key		(GsReposSection		*self);
void		 gs_repos_section_set_sort_key		(GsReposSection		*self,
							 const gchar		*sort_key);
gboolean	 gs_repos_section_get_related_loaded	(GsReposSection *self);
void		 gs_repos_section_set_related_loaded	(GsReposSection *self,
							 gboolean value);

G_END_DECLS
