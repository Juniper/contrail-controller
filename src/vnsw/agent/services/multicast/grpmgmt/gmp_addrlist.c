/* $Id: gmp_addrlist.c 346474 2009-11-14 10:18:58Z ssiano $
 *
 * gmp_addrlist.c - IGMP/MLD address list management
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 * This file defines manipulations of Address Lists, which are the key
 * data structure in the GMP toolkit.  An address list is, conceptually,
 * a list of addresses (!).  The reality is much more complex, however,
 * as the address lists are subject to set operations (union, intersection,
 * etc.)  See gmp_private.h for details.
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_externs.h"
#include "gmp_private.h"

static gmpx_block_tag gmp_addr_list_tag;
static gmpx_block_tag gmp_addr_list_entry_tag;
static gmpx_block_tag gmp_adcat_entry_tag;
static gmpx_block_tag gmp_addr_thread_tag;
static gmpx_block_tag gmp_addr_thread_entry_tag;
static boolean ga_initialized;

static gmp_addr_string zero_addr;

/*
 * gmp_addr_is_zero
 *
 * Returns TRUE if the address is all zero, or FALSE if not.
 */
boolean
gmp_addr_is_zero (gmp_addr_string *addr, uint32_t addr_len)
{
    return !memcmp(addr->gmp_addr, zero_addr.gmp_addr, addr_len);
}


/*
 * Address thread manipulation.
 *
 * Address threads are used to carry lists of addresses in explicit
 * form (the actual address bytes.)  Much of this code uses ordinals
 * to represent addresses, but ordinals are not visible outside of
 * this module, so we need to have a generic representation of a list
 * of addresses to pass in and out.
 */

/*
 * gmp_init_addr_thread
 *
 * Initialize an address thread.
 */
static void
gmp_init_addr_thread (gmp_addr_thread *addr_thread)
{
    thread_new_circular_thread(&addr_thread->gmp_addr_thread_head);
    addr_thread->gmp_addr_thread_count = 0;
}


/*
 * gmp_addr_thread_count
 *
 * Returns the count of entries in an address thread.
 *
 * Tolerates null pointers.
 */
uint32_t
gmp_addr_thread_count (gmp_addr_thread *addr_thread)
{
    if (!addr_thread)
	return 0;

    return addr_thread->gmp_addr_thread_count;
}


/*
 * gmp_alloc_addr_thread
 *
 * Allocate and initialize an address thread.
 *
 * Returns a pointer to the address thread, or NULL if out of memory.
 */
gmp_addr_thread *
gmp_alloc_addr_thread (void)
{
    gmp_addr_thread *addr_thread;

    addr_thread = gmpx_malloc_block(gmp_addr_thread_tag);
    if (addr_thread)
	gmp_init_addr_thread(addr_thread);

    return addr_thread;
}


/*
 * gmp_enqueue_addr_thread_addr
 *
 * Enqueue an address on an address thread.
 *
 * Returns 0 if all OK, or -1 if out of memory.
 */
int
gmp_enqueue_addr_thread_addr(gmp_addr_thread *addr_thread,
			     uint8_t *addr, uint32_t addr_len)
{
    gmp_addr_thread_entry *thread_entry;

    /* Allocate an address thread entry. */

    thread_entry = gmpx_malloc_block(gmp_addr_thread_entry_tag);
    if (!thread_entry)
	return -1;			/* Out of memory */

    /* Copy in the address. */

    memmove(thread_entry->gmp_adth_addr.gmp_addr, addr, addr_len);

    /* Enqueue the entry. */

    thread_circular_add_bottom(&addr_thread->gmp_addr_thread_head,
			       &thread_entry->gmp_adth_thread);

    /* Bump the count. */

    addr_thread->gmp_addr_thread_count++;

    return 0;
}


/*
 * gmp_next_addr_thread_addr
 *
 * Get the next address thread address, given a pointer to the last one.
 * If the last one was NULL, get the first one.
 *
 * Returns a pointer to the address string in the entry, or NULL if there
 * are no more entries.
 *
 * Updates the pointer.
 *
 * Tolerates null pointers.
 */
gmp_addr_string *
gmp_next_addr_thread_addr (gmp_addr_thread *addr_thread,
			   gmp_addr_thread_entry **entry_ptr)
{
    thread *thread_ptr;
    gmp_addr_thread_entry *thread_entry;

    /* Bail if there's no thread. */

    if (!addr_thread)
	return NULL;

    /* Pick up the current position. */

    thread_ptr = NULL;
    if (*entry_ptr)
	thread_ptr = &((*entry_ptr)->gmp_adth_thread);

    /* Fetch the next entry. */

    thread_ptr =
	thread_circular_thread_next(&addr_thread->gmp_addr_thread_head,
				    thread_ptr);
    thread_entry = gmp_adth_thread_to_thread_entry(thread_ptr);
    *entry_ptr = thread_entry;

    /* Return a pointer to the address string, if it's there. */

    if (thread_entry) {
	return &thread_entry->gmp_adth_addr;
    } else {
	return NULL;
    }
}


/*
 * gmp_destroy_addr_thread
 *
 * Destroy an address thread
 *
 * Flushes the thread and frees the entries and the thread head.
 */
