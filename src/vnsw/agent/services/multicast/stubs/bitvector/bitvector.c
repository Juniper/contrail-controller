/* $Id: bitvector.c 492816 2012-01-25 00:14:30Z ib-builder $
 *
 * bitvector.c - Bit vector manipulation
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/*
 * See bitvector.h for an overview of how this stuff works.
 */


#include "bvx_environment.h"
#include "bitvector.h"
#include "bitvector_private.h"


static bvx_block_tag bv_ent_tag;

/*
 * Array of count of bits set.
 *
 * Index into this array with a byte of data, returns the number of bits
 * set in the byte.
 */
static const uint8_t bitcount_array[256] =
    {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
     1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
     1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
     2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
     1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
     2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
     2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
     3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
     1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
     2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
     2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
     3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
     2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
     3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
     3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
     4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8};


/*
 * bv_bitcount
 *
 * Returns the number of set bits in a vector word.
 */
static uint32_t
bv_bitcount (bv_word_t word)
{
    uint32_t bitcount;
    uint32_t bytenum;
    uint8_t byteval;

    bitcount = 0;

    /* Walk each byte and accumulate the bit count. */

    for (bytenum = 0; bytenum < sizeof(bv_word_t); bytenum++) {

	/* Quick cheat.  If the residual is zero, we're done. */

	if (!word)
	    break;
	byteval = word & 0xff;
	bitcount += bitcount_array[byteval];
	word >>= 8;
    }

    return bitcount;
}


/*
 * Array of bit number of the first set bit.
 *
 * Index into this array with a byte of data, returns the bit number of the
 * first one bit.  A value of -1 means that all bits are clear.
 */
static const int8_t bitset_array[256] =
    {-1, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
      4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0};


/*
 * bv_first_set
 *
 * Returns the bit number of the first bit set in a word, or 1 if no bits
 * are set.
 */
static int
bv_first_set (bv_word_t word)
{
    int bitnum;
    uint32_t bytenum;
    uint8_t byteval;

    /* Quick cheat.  Bail if the word is zero. */

    if (word == 0)
	return -1;

    /* Walk each byte looking for the first set bit. */

    for (bytenum = 0; bytenum < sizeof(bv_word_t); bytenum++) {

	/* Quick cheat.  If the residual is zero, we're done. */

	if (!word)
	    return -1;
	byteval = word & 0xff;
	bitnum = bitset_array[byteval];
	if (bitnum >= 0)
	    return ((bytenum * 8) + bitnum);
	word >>= 8;
    }

    /* We shouldn't get here, but quiet the compiler. */

    return -1;
}


/*
 * Array of bit number of the first clear bit.
 *
 * Index into this array with a byte of data, returns the bit number of the
 * first zero bit.  A value of -1 means that all bits are set.
 */
static const int8_t bitclear_array[256] =
    {0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 7,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 6,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 5,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, 4,
     0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0, -1};


/*
 * bv_first_clear
 *
 * Returns the bit number of the first bit clear in a word, or -1 if no bits
 * are clear.
 */
static int
bv_first_clear (bv_word_t word)
{
    int bitnum;
    uint32_t bytenum;
    uint8_t byteval;

    /* Quick cheat.  Bail if the word is all one. */

    if (word == BV_ALLSET)
	return -1;

    /* Walk each byte looking for the first clear bit. */

    for (bytenum = 0; bytenum < sizeof(bv_word_t); bytenum++) {

	byteval = word & 0xff;
	bitnum = bitclear_array[byteval];
	if (bitnum >= 0)
	    return ((bytenum * 8) + bitnum);
	word >>= 8;
    }

    /* We shouldn't get here, but quiet the compiler. */

    return -1;
}


/*
 * bv_empty_cb
 *
 * Callback for seeing if there are any set bits.  We set the flag and
 * abort the search if we get called (meaning there was a bit set.)
 */
static boolean
bv_empty_cb (void *context, bv_bitnum_t bit_number BVX_UNUSED,
	     boolean new_bit_value BVX_UNUSED,
	     boolean old_bit_value BVX_UNUSED)
{
    boolean *bit_found;

    bit_found = context;
    *bit_found = TRUE;

    return TRUE;			/* Abort */
}


/*
 * bv_empty
 *
 * Returns TRUE if a bit vector is empty, or FALSE if not.
 */
boolean
bv_empty (bit_vector *bv)
{
    boolean bit_found;

    /* No touching the vector from a callback routine. */

    bvx_assert(!bv->bv_cb_result);
    bvx_assert(!bv->bv_cb_source);

    /* If the entry count is zero, it's definitely empty. */

    bit_found = (bv->bv_entry_count != 0);
    if (bit_found) {
	/*
	 * If we're using fast vectors, we can't rely on the entry count, since
	 * we don't always delete empty entries.  So walk it instead.
	 */
	if (bv->bv_fastvects) {
	    bv_walk_vector(bv, bv_empty_cb, &bit_found);
	}
    }
    return (!bit_found);
}


/*
 * bv_entry_active_here
 *
 * Returns TRUE if the specified vector entry matches our current position.
 */
static inline boolean
bv_entry_active_here (bv_entry *bv_ent, bv_bitnum_t start_bitnum)
{
    return (bv_ent && bv_ent->bv_start == start_bitnum);
}


/*
 * bv_next_entry
 *
 * Returns the next entry pointer for a vector, given the current one,
 * or NULL if none are left.
 */
static bv_entry *
bv_next_entry (bit_vector *bv, bv_entry *cur_ent)
{
    bv_entry *next_ent;

    next_ent =
	bv_patnode_to_bv_entry(bvx_patricia_get_next(bv->bv_root,
						     &cur_ent->bv_ent_node));
    return next_ent;
}


/*
 * bv_advance_entry
 *
 * Advance the entry pointer for a vector if it's pointing at our current
 * position.  Otherwise leave the pointer be.
 *
 * Returns a pointer, or NULL if nothing left.
 */
static bv_entry *
bv_advance_entry (bit_vector *bv, bv_entry *cur_ent, bv_bitnum_t start_bitnum)
{
    bv_entry *next_ent;

    /* If the current entry is NULL, so is the next one. */

    if (!cur_ent)
	return NULL;

    if (bv_entry_active_here(cur_ent, start_bitnum)) {
	next_ent = bv_next_entry(bv, cur_ent);
    } else {
	next_ent = cur_ent;
    }

    return next_ent;
}


/*
 * bv_init_vector
 *
 * Initialize a bit vector.
 *
 * Zeroes the block and initializes it.  We allocate the patricia tree
 * when it's time to add the first entry.
 */
void
bv_init_vector (bit_vector *bv, boolean fast_vects)
{
    memset(bv, 0, sizeof(bit_vector));
    bv->bv_fastvects = fast_vects;
    bv->bv_freed_ord = BV_BAD_BITNUM;
    thread_new_circular_thread(&bv->bv_nonfull_head);
}


