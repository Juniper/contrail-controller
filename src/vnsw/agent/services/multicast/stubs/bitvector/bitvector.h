/* $Id: bitvector.h 346474 2009-11-14 10:18:58Z ssiano $
 *
 * bitvector.h - Bit vector manipulation definitions
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/*
 * Overview
 *
 * This module provides generalized manipulation of arbitrary-size
 * bit vectors.  Single-bit operations are provided (set, clear, test,
 * find) as well as vector set operations (and, or, xor, clear, copy,
 * walk).
 *
 * When performing vector operations, a callback can optionally
 * be provided that will be called either for every bit in the result
 * that changes, or any bit that is set after the operation.  This allows
 * for efficient generalized vector operations for which the bit vectors
 * represent set elements.
 *
 * The bit vector is represented by a bit_vector structure, typically
 * embedded in another structure, to which are attached zero or more
 * bit vector entries.  This allows for sparse bit vectors, as vector
 * entries are only allocated for bits that are set.
 *
 * Vector operations are normally fairly expensive, as per-bit
 * analysis must be done in order to perform per-bit callbacks and to
 * keep track of how many bits in each entry are set in order to do
 * pruning of the tree as entries are completely cleared.  However,
 * vector operations can be significantly sped up if the "fast_vects"
 * flag is set on the call to bv_init_vector--the vector operations
 * then become word-sized and no per-bit operations are done.  The
 * downside to this is that pruning the tree of empty blocks does not
 * generally occur (which can eat memory), and this in turn can slow
 * down bit-find operations.  It is up to the application to weigh the
 * tradeoffs.
 *
 *
 * Callbacks
 *
 * For vector set operations, an optional callback is provided with
 * the bit number, the old and new values of the bit, and the context
 * pointer provided by the original caller.  If no callback is
 * supplied, the only action is to store the results of the set
 * operation.
 *
 * If the callback option BV_CALL_SET is used, the callback will be
 * called for every bit in the result that is set to one.  If
 * BV_CALL_CHANGE is used, the callback will be called for every bit
 * in the result that changed.
 *
 * If a callback is specified, the result vector pointer may be NULL;
 * in this case the previous value of the result vector is assumed to
 * be zero for purposes of deciding which bits should be called back
 * about (and the results of the operation are otherwise discarded.)
 * Note that in this case, BV_CALL_SET and BV_CALL_CHANGE will give
 * the same results.
 *
 * A callback can do almost anything *except* attempt any operations
 * of any sort on the result vector, because it will be in an
 * inconsistent state at the time of the callback.  This is enforced
 * by assertion.  Callbacks must glean any information by other means.
 * A callback may set or clear the current bit in one of the source
 * vectors, but may not touch any other bit (for the same reason.)
 */

