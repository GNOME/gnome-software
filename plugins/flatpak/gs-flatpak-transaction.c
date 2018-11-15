/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2018 Kalev Lember <klember@redhat.com>
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

#include <config.h>

#include "gs-flatpak-app.h"
#include "gs-flatpak-transaction.h"

struct _GsFlatpakTransaction {
	FlatpakTransaction	 parent_instance;
	FlatpakInstallation	*installation;
	GHashTable		*refhash;	/* ref:GsApp */
	GError			*first_operation_error;
};

enum {
	SIGNAL_REF_TO_APP,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GsFlatpakTransaction, gs_flatpak_transaction, FLATPAK_TYPE_TRANSACTION)

FlatpakInstallation *
gs_flatpak_transaction_get_inst (FlatpakTransaction *transaction)
{
	GsFlatpakTransaction *self = GS_FLATPAK_TRANSACTION (transaction);
	return self->installation;
}

static void
gs_flatpak_transaction_finalize (GObject *object)
{
	GsFlatpakTransaction *self;
	g_return_if_fail (GS_IS_FLATPAK_TRANSACTION (object));
	self = GS_FLATPAK_TRANSACTION (object);

	g_assert (self != NULL);
	g_hash_table_unref (self->refhash);
	if (self->first_operation_error != NULL)
		g_error_free (self->first_operation_error);

	G_OBJECT_CLASS (gs_flatpak_transaction_parent_class)->finalize (object);
}

GsApp *
gs_flatpak_transaction_get_app_by_ref (FlatpakTransaction *transaction, const gchar *ref)
{
	GsFlatpakTransaction *self = GS_FLATPAK_TRANSACTION (transaction);
	return g_hash_table_lookup (self->refhash, ref);
}

static void
gs_flatpak_transaction_add_app_internal (GsFlatpakTransaction *self, GsApp *app)
{
	g_autofree gchar *ref = gs_flatpak_app_get_ref_display (app);
	g_hash_table_insert (self->refhash, g_steal_pointer (&ref), g_object_ref (app));
}

void
gs_flatpak_transaction_add_app (FlatpakTransaction *transaction, GsApp *app)
{
	GsFlatpakTransaction *self = GS_FLATPAK_TRANSACTION (transaction);
	gs_flatpak_transaction_add_app_internal (self, app);
	if (gs_app_get_runtime (app) != NULL)
		gs_flatpak_transaction_add_app_internal (self, gs_app_get_runtime (app));
}

static GsApp *
_ref_to_app (GsFlatpakTransaction *self, const gchar *ref)
{
	GsApp *app = g_hash_table_lookup (self->refhash, ref);
	if (app != NULL)
		return g_object_ref (app);
	g_signal_emit (self, signals[SIGNAL_REF_TO_APP], 0, ref, &app);
	return app;
}

static void
_transaction_operation_set_app (FlatpakTransactionOperation *op, GsApp *app)
{
	g_object_set_data_full (G_OBJECT (op), "GsApp",
				g_object_ref (app), (GDestroyNotify) g_object_unref);
}

static GsApp *
_transaction_operation_get_app (FlatpakTransactionOperation *op)
{
	return g_object_get_data (G_OBJECT (op), "GsApp");
}

gboolean
gs_flatpak_transaction_run (FlatpakTransaction *transaction,
                            GCancellable *cancellable,
                            GError **error)