void
gmp_destroy_addr_thread (gmp_addr_thread *addr_thread)
{
    gmp_addr_thread_entry *thread_entry;
    thread *thread_ptr;

    /* Tolerate NULL pointers. */

    if (!addr_thread)
	return;

    /* Pull entries off the thread until it is empty. */

    while (TRUE) {
	thread_ptr =
	    thread_circular_dequeue_top(&addr_thread->gmp_addr_thread_head);
	thread_entry = gmp_adth_thread_to_thread_entry(thread_ptr);
	if (!thread_entry)
	    break;
	gmpx_free_block(gmp_addr_thread_entry_tag, thread_entry);
    }

    /* Free the thread head. */

    gmpx_free_block(gmp_addr_thread_tag, addr_thread);
}


/*
 * gmp_alloc_generic_addr_list_entry
 *
 * Allocate memory for a generic (non-embedded) address list entry.
 *
 * Returns a pointer to the entry, or NULL if out of memory.
 */
gmp_addr_list_entry *
gmp_alloc_generic_addr_list_entry (void *context GMPX_UNUSED)
{
    gmp_addr_list_entry *addr_entry;

    addr_entry = gmpx_malloc_block(gmp_addr_list_entry_tag);
    return addr_entry;
}



/*
 * Address catalog manipulation.
 *
 * An address catalog maps addresses to ordinals.  The ordinals are
 * then used both as shorthand for addresses (for IPv6) and as bit
 * positions in address vectors (for vector set operations.)
 *
 * The catalog maps in each direction between ordinal and address.  A
 * refcount is kept, and the catalog entry is freed when the last
 * reference is deleted.
 *
 * One must be *very* careful with the lock/unlock pairings; in
 * particular, if an entry is unlocked when it shouldn't be, this
 * could free the ordinal, which in turn will either cause a crash (an
 * ordinal without a matching catalog entry) or worse, the ordinal
 * could be reassigned, making much mischief with addresses.
 */

/*
 * gmp_destroy_addr_catalog
 *
 * Destroy an address catalog.  Cleans out its contents.
 */
void
gmp_destroy_addr_catalog (gmp_addr_catalog *catalog)
{
    /* The catalog better be empty! */

    gmpx_assert(!(gmpx_patricia_lookup_least(catalog->adcat_addr_root)));
    gmpx_assert(!(gmpx_patricia_lookup_least(catalog->adcat_ord_root)));

    /* Destroy the patricia trees. */

    gmpx_patroot_destroy(catalog->adcat_addr_root);
    catalog->adcat_addr_root = NULL;
    gmpx_patroot_destroy(catalog->adcat_ord_root);
    catalog->adcat_ord_root = NULL;

    /* Free the ordinal space. */

    ord_destroy_context(catalog->adcat_ord_handle);
    catalog->adcat_ord_handle = NULL;
}


/*
 * gmp_init_addr_catalog
 *
 * Initialize an address catalog header.
 *
 * Returns 0 if all OK, or -1 if out of memory.
 */
int
gmp_init_addr_catalog (gmp_addr_catalog *catalog, uint32_t addr_len)
{
    /* Create the tree roots. */

    catalog->adcat_addr_root =
	gmpx_patroot_init(addr_len, GMPX_PATRICIA_OFFSET(gmp_addr_cat_entry,
							 adcat_ent_addr_node,
							 adcat_ent_addr));
    if (!catalog->adcat_addr_root)
	return -1;			/* Out of memory */

    catalog->adcat_ord_root =
	gmpx_patroot_init(sizeof(ordinal_t),
			  GMPX_PATRICIA_OFFSET(gmp_addr_cat_entry,
					       adcat_ent_ord_node,
					       adcat_ent_ord));
    if (!catalog->adcat_ord_root) {
	gmpx_patroot_destroy(catalog->adcat_addr_root);
	catalog->adcat_addr_root = NULL;
	return -1;			/* Out of memory */
    }

    /* Allocate ordinal space. */

    catalog->adcat_ord_handle = ord_create_context(ORD_PERFORMANCE);
    if (!catalog->adcat_ord_handle) {
	gmpx_patroot_destroy(catalog->adcat_addr_root);
	catalog->adcat_addr_root = NULL;
	gmpx_patroot_destroy(catalog->adcat_ord_root);
	catalog->adcat_ord_root = NULL;
	return -1;			/* Out of memory */
    }

    /* Set the address length. */

    catalog->adcat_addrlen = addr_len;

    return 0;
}


/*
 * gmp_addr_list_copy_cb
 *
 * Bit vector callback to copy an address list entry.
 *
 * If an entry is already present in the destination list, it is left there.
 */
boolean
gmp_addr_list_copy_cb (void *context, bv_bitnum_t bitnum,
		       boolean new_val GMPX_UNUSED,
		       boolean old_val GMPX_UNUSED)
{
    gmp_addr_list *dest;

    dest = context;

    /* See if the entry is already present in the destination list. */

    if (!gmp_addr_in_list(dest, bitnum)) {

	/* Not present.  Create a new entry. */

	gmp_create_addr_list_entry(dest, bitnum);
    }

    return FALSE;
}


