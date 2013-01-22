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

#include "config.h"

#include <string.h>
#include <glib.h>

#include "ch-markdown.h"

/***********************************************************************
 *
 * This is a simple Markdown parser.
 * It can output to Pango markup. The following limitations are
 * already known, and properly deliberate:
 *
 * - No code section support
 * - No ordered list support
 * - No blockquote section support
 * - No image support
 * - No links or email support
 * - No backslash escapes support
 * - No HTML escaping support
 * - Auto-escapes certain word patterns, like http://
 *
 * It does support the rest of the standard pretty well, although it's not
 * been run against any conformance tests. The parsing is single pass, with
 * a simple enumerated intepretor mode and a single line back-memory.
 *
 **********************************************************************/

static void     ch_markdown_finalize	(GObject		*object);

#define CH_MARKDOWN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CH_TYPE_MARKDOWN, ChMarkdownPrivate))

#define CH_MARKDOWN_MAX_LINE_LENGTH	1024

typedef enum {
	CH_MARKDOWN_MODE_BLANK,
	CH_MARKDOWN_MODE_RULE,
	CH_MARKDOWN_MODE_BULLETT,
	CH_MARKDOWN_MODE_PARA,
	CH_MARKDOWN_MODE_H1,
	CH_MARKDOWN_MODE_H2,
	CH_MARKDOWN_MODE_UNKNOWN
} ChMarkdownMode;

struct ChMarkdownPrivate
{
	ChMarkdownMode		 mode;
	gint			 line_count;
	gboolean		 smart_quoting;
	gboolean		 escape;
	gboolean		 autocode;
	GString			*pending;
	GString			*processed;
};

G_DEFINE_TYPE (ChMarkdown, ch_markdown, G_TYPE_OBJECT)

/**
 * ch_markdown_to_text_line_is_rule:
 *
 * Horizontal rules are created by placing three or more hyphens, asterisks,
 * or underscores on a line by themselves.
 * You may use spaces between the hyphens or asterisks.
 **/
static gboolean
ch_markdown_to_text_line_is_rule (const gchar *line)
{
	guint i;
	guint len;
	guint count = 0;
	gchar *copy = NULL;
	gboolean ret = FALSE;

	len = strlen (line);
	if (len == 0 || len > CH_MARKDOWN_MAX_LINE_LENGTH)
		goto out;

	/* replace non-rule chars with ~ */
	copy = g_strdup (line);
	g_strcanon (copy, "-*_ ", '~');
	for (i=0; i<len; i++) {
		if (copy[i] == '~')
			goto out;
		if (copy[i] != ' ')
			count++;
	}

	/* if we matched, return true */
	if (count >= 3)
		ret = TRUE;
out:
	g_free (copy);
	return ret;
}

/**
 * ch_markdown_to_text_line_is_bullett:
 **/
static gboolean
ch_markdown_to_text_line_is_bullett (const gchar *line)
{
	return (g_str_has_prefix (line, "- ") ||
		g_str_has_prefix (line, "* ") ||
		g_str_has_prefix (line, "+ ") ||
		g_str_has_prefix (line, " - ") ||
		g_str_has_prefix (line, " * ") ||
		g_str_has_prefix (line, " + "));
}

/**
 * ch_markdown_to_text_line_is_header1:
 **/
static gboolean
ch_markdown_to_text_line_is_header1 (const gchar *line)
{
	return g_str_has_prefix (line, "# ");
}

/**
 * ch_markdown_to_text_line_is_header2:
 **/
static gboolean
ch_markdown_to_text_line_is_header2 (const gchar *line)
{
	return g_str_has_prefix (line, "## ");
}

/**
 * ch_markdown_to_text_line_is_header1_type2:
 **/
static gboolean
ch_markdown_to_text_line_is_header1_type2 (const gchar *line)
{
	return g_str_has_prefix (line, "===");
}

/**
 * ch_markdown_to_text_line_is_header2_type2:
 **/
static gboolean
ch_markdown_to_text_line_is_header2_type2 (const gchar *line)
{
	return g_str_has_prefix (line, "---");
}

#if 0
/**
 * ch_markdown_to_text_line_is_code:
 **/
static gboolean
ch_markdown_to_text_line_is_code (const gchar *line)
{
	return (g_str_has_prefix (line, "    ") || g_str_has_prefix (line, "\t"));
}

/**
 * ch_markdown_to_text_line_is_blockquote:
 **/
