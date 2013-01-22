/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008-2011 Richard Hughes <richard@hughsie.com>
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

#ifndef __CH_MARKDOWN_H
#define __CH_MARKDOWN_H

#include <glib-object.h>

G_BEGIN_DECLS

#define CH_TYPE_MARKDOWN		(ch_markdown_get_type ())
#define CH_MARKDOWN(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), CH_TYPE_MARKDOWN, ChMarkdown))
#define CH_MARKDOWN_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), CH_TYPE_MARKDOWN, ChMarkdownClass))
#define CH_IS_MARKDOWN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), CH_TYPE_MARKDOWN))
#define CH_IS_MARKDOWN_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), CH_TYPE_MARKDOWN))
#define CH_MARKDOWN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), CH_TYPE_MARKDOWN, ChMarkdownClass))
#define CH_MARKDOWN_ERROR		(ch_markdown_error_quark ())
#define CH_MARKDOWN_TYPE_ERROR		(ch_markdown_error_get_type ())

typedef struct ChMarkdownPrivate ChMarkdownPrivate;

typedef struct
{
	 GObject		 parent;
	 ChMarkdownPrivate	*priv;
} ChMarkdown;

typedef struct
{
	GObjectClass	parent_class;
} ChMarkdownClass;

GType		 ch_markdown_get_type	  		(void);
ChMarkdown	*ch_markdown_new			(void);
gchar		*ch_markdown_parse			(ChMarkdown		*self,
							 const gchar		*text);

G_END_DECLS

#endif /* __CH_MARKDOWN_H */