/*
 * gmp_get_addr_cat_by_ordinal
 *
 * Look up an address catalog entry by its ordinal.
 *
 * Returns a pointer to the catalog entry, or NULL if not found.
 */
gmp_addr_cat_entry *
gmp_get_addr_cat_by_ordinal (gmp_addr_catalog *catalog, ordinal_t ordinal)
{
    gmp_addr_cat_entry *cat_entry;
    gmpx_patnode *node;

    /* Look it up. */

    node = gmpx_patricia_lookup(catalog->adcat_ord_root, &ordinal);
    cat_entry = gmp_ord_patnode_to_addr_cat_entry(node);

    return cat_entry;
}


/*
 * gmp_unlock_adcat_entry
 *
 * Unlock an address catalog entry.
 *
 * If the refcount has gone to zero, release the entry.
 */
void
gmp_unlock_adcat_entry (gmp_addr_catalog *catalog, ordinal_t ordinal)
{
    gmp_addr_cat_entry *cat_entry;

    /* Look up the catalog entry by ordinal. */

    cat_entry = gmp_get_addr_cat_by_ordinal(catalog, ordinal);
    gmpx_assert(cat_entry);

    cat_entry->adcat_ent_refcount--;
    gmpx_assert(cat_entry->adcat_ent_refcount >= 0);

    /* Free the entry and its ordinal if the refcount has gone to zero. */

    if (cat_entry->adcat_ent_refcount == 0) {
	ord_free_ordinal(catalog->adcat_ord_handle, ordinal);
	gmpx_assert(gmpx_patricia_delete(catalog->adcat_addr_root,
					 &cat_entry->adcat_ent_addr_node));
	gmpx_assert(gmpx_patricia_delete(catalog->adcat_ord_root,
					 &cat_entry->adcat_ent_ord_node));
	gmpx_free_block(gmp_adcat_entry_tag, cat_entry);
    }
}


/*
 * gmp_lock_adcat_entry
 *
 * Lock an address catalog entry.  Just bump the refcount.
 */
void
gmp_lock_adcat_entry (gmp_addr_catalog *catalog, ordinal_t ordinal)
{
    gmp_addr_cat_entry *cat_entry;

    /* Look up the catalog entry by ordinal. */

    cat_entry = gmp_get_addr_cat_by_ordinal(catalog, ordinal);
    gmpx_assert(cat_entry);

    cat_entry->adcat_ent_refcount++;
}


/*
 * gmp_lookup_addr_cat_entry
 *
 * Look up an address catalog entry by address.
 * 
 * Returns a pointer to the entry, or NULL if not found.
 */
gmp_addr_cat_entry *
gmp_lookup_addr_cat_entry (gmp_addr_catalog *catalog, const uint8_t *addr)
{
    gmpx_patnode *node;
    gmp_addr_cat_entry *cat_entry;

    /* Look up the entry. */

    node = gmpx_patricia_lookup(catalog->adcat_addr_root, addr);
    cat_entry = gmp_addr_patnode_to_addr_cat_entry(node);

    return cat_entry;
}


/*
 * gmp_lookup_create_addr_cat_entry
 *
 * Look up an address catalog entry by address, and create an entry if one
 * isn't there.
 *
 * Returns the ordinal of the entry, or ORD_BAD_ORDINAL if out of memory.
 *
 * Note that the caller is responsible for locking the entry!
 */
ordinal_t
gmp_lookup_create_addr_cat_entry (gmp_addr_catalog *catalog,
				  uint8_t *addr)
{
    gmp_addr_cat_entry *cat_entry;

    /* Look up the entry. */

    cat_entry = gmp_lookup_addr_cat_entry(catalog, addr);

    if (!cat_entry) {

        /* No entry found.  Create a new one. */

        cat_entry = gmpx_malloc_block(gmp_adcat_entry_tag);
        if (!cat_entry)
            return ORD_BAD_ORDINAL;	/* Out of memory */

        /* Got one.  Initialize it. */

        memmove(cat_entry->adcat_ent_addr.gmp_addr, addr,
            catalog->adcat_addrlen);
        cat_entry->adcat_ent_ord = ord_get_ordinal(catalog->adcat_ord_handle);
        if (cat_entry->adcat_ent_ord == ORD_BAD_ORDINAL) {
            free(cat_entry);
            return ORD_BAD_ORDINAL;	/* Out of memory */
        }

        /* Add it to the patricia trees. */

        gmpx_assert(gmpx_patricia_add(catalog->adcat_addr_root,
                          &cat_entry->adcat_ent_addr_node));
        gmpx_assert(gmpx_patricia_add(catalog->adcat_ord_root,
                          &cat_entry->adcat_ent_ord_node));
    }

    return cat_entry->adcat_ent_ord;
}


/*
 * Generic address list entry routines.  Generic address list entries
 * are used when no additional information needs to be associated with
 * the entry--the semantics are just an address.  These routines
 * allocate and deallocate naked address list entries for those who
 * need them.  When other information needs to be associated with an
 * address, a more specific structure is created with an address list
 * entry embedded therein; those users provide their own alloc and
 * free routines.
 */

