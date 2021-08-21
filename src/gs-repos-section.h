/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <adwaita.h>
#include <gtk/gtk.h>

#include "gnome-software-private.h"
#include "gs-app.h"

G_BEGIN_DECLS

#define GS_TYPE_REPOS_SECTION (gs_repos_section_get_type ())

G_DECLARE_FINAL_TYPE (GsReposSection, gs_repos_section, GS, REPOS_SECTION, AdwPreferencesGroup)

GtkWidget	*gs_repos_section_new			(GsPluginLoader		*plugin_loader,
							 gboolean		 always_allow_enable_disable);
void		 gs_repos_section_add_repo		(GsReposSection		*self,
							 GsApp			*repo);
const gchar	*gs_repos_section_get_title		(GsReposSection		*self);
const gchar	*gs_repos_section_get_sort_key		(GsReposSection		*self);
void		 gs_repos_section_set_sort_key		(GsReposSection		*self,
							 const gchar		*sort_key);

G_END_DECLS
