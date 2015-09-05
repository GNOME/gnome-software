/*
 * gs-shell-search-provider.h - Implementation of a GNOME Shell
 *   search provider
 *
 * Copyright (C) 2013 Matthias Clasen <mclasen@redhat.com>
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

#ifndef __GS_SHELL_SEARCH_PROVIDER_H
#define __GS_SHELL_SEARCH_PROVIDER_H

#include "gs-plugin-loader.h"

#define GS_TYPE_SHELL_SEARCH_PROVIDER gs_shell_search_provider_get_type()
#define GS_SHELL_SEARCH_PROVIDER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GS_TYPE_SHELL_SEARCH_PROVIDER, GsShellSearchProvider))

typedef struct _GsShellSearchProvider GsShellSearchProvider;
typedef GObjectClass GsShellSearchProviderClass;

GType gs_shell_search_provider_get_type (void);
gboolean gs_shell_search_provider_register (GsShellSearchProvider *self,
                                            GDBusConnection       *connection,
                                            GError               **error);
void gs_shell_search_provider_unregister (GsShellSearchProvider *self);
GsShellSearchProvider * gs_shell_search_provider_new (void);
void gs_shell_search_provider_setup (GsShellSearchProvider *provider,
				     GsPluginLoader *loader);

#endif /* __GS_SHELL_SEARCH_PROVIDER_H */
