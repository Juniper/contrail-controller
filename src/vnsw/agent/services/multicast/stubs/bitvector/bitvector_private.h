/* $Id: bitvector_private.h 346474 2009-11-14 10:18:58Z ssiano $
 *
 * bitvector_private.h - Bit vector manipulation private definitions
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef __BITVECTOR_PRIVATE_H__
#define __BITVECTOR_PRIVATE_H__

/*
 * This module defines a bit vector and a set of operations thereon.
 *
 * A bit vector is constructed out of a patricia tree of bit vector entries,
 * each of which contains a block of bits and some housekeeping information.
 *
 * The patricia tree allows for rapid access to a bit vector entries, and
 * also allows for sparse vectors (missing entries are equivalent to a set
 * of all zero bits.)
 *
 * The size of a bit vector entry is defined in the environment file at
 * compile time.  Larger entries are more memory-efficient for larger
 * vectors (less overhead), but less so for smaller vectors (more wasted
 * space.)  The computational cost is slightly higher for smaller entries
 * when vectors are large.
 */


/*
 * Size definitions
 *
 * Sizing is driven by the definition of BV_BITSIZE_LOG2, the base 2
 * log of the number of bits in a vector entry.  The size of each word
 * in the vector is defined by bv_word_t.  All are defined in the
 * environment file.
 */
#define BV_BITSPERWORD    (sizeof(bv_word_t) * 8)    /* Bits per word */
#define BV_BITSIZE    (1 << (BV_BITSIZE_LOG2)) /* Number of bits per entry */
#define BV_WORDSIZE    ((BV_BITSIZE + BV_BITSPERWORD - 1) / BV_BITSPERWORD)


/*
 * Bit vector entry
 *
 * A bit vector entry contains a block of bits with a position offset.
 * The bits are numbered from the low order position upward.
 *
 * Each block has a starting bit number (its offset into the overall
 * bit vector) and is stored in a patricia tree with the starting bit
 * number as the key.
 *
 * Entries that aren't completely full are threaded from the bit
 * vector head to make finding a free bit easier.
 *
 * In general we try to keep a count of how many bits are set in this
 * entry, since it speeds up some operations.  However, in order to do
 * fast vector operations we can't afford to keep track of the bit count,
 * so once we lose track of the bit count we set it to BV_UNKNOWN_COUNT
 * to flag this case.
 */
typedef struct bv_entry_ {
    thread bv_ent_nonfull_thread;    /* Entry on non-full-entry thread */
    bvx_patnode bv_ent_node;        /* Patricia node */
    uint8_t bv_key[sizeof(bv_bitnum_t)]; /* Patricia key */
    bv_bitnum_t bv_start;        /* Starting bit number */
    int bv_setcount;            /* Number of set bits */
    bv_word_t bv_bits[BV_WORDSIZE];    /* Actual bits */
} bv_entry;

BVX_PATNODE_TO_STRUCT(bv_patnode_to_bv_entry, bv_entry, bv_ent_node);
THREAD_TO_STRUCT(bv_thread_to_bv_entry, bv_entry, bv_ent_nonfull_thread);

#define BV_UNKNOWN_COUNT -1         /* bv_setcount unknown */
#define BV_ALLSET ((bv_word_t)(~0))    /* All-ones constant */
#define BV_MAX_BITNUM ((0x7fffffff - BV_BITSIZE) + 1) /* Max bit num + 1 */


/*
 * bv_start_bit
 *
 * Returns the starting bit number of the bit vector entry corresponding
 * to a particular bit number.
 */
static inline bv_bitnum_t
bv_start_bit (bv_bitnum_t bit_number)
{
    return bit_number & (~(BV_BITSIZE - 1));
}


/*
 * bv_word_offset
 *
 * Returns the word offset into a vector entry of a bit vector word,
 * given the bit number.
 *
 * Don't worry, all that integer math should be optimized to a shift and
 * a mask.
 */
static inline uint32_t
bv_word_offset (bv_bitnum_t bit_number)
{
    return ((bit_number % (BV_BITSIZE)) / BV_BITSPERWORD);
}

/*
 * bv_word_mask
 *
 * Returns the word mask corresponding to a bit, given the bit number.
 */
static inline bv_word_t
bv_word_mask (bv_bitnum_t bit_number)
{
    return 1 << (bit_number & (BV_BITSPERWORD - 1));
}

#endif /* __BITVECTOR_PRIVATE_H__ */
