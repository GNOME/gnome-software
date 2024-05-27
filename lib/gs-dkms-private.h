/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define GS_AKMODS_KEY_PATH "/etc/pki/akmods/certs"
#define GS_AKMODS_KEY_FILENAME GS_AKMODS_KEY_PATH "/public_key.der"

typedef enum {
	GS_DKMS_KEY_KIND_AKMODS,
	GS_DKMS_KEY_KIND_DKMS
} GsDkmsKeyKind;

typedef enum {
	GS_DKMS_STATE_ERROR		= 0,
	GS_DKMS_STATE_ENROLLED		= 1,
	GS_DKMS_STATE_NOT_FOUND		= 2,
	GS_DKMS_STATE_NOT_ENROLLED	= 3,
	GS_DKMS_STATE_PENDING		= 4
} GsDkmsState;

typedef enum {
	GS_SECUREBOOT_STATE_UNKNOWN = 0,
	GS_SECUREBOOT_STATE_DISABLED = 1,
	GS_SECUREBOOT_STATE_ENABLED,
	GS_SECUREBOOT_STATE_NOT_SUPPORTED
} GsSecurebootState;

void			gs_dkms_get_key_state_async		(GsDkmsKeyKind key_kind,
								 GCancellable *cancellable,
								 GAsyncReadyCallback callback,
								 gpointer user_data);
GsDkmsState		gs_dkms_get_key_state_finish		(GAsyncResult *result,
								 GError **error);
void			gs_dkms_enroll_async			(GsDkmsKeyKind key_kind,
								 const gchar *password,
								 GCancellable *cancellable,
								 GAsyncReadyCallback callback,
								 gpointer user_data);
GsDkmsState		gs_dkms_enroll_finish			(GAsyncResult *result,
								 GError **error);
void			gs_dkms_get_secureboot_state_async	(GCancellable *cancellable,
								 GAsyncReadyCallback callback,
								 gpointer user_data);
GsSecurebootState	gs_dkms_get_secureboot_state_finish	(GAsyncResult *result,
								 GError **error);
GsSecurebootState	gs_dkms_get_last_secureboot_state	(void);
gchar *			gs_dkms_get_dkms_key_path		(void);
gchar *			gs_dkms_get_dkms_key_filename		(void);

G_END_DECLS
