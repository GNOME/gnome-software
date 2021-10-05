/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2021 Endless OS Foundation LLC
 *
 * Author: Georges Basile Stavracas Neto <georges.stavracas@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "gs-summary-tile-layout.h"

struct _GsSummaryTileLayout
{
	GtkLayoutManager	parent_instance;

	gint			preferred_width;
};

G_DEFINE_TYPE (GsSummaryTileLayout, gs_summary_tile_layout, GTK_TYPE_LAYOUT_MANAGER)

static void
gs_summary_tile_layout_layout_measure (GtkLayoutManager *layout_manager,
				       GtkWidget        *widget,
				       GtkOrientation    orientation,
				       gint              for_size,
				       gint             *minimum,
				       gint             *natural,
				       gint             *minimum_baseline,
				       gint             *natural_baseline)
{
	GsSummaryTileLayout *self = GS_SUMMARY_TILE_LAYOUT (layout_manager);
	GtkWidget *child;
	gint min = 0;
	gint nat = 0;

	for (child = gtk_widget_get_first_child (widget);
	     child != NULL;
	     child = gtk_widget_get_next_sibling (child)) {
		gint child_min_baseline = -1;
		gint child_nat_baseline = -1;
		gint child_min = 0;
		gint child_nat = 0;

		if (!gtk_widget_should_layout (child))
			continue;

		gtk_widget_measure (child, orientation,
				    for_size,
				    &child_min, &child_nat,
				    &child_min_baseline,
				    &child_nat_baseline);

		min = MAX (min, child_min);
		nat = MAX (nat, child_nat);

		if (child_min_baseline > -1)
			*minimum_baseline = MAX (*minimum_baseline, child_min_baseline);
		if (child_nat_baseline > -1)
		    *natural_baseline = MAX (*natural_baseline, child_nat_baseline);
	}

	*minimum = min;
	*natural = nat;

	/* Limit the natural width */
	if (self->preferred_width > 0 && orientation == GTK_ORIENTATION_HORIZONTAL)
		*natural = MAX (min, self->preferred_width);
}

static void
gs_summary_tile_layout_layout_allocate (GtkLayoutManager *layout_manager,
                                        GtkWidget        *widget,
                                        gint              width,
                                        gint              height,
                                        gint              baseline)
{
	GtkWidget *child;

	for (child = gtk_widget_get_first_child (widget);
	     child != NULL;
	     child = gtk_widget_get_next_sibling (child)) {
		if (child && gtk_widget_should_layout (child))
			gtk_widget_allocate (child, width, height, baseline, NULL);
	}
}

static void
gs_summary_tile_layout_class_init (GsSummaryTileLayoutClass *klass)
{
	GtkLayoutManagerClass *layout_manager_class = GTK_LAYOUT_MANAGER_CLASS (klass);

	layout_manager_class->measure = gs_summary_tile_layout_layout_measure;
	layout_manager_class->allocate = gs_summary_tile_layout_layout_allocate;
}

static void
gs_summary_tile_layout_init (GsSummaryTileLayout *self)
{
	self->preferred_width = -1;
}

void
gs_summary_tile_layout_set_preferred_width (GsSummaryTileLayout *self,
                                            gint                 preferred_width)
{
	g_return_if_fail (GS_IS_SUMMARY_TILE_LAYOUT (self));

	if (self->preferred_width == preferred_width)
		return;

	self->preferred_width = preferred_width;
	gtk_layout_manager_layout_changed (GTK_LAYOUT_MANAGER (self));
}
