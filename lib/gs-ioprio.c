/*
 * Copyright (C) 2005, Novell, Inc.
 * Copyright (C) 2006, Jamie McCracken <jamiemcc@gnome.org>
 * Copyright (C) 2006, Anders Aagaard
 *
 * Based mostly on code by Robert Love <rml@novell.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"

#ifdef __linux__

#include <stdio.h>
#include <errno.h>

#ifdef HAVE_LINUX_UNISTD_H
#include <linux/unistd.h>
#endif

#include <sys/syscall.h>
#include <unistd.h>

#include <glib/gstdio.h>

#endif /* __linux__ */

#include "gs-ioprio.h"

/* We assume ALL linux architectures have the syscalls defined here */
#ifdef __linux__

/* Make sure the system call is supported */
#ifndef __NR_ioprio_set

#if defined(__i386__)
#define __NR_ioprio_set                 289
#define __NR_ioprio_get                 290
#elif defined(__powerpc__) || defined(__powerpc64__)
#define __NR_ioprio_set                 273
#define __NR_ioprio_get                 274
#elif defined(__x86_64__)
#define __NR_ioprio_set                 251
#define __NR_ioprio_get                 252
#elif defined(__ia64__)
#define __NR_ioprio_set                 1274
#define __NR_ioprio_get                 1275
#elif defined(__alpha__)
#define __NR_ioprio_set                 442
#define __NR_ioprio_get                 443
#elif defined(__s390x__) || defined(__s390__)
#define __NR_ioprio_set                 282
#define __NR_ioprio_get                 283
#elif defined(__SH4__)
#define __NR_ioprio_set                 288
#define __NR_ioprio_get                 289
#elif defined(__SH5__)
#define __NR_ioprio_set                 316
#define __NR_ioprio_get                 317
#elif defined(__sparc__) || defined(__sparc64__)
#define __NR_ioprio_set                 196
#define __NR_ioprio_get                 218
#elif defined(__arm__)
#define __NR_ioprio_set                 314
#define __NR_ioprio_get                 315
#else
#error "Unsupported architecture!"
#endif

#endif /* __NR_ioprio_set */

enum {
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE,
};

enum {
	IOPRIO_WHO_PROCESS = 1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER,
};

#define IOPRIO_CLASS_SHIFT 13

static inline int
ioprio_set (int which, int who, int ioprio_val)
{
	return syscall (__NR_ioprio_set, which, who, ioprio_val);
}

static int
set_io_priority_idle (void)
{
	int ioprio, ioclass;

	ioprio = 7; /* priority is ignored with idle class */
	ioclass = IOPRIO_CLASS_IDLE << IOPRIO_CLASS_SHIFT;

	return ioprio_set (IOPRIO_WHO_PROCESS, 0, ioprio | ioclass);
}

static int
set_io_priority_best_effort (int ioprio_val)
{
	int ioclass;

	ioclass = IOPRIO_CLASS_BE << IOPRIO_CLASS_SHIFT;

	return ioprio_set (IOPRIO_WHO_PROCESS, 0, ioprio_val | ioclass);
}

void
gs_ioprio_init (void)
{
	if (set_io_priority_idle () == -1) {
		g_message ("Could not set idle IO priority, attempting best effort of 7");

		if (set_io_priority_best_effort (7) == -1) {
			g_message ("Could not set best effort IO priority either, giving up");
		}
	}
}

#else  /* __linux__ */

void
gs_ioprio_init (void)
{
}

#endif /* __linux__ */