/*
 * gmp_free_generic_addr_list_entry
 *
 * Callback to free a generic address list entry.
 */
void
gmp_free_generic_addr_list_entry (gmp_addr_list_entry *addr_entry)
{
    /* Simply free the block. */

    gmpx_free_block(gmp_addr_list_entry_tag, addr_entry);
}


/*
 * gmp_addr_list_empty
 * 
 * Returns TRUE if the address list is empty, or FALSE if not.
 */
boolean
gmp_addr_list_empty (gmp_addr_list *list)
{
    return (!list->addr_count);
}


/*
 * gmp_addr_list_next_entry
 *
 * Returns the next entry in an address list, given a pointer to the
 * previous one.  If the provided pointer is NULL, returns the first
 * entry on the list.
 *
 * Returns NULL when there are no more entries on the list.
 *
 * The entries are *not* returned in any particular lexicographic order.
 */
gmp_addr_list_entry *
gmp_addr_list_next_entry (gmp_addr_list *list, gmp_addr_list_entry *prev)
{
    thread *new_thread;
    thread *cur_thread;

    /* Get the current position. */

    if (prev) {
	cur_thread = &prev->addr_ent_thread;
    } else {
	cur_thread = NULL;
    }

    /* Get the next entry. */

    new_thread = thread_circular_thread_next(&list->addr_list_head,
					     cur_thread);
    return gmp_thread_to_addr_list_entry(new_thread);
}


/*
 * gmp_addr_list_init
 *
 * Initialize an address list header.  We leave the patricia tree root
 * empty unless we actually get sources to work with.
 */
void
gmp_addr_list_init (gmp_addr_list *list, gmp_addr_catalog *catalog,
		    gmp_addr_list_alloc_func alloc_func,
		    gmp_addr_list_free_func free_func,
		    void *context)
{
    /* Zero it out. */

    memset(list, 0, sizeof(gmp_addr_list));

    /* Initialize the threads. */

    thread_new_circular_thread(&list->addr_list_head);
    thread_new_circular_thread(&list->addr_list_xmit_head);

    /* Initialize the address vector. */

    gmp_init_addr_vector(&list->addr_vect, catalog);

    /* Initialize the allocation/free pointers. */

    list->addr_alloc = alloc_func;
    list->addr_free = free_func;
    list->addr_context = context;
}



/*
 * Transmit list manipulations.  These routines operate on the transmit
 * thread of an address list.
 */

/*
 * gmp_flush_xmit_list
 *
 * Flush the transmit list of an address list.
 */
void
gmp_flush_xmit_list (gmp_addr_list *addr_list)
{
    thread *thread_ptr;

    if (!addr_list)
	return;				/* Tolerate NULL pointers */

    /* Just walk the list, deleting everything. */

    while (TRUE) {
	thread_ptr = thread_circular_top(&addr_list->addr_list_xmit_head);
	if (!thread_ptr)
	    break;
	thread_remove(thread_ptr);
    }
    addr_list->xmit_addr_count = 0;
}


/*
 * gmp_enqueue_xmit_addr_entry
 *
 * Enqueue an address entry on its owning address list transmit thread
 * if it is not already enqueued.
 */
void
gmp_enqueue_xmit_addr_entry (gmp_addr_list_entry *addr_entry)
{ 
    gmp_addr_list *addr_list;

   /* Enqueue it if it's not already on a queue. */

    if(!thread_node_on_thread(&addr_entry->addr_ent_xmit_thread)) {
	addr_list = addr_entry->addr_ent_list;
	thread_circular_add_bottom(&addr_list->addr_list_xmit_head,
				   &addr_entry->addr_ent_xmit_thread);
	addr_list->xmit_addr_count++;
    }
}


/*
 * gmp_dequeue_xmit_addr_entry
 *
 * Dequeue an address entry from its owning address list transmit thread.
 */
void
gmp_dequeue_xmit_addr_entry (gmp_addr_list_entry *addr_entry)
{
    gmp_addr_list *addr_list;    
    if (thread_node_on_thread(&addr_entry->addr_ent_xmit_thread)) {
	addr_list = addr_entry->addr_ent_list;
	thread_remove(&addr_entry->addr_ent_xmit_thread);
	addr_list->xmit_addr_count--;
	gmpx_assert(addr_list->xmit_addr_count >= 0);
    }
}


/*
 * gmp_xmit_addr_list_empty
 * 
 * Returns TRUE if the transmit thread on an address list is empty, or
 * FALSE if not.
 */
boolean
gmp_xmit_addr_list_empty (gmp_addr_list *list)
{
    return (list->xmit_addr_count == 0);
}


/*
 * gmp_first_xmit_addr_entry
 *
 * Returns a pointer to the first address entry on an address list
 * transmit thread, or NULL if the thread is empty.
 */
gmp_addr_list_entry *
gmp_first_xmit_addr_entry (gmp_addr_list *addr_list)
{
    thread *entry_thread;

    entry_thread = thread_circular_top(&addr_list->addr_list_xmit_head);
    return gmp_xmit_thread_to_addr_list_entry(entry_thread);
}


/*
 * gmp_enqueue_xmit_addr_list
 *
 * Thread all entries in an address list onto the list transmit
 * thread.  We traverse the address list thread for speed.
 */