/*
 * Calling Sequence
 *
 * Each bitvector to be manipulated must be first initialized with a call
 * to bv_init_vector() prior to use.  Once initialized, the contents
 * cannot be manipulated directly, and the structure containing the vector
 * must not be freed, lest memory leaks result.
 *
 * After initialization, any of the single bit or vector operations may be
 * performed.
 *
 * When the vector is no longer needed, either bv_clean() or
 * bv_clear_all_bits() must be called to clean up and free memory.  Once
 * this is done, the bitvector can be discarded.
 *
 *
 * Initialization:
 *
 *   bv_init_vector(bit_vector *bv, boolean fast_vects)
 *     Initialize a new bit vector structure.  "fast_vects" specifies whether
 *     the caller wishes to use fast vector operations (see above for details.)
 *
 *   bv_clean(bit_vector *bv)
 *     Clears all bits and frees all memory associated with a bit vector.
 *     This is a fast way to zero a vector, and is also required to avoid
 *     memory leaks before freeing the structure in which the bit_vector
 *     structure is embedded.
 *
 *
 * Bit operations:
 *
 *   bv_set_bit(bit_vector *bv, bv_bitnum_t bit_number)
 *     Sets the specified bit number.  Returns the previous state of the
 *     bit (0 or 1), or -1 of out of memory.
 *
 *   bv_clear_bit(bit_vector *bv, bv_bitnum_t bit_number)
 *     Clears the specified bit number.  Returns the previous state of the
 *     bit (0 or 1), or -1 of out of memory.
 *
 *   bv_bit_is_set(bit_vector *bv, bv_bitnum_t bit_number)
 *     Returns TRUE if the bit is set, or FALSE if the bit is clear.
 *
 *   bv_bitnum_t bv_first_set_bit(bit_vector *bv)
 *     Returns the bit number of the first set bit in the vector, or
 *     BV_BAD_BITNUM if no bits are set.
 *
 *   bv_bitnum_t bv_first_clear_bit(bit_vector *bv)
 *     Returns the bit number of the first clear bit in the vector, or
 *     BV_BAD_BITNUM if the bit vector has all bits set and cannot grow
 *     further.  This generally will always succeed.
 *
 *   bv_bitnum_t bv_find_clear_bit(bit_vector *bv)
 *     Same as bv_first_clear_bit, except that the bit number returned is
 *     not necessarily the first clear bit.  This is potentially faster
 *     than bv_first_clear_bit but may be less memory efficient, since
 *     the resulting vector may be more sparse.
 *
 *   boolean bv_empty(bit_vector *bv)
 *     Returns TRUE if the bit vector is empty (no set bits), or FALSE if
 *     not.  This is cheaper than using bv_first_set_bit(), for example.
 *
 *
 * Bit vector vperations
 *
 * The following calls allow the manipulation of bit vectors as sets.  All
 * of them have the optional callback as described above.  A NULL vector
 * pointer as a source parameter is treated as vector with all bits clear.
 * A NULL result pointer means that the caller doesn't actually care about
 * the results as a vector (a callback is required for this to be anything
 * other than a way to waste CPU.)  Each routine returns 0 if all went
 * OK, or -1 if we ran out of memory.
 *
 *  The callback must be defined as follows:
 *
 *   boolean cb_routine(void *context, bv_bitnum_t bit_number, boolean
 *                      new_bit_value, boolean old_bit_value)
 *
 *     Context is the opaque context passed from the original call to
 *     the vector operation.  bit_number is the bit number in the
 *     vector being signaled (either because it is set or because it
 *     changed, depending on the original call.)  new_bit_value and
 *     old_bit_value are the old and new values of the bit, which may
 *     be changing, depending on the operation.  The callback routine
 *     must return TRUE if the operation is to be aborted (which can
 *     save CPU in some cases) or FALSE if it should continue.
 *
 *
 * The operations are:
 *
 *   int bv_and_vectors(bit_vector *first, bit_vector *second,
 *                      bit_vector *result, bv_callback callback,
 *                      void *context, bv_callback_option cb_opt)
 *   int bv_or_vectors(bit_vector *first, bit_vector *second,
 *                     bit_vector *result, bv_callback callback,
 *                     void *context, bv_callback_option cb_opt)
 *   int bv_xor_vectors(bit_vector *first, bit_vector *second,
 *                      bit_vector *result, bv_callback callback,
 *                      void *context, bv_callback_option cb_opt)
 *   int bv_clear_vectors(bit_vector *first, bit_vector *second,
 *                        bit_vector *result, bv_callback callback,
 *                        void *context, bv_callback_option cb_opt)
 *     Performs a boolean AND, OR, XOR, or CLEAR operation between
 *     vectors "first" and "second", storing the results in "result",
 *     with optional callback.  (A "clear" operation clears any bit
 *     that is set in the second vector.)
 *
 *   int bv_copy_vector(bit_vector *src, bit_vector *dest,
 *                      bv_callback callback, void *context,
 *                      bv_callback_option cb_opt)
 *     Copies the contents of vector "src" to vector "dest", with optional
 *     callback.
 *
 *   int bv_walk_vector(bit_vector *vect, bv_callback callback, void *context,
 *                      bv_callback_option cb_opt)
 *     Walks the bit vector, calling back for every bit set in the vector.
 *
 *   void bv_clear_all_bits(bit_vector *vect, bv_callback callback,
 *                          void *context, bv_callback_option cb_opt)
 *     Clears all bits in the vector (like bv_clean()) but does so with a
 *     callback for each bit that's set in the vector.
 */


