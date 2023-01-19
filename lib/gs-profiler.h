/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2023 Endless OS Foundation LLC
 *
 * Authors:
 *  - Georges Basile Stavracas Neto <georges@endlessos.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <config.h>
#include <gnome-software.h>

/**
 * SECTION:gs-profiler
 * @short_description: Macros for profiling
 *
 * GNOME Software provides a simple profiling mechanism that both plugins
 * and GNOME Software itself can make use. Use the GS_PROFILER_BEGIN()
 * macro to start profiling a specific code section; and pair it with
 * GS_PROFILER_END() to finish profiling the section:
 *
 * ```
 * GS_PROFILER_BEGIN(Flatpak, "list-installed-refs", "Description");
 * ... list all installed refs ...
 * GS_PROFILER_END(Flatpak);
 *
 *
 * GS_PROFILER_BEGIN(Foo, "parse-something", "Parse some data");
 * ... parse some data ...
 * GS_PROFILER_END(Foo);
 * ```
 *
 * The description argument is nullable:
 *
 * ```
 * GS_PROFILER_BEGIN(Flatpak, "list-installed-refs", NULL);
 * ... list all installed refs ...
 * GS_PROFILER_END(Flatpak);
 *```
 *
 * A rather common case is to allocate new strings for the Sysprof name
 * and description. The convenience macro GS_PROFILER_BEGIN_TAKE() is
 * provided for that:
 *
 * ```
 * GS_PROFILER_BEGIN(Foo, g_strdup_printf ("list-installed-refs:%s", name), NULL);
 * ... list all installed refs ...
 * GS_PROFILER_END(Foo);
 *```
 *
 * Asynchronous operations might need to track the start and end times in
 * separate functions. The convenience macros GS_PROFILER_ADD_MARK() and
 * GS_PROFILER_ADD_MARK_TAKE() allow for passing an independent begin time:
 *
 * ```
 * GS_PROFILER_ADD_MARK(Foo, task->begin_time, "do-something", NULL);
 *```
 *
 * Another common case is profiling functions that may have various
 * early returns, and adding one GS_PROFILER_END() before each return
 * is inconvenient. For that, wrap the entire function with a
 * GS_PROFILER_BEGIN_SCOPED() (or GS_PROFILER_BEGIN_SCOPED_TAKE()) and
 * a GS_PROFILER_END_SCOPED() pair of macros:
 *
 * ```
 * GS_PROFILER_BEGIN_SCOPED(Foo, "list-applications", NULL);
 *
 * if (!foo_is_correct (foo))
 * 	return FALSE;
 *
 * if (!long_async_op (foo, &error))
 * 	return FALSE;
 *
 * return TRUE;
 *
 * GS_PROFILER_END_SCOPED(Foo);
 *```
 *
 * Since: 44
 */

#ifdef HAVE_SYSPROF
#include <sysprof-capture.h>

typedef struct
{
	int64_t begin_time;
	gchar *name;
	gchar *description;
} GsProfilerHead;

static inline void
gs_profiler_tracing_end (GsProfilerHead *head)
{
	sysprof_collector_mark (head->begin_time,
				SYSPROF_CAPTURE_CURRENT_TIME - head->begin_time,
				"gnome-software",
				head->name,
				head->description);

	g_clear_pointer (&head->name, g_free);
	g_clear_pointer (&head->description, g_free);
}
static inline void
gs_profiler_auto_trace_end_helper (GsProfilerHead **head)
{
	if (*head)
		gs_profiler_tracing_end (*head);
}

#define GS_PROFILER_BEGIN_TAKE(Name, sysprof_name, sysprof_description) \
	G_STMT_START { \
	GsProfilerHead GsProfiler##Name = { \
		.begin_time = SYSPROF_CAPTURE_CURRENT_TIME, \
		.name = sysprof_name, \
		.description = sysprof_description, \
	};

#define GS_PROFILER_BEGIN(Name, sysprof_name, sysprof_description) \
	GS_PROFILER_BEGIN_TAKE (Name, g_strdup (sysprof_name), g_strdup (sysprof_description))

#define GS_PROFILER_END(Name) \
	gs_profiler_tracing_end (&GsProfiler##Name); \
	} G_STMT_END

#define GS_PROFILER_BEGIN_SCOPED_TAKE(Name, sysprof_name, sysprof_description) \
	G_STMT_START { \
	GsProfilerHead GsProfiler##Name; \
	__attribute__((cleanup (gs_profiler_auto_trace_end_helper))) \
		GsProfilerHead *ScopedGsProfilerTraceHead##Name = &GsProfiler##Name; \
	GsProfiler##Name = (GsProfilerHead) { \
		.begin_time = SYSPROF_CAPTURE_CURRENT_TIME, \
		.name = sysprof_name, \
		.description = sysprof_description, \
	};

#define GS_PROFILER_BEGIN_SCOPED(Name, sysprof_name, sysprof_description) \
	GS_PROFILER_BEGIN_SCOPED_TAKE (Name, g_strdup (sysprof_name), g_strdup (sysprof_description))

#define GS_PROFILER_END_SCOPED(Name) \
	} G_STMT_END

#define GS_PROFILER_ADD_MARK_TAKE(Name, begin_time, sysprof_name, sysprof_description) \
	G_STMT_START { \
		g_autofree char *_owned_sysprof_name_##Name = sysprof_name; \
		g_autofree char *_owned_sysprof_description_##Name = sysprof_description; \
		sysprof_collector_mark (begin_time, \
					SYSPROF_CAPTURE_CURRENT_TIME - begin_time, \
					"gnome-software", \
					sysprof_name, \
					sysprof_description); \
	} G_STMT_END

#define GS_PROFILER_ADD_MARK(Name, begin_time, sysprof_name, sysprof_description) \
	GS_PROFILER_ADD_MARK_TAKE (Name, begin_time, g_strdup (sysprof_name), g_strdup (sysprof_description))

#else

#define GS_PROFILER_BEGIN_TAKE(Name, sysprof_name, sysprof_description)
#define GS_PROFILER_BEGIN(Name, name, description)
#define GS_PROFILER_END(Name)
#define GS_PROFILER_BEGIN_SCOPED_TAKE(Name, sysprof_name, sysprof_description) \
	G_STMT_START {
#define GS_PROFILER_BEGIN_SCOPED(Name, sysprof_name, sysprof_description) \
	G_STMT_START {
#define GS_PROFILER_END_SCOPED(Name) \
	} G_STMT_END
#define GS_PROFILER_ADD_MARK_TAKE(Name, begin_time, sysprof_name, sysprof_description)
#define GS_PROFILER_ADD_MARK(Name, begin_time, sysprof_name, sysprof_description)

#endif
