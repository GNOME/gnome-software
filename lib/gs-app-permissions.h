/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2022 Red Hat <www.redhat.com>
 * Copyright (C) 2025 GNOME Foundation, Inc.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Additional authors:
 *  - Philip Withnall <pwithnall@gnome.org>
 */

#pragma once

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

/**
 * GsAppPermissionsFlags:
 * @GS_APP_PERMISSIONS_FLAGS_SYSTEM_BUS: App has unfiltered access to the D-Bus
 *    system bus (i.e. can talk to and own any name on it).
 * @GS_APP_PERMISSIONS_FLAGS_SESSION_BUS: App has unfiltered access to the D-Bus
 *    session bus (i.e. can talk to and own any name on it).
 * @GS_APP_PERMISSIONS_FLAGS_DEVICES: App can access general purpose devices
 *   such as webcams or gaming controllers.
 * @GS_APP_PERMISSIONS_FLAGS_SYSTEM_DEVICES: App can access non-physical
 *   privileged system devices, such as `/dev/shm` or `/dev/kvm` (Since: 44)
 * @GS_APP_PERMISSIONS_FLAGS_SCREEN: App can access screen contents
 *   without asking, e.g. by reading Pipewire ScreenCast streams (Since: 46)
 * @GS_APP_PERMISSIONS_FLAGS_INPUT_DEVICES: App can access input devices, under `/dev/input` (Since: 46)
 * @GS_APP_PERMISSIONS_FLAGS_AUDIO_DEVICES: App can access audio devices (such as microphones and speakers) from PulseAudio and pipewire directly (Since: 48)
 * @GS_APP_PERMISSIONS_FLAGS_BUS_POLICY_OTHER: App has one or more #GsBusPolicys
 *    which give it some access to non-portal services on the system or session
 *    D-Bus buses. (Since: 49)
 *
 * Flags to indicate what permissions an app requires, at a high level.
 */
typedef enum {
	GS_APP_PERMISSIONS_FLAGS_NONE			= 0,
	GS_APP_PERMISSIONS_FLAGS_NETWORK 		= 1 << 1,
	GS_APP_PERMISSIONS_FLAGS_SYSTEM_BUS   		= 1 << 2,
	GS_APP_PERMISSIONS_FLAGS_SESSION_BUS		= 1 << 3,
	GS_APP_PERMISSIONS_FLAGS_DEVICES 		= 1 << 4,
	GS_APP_PERMISSIONS_FLAGS_HOME_FULL 		= 1 << 5,
	GS_APP_PERMISSIONS_FLAGS_HOME_READ		= 1 << 6,
	GS_APP_PERMISSIONS_FLAGS_FILESYSTEM_FULL	= 1 << 7,
	GS_APP_PERMISSIONS_FLAGS_FILESYSTEM_READ	= 1 << 8,
	GS_APP_PERMISSIONS_FLAGS_DOWNLOADS_FULL 	= 1 << 9,
	GS_APP_PERMISSIONS_FLAGS_DOWNLOADS_READ		= 1 << 10,
	GS_APP_PERMISSIONS_FLAGS_SETTINGS		= 1 << 11,
	GS_APP_PERMISSIONS_FLAGS_X11			= 1 << 12,
	GS_APP_PERMISSIONS_FLAGS_ESCAPE_SANDBOX		= 1 << 13,
	GS_APP_PERMISSIONS_FLAGS_FILESYSTEM_OTHER	= 1 << 14,
	GS_APP_PERMISSIONS_FLAGS_SYSTEM_DEVICES		= 1 << 15,
	GS_APP_PERMISSIONS_FLAGS_SCREEN			= 1 << 16,
	GS_APP_PERMISSIONS_FLAGS_INPUT_DEVICES		= 1 << 17,
	GS_APP_PERMISSIONS_FLAGS_AUDIO_DEVICES		= 1 << 18,
	GS_APP_PERMISSIONS_FLAGS_BUS_POLICY_OTHER	= 1 << 19,
	GS_APP_PERMISSIONS_FLAGS_LAST  /*< skip >*/
} GsAppPermissionsFlags;

#define LIMITED_PERMISSIONS (GS_APP_PERMISSIONS_FLAGS_SETTINGS | \
			GS_APP_PERMISSIONS_FLAGS_NETWORK | \
			GS_APP_PERMISSIONS_FLAGS_DOWNLOADS_READ | \
			GS_APP_PERMISSIONS_FLAGS_DOWNLOADS_FULL)