static gboolean
ch_markdown_to_text_line_is_blockquote (const gchar *line)
{
	return (g_str_has_prefix (line, "> "));
}
#endif

/**
 * ch_markdown_to_text_line_is_blank:
 **/
static gboolean
ch_markdown_to_text_line_is_blank (const gchar *line)
{
	guint i;
	guint len;
	gboolean ret = FALSE;

	/* a line with no characters is blank by definition */
	len = strlen (line);
	if (len == 0) {
		ret = TRUE;
		goto out;
	}

	/* find if there are only space chars */
	for (i=0; i<len; i++) {
		if (line[i] != ' ' && line[i] != '\t')
			goto out;
	}

	/* if we matched, return true */
	ret = TRUE;
out:
	return ret;
}

/**
 * ch_markdown_replace:
 **/
static gchar *
ch_markdown_replace (const gchar *haystack,
		     const gchar *needle,
		     const gchar *replace)
{
	gchar *new;
	gchar **split;

	split = g_strsplit (haystack, needle, -1);
	new = g_strjoinv (replace, split);
	g_strfreev (split);

	return new;
}

/**
 * ch_markdown_strstr_spaces:
 **/
static gchar *
ch_markdown_strstr_spaces (const gchar *haystack, const gchar *needle)
{
	gchar *found;
	const gchar *haystack_new = haystack;

retry:
	/* don't find if surrounded by spaces */
	found = strstr (haystack_new, needle);
	if (found == NULL)
		return NULL;

	/* start of the string, always valid */
	if (found == haystack)
		return found;

	/* end of the string, always valid */
	if (*(found-1) == ' ' && *(found+1) == ' ') {
		haystack_new = found+1;
		goto retry;
	}
	return found;
}

/**
 * ch_markdown_to_text_line_formatter:
 **/
static gchar *
ch_markdown_to_text_line_formatter (const gchar *line,
				    const gchar *formatter,
				    const gchar *left,
				    const gchar *right)
{
	guint len;
	gchar *str1;
	gchar *str2;
	gchar *start = NULL;
	gchar *middle = NULL;
	gchar *end = NULL;
	gchar *copy = NULL;
	gchar *data = NULL;
	gchar *temp;

	/* needed to know for shifts */
	len = strlen (formatter);
	if (len == 0)
		goto out;

	/* find sections */
	copy = g_strdup (line);
	str1 = ch_markdown_strstr_spaces (copy, formatter);
	if (str1 != NULL) {
		*str1 = '\0';
		str2 = ch_markdown_strstr_spaces (str1+len, formatter);
		if (str2 != NULL) {
			*str2 = '\0';
			middle = str1 + len;
			start = copy;
			end = str2 + len;
		}
	}

	/* if we found, replace and keep looking for the same string */
	if (start != NULL && middle != NULL && end != NULL) {
		temp = g_strdup_printf ("%s%s%s%s%s", start, left, middle, right, end);
		/* recursive */
		data = ch_markdown_to_text_line_formatter (temp, formatter, left, right);
		g_free (temp);
	} else {
		/* not found, keep return as-is */
		data = g_strdup (line);
	}
out:
	g_free (copy);
	return data;
}

/**
 * ch_markdown_to_text_line_format_sections:
 **/
static gchar *
ch_markdown_to_text_line_format_sections (ChMarkdown *markdown, const gchar *line)
{
	gchar *data = g_strdup (line);
	gchar *temp;

	/* bold1 */
	temp = data;
	data = ch_markdown_to_text_line_formatter (temp, "**", "<b>", "</b>");
	g_free (temp);

	/* bold2 */
	temp = data;
	data = ch_markdown_to_text_line_formatter (temp, "__", "<b>", "</b>");
	g_free (temp);

	/* italic1 */
	temp = data;
	data = ch_markdown_to_text_line_formatter (temp, "*", "<i>", "</i>");
	g_free (temp);

	/* italic2 */
	temp = data;
	data = ch_markdown_to_text_line_formatter (temp, "_", "<i>", "</i>");
	g_free (temp);

	/* em-dash */
	temp = data;
	data = ch_markdown_replace (temp, " -- ", " — ");
	g_free (temp);

	/* smart quoting */
	if (markdown->priv->smart_quoting) {
		temp = data;
		data = ch_markdown_to_text_line_formatter (temp, "\"", "“", "”");
		g_free (temp);

		temp = data;
		data = ch_markdown_to_text_line_formatter (temp, "'", "‘", "’");
		g_free (temp);
	}

	return data;
}