/*
 * bv_init_vector_tree
 *
 * Initialize a bit vector tree.
 *
 * Returns 0 if all OK, or -1 if out of memory.
 */
static int
bv_init_vector_tree (bit_vector *bv)
{
    /* Create the patricia tree. */

    bv->bv_root = bvx_patroot_init(sizeof(bv_bitnum_t),
				   BVX_PATRICIA_OFFSET(bv_entry,
						       bv_ent_node,
						       bv_key));
    if (!bv->bv_root)
	return -1;			/* Out of memory */

    return 0;
}


/*
 * bv_build_key
 *
 * Build a patricia key (which consists of putting the low-order bits
 * of the start bit number into the highest address of the key so that
 * it ends up in lexicographic order.)  In big-endian machines this
 * will burn some CPU but still do the right thing.
 */
static void
bv_build_key (uint8_t key_ptr[], bv_bitnum_t bit_number)
{
    int key_count;

    key_count = sizeof(bv_bitnum_t);
    do {
	key_count--;
	key_ptr[key_count] = bit_number & 0xff;
	bit_number >>= 8;
    } while (key_count);
}


/*
 * bv_ent_create
 *
 * Create a bit vector entry, initialize it, and put it into the tree,
 * given the desired bit number.
 *
 * Returns a pointer to the vector entry, or NULL if out of memory.
 */
static bv_entry *
bv_ent_create (bit_vector *bv, bv_bitnum_t bit_number)
{
    bv_entry *bv_ent;
    bv_entry *next_ent;
    bv_bitnum_t next_ord;

    /* If the tree doesn't exist yet, create it. */

    if (!bv->bv_root) {
	if (bv_init_vector_tree(bv) < 0)
	    return NULL;		/* Out of memory */
    }

    /* Create the memory block if it doesn't already exist. */

    if (!bv_ent_tag) {
	bv_ent_tag =
	    bvx_malloc_block_create(sizeof(bv_entry), "Bit vector entry");
    }

    /* Allocate the block. */

    bv_ent = bvx_malloc_block(bv_ent_tag);
    if (!bv_ent)
	return bv_ent;			/* Out of memory */

    /* Got the entry.  Initialize it and put it into the tree. */

    bv_ent->bv_start = bv_start_bit(bit_number);
    bv_build_key(bv_ent->bv_key, bv_ent->bv_start);

    thread_circular_add_top(&bv->bv_nonfull_head,
			    &bv_ent->bv_ent_nonfull_thread);
    bvx_assert(bvx_patricia_add(bv->bv_root, &bv_ent->bv_ent_node));
    bv->bv_entry_count++;

    /*
     * If the freed ordinal was pointing at this one, or there isn't
     * one noted yet, look at the next entry in the tree.  If it is
     * nonexistent, or there is a hole in the bit number space, save
     * the ordinal after the current block as being free.  Otherwise,
     * set it to BADNUM since we don't know where to find one.
     */
    if (bv->bv_freed_ord == bv_ent->bv_start ||
	bv->bv_freed_ord == BV_BAD_BITNUM) {
	next_ent = bv_next_entry(bv, bv_ent);
	next_ord = bv_ent->bv_start + BV_BITSIZE;
	if (next_ent && next_ent->bv_start == next_ord) 
	    next_ord = BV_BAD_BITNUM;	/* No hole here. */
	bv->bv_freed_ord = next_ord;
    }
    return bv_ent;
}


/*
 * bv_ent_destroy
 *
 * Destroy a bit vector entry.  The entry is deleted from the bit vector
 * patricia tree and freed.
 */
static void
bv_ent_destroy (bit_vector *bv, bv_entry *bv_ent)
{
    /*
     * If the ordinal of this entry is less than the current freed ordinal,
     * or the freed ordinal is unset, point the freed ordinal at this one.
     */
    if (bv->bv_freed_ord == BV_BAD_BITNUM ||
	bv_ent->bv_start < bv->bv_freed_ord) {
	bv->bv_freed_ord = bv_ent->bv_start;
    }

    /* Delete the entry from the tree and free it. */

    bvx_patricia_delete(bv->bv_root, &bv_ent->bv_ent_node);
    thread_remove(&bv_ent->bv_ent_nonfull_thread);
    bvx_free_block(bv_ent_tag, bv_ent);
    bvx_assert(bv->bv_entry_count > 0);
    bv->bv_entry_count--;
}


/*
 * bv_attempt_entry_free
 *
 * Destroy a bit vector entry if it is known to be all zero.
 *
 * Tolerates NULL pointers.
 */
static void
bv_attempt_entry_free (bit_vector *bv, bv_entry *bv_ent)
{
    if (bv && bv_ent) {
	if (bv_ent->bv_setcount == 0) {
	    bv_ent_destroy(bv, bv_ent);
	}
    }
}


/*
 * bv_ent_lookup
 *
 * Look up the bit vector entry containing the specified bit number.
 *
 * Returns a pointer to the entry, or NULL if not present.
 */
static bv_entry *
bv_ent_lookup (bit_vector *bv, bv_bitnum_t bit_number)
{
    bv_entry *result;
    bvx_patnode *node;
    bv_bitnum_t start_bit;
    uint8_t key[sizeof(bv_bitnum_t)];

    /* If there's no bit vector or patricia tree, the entry isn't here. */

    if (!bv || !bv->bv_root)
	return NULL;

    /* Look it up in the patricia tree. */

    start_bit = bv_start_bit(bit_number);
    bv_build_key(key, start_bit);
    node = bvx_patricia_lookup(bv->bv_root, key);
    result = bv_patnode_to_bv_entry(node);

    return result;
}


/*
 * bv_ent_lookup_first
 *
 * Look up the first bit vector entry in a vector.
 *
 * Returns a pointer to the entry, or NULL if nothing's there.
 */
static bv_entry *
bv_ent_lookup_first (bit_vector *bv)
{
    bv_entry *bv_ent;

    /* If no vector, there's nothing there. */

    if (!bv)
	return NULL;

    /* If no tree, there's nothing here. */

    if (!bv->bv_root)
	return NULL;

    bv_ent =
	bv_patnode_to_bv_entry(bvx_patricia_lookup_least(bv->bv_root));

    return bv_ent;
}


/*
 * bv_ent_lookup_last
 *
 * Look up the last bit vector entry in a vector.
 *
 * Returns a pointer to the entry, or NULL if nothing's there.
 */
static bv_entry *
bv_ent_lookup_last (bit_vector *bv)
{
    bv_entry *bv_ent;

    /* If no vector, there's nothing there. */

    if (!bv)
	return NULL;

    /* If no tree, there's nothing here. */

    if (!bv->bv_root)
	return NULL;

    bv_ent =
	bv_patnode_to_bv_entry(bvx_patricia_lookup_greatest(bv->bv_root));

    return bv_ent;
}


/*
 * bv_destroy_tree
 *
 * Destroys the patricia tree root in the bit vector.
 */