void
gmp_enqueue_xmit_addr_list (gmp_addr_list *addr_list)
{
    gmp_addr_list_entry *addr_entry;

    /* Walk everything on the address list. */

    addr_entry = NULL;
    while (TRUE) {
	addr_entry = gmp_addr_list_next_entry(addr_list, addr_entry);

	/* Bail if nothing left. */

	if (!addr_entry)
	    break;

	/* Enqueue the entry. */

	gmp_enqueue_xmit_addr_entry(addr_entry);
    }
}


/*
 * gmp_remove_addr_list_entry
 *
 * Remove an address list entry from an address list (but don't free it.)
 *
 * Note that we do NOT unlock the corresponding catalog entry.
 *
 */
static void
gmp_remove_addr_list_entry (gmp_addr_list_entry *addr_entry)
{
    gmp_addr_list *addr_list;

    /* Delete the entry from the tree. */

    addr_list = addr_entry->addr_ent_list;
    gmpx_assert(gmpx_patricia_delete(addr_list->addr_list_root,
				     &addr_entry->addr_ent_patnode));

    /* Clear the corresponding vector bit.  Better have been set. */

    gmpx_assert(bv_clear_bit(&addr_list->addr_vect.av_vector,
			     addr_entry->addr_ent_ord));

    /* Remove it from the threads if it is there. */

    thread_remove(&addr_entry->addr_ent_thread);
    gmp_dequeue_xmit_addr_entry(addr_entry);
    addr_entry->addr_ent_list = NULL;

    /* Drop the entry count. */

    addr_list->addr_count--;
    gmpx_assert(addr_list->addr_count >= 0);
}


/*
 * gmp_delete_addr_list_entry
 *
 * Remove an address entry from a list and free it.
 *
 * Clears the vector bit and unlocks the corresponding catalog entry.
 */
void
gmp_delete_addr_list_entry (gmp_addr_list_entry *addr_entry)
{
    gmp_addr_list *addr_list;

    addr_list = addr_entry->addr_ent_list;

    /* Remove the entry from its list. */

    gmp_remove_addr_list_entry(addr_entry);

    /* Unlock the address catalog entry. */

    gmp_unlock_adcat_entry(addr_list->addr_vect.av_catalog,
			   addr_entry->addr_ent_ord);

    /* Free the entry. */

    (*addr_list->addr_free)(addr_entry);
}


/*
 * gmp_lookup_addr_entry
 *
 * Look up an address in an address list.
 *
 * Returns a pointer to the address entry, or NULL if not there.
 */
gmp_addr_list_entry *
gmp_lookup_addr_entry (gmp_addr_list *addr_list, ordinal_t ordinal)
{
    gmpx_patnode *node;
    gmp_addr_list_entry *addr_entry;

    /* Bail if the list is empty. */

    if (gmp_addr_list_empty(addr_list))
	return NULL;

    /* Look up the address entry. */

    node = gmpx_patricia_lookup(addr_list->addr_list_root, &ordinal);
    addr_entry = gmp_patnode_to_addr_list_entry(node);

    return addr_entry;
}


/*
 * gmp_flush_addr_list
 *
 * Flush an address list.  The address list header (including the
 * patrica root) is not freed, but all of the list entries are.
 *
 * Calls back to the callback pointer to actually free each entry.
 *
 * Tolerates a null address list tree.
 */
void
gmp_flush_addr_list (gmp_addr_list *addr_list)
{
    gmp_addr_list_entry *addr_entry;

    /* Just pull entries out of the thread until it's empty. */

    while (TRUE) {

	/* Get the first entry. */

	addr_entry = gmp_addr_list_next_entry(addr_list, NULL);
	if (!addr_entry)		/* All done */
	    break;

	/* Remove it from the address list and free it. */

	gmp_delete_addr_list_entry(addr_entry);
    }
}


/*
 * gmp_add_addr_list_tree
 *
 * Add the patricia tree to an address list.  We try not to do this unless
 * we have to (to save memory.)
 *
 * Returns 0 if all OK, or -1 if no memory.
 */
static int
gmp_add_addr_list_tree (gmp_addr_list *addr_list)
{
    /* Be extra paranoid that there are no addresses there now. */

    gmpx_assert((addr_list->addr_list_root == NULL) &&
		gmp_addr_list_empty(addr_list));

    /* Create the patricia tree. */

    addr_list->addr_list_root =
	gmpx_patroot_init(sizeof(ordinal_t),
			  GMPX_PATRICIA_OFFSET(gmp_addr_list_entry,
					       addr_ent_patnode,
					       addr_ent_ord));
    if (!addr_list->addr_list_root)
	return -1;			/* Out of memory */

    return 0;
}


/*
 * gmp_add_addr_list_entry
 *
 * Add an address list entry to an address list.  Creates an address tree
 * if one does not exist.
 *
 * Assumes that the corresponding address catalog entry has already been
 * locked, and does *NOT* do so here.
 *
 * Returns 0 if all OK, or -1 if out of memory.
 */
