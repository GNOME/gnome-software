/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2026 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * SECTION:gs-silo-wrapper
 * @title: GsSiloWrapper
 * @include: gnome-software.h
 * @stability: Unstable
 * @short_description: A thread-safe XbSilo wrapper
 *
 * The XbSilo contains valid data only until the underlying file (which
 * is mmap-ed into the memory) does not change. Adding a reference
 * on the XbSilo instance is not enough to make it work properly.
 *
 * The GsSiloWrapper object wraps the XbSilo in a thread-safe way, thus
 * when there's a need to rebuild the XbSilo, them a calling thread waits
 * for all the readers to release the wrapper object to regenerate it.
 * There can be more users of the wrapper at the same time.
 *
 * The way of work with the wrapper is to create one at the start, with
 * provided GsSiloWrapperBuildFunc rebuild function. Then call gs_silo_wrapper_acquire()
 * to refresh the silo if needed and to obtain a read access to the wrapper's
 * members. Once finished with the silo call gs_silo_wrapper_release(), which
 * can signal any pending threads that the readers of it are finished
 * and the content can be rebuilt, if needed. When the acquire is called
 * on a valid silo wrapper, it grants the read access to the wrapper
 * members and returns immediately.
 *
 * Since: 50
 **/
#include "config.h"

#include <glib-object.h>
#include <xmlb.h>

#include "gs-silo-wrapper.h"

struct _GsSiloWrapper
{
	GObject parent_instance;

	GMutex mutex;
	GCond cond;

	GsSiloWrapperBuildFunc build_func;  /* (not nullable) */
	gpointer user_data;
	GDestroyNotify free_user_data;  /* (nullable) */

	guint n_acquired; /* current active users, those whom acquired and did not release yet */
	gboolean building;

	XbSilo *silo;  /* (owned) (nullable) */
	gchar *filename;  /* (owned) (nullable) */
	GHashTable *installed_by_desktopid; /* (element-type utf8 GPtrArray (element-type XbNode)) (owned) (nullable) */
	AsComponentScope scope;

	GPtrArray *file_monitors; /* (owned) (element-type GFileMonitor) */
	/* The stamps help to avoid locking the silo lock in the main thread
	   and also to detect changes while loading other appstream data. */
	gint change_stamp; /* the silo change stamp, increased on every silo change */
	gint change_stamp_current; /* the currently known silo change stamp, checked for changes */
};

G_DEFINE_TYPE (GsSiloWrapper, gs_silo_wrapper, G_TYPE_OBJECT)

static gboolean
gs_silo_wrapper_build (GsSiloWrapper *self,
		       gboolean interactive,
		       GCancellable *cancellable,
		       GError **error)
{
	g_autoptr(GMainContext) old_thread_default = NULL;

	g_assert (!self->building);

	/* FIXME: https://gitlab.gnome.org/GNOME/gnome-software/-/issues/1422 */
	old_thread_default = g_main_context_ref_thread_default ();
	if (old_thread_default == g_main_context_default ())
		g_clear_pointer (&old_thread_default, g_main_context_unref);
	if (old_thread_default != NULL)
		g_main_context_pop_thread_default (old_thread_default);

	self->building = TRUE;

	do {
		g_clear_object (&self->silo);
		g_clear_pointer (&self->filename, g_free);
		g_clear_pointer (&self->installed_by_desktopid, g_hash_table_unref);
		self->scope = AS_COMPONENT_SCOPE_UNKNOWN;
		g_ptr_array_set_size (self->file_monitors, 0);
		g_atomic_int_set (&self->change_stamp_current, g_atomic_int_get (&self->change_stamp));

		self->silo = self->build_func (self, interactive, self->user_data, cancellable, error);

		if (self->silo != NULL) {
			g_autoptr(GPtrArray) installed = NULL;
			g_autoptr(XbNode) node = NULL;

			self->installed_by_desktopid = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) g_ptr_array_unref);

			installed = xb_silo_query (self->silo, "/component[@type='desktop-application']/launchable[@type='desktop-id']", 0, NULL);
			for (guint i = 0; installed != NULL && i < installed->len; i++) {
				XbNode *launchable = g_ptr_array_index (installed, i);
				const gchar *id = xb_node_get_text (launchable);
				if (id != NULL && *id != '\0') {
					GPtrArray *nodes = g_hash_table_lookup (self->installed_by_desktopid, id);
					if (nodes == NULL) {
						nodes = g_ptr_array_new_with_free_func (g_object_unref);
						g_hash_table_insert (self->installed_by_desktopid, g_strdup (id), nodes);
					}
					g_ptr_array_add (nodes, xb_node_get_parent (launchable));
				}
			}

			node = xb_silo_query_first (self->silo, "info", NULL);
			if (node != NULL) {
				g_autoptr(XbNode) child = NULL;
				g_autoptr(XbNode) next = NULL;
				for (child = xb_node_get_child (node);
				     child != NULL && (self->filename == NULL || self->scope == AS_COMPONENT_SCOPE_UNKNOWN);
				     g_object_unref (child), child = g_steal_pointer (&next)) {
					const gchar *elem = xb_node_get_element (child);
					next = xb_node_get_next (child);
					if (self->filename == NULL && g_strcmp0 (elem, "filename") == 0) {
						self->filename = g_strdup (xb_node_get_text (child));
					} else if (self->scope == AS_COMPONENT_SCOPE_UNKNOWN && g_strcmp0 (elem, "scope") == 0) {
						const gchar *tmp = xb_node_get_text (child);
						if (tmp != NULL)
							self->scope = as_component_scope_from_string (tmp);
					}
				}
			}
		}
	} while (self->silo != NULL && g_atomic_int_get (&self->change_stamp_current) != g_atomic_int_get (&self->change_stamp));

	self->building = FALSE;

	/* FIXME: https://gitlab.gnome.org/GNOME/gnome-software/-/issues/1422 */
	if (old_thread_default != NULL)
		g_main_context_push_thread_default (old_thread_default);

	return self->silo != NULL;
}