static void
bv_destroy_tree (bit_vector *bv)
{
    /* Toss the tree. */

    bvx_patroot_destroy(bv->bv_root);
    bv->bv_root = NULL;
}


/*
 * bv_clean
 *
 * Destroy all bit vector entries on a bit vector.
 *
 * Leaves the vector squeaky clean.
 */
void
bv_clean (bit_vector *bv)
{
    bv_entry *bv_ent;

    /* No touching the vector from a callback routine. */

    bvx_assert(!bv->bv_cb_result);
    bvx_assert(!bv->bv_cb_source);

    /* Bail if the tree is pristine. */

    if (!bv->bv_root)
	return;

    /* Walk all of the vector entries and free them. */

    while (TRUE) {

	/* See if the next node is there. */

	bv_ent = bv_ent_lookup_first(bv);
	if (!bv_ent)
	    break;			/* All done */

	/* Got it.  Destroy it. */

	bv_ent_destroy(bv, bv_ent);
    }

    /* Toss the tree. */

    bv_destroy_tree(bv);
}

	    
/*
 * bv_set_bit
 *
 * Set a bit in a bit vector.
 *
 * May allocate a new bit vector entry.
 *
 * Returns the previous bit setting, or -1 if out of memory.
 */
int
bv_set_bit (bit_vector *bv, bv_bitnum_t bit_number)
{
    bv_entry *bv_ent;
    boolean bit_is_set;
    bv_word_t *bit_word;
    bv_word_t bit_mask;

    /* No touching the vector from a callback routine. */

    bvx_assert(!bv->bv_cb_result);

    /*
     * If this vector is a callback source, we better only be touching the
     * current bit.
     */
    if (bv->bv_cb_source)
	bvx_assert(bv->bv_callback_ord == bit_number);

    /* The bit number better be valid. */

    bvx_assert(bit_number < BV_MAX_BITNUM);

    /* Look up the bit vector entry. */

    bit_is_set = FALSE;
    bv_ent = bv_ent_lookup(bv, bit_number);
    bit_mask = bv_word_mask(bit_number);
    if (!bv_ent) {

	/* No entry.  Create one. */

	bv_ent = bv_ent_create(bv, bit_number);
	if (!bv_ent)
	    return -1;			/* Out of memory */
	bit_word = &bv_ent->bv_bits[bv_word_offset(bit_number)];

    } else {

	/* Entry is there.  Note if the bit is already set. */

	bit_word = &bv_ent->bv_bits[bv_word_offset(bit_number)];
	bit_is_set = ((*bit_word & bit_mask) != 0);
    }

    /* Got the entry.  If the bit isn't already set, do so now. */

    if (!bit_is_set) {
	*bit_word |= bit_mask;
	if (bv_ent->bv_setcount != BV_UNKNOWN_COUNT) {

	    /*
	     * We're manipulating bit counts.  Bump this one.  If it's
	     * full, take it off of the non-full thread.
	     */
	    bv_ent->bv_setcount++;
	    bvx_assert(bv_ent->bv_setcount <= BV_BITSIZE);
	    if (bv_ent->bv_setcount == BV_BITSIZE)
		thread_remove(&bv_ent->bv_ent_nonfull_thread);
	}
    }

    return bit_is_set;
}


/*
 * bv_clear_bit
 *
 * Clear a bit in a bit vector.
 *
 * Returns the previous bit setting.
 *
 * May free the vector entry.
 */
boolean
bv_clear_bit (bit_vector *bv, bv_bitnum_t bit_number)
{
    bv_entry *bv_ent;
    boolean bit_is_set;
    bv_word_t *bit_word;
    bv_word_t bit_mask;

    /* No touching the vector from a callback routine. */

    bvx_assert(!bv->bv_cb_result);

    /*
     * If this vector is a callback source, we better only be touching the
     * current bit.
     */
    if (bv->bv_cb_source)
	bvx_assert(bv->bv_callback_ord == bit_number);

    /* The bit number better be valid. */

    bvx_assert(bit_number < BV_MAX_BITNUM);

    /*
     * Look up the bit vector entry.  If it's not there, the bit is
     * already clear.
     */
    bit_is_set = FALSE;
    bv_ent = bv_ent_lookup(bv, bit_number);
    if (bv_ent) {
	bit_mask = bv_word_mask(bit_number);
	bit_word = &bv_ent->bv_bits[bv_word_offset(bit_number)];

	/* Entry is there.  Note if the bit is set. */

	bit_is_set = ((*bit_word & bit_mask) != 0);
	if (bit_is_set) {

	    /* Bit is set.  Clear it. */

	    *bit_word &= ~bit_mask;

	    /* Manipulate the bit count if appropriate. */

	    if (bv_ent->bv_setcount != BV_UNKNOWN_COUNT) {

		/* We're keeping track of bits.  Decrement this one. */

		bvx_assert(bv_ent->bv_setcount > 0);
		bv_ent->bv_setcount--;

		/* If all bits are clear, free the vector entry. */

		if (!bv_ent->bv_setcount) {

		    /*
		     * All bits are clear.  Destroy the entry unless we're
		     * on a callback with this vector as a source (we'll
		     * free the entry later if so.)
		     */
		    if (!bv->bv_cb_source) {
			bv_ent_destroy(bv, bv_ent);
		    }
		    bv_ent = NULL;
		}
	    }

	    /*
	     * If we still have an entry (it wasn't destroyed) we know that
	     * it is no longer full, since we just cleared a bit.  Add it
	     * to the not-full list in that case.
	     */
	    if (bv_ent) {
		if (!thread_node_on_thread(&bv_ent->bv_ent_nonfull_thread)) {
		    thread_circular_add_top(&bv->bv_nonfull_head,
					    &bv_ent->bv_ent_nonfull_thread);
		}
	    }
	}
    }

    return bit_is_set;
}


/*
 * bv_bit_is_set
 *
 * Returns TRUE if the specified bit is set, or FALSE if not.
 */
boolean
bv_bit_is_set (bit_vector *bv, bv_bitnum_t bit_number)
{
    boolean bit_is_set;
    bv_entry *bv_ent;
    bv_word_t bit_mask, bit_word;

    /* No touching the vector from a callback routine. */

    bvx_assert(!bv->bv_cb_result);

    /* The bit number better be valid. */

    bvx_assert(bit_number < BV_MAX_BITNUM);

    /* Look up the bit vector entry.  If it's not there, the bit is clear. */

    bit_is_set = FALSE;
    bv_ent = bv_ent_lookup(bv, bit_number);
    if (bv_ent) {

	/* Entry is there.  Note if the bit is set. */

	bit_mask = bv_word_mask(bit_number);
	bit_word = bv_ent->bv_bits[bv_word_offset(bit_number)];
	bit_is_set = ((bit_word & bit_mask) != 0);
    }

    return bit_is_set;
}


