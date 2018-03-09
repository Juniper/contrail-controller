/* $Id: gmph_intf.c 362048 2010-02-09 00:25:11Z builder $
 *
 * gmph_intf.c - IGMP/MLD Host-Side Interface Routines
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

/* Global tree of all interfaces, per protocol. */

gmpx_patroot *gmph_global_intf_tree[GMP_NUM_PROTOS];

/* Forward references */

static void gmph_intf_query_timer_expiry(gmpx_timer *timer, void *context);

/*
 * gmph_intf_lookup
 *
 * Look up an interface, given the instance and interface ID.
 *
 * Returns a pointer to the interface structure, or NULL if not there.
 */
gmph_intf *
gmph_intf_lookup (gmph_instance *instance, gmpx_intf_id intf_id)
{
    gmph_intf *intf;
    gmpx_patnode *node;

    /* Look up the interface in the tree. */

    node = gmpx_patricia_lookup(instance->hinst_intfs, &intf_id);
    intf = gmph_inst_patnode_to_intf(node);

    return intf;
}


/*
 * gmph_intf_lookup_global
 *
 * Look up an interface, given the protocol and interface ID.
 *
 * Returns a pointer to the interface structure, or NULL if not there.
 */
gmph_intf *
gmph_intf_lookup_global (gmp_proto proto, gmpx_intf_id intf_id)
{
    gmph_intf *intf;
    gmpx_patnode *node;

    /* Look up the interface in the tree. */

    gmpx_assert(proto < GMP_NUM_PROTOS);
    node = gmpx_patricia_lookup(gmph_global_intf_tree[proto], &intf_id);
    intf = gmph_global_patnode_to_intf(node);

    return intf;
}


/*
 * gmph_destroy_intf
 *
 * Free an interface.
 */
static void
gmph_destroy_intf (gmph_intf *intf)
{
    gmph_instance *instance;

    /* Toss the group tree.  It better be empty. */

    gmpx_assert(gmph_group_lookup_first(intf) == NULL);
    gmpx_patroot_destroy(intf->hintf_group_root);
    intf->hintf_group_root = NULL;

    /* The group transmit thread better be empty. */

    gmpx_assert(thread_circular_thread_empty(&intf->hintf_xmit_head));

    /* Destroy the timers. */

    gmpx_destroy_timer(intf->hintf_gen_query_timer);
    gmpx_destroy_timer(intf->hintf_basic_querier);
    gmpx_destroy_timer(intf->hintf_leaves_querier);
    gmpx_destroy_timer(intf->hintf_soft_detach_timer);

    /* Delete the interface from the threads. */

    instance = intf->hintf_instance;
    gmpx_assert(gmpx_patricia_delete(instance->hinst_intfs,
				     &intf->hintf_inst_patnode));
    gmpx_assert(
	gmpx_patricia_delete(gmph_global_intf_tree[instance->hinst_proto],
			     &intf->hintf_global_patnode));
    gmpx_free_block(gmph_intf_tag, intf);
}


/*
 * gmph_soft_detach_timer_expiry
 *
 * Called with deferral to finish cleaning up an interface when
 * a soft detach has been done.  We must defer the destruction of the
 * interface so that synchronous calls don't get left with a bogus
 * pointer.
 */
static void
gmph_soft_detach_timer_expiry (gmpx_timer *timer, void *context)
{
    gmph_intf *intf;

    gmpx_stop_timer(timer);
    intf = context;

    /* Alert the client by calling the callback. */

    (*intf->hintf_soft_detach_callback)(intf->hintf_instance->hinst_proto,
					intf->hintf_id,
					intf->hintf_soft_detach_context);

    /* Blast the interface. */

    gmph_destroy_intf_groups(intf);
    gmph_destroy_intf(intf);
}


/*
 * gmph_attempt_intf_free
 *
 * Try to get rid of an interface if it is ready.
 *
 * We delay getting rid of it unless there are no pending
 * transmissions for any group, and there is a soft-detach callback,
 * in which case we launch a timer to call back that routine before
 * finishing the job.
 */
void
gmph_attempt_intf_free (gmph_intf *intf)
{
    /* Bail if we're not shutting down. */

    if (!gmph_intf_shutting_down(intf))
	return;

    /* Bail if there are any transmissions pending. */

    if (intf->hintf_pending_xmit_count)
	return;

    /* Bail if the timer is already running. */

    if (gmpx_timer_running(intf->hintf_soft_detach_timer))
	return;
    
    /* Blast the interface in a deferred manner. */

    gmpx_start_timer(intf->hintf_soft_detach_timer, 0, 0);
}


/*
 * gmph_destroy_instance_intfs
 *
 * Destroy all interfaces bound to an instance.  This is all quite
 * unceremonious;  we simply blast all of the state.
 */
