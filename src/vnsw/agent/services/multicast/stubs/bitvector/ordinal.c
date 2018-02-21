/* $Id: ordinal.c 346474 2009-11-14 10:18:58Z ssiano $
 *
 * ordinal.c - Compact ordinal assignment
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#include "bvx_environment.h"
#include "bitvector.h"
#include "ordinal.h"
#include "ordinal_private.h"

/*
 * This module hands out ordinals from a dense ordinal space.  The ordinals
 * are guaranteed to be as compact as possible, and the lowest ordinal
 * available will be provided to the next caller.
 *
 * A caller first establishes a context, and then grabs and releases
 * ordinals.  Each context has a separate ordinal number space.
 *
 * This routine relies on the bit vector code to do most of the work.
 *
 * See comments in ordinal.h for details.
 */

static bvx_block_tag context_tag;


/*
 * ord_create_context
 * 
 * Create a new ordinal context.
 *
 * Returns a context pointer, or NULL if out of memory.
 *
 * "compact" is ORD_COMPACT for the most compact ordinal space possible,
 * or ORD_PERFORMANCE for a possibly less compact space with better CPU
 * performance.
 */
ordinal_handle
ord_create_context (ord_compact_option compact)
{
    ord_context *context;

    /* Create the context tag if it doesn't already exist. */

    if (!context_tag) {
	context_tag = bvx_malloc_block_create(sizeof(ord_context),
					      "Ordinal context");
    }

    /* Allocate a context block. */

    context = bvx_malloc_block(context_tag);
    if (!context)
	return NULL;			/* Out of memory */

    /* Initialize the bit vector. */

    bv_init_vector(&context->ord_vector, FALSE);
    context->ord_compact = compact;

    return context;
}


/*
 * ord_destroy_context
 *
 * Destroy an ordinal context.
 */
void
ord_destroy_context (ordinal_handle handle)
{
    ord_context *context;

    context = handle;

    /* Clean out the bit vector. */

    bv_clean(&context->ord_vector);

    /* Free the context block. */

    bvx_free_block(context_tag, context);
}


/*
 * ord_get_ordinal
 *
 * Get a new ordinal.
 *
 * Returns the ordinal, or ORD_BAD_ORDINAL if out of memory.
 */
uint32_t
ord_get_ordinal (ordinal_handle handle)
{
    ord_context *context;
    bv_bitnum_t ordinal;
    int bit_set;

    context = handle;

    /* Find a free bit from the vector. */

    if (context->ord_compact == ORD_PERFORMANCE)
	ordinal = bv_find_clear_bit(&context->ord_vector);
    else
	ordinal = bv_first_clear_bit(&context->ord_vector);

    if (ordinal != BV_BAD_BITNUM) {
	bit_set = bv_set_bit(&context->ord_vector, ordinal);
	if (bit_set < 0)
	    return ORD_BAD_ORDINAL;	/* Out of memory */
	bvx_assert(bit_set == 0);

	return ordinal;
    }
    return ORD_BAD_ORDINAL;
}


/*
 * ord_free_ordinal
 *
 * Free an ordinal.
 */
void
ord_free_ordinal (ordinal_handle handle, ordinal_t ordinal)
{
    ord_context *context;
    boolean bit_clear;

    context = handle;

    /* Clear the bit.  It better have been set. */

    bit_clear = bv_clear_bit(&context->ord_vector, ordinal);
    bvx_assert(bit_clear);
}
