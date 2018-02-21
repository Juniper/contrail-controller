/* $Id: gmph_group.c 493569 2012-01-28 13:26:58Z ib-builder $
 *
 * gmph_group.c - IGMP/MLD Host-Side group handling
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_host.h"
#include "gmp_private.h"
#include "gmph_private.h"



/*
 * gmph_alloc_rpt_addr_entry
 *
 * Allocate an address entry for insertion into the protocol report
 * (Allow/Block) address lists.
 *
 * Returns a pointer to the embedded address list entry, or NULL if
 * out of memory.
 */
static gmp_addr_list_entry *
gmph_alloc_rpt_addr_entry (void *context GMPX_UNUSED)
{
    gmph_rpt_msg_addr_entry *report_entry;

    /* Allocate a block and return it. */

    report_entry = gmpx_malloc_block(gmph_group_rpt_entry_tag);

    return &report_entry->msg_addr_entry;
}


/*
 * gmph_free_rpt_addr_entry
 *
 * Callback to free a report address entry.
 */
static void
gmph_free_rpt_addr_entry (gmp_addr_list_entry *addr_entry)
{
    gmph_rpt_msg_addr_entry *report_entry;

    /* Just free the block. */

    report_entry = gmph_addr_list_to_group_list(addr_entry);
    gmpx_free_block(gmph_group_rpt_entry_tag, report_entry);
}


/*
 * gmph_delete_rpt_addr_entry
 *
 * Delete a report address entry from its list and free it.
 */
void
gmph_delete_rpt_addr_entry (gmph_rpt_msg_addr_entry *report_entry)
{
    gmp_delete_addr_list_entry(&report_entry->msg_addr_entry);
}


/*
 * gmph_group_lookup_first
 *
 * Look up the first group on an interface.
 *
 * Returns a pointer to the group, or NULL if it's not there.
 */
gmph_group *
gmph_group_lookup_first (gmph_intf *intf)
{
    gmpx_patnode *node;
    gmph_group *group;

    node = gmpx_patricia_lookup_least(intf->hintf_group_root);
    group = gmph_intf_patnode_to_group(node);

    return group;
}


/*
 * gmph_group_lookup
 *
 * Look up a group entry given the group address and the interface.
 *
 * Returns a pointer to the group entry, or NULL if it's not there.
 */
gmph_group *
gmph_group_lookup (gmph_intf *intf, const u_int8_t *group_addr)
{
    gmph_group *group;
    patnode *node;

    /* Look up the entry. */

    node = gmpx_patricia_lookup(intf->hintf_group_root, group_addr);
    group = gmph_intf_patnode_to_group(node);

    return group;
}


/*
 * gmph_group_create
 *
 * Create a new group entry, given the group and interface, and link it in.
 *
 * Returns a pointer to the group entry, or NULL if no memory.
 */
gmph_group *
gmph_group_create (gmph_intf *intf, const u_int8_t *group_addr)
{
    gmph_group *group;
    gmph_instance *instance;

    instance = intf->hintf_instance;

    /* Allocate the block. */

    group = gmpx_malloc_block(gmph_group_tag);
    if (!group)
	return NULL;			/* No memory */

    /* Initialize it. */

    thread_new_circular_thread(&group->hgroup_client_thread);
    memmove(group->hgroup_addr.gmp_addr, group_addr, instance->hinst_addrlen);

    gmp_addr_list_init(&group->hgroup_src_addr_list, &instance->hinst_addr_cat,
		       gmp_alloc_generic_addr_list_entry,
		       gmp_free_generic_addr_list_entry, NULL);
    gmp_addr_list_init(&group->hgroup_allow_list, &instance->hinst_addr_cat,
		       gmph_alloc_rpt_addr_entry, gmph_free_rpt_addr_entry,
		       NULL);
    gmp_addr_list_init(&group->hgroup_block_list, &instance->hinst_addr_cat,
		       gmph_alloc_rpt_addr_entry, gmph_free_rpt_addr_entry,
		       NULL);
    gmp_addr_list_init(&group->hgroup_query_list, &instance->hinst_addr_cat,
		       gmp_alloc_generic_addr_list_entry,
		       gmp_free_generic_addr_list_entry, NULL);

    group->hgroup_change_rpt_timer =
	gmpx_create_timer(instance->hinst_context, "GMP host change report",
			  gmph_group_change_report_timer_expiry, group);
    group->hgroup_query_timer =
	gmpx_create_timer(instance->hinst_context, "GMP host group query",
			  gmph_group_query_timer_expiry, group);

    group->hgroup_filter_mode = GMP_FILTER_MODE_INCLUDE;
    group->hgroup_intf = intf;

    /* Link it in. */

    gmpx_assert(gmpx_patricia_add(intf->hintf_group_root,
				  &group->hgroup_intf_patnode));

    return group;
}