/*
 * bv_find_clear_in_ent
 *
 * Finds a clear bit in a vector entry.
 *
 * Returns the vector bit number, or BV_BAD_BITNUM if there are no
 * clear bits in the entry.
 *
 * As a side effect, if the entry turns out to have no free bits, we
 * set the bit count if we lost track earlier, since we've gone to the
 * trouble of looking at all the bits.
 */
static bv_bitnum_t
bv_find_clear_in_ent (bv_entry *bv_ent)
{
    uint32_t word_offset;
    bv_word_t bitword;
    int word_bitnum;

    /* Walk the words looking for a free bit. */

    for (word_offset = 0;  word_offset < BV_WORDSIZE; word_offset++) {
	bitword = bv_ent->bv_bits[word_offset];
	if (bitword != BV_ALLSET) {

	    /*
	     * Got a word with at least one clear bit.  Calculate the
	     * first clear bit.
	     */
	    word_bitnum = bv_first_clear(bitword);
	    bvx_assert(word_bitnum >= 0);
	    return (bv_ent->bv_start + (word_offset * BV_BITSPERWORD) +
		    word_bitnum);
	}
    }

    /* Didn't find one.  Set the bit count to the max. */

    bvx_assert(bv_ent->bv_setcount == BV_BITSIZE ||
	       bv_ent->bv_setcount == BV_UNKNOWN_COUNT);
    bv_ent->bv_setcount = BV_BITSIZE;

    return BV_BAD_BITNUM;		/* Didn't find one. */
}


/*
 * bv_first_clear_bit
 *
 * Returns the ordinal of the lowest-numbered zero bit.  One will
 * always be found unless there are two billion bits set, though it
 * may be past the end of the current array.
 *
 * If one is not particular about finding the lowest bit number,
 * bv_find_clear_bit() is potentially less expensive.
 *
 * Returns the bit number, or BV_BAD_BITNUM if all bits are exhausted.
 */
bv_bitnum_t
bv_first_clear_bit (bit_vector *bv)
{
    bv_entry *bv_ent;
    bv_bitnum_t bitnum, ret_bitnum;

    /* No touching the vector from a callback routine. */

    bvx_assert(!bv->bv_cb_result);

    /* Look up the first vector entry. */

    bv_ent = bv_ent_lookup_first(bv);
    bitnum = 0;

    /* Walk all tree entries. */

    while (TRUE) {

	/* If we've exceeded the maximum bit number, bail. */

	if (bitnum >= BV_MAX_BITNUM)
	    return BV_BAD_BITNUM;

	/* If there is no entry at this position, we've found a free bit. */

	if (!bv_entry_active_here(bv_ent, bitnum))
	    return bitnum;

	/*
	 * Process the entry if we think it's not full.  Note that this
	 * test will include any entries where we've lost track of the
	 * bit count, so such entries may in fact be full.
	 */
	if (bv_ent->bv_setcount != BV_BITSIZE) {

	    /* Find a clear bit in the entry. */

	    ret_bitnum = bv_find_clear_in_ent(bv_ent);
	    if (ret_bitnum != BV_BAD_BITNUM)
		return ret_bitnum;
	}

	/* Advance to the next entry. */

	bv_ent = bv_advance_entry(bv, bv_ent, bitnum);
	bitnum += BV_BITSIZE;
    }

    return BV_BAD_BITNUM;		/* Quiet the compiler. */
}


/*
 * bv_find_clear_bit
 *
 * Returns the ordinal of any zero bit.  One will always be found
 * (unless two billion bits are set), though it may be past the end
 * of the current array.
 *
 * This routine will always return a free bit in an existing vector
 * entry if it can, which may be more memory-efficient.
 *
 * This routine will potentially be more expensive if fast vector
 * operations are in use, as it may be forced to walk the tree
 * searching for free bits instead of pulling entries from the non-full
 * thread, though we try hard to optimize it.
 *
 * Returns the bit ordinal, or BV_BAD_BITNUM if all bits are exhausted.
 */
bv_bitnum_t
bv_find_clear_bit (bit_vector *bv)
{
    bv_entry *bv_ent;
    bv_bitnum_t bitnum, ret_bitnum;
    thread *thread_ptr;
    bv_bitnum_t missing_bitnum;
    boolean found_missing;

    /* No touching the vector from a callback routine. */

    bvx_assert(!bv->bv_cb_result);

    /* First, try the head of the non-full thread. */

    thread_ptr = thread_circular_top(&bv->bv_nonfull_head);
    bv_ent = bv_thread_to_bv_entry(thread_ptr);
    if (bv_ent) {

	/* Got an entry on the thread.  It better have a free bit. */

	ret_bitnum = bv_find_clear_in_ent(bv_ent);
	bvx_assert(ret_bitnum != BV_BAD_BITNUM);
	return ret_bitnum;
    }

    /*
     * Nothing in the non-full thread.  If fast vectors are off, this
     * means that all existing entries are definitely full, given that
     * there was nothing in the non-full list.  See if we've cached
     * a free entry, and use it if so.
     */
    if (!bv->bv_fastvects) {
	if (bv->bv_freed_ord != BV_BAD_BITNUM)
	    return bv->bv_freed_ord;

	/*
	 * No freed ordinal was cached.  Look up the last entry in the
	 * tree and return the next ordinal after that.
	 */
	bv_ent = bv_ent_lookup_last(bv);

	/*
	 * See if we got an entry.  If we didn't, the tree is completely
	 * empty, so we return bit number zero.
	 */
	if (!bv_ent)
	    return 0;

	/*
	 * Got the last entry.  If we're running out of bit numbers,
	 * fall through to a brute-force search.  Otherwise, return
	 * the bit number of the next (as yet nonexistent) block.
	 */
	if (bv_ent->bv_start < (BV_MAX_BITNUM - BV_BITSIZE))
	    return bv_ent->bv_start + BV_BITSIZE;
    }

    /*
     * If we've gotten here, we need to do a brute-force search to find an
     * entry with a free bit.  First, look up the first vector entry.
     */
    bv_ent = bv_ent_lookup_first(bv);
    bitnum = 0;
    missing_bitnum = 0;
    found_missing = FALSE;

    /* Walk all tree entries. */

    while (bv_ent) {

	/* If we've exceeded the maximum bit number, bail. */

	if (bitnum >= BV_MAX_BITNUM)
	    return BV_BAD_BITNUM;

	/*
	 * If there is no entry at this position, and it's the first one
	 * that was empty, note it.
	 */
	if (!bv_entry_active_here(bv_ent, bitnum) && !found_missing) {
	    missing_bitnum = bitnum;
	    found_missing = TRUE;
	}

	/* Update the bit position to match the entry. */

	bitnum = bv_ent->bv_start;

	/* Found an entry.  Process the entry if it might not be full. */

	if (bv_ent->bv_setcount != BV_BITSIZE) {

	    /* Possibly not full.  Walk the words looking for a free bit. */

	    ret_bitnum = bv_find_clear_in_ent(bv_ent);
	    if (ret_bitnum != BV_BAD_BITNUM)
		return ret_bitnum;
	}

	/* Advance to the next entry. */

	bv_ent = bv_next_entry(bv, bv_ent);
	bitnum += BV_BITSIZE;
    }

    /*
     * If we've gotten this far, we didn't find any free bits in any
     * vector entry that exists.  If we found a hole in the entry
     * space (indicating a block of free bits), return that value.
     */
    if (found_missing)
	return missing_bitnum;

    /*
     * The entire bit array is packed full.  Return the bit number of
     * the next (nonexistent) entry unless we've hit the max.
     */
    if (bitnum >= BV_MAX_BITNUM)
	return BV_BAD_BITNUM;

    return bitnum;
}