int
gmp_add_addr_list_entry (gmp_addr_list *addr_list,
			 gmp_addr_list_entry *addr_entry)
{
    int retval;

    /* If there's no patricia tree there, create one. */

    if (addr_list->addr_list_root == NULL) {
	if (gmp_add_addr_list_tree(addr_list) < 0)
	    return -1;			/* Out of memory */
    }

    /* Add the entry to the patricia tree. */

    gmpx_assert(gmpx_patricia_add(addr_list->addr_list_root,
				  &addr_entry->addr_ent_patnode));

    /* Put the entry on the address list thread. */

    addr_list->addr_count++;
    gmpx_assert(!thread_node_on_thread(&addr_entry->addr_ent_thread));
    gmpx_assert(!thread_node_on_thread(&addr_entry->addr_ent_xmit_thread));
    thread_circular_add_bottom(&addr_list->addr_list_head,
			       &addr_entry->addr_ent_thread);
    addr_entry->addr_ent_list = addr_list;

    /* Set the vector bit. */

    retval = bv_set_bit(&addr_list->addr_vect.av_vector,
			addr_entry->addr_ent_ord);
    gmpx_assert(retval != 1);		/* Better not have been set */

    return retval;
}


/*
 * gmp_move_addr_list_entry
 *
 * Move an address list entry from its current list to another.
 */
void
gmp_move_addr_list_entry (gmp_addr_list *to_list,
			  gmp_addr_list_entry *addr_entry)
{
    gmp_addr_list *from_list;

    from_list = addr_entry->addr_ent_list;

    /*
     * Ensure that the lists have the same allocation and free pointers.
     * This makes sure that the lists are compatible.
     */
    gmpx_assert(from_list->addr_alloc == to_list->addr_alloc &&
		from_list->addr_free == to_list->addr_free);

    /* Remove the entry from the from list. */

    gmp_remove_addr_list_entry(addr_entry);

    /* Stick it onto the to list. */

    gmp_add_addr_list_entry(to_list, addr_entry);
}


/*
 * gmp_create_addr_list_entry
 *
 * Create and enqueue an address list entry, given the list and the address
 * ordinal.
 *
 * The entry is allocated via the allocation pointer in the address list.
 *
 * The catalog entry corresponding to the ordinal is locked.
 *
 * Returns a pointer to the new entry, or NULL if out of memory or the
 * allocation routine declined to create an entry.
 */
gmp_addr_list_entry *
gmp_create_addr_list_entry (gmp_addr_list *addr_list, ordinal_t ordinal)
{
    gmp_addr_list_entry *addr_entry;

    /* Allocate the entry. */

    addr_entry = (*addr_list->addr_alloc)(addr_list->addr_context);
    if (!addr_entry)
	return NULL;			/* Out of memory */

    addr_entry->addr_ent_ord = ordinal;

    /* Add it to the list. */

    gmp_add_addr_list_entry(addr_list, addr_entry);

    /* Lock the catalog entry. */

    gmp_lock_adcat_entry(addr_list->addr_vect.av_catalog, ordinal);

    return addr_entry;
}


/*
 * gmp_init_addr_vector
 *
 * Initialize an address vector.
 *
 * If no catalog pointer is supplied, it's a scratch vector and catalog
 * entries will not be locked and unlocked as the vector is manipulated.
 */
void
gmp_init_addr_vector (gmp_addr_vect *vector, gmp_addr_catalog *catalog)
{
    /* Initialize the bit vector.  We always use fast vector operations. */

    bv_init_vector(&vector->av_vector, TRUE);

    /* Stash the catalog pointer. */

    vector->av_catalog = catalog;
}


/*
 * gmp_addr_vect_clean_cb
 *
 * Callback from the bit vector code when cleaning out a vector entry.
 *
 * We decrement the catalog refcount and release the entry if the
 * refcount has gone to zero.
 */
static boolean
gmp_addr_vect_clean_cb (void *context GMPX_UNUSED, uint32_t bitnum,
			boolean new_bitval, boolean old_bitval GMPX_UNUSED)
{
    gmp_addr_catalog *catalog;

    catalog = context;

    gmpx_assert(new_bitval == 0);	/* Better be turning off the bit! */

    gmp_unlock_adcat_entry(catalog, bitnum);

    return FALSE;
}


/*
 * gmp_addr_vect_set
 *
 * Add an address to an address vector, by creating an address catalog
 * entry for it and setting the bit in the vector.
 *
 * This routine tolerates duplicate addresses.
 *
 * Returns 0 if all OK, or -1 if no memory.
 *
 * The address catalog entry is locked, but only once when duplicate
 * addresses are present.
 */
int
gmp_addr_vect_set(gmp_addr_vect *addr_vect, gmp_addr_string *addr)
{
    int set_result;
    ordinal_t ordinal;

    /* Look up or create an address catalog entry. */

    ordinal = gmp_lookup_create_addr_cat_entry(addr_vect->av_catalog,
					       addr->gmp_addr);
    if (ordinal == ORD_BAD_ORDINAL)	/* No memory */
	return -1;

    /* Got the entry.  Set the bit in the vector. */

    set_result = bv_set_bit(&addr_vect->av_vector, ordinal);
    if (set_result < 0)
	return -1;			/* Out of memory */

    /*
     * If the bit wasn't already set (not a duplicate), lock the
     * catalog entry.
     */
    if (set_result == 0)
	gmp_lock_adcat_entry(addr_vect->av_catalog, ordinal);

    return 0;
}