#define MEDIUM_PERMISSIONS (LIMITED_PERMISSIONS | \
			GS_APP_PERMISSIONS_FLAGS_X11)

/**
 * GsBusPolicyPermission:
 * @GS_BUS_POLICY_PERMISSION_NONE: No permissions. The bus name is invisible to the app.
 * @GS_BUS_POLICY_PERMISSION_SEE: The bus name can be enumerated by the app.
 * @GS_BUS_POLICY_PERMISSION_TALK: The app can exchange messages with the bus name.
 * @GS_BUS_POLICY_PERMISSION_OWN: The app can own the bus name.
 * @GS_BUS_POLICY_PERMISSION_UNKNOWN: Permissions are unknown.
 *
 * Permissions for app interactions with services on a D-Bus bus.
 *
 * These are in strictly ascending order of what they allow (so each enum member
 * allows all of what the lower-valued members allow). It follows exactly the
 * same semantics as [flatpak](man:flatpak-metadata(5)).
 *
 * Since: 48
 */
typedef enum {
	GS_BUS_POLICY_PERMISSION_NONE = 0,
	GS_BUS_POLICY_PERMISSION_SEE,
	GS_BUS_POLICY_PERMISSION_TALK,
	GS_BUS_POLICY_PERMISSION_OWN,
	GS_BUS_POLICY_PERMISSION_UNKNOWN,
} GsBusPolicyPermission;

/**
 * GsBusPolicy:
 * @bus_type: Bus type this applies to.
 * @bus_name: Bus name or prefix (such as `org.gtk.vfs.*`) this applies to.
 * @permission: Permissions granted.
 *
 * A single entry in a bus policy which determines which bus names an app can
 * interact with while sandboxed.
 *
 * Bus policies are keyed by the combination of @bus_type and @bus_name.
 *
 * Since: 49
 */
typedef struct {
	GBusType bus_type;
	char *bus_name;  /* (owned) */
	GsBusPolicyPermission permission;
} GsBusPolicy;

GsBusPolicy		*gs_bus_policy_new		(GBusType		 bus_type,
							 const char		*bus_name,
							 GsBusPolicyPermission	 permission);
void			 gs_bus_policy_free		(GsBusPolicy		*self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GsBusPolicy, gs_bus_policy_free)

#define GS_TYPE_APP_PERMISSIONS (gs_app_permissions_get_type ())

G_DECLARE_FINAL_TYPE (GsAppPermissions, gs_app_permissions, GS, APP_PERMISSIONS, GObject)

GsAppPermissions	*gs_app_permissions_new		(void);
void			 gs_app_permissions_seal	(GsAppPermissions	*self);
gboolean		 gs_app_permissions_is_sealed	(GsAppPermissions	*self);

gboolean		 gs_app_permissions_is_empty	(GsAppPermissions	*self);
GsAppPermissions	*gs_app_permissions_diff	(GsAppPermissions	*self,
							 GsAppPermissions	*other);

void			 gs_app_permissions_set_flags	(GsAppPermissions	*self,
							 GsAppPermissionsFlags	 flags);
GsAppPermissionsFlags	 gs_app_permissions_get_flags	(GsAppPermissions	*self);
void			 gs_app_permissions_add_flag	(GsAppPermissions	*self,
							 GsAppPermissionsFlags	 flags);
void			 gs_app_permissions_remove_flag	(GsAppPermissions	*self,
							 GsAppPermissionsFlags	 flags);
void			 gs_app_permissions_add_filesystem_read
							(GsAppPermissions	*self,
							 const gchar		*filename);
const GPtrArray		*gs_app_permissions_get_filesystem_read
							(GsAppPermissions	*self);
gboolean		 gs_app_permissions_contains_filesystem_read
							(GsAppPermissions *self,
							 const gchar *filename);
void			 gs_app_permissions_add_filesystem_full
							(GsAppPermissions	*self,
							 const gchar		*filename);
const GPtrArray		*gs_app_permissions_get_filesystem_full
							(GsAppPermissions	*self);
gboolean		 gs_app_permissions_contains_filesystem_full
							(GsAppPermissions *self,
							 const gchar *filename);

void			 gs_app_permissions_add_bus_policy	(GsAppPermissions	*self,
								 GBusType		 bus_type,
								 const char		*bus_name,
								 GsBusPolicyPermission	 permission);
const GsBusPolicy * const *gs_app_permissions_get_bus_policies	(GsAppPermissions	*self,
								 size_t			*out_n_bus_policies);

G_END_DECLS