/*
 * Environment
 *
 * The bitvector code is a portable source library.  It requires certain
 * services from the environment in which it runs.  These services are
 * called through procedures named bvx_<foo> and are listed below.
 *
 * The environment is defined by an application-specific file that is
 * included by the library called "bvx_environment.h".  That file typically
 * contains #defines and typedefs and externs that bind the toolkit's
 * calls to the local environment.
 *
 * The required definitions are as follows.  Note that if the exact type
 * of parameter or return value is not specified, the toolkit treats it in
 * a generic way and does not explicitly type it (for instance, zero/nonzero
 * procedure returns.)
 *
 * Patricia library:
 *
 *  bvx_patroot
 *    An opaque type for a patricia root structure
 *
 *  bvx_patnode
 *    An opaque type for a patricia node
 *
 *  bvx_patroot *bvx_patroot_init(keylen, offset)
 *    Initializes a patricia root.  Keylen is the length of the key in bytes.
 *    Offset is the byte offset from the start of the key to the start of
 *    the patricia node.
 *
 *  bvx_patnode *bvx_patricia_lookup(bvx_patroot *root, void *key)
 *    Looks up a patricia entry, returns a pointer to the patricia node,
 *    or NULL if not found.
 *
 *  bvx_patnode *bvx_patricia_lookup_least(bvx_patroot *root)
 *    Looks up the first entry in a patricia tree, returns a pointer to the
 *    patricia node, or NULL if the tree is empty.
 *
 *  bvx_patnode *bvx_patricia_lookup_greatest(bvx_patroot *root)
 *    Looks up the last entry in a patricia tree, returns a pointer to the
 *    patricia node, or NULL if the tree is empty.
 *
 *  bvx_patricia_add(bvx_patroot *root, bvx_patnode *node)
 *    Adds the node to the patricia tree.  Returns nonzero if successful,
 *    zero if failed (e.g., key already in the tree).
 *
 *  bvx_patricia_delete(bvx_patroot *root, bvx_patnode *node)
 *    Deletes the node from the patricia tree.  Returns nonzero if successful,
 *    zero if failed (e.g., node is not in the tree).
 *
 *  bvx_patnode *bvx_patricia_get_next(bvx_patroot *root, bvx_patnode *node)
 *    Returns the patricia node following the specified one.  If the specified
 *    node is NULL, returns the first node in the tree.  Returns NULL if there
 *    are no more nodes in the tree.
 *
 *  bvx_patroot_destroy(bvx_patroot *root)
 *    Destroys the patricia tree root.  The toolkit ensures that all nodes are
 *    first deleted from the tree.
 *
 *  BVX_PATRICIA_OFFSET(structname, nodefield, keyfield)
 *    Returns the offset in a structure from the patricia node to the
 *    patricia key.  This should resolve to a bunch of pointer math.  This
 *    is used in calls to bvx_patroot_init().
 *
 *  BVX_PATNODE_TO_STRUCT(procname, structname, nodefieldname)
 *    Creates an inline procedure that, given a pointer to an embedded
 *    bvx_patnode in a structure, returns a pointer to the enclosing
 *    structure, or returns NULL if the node pointer is NULL.
 *
 *
 * Thread Library:
 *
 *  The library for doubly-linked-list data structure.
 *
 *
 * Memory management:
 *
 *  The bitvector toolkit expects a block memory management system, in which
 *  a block size is first defined, and then requests for specific block sizes
 *  are made thereafter (modeled on rpd's memory manager.)
 *
 *  bvx_block_tag
 *    An opaque type that defines a tag representing a memory block size.
 *    Semantically, this is the size itself (and in fact a sleazy way to
 *    implement this in a naked malloc() environment is to define this as
 *    an int and simply store the block size here.)  The tag must be a
 *    scalar (so that it can be compared to zero) and it must be nonzero
 *    when it is valid (since a zero value implies an uninitialized tag.)
 *
 *  bvx_block_tag bvx_malloc_block_create(size, const char *name)
 *    Create a block tag for memory blocks of a particular size.  The name
 *    can be used for diagnostic purposes.  Returns the tag.
 *
 *  void *bvx_malloc_block(bvx_block_tag tag)
 *    Allocate a block of memory of the size denoted by the tag.  Returns
 *    a pointer to the block, or NULL if out of memory.  The toolkit
 *    does not specify the pointer type of the returned value, but expects
 *    to be able to assign it to any pointer type without coercion (thus
 *    the void *).
 *
 *  bvx_free_block(bvx_block_tag tag, void *block)
 *    Free a block of memory to the size pool specified by the tag.
 *
 *
 * Miscellaneous stuff:
 *
 *   bvx_assert(condition)
 *     Crashes if the condition is not true.
 *
 *   BVX_UNUSED
 *     Used in procedure declarations after unused parameters to avoid
 *     generating compilation warnings, e.g. "int flort(int foo BVX_UNUSED)".
 *
 *   uint32_t
 *     Must be typedefed to an unsigned, 32 bit scalar.
 *
 *   boolean, TRUE, FALSE
 *     The boolean type and the TRUE and FALSE constants must be defined.
 *     TRUE must be nonzero, and FALSE must be zero.  There is no assumption
 *     about the size of the boolean type.
 *
 *
 * Size definitions:
 *
 *  The bitvector toolkit requires two items to be specified that impact
 *  how memory is used by the toolkit.  They are:
 *
 *  BV_BITSIZE_LOG2
 *    The base-2 log of the size of a bit vector entry.
 *
 *  bv_word_t
 *    A type defining the size of a scalar word.
 *
 *  BV_BITSIZE_LOG2 must be greater than or equal to the base 2 log of the
 *  bit size of bv_word_t.  So if bv_word_t is typedefed as a uint32_t,
 *  BV_BITSIZE_LOG2 must be greater than or equal to 5.
 *
 *  BV_BITSIZE_LOG2 represents the granularity of memory allocated to maintain
 *  the bit vector.  Larger values provide lower overhead when using sparse
 *  arrays (since the number of individual memory blocks and the size of the
 *  patricia tree will be smaller), but waste memory if the vector is very
 *  sparse or has very few bits set.
 *
 *  bv_word_t represents the size of a scalar in which bits are stored.  This
 *  type needs to be able to be compared to zero, for example.  This ought
 *  to match the largest native scalar type on the processor in use that
 *  is small enough to fit within BV_BITSIZE_LOG2.
 */

