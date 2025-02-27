/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2022 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/**
 * SECTION:gs-app-permissions
 * @short_description: A representation of the permissions requested by an app
 *
 * #GsAppPermissions is an object to represent the permissions requested by an app.
 *
 * While some common permissions are handled with the #GsAppPermissionsFlags,
 * the object allows more detailed permissions to be represented, such as
 * specific file system path access.
 *
 * Since: 43
 */

#include "config.h"

#include <stdlib.h>

#include <glib.h>
#include <glib-object.h>

#include "gs-app-permissions.h"

/**
 * gs_bus_policy_new:
 * @bus_type: bus type this applies to
 * @bus_name: bus name or prefix (such as `org.gtk.vfs.*`) this applies to
 * @permission: permissions granted
 *
 * Create a new #GsBusPolicy.
 *
 * Returns: (transfer full): a new #GsBusPolicy
 * Since: 49
 */
GsBusPolicy *
gs_bus_policy_new (GBusType               bus_type,
                   const char            *bus_name,
                   GsBusPolicyPermission  permission)
{
	g_autoptr(GsBusPolicy) policy = NULL;

	g_return_val_if_fail (bus_type != G_BUS_TYPE_NONE, NULL);
	g_return_val_if_fail (bus_name != NULL && *bus_name != '\0', NULL);

	policy = g_new0 (GsBusPolicy, 1);
	policy->bus_type = bus_type;
	policy->bus_name = g_strdup (bus_name);
	policy->permission = permission;

	return g_steal_pointer (&policy);
}

/**
 * gs_bus_policy_free:
 * @self: (transfer full): a #GsBusPolicy
 *
 * Free a #GsBusPolicy.
 *
 * Since: 49
 */
void
gs_bus_policy_free (GsBusPolicy *self)
{
	g_return_if_fail (self != NULL);

	g_free (self->bus_name);
	g_free (self);
}

#define DOES_NOT_CONTAIN ((guint) ~0)

struct _GsAppPermissions
{
	GObject parent;

	gboolean is_sealed;
	GsAppPermissionsFlags flags;
	GPtrArray *filesystem_read; /* (owner) (nullable) (element-type utf8) */
	GPtrArray *filesystem_full; /* (owner) (nullable) (element-type utf8) */
	GPtrArray *bus_policies;  /* (owned) (nullable) (element-type GsBusPolicy) */
};

G_DEFINE_TYPE (GsAppPermissions, gs_app_permissions, G_TYPE_OBJECT)

static gint
cmp_filename_pointers (gconstpointer item1,
                       gconstpointer item2)
{
	const gchar * const *pitem1 = item1;
	const gchar * const *pitem2 = item2;
	return strcmp (*pitem1, *pitem2);
}

static int
bus_policy_compare (const GsBusPolicy *policy1,
                    const GsBusPolicy *policy2)
{
	if (policy1->bus_type != policy2->bus_type)
		return policy1->bus_type - policy2->bus_type;

	return strcmp (policy1->bus_name, policy2->bus_name);
}

static int
bus_policy_compare_lopsided_with_permissions (const GsBusPolicy *policy1,
                                              const GsBusPolicy *policy2)
{
	int compare_keys = bus_policy_compare (policy1, policy2);

	if (compare_keys != 0)
		return compare_keys;

	/* If the two policies have the same keys, they might differ by value
	 * (GsBusPolicy.permission). If so, we want to flag to the user if
	 * @policy2 has a higher permission, but not if it has a lower permission.
	 * Consequently, *this function is not symmetric on its inputs* and
	 * can’t be used for sorting things.  */
	if (policy2->permission > policy1->permission)
		return 1;

	return 0;
}

static int
cmp_bus_policy_qsort (const void *item1,
                      const void *item2)
{
	const GsBusPolicy * const *pitem1 = item1;
	const GsBusPolicy * const *pitem2 = item2;

	return bus_policy_compare (*pitem1, *pitem2);
}

static GsBusPolicy *
bus_policy_copy (const GsBusPolicy *policy,
                 void              *user_data)
{
	return gs_bus_policy_new (policy->bus_type,
				  policy->bus_name,
				  policy->permission);
}