static void
gs_silo_wrapper_finalize (GObject *object)
{
	GsSiloWrapper *self = GS_SILO_WRAPPER (object);

	if (self->free_user_data != NULL)
		self->free_user_data (self->user_data);

	g_mutex_clear (&self->mutex);
	g_cond_clear (&self->cond);
	g_clear_pointer (&self->file_monitors, g_ptr_array_unref);
	g_clear_pointer (&self->filename, g_free);
	g_clear_pointer (&self->installed_by_desktopid, g_hash_table_unref);

	G_OBJECT_CLASS (gs_silo_wrapper_parent_class)->finalize (object);
}

static void
gs_silo_wrapper_class_init (GsSiloWrapperClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_silo_wrapper_finalize;
}

static void
gs_silo_wrapper_init (GsSiloWrapper *self)
{
	g_mutex_init (&self->mutex);
	g_cond_init (&self->cond);

	self->file_monitors = g_ptr_array_new_with_free_func (g_object_unref);

	/* it needs rebuild at the start */
	self->change_stamp = 0;
	self->change_stamp_current = 1;
}

/**
 * gs_silo_wrapper_new:
 * @build_func: (scope notified) (closure user_data): a #GsSiloWrapperBuildFunc callback
 * @user_data: (destroy free_user_data): user data passed to @build_func
 * @free_user_data: (nullable): a function to free @user_data, or %NULL
 *
 * Creates a new #GsSiloWrapper. The @build_func is called every time the silo
 * needs to be rebuilt.
 *
 * Returns: (transfer full): a new #GsSiloWrapper
 *
 * Since: 50
 **/
GsSiloWrapper *
gs_silo_wrapper_new (GsSiloWrapperBuildFunc build_func,
		     gpointer user_data,
		     GDestroyNotify free_user_data)
{
	GsSiloWrapper *self;

	g_assert (build_func != NULL);

	self = g_object_new (GS_TYPE_SILO_WRAPPER, NULL);

	self->build_func = build_func;
	self->user_data = user_data;
	self->free_user_data = free_user_data;

	return self;
}

static void
gs_silo_wrapper_file_monitor_changed_cb (GFileMonitor *monitor,
					 GFile *file,
					 GFile *other_file,
					 GFileMonitorEvent event_type,
					 gpointer user_data)
{
	GsSiloWrapper *self = user_data;

	gs_silo_wrapper_invalidate (self);
}

/**
 * gs_silo_wrapper_add_file_monitor:
 * @self: a #GsSiloWrapper
 * @file_monitor: (transfer none): a #GFileMonitor
 *
 * Adds the @file_monitor as a file monitor, which
 * on change invalidates the @self. The @file_monitor
 * can be %NULL, then the function does nothing and returns.
 *
 * This function can be called only from within the build_func
 * passed to the gs_silo_wrapper_new().
 *
 * Since: 50
 **/
void
gs_silo_wrapper_add_file_monitor (GsSiloWrapper *self,
				  GFileMonitor *file_monitor)
{
	g_return_if_fail (GS_IS_SILO_WRAPPER (self));
	g_return_if_fail (self->building);
	g_return_if_fail (G_IS_FILE_MONITOR (file_monitor));

	g_signal_connect_object (file_monitor, "changed",
		G_CALLBACK (gs_silo_wrapper_file_monitor_changed_cb), self, 0);

	g_ptr_array_add (self->file_monitors, g_object_ref (file_monitor));
}

/**
 * gs_silo_wrapper_acquire:
 * @self: a #GsSiloWrapper
 * @interactive: whether the call is part of an interactive job
 * @cancellable: a #GCancellable, or %NULL
 * @error: return locationfor a #GError, or %NULL
 *
 * Acquires read access on the @self members. If needed, refreshes
 * the underlying silo and all the members before returning, waiting
 * for already acquired users to finish first.
 *
 * Call gs_silo_wrapper_release() when the silo or other members
 * are not needed anymore.
 *
 * Returns: whether succeeded, which means whether was able to acquire
 *    the @self
 *
 * Since: 50
 **/
