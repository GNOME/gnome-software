/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2022 Red Hat (www.redhat.com)
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "config.h"

#include "gs-lozenge-layout.h"

struct _GsLozengeLayout
{
	GsLayoutManager		 parent_instance;

	gboolean		 circular;
};

G_DEFINE_TYPE (GsLozengeLayout, gs_lozenge_layout, GS_TYPE_LAYOUT_MANAGER)

typedef enum {
	PROP_CIRCULAR = 1,
} GsLozengeLayoutProperty;

static GParamSpec *obj_props[PROP_CIRCULAR + 1] = { NULL, };

static void
gs_lozenge_layout_measure (GtkLayoutManager *layout_manager,
			   GtkWidget	    *widget,
			   GtkOrientation    orientation,
			   gint		     for_size,
			   gint		    *minimum,
			   gint		    *natural,
			   gint		    *minimum_baseline,
			   gint		    *natural_baseline)
{
	GsLozengeLayout *self = GS_LOZENGE_LAYOUT (layout_manager);

	GTK_LAYOUT_MANAGER_CLASS (gs_lozenge_layout_parent_class)->measure (layout_manager,
		widget, orientation, for_size, minimum, natural, minimum_baseline, natural_baseline);

	if (self->circular) {
		*minimum = MAX (for_size, *minimum);
		*natural = *minimum;
		*natural_baseline = *minimum_baseline;
	}

	if (*natural_baseline > *natural)
		*natural_baseline = *natural;
	if (*minimum_baseline > *minimum)
		*minimum_baseline = *minimum;
}

static void
gs_lozenge_layout_get_property (GObject    *object,
				guint       prop_id,
				GValue     *value,
				GParamSpec *pspec)
{
	GsLozengeLayout *self = GS_LOZENGE_LAYOUT (object);

	switch ((GsLozengeLayoutProperty) prop_id) {
	case PROP_CIRCULAR:
		g_value_set_boolean (value, gs_lozenge_layout_get_circular (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_lozenge_layout_set_property (GObject      *object,
				guint         prop_id,
				const GValue *value,
				GParamSpec   *pspec)
{
	GsLozengeLayout *self = GS_LOZENGE_LAYOUT (object);

	switch ((GsLozengeLayoutProperty) prop_id) {
	case PROP_CIRCULAR:
		gs_lozenge_layout_set_circular (self, g_value_get_boolean (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_lozenge_layout_class_init (GsLozengeLayoutClass *klass)
{
	GtkLayoutManagerClass *layout_manager_class = GTK_LAYOUT_MANAGER_CLASS (klass);
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	layout_manager_class->measure = gs_lozenge_layout_measure;

	object_class->get_property = gs_lozenge_layout_get_property;
	object_class->set_property = gs_lozenge_layout_set_property;

	/**
	 * GsLozengeLayout:circular:
	 *
	 * Whether the lozenge should be circular, thus its size square.
	 *
	 * The default is %FALSE.
	 *
	 * Since: 43
	 */
	obj_props[PROP_CIRCULAR] =
		g_param_spec_boolean ("circular", NULL, NULL,
				      FALSE,
				      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);
}

static void
gs_lozenge_layout_init (GsLozengeLayout *self)
{
}

/**
 * gs_lozenge_layout_new:
 *
 * Create a new #GsLozengeLayout.
 *
 * Returns: (transfer full): a new #GsLozengeLayout
 *
 * Since: 43
 **/
GsLozengeLayout *
gs_lozenge_layout_new (void)
{
	return g_object_new (GS_TYPE_LOZENGE_LAYOUT, NULL);
}

/**
 * gs_lozenge_layout_get_circular:
 * @self: a #GsLozengeLayout
 *
 * Get the value of #GsLozengeLayout:circular.
 *
 * Returns: %TRUE if the lozenge has to be circular, %FALSE otherwise
 * Since: 43
 */
gboolean
gs_lozenge_layout_get_circular (GsLozengeLayout *self)
{
	g_return_val_if_fail (GS_IS_LOZENGE_LAYOUT (self), FALSE);

	return self->circular;
}

/**
 * gs_lozenge_layout_set_circular:
 * @self: a #GsLozengeLayout
 * @value: %TRUE if the lozenge should be circular, %FALSE otherwise
 *
 * Set the value of #GsLozengeLayout:circular to @value.
 *
 * Since: 43
 */
void
gs_lozenge_layout_set_circular (GsLozengeLayout *self,
				gboolean value)
{
	g_return_if_fail (GS_IS_LOZENGE_LAYOUT (self));

	if ((!self->circular) == (!value))
		return;

	self->circular = value;

	gtk_layout_manager_layout_changed (GTK_LAYOUT_MANAGER (self));

	g_object_notify_by_pspec (G_OBJECT (self), obj_props[PROP_CIRCULAR]);
}