/*
 * gmph_destroy_group
 *
 * Destroy a group entry.  Delinks it and frees memory.
 * If the entry is locked (the lock count is nonzero) we flag the entry as
 * deleted. This routine will very shortly be called back with the lock
 * removed.
 */
static void
gmph_destroy_group (gmph_group *group)
{
    gmph_intf *intf;

    intf = group->hgroup_intf;

    /* Make sure there are no clients; they should already be cleaned up. */

    gmpx_assert(thread_circular_thread_empty(&group->hgroup_client_thread));

    /* If the refcount is nonzero, flag that we're deleted and bail. */

    if (group->hgroup_lock_count) {
	group->hgroup_is_deleted = TRUE;
	return;
    }

    /* Unmark any pending transmisison for the group. */

    gmph_unmark_pending_group_xmit(group, TRUE);

    /*
     * Flush the address lists.  They should normally be empty, but may not
     * be if we're shutting down unceremoniously.
     */
    gmp_addr_list_clean(&group->hgroup_allow_list);
    gmp_addr_list_clean(&group->hgroup_block_list);
    gmp_addr_list_clean(&group->hgroup_query_list);
    gmp_addr_list_clean(&group->hgroup_src_addr_list);

    /* Delink from the interface transmit thread. */

    gmph_dequeue_group_xmit(group);

    /* Destroy the timers. */

    gmpx_destroy_timer(group->hgroup_change_rpt_timer);
    gmpx_destroy_timer(group->hgroup_query_timer);

    /* Remove ourselves from the interface tree. */

    gmpx_assert(gmpx_patricia_delete(intf->hintf_group_root,
				     &group->hgroup_intf_patnode));

    /* Free the block. */

    gmpx_free_block(gmph_group_tag, group);
}


/*
 * gmph_lock_group
 *
 * Temporarily lock a group to keep it from being deleted.
 * These locks are expected to be very short-lived (within the scope
 * of a single execution) and to never be nested (though we allow
 * a limited amount of nesting.)
 */
void
gmph_lock_group (gmph_group *group)
{
    group->hgroup_lock_count++;
}


/*
 * gmph_unlock_group
 *
 * Unlock the group. If the refcount is zero and the deleted flag
 * is set, go ahead and destroy it now.
 * Returns TRUE if the group has not been deleted, or FALSE if deleted
 *
 */
boolean
gmph_unlock_group (gmph_group *group)
{
    gmpx_assert(group->hgroup_lock_count);
    group->hgroup_lock_count--;

    if (!group->hgroup_lock_count && group->hgroup_is_deleted)
    {
	gmph_destroy_group(group);
	return FALSE;
    }
    return TRUE;
}


/*
 * gmph_destroy_intf_groups
 *
 * Unceremoniously destroy all groups on an interface.
 */
void
gmph_destroy_intf_groups (gmph_intf *intf)
{
    gmph_group *group;

    /* Walk all groups on the interface. */

    while (TRUE) {
	group = gmph_group_lookup_first(intf);
	if (!group)
	    break;

	/* Got a group.  Blast all of the associated client groups. */

	gmph_destroy_group_client_groups(group);

	/* Now destroy the group itself. */

	gmph_destroy_group(group);
    }
}


/*
 * gmph_group_lookup_create
 *
 * Look up a group entry for an interface, and create one if it's not there.
 *
 * Returns a pointer to the group entry, or NULL if out of memory.
 */