void
gmph_destroy_instance_intfs (gmph_instance *instance)
{
    gmpx_patnode *node;
    gmph_intf *intf;

    /* Walk each interface on the instance and destroy it. */

    while (TRUE) {

	node = gmpx_patricia_lookup_least(instance->hinst_intfs);
	intf = gmph_inst_patnode_to_intf(node);
	if (!intf)
	    break;			/* All done */

	/* Got an interface.  Blast all the groups on it. */

	gmph_destroy_intf_groups(intf);

	/* Now blast the interface itself. */

	gmph_destroy_intf(intf);
    }
}


/*
 * gmph_intf_increment_pending_xmit_count
 *
 * Increment the count of pending (eventual) transmissions on the interface.
 */
void
gmph_intf_increment_pending_xmit_count (gmph_intf *intf)
{
    intf->hintf_pending_xmit_count++;
}


/*
 * gmph_intf_decrement_pending_xmit_count
 *
 * Decrement the count of pending (eventual) transmissions on the interface.
 * Triggers the destruction of the interface if a soft detach is pending
 * and there are no more pending transmissions.
 */
void
gmph_intf_decrement_pending_xmit_count (gmph_intf *intf)
{
    gmpx_assert(intf->hintf_pending_xmit_count);
    intf->hintf_pending_xmit_count--;
    if (!intf->hintf_pending_xmit_count)
	gmph_attempt_intf_free(intf);
}


/*
 * gmph_start_general_query_timer
 *
 * Start the general query timer for an interface.  We charge one pending
 * transmission against the general query timer until after it expires.
 */
void
gmph_start_general_query_timer (gmph_intf *intf, u_int32_t ivl,
				u_int jitter_pct)
{
    /* If the timer isn't running, bump the pending transmit count. */

    if (!gmpx_timer_running(intf->hintf_gen_query_timer))
	gmph_intf_increment_pending_xmit_count(intf);

    /* Start the timer. */

    gmpx_start_timer(intf->hintf_gen_query_timer, ivl, jitter_pct);
}


/*
 * gmph_intf_querier_timer_expiry
 *
 * Called when either of the old querier timers expires.  We reevaluate
 * the version.
 */
static void
gmph_intf_querier_timer_expiry (gmpx_timer *timer, void *context)
{
    gmph_intf *intf;

    intf = context;

    /* Stop the timer. */

    gmpx_stop_timer(timer);

    /* Reevaluate the version. */

    gmph_intf_evaluate_version(intf);
}


/*
 * gmph_create_intf
 *
 * Create an interface structure and add it to the instance tree.
 *
 * Returns a pointer to the interface structure, or NULL if no memory.
 */
static gmph_intf *
gmph_create_intf (gmph_instance *instance, gmpx_intf_id intf_id)
{
    gmph_intf *intf;

    /* Allocate the block. */

    intf = gmpx_malloc_block(gmph_intf_tag);
    if (!intf)
	return NULL;

    /* Initialize the tree. */

    intf->hintf_group_root = 
	gmpx_patroot_init(instance->hinst_addrlen,
			  GMPX_PATRICIA_OFFSET(gmph_group, hgroup_intf_patnode,
					       hgroup_addr));
    if (!intf->hintf_group_root) { /* No memory */
	gmpx_free_block(gmph_intf_tag, intf);
	return NULL;
    }

    /* Initialize the transmit queue. */

    thread_new_circular_thread(&intf->hintf_xmit_head);

    /* Put the interface into the instance tree. */

    intf->hintf_id = intf_id;
    gmpx_assert(gmpx_patricia_add(instance->hinst_intfs,
				  &intf->hintf_inst_patnode));

    /* Put the interface into the global tree. */

    gmpx_assert(
	gmpx_patricia_add(gmph_global_intf_tree[instance->hinst_proto],
			  &intf->hintf_global_patnode));

    /* Initialize a few more things. */

    intf->hintf_instance = instance;
    intf->hintf_ver = GMP_VERSION_DEFAULT;
    intf->hintf_cfg_ver = GMP_VERSION_DEFAULT;
    intf->hintf_robustness = GMP_ROBUSTNESS_DEFAULT;
    intf->hintf_unsol_rpt_ivl = GMP_UNSOL_RPT_IVL_DEFAULT;

    intf->hintf_gen_query_timer =
	gmpx_create_timer(instance->hinst_context, "GMP host general query",
			  gmph_intf_query_timer_expiry, intf);
    intf->hintf_basic_querier =
	gmpx_create_timer(instance->hinst_context, "GMP basic version querier",
			  gmph_intf_querier_timer_expiry, intf);
    intf->hintf_leaves_querier =
	gmpx_create_timer(instance->hinst_context,
			  "GMP leaves version querier",
			  gmph_intf_querier_timer_expiry, intf);
    intf->hintf_soft_detach_timer =
	gmpx_create_timer(instance->hinst_context,
			  "GMP intf soft detach",
			  gmph_soft_detach_timer_expiry, intf);

    return intf;
}