static void
gs_app_permissions_finalize (GObject *object)
{
	GsAppPermissions *self = GS_APP_PERMISSIONS (object);

	g_clear_pointer (&self->filesystem_read, g_ptr_array_unref);
	g_clear_pointer (&self->filesystem_full, g_ptr_array_unref);
	g_clear_pointer (&self->bus_policies, g_ptr_array_unref);

	G_OBJECT_CLASS (gs_app_permissions_parent_class)->finalize (object);
}

static void
gs_app_permissions_class_init (GsAppPermissionsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = gs_app_permissions_finalize;
}

static void
gs_app_permissions_init (GsAppPermissions *self)
{
}

/**
 * gs_app_permissions_new:
 *
 * Create a new #GsAppPermissions containing the app permissions.
 *
 * Returns: (transfer full): a new #GsAppPermissions
 * Since: 43
 */
GsAppPermissions *
gs_app_permissions_new (void)
{
	return g_object_new (GS_TYPE_APP_PERMISSIONS, NULL);
}

/**
 * gs_app_permissions_seal:
 * @self: a #GsAppPermissions
 *
 * Seal the @self. After being called, no modifications can be
 * done on the @self.
 *
 * Since: 43
 **/
void
gs_app_permissions_seal (GsAppPermissions *self)
{
	g_return_if_fail (GS_IS_APP_PERMISSIONS (self));

	if (self->is_sealed)
		return;

	self->is_sealed = TRUE;

	/* Sort the arrays, which will help with searching */
	if (self->filesystem_read)
		qsort (self->filesystem_read->pdata, self->filesystem_read->len, sizeof (gpointer), cmp_filename_pointers);

	if (self->filesystem_full)
		qsort (self->filesystem_full->pdata, self->filesystem_full->len, sizeof (gpointer), cmp_filename_pointers);

	if (self->bus_policies)
		qsort (self->bus_policies->pdata, self->bus_policies->len, sizeof (gpointer), cmp_bus_policy_qsort);
}

/**
 * gs_app_permissions_is_sealed:
 * @self: a #GsAppPermissions
 *
 * Checks whether the @self had been sealed. Once the @self is sealed,
 * no modifications can be made to it.
 *
 * Returns: whether the @self had been sealed
 *
 * Since: 43
 **/
gboolean
gs_app_permissions_is_sealed (GsAppPermissions *self)
{
	g_return_val_if_fail (GS_IS_APP_PERMISSIONS (self), TRUE);

	return self->is_sealed;
}

/**
 * gs_app_permissions_is_empty:
 * @self: a #GsAppPermissions
 *
 * Gets whether the #GsAppPermissions is empty, i.e. the app is requesting no
 * permissions.
 *
 * This function works regardless of whether the #GsAppPermissions is sealed.
 *
 * Returns: true if the #GsAppPermissions is empty, false otherwise
 * Since: 48
 */
gboolean
gs_app_permissions_is_empty (GsAppPermissions *self)
{
	g_return_val_if_fail (GS_IS_APP_PERMISSIONS (self), TRUE);

	return (self->flags == GS_APP_PERMISSIONS_FLAGS_NONE &&
		(self->filesystem_read == NULL || self->filesystem_read->len == 0) &&
		(self->filesystem_full == NULL || self->filesystem_full->len == 0) &&
		(self->bus_policies == NULL || self->bus_policies->len == 0));
}

/* Works out (array2 - array1), i.e. returns all the elements of @array2 which
 * aren’t in @array1. @array1 and @array2 are required to be sorted in advance.
 * A `NULL` array is considered equal to an empty array, and `NULL` will be
 * returned if the resulting array is empty.
 * @array1 and @array2 are not modified, and any array elements in the result
 * are copied. */
