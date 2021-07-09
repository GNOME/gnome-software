/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

#include <glib-object.h>
#include <gnome-software.h>
#include <packagekit-glib2/packagekit.h>

G_BEGIN_DECLS

#define GS_TYPE_PACKAGEKIT_TASK (gs_packagekit_task_get_type ())

G_DECLARE_DERIVABLE_TYPE (GsPackageKitTask, gs_packagekit_task, GS, PACKAGEKIT_TASK, PkTask)

struct _GsPackageKitTaskClass
{
	PkTaskClass parent_class;
};

PkTask		*gs_packagekit_task_new		(GsPlugin		*plugin);
void		 gs_packagekit_task_setup	(GsPackageKitTask	*task,
						 GsPluginAction		 action,
						 gboolean		 interactive);
GsPluginAction	 gs_packagekit_task_get_action	(GsPackageKitTask	*task);

G_END_DECLS