/*
 * gmph_attach_intf_internal
 *
 * Create an interface entry based on the instance and interface ID.
 *
 * Returns 0 if all OK, -1 if out of memory, or 1 if the interface already
 * exists.
 */
int
gmph_attach_intf_internal (gmph_instance *instance, gmpx_intf_id intf_id)
{
    gmph_intf *intf;

    /*
     * Look up the interface.  Bail if it's already there.  Cancel any
     * pending shutdown if so.
     */
    intf = gmph_intf_lookup(instance, intf_id);
    if (intf) {
	intf->hintf_soft_detach_callback = NULL;
	return 1;			/* Already exists */
    }

    /* Create a new one. */

    intf = gmph_create_intf(instance, intf_id);
    if (!intf)
	return -1;			/* Out of memory */

    return 0;
}


/*
 * gmph_detach_intf_internal
 *
 * Get rid of an interface.  Under normal circumstances, the interface
 * will be inactive, but we tolerate unceremonious deletions by
 * cleaning up all of the attached state.
 *
 * If the "callback" parameter is non-NULL, this means that we're
 * doing a soft detach, and so we wait for all of the groups on the
 * interface to go away (which means that all Leave packets have been
 * sent out) and then later destroy the interface (after calling the
 * callback).
 *
 * Returns 0 if all OK, or 1 if the interface doesn't exist.
 */
int
gmph_detach_intf_internal (gmph_instance *instance, gmpx_intf_id intf_id,
			   gmph_soft_detach_callback callback, void *context)
{
    gmph_intf *intf;

    /* Look up the interface.  Bail if it doesn't exist. */

    intf = gmph_intf_lookup(instance, intf_id);
    if (!intf)
	return 1;			/* Doesn't exist */

    /* If there's a callback, do the soft detach. */

    if (callback) {

	/* Get upset if one is already pending. */

	gmpx_assert(intf->hintf_soft_detach_callback == NULL);

	/* Save the pointer and context. */

	intf->hintf_soft_detach_callback = callback;
	intf->hintf_soft_detach_context = context;

	/* Try to get rid of the interface. */

	gmph_attempt_intf_free(intf);

    } else {

	/* Not soft.  Blast all of the groups on the interface. */

	gmph_destroy_intf_groups(intf);

	/* Should be clean now.  Destroy the interface. */

	gmph_destroy_intf(intf);
    }

    return 0;
}


/*
 * gmph_intf_query_timer_expiry
 *
 * The interface general query timer has expired.
 */
static void
gmph_intf_query_timer_expiry (gmpx_timer *timer, void *context)
{
    gmph_intf *intf;
    gmpx_patnode *node;
    gmph_group *group;

    intf = context;
    gmpx_stop_timer(timer);

    /* Walk all groups on the interface, and enqueue their contents. */

    node = NULL;
    while (TRUE) {
	node = gmpx_patricia_get_next(intf->hintf_group_root, node);
	group = gmph_intf_patnode_to_group(node);
	if (!group)
	    break;
	gmph_group_general_query_timer_expiry(group);
    }
    gmph_kick_xmit(intf);

    /*
     * Decrement the pending transmit count on the interface if the
     * timer is no longer running.
     */
    if (!gmpx_timer_running(timer))
	gmph_intf_decrement_pending_xmit_count(intf);
}


/*
 * gmph_kick_xmit
 *
 * Kick the transmission state machinery for an interface.
 */
void
gmph_kick_xmit (gmph_intf *intf)
{
    /* Do it only if we don't have a pending transmission. */

    if (!intf->hintf_xmit_pending) {

	/* Set the flag and kick the I/O. */

	intf->hintf_xmit_pending = TRUE;
	gmpp_start_xmit(GMP_ROLE_HOST, intf->hintf_instance->hinst_proto,
			intf->hintf_id);
    }
}


/*
 * gmph_intf_evaluate_version
 *
 * Evaluate the version number running on the interface, and make any
 * necessary changes.
 */
void
gmph_intf_evaluate_version (gmph_intf *intf)
{
    gmp_version new_ver;

    /* Base the version on the querier timers. */

    if (gmpx_timer_running(intf->hintf_basic_querier)) {
	new_ver = GMP_VERSION_BASIC;
    } else if (gmpx_timer_running(intf->hintf_leaves_querier)) {
	new_ver = GMP_VERSION_LEAVES;
    } else {
	new_ver = GMP_VERSION_SOURCES;
    }

    /* Now limit it to the maximum configured version. */

    if (intf->hintf_cfg_ver < new_ver)
	new_ver = intf->hintf_cfg_ver;

    intf->hintf_ver = new_ver;
}
