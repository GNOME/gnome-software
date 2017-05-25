/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2016 Canonical Ltd.
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

#include <glib/gi18n.h>

#include "gs-price.h"

struct _GsPrice
{
	GObject			 parent_instance;

	gdouble			 amount;
	gchar			*currency;
};

G_DEFINE_TYPE (GsPrice, gs_price, G_TYPE_OBJECT)

/**
 * gs_price_get_amount:
 * @price: a #GsPrice
 *
 * Get the amount of money in this price.
 *
 * Returns: The amount of money in this price, e.g. 0.99
 */
gdouble
gs_price_get_amount (GsPrice *price)
{
	g_return_val_if_fail (GS_IS_PRICE (price), 0);
	return price->amount;
}

/**
 * gs_price_set_amount:
 * @price: a #GsPrice
 * @amount: The amount of this price, e.g. 0.99
 *
 * Set the amount of money in this price.
 */
void
gs_price_set_amount (GsPrice *price, gdouble amount)
{
	g_return_if_fail (GS_IS_PRICE (price));
	price->amount = amount;
}

/**
 * gs_price_get_currency:
 * @price: a #GsPrice
 *
 * Get the currency a price is using.
 *
 * Returns: an ISO 4217 currency code for this price, e.g. "USD"
 */
const gchar *
gs_price_get_currency (GsPrice *price)
{
	g_return_val_if_fail (GS_IS_PRICE (price), NULL);
	return price->currency;
}

/**
 * gs_price_set_currency:
 * @price: a #GsPrice
 * @currency: An ISO 4217 currency code, e.g. "USD"
 *
 * Set the currency this price is using.
 */
void
gs_price_set_currency (GsPrice *price, const gchar *currency)
{
	g_return_if_fail (GS_IS_PRICE (price));
	g_free (price->currency);
	price->currency = g_strdup (currency);
}

/**
 * gs_price_to_string:
 * @price: a #GsPrice
 *
 * Convert a price object to a human readable string.
 *
 * Returns: A human readable string for this price, e.g. "US$0.99"
 */
gchar *
gs_price_to_string (GsPrice *price)
{
	g_return_val_if_fail (GS_IS_PRICE (price), NULL);

	if (g_strcmp0 (price->currency, "AUD") == 0) {
		return g_strdup_printf (_("A$%.2f"), price->amount);
	} else if (g_strcmp0 (price->currency, "CAD") == 0) {
		return g_strdup_printf (_("C$%.2f"), price->amount);
	} else if (g_strcmp0 (price->currency, "CNY") == 0) {
		return g_strdup_printf (_("CN¥%.2f"), price->amount);
	} else if (g_strcmp0 (price->currency, "EUR") == 0) {
		return g_strdup_printf (_("€%.2f"), price->amount);
	} else if (g_strcmp0 (price->currency, "GBP") == 0) {
		return g_strdup_printf (_("£%.2f"), price->amount);
	} else if (g_strcmp0 (price->currency, "JPY") == 0) {
		return g_strdup_printf (_("¥%.2f"), price->amount);
	} else if (g_strcmp0 (price->currency, "NZD") == 0) {
		return g_strdup_printf (_("NZ$%.2f"), price->amount);
	} else if (g_strcmp0 (price->currency, "RUB") == 0) {
		return g_strdup_printf (_("₽%.2f"), price->amount);
	} else if (g_strcmp0 (price->currency, "USD") == 0) {
		return g_strdup_printf (_("US$%.2f"), price->amount);
	} else {
		return g_strdup_printf (_("%s %f"), price->currency, price->amount);
	}
}

static void
gs_price_finalize (GObject *object)
{
	GsPrice *price = GS_PRICE (object);

	g_free (price->currency);

	G_OBJECT_CLASS (gs_price_parent_class)->finalize (object);
}

static void
gs_price_class_init (GsPriceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gs_price_finalize;
}

static void
gs_price_init (GsPrice *price)
{
}

/**
 * gs_price_new:
 * @amount: The amount of this price, e.g. 0.99
 * @currency: An ISO 4217 currency code, e.g. "USD"
 *
 * Creates a new price object.
 *
 * Return value: a new #GsPrice object.
 **/
GsPrice *
gs_price_new (gdouble amount, const gchar *currency)
{
	GsPrice *price;
	price = g_object_new (GS_TYPE_PRICE, NULL);
	price->amount = amount;
	price->currency = g_strdup (currency);
	return GS_PRICE (price);
}

/* vim: set noexpandtab: */
