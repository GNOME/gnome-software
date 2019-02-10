/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2017 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef __GS_FLATPAK_UTILS_H
#define __GS_FLATPAK_UTILS_H

G_BEGIN_DECLS

#include <gnome-software.h>

void		 gs_flatpak_error_convert		(GError		**perror);
GsApp		*gs_flatpak_app_new_from_remote		(FlatpakRemote	*xremote);
GsApp		*gs_flatpak_app_new_from_repo_file	(GFile		*file,
							 GCancellable	*cancellable,
							 GError		**error);

G_END_DECLS

#endif /* __GS_FLATPAK_UTILS_H */