#ifndef __BITVECTOR_H__
#define __BITVECTOR_H__

/*
 * Bit vector
 *
 * A bit vector is a patricia tree of bit vector entries.
 *
 * An empty tree may have a null root pointer.
 *
 * bv_fastvects is set if the application wants to do fast vector operations
 * (but see the comments in the front of this file regarding the tradeoffs.)
 * The side effect of setting bv_fastvects is that the bit count in a vector
 * entry may become unknown, which in turn means that completely empty
 * entries may be left in the tree, and that non-full entries may not end
 * up in the non-full list.  This impacts the cost of a search for free
 * bits.
 *
 * bv_nonfull_head is an optimization to help searches for clear bits.
 * Normally, all entries that aren't full are threaded there in order
 * to make searching for a free bit cheap.  However, if vector
 * operations are in effect, this list may be incomplete.
 *
 * bv_freed_ord is also an optimization to help searches for clear
 * bits.  This is set to be either the starting bit ordinal of a free
 * (nonexistent) block of bits, or BV_BAD_BITNUM if it is unset.  The
 * code endeavors to keep this set cheaply; it is used as a free bit
 * number if no allocated blocks with free bits are available.
 */

typedef uint32_t bv_bitnum_t;        /* Bit number */
#define BV_BAD_BITNUM 0xffffffff    /* Illegal bit number */

typedef struct bit_vector_ {
    bvx_patroot *bv_root;        /* Patricia root */
    uint32_t bv_entry_count;        /* Number of attached entries */
    thread bv_nonfull_head;        /* Head of non-full entries */
    bv_bitnum_t bv_callback_ord;    /* Ordinal of callback bit */
    bv_bitnum_t bv_freed_ord;        /* Ordinal of a freed entry */
    boolean bv_fastvects;        /* Perform fast vector operations */
    boolean bv_cb_result;        /* TRUE if vector is callback result */
    boolean bv_cb_source;        /* TRUE if vector is callback source */
} bit_vector;

/*
 * Bit vector callback definition.  The bit vector routines can call
 * back to an application as the results of bit vector manipulations
 * are generated.
 *
 * If the callback returns TRUE, the set operation is aborted.  This can
 * save time in some situations.
 */
typedef boolean (*bv_callback)(void *context, bv_bitnum_t bit_number,
                   boolean new_bit_value, boolean old_bit_value);



/*
 * Definitions of bit result callback options.
 */
typedef enum {
    BV_CALL_CHANGE,            /* Call back if result bit changed */
    BV_CALL_SET                 /* Call back if result bit is set */
} bv_callback_option;

/* Externals */

extern void bv_clean(bit_vector *bv);
extern int bv_set_bit(bit_vector *bv, bv_bitnum_t bit_number);
extern boolean bv_clear_bit(bit_vector *bv, bv_bitnum_t bit_number);
extern boolean bv_bit_is_set(bit_vector *bv, bv_bitnum_t bit_number);
extern void bv_init_vector(bit_vector *bv, boolean fast_vects);
extern int bv_and_vectors(bit_vector *first, bit_vector *second,
              bit_vector *result, bv_callback callback,
              void *context, bv_callback_option cb_opt);
extern int bv_or_vectors(bit_vector *first, bit_vector *second,
             bit_vector *result, bv_callback callback,
             void *context, bv_callback_option cb_opt);
extern int bv_xor_vectors(bit_vector *first, bit_vector *second,
              bit_vector *result, bv_callback callback,
              void *context, bv_callback_option cb_opt);
extern int bv_clear_vectors(bit_vector *first, bit_vector *second,
                bit_vector *result, bv_callback callback,
                void *context, bv_callback_option cb_opt);
extern int bv_copy_vector(bit_vector *src, bit_vector *dest,
              bv_callback callback, void *context,
              bv_callback_option cb_opt);
extern int bv_walk_vector(bit_vector *vect, bv_callback callback,
              void *context);
extern bv_bitnum_t bv_first_set_bit(bit_vector *bv);
extern boolean bv_empty(bit_vector *bv);
extern bv_bitnum_t bv_first_clear_bit(bit_vector *bv);
extern bv_bitnum_t bv_find_clear_bit(bit_vector *bv);
extern void bv_clear_all_bits(bit_vector *bv, bv_callback callback,
                  void *context, bv_callback_option cb_opt);

#endif /* __BITVECTOR_H__ */