/**
 * ch_markdown_to_text_line_format:
 **/
static gchar *
ch_markdown_to_text_line_format (ChMarkdown *markdown, const gchar *line)
{
	guint i;
	gchar *text;
	gboolean mode = FALSE;
	gchar **codes;
	GString *string;

	/* optimise the trivial case where we don't have any code tags */
	text = strstr (line, "`");
	if (text == NULL) {
		text = ch_markdown_to_text_line_format_sections (markdown, line);
		goto out;
	}

	/* we want to parse the code sections without formatting */
	codes = g_strsplit (line, "`", -1);
	string = g_string_new ("");
	for (i=0; codes[i] != NULL; i++) {
		if (!mode) {
			text = ch_markdown_to_text_line_format_sections (markdown, codes[i]);
			g_string_append (string, text);
			g_free (text);
			mode = TRUE;
		} else {
			/* just append without formatting */
			g_string_append (string, "<tt>");
			g_string_append (string, codes[i]);
			g_string_append (string, "</tt>");
			mode = FALSE;
		}
	}
	text = g_string_free (string, FALSE);
out:
	return text;
}

/**
 * ch_markdown_add_pending:
 **/
static gboolean
ch_markdown_add_pending (ChMarkdown *markdown, const gchar *line)
{
	gchar *copy;

	copy = g_strdup (line);

	/* strip leading and trailing spaces */
	g_strstrip (copy);

	/* append */
	g_string_append_printf (markdown->priv->pending, "%s ", copy);

	g_free (copy);
	return TRUE;
}

/**
 * ch_markdown_add_pending_header:
 **/
static gboolean
ch_markdown_add_pending_header (ChMarkdown *markdown, const gchar *line)
{
	gchar *copy;
	gboolean ret;

	/* strip trailing # */
	copy = g_strdup (line);
	g_strdelimit (copy, "#", ' ');
	ret = ch_markdown_add_pending (markdown, copy);
	g_free (copy);
	return ret;
}

/**
 * ch_markdown_count_chars_in_word:
 **/
static guint
ch_markdown_count_chars_in_word (const gchar *text, gchar find)
{
	guint i;
	guint len;
	guint count = 0;

	/* get length */
	len = strlen (text);
	if (len == 0)
		goto out;

	/* find matching chars */
	for (i=0; i<len; i++) {
		if (text[i] == find)
			count++;
	}
out:
	return count;
}

/**
 * ch_markdown_word_is_code:
 **/
static gboolean
ch_markdown_word_is_code (const gchar *text)
{
	/* already code */
	if (g_str_has_prefix (text, "`"))
		return FALSE;
	if (g_str_has_suffix (text, "`"))
		return FALSE;

	/* paths */
	if (g_str_has_prefix (text, "/"))
		return TRUE;

	/* bugzillas */
	if (g_str_has_prefix (text, "#"))
		return TRUE;

	/* uri's */
	if (g_str_has_prefix (text, "http://"))
		return TRUE;
	if (g_str_has_prefix (text, "https://"))
		return TRUE;
	if (g_str_has_prefix (text, "ftp://"))
		return TRUE;

	/* patch files */
	if (g_strrstr (text, ".patch") != NULL)
		return TRUE;
	if (g_strrstr (text, ".diff") != NULL)
		return TRUE;

	/* function names */
	if (g_strrstr (text, "()") != NULL)
		return TRUE;

	/* email addresses */
	if (g_strrstr (text, "@") != NULL)
		return TRUE;

	/* compiler defines */
	if (text[0] != '_' &&
	    ch_markdown_count_chars_in_word (text, '_') > 1)
		return TRUE;

	/* nothing special */
	return FALSE;
}

/**
 * ch_markdown_word_auto_format_code:
 **/
static gchar *
ch_markdown_word_auto_format_code (const gchar *text)
{
	guint i;
	gchar *temp;
	gchar **words;
	gboolean ret = FALSE;

	/* split sentence up with space */
	words = g_strsplit (text, " ", -1);

	/* search each word */
	for (i=0; words[i] != NULL; i++) {
		if (ch_markdown_word_is_code (words[i])) {
			temp = g_strdup_printf ("`%s`", words[i]);
			g_free (words[i]);
			words[i] = temp;
			ret = TRUE;
		}
	}

	/* no replacements, so just return a copy */
	if (!ret) {
		temp = g_strdup (text);
		goto out;
	}

	/* join the array back into a string */
	temp = g_strjoinv (" ", words);
out:
	g_strfreev (words);
	return temp;
}