gboolean
gs_silo_wrapper_acquire (GsSiloWrapper *self,
			 gboolean interactive,
			 GCancellable *cancellable,
			 GError **error)
{
	gboolean success = FALSE;

	g_return_val_if_fail (GS_IS_SILO_WRAPPER (self), FALSE);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	g_mutex_lock (&self->mutex);

	do {
		if (self->silo != NULL && g_atomic_int_get (&self->change_stamp_current) == g_atomic_int_get (&self->change_stamp)) {
			success = TRUE;
		} else if (self->n_acquired == 0) {
			success = gs_silo_wrapper_build (self, interactive, cancellable, error);
			break;
		} else if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
			break;
		} else {
			g_cond_wait (&self->cond, &self->mutex);
		}
	} while (!success);

	if (success)
		self->n_acquired++;

	/* in case more threads had been waiting for a rebuild,
	   to not have starved the other threads */
	g_cond_broadcast (&self->cond);

	g_mutex_unlock (&self->mutex);

	return success;
}

/**
 * gs_silo_wrapper_release:
 * @self: a #GsSiloWrapper
 *
 * A part call to gs_silo_wrapper_acquire(), to release
 * successully acquired @self.
 *
 * Since: 50
 **/
void
gs_silo_wrapper_release (GsSiloWrapper *self)
{
	g_return_if_fail (GS_IS_SILO_WRAPPER (self));

	g_mutex_lock (&self->mutex);

	if (!self->n_acquired) {
		g_assert_not_reached ();
		g_mutex_unlock (&self->mutex);

		return;
	}

	self->n_acquired--;
	g_cond_signal (&self->cond);

	g_mutex_unlock (&self->mutex);
}

/**
 * gs_silo_wrapper_invalidate:
 * @self: a #GsSiloWrapper
 *
 * Marks the @self to need rebuild the next time
 * the gs_silo_wrapper_acquire() is called.
 *
 * It does not invalidate the members of the @self
 * for any current users of the @self.
 *
 * Since: 50
 **/
void
gs_silo_wrapper_invalidate (GsSiloWrapper *self)
{
	g_return_if_fail (GS_IS_SILO_WRAPPER (self));

	g_atomic_int_inc (&self->change_stamp);
}

/**
 * gs_silo_wrapper_get_silo:
 * @self: a #GsSiloWrapper
 *
 * Gets an #XbSilo instance of the @self.
 *
 * Note: The value is valid only after successful call to gs_silo_wrapper_acquire()
 *    and before gs_silo_wrapper_release() is called.
 *
 * Returns: (transfer none): an #XbSilo instance
 *
 * Since: 50
 **/
XbSilo *
gs_silo_wrapper_get_silo (GsSiloWrapper *self)
{
	g_return_val_if_fail (GS_IS_SILO_WRAPPER (self), NULL);

	return self->silo;
}

/**
 * gs_silo_wrapper_get_scope:
 * @self: a #GsSiloWrapper
 *
 * Gets an #AsComponentScope as stored in the silo of the @self.
 * It can return %AS_COMPONENT_SCOPE_UNKNOWN when the silo does
 * not have stored such information.
 *
 * Note: The value is valid only after successful call to gs_silo_wrapper_acquire()
 *    and before gs_silo_wrapper_release() is called.
 *
 * Returns: an #AsComponentScope or %AS_COMPONENT_SCOPE_UNKNOWN when not known
 *
 * Since: 50
 **/
AsComponentScope
gs_silo_wrapper_get_scope (GsSiloWrapper *self)
{
	g_return_val_if_fail (GS_IS_SILO_WRAPPER (self), AS_COMPONENT_SCOPE_UNKNOWN);

	return self->scope;
}

/**
 * gs_silo_wrapper_get_filename:
 * @self: a #GsSiloWrapper
 *
 * Gets a silo filename.
 *
 * Note: The value is valid only after successful call to gs_silo_wrapper_acquire()
 *    and before gs_silo_wrapper_release() is called.
 *
 * Returns: (nullable) (type filename): a silo filename, or %NULL when not known
 *
 * Since: 50
 **/
const gchar *
gs_silo_wrapper_get_filename (GsSiloWrapper *self)
{
	g_return_val_if_fail (GS_IS_SILO_WRAPPER (self), NULL);

	return self->filename;
}

/**
 * gs_silo_wrapper_get_installed_by_desktopid:
 * @self: a #GsSiloWrapper
 *
 * Gets installed components indexed by their desktop ID.
 * The key of the returned hash table is the desktop ID,
 * the value is a #GPtrArray, which contains #XbNode-s
 * of the corresponding components.
 *
 * Note: The value is valid only after successful call to gs_silo_wrapper_acquire()
 *    and before gs_silo_wrapper_release() is called.
 *
 * Returns: (transfer none) (element-type utf8 GPtrArray): installed
 *    components indexed by their desktop ID
 *
 * Since: 50
 **/
GHashTable *
gs_silo_wrapper_get_installed_by_desktopid (GsSiloWrapper *self)
{
	g_return_val_if_fail (GS_IS_SILO_WRAPPER (self), NULL);

	return self->installed_by_desktopid;
}
