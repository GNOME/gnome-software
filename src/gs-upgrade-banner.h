/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Kalev Lember <klember@redhat.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef GS_UPGRADE_BANNER_H
#define GS_UPGRADE_BANNER_H

#include <gtk/gtk.h>

#include "gs-app.h"

G_BEGIN_DECLS

#define GS_TYPE_UPGRADE_BANNER (gs_upgrade_banner_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsUpgradeBanner, gs_upgrade_banner, GS, UPGRADE_BANNER, GtkBin)

struct _GsUpgradeBannerClass
{
	GtkBinClass	 parent_class;

	void		(*download_button_clicked)	(GsUpgradeBanner	*self);
	void		(*install_button_clicked)	(GsUpgradeBanner	*self);
	void		(*learn_more_button_clicked)	(GsUpgradeBanner	*self);
};

GtkWidget	*gs_upgrade_banner_new			(GsApp			*app);

G_END_DECLS

#endif /* GS_UPGRADE_BANNER_H */

/* vim: set noexpandtab: */