/*
 * gmp_addr_vect_clean
 *
 * Clean out an address vector.  Clears all of the vector bits and drops
 * the refcounts as necessary.  Address catalog entries may be freed as a
 * side effect.
 *
 * The catalog pointer can be NULL if the vector is not persistent.
 */
void
gmp_addr_vect_clean (gmp_addr_vect *vector)
{
    /*
     * Call the bit vector routine to do the work.  If a catalog
     * pointer is provided, pass a pointer to the callback routine
     * above to unlock each address.
     */
    bv_clear_all_bits(&vector->av_vector, vector->av_catalog ?
		      gmp_addr_vect_clean_cb : NULL, vector->av_catalog,
		      BV_CALL_CHANGE);
}


/*
 * gmp_addr_vect_empty
 *
 * Returns TRUE if an address vector is empty (has no set bits), or FALSE
 * if not.
 */
boolean
gmp_addr_vect_empty (gmp_addr_vect *vector)
{
    return bv_empty(&vector->av_vector);
}


/*
 * gmp_build_addr_cb
 *
 * Callback from bit vector routines for gmp_build_addr_list().
 *
 * This routine is called for each bit that is different between the
 * two vectors.
 *
 * New entries are allocated, and departing entries are freed, as
 * appropriate.
 */
static boolean
gmp_build_addr_cb (void *context, bv_bitnum_t bitnum,
		   boolean new_bit GMPX_UNUSED, boolean old_bit GMPX_UNUSED)
{
    gmp_addr_list *addr_list;
    gmp_addr_list_entry *addr_entry;

    addr_list = context;

    /* If we're creating a new entry, add it to the list. */

    if (!gmp_addr_in_list(addr_list, bitnum)) {
	addr_entry = gmp_create_addr_list_entry(addr_list, bitnum);

    } else {

	/* Getting rid of an old entry.  Trash it. */

	addr_entry = gmp_lookup_addr_entry(addr_list, bitnum);
	gmpx_assert(addr_entry);	/* Better be there! */
	gmp_delete_addr_list_entry(addr_entry);
    }

    return FALSE;
}


/*
 * gmp_build_addr_list
 *
 * Builds an address list from an address vector.
 *
 * Returns 0 if all OK, or -1 if out of memory.
 *
 * The contents of the address list are changed to match the new vector.
 * Any existing entries not on the new vector are deleted, and any
 * existing entries that are on the new vector are left untouched.
 */
int
gmp_build_addr_list (gmp_addr_list *addr_list, gmp_addr_vect *vector)
{
    /*
     * Compare the two vectors;  we'll be called back for any bits that
     * are different, and the callback routine will make the appropriate
     * adjustments.
     */
    return gmp_addr_vect_compare(vector, &addr_list->addr_vect,
				 gmp_build_addr_cb, addr_list);
}


/*
 * gmp_addr_vect_fill
 *
 * Fill an address list vector from an address thread.
 *
 * Returns 0 if all OK, or -1 if no memory.
 *
 * Address catalog entries for each address are locked.
 */
int
gmp_addr_vect_fill (gmp_addr_vect *addr_vect, gmp_addr_thread *addr_thread)
{
    int set_result;
    ordinal_t ordinal;
    gmp_addr_thread_entry *thread_entry;
    gmp_addr_string *addr;

    /* Walk each of the source addresses, if any. */

    thread_entry = NULL;
    while (TRUE) {
	addr = gmp_next_addr_thread_addr(addr_thread, &thread_entry);
	if (!addr)
	    break;
	
	/* Look up or create an address catalog entry. */

	ordinal = gmp_lookup_create_addr_cat_entry(addr_vect->av_catalog,
						   addr->gmp_addr);
	if (ordinal == ORD_BAD_ORDINAL) { /* No memory */
	    gmp_addr_vect_clean(addr_vect);
	    return -1;
	}

	/* Got the entry.  Lock it. */

	gmp_lock_adcat_entry(addr_vect->av_catalog, ordinal);

	/* Set the bit in the vector. */

	set_result = bv_set_bit(&addr_vect->av_vector, ordinal);
	gmpx_assert(set_result != 1);	/* Better not have been set! */
	if (set_result < 0) {
	    gmp_unlock_adcat_entry(addr_vect->av_catalog, ordinal);
	    gmp_addr_vect_clean(addr_vect);
	    return -1;			/* Out of memory */
	}
    }

    return 0;
}


/*
 * gmp_addr_list_clean
 *
 * Flush an address list and then free the patricia tree root.
 *
 * The address list structure itself is left intact but empty.
 *
 * Calls back to free each entry.
 */
void
gmp_addr_list_clean (gmp_addr_list *addr_list)
{
    /* Flush the addresses. */

    gmp_flush_addr_list(addr_list);

    /* Clean the bit vector. */

    bv_clean(&addr_list->addr_vect.av_vector);

    /* Free the patricia tree root. */

    if (addr_list->addr_list_root) {
	gmpx_assert(gmpx_patricia_lookup_least(addr_list->addr_list_root) ==
		    NULL);
	gmpx_patroot_destroy(addr_list->addr_list_root);
	addr_list->addr_list_root = NULL;
    }
}


