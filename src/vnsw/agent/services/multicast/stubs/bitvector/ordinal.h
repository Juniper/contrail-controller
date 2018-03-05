/* $Id: ordinal.h 346474 2009-11-14 10:18:58Z ssiano $
 *
 * ordinal.h - Ordinal assignment definitions
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/*
 * Overview
 *
 * This module hands out ordinals from a dense ordinal space.  Two
 * options are available--the ordinals can be as compact as possible,
 * with a cost in CPU time, or the ordinals may be less than optimally
 * compact but with a significant improvement in performance when the
 * number of ordinals is large.
 *
 * If the compact option is chosen, the ordinals are guaranteed to be
 * as compact as possible, and the lowest ordinal available will be
 * provided to the next caller.
 *
 * If the compact option is not chosen, the ordinals delivered may have
 * "holes" in the space if ordinals are repeatedly allocated and then
 * freed.
 *
 * This routine relies on the bit vector code to do most of the work.
 */


/*
 *
 * Calling Sequence
 *
 * ordinal_handle ord_create_context(ord_compact_option compact)
 *   Create an ordinal context.  An ordinal context represents an ordinal
 *   space; the caller can have multiple ordinal spaces by creating multiple
 *   contexts.  If "compact" is ORD_COMPACT, the ordinal space is as
 *   compact as possible.  If the value is ORD_PERFORMANCE, the ordinal
 *   space may have holes in it if ordinals are repeatedly created and
 *   freed, but at a lower performance cost.
 *
 * ord_destroy_context(ordinal_handle handle)
 *   Destroy an ordinal context.  This frees all memory associated with
 *   the ordinal context.  Any assigned ordinals are freed.
 *
 * u_int32_t ord_get_ordinal(ordinal_handle handle)
 *   Returns the lowest-numbered free ordinal, or ORD_BAD_ORDINAL if out
 *   of memory or out of ordinal space.
 *
 * ord_free_ordinal(ordinal_handle handle, u_int32_t ordinal)
 *   Free a previously-assigned ordinal.  Asserts if the ordinal wasn't
 *   previously assigned.
 *
 *
 * A caller must first call ord_create_context() to create a context.
 * Within the context, ord_get_ordinal() and ord_free_ordinal() can be
 * used to allocate and free ordinal values.  Finally,
 * ord_destroy_context() must be called to free up the context to
 * avoid memory leaks.
 */

#ifndef __ORDINAL_H__
#define __ORDINAL_H__

/* Bad ordinal value. */

#define ORD_BAD_ORDINAL BV_BAD_BITNUM


/*
 * Opaque ordinal handle
 */
typedef void *ordinal_handle;

/*
 * Ordinal number
 */
typedef bv_bitnum_t ordinal_t;

/*
 * Ordinal compact option
 */
typedef enum {ORD_PERFORMANCE, ORD_COMPACT} ord_compact_option;


/* Externs */

extern ordinal_handle ord_create_context(ord_compact_option compact);
extern void ord_destroy_context(ordinal_handle handle);
extern uint32_t ord_get_ordinal(ordinal_handle handle);
extern void ord_free_ordinal(ordinal_handle handle, uint32_t ordinal);

#endif /* __ORDINAL_H__ */