static GPtrArray *
ptr_array_diff (GPtrArray      *array1,
                GPtrArray      *array2,
                GCompareFunc    compare_func,
                GCopyFunc       copy_func,
                void           *copy_func_data,
                GDestroyNotify  free_func)
{
	g_autoptr(GPtrArray) diff = NULL;

	if (array2 == NULL)
		return NULL;
	if (array1 == NULL)
		return g_ptr_array_copy (array2, copy_func, copy_func_data);

	diff = g_ptr_array_new_with_free_func (free_func);

	/* Walk both arrays in parallel, comparing the elements. As both arrays
	 * are guaranteed to be sorted, this means that if an element on one
	 * array compares less than the element on the other array, that element
	 * isn’t present in the other array.
	 *
	 * This loop is guaranteed to terminate as at least one of the indices
	 * is incremented on each iteration. */
	for (unsigned int index1 = 0, index2 = 0;
	     index1 < array1->len || index2 < array2->len;
	     ) {
		const void *element1, *element2;
		int comparison;

		if (index1 >= array1->len) {
			element2 = g_ptr_array_index (array2, index2);
			comparison = 1;
		} else if (index2 >= array2->len) {
			element1 = g_ptr_array_index (array1, index1);
			comparison = -1;
		} else {
			element1 = g_ptr_array_index (array1, index1);
			element2 = g_ptr_array_index (array2, index2);
			comparison = compare_func (element1, element2);
		}

		if (comparison < 0) {
			/* @element1 isn’t present in @array2 */
			index1++;
		} else if (comparison == 0) {
			/* Element is present in both */
			index1++;
			index2++;
		} else {
			/* @element2 isn’t present in @array1 */
			g_ptr_array_add (diff, copy_func (element2, copy_func_data));
			index2++;
		}
	}

	return (diff->len > 0) ? g_steal_pointer (&diff) : NULL;
}

/**
 * gs_app_permissions_diff:
 * @self: a #GsAppPermissions
 * @other: another #GsAppPermissions
 *
 * Calculate the difference between two #GsAppPermissions instances.
 *
 * This effectively calculates (`other` - `self`), i.e. it returns all the
 * permissions which are set in @other but not set in @self.
 *
 * The returned #GsAppPermissions will be sealed. Both @self and @other must be
 * sealed before calling this function.
 *
 * Returns: (transfer full): difference between @other and @self
 * Since: 48
 */
GsAppPermissions *
gs_app_permissions_diff (GsAppPermissions *self,
                         GsAppPermissions *other)
{
	g_autoptr(GsAppPermissions) diff = gs_app_permissions_new ();
	const GPtrArray *new_paths;

	g_return_val_if_fail (GS_IS_APP_PERMISSIONS (self), NULL);
	g_return_val_if_fail (self->is_sealed, NULL);
	g_return_val_if_fail (GS_IS_APP_PERMISSIONS (other), NULL);
	g_return_val_if_fail (other->is_sealed, NULL);

	/* Flags */
	gs_app_permissions_set_flags (diff, other->flags & ~self->flags);

	/* File access */
	new_paths = gs_app_permissions_get_filesystem_read (other);
	for (unsigned int i = 0; new_paths != NULL && i < new_paths->len; i++) {
		const char *new_path = g_ptr_array_index (new_paths, i);
		if (!gs_app_permissions_contains_filesystem_read (self, new_path))
			gs_app_permissions_add_filesystem_read (diff, new_path);
	}

	new_paths = gs_app_permissions_get_filesystem_full (other);
	for (unsigned int i = 0; new_paths != NULL && i < new_paths->len; i++) {
		const char *new_path = g_ptr_array_index (new_paths, i);
		if (!gs_app_permissions_contains_filesystem_full (self, new_path))
			gs_app_permissions_add_filesystem_full (diff, new_path);
	}

	/* Bus policies. Use a special comparison function which will highlight
	 * if any of the other->bus_policies have a higher permission than a
	 * corresponding bus policy in self->bus_policies (but not the other
	 * way round). */
	diff->bus_policies = ptr_array_diff (self->bus_policies, other->bus_policies,
					     (GCompareFunc) bus_policy_compare_lopsided_with_permissions,
					     (GCopyFunc) bus_policy_copy, NULL,
					     (GDestroyNotify) gs_bus_policy_free);

	gs_app_permissions_seal (diff);

	return g_steal_pointer (&diff);
}