/*
 * gmp_addr_vect_inter
 *
 * Form an intersection of two address vectors, storing the result in
 * the third vector.  Typically, the caller will supply a callback and
 * context, and most of the real work will be done there.
 *
 * If a callback is provided, it will be called for each bit set or
 * changed in the intersection of the two vectors according to cb_opt.
 * 
 * The destination may be NULL, or may be one of the two parameters.
 *
 * Returns 0 if all OK, or -1 if out of memory.
 */
int gmp_addr_vect_inter (gmp_addr_vect *src1, gmp_addr_vect *src2,
			 gmp_addr_vect *dest, bv_callback callback,
			 void *context, bv_callback_option cb_opt)
{
    return bv_and_vectors(gmp_addr_vector(src1), gmp_addr_vector(src2),
			  gmp_addr_vector(dest), callback, context, cb_opt);
}


/*
 * gmp_addr_vect_union
 *
 * Form a union of two address vectors, storing the result in the
 * third vector.  Typically, the caller will supply a callback and
 * context, and most of the real work will be done there.
 *
 * If a callback is provided, it will be called for each bit set or
 * changed in the union of the two vectors according to cb_opt.
 * 
 * The destination may be NULL, or may be one of the two parameters.
 *
 * Returns 0 if all OK, or -1 if out of memory.
 */
int gmp_addr_vect_union (gmp_addr_vect *src1, gmp_addr_vect *src2,
			 gmp_addr_vect *dest, bv_callback callback,
			 void *context, bv_callback_option cb_opt)
{
    return bv_or_vectors(gmp_addr_vector(src1), gmp_addr_vector(src2),
			 gmp_addr_vector(dest), callback, context, cb_opt);
}


/*
 * gmp_addr_vect_minus
 *
 * Clear all bits set in the second vector from those set in the first
 * storing the result in the third vector.  Typically, the caller will
 * supply a callback and context, and most of the real work will be
 * done there.
 *
 * If a callback is provided, it will be called for each bit set or
 * changed in the result according to cb_opt.
 *
 * The destination may be NULL or may be one of the two parameters.
 *
 * Returns 0 if all OK, or -1 if out of memory.
 */
int gmp_addr_vect_minus (gmp_addr_vect *src1, gmp_addr_vect *src2,
			 gmp_addr_vect *dest, bv_callback callback,
			 void *context, bv_callback_option cb_opt)
{
    return bv_clear_vectors(gmp_addr_vector(src1), gmp_addr_vector(src2),
			    gmp_addr_vector(dest), callback, context, cb_opt);
}


/*
 * gmp_addr_vect_compare
 *
 * Compare all bits in the two vectors.  A callback will be made for
 * every bit that is different between the two.
 *
 * The destination may be NULL or may be one of the two parameters.
 *
 * Returns 0 if all OK, or -1 if out of memory.
 */
int gmp_addr_vect_compare (gmp_addr_vect *src1, gmp_addr_vect *src2,
			   bv_callback callback, void *context)
{
    return bv_xor_vectors(gmp_addr_vector(src1), gmp_addr_vector(src2),
			  NULL, callback, context, BV_CALL_SET);
}


/*
 * gmp_addr_vect_walk
 *
 * Walk all bits set in the vector, calling the callback for each set
 * bit.

 * Returns 0 if all OK, or -1 if out of memory.
 */
int gmp_addr_vect_walk (gmp_addr_vect *src, bv_callback callback,
			void *context)
{
    gmpx_assert(src);
    return bv_walk_vector(&src->av_vector, callback, context);
}


/*
 * gmp_addrlist_init
 *
 * Initialize address list management.
 */
static void
gmp_addrlist_init (void)
{
    /* Set up memory blocks. */

    gmp_addr_list_entry_tag =
	gmpx_malloc_block_create(sizeof(gmp_addr_list_entry),
				 "GMP generic address list entry");
    gmp_addr_list_tag =
	gmpx_malloc_block_create(sizeof(gmp_addr_list),
				 "GMP address list");
    gmp_adcat_entry_tag =
	gmpx_malloc_block_create(sizeof(gmp_addr_cat_entry),
				 "GMP address catalog entry");
    gmp_addr_thread_tag =
	gmpx_malloc_block_create(sizeof(gmp_addr_thread),
				 "GMP address thread");
    gmp_addr_thread_entry_tag =
	gmpx_malloc_block_create(sizeof(gmp_addr_thread_entry),
				 "GMP address thread entry");
}


/*
 * gmp_common_init
 *
 * Initialize common GMP code.
 *
 * If both host and router support are compiled in, this routine will be
 * called twice, so it exits silently after the first call.
 */
void
gmp_common_init (void)
{
    /* Bail if we've already been called. */

    if (ga_initialized)
	return;
    ga_initialized = TRUE;

    /* Initialize address list support. */

    gmp_addrlist_init();

    /* Initialize generic packet support. */

    gmpp_init();
}