/**
 * ch_markdown_flush_pending:
 **/
static void
ch_markdown_flush_pending (ChMarkdown *markdown)
{
	gchar *copy;
	gchar *temp;

	/* no data yet */
	if (markdown->priv->mode == CH_MARKDOWN_MODE_UNKNOWN)
		return;

	/* remove trailing spaces */
	while (g_str_has_suffix (markdown->priv->pending->str, " ")) {
		g_string_set_size (markdown->priv->pending,
				   markdown->priv->pending->len - 1);
	}

	/* pango requires escaping */
	copy = g_strdup (markdown->priv->pending->str);
	if (!markdown->priv->escape) {
		g_strdelimit (copy, "<", '(');
		g_strdelimit (copy, ">", ')');
	}

	/* check words for code */
	if (markdown->priv->autocode &&
	    (markdown->priv->mode == CH_MARKDOWN_MODE_PARA ||
	     markdown->priv->mode == CH_MARKDOWN_MODE_BULLETT)) {
		temp = ch_markdown_word_auto_format_code (copy);
		g_free (copy);
		copy = temp;
	}

	/* escape */
	if (markdown->priv->escape) {
		temp = g_markup_escape_text (copy, -1);
		g_free (copy);
		copy = temp;
	}

	/* do formatting */
	temp = ch_markdown_to_text_line_format (markdown, copy);
	if (markdown->priv->mode == CH_MARKDOWN_MODE_BULLETT) {
		g_string_append_printf (markdown->priv->processed, "%s%s%s\n", "• ", temp, "");
		markdown->priv->line_count++;
	} else if (markdown->priv->mode == CH_MARKDOWN_MODE_H1) {
		g_string_append_printf (markdown->priv->processed, "%s%s%s\n", "<big>", temp, "</big>");
	} else if (markdown->priv->mode == CH_MARKDOWN_MODE_H2) {
		g_string_append_printf (markdown->priv->processed, "%s%s%s\n", "<b>", temp, "</b>");
	} else if (markdown->priv->mode == CH_MARKDOWN_MODE_PARA ||
		   markdown->priv->mode == CH_MARKDOWN_MODE_RULE) {
		g_string_append_printf (markdown->priv->processed, "%s\n", temp);
		markdown->priv->line_count++;
	}

	/* clear */
	g_string_truncate (markdown->priv->pending, 0);
	g_free (copy);
	g_free (temp);
}

/**
 * ch_markdown_to_text_line_process:
 **/
static gboolean
ch_markdown_to_text_line_process (ChMarkdown *markdown, const gchar *line)
{
	gboolean ret;

	/* blank */
	ret = ch_markdown_to_text_line_is_blank (line);
	if (ret) {
		ch_markdown_flush_pending (markdown);
		/* a new line after a list is the end of list, not a gap */
		if (markdown->priv->mode != CH_MARKDOWN_MODE_BULLETT)
			ret = ch_markdown_add_pending (markdown, "\n");
		markdown->priv->mode = CH_MARKDOWN_MODE_BLANK;
		goto out;
	}

	/* header1_type2 */
	ret = ch_markdown_to_text_line_is_header1_type2 (line);
	if (ret) {
		if (markdown->priv->mode == CH_MARKDOWN_MODE_PARA)
			markdown->priv->mode = CH_MARKDOWN_MODE_H1;
		goto out;
	}

	/* header2_type2 */
	ret = ch_markdown_to_text_line_is_header2_type2 (line);
	if (ret) {
		if (markdown->priv->mode == CH_MARKDOWN_MODE_PARA)
			markdown->priv->mode = CH_MARKDOWN_MODE_H2;
		goto out;
	}

	/* rule */
	ret = ch_markdown_to_text_line_is_rule (line);
	if (ret) {
		ch_markdown_flush_pending (markdown);
		markdown->priv->mode = CH_MARKDOWN_MODE_RULE;
		ret = ch_markdown_add_pending (markdown, "⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯⎯\n");
		goto out;
	}

	/* bullett */
	ret = ch_markdown_to_text_line_is_bullett (line);
	if (ret) {
		ch_markdown_flush_pending (markdown);
		markdown->priv->mode = CH_MARKDOWN_MODE_BULLETT;
		ret = ch_markdown_add_pending (markdown, &line[2]);
		goto out;
	}

	/* header1 */
	ret = ch_markdown_to_text_line_is_header1 (line);
	if (ret) {
		ch_markdown_flush_pending (markdown);
		markdown->priv->mode = CH_MARKDOWN_MODE_H1;
		ret = ch_markdown_add_pending_header (markdown, &line[2]);
		goto out;
	}

	/* header2 */
	ret = ch_markdown_to_text_line_is_header2 (line);
	if (ret) {
		ch_markdown_flush_pending (markdown);
		markdown->priv->mode = CH_MARKDOWN_MODE_H2;
		ret = ch_markdown_add_pending_header (markdown, &line[3]);
		goto out;
	}

	/* paragraph */
	if (markdown->priv->mode == CH_MARKDOWN_MODE_BLANK || markdown->priv->mode == CH_MARKDOWN_MODE_UNKNOWN) {
		ch_markdown_flush_pending (markdown);
		markdown->priv->mode = CH_MARKDOWN_MODE_PARA;
	}

	/* add to pending */
	ret = ch_markdown_add_pending (markdown, line);
out:
	/* if we failed to add, we don't know the mode */
	if (!ret)
		markdown->priv->mode = CH_MARKDOWN_MODE_UNKNOWN;
	return ret;
}