/**
 * gs_app_permissions_set_flags:
 * @self: a #GsAppPermissions
 * @flags: a #GsAppPermissionsFlags to set
 *
 * Set the permission flags, overwriting any previously set flags.
 * Compare to gs_app_permissions_add_flag() and
 * gs_app_permissions_remove_flag().
 *
 * Since: 43
 */
void
gs_app_permissions_set_flags (GsAppPermissions *self,
			      GsAppPermissionsFlags flags)
{
	g_return_if_fail (GS_IS_APP_PERMISSIONS (self));

	g_assert (!self->is_sealed);

	self->flags = flags;
}

/**
 * gs_app_permissions_get_flags:
 * @self: a #GsAppPermissions
 *
 * Get the permission flags.
 *
 * Returns: the permission flags
 * Since: 43
 */
GsAppPermissionsFlags
gs_app_permissions_get_flags (GsAppPermissions *self)
{
	g_return_val_if_fail (GS_IS_APP_PERMISSIONS (self), GS_APP_PERMISSIONS_FLAGS_NONE);

	return self->flags;
}

/**
 * gs_app_permissions_add_flag:
 * @self: a #GsAppPermissions
 * @flags: a #GsAppPermissionsFlags to add
 *
 * Add the @flags into the already set flags. The @flags cannot be
 * #GS_APP_PERMISSIONS_FLAGS_NONE.
 * To set that use gs_app_permissions_set_flags() instead.
 *
 * In case the current flags contain #GS_APP_PERMISSIONS_FLAGS_NONE, it's
 * automatically unset.
 *
 * Since: 43
 */
void
gs_app_permissions_add_flag (GsAppPermissions *self,
			     GsAppPermissionsFlags flags)
{
	g_return_if_fail (GS_IS_APP_PERMISSIONS (self));
	g_return_if_fail (flags != GS_APP_PERMISSIONS_FLAGS_NONE);

	g_assert (!self->is_sealed);

	self->flags = self->flags | flags;
}

/**
 * gs_app_permissions_remove_flag:
 * @self: a #GsAppPermissions
 * @flags: a #GsAppPermissionsFlags to remove
 *
 * Remove the @flags from the already set flags. The @flags cannot be
 * #GS_APP_PERMISSIONS_FLAGS_NONE.
 * To set this use gs_app_permissions_set_flags() instead.
 *
 * In case the result of the removal would lead to no flag set the #GS_APP_PERMISSIONS_FLAGS_NONE
 * is set automatically.
 *
 * Since: 43
 */
void
gs_app_permissions_remove_flag (GsAppPermissions *self,
				GsAppPermissionsFlags flags)
{
	g_return_if_fail (GS_IS_APP_PERMISSIONS (self));
	g_return_if_fail (flags != GS_APP_PERMISSIONS_FLAGS_NONE);

	g_assert (!self->is_sealed);

	self->flags = (self->flags & (~flags));
}

static guint
app_permissions_get_array_index (GPtrArray *array,
				 const gchar *filename)
{
	g_return_val_if_fail (filename != NULL, DOES_NOT_CONTAIN);

	if (array == NULL)
		return DOES_NOT_CONTAIN;

	for (guint i = 0; i < array->len; i++) {
		const gchar *item = g_ptr_array_index (array, i);
		if (g_strcmp0 (item, filename) == 0)
			return 0;
	}

	return DOES_NOT_CONTAIN;
}

/**
 * gs_app_permissions_add_filesystem_read:
 * @self: a #GsAppPermissions
 * @filename: a filename to access
 *
 * Add @filename as a file to access for read. The @filename
 * can be either a path or a localized pretty name of it, like "Documents".
 * The addition is ignored in case the same @filename is part of
 * the read or full access file names.
 *
 * Since: 43
 */
void
gs_app_permissions_add_filesystem_read (GsAppPermissions *self,
					const gchar *filename)
{
	g_return_if_fail (GS_IS_APP_PERMISSIONS (self));
	g_return_if_fail (filename != NULL);

	g_assert (!self->is_sealed);

	/* Already known */
	if (app_permissions_get_array_index (self->filesystem_read, filename) != DOES_NOT_CONTAIN ||
	    app_permissions_get_array_index (self->filesystem_full, filename) != DOES_NOT_CONTAIN)
		return;

	if (self->filesystem_read == NULL)
		self->filesystem_read = g_ptr_array_new_with_free_func (g_free);

	g_ptr_array_add (self->filesystem_read, g_strdup (filename));
}