gmph_group *
gmph_group_lookup_create (gmph_intf *intf, const u_int8_t *group_addr)
{
    gmph_group *group;

    /* Look it up. */

    group = gmph_group_lookup(intf, group_addr);

    /* If it's not there, create it. */

    if (!group)
	group = gmph_group_create(intf, group_addr);

    return group;
}


/*
 * gmph_start_change_rpt_timer
 *
 * Start the change report timer for a group.  Marks the group as having a
 * transmission pending.
 */
void
gmph_start_change_rpt_timer (gmph_group *group, u_int32_t ivl, u_int jitter_pct)
{
    gmpx_start_timer(group->hgroup_change_rpt_timer, ivl, jitter_pct);
    gmph_mark_pending_group_xmit(group);
}


/*
 * gmph_start_query_timer
 *
 * Start the query timer for a group.  Marks the group as having a
 * transmission pending.
 */
void
gmph_start_query_timer (gmph_group *group, u_int32_t ivl, u_int jitter_pct)
{
    gmpx_start_timer(group->hgroup_query_timer, ivl, jitter_pct);
    gmph_mark_pending_group_xmit(group);
}


/*
 * gmph_enqueue_group_xmit
 *
 * Enqueue a group entry for transmission of a Report message on an
 * interface.
 *
 * All timers and linkages are expected to be set.
 */
void
gmph_enqueue_group_xmit (gmph_group *group)
{
    /* Enqueue it if it's not already on the queue. */

    if (!thread_node_on_thread(&group->hgroup_xmit_thread)) {
	thread_circular_add_bottom(&group->hgroup_intf->hintf_xmit_head,
				   &group->hgroup_xmit_thread);

	/* Note that the group is pending transmission. */

	gmph_mark_pending_group_xmit(group);
    }
}


/*
 * gmph_first_group_xmit
 *
 * Get the first group entry on an interface transmit list.
 *
 * Returns a pointer to the group, or NULL if the list is empty.
 */
gmph_group *
gmph_first_group_xmit (gmph_intf *intf)
{
    thread *thread_ptr;
    gmph_group *group;

    thread_ptr = thread_circular_top(&intf->hintf_xmit_head);
    group = gmph_xmit_thread_to_group(thread_ptr);

    return group;
}


/*
 * gmph_next_group_xmit
 *
 * Get the next group entry on an interface transmit list given a group.
 *
 * Returns a pointer to the group, or NULL if the list is empty.
 */
gmph_group *
gmph_next_group_xmit (gmph_group *group)
{
    thread *thread_ptr;
    gmph_group *next_group;

    thread_ptr = 
	thread_circular_thread_next(&group->hgroup_intf->hintf_xmit_head,
				    &group->hgroup_xmit_thread);
    next_group = gmph_xmit_thread_to_group(thread_ptr);

    return next_group;
}


/*
 * gmph_dequeue_group_xmit
 *
 * Dequeue a group entry from an interface.
 */
void
gmph_dequeue_group_xmit (gmph_group *group)
{
    thread_remove(&group->hgroup_xmit_thread);
    gmph_unmark_pending_group_xmit(group, FALSE);
}


/*
 * gmph_group_xmit_pending
 *
 * Returns TRUE if the group is pending transmission, or FALSE if not.
 */
boolean
gmph_group_xmit_pending (gmph_group *group)
{
    return thread_node_on_thread(&group->hgroup_xmit_thread);
}


/*
 * gmph_mark_pending_group_xmit
 *
 * Mark a group as pending some kind of transmission (eventually.)  If the
 * flag isn't set, we bump the count on the interface, and if it is set, we
 * leave it alone.
 */
void
gmph_mark_pending_group_xmit (gmph_group *group)
{
    /* Do it if not already marked. */

    if (!group->hgroup_xmit_pending) {
	group->hgroup_xmit_pending = TRUE;
	gmph_intf_increment_pending_xmit_count(group->hgroup_intf);
    }
}


/*
 * gmph_unmark_pending_group_xmit
 *
 * Unmark a group as pending some kind of transmission if all of the
 * pieces are in place.  If the flag is set, we decrement the count on
 * the interface, and if it is clear, we leave it alone.
 *
 * If "force" is TRUE, we unconditionally clear the flag.
 */