/*
 * bv_first_set_bit
 *
 * Returns the ordinal of the first nonzero bit, or BV_BAD_BITNUM if
 * none are found.
 */
bv_bitnum_t
bv_first_set_bit (bit_vector *bv)
{
    bv_entry *bv_ent;
    uint32_t word_offset;
    bv_bitnum_t bitnum;
    int word_bitnum;
    bv_word_t bitword;

    /* No touching the vector from a callback routine. */

    bvx_assert(!bv->bv_cb_result);
    bvx_assert(!bv->bv_cb_source);

    /*
     * Walk all tree entries.  If we haven't done any fast set
     * operations, we're guaranteed to find a set bit in the first
     * block, so this is pretty cheap.  But if any fast vector operations
     * have been done, there may be empty blocks on the tree, so, we
     * may potentially walk all blocks and never find anything.
     */
    while (TRUE) {

	/* Look up the first vector entry. */

	bv_ent = bv_ent_lookup_first(bv);

	/* Bail if there are no set bits. */

	if (!bv_ent)
	    break;

	/* Find the first nonzero bit in the vector entry. */

	bitnum = bv_ent->bv_start;
	for (word_offset = 0;  word_offset < BV_WORDSIZE; word_offset++) {
	    bitword = bv_ent->bv_bits[word_offset];
	    if (bitword) {

		/* Got a word with bits set.  Calculate the first set bit. */

		word_bitnum = bv_first_set(bitword);
		bvx_assert(word_bitnum >= 0);
		return (bitnum + (word_offset * BV_BITSPERWORD) + word_bitnum);
	    }
	}

	/*
	 * If we've gotten this far, we walked all of the words of a
	 * vector entry and saw no set bits.  This can happen if we're
	 * doing fast vector operations, since we may have lost track
	 * of the bit count.  Delete the block and fetch the next one.
	 */
	bvx_assert(bv_ent->bv_setcount == 0 ||
		   bv_ent->bv_setcount == BV_UNKNOWN_COUNT);
	bv_ent_destroy(bv, bv_ent);
    }

    return BV_BAD_BITNUM;
}


/*
 * bv_update_result
 *
 * Update a result word with an updated value.
 *
 * Returns the net number of bits set in the entry.  Returns BV_MAX_BITNUM
 * if the callback routine aborted the walk.
 *
 * This routine can be called with a NULL destination pointer.  In this
 * case no result bits are generated, but callbacks are made (it's not
 * too useful if there is no callback pointer.)
 *
 * If the Fast Vector flag is set and no callback was supplied, just
 * do straight word copies and flag that the bit count in the entry
 * has been lost.
 *
 * This routine is pretty ugly because it tries to do some optimizations.
 * The core issues are this, assuming fast vector processing isn't happening.
 * Firstly, if the value and the existing destination values are different,
 * the bit count in the destination needs to be updated.  Secondly, if
 * a bits-set callback is provided, we need to process every bit set in the
 * value, even if the destination bit is the same.
 */
static int
bv_update_result (bit_vector *bv, bv_entry *dest_ent, uint32_t word_index,
		  bv_word_t value, bv_bitnum_t word_bitnum,
		  bit_vector *src_bv1, bit_vector *src_bv2,
		  bv_callback callback, void *context,
		  bv_callback_option cb_opt)
{
    bv_word_t dest_copy, value_copy;
    bv_word_t *dest_ptr;
    int net_set_count;
    uint32_t dest_bit, value_bit;
    uint32_t byte_ix, bit_ix;
    uint8_t dest_copy_byte, value_copy_byte;
    boolean cb_bitset;
    boolean abort_walk;
    bv_bitnum_t cur_bitnum;

    /* Flag that we might be doing a callback. */

    if (bv)
	bv->bv_cb_result = TRUE;
    if (src_bv1)
	src_bv1->bv_cb_source = TRUE;
    if (src_bv2)
	src_bv2->bv_cb_source = TRUE;

    /* Note if we need to callback on bit sets. */

    cb_bitset = (callback && (cb_opt == BV_CALL_SET));

    /*
     * Initialize.  If no destination was specified, assume the old
     * bits were zero.
     */
    net_set_count = 0;
    if (dest_ent) {
	dest_ptr = &dest_ent->bv_bits[word_index];
	dest_copy = *dest_ptr;
    } else {
	dest_ptr = NULL;
	dest_copy = 0;
    }
    value_copy = value;

    /* Update the result word. */

    if (dest_ptr)
	*dest_ptr = value;

    /*
     * See if we're doing fast vector processing without a callback.
     * If so, we don't need to walk the bits.
     */
    if (bv && bv->bv_fastvects && !callback) {

	/*
	 * Doing fast vector processing.  Flag that we're losing track
	 * of the bit count.  Also remove the entry from the nonfull
	 * thread, since we don't know if it's not full.
	 */
	if (dest_ent) {
	    dest_ent->bv_setcount = BV_UNKNOWN_COUNT;
	    thread_remove(&dest_ent->bv_ent_nonfull_thread);
	}

    } else if (!callback) {

	/*
	 * There's no callback, so we don't have to walk all of the bits.
	 * We just need to calculate the difference in bit count between
	 * the old and new values.
	 */
	net_set_count = bv_bitcount(value_copy) - bv_bitcount(dest_copy);

    } else {

	/*
	 * Walk each byte of the word, and each bit therein, and call
	 * the callback for each indicated bit if one has been
	 * specified.
	 */
	for (byte_ix = 0; byte_ix < sizeof(bv_word_t); byte_ix++) {

	    /*
	     * See if we can bail from the loop.  We can do so if
	     * the remaining word values are equal (so we don't
	     * need to do any more bit count updates) and either
	     * we're not doing bit-set callbacks or both word values
	     * are zero.
	     */
	    if (dest_copy == value_copy && (!cb_bitset || value_copy == 0))
		break;

	    /*
	     * See if we have to process this byte .  We need to do so
	     * if the bytes are unequal (so we can update the
	     * destination bit count), or if we're calling back on set
	     * bits and the result byte is nonzero.
	     */
	    dest_copy_byte = dest_copy & 0xff;
	    value_copy_byte = value_copy & 0xff;

	    if (dest_copy_byte != value_copy_byte ||
		(cb_bitset && dest_copy_byte != 0)) {

		/* Walk each bit in the byte. */

		for (bit_ix = 0;  bit_ix < 8;  bit_ix++) {

		    /*
		     * See if we can exit early.  We can do so if the
		     * remaining bits are all the same and either
		     * we're not calling back on set bits (no bit
		     * count to adjust and no callback to make) or
		     * both the remaining value and result bits are
		     * all zero (ditto.)
		     */
		    if ((dest_copy_byte == value_copy_byte) &&
			(!cb_bitset || dest_copy_byte == 0)) {
			break;
		    }

		    /*
		     * We still need to continue .  Mask off the low order
		     * bits.  We need to call back if either CHANGE is
		     * indicated and the bits are different, or if SET is
		     * indicated and the result bit is set.
		     */
		    dest_bit = dest_copy_byte & 1;
		    value_bit = value_copy_byte & 1;
		    net_set_count += value_bit - dest_bit;
		    if (callback &&
			((dest_bit != value_bit && cb_opt == BV_CALL_CHANGE) ||
			 (value_bit && cb_opt == BV_CALL_SET))) {
			cur_bitnum = word_bitnum + (byte_ix * 8) + bit_ix;
			if (src_bv1)
			    src_bv1->bv_callback_ord = cur_bitnum;
			if (src_bv2)
			    src_bv2->bv_callback_ord = cur_bitnum;
			abort_walk =
			    (*callback)(context, cur_bitnum,
					(boolean) value_bit,
					(boolean) dest_bit);
			if (abort_walk) {
			    net_set_count = BV_MAX_BITNUM;
			    goto bail;
			}
		    }

		    /* Shift the bytes down a bit. */

		    dest_copy_byte >>= 1;
		    value_copy_byte >>= 1;
		}
	    }

	    /* Shift the words down a byte. */

	    dest_copy >>= 8;
	    value_copy >>= 8;
	}
    }

  bail:

    /* Clear the callback flags. */

    if (bv)
	bv->bv_cb_result = FALSE;
    if (src_bv1)
	src_bv1->bv_cb_source = FALSE;
    if (src_bv2)
	src_bv2->bv_cb_source = FALSE;

    return net_set_count;
}