/**
 * ch_markdown_parse:
 **/
gchar *
ch_markdown_parse (ChMarkdown *markdown, const gchar *text)
{
	gchar **lines;
	guint i;
	guint len;
	gchar *temp;
	gboolean ret;

	g_return_val_if_fail (CH_IS_MARKDOWN (markdown), NULL);

	/* process */
	markdown->priv->mode = CH_MARKDOWN_MODE_UNKNOWN;
	markdown->priv->line_count = 0;
	g_string_truncate (markdown->priv->pending, 0);
	g_string_truncate (markdown->priv->processed, 0);
	lines = g_strsplit (text, "\n", -1);
	len = g_strv_length (lines);

	/* process each line */
	for (i=0; i<len; i++) {
		ret = ch_markdown_to_text_line_process (markdown, lines[i]);
		if (!ret)
			break;
	}
	g_strfreev (lines);
	ch_markdown_flush_pending (markdown);

	/* remove trailing \n */
	while (g_str_has_suffix (markdown->priv->processed->str, "\n"))
		g_string_set_size (markdown->priv->processed, markdown->priv->processed->len - 1);

	/* get a copy */
	temp = g_strdup (markdown->priv->processed->str);
	g_string_truncate (markdown->priv->pending, 0);
	g_string_truncate (markdown->priv->processed, 0);
	return temp;
}

/**
 * ch_markdown_class_init:
 **/
static void
ch_markdown_class_init (ChMarkdownClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = ch_markdown_finalize;
	g_type_class_add_private (klass, sizeof (ChMarkdownPrivate));
}

/**
 * ch_markdown_init:
 **/
static void
ch_markdown_init (ChMarkdown *markdown)
{
	markdown->priv = CH_MARKDOWN_GET_PRIVATE (markdown);
	markdown->priv->mode = CH_MARKDOWN_MODE_UNKNOWN;
	markdown->priv->pending = g_string_new ("");
	markdown->priv->processed = g_string_new ("");
	markdown->priv->smart_quoting = FALSE;
	markdown->priv->escape = FALSE;
	markdown->priv->autocode = FALSE;
}

/**
 * ch_markdown_finalize:
 **/
static void
ch_markdown_finalize (GObject *object)
{
	ChMarkdown *markdown;

	g_return_if_fail (CH_IS_MARKDOWN (object));

	markdown = CH_MARKDOWN (object);

	g_return_if_fail (markdown->priv != NULL);
	g_string_free (markdown->priv->pending, TRUE);
	g_string_free (markdown->priv->processed, TRUE);

	G_OBJECT_CLASS (ch_markdown_parent_class)->finalize (object);
}

/**
 * ch_markdown_new:
 **/
ChMarkdown *
ch_markdown_new (void)
{
	ChMarkdown *markdown;
	markdown = g_object_new (CH_TYPE_MARKDOWN, NULL);
	return CH_MARKDOWN (markdown);
}