void
gmph_unmark_pending_group_xmit (gmph_group *group, boolean force)
{
    if (!force) {

	/* Bail if the group is still pending transmission. */

	if (gmph_group_xmit_pending(group))
	    return;

	/* Bail if the group has a change message pending. */

	if (gmpx_timer_running(group->hgroup_change_rpt_timer) ||
	    group->hgroup_change_msg_due)
	    return;

	/* Bail if the group has a query reply pending. */

	if (gmpx_timer_running(group->hgroup_query_timer) ||
	    group->hgroup_reply_due)
	    return;

    }

    /* Looks good.  Clear the flag if it has been set. */

    if (group->hgroup_xmit_pending) {
	group->hgroup_xmit_pending = FALSE;
	gmph_intf_decrement_pending_xmit_count(group->hgroup_intf);
    }
}


/*
 * gmph_attempt_group_free
 *
 * Attempt to free the group entry.  We will do so if there is no client
 * interest in the group and there's nothing more to send for the group.
 */
void
gmph_attempt_group_free (gmph_group *group)
{
    /* Bail if there is any client interest. */

    if (!thread_circular_thread_empty(&group->hgroup_client_thread))
	return;

    /* Bail if we have any source state. */

    if (gmph_group_is_active(group))
	return;

    /* Bail if there's anything in the Allow or Block lists. */

    if (!gmp_addr_list_empty(&group->hgroup_allow_list) ||
	!gmp_addr_list_empty(&group->hgroup_block_list))
	return;

    /* Bail if we are waiting to send a change report. */

    if (gmpx_timer_running(group->hgroup_change_rpt_timer) ||
	group->hgroup_change_msg_due ||
	group->hgroup_mode_change_rexmit_count)
	return;

    /* Looks safe.  Destroy the group. */

    gmph_destroy_group(group);
}


/*
 * gmph_group_aggregate_client_state
 *
 * Aggregates the filter state of each client into the new filter state
 * for the interface.
 *
 * Two address vectors are built, one with all of the sources for which there
 * was include interest, and one with all sources for which there was a
 * unanimous exclude interest across all clients.  The resultant filter
 * mode is also returned.
 *
 * Returns 0 if all OK, or -1 if out of memory.
 */
static int
gmph_group_aggregate_client_state (gmph_group *group,
				   gmp_addr_vect *includes,
				   gmp_addr_vect *excludes,
				   gmp_addr_vect **group_addr_vect,
				   gmp_filter_mode *filter_mode)
{
    thread *cur_thread;
    gmph_client_group *client_group;
    boolean exclude_seen;

    exclude_seen = FALSE;

    /*
     * Walk through all clients bound to the group.
     */
    FOR_ALL_CIRCULAR_THREAD_ENTRIES(&group->hgroup_client_thread,
				    cur_thread) {
	client_group = gmph_thread_to_client_group(cur_thread);

	/*
	 * If this is an Include request, form the union of the existing
	 * include entries and the ones from the client.
	 */
	if (client_group->client_filter_mode == GMP_FILTER_MODE_INCLUDE) {

	    if (bv_or_vectors(&includes->av_vector,
			      &client_group->client_addr_vect.av_vector,
			      &includes->av_vector, NULL, NULL,
			      BV_CALL_SET) < 0) {

		/* Ran out of memory. */

	      memory_gone:
		gmp_addr_vect_clean(includes);
		gmp_addr_vect_clean(excludes);
		return -1;
	    }

	} else {

	    /*
	     * Exclude request.  If this is the first one we've seen,
	     * copy the client request into the scratch list.  Otherwise,
	     * form the set intersection of the scratch list and the
	     * client list.
	     */
	    if (!exclude_seen) {	/* First one */
		exclude_seen = TRUE;
		if (bv_copy_vector(&client_group->client_addr_vect.av_vector,
				   &excludes->av_vector, NULL, NULL,
				   BV_CALL_SET) < 0) {
		    goto memory_gone;	/* Out of memory */
		}

	    } else {			/* Not the first one */

		if (bv_and_vectors(&excludes->av_vector,
				   &client_group->client_addr_vect.av_vector,
				   &excludes->av_vector, NULL, NULL,
				   BV_CALL_SET) < 0) {
		    goto memory_gone;	/* Out of memory */
		}
	    }
	}
    }

    /*
     * We've gotten through them all.  Now the final step.  If there
     * were any excludes, the interface list is the excludes scratch
     * list, minus the include scratch list.
     */
    if (exclude_seen) {
	if (bv_clear_vectors(&excludes->av_vector, &includes->av_vector,
			     &excludes->av_vector, NULL, NULL,
			     BV_CALL_SET) < 0)
	    goto memory_gone;		/* Out of memory */
	*filter_mode = GMP_FILTER_MODE_EXCLUDE;
	*group_addr_vect = excludes;
    } else {
	*filter_mode = GMP_FILTER_MODE_INCLUDE;
	*group_addr_vect = includes;
    }

    return 0;
}


