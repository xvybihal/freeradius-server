/*
 *   This library is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU Lesser General Public
 *   License as published by the Free Software Foundation; either
 *   version 2.1 of the License, or (at your option) any later version.
 *
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 *   Lesser General Public License for more details.
 *
 *   You should have received a copy of the GNU Lesser General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/** Functions to encode / decode structures on the wire
 *
 * @file src/lib/util/struct.c
 *
 * @copyright 2018 The FreeRADIUS server project
 * @copyright 2018 Alan DeKok (aland@freeradius.org)
 */
RCSID("$Id$")

#include "struct.h"

VALUE_PAIR *fr_unknown_from_network(TALLOC_CTX *ctx, fr_dict_attr_t const *parent, uint8_t const *data, size_t data_len)
{
	VALUE_PAIR *vp;
	fr_dict_attr_t const *child;

#if defined(__clang_analyzer__) || !defined(NDEBUG)
	if (!parent->parent) return NULL; /* stupid static analyzers */
#endif

	/*
	 *	Build an unknown attr of the entire STRUCT.
	 */
	child = fr_dict_unknown_afrom_fields(ctx, parent->parent,
					     fr_dict_vendor_num_by_da(parent), parent->attr);
	if (!child) return NULL;

	vp = fr_pair_afrom_da(ctx, child);
	if (!vp) return NULL;

	if (fr_value_box_from_network(vp, &vp->data, vp->da->type, vp->da, data, data_len, true) < 0) {
		TALLOC_FREE(vp);
		return NULL;
	}

	vp->type = VT_DATA;
	return vp;
}



/** Convert a STRUCT to one or more VPs
 *
 */
ssize_t fr_struct_from_network(TALLOC_CTX *ctx, fr_cursor_t *cursor,
			       fr_dict_attr_t const *parent, uint8_t const *data, size_t data_len,
			       fr_dict_attr_t const **child_p)
{
	unsigned int		child_num;
	uint8_t const		*p = data, *end = data + data_len;
	fr_dict_attr_t const	*child;
	VALUE_PAIR		*head = NULL;
	fr_cursor_t		child_cursor;
	VALUE_PAIR		*vp;

	if (data_len < 1) return -1; /* at least one byte of data */

	/*
	 *  Record where we were in the list when this function was called
	 */
	fr_cursor_init(&child_cursor, &head);
	*child_p = NULL;

	child_num = 1;
	while (p < end) {
		size_t child_length;

		/*
		 *	Go to the next child.  If it doesn't exist, we're done.
		 */
		child = fr_dict_attr_child_by_num(parent, child_num);
		if (!child) break;

		/*
		 *	Decode child TLVs, according to the parent attribute.
		 *
		 *	Return only PARTIALLY decoded data.  Let the
		 *	caller decode the rest.
		 */
		if (child->type == FR_TYPE_TLV) {
			*child_p = child;

			fr_cursor_head(&child_cursor);
			fr_cursor_tail(cursor);
			fr_cursor_merge(cursor, &child_cursor);	/* Wind to the end of the new pairs */
			return (p - data);
		}

		child_length = child->flags.length;
		if (!child_length) child_length = (end - p);

		vp = fr_pair_afrom_da(ctx, child);
		if (!vp) return -1;

		/*
		 *	We only allow a limited number of data types
		 *	inside of a struct.
		 */
		switch (child->type) {
		default:
			fr_strerror_printf("Invalid data type passed to decode_struct");
			return -1;

		case FR_TYPE_VALUES:
			break;
		}

		/*
		 *	No protocol-specific data types here (yet).
		 *
		 *	If we can't decode this field, then the entire
		 *	structure is treated as a raw blob.
		 */
		if (fr_value_box_from_network(vp, &vp->data, vp->da->type, vp->da, p, child_length, true) < 0) {
			TALLOC_FREE(vp);
			fr_pair_list_free(&head);
			fr_cursor_init(&child_cursor, &head);

			vp = fr_unknown_from_network(ctx, parent, data, data_len);
			if (!vp) return -1;

			fr_cursor_append(&child_cursor, vp);
			return data_len;
		}

		vp->type = VT_DATA;
		vp->vp_tainted = true;
		fr_cursor_append(&child_cursor, vp);

		/*
		 *	Note that we're decoding fixed fields here.
		 *	So we skip the input based on the *known*
		 *	length, and not on the *decoded* length.
		 */
		p += child_length;
		child_num++;	/* go to the next child */
	}

	fr_cursor_head(&child_cursor);
	fr_cursor_tail(cursor);
	fr_cursor_merge(cursor, &child_cursor);	/* Wind to the end of the new pairs */

	return data_len;
}


ssize_t fr_struct_to_network(uint8_t *out, size_t outlen,
			     fr_dict_attr_t const *parent, fr_cursor_t *cursor)
{
	ssize_t			len;
	unsigned int		child_num = 1;
	uint8_t			*p = out;
	VALUE_PAIR const	*vp = fr_cursor_current(cursor);

	VP_VERIFY(fr_cursor_current(cursor));

	if (parent->type != FR_TYPE_STRUCT) {
		fr_strerror_printf("%s: Expected type \"struct\" got \"%s\"", __FUNCTION__,
				   fr_int2str(fr_value_box_type_table, parent->type, "?Unknown?"));
		return -1;
	}

	if (!vp || (vp->da->parent != parent)) {
		fr_strerror_printf("%s: Can't encode empty struct", __FUNCTION__);
		return -1;
	}

	while (outlen) {
		fr_dict_attr_t const *child;

		/*
		 *	The child attributes should be in order.  If
		 *	they're not, we fill the struct with zeroes.
		 */
		child = vp->da;
		if (child->attr != child_num) {
			child = fr_dict_attr_child_by_num(parent, child_num);

			if (!child) break;

			if (child->flags.length > outlen) {
				len = outlen;
			} else {
				len = child->flags.length;
			}

			memset(p, 0, len);
			p += len;
			outlen -= len;
			child_num++;
			continue;
		}

		/*
		 *	Determine the nested type and call the appropriate encoder
		 */
		len = fr_value_box_to_network(NULL, p, outlen, &vp->data);
		if (len <= 0) return -1;

		p += len;
		outlen -= len;				/* Subtract from the buffer we have available */
		child_num++;

		do {
			vp = fr_cursor_next(cursor);
			if (!vp || !vp->da->flags.internal) break;
		} while (vp != NULL);

		/*
		 *	Nothing more to do, or we've done all of the
		 *	entries in this structure, stop.
		 */
		if (!vp || (vp->da->parent != parent)) break;
	}

	return p - out;
}