/*
 * bv_clear_result_entry
 *
 * The result of a bit operation is all zeros.  Clear out the result entry,
 * if present, and free it.
 */
static void
bv_clear_result_entry (bit_vector *src1, bit_vector *src2, bit_vector *result,
		       bv_entry *result_ent, bv_bitnum_t start_bitnum,
		       bv_callback callback, void *context,
		       bv_callback_option cb_opt)
{
    uint32_t i;
    int net_set_count;

    /* Only bother if there's an entry here. */

    if (bv_entry_active_here(result_ent, start_bitnum)) {

	/* Update the vector if there is a callback. */

	if (callback) {
	    for (i = 0;  i < BV_WORDSIZE; i++) {
		net_set_count =
		    bv_update_result(result, result_ent, i, 0,
				     start_bitnum + (i * BV_BITSPERWORD),
				     src1, src2, callback, context, cb_opt);
		if (net_set_count == BV_MAX_BITNUM)
		    return;		/* Callback aborted */
	    }
	}

	/* Destroy the result entry. */

	bv_ent_destroy(result, result_ent);
    }
}


/*
 * bv_clear_all_bits
 *
 * Clears all bits in the bit vector.  All vector entries are freed as a
 * side effect, along with the tree root.  This is equivalent to bv_clean,
 * but with a callback for each cleared bit.
 */
void
bv_clear_all_bits (bit_vector *bv, bv_callback callback, void *context,
		   bv_callback_option cb_opt)
{
    bv_entry *bv_ent;

    /* No touching the vector from a callback routine. */

    bvx_assert(!bv->bv_cb_result);
    bvx_assert(!bv->bv_cb_source);

    /*
     * If there's no callback, simply free the vector entries.
     */
    if (!callback) {
	bv_clean(bv);
	return;
    }

    /* Walk the entire vector entry tree. */

    while (TRUE) {

	/* Look up the first vector entry. */

	bv_ent = bv_ent_lookup_first(bv);

	/* Bail if there's nothing left. */

	if (!bv_ent)
	    break;

	/* Clear the vector entry. */

	bv_clear_result_entry(NULL, NULL, bv, bv_ent, bv_ent->bv_start,
			      callback, context, cb_opt);
    }

    /* Get rid of the tree root. */

    bv_destroy_tree(bv);
}


/*
 * bv_copy_result
 *
 * Copy a vector entry to a result entry, and make any necessary
 * callbacks.  If there is no copied entry, clear the result entry
 * instead.
 *
 * If there is no result pointer, the callbacks are made but the results
 * are not stored.
 *
 * Returns 0 if all OK, or -1 if out of memory.
 */
static int
bv_copy_result (bit_vector *src1, bit_vector *src2, bv_entry *copy_ptr,
		bit_vector *result, bv_entry *result_ent,
		bv_bitnum_t start_bitnum, bv_callback callback, void *context,
		bv_callback_option cb_opt)
{
    int net_set_count;
    int set_count_delta;
    uint32_t i;

    /* See if the entry copied from is present. */

    if (bv_entry_active_here(copy_ptr, start_bitnum)) {

	/*
	 * Something to copy.  See if there is a result entry in this
	 * position.
	 */
	if (result && !bv_entry_active_here(result_ent, start_bitnum)) {

	    /*
	     * No active entry present here.  Create a result entry at
	     * the new position.
	     */
	    result_ent = bv_ent_create(result, start_bitnum);
	    if (!result_ent)
		return -1;	/* Out of memory */
	}

	/*
	 * If doing fast vector operations without a callback, just
	 * copy the bits.
	 */
	if (result && result->bv_fastvects && !callback) {
            memmove(&result_ent->bv_bits, &copy_ptr->bv_bits, sizeof(result_ent->bv_bits));
	    result_ent->bv_setcount = BV_UNKNOWN_COUNT;
	    thread_remove(&result_ent->bv_ent_nonfull_thread);

	} else {

	    /* Update the bits. */

	    net_set_count = 0;
	    for (i = 0; i < BV_WORDSIZE; i++) {
		set_count_delta =
		    bv_update_result(result, result_ent, i,
				     copy_ptr->bv_bits[i],
				     start_bitnum + (i * BV_BITSPERWORD),
				     src1, src2, callback, context, cb_opt);
		if (set_count_delta == BV_MAX_BITNUM)
		    return 0;		/* Callback aborted */
		net_set_count += set_count_delta;
	    }

	    if (result_ent && result_ent->bv_setcount != BV_UNKNOWN_COUNT) {
		result_ent->bv_setcount += net_set_count;
		bvx_assert(result_ent->bv_setcount >= 0);
	    }
	}

    } else {

	/* Nothing to copy.  Clear the result entry if present. */

	if (result)
	    bv_clear_result_entry(src1, src2, result, result_ent, start_bitnum,
				  callback, context, cb_opt);
    }

    return 0;
}