/*
 * gmph_set_report_entry_rexmit
 *
 * Initializes a report address entry, given a pointer to the embedded
 * address list entry.
 */
void
gmph_set_report_entry_rexmit (gmph_group *group,
			      gmp_addr_list_entry *addr_entry)
{
    gmph_rpt_msg_addr_entry *rpt_entry;

    /* Initialize it. */

    rpt_entry = gmph_addr_list_to_group_list(addr_entry);
    rpt_entry->msg_rexmit_count = group->hgroup_intf->hintf_robustness;
}


/*
 * gmph_same_mode_callback
 *
 * Callback from the list compare routines for handling the case where both
 * old and new interface states have the same filter mode.
 *
 * Returns 0 if all OK, or 1 if out of memory.
 */
static boolean
gmph_same_mode_callback (void *context, bv_bitnum_t bit_number,
			 boolean new_value GMPX_UNUSED,
			 boolean old_value GMPX_UNUSED)
{
    gmph_intf *intf;
    gmph_group *group;
    gmph_group_set_context *group_context;
    gmp_addr_list_entry *new_addr_entry;

    group_context = context;
    group = group_context->ctx_group;
    intf = group->hgroup_intf;

    /*
     * We start with the ordinal of an entry to be added to the add list.
     * First, look up the entry on the delete list, and if it's there,
     * move it to the add list.
     */
    new_addr_entry =
	gmp_lookup_addr_entry(group_context->ctx_del_list, bit_number);
    if (new_addr_entry) {

	/* In the delete list.  Move it to the add list. */

	gmp_move_addr_list_entry(group_context->ctx_add_list,
				 new_addr_entry);

    } else {

	/*
	 * Not in the delete list.  See if the entry is already in the add
	 * list.
	 */
	new_addr_entry = gmp_lookup_addr_entry(group_context->ctx_add_list,
					       bit_number);
	if (!new_addr_entry) {

	    /* Not in the add list.  Create a new entry. */

	    new_addr_entry =
		gmp_create_addr_list_entry(group_context->ctx_add_list,
					   bit_number);
	    if (!new_addr_entry)
		return TRUE;		/* Out of memory */
	}
    }

    /* Reset the retransmit count in the address entry. */

    gmph_set_report_entry_rexmit(group, new_addr_entry);

    return FALSE;
}


/*
 * gmph_compare_same_mode
 *
 * Perform the necessary actions when both the old and new interface
 * filter state are in the same filter mode.
 *
 * Returns 0 if all OK, or -1 if no memory.
 */
static int
gmph_compare_same_mode (gmph_group *group, gmp_addr_vect *group_addr_vect,
			gmp_filter_mode filter_mode)
{
    gmph_group_set_context context;
    gmp_addr_vect *allow_adds, *block_adds;

    /*
     * The old and new lists are in the same mode.
     *
     * For Include mode, add everything in the set {old - new} to the
     * Block list and everything in the set {new - old} to the Allow
     * list.
     *
     * For Exclude mode, add everything in the set {old - new} to the
     * Allow list and everything in the set {new - old} to the Block
     * list.
     *
     * Most of this magic happens in the callback routine.
     */
    context.ctx_group = group;

    if (filter_mode == GMP_FILTER_MODE_INCLUDE) {
	allow_adds = group_addr_vect;
	block_adds = &group->hgroup_src_addr_list.addr_vect;
    } else {
	allow_adds = &group->hgroup_src_addr_list.addr_vect;
	block_adds = group_addr_vect;
    }

    /* First, update the Allow list with {new - old}. */

    context.ctx_add_list = &group->hgroup_allow_list;
    context.ctx_del_list = &group->hgroup_block_list;
    if (gmp_addr_vect_minus(allow_adds, block_adds, NULL,
			    gmph_same_mode_callback, &context,
			    BV_CALL_SET) < 0) {
	return -1;			/* Out of memory */
    }

    /* Now update the Block list with {old - new}. */

    context.ctx_add_list = &group->hgroup_block_list;
    context.ctx_del_list = &group->hgroup_allow_list;
    if (gmp_addr_vect_minus(block_adds, allow_adds, NULL,
			    gmph_same_mode_callback, &context,
			    BV_CALL_SET) < 0) {
	return -1;			/* Out of memory */
    }

    return 0;
}