{
	GsFlatpakTransaction *self = GS_FLATPAK_TRANSACTION (transaction);
	g_autoptr(GError) error_local = NULL;

	if (!flatpak_transaction_run (transaction, cancellable, &error_local)) {
		/* whole transaction failed; restore the state for all the apps involved */
		g_autolist(GObject) ops = flatpak_transaction_get_operations (transaction);
		for (GList *l = ops; l != NULL; l = l->next) {
			FlatpakTransactionOperation *op = l->data;
			const gchar *ref = flatpak_transaction_operation_get_ref (op);
			g_autoptr(GsApp) app = _ref_to_app (self, ref);
			if (app == NULL) {
				g_warning ("failed to find app for %s", ref);
				continue;
			}
			gs_app_set_state_recover (app);
		}

		if (self->first_operation_error != NULL) {
			g_propagate_error (error, g_steal_pointer (&self->first_operation_error));
			return FALSE;
		} else {
			g_propagate_error (error, g_steal_pointer (&error_local));
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean
_transaction_ready (FlatpakTransaction *transaction)
{
	GsFlatpakTransaction *self = GS_FLATPAK_TRANSACTION (transaction);
	g_autolist(GObject) ops = NULL;

	/* nothing to do */
	ops = flatpak_transaction_get_operations (transaction);
	if (ops == NULL)
		return TRUE; // FIXME: error?
	for (GList *l = ops; l != NULL; l = l->next) {
		FlatpakTransactionOperation *op = l->data;
		const gchar *ref = flatpak_transaction_operation_get_ref (op);
		g_autoptr(GsApp) app = _ref_to_app (self, ref);
		if (app != NULL) {
			_transaction_operation_set_app (op, app);
			/* if we're updating a component, then mark all the apps
			 * involved to ensure updating the button state */
			if (flatpak_transaction_operation_get_operation_type (op) ==
					FLATPAK_TRANSACTION_OPERATION_UPDATE)
				gs_app_set_state (app, AS_APP_STATE_INSTALLING);
		}
	}
	return TRUE;
}

static void
_transaction_progress_changed_cb (FlatpakTransactionProgress *progress,
				  gpointer user_data)
{
	GsApp *app = GS_APP (user_data);
	guint percent = flatpak_transaction_progress_get_progress (progress);
	if (flatpak_transaction_progress_get_is_estimating (progress))
		return;
	gs_app_set_progress (app, percent);
}

static const gchar *
_flatpak_transaction_operation_type_to_string (FlatpakTransactionOperationType ot)
{
	if (ot == FLATPAK_TRANSACTION_OPERATION_INSTALL)
		return "install";
	if (ot == FLATPAK_TRANSACTION_OPERATION_UPDATE)
		return "update";
	if (ot == FLATPAK_TRANSACTION_OPERATION_INSTALL_BUNDLE)
		return "install-bundle";
	if (ot == FLATPAK_TRANSACTION_OPERATION_UNINSTALL)
		return "uninstall";
	return NULL;
}

static void
_transaction_new_operation (FlatpakTransaction *transaction,
			    FlatpakTransactionOperation *operation,
			    FlatpakTransactionProgress *progress)
{
	GsApp *app;

	/* find app */
	app = _transaction_operation_get_app (operation);
	if (app == NULL) {
		FlatpakTransactionOperationType ot;
		ot = flatpak_transaction_operation_get_operation_type (operation);
		g_warning ("failed to find app for %s during %s",
			   flatpak_transaction_operation_get_ref (operation),
			   _flatpak_transaction_operation_type_to_string (ot));
		return;
	}

	/* report progress */
	g_signal_connect_object (progress, "changed",
				 G_CALLBACK (_transaction_progress_changed_cb),
				 app, 0);
	flatpak_transaction_progress_set_update_frequency (progress, 100); /* FIXME? */

	/* set app status */
	switch (flatpak_transaction_operation_get_operation_type (operation)) {
	case FLATPAK_TRANSACTION_OPERATION_INSTALL:
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_AVAILABLE);
		gs_app_set_state (app, AS_APP_STATE_INSTALLING);
		break;
	case FLATPAK_TRANSACTION_OPERATION_INSTALL_BUNDLE:
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_AVAILABLE_LOCAL);
		gs_app_set_state (app, AS_APP_STATE_INSTALLING);
		break;
	case FLATPAK_TRANSACTION_OPERATION_UPDATE:
		if (gs_app_get_state (app) == AS_APP_STATE_UNKNOWN)
			gs_app_set_state (app, AS_APP_STATE_UPDATABLE_LIVE);
		gs_app_set_state (app, AS_APP_STATE_INSTALLING);
		break;
	case FLATPAK_TRANSACTION_OPERATION_UNINSTALL:
		gs_app_set_state (app, AS_APP_STATE_REMOVING);
		break;
	default:
		break;
	}
}

static void
_transaction_operation_done (FlatpakTransaction *transaction,
			     FlatpakTransactionOperation *operation,
#if FLATPAK_CHECK_VERSION(1,0,4)
			     const gchar *commit,
#endif
			     FlatpakTransactionResult details)
{
	/* invalidate */
	GsApp *app = _transaction_operation_get_app (operation);
	if (app == NULL) {
		g_warning ("failed to find app for %s",
			   flatpak_transaction_operation_get_ref (operation));
		return;
	}
	switch (flatpak_transaction_operation_get_operation_type (operation)) {
	case FLATPAK_TRANSACTION_OPERATION_INSTALL:
	case FLATPAK_TRANSACTION_OPERATION_INSTALL_BUNDLE:
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		break;
	case FLATPAK_TRANSACTION_OPERATION_UPDATE:
		gs_app_set_version (app, gs_app_get_update_version (app));
		gs_app_set_update_details (app, NULL);
		gs_app_set_update_urgency (app, AS_URGENCY_KIND_UNKNOWN);
		gs_app_set_update_version (app, NULL);
		/* force getting the new runtime */
		gs_app_remove_kudo (app, GS_APP_KUDO_SANDBOXED);
		gs_app_set_state (app, AS_APP_STATE_INSTALLED);
		break;
	case FLATPAK_TRANSACTION_OPERATION_UNINSTALL:
		/* we don't actually know if this app is re-installable */
		gs_flatpak_app_set_commit (app, NULL);
		gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
		break;
	default:
		gs_app_set_state (app, AS_APP_STATE_UNKNOWN);
		break;
	}
}

static gboolean
_transaction_operation_error (FlatpakTransaction *transaction,
			      FlatpakTransactionOperation *operation,
			      const GError *error,
			      FlatpakTransactionErrorDetails detail)
{
	GsFlatpakTransaction *self = GS_FLATPAK_TRANSACTION (transaction);
	FlatpakTransactionOperationType operation_type = flatpak_transaction_operation_get_operation_type (operation);
	GsApp *app = _transaction_operation_get_app (operation);
	const gchar *ref = flatpak_transaction_operation_get_ref (operation);

	if (g_error_matches (error, FLATPAK_ERROR, FLATPAK_ERROR_SKIPPED)) {
		g_debug ("skipped to %s %s: %s",
		         _flatpak_transaction_operation_type_to_string (operation_type),
		         ref,
		         error->message);
		return TRUE; /* continue */
	}

	if (detail & FLATPAK_TRANSACTION_ERROR_DETAILS_NON_FATAL) {
		g_warning ("failed to %s %s (non fatal): %s",
		           _flatpak_transaction_operation_type_to_string (operation_type),
		           ref,
		           error->message);
		return TRUE; /* continue */
	}

	if (self->first_operation_error == NULL) {
		g_propagate_error (&self->first_operation_error,
		                   g_error_copy (error));
		if (app != NULL)
			gs_utils_error_add_app_id (&self->first_operation_error, app);
	}
	return FALSE; /* stop */
}

static int
_transaction_choose_remote_for_ref (FlatpakTransaction *transaction,
				    const char *for_ref,
				    const char *runtime_ref,
				    const char * const *remotes)
{
	//FIXME: do something smarter
	return 0;
}

static void
_transaction_end_of_lifed (FlatpakTransaction *transaction,
			   const gchar *ref,
			   const gchar *reason,
			   const gchar *rebase)
{
	if (rebase) {
		g_printerr ("%s is end-of-life, in preference of %s\n", ref, rebase);
	} else if (reason) {
		g_printerr ("%s is end-of-life, with reason: %s\n", ref, reason);
	}
	//FIXME: show something in the UI
}

static gboolean
_transaction_add_new_remote (FlatpakTransaction *transaction,
			     FlatpakTransactionRemoteReason reason,
			     const char *from_id,
			     const char *remote_name,
			     const char *url)
{
	/* additional applications */
	if (reason == FLATPAK_TRANSACTION_REMOTE_GENERIC_REPO) {
		g_debug ("configuring %s as new generic remote", url);
		return TRUE; //FIXME?
	}

	/* runtime deps always make sense */
	if (reason == FLATPAK_TRANSACTION_REMOTE_RUNTIME_DEPS) {
		g_debug ("configuring %s as new remote for deps", url);
		return TRUE;
	}

	return FALSE;
}

static void
gs_flatpak_transaction_class_init (GsFlatpakTransactionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	FlatpakTransactionClass *transaction_class = FLATPAK_TRANSACTION_CLASS (klass);
	object_class->finalize = gs_flatpak_transaction_finalize;
	transaction_class->ready = _transaction_ready;
	transaction_class->add_new_remote = _transaction_add_new_remote;
	transaction_class->new_operation = _transaction_new_operation;
	transaction_class->operation_done = _transaction_operation_done;
	transaction_class->operation_error = _transaction_operation_error;
	transaction_class->choose_remote_for_ref = _transaction_choose_remote_for_ref;
	transaction_class->end_of_lifed = _transaction_end_of_lifed;

	signals[SIGNAL_REF_TO_APP] =
		g_signal_new ("ref-to-app",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      0, NULL, NULL, NULL, G_TYPE_OBJECT, 1, G_TYPE_STRING);
}

static void
gs_flatpak_transaction_init (GsFlatpakTransaction *self)
{
	self->refhash = g_hash_table_new_full (g_str_hash, g_str_equal,
					       g_free, (GDestroyNotify) g_object_unref);
}

FlatpakTransaction *
gs_flatpak_transaction_new (FlatpakInstallation	*installation,
			    GCancellable *cancellable,
			    GError **error)
{
	GsFlatpakTransaction *self;
	self = g_initable_new (GS_TYPE_FLATPAK_TRANSACTION,
			       cancellable, error,
			       "installation", installation,
			       NULL);
	if (self == NULL)
		return NULL;
	return FLATPAK_TRANSACTION (self);
}