/*
 * Vector operation types
 */
typedef enum {
    VEC_OP_AND,				/* AND operation */
    VEC_OP_OR,				/* OR operation */
    VEC_OP_XOR,				/* XOR operation */
    VEC_OP_CLEAR,			/* CLEAR operation */
    VEC_OP_COPY,			/* Copy operation */
    VEC_OP_WALK				/* Walk operation */
} vector_op_type;


/*
 * bv_vector_op
 *
 * Perform the specified operation on two bit vectors, and store the result.
 *
 * The result may be the same as one of the parameters.
 *
 * Returns 0 if all ok, or -1 if out of memory.
 *
 * If a callback is supplied, it is called based on the callback
 * option type--either for every bit changed in the result, or for
 * every bit set in the result.
 *
 * We crawl down the three vectors (two sources and the result) and
 * perform the operation requested.  Each of the vectors may have
 * holes in it (corresponding to a block of zero bits) and the code
 * deals with that properly.
 *
 * We're lazy and pull the destination block off of the not-full list.
 * Otherwise we'd have to keep track from down in the bowels as to
 * whether the current destination entry was freed, and pass that all
 * the way up here in order to know whether the pointer is valid.
 */
static int
bv_vector_op (vector_op_type op_type, bit_vector *first, bit_vector *second,
	      bit_vector *result, bv_callback callback, void *context,
	      bv_callback_option cb_opt)
{
    bv_entry *first_ent, *second_ent, *result_ent;
    bv_entry *first_next, *second_next, *result_next;
    bv_bitnum_t start_bitnum;
    bv_word_t result_bits;
    int net_set_count;
    int set_count_delta;
    bv_entry scratch_ent;
    bv_entry *result_ptr;
    bv_entry *copy_ptr;
    uint32_t i;
    boolean local_result;

    /* No null result with no callback. */

    bvx_assert(!(!result && !callback));

    /* Sources must be different. */

    bvx_assert(first != second);

    /* No touching the result vector from a callback routine. */

    if (result)
	bvx_assert(!result->bv_cb_result);

    /* Walk the vectors. */

    first_ent = bv_ent_lookup_first(first);
    second_ent = bv_ent_lookup_first(second);
    result_ent = bv_ent_lookup_first(result);

    while (first_ent || second_ent || result_ent) {

	/*
	 * Update our current start bit position.  It's the lowest of
	 * the positions of any of the current vector entries.
	 */
	start_bitnum = BV_MAX_BITNUM;
	if (first_ent && first_ent->bv_start < start_bitnum)
	    start_bitnum = first_ent->bv_start;
	if (second_ent && second_ent->bv_start < start_bitnum)
	    start_bitnum = second_ent->bv_start;
	if (result_ent && result_ent->bv_start < start_bitnum)
	    start_bitnum = result_ent->bv_start;

	/*
	 * Get the next entry for each vector, in case we end up deleting
	 * an entry.  We advance any entry that matches our bit position.
	 */
	first_next = bv_advance_entry(first, first_ent, start_bitnum);
	second_next = bv_advance_entry(second, second_ent, start_bitnum);
	result_next = bv_advance_entry(result, result_ent, start_bitnum);

	/*
	 * Take the result entry off of the not-full list, since it
	 * isn't easy to keep track of this once we get done toying
	 * with the entry (or freeing it.)
	 */
	if (bv_entry_active_here(result_ent, start_bitnum))
	    thread_remove(&result_ent->bv_ent_nonfull_thread);

	/*
	 * See if we're missing one or the other of the parameters (or both.)
	 */
	if (!bv_entry_active_here(first_ent, start_bitnum) ||
	    !bv_entry_active_here(second_ent, start_bitnum)) {

	    /*
	     * One of them is missing.  Optimize this case based on
	     * the operation.
	     */
	    switch (op_type) {
	      case VEC_OP_AND:

		/*
		 * AND operation.  The result will be all clear.  Clear
		 * the result, if any.
		 */
		bv_clear_result_entry(first, second, result, result_ent,
				      start_bitnum, callback, context, cb_opt);
		break;

	      case VEC_OP_OR:
	      case VEC_OP_XOR:

		/*
		 * OR or XOR operation.  Copy whichever entry is
		 * present, if any, to the result.  This has the side
		 * effect of clearing the result if the entry is all
		 * zero.
		 */
		copy_ptr = NULL;
		if (bv_entry_active_here(first_ent, start_bitnum)) {
		    copy_ptr = first_ent;
		} else {
		    copy_ptr = second_ent;
		}
		if (bv_copy_result(first, second, copy_ptr, result, result_ent,
				   start_bitnum, callback, context,
				   cb_opt) < 0) {
		    return -1;		/* Out of memory */
		}
		break;

	      case VEC_OP_CLEAR:

		/*
		 * Clear operation.  If the first entry isn't present, clear
		 * the result.
		 */
		if (!bv_entry_active_here(first_ent, start_bitnum)) {
		    bv_clear_result_entry(first, second, result, result_ent,
					  start_bitnum, callback, context,
					  cb_opt);
		} else {

		    /*
		     * The first entry is here (meaning that the second is
		     * not.)  Copy it to the result.
		     */
		    if (bv_copy_result(first, second, first_ent, result,
				       result_ent, start_bitnum, callback,
				       context, cb_opt) < 0)
			return -1;	/* Out of memory */
		}
		break;

	      case VEC_OP_COPY:

		/*
		 * Copy operation.  Copy the first vector, if present,
		 * to the result.  This has the side effect of
		 * clearing the result if it is all zero.
		 */
		if (bv_copy_result(first, second, first_ent, result,
				   result_ent, start_bitnum, callback, context,
				   cb_opt) < 0) {
		    return -1;		/* Out of memory */
		}
		break;

	      case VEC_OP_WALK:

		/*
		 * Walk operation.  Copy the first vector to the result, which
		 * is known to be NULL.  This results in executing the callback
		 * for every set bit.
		 */
		if (bv_copy_result(first, second, first_ent, result,
				   result_ent, start_bitnum, callback, context,
				   cb_opt) < 0) {
		    return -1;		/* Out of memory */
		}
		break;

	      default:
		bvx_assert(FALSE);
		break;
	    }

	} else {

	    /*
	     * Both the first and second entries have something at this
	     * position.  Walk the words, performing the operation.  We use a
	     * local temporary entry if there is no result entry at
	     * this point.
	     */
	    net_set_count = 0;
	    if (bv_entry_active_here(result_ent, start_bitnum)) {
		result_ptr = result_ent;
		local_result = FALSE;
	    } else {
                memset(&scratch_ent, 0, sizeof(scratch_ent));
		scratch_ent.bv_start = start_bitnum;
		result_ptr = &scratch_ent;
		local_result = TRUE;
	    }
	    for (i = 0; i < BV_WORDSIZE; i++) {
		switch (op_type) {
		  case VEC_OP_AND:
		    result_bits =
			first_ent->bv_bits[i] & second_ent->bv_bits[i];
		    break;

		  case VEC_OP_OR:
		    result_bits =
			first_ent->bv_bits[i] | second_ent->bv_bits[i];
		    break;

		  case VEC_OP_XOR:
		    result_bits =
			first_ent->bv_bits[i] ^ second_ent->bv_bits[i];
		    break;

		  case VEC_OP_CLEAR:
		    result_bits =
			first_ent->bv_bits[i] & ~(second_ent->bv_bits[i]);
		    break;

		  default:
		    bvx_assert(FALSE);
		    result_bits = 0;	/* Quiet the compiler */
		}

		/*
		 * Slightly grody hack.  If we're updating a local result,
		 * we don't want to call the callback, since that will
		 * happen when we do the copy later.
		 */
		set_count_delta =
		    bv_update_result(result, result_ptr, i, result_bits,
				     start_bitnum + (i * BV_BITSPERWORD),
				     first, second,
				     (local_result ? NULL : callback),
				     context, cb_opt);
		if (set_count_delta == BV_MAX_BITNUM)
		    return 0;		/* Callback aborted */
		net_set_count += set_count_delta;
	    }

	    /* See if there was a result entry at this location. */

	    if (!local_result) {

		/*
		 * We were updating a live result entry.  Update the count.
		 * Delete the entry if all bits are clear and we're allowed
		 * to release blocks.
		 */
		if (result_ent->bv_setcount != BV_UNKNOWN_COUNT) {
		    result_ent->bv_setcount += net_set_count;
		    bvx_assert(result_ent->bv_setcount >= 0);
		    if (result_ent->bv_setcount == 0) {
			bv_ent_destroy(result, result_ent);
			result_ent = NULL;
		    }
		}

	    } else {

		/*
		 * There was no live result entry.  See if there were
		 * any nonzero bits created (if we can tell.)  If not,
		 * we're done.
		 */
		if (net_set_count ||
		    scratch_ent.bv_setcount == BV_UNKNOWN_COUNT) {

		    /*
		     * Copy the results into the result vector
		     * (creating a new entry.)
		     */
		    if (scratch_ent.bv_setcount != BV_UNKNOWN_COUNT)
			scratch_ent.bv_setcount = net_set_count;
		    if (bv_copy_result(first, second, &scratch_ent, result,
				       result_ent, start_bitnum, callback,
				       context, cb_opt) < 0) {
			return -1;	/* Out of memory */
		    }
		}
	    }
	}

	/*
	 * We're done with the current entry.  Try freeing the source
	 * entries in case the callback routine cleared the last bits.
	 * We need to look up the source entries again, since one or
	 * the other may have been freed above as a side effect if
	 * one is being used as the result vector.
	 */
	first_ent = bv_ent_lookup(first, start_bitnum);
	bv_attempt_entry_free(first, first_ent);
	second_ent = bv_ent_lookup(second, start_bitnum);
	bv_attempt_entry_free(second, second_ent);

	/* Advance the pointers. */

	first_ent = first_next;
	second_ent = second_next;
	result_ent = result_next;
    }

    return 0;
}