/*
 * gmp_diff_mode_callback
 *
 * Callback when walking an address vector to the allow or block list,
 * as appropriate.
 *
 * We create an entry for the destination list and install it.
 */
static boolean
gmph_diff_mode_callback (void *context, bv_bitnum_t bit_number,
			 boolean new_value GMPX_UNUSED, boolean old_value)
{
    gmp_addr_list_entry *addr_entry;
    gmph_group_set_context *group_context;

    group_context = context;

    gmpx_assert(!old_value);		/* It's supposed to be empty! */

    /* Create a new address list entry and enqueue it. */

    addr_entry = gmp_create_addr_list_entry(group_context->ctx_add_list,
					    bit_number);

    /* Initialize the entry. */

    gmph_set_report_entry_rexmit(group_context->ctx_group, addr_entry);

    return FALSE;
}


/*
 * gmph_compare_different_mode
 *
 * Perform the necessary actions when the old and new filter modes are
 * different.
 *
 * Returns 0 if all OK, or -1 if no memory.
 */
static int
gmph_compare_different_mode (gmph_group *group, gmp_addr_vect *group_addr_vect,
			     gmp_filter_mode filter_mode)
{
    gmph_group_set_context context;
    gmph_intf *intf;

    /*
     * The old and new lists are in different modes.
     *
     * For Include mode, add everything in the new list to the Allow
     * list and flush the Block list.
     *
     * For Exclude mode, add everything in the new list to the Block
     * list and flush the Allow list.
     *
     * Most of this magic happens in the callback routine.
     */
    context.ctx_group = group;

    if (filter_mode == GMP_FILTER_MODE_INCLUDE) {
	context.ctx_add_list = &group->hgroup_allow_list;
	context.ctx_del_list = &group->hgroup_block_list;
    } else {
	context.ctx_add_list = &group->hgroup_block_list;
	context.ctx_del_list = &group->hgroup_allow_list;
    }

    /* Replace the list we're adding to with our new set. */

    gmp_flush_addr_list(context.ctx_add_list);
    if (gmp_addr_vect_walk(group_addr_vect, gmph_diff_mode_callback,
			   &context) < 0) {
	return -1;			/* Out of memory */
    }

    /* Now flush the list we're not using. */

    gmp_flush_addr_list(context.ctx_del_list);

    /*
     * If the interface is passive, clean up everything involving mode
     * change messages, since we're not going to be sending any.  If
     * not passive, flag that we need to send them.
     */
    intf = group->hgroup_intf;
    if (intf->hintf_passive) {
	group->hgroup_mode_change_rexmit_count = 0;
	gmpx_stop_timer(group->hgroup_change_rpt_timer);
	group->hgroup_change_msg_due = FALSE;
	gmph_unmark_pending_group_xmit(group, FALSE);
    } else {
	group->hgroup_mode_change_rexmit_count = intf->hintf_robustness;
    }

    /*
     * Flush the main group and query transmit lists (for answering
     * queries).  If there are entries on them, it means that we're in
     * the middle of answering a query and the entries are based on
     * the old filter mode.
     */
    gmp_flush_xmit_list(&group->hgroup_src_addr_list);
    gmp_flush_xmit_list(&group->hgroup_query_list);

    return 0;
}


/*
 * gmph_reevaluate_group
 *
 * Reevaluate the interface state, based on the newly-updated client state.
 *
 * Returns 0 if all OK, or -1 if no memory.
 */
