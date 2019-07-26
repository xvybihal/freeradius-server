#pragma once
/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/** Boxed value structures and functions to manipulate them
 *
 * @file src/lib/util/struct.h
 *
 * @copyright 2018 Alan DeKok (aland@freeradius.org)
 */
RCSIDH(struct_h, "$Id$")

#include <freeradius-devel/util/value.h>
#include <freeradius-devel/util/cursor.h>
#include <freeradius-devel/util/pair.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t fr_struct_from_network(TALLOC_CTX *ctx, fr_cursor_t *cursor,
			       fr_dict_attr_t const *parent, uint8_t const *data, size_t data_len,
			       fr_dict_attr_t const **child) CC_HINT(nonnull(2,3,4));
ssize_t fr_struct_to_network(uint8_t *out, size_t outlen,
			     fr_dict_attr_t const *parent, fr_cursor_t *cursor) CC_HINT(nonnull);

VALUE_PAIR *fr_unknown_from_network(TALLOC_CTX *ctx, fr_dict_attr_t const *parent,
				    uint8_t const *data, size_t data_len) CC_HINT(nonnull);

#ifdef __cplusplus
}
#endif