/**
 * gs_app_permissions_get_filesystem_read:
 * @self: a #GsAppPermissions
 *
 * Get the list of filesystem file names requested for read access using
 * gs_app_permissions_add_filesystem_read().
 * The array is owned by the @self and should not be modified by any way.
 * It can be %NULL, when no file access was set.
 *
 * Returns: (nullable) (transfer none) (element-type utf8): an array of
 *    file names requesting read access or %NULL, when none was set.
 *
 * Since: 43
 */
const GPtrArray *
gs_app_permissions_get_filesystem_read (GsAppPermissions *self)
{
	g_return_val_if_fail (GS_IS_APP_PERMISSIONS (self), NULL);

	return self->filesystem_read;
}

static gboolean
array_contains_filename (GPtrArray *array,
			 const gchar *filename)
{
	if (array == NULL)
		return FALSE;

	return bsearch (&filename, array->pdata, array->len, sizeof (gpointer), cmp_filename_pointers) != NULL;
}

/**
 * gs_app_permissions_contains_filesystem_read:
 * @self: a #GsAppPermissions
 * @filename: a file name to search for
 *
 * Checks whether the @filename is included in the filesystem read permissions.
 * This can be called only after the @self is sealed.
 *
 * Returns: whether the @filename is part of the filesystem read permissions
 *
 * Since: 43
 **/
