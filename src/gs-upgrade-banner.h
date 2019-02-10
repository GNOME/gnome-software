/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef GS_UPGRADE_BANNER_H
#define GS_UPGRADE_BANNER_H

#include <gtk/gtk.h>

#include "gnome-software-private.h"

G_BEGIN_DECLS

#define GS_TYPE_UPGRADE_BANNER (gs_upgrade_banner_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsUpgradeBanner, gs_upgrade_banner, GS, UPGRADE_BANNER, GtkBin)

struct _GsUpgradeBannerClass
{
	GtkBinClass	 parent_class;

	void		(*download_clicked)	(GsUpgradeBanner	*self);
	void		(*install_clicked)	(GsUpgradeBanner	*self);
	void		(*cancel_clicked)	(GsUpgradeBanner	*self);
	void		(*help_clicked)		(GsUpgradeBanner	*self);
};

GtkWidget	*gs_upgrade_banner_new			(void);
void		 gs_upgrade_banner_set_app		(GsUpgradeBanner	*self,
							 GsApp			*app);
GsApp		*gs_upgrade_banner_get_app		(GsUpgradeBanner	*self);

G_END_DECLS

#endif /* GS_UPGRADE_BANNER_H */

/* vim: set noexpandtab: */