int
gmph_reevaluate_group (gmph_group *group)
{
    gmph_instance *instance;
    gmph_intf *intf;
    gmp_addr_vect *group_addr_vect;
    gmp_filter_mode filter_mode;
    gmp_addr_vect includes;
    gmp_addr_vect excludes;

    intf = group->hgroup_intf;

    gmp_init_addr_vector(&includes, NULL);
    gmp_init_addr_vector(&excludes, NULL);

    instance = intf->hintf_instance;

    /* Aggregate the client state into the new group state. */

    if (gmph_group_aggregate_client_state(group, &includes, &excludes,
					  &group_addr_vect,
					  &filter_mode) < 0) {
	return -1;			/* Out of memory */
    }


    /*
     * We have our new list and filter mode.  Now we compare them to the
     * old ones and perform the arcane operations thus required.
     *
     * We cheat a little bit here, which is that if the new mode is
     * Include and the address list is empty (meaning that the group
     * is to be deleted) we fake that we're switching modes, even
     * though we might not be.  The reason is to generate a TO_IN{}
     * that is a clear indication of the group going away; otherwise
     * if the old mode was Include we would just generate a BLOCK
     * deleting the last sources, and can't tell that it's really a
     * Leave without context.  All of this is really so that we can
     * cleanly translate to a Leave if we're doing IGMPv2 or MLDv1.
     */
    if (group->hgroup_filter_mode != filter_mode ||
	(filter_mode == GMP_FILTER_MODE_INCLUDE &&
	 gmp_addr_vect_empty(group_addr_vect))) {

	/*
	 * The old and new lists are in different modes.  Do the
	 * appropriate processing.
	 */
	if (gmph_compare_different_mode(group, group_addr_vect,
					filter_mode) < 0) {
	    return -1;		/* Out of memory */
	}

    } else {

	/*
	 * Both old and new lists are in the same mode.  Do the appropriate
	 * processing.
	 */
	if (gmph_compare_same_mode(group, group_addr_vect, filter_mode) < 0) {
	    return -1;		/* Out of memory */
	}
    }
    group->hgroup_filter_mode = filter_mode;

    /* Everything has been updated.  Update the group source list. */

    gmp_build_addr_list(&group->hgroup_src_addr_list, group_addr_vect);

    /* Clean up the scratch vectors. */

    gmp_addr_vect_clean(&includes);
    gmp_addr_vect_clean(&excludes);

    /* If we're not passive, start the timer to send the packets. */

    if (!intf->hintf_passive) {
	gmph_start_change_rpt_timer(group, 0, GMPH_INITIAL_REPORT_JITTER);
    }

    /*
     * Attempt to destroy the group.  This won't actually happen unless we
     * have just received an Include{} and we're in passive mode.
     */
    gmph_attempt_group_free(group);

    return 0;
}


/*
 * gmph_group_source_requested
 *
 * Returns TRUE if the source is being requested as part of the group, or
 * FALSE if not.
 */
boolean
gmph_group_source_requested (gmph_group *group, const u_int8_t *source_addr)
{
    gmph_instance *instance;
    gmp_addr_cat_entry *cat_entry;

    instance = group->hgroup_intf->hintf_instance;

    /* Look up the catalog entry.  It may or may not be there. */

    cat_entry = gmp_lookup_addr_cat_entry(&instance->hinst_addr_cat,
					  source_addr);

    /* Check out the filter mode. */

    if (group->hgroup_filter_mode == GMP_FILTER_MODE_INCLUDE) {

	/* Include mode.  If no catalog entry, we didn't request it. */

	if (!cat_entry)
	    return FALSE;

	/*
	 * If the address is in the source list, we requested it.  If
	 * it's not, we're not.
	 */
	return (gmp_addr_in_list(&group->hgroup_src_addr_list,
				 cat_entry->adcat_ent_ord));
    } else {

	/* Exclude mode.  If no catalog entry, we requested it. */

	if (!cat_entry)
	    return TRUE;

	/*
	 * If the address is in the source list, we're not requesting
	 * it.  If it's not, we are.
	 */
	return (!gmp_addr_in_list(&group->hgroup_src_addr_list,
				  cat_entry->adcat_ent_ord));
    }
}
