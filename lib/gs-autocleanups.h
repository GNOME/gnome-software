/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2019 Kalev Lember <klember@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#pragma once

G_BEGIN_DECLS

#if !GLIB_CHECK_VERSION(2, 61, 1)

/* Backported GRWLock autoptr support for older glib versions */

typedef void GRWLockWriterLocker;

static inline GRWLockWriterLocker *
g_rw_lock_writer_locker_new (GRWLock *rw_lock)
{
	g_rw_lock_writer_lock (rw_lock);
	return (GRWLockWriterLocker *) rw_lock;
}

static inline void
g_rw_lock_writer_locker_free (GRWLockWriterLocker *locker)
{
	g_rw_lock_writer_unlock ((GRWLock *) locker);
}

typedef void GRWLockReaderLocker;

static inline GRWLockReaderLocker *
g_rw_lock_reader_locker_new (GRWLock *rw_lock)
{
	g_rw_lock_reader_lock (rw_lock);
	return (GRWLockReaderLocker *) rw_lock;
}

static inline void
g_rw_lock_reader_locker_free (GRWLockReaderLocker *locker)
{
	g_rw_lock_reader_unlock ((GRWLock *) locker);
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GRWLockWriterLocker, g_rw_lock_writer_locker_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GRWLockReaderLocker, g_rw_lock_reader_locker_free)

#endif

G_END_DECLS