/*
 * bv_and_vectors
 *
 * Perform an AND operation on two bit vectors, and store the result.
 *
 * The result may be the same as one of the parameters.
 *
 * Returns 0 if all ok, or -1 if out of memory.
 *
 * If a callback is supplied, it is called for every bit that either changes
 * or is set in the result, according to cb_opt.
 */
int
bv_and_vectors (bit_vector *first, bit_vector *second, bit_vector *result,
		bv_callback callback, void *context, bv_callback_option cb_opt)
{
    return bv_vector_op(VEC_OP_AND, first, second, result, callback, context,
			cb_opt);
}


/*
 * bv_or_vectors
 *
 * Perform an OR operation on two bit vectors, and store the result.
 *
 * The result may be the same as one of the parameters.
 *
 * Returns 0 if all ok, or -1 if out of memory.
 *
 * If a callback is supplied, it is called for every bit that either changes
 * or is set in the result, according to cb_opt.
 */
int
bv_or_vectors (bit_vector *first, bit_vector *second, bit_vector *result,
	       bv_callback callback, void *context, bv_callback_option cb_opt)
{
    return bv_vector_op(VEC_OP_OR, first, second, result, callback, context,
			cb_opt);
}


/*
 * bv_xor_vectors
 *
 * Perform an XOR operation on two bit vectors, and store the result.  This
 * is also handy as a compare operation;  if BV_CALL_CHANGE is used, a
 * callback will be made for every bit that is different between the two.
 *
 * The result may be the same as one of the parameters.
 *
 * Returns 0 if all ok, or -1 if out of memory.
 *
 * If a callback is supplied, it is called for every bit that either changes
 * or is set in the result, according to cb_opt.
 */
int
bv_xor_vectors (bit_vector *first, bit_vector *second, bit_vector *result,
		bv_callback callback, void *context, bv_callback_option cb_opt)
{
    return bv_vector_op(VEC_OP_XOR, first, second, result, callback, context,
			cb_opt);
}


/*
 * bv_clear_vectors
 *
 * Perform a Clear operation on two bit vectors, and store the result.
 * Any set bits in the second parameter are cleared from the first.
 *
 * The result may be the same as one of the parameters.
 *
 * Returns 0 if all ok, or -1 if out of memory.
 *
 * If a callback is supplied, it is called for every bit that either changes
 * or is set in the result, according to cb_opt.
 */
int
bv_clear_vectors (bit_vector *first, bit_vector *second, bit_vector *result,
		  bv_callback callback, void *context,
		  bv_callback_option cb_opt)
{
    return bv_vector_op(VEC_OP_CLEAR, first, second, result, callback,
			context, cb_opt);
}


/*
 * bv_copy_vector
 *
 * Copy a bit vector to another one.
 *
 * Returns 0 if all ok, or -1 if out of memory.
 *
 * If a callback is supplied, it is called for every bit that either changes
 * or is set in the result, according to cb_opt.
 */
int
bv_copy_vector (bit_vector *src, bit_vector *dest, bv_callback callback,
		void *context, bv_callback_option cb_opt)
{
    return bv_vector_op(VEC_OP_COPY, src, NULL, dest, callback,	context,
			cb_opt);
}


/*
 * bv_walk_vector
 *
 * Walk a bit vector, 
 *
 * Returns 0 if all ok, or -1 if out of memory.
 *
 * The callback is called for every bit that is set in the vector,
 * according to cb_opt.
 */
int
bv_walk_vector (bit_vector *vect, bv_callback callback,	void *context)
{
    return bv_vector_op(VEC_OP_WALK, vect, NULL, NULL, callback, context,
			BV_CALL_SET);
}
