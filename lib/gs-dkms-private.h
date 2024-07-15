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

/*
 * GsDkmsKeyKind:
 * @GS_DKMS_KEY_KIND_AKMODS: use akmods key
 * @GS_DKMS_KEY_KIND_DKMS: use DKMS key
 *
 * The DKMS code can handle both DKMS and akmods keys. This enum helps
 * to distinguish, which one should be used.
 *
 * Since: 47
 **/

typedef enum {
	GS_DKMS_KEY_KIND_AKMODS,
	GS_DKMS_KEY_KIND_DKMS
} GsDkmsKeyKind;

/*
 * GsDkmsState:
 * @GS_DKMS_STATE_ERROR: there was an error determining the key state
 * @GS_DKMS_STATE_ENROLLED: the key is enrolled, which means it can used to sign the drivers
 * @GS_DKMS_STATE_NOT_FOUND: the key was not found, it's needed to be created first
 * @GS_DKMS_STATE_NOT_ENROLLED: the key exists, but is not enrolled yet
 * @GS_DKMS_STATE_PENDING: the key is scheduled to be enrolled the next boot
 *
 * Declares DKMS or akmods key states.
 *
 * Since: 47
 **/
typedef enum {
	GS_DKMS_STATE_ERROR		= 0,
	GS_DKMS_STATE_ENROLLED		= 1,
	GS_DKMS_STATE_NOT_FOUND		= 2,
	GS_DKMS_STATE_NOT_ENROLLED	= 3,
	GS_DKMS_STATE_PENDING		= 4
} GsDkmsState;

/*
 * GsSecurebootState:
 * @GS_SECUREBOOT_STATE_UNKNOWN: the Secure Boot state is unknown; it can
 *    for example mean there is not installed the tool to check its state
 * @GS_SECUREBOOT_STATE_DISABLED: the Secure Boot is disabled
 * @GS_SECUREBOOT_STATE_ENABLED: the Secure Boot is disabled
 * @GS_SECUREBOOT_STATE_NOT_SUPPORTED: the Secure Boot is not supported
 *    in this installation, like for example when the system is not
 *    installed with UEFI
 *
 * Declares states of the Secure Boot.
 *
 * Since: 47
 **/
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