gboolean
gs_app_permissions_contains_filesystem_read (GsAppPermissions *self,
					     const gchar *filename)
{
	g_return_val_if_fail (GS_IS_APP_PERMISSIONS (self), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (self->is_sealed, FALSE);

	return array_contains_filename (self->filesystem_read, filename);
}

/**
 * gs_app_permissions_add_filesystem_full:
 * @self: a #GsAppPermissions
 * @filename: a filename to access
 *
 * Add @filename as a file to access for read and write. The @filename
 * can be either a path or a localized pretty name of it, like "Documents".
 * The addition is ignored in case the same @filename is include in the list
 * already. The @filename is removed from the read list, if it's part of it.
 *
 * Since: 43
 */
void
gs_app_permissions_add_filesystem_full (GsAppPermissions *self,
					const gchar *filename)
{
	guint read_index;

	g_return_if_fail (GS_IS_APP_PERMISSIONS (self));
	g_return_if_fail (filename != NULL);

	g_assert (!self->is_sealed);

	/* Already known */
	if (app_permissions_get_array_index (self->filesystem_full, filename) != DOES_NOT_CONTAIN)
		return;

	if (self->filesystem_full == NULL)
		self->filesystem_full = g_ptr_array_new_with_free_func (g_free);

	g_ptr_array_add (self->filesystem_full, g_strdup (filename));

	/* Remove from the read list and free the read list if becomes empty */
	read_index = app_permissions_get_array_index (self->filesystem_read, filename);
	if (read_index != DOES_NOT_CONTAIN) {
		g_ptr_array_remove_index (self->filesystem_read, read_index);
		if (self->filesystem_read->len == 0)
			g_clear_pointer (&self->filesystem_read, g_ptr_array_unref);
	}
}

/**
 * gs_app_permissions_get_filesystem_full:
 * @self: a #GsAppPermissions
 *
 * Get the list of filesystem file names requested for read and write access using
 * gs_app_permissions_add_filesystem_full().
 * The array is owned by the @self and should not be modified by any way.
 * It can be %NULL, when no file access was set.
 *
 * Returns: (nullable) (transfer none) (element-type utf8): an array of
 *    file names requesting read and write access or %NULL, when none was set.
 *
 * Since: 43
 */
const GPtrArray *
gs_app_permissions_get_filesystem_full (GsAppPermissions *self)
{
	g_return_val_if_fail (GS_IS_APP_PERMISSIONS (self), NULL);

	return self->filesystem_full;
}

/**
 * gs_app_permissions_contains_filesystem_full:
 * @self: a #GsAppPermissions
 * @filename: a file name to search for
 *
 * Checks whether the @filename is included in the filesystem full permissions.
 * This can be called only after the @self is sealed.
 *
 * Returns: whether the @filename is part of the filesystem full permissions
 *
 * Since: 43
 **/
gboolean
gs_app_permissions_contains_filesystem_full (GsAppPermissions *self,
					     const gchar *filename)
{
	g_return_val_if_fail (GS_IS_APP_PERMISSIONS (self), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (self->is_sealed, FALSE);

	return array_contains_filename (self->filesystem_full, filename);
}

/**
 * gs_app_permissions_add_bus_policy:
 * @self: a #GsAppPermissions
 * @bus_type: the type of bus this policy applies to
 * @bus_name: the bus name the policy applies to
 * @policy: the policy
 *
 * Add @policy as the access policy for @bus_name on @bus_type.
 *
 * The @bus_name is either a D-Bus well-known name or a prefix of one (such as
 * `org.gtk.vfs.*`). Policies are indexed by both @bus_type and @bus_name. If a
 * policy would be a duplicate, the higher @permission of the original and new
 * policy is stored.
 *
 * Since: 49
 */
void
gs_app_permissions_add_bus_policy (GsAppPermissions      *self,
                                   GBusType               bus_type,
                                   const char            *bus_name,
                                   GsBusPolicyPermission  permission)
{
	g_return_if_fail (GS_IS_APP_PERMISSIONS (self));
	g_return_if_fail (bus_type != G_BUS_TYPE_NONE);
	g_return_if_fail (bus_name != NULL && *bus_name != '\0');
	g_return_if_fail (permission != GS_BUS_POLICY_PERMISSION_UNKNOWN);

	g_assert (!self->is_sealed);

	/* Already known? */
	for (unsigned int i = 0; self->bus_policies != NULL && i < self->bus_policies->len; i++) {
		GsBusPolicy *policy = g_ptr_array_index (self->bus_policies, i);

		if (policy->bus_type == bus_type &&
		    g_str_equal (policy->bus_name, bus_name)) {
			policy->permission = MAX (policy->permission, permission);
			return;
		}
	}

	/* Ignore no-op policies */
	if (permission == GS_BUS_POLICY_PERMISSION_NONE)
		return;

	if (self->bus_policies == NULL)
		self->bus_policies = g_ptr_array_new_with_free_func ((GDestroyNotify) gs_bus_policy_free);

	g_ptr_array_add (self->bus_policies, gs_bus_policy_new (bus_type, bus_name, permission));
}

/**
 * gs_app_permissions_get_bus_policies:
 * @self: a #GsAppPermissions
 * @out_n_bus_policies: (out caller-allocates) (optional): return location for
 *    the number of bus policies, or %NULL to ignore
 *
 * Get the bus policies stored on this #GsAppPermissions.
 *
 * Note that if %GS_APP_PERMISSIONS_FLAGS_SYSTEM_BUS or
 * %GS_APP_PERMISSIONS_FLAGS_SESSION_BUS are set, it’s unlikely that there will
 * be any bus policies to return here, as those flags indicate unfiltered access
 * to the buses is allowed for the app.
 *
 * Otherwise, each #GsBusPolicy returned here indicates a sandbox hole granting
 * the app permission to interact with that bus service in some way.
 *
 * Returns: (nullable) (transfer none): array of #GsBusPolicy instances, which
 *    must not be modified; may be %NULL if there are no policies
 * Since: 49
 */
const GsBusPolicy * const *
gs_app_permissions_get_bus_policies (GsAppPermissions *self,
                                     size_t           *out_n_bus_policies)
{
	g_return_val_if_fail (GS_IS_APP_PERMISSIONS (self), NULL);
	g_return_val_if_fail (self->is_sealed, NULL);

	if (out_n_bus_policies != NULL)
		*out_n_bus_policies = (self->bus_policies != NULL) ? self->bus_policies->len : 0;

	return (self->bus_policies != NULL && self->bus_policies->len > 0) ? (const GsBusPolicy * const *) self->bus_policies->pdata : NULL;
}
