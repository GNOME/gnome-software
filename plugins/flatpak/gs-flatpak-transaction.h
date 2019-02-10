/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifndef __GS_FLATPAK_TRANSACTION_H
#define __GS_FLATPAK_TRANSACTION_H

#include <gnome-software.h>
#include <flatpak.h>

G_BEGIN_DECLS

#define GS_TYPE_FLATPAK_TRANSACTION (gs_flatpak_transaction_get_type ())

G_DECLARE_FINAL_TYPE (GsFlatpakTransaction, gs_flatpak_transaction, GS, FLATPAK_TRANSACTION, FlatpakTransaction)

FlatpakTransaction	*gs_flatpak_transaction_new		(FlatpakInstallation	*installation,
								 GCancellable		*cancellable,
								 GError			**error);
FlatpakInstallation	*gs_flatpak_transaction_get_inst	(FlatpakTransaction	*transaction);
GsApp			*gs_flatpak_transaction_get_app_by_ref	(FlatpakTransaction	*transaction,
								 const gchar		*ref);
void			 gs_flatpak_transaction_add_app		(FlatpakTransaction	*transaction,
								 GsApp			*app);
gboolean		 gs_flatpak_transaction_run		(FlatpakTransaction	*transaction,
								 GCancellable		*cancellable,
								 GError			**error);

G_END_DECLS

#endif /* __GS_FLATPAK_TRANSACTION_H */

