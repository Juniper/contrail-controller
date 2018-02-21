/* $Id: gmpr_intf.c 514187 2012-05-06 12:25:25Z ib-builder $
 *
 * gmpr_intf.c - IGMP/MLD Router-Side Interface Routines
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_private.h"
#include "gmp_router.h"
#include "gmpr_private.h"
#include "gmpr_trace.h"
/* Global tree of all interfaces, per protocol. */

gmpx_patroot *gmpr_global_intf_tree[GMP_NUM_PROTOS];

/* Forward references */

static void gmpr_intf_query_timer_expiry(gmpx_timer *timer, void *context);

/*
 * gmpr_next_instance_intf
 *
 * Returns the next interface on an instance, given the previous one.
 * If the previous pointer is NULL, returns the first interface on the
 * instance.
 *
 * Returns a pointer to the interface, or NULL if there are no more
 * interfaces.
 */
gmpr_intf *
gmpr_next_instance_intf (gmpr_instance *instance, gmpr_intf *prev_intf)
{
    gmpr_intf *next_intf;
    gmpx_patnode *node;

    /* Look up the node. */

    if (prev_intf) {
	node = gmpx_patricia_get_next(instance->rinst_intfs,
				      &prev_intf->rintf_inst_patnode);
    } else {
	node = gmpx_patricia_lookup_least(instance->rinst_intfs);
    }

    /* Convert to a intf pointer.  The pointer may be NULL. */

    next_intf = gmpr_inst_patnode_to_intf(node);

    return next_intf;
}


/*
 * gmpr_intf_lookup
 *
 * Look up an interface, given the instance and interface ID.
 *
 * Returns a pointer to the interface structure, or NULL if not there.
 */
gmpr_intf *
gmpr_intf_lookup (gmpr_instance *instance, gmpx_intf_id intf_id)
{
    gmpr_intf *intf;
    gmpx_patnode *node;

    /* Look up the interface in the tree. */

    node = gmpx_patricia_lookup(instance->rinst_intfs, &intf_id);
    intf = gmpr_inst_patnode_to_intf(node);

    return intf;
}


/*
 * gmpr_intf_lookup_global
 *
 * Look up an interface, given the protocol and interface ID.
 *
 * Returns a pointer to the interface structure, or NULL if not there.
 */
gmpr_intf *
gmpr_intf_lookup_global (gmp_proto proto, gmpx_intf_id intf_id)
{
    gmpr_intf *intf;
    gmpx_patnode *node;

    /* Look up the interface in the tree. */

    gmpx_assert(proto < GMP_NUM_PROTOS);
    node = gmpx_patricia_lookup(gmpr_global_intf_tree[proto], &intf_id);
    intf = gmpr_global_patnode_to_intf(node);

    return intf;
}


/*
 * gmpr_destroy_intf
 *
 * Free an interface.
 */
static void
gmpr_destroy_intf (gmpr_intf *intf)
{
    gmpr_instance *instance;

    /* Toss the input group tree.  It better be empty. */

    gmpx_assert(gmpx_patricia_lookup_least(intf->rintf_group_root) == NULL);
    gmpx_patroot_destroy(intf->rintf_group_root);
    intf->rintf_group_root = NULL;

    /* Toss the output group tree.  It better be empty. */

    gmpx_assert(gmpx_patricia_lookup_least(intf->rintf_oif_group_root) ==
		NULL);
    gmpx_patroot_destroy(intf->rintf_oif_group_root);
    intf->rintf_oif_group_root = NULL;

    /* Toss the host tree.  It better be empty. */

    gmpx_assert(gmpx_patricia_lookup_least(intf->rintf_host_root) == NULL);
    gmpx_patroot_destroy(intf->rintf_host_root);
    intf->rintf_host_root = NULL;

    /* The group transmit thread better be empty. */

    gmpx_assert(thread_circular_thread_empty(&intf->rintf_xmit_head));

    /* Destroy the timers. */

    gmpx_destroy_timer(intf->rintf_query_timer);
    gmpx_destroy_timer(intf->rintf_other_querier_present);

    /* Delete the interface from the threads. */

    instance = intf->rintf_instance;
    gmpx_assert(gmpx_patricia_delete(instance->rinst_intfs,
				     &intf->rintf_inst_patnode));
    gmpx_assert(
	gmpx_patricia_delete(gmpr_global_intf_tree[instance->rinst_proto],
			     &intf->rintf_global_patnode));
    thread_remove(&intf->rintf_startup_thread);

    /* Free the interface entry itself. */

    gmpx_free_block(gmpr_intf_tag, intf);
}


/*
 * gmpr_destroy_instance_intfs
 *
 * Destroy all interfaces bound to an instance.  This is all quite
 * unceremonious;  we simply blast all of the state.
 */
void
gmpr_destroy_instance_intfs (gmpr_instance *instance)
{
    gmpr_intf *intf;

    /* Walk each interface on the instance and destroy it. */

    while (TRUE) {

	intf = gmpr_next_instance_intf(instance, NULL);
	if (!intf)
	    break;			/* All done */

	/* Got an interface.  Blast all the hosts on it. */

	gmpr_destroy_intf_hosts(intf);

	/* Blast all the groups on it. */

	gmpr_destroy_intf_groups(intf);

	/* Now blast the interface itself. */

	gmpr_destroy_intf(intf);
    }
}


/*
 * gmpr_intf_update_gmi
 *
 * Update the group membership interval
 */
static void
gmpr_intf_update_gmi (gmpr_intf *intf)
{
    intf->rintf_group_membership_ivl =
	(intf->rintf_robustness * intf->rintf_query_ivl) +
	intf->rintf_query_resp_ivl;
}


/*
 * gmpr_intf_update_other_querier_interval
 *
 * Update the other-querier-present interval.
 */
static void
gmpr_intf_update_other_querier_interval (gmpr_intf *intf)
{
    intf->rintf_other_querier_ivl =
	(intf->rintf_robustness * intf->rintf_query_ivl) +
	(intf->rintf_query_resp_ivl / 2);
}


/*
 * gmpr_intf_update_lmqt
 *
 * Update the Last Member Query Time.  The spec says to make it twice the
 * query interval, but this means that the timers will expire just before
 * the last query is sent, so we add half of a query interval as well.
 */
static void
gmpr_intf_update_lmqt (gmpr_intf *intf)
{
    intf->rintf_lmqt = (intf->rintf_lmq_ivl * intf->rintf_lmq_count) +
	(intf->rintf_lmq_ivl / 2);
}


/*
 * gmpr_update_lmq_count
 *
 * Update the LMQ count variable.
 */
static void
gmpr_intf_update_lmq_count (gmpr_intf *intf)
{
    /* Use the robustness variable. */

    intf->rintf_lmq_count = intf->rintf_robustness;

    /* Update dependent variables. */

    gmpr_intf_update_lmqt(intf);
}


/*
 * gmp_intf_update_robustness
 *
 * Update the robustness variable.
 */
void
gmpr_intf_update_robustness (gmpr_intf *intf, uint32_t robustness)
{
    /* If the value is zero, use the local value. */

    if (robustness == 0)
	robustness = intf->rintf_local_robustness;

    /* Save the new value. */

    intf->rintf_robustness = robustness;

    /* Update dependent variables. */

    gmpr_intf_update_lmq_count(intf);
    gmpr_intf_update_gmi(intf);
    gmpr_intf_update_other_querier_interval(intf);
}


/*
 * gmpr_intf_update_query_resp_ivl
 *
 * Update the query response interval variable.
 */
static void
gmpr_intf_update_query_resp_ivl (gmpr_intf *intf)
{
    uint32_t ivl;
    uint32_t ivl_target;
    gmpr_instance *instance;

    instance = intf->rintf_instance;

    /* Try the local value first. */

    ivl = intf->rintf_local_query_resp_ivl;

    /*
     * The value must be at least a second less than the query
     * interval.  Adjust it accordingly.
     */
    if (intf->rintf_query_ivl > MSECS_PER_SEC)
	ivl_target = intf->rintf_query_ivl - MSECS_PER_SEC;
    else
	ivl_target = intf->rintf_query_ivl / 2;

    if (ivl > ivl_target)
	ivl = ivl_target;

    /* It also has to be at least the minimum for the protocol. */

    if (ivl < instance->rinst_min_max_resp)
	ivl = instance->rinst_min_max_resp;

    /* Save the updated value. */

    intf->rintf_query_resp_ivl = ivl;

    /* Update dependent variables. */

    gmpr_intf_update_gmi(intf);
    gmpr_intf_update_other_querier_interval(intf);
}


/*
 * gmpr_intf_update_query_ivl
 *
 * Update the query interval variable.
 */
void
gmpr_intf_update_query_ivl (gmpr_intf *intf, uint32_t query_ivl)
{
    /* If the value is zero, use the default value. */

    if (query_ivl == 0)
	query_ivl = GMPR_QUERY_IVL_DEFAULT;

    /* Save the new value. */

    intf->rintf_query_ivl = query_ivl;

    /* Update dependent variables. */

    gmpr_intf_update_query_resp_ivl(intf);
    gmpr_intf_update_gmi(intf);
    gmpr_intf_update_other_querier_interval(intf);
}


/*
 * gmpr_other_querier_present_expiry
 *
 * The other-querier-present timer has expired, which means that we're the
 * querier again.  Send a general query.
 */
static void
gmpr_other_querier_present_expiry (gmpx_timer *timer, void *context)
{
    gmpr_intf *intf;

    gmpx_stop_timer(timer);
    intf = context;

    gmpr_update_querier(intf, &intf->rintf_local_addr, TRUE);
    gmpx_start_timer(intf->rintf_query_timer, 0, 0);
}


/*
 * gmpr_restart_query_timer
 *
 * Restart (or stop) the query timer for an interface.  We stop it if
 * general queries are suppressed, unless the gen_query_requested flag
 * is set.
 */
static void
gmpr_restart_query_timer (gmpr_intf *intf)
{
    gmpr_instance *instance;
    uint32_t ivl;
    int intf_count;
    thread *thread_ptr;

    instance = intf->rintf_instance;

    /*
     * If queries are being suppressed and are not being explicitly
     * requested, stop the timer, clean up, and bail.
     */
    if (intf->rintf_suppress_gen_query && !intf->rintf_gen_query_requested) {
	gmpx_stop_timer(intf->rintf_query_timer);
	thread_remove(&intf->rintf_startup_thread);
	intf->rintf_startup_query_count = 0;
	intf->rintf_first_query_pending = FALSE;
	return;
    }

    /*
     * We need to start the timer.  See if we're sending the first packet
     * in a startup sequence (which generally happens very quickly.)
     */
    if (intf->rintf_first_query_pending) {

	/*
	 * First packet in the sequence.  See if there are a lot
	 * of interfaces all queued up at the same time.
	 */
	intf->rintf_first_query_pending = FALSE;
	intf_count = GMPX_MANY_INTFS + 1;
	FOR_ALL_CIRCULAR_THREAD_ENTRIES(&instance->rinst_startup_intf_thread,
					thread_ptr) {
	    intf_count--;
	    if (intf_count == 0)
		break;
	}

	/*
	 * If there are lots of interfaces starting up
	 * simultaneously, use the standard query interval.  If
	 * not, use a very short interval to speed things up.
	 * Jitter is unnecessary, since we are going to smear the
	 * timers after setting them.
	 */
	if (intf_count == 0) {		/* Too many interfaces */
	    ivl = intf->rintf_query_ivl;
	} else {
	    ivl = GMPR_INIT_FIRST_QUERY_IVL;
	}

    } else if (intf->rintf_startup_query_count) {

	/*
	 * Not the first packet in the sequence.  If we're still part
	 * of the startup sequence, send this one with a fairly short
	 * fixed interval.
	 */
	ivl = GMPR_INIT_LATER_QUERY_IVL;

    } else {

	/* Not part of a startup sequence.  Use the regular interval. */

	ivl = intf->rintf_query_ivl;
    }

    /* Start the timer. */

    gmpx_start_timer(intf->rintf_query_timer, ivl, 0);
}


/*
 * gmpr_setup_initial_query_timer_internal
 *
 * Set up the query timer to send a sequence of queries.  The number
 * of queries in the sequence is provided by the caller.
 *
 * If general queries are being suppressed, this will stop the query
 * timer unless the gen_query_requested flag is set (meaning that the
 * client really really really wants queries sent.)
 *
 * If there aren't too many interfaces starting up simultaneously,
 * start it with a short interval.  If there are lots, start it on its
 * regular interval to avoid bunching.  (The timers will be smeared to
 * smooth things out.)
 */
static void
gmpr_setup_initial_query_timer_internal (gmpr_intf *intf, uint32_t query_count)
{

    gmpr_instance *instance;

    instance = intf->rintf_instance;

    /*
     * Bail if there's already a query sequence being transmitted and
     * the number of pending queries is at least as large as that
     * being requested, as there's no reason to start another.
     */
    if (intf->rintf_startup_query_count >= query_count)
	return;

    /* Set the startup query count and set the "first_query" flag. */

    intf->rintf_startup_query_count = query_count;
    intf->rintf_first_query_pending = TRUE;

    /*
     * Put the interface on the thread of startup interfaces.  This will
     * make us throttle the rate at which we send queries if the interface
     * count is high.
     */
    thread_remove(&intf->rintf_startup_thread);	/* Just in case. */
    thread_circular_add_top(&instance->rinst_startup_intf_thread,
			    &intf->rintf_startup_thread);

    /* Restart (or stop) the query timer. */

    gmpr_restart_query_timer(intf);

    /* Schedule a smearing of the query timers. */

    gmpr_accelerate_query_smear(instance);
}


/*
 * gmpr_setup_initial_query_timer
 *
 * Set up the query timer to send a sequence of queries.  This is done
 * when an interface is first created, or when the client requests it,
 * or when the query interval is changed (and probably other times as
 * well).
 */
void
gmpr_setup_initial_query_timer (gmpr_intf *intf)
{
    gmpr_setup_initial_query_timer_internal(intf, intf->rintf_robustness);
}


/*
 * gmpr_trigger_one_query
 *
 * Trigger the transmission of a single fast query.  We simulate a startup
 * sequence with a single packet, so it will be throttled appropriately if
 * there are lots of interfaces starting up.
 */
void
gmpr_trigger_one_query (gmpr_intf *intf)
{
    gmpr_setup_initial_query_timer_internal(intf, 1);
}

/*
 * gmpr_create_intf
 *
 * Create an interface structure and add it to the instance tree.
 *
 * Returns a pointer to the interface structure, or NULL if no memory.
 */
static gmpr_intf *
gmpr_create_intf (gmpr_instance *instance, gmpx_intf_id intf_id)
{
    gmpr_intf *intf;

    /* Allocate the block. */

    intf = gmpx_malloc_block(gmpr_intf_tag);
    if (!intf)
	return NULL;

    /* Initialize the input group tree. */

    intf->rintf_group_root = 
	gmpx_patroot_init(instance->rinst_addrlen,
			  GMPX_PATRICIA_OFFSET(gmpr_group, rgroup_intf_patnode,
					       rgroup_addr));
    if (!intf->rintf_group_root) { /* No memory */
	gmpx_free_block(gmpr_intf_tag, intf);
	return NULL;
    }

    /* Initialize the output group tree. */

    intf->rintf_oif_group_root = 
	gmpx_patroot_init(instance->rinst_addrlen,
			  GMPX_PATRICIA_OFFSET(gmpr_ogroup,
					       rogroup_intf_patnode,
					       rogroup_addr));
    if (!intf->rintf_oif_group_root) { /* No memory */
	gmpx_patroot_destroy(intf->rintf_group_root);
	gmpx_free_block(gmpr_intf_tag, intf);
	return NULL;
    }

    /* Create the host tree. */

    intf->rintf_host_root = 
	gmpx_patroot_init(instance->rinst_addrlen,
			  GMPX_PATRICIA_OFFSET(gmpr_host, rhost_node,
					       rhost_addr));
    if (!intf->rintf_host_root) { /* No memory */
	gmpx_patroot_destroy(intf->rintf_group_root);
	gmpx_patroot_destroy(intf->rintf_oif_group_root);
	gmpx_free_block(gmpr_intf_tag, intf);
	return NULL;
    }

    /* Initialize the transmit queue. */

    thread_new_circular_thread(&intf->rintf_xmit_head);

    /* Put the interface into the instance tree. */

    intf->rintf_id = intf_id;
    gmpx_assert(gmpx_patricia_add(instance->rinst_intfs,
				  &intf->rintf_inst_patnode));

    /* Put the interface into the global tree. */

    gmpx_assert(
	gmpx_patricia_add(gmpr_global_intf_tree[instance->rinst_proto],
			  &intf->rintf_global_patnode));

    /* Initialize a few more things. */

    intf->rintf_instance = instance;
    intf->rintf_ver = GMP_VERSION_DEFAULT;
    intf->rintf_query_ivl = GMPR_QUERY_IVL_DEFAULT;
    intf->rintf_local_query_ivl = GMPR_QUERY_IVL_DEFAULT;
    intf->rintf_query_resp_ivl = GMPR_QUERY_RESP_IVL_DEFAULT;
    intf->rintf_local_query_resp_ivl = GMPR_QUERY_RESP_IVL_DEFAULT;
    intf->rintf_robustness = GMP_ROBUSTNESS_DEFAULT;
    intf->rintf_local_robustness = GMP_ROBUSTNESS_DEFAULT;
    intf->rintf_lmq_ivl = GMPR_LMQI_DEFAULT;
    intf->rintf_lmq_count = GMP_ROBUSTNESS_DEFAULT;
    intf->rintf_querier = FALSE;
    intf->rintf_querier_enabled = TRUE;

    /* Update the group membership interval. */

    gmpr_intf_update_gmi(intf);

    /* Update the other querier present interval. */

    gmpr_intf_update_other_querier_interval(intf);

    /* Update the Last Member Query Time. */

    gmpr_intf_update_lmqt(intf);

    /* Create the timers. */

    intf->rintf_query_timer =
	gmpx_create_grouped_timer(GMP_TIMER_GROUP_GEN_QUERY,
				  instance->rinst_context,
				  "GMP router general query",
				  gmpr_intf_query_timer_expiry, intf);
    intf->rintf_other_querier_present =
	gmpx_create_timer(instance->rinst_context,
			  "GMP router other querier present",
			  gmpr_other_querier_present_expiry, intf);
    
    /* Initialized to null, will be created if log-interval is configured */

    /* Set up the query timer. */

    gmpr_setup_initial_query_timer(intf);

    return intf;
}


/*
 * gmpr_attach_intf_internal
 *
 * Create an interface entry based on the instance and interface ID.
 *
 * Returns 0 if all OK, -1 if out of memory, or 1 if the interface already
 * exists.
 */
int
gmpr_attach_intf_internal (gmpr_instance *instance, gmpx_intf_id intf_id)
{
    gmpr_intf *intf;

    /* Look up the interface.  Bail if it's already there. */

    intf = gmpr_intf_lookup_global(instance->rinst_proto, intf_id);
    if (intf)
	return 1;			/* Already exists */

    /* Create a new one. */

    intf = gmpr_create_intf(instance, intf_id);
    if (!intf)
	return -1;			/* Out of memory */

    return 0;
}


/*
 * gmpr_detach_intf_internal
 *
 * Get rid of an interface.  Under normal circumstances, the interface will
 * be inactive, but we tolerate unceremonious deletions by cleaning up all
 * of the attached state.
 *
 * Returns 0 if all OK, or 1 if the interface doesn't exist.
 */
int
gmpr_detach_intf_internal (gmpr_instance *instance, gmpx_intf_id intf_id)
{
    gmpr_intf *intf;

    /* Look up the interface.  Bail if it doesn't exist. */

    intf = gmpr_intf_lookup(instance, intf_id);
    if (!intf)
	return 1;			/* Doesn't exist */

    /* Blast all of the groups on the interface. */

    gmpr_destroy_intf_groups(intf);

    /* Blast all of the hosts on the interface. */

    gmpr_destroy_intf_hosts(intf);

    /* Should be clean now.  Destroy the interface. */

    gmpr_destroy_intf(intf);

    return 0;
}

/*
 * gmpr_intf_set_params
 *
 * Set parameters for an interface.
 *
 * Returns 0 if OK, -1 if out of memory, or 1 if the interface doesn't exist.
 */
int
gmpr_intf_set_params (gmpr_instance *instance, gmpx_intf_id intf_id,
		      gmpr_intf_params *params)
{
    gmpr_intf *intf;
    gmpr_group *group;
    boolean version_changed;
    gmp_version new_version;
    uint32_t old_query_ivl;
    uint32_t old_robustness;
    boolean send_query;

    /* Get the interface. */

    intf = gmpr_intf_lookup(instance, intf_id);
    if (!intf)
	return 1;			/* Doesn't exist. */

    /* Set the parameters. */

    new_version = gmp_translate_version(instance->rinst_proto,
					params->gmpr_ifparm_version);
    version_changed = (intf->rintf_ver != new_version);
    intf->rintf_ver = new_version;

    /* If the version changed, update all group versions. */

    if (version_changed) {

	/* Walk all groups on the interface. */

	group = NULL;

	while (TRUE) {
	    group = gmpr_next_intf_group(intf, group);

	    /* Bail if done. */

	    if (!group)
		break;

	    /* Update the compatibility mode. */

	    gmpr_evaluate_group_version(group);
	}
    }

    /* Remember if the suppression parameters are changing. */

    send_query = FALSE;
    if (intf->rintf_suppress_gen_query !=
	    params->gmpr_ifparm_suppress_gen_query) {
	send_query = TRUE;
    }

    /* Set the parameters. */

    old_query_ivl = intf->rintf_local_query_ivl;
    intf->rintf_local_query_ivl = params->gmpr_ifparm_qivl;
    intf->rintf_local_query_resp_ivl = params->gmpr_ifparm_qrivl;
    intf->rintf_lmq_ivl = params->gmpr_ifparm_lmqi;
    intf->rintf_fast_leaves = params->gmpr_ifparm_fast_leave;
    intf->rintf_channel_limit = params->gmpr_ifparm_chan_limit;
    
    if(intf->rintf_channel_count > intf->rintf_channel_limit)
        intf->rintf_limit_state = GMPR_ABOVE_LIMIT;
    else
	intf->rintf_limit_state = GMPR_BELOW_THRESHOLD;

    /* threshold and log-interval can be configured only
       when channel_limit is configured */

    if(intf->rintf_channel_limit) {
        intf->rintf_channel_threshold = params->gmpr_ifparm_chan_threshold;
        intf->rintf_log_interval = params->gmpr_ifparm_log_interval;
	if(intf->rintf_channel_count > intf->rintf_channel_threshold)
	    intf->rintf_limit_state = GMPR_ABOVE_THRESHOLD_BELOW_LIMIT;
        else
	    intf->rintf_limit_state = GMPR_BELOW_THRESHOLD;
    } else {
	/* Threshold and log interval does not have a significance without limit */
	intf->rintf_channel_threshold = 0 ;
	intf->rintf_log_interval = 0;
    }
    
    intf->rintf_passive_receive = params->gmpr_ifparm_passive_receive;
    intf->rintf_suppress_gen_query = params->gmpr_ifparm_suppress_gen_query;
    intf->rintf_suppress_gs_query = params->gmpr_ifparm_suppress_gs_query;
    intf->rintf_local_robustness = params->gmpr_ifparm_robustness;

    /* If we're doing fast leaves, force host tracking. */

    if (intf->rintf_fast_leaves)
	instance->rinst_host_tracking = TRUE;

    /* Update dependent parameters. */

    old_robustness = intf->rintf_robustness;
    gmpr_intf_update_robustness(intf, params->gmpr_ifparm_robustness);
    gmpr_intf_update_query_ivl(intf, params->gmpr_ifparm_qivl);

    /* Restart the query timer if the interval changed. */

    if (old_query_ivl != intf->rintf_local_query_ivl)
	send_query = TRUE;

    /*
     * If the channel limit is now less than the number of channels on the
     * interface, blow away the interface state and issue a query to rebuild
     * it.
     */
    if (intf->rintf_channel_limit &&
	intf->rintf_channel_count > intf->rintf_channel_limit) {

	/* Walk all groups on the interface. */
        intf->rintf_limit_state = GMPR_ABOVE_LIMIT;
	group = NULL;

	while (TRUE) {
	    group = gmpr_next_intf_group(intf, group);

	    /* Bail if done. */

	    if (!group)
		break;

	    /* Make the group go away. */

	    gmpr_timeout_group(group);
	}
	send_query = TRUE;
    } else if(intf->rintf_channel_limit &&
	      intf->rintf_channel_count > intf->rintf_channel_threshold *
					   intf->rintf_channel_limit/100 ) {
	intf->rintf_limit_state = GMPR_ABOVE_THRESHOLD_BELOW_LIMIT;
    } else {
	intf->rintf_limit_state = GMPR_BELOW_THRESHOLD;
    }
   
    /*
     * If queries are suppressed, zap the startup query counter,
     * since it isn't relevant and may get in the way later.
     */
    if (intf->rintf_suppress_gen_query)
	intf->rintf_startup_query_count = 0;

    /*
     * Kick the query timer if necessary (which may stop it.)
     * If the robustness variable changed, send a whole query sequence
     * instead.
     */
    if (old_robustness != intf->rintf_robustness) {
	gmpr_setup_initial_query_timer(intf);
    } else if (send_query) {
	gmpr_trigger_one_query(intf);
    }

    return 0;
}


/*
 * gmpr_intf_query_timer_expiry
 *
 * The interface general query timer has expired.
 */
static void
gmpr_intf_query_timer_expiry (gmpx_timer *timer GMPX_UNUSED, void *context)
{
    gmpr_intf *intf;

    intf = context;

    /*
     * Set the flag saying that we need to send a query, and kick the
     * transmitter.
     */
    intf->rintf_send_gen_query = TRUE;
    gmpr_kick_xmit(intf);

    /* If we're in an initial query sequence, drop the query count. */

    if (intf->rintf_startup_query_count)
	intf->rintf_startup_query_count--;

    /*
     * If the startup query count is zero, clear the "query requested"
     * flag, as any requested query sequence is complete.  Likewise,
     * take the interface off of the startup thread.
     */
    if (!intf->rintf_startup_query_count) {
	intf->rintf_gen_query_requested = FALSE;
	thread_remove(&intf->rintf_startup_thread);
    }
    gmpr_restart_query_timer(intf);
}


/*
 * gmpr_kick_xmit
 *
 * Kick the transmission state machinery for an interface.
 */
void
gmpr_kick_xmit (gmpr_intf *intf)
{
    /*
     * Do it only if we don't have a pending transmission and the
     * interface is up.
     */

    if (!intf->rintf_xmit_pending && intf->rintf_up) {

	/* Set the flag and kick the I/O. */

	intf->rintf_xmit_pending = TRUE;
	gmpp_start_xmit(GMP_ROLE_ROUTER, intf->rintf_instance->rinst_proto,
			intf->rintf_id);
    }
}


/*
 * gmpr_update_querier
 *
 * Update querier status on this interface.  If it's changing, post a
 * notification if enabled.
 *
 * The address parameter will be NULL if we're the querier.
 */
void
gmpr_update_querier (gmpr_intf *intf, gmp_addr_string *addr, boolean querier)
{
    gmpr_instance *instance;
    gmpr_client *client;
    thread *thread_ptr;

    instance = intf->rintf_instance;

    /* Bail if nothing has changed. */

    if (querier == intf->rintf_querier &&
        (!addr || !memcmp(addr->gmp_addr, intf->rintf_querier_addr.gmp_addr,
            instance->rinst_addrlen))) {
	return;
    }

    /* Update the status. */

    intf->rintf_querier = querier;
    memmove(intf->rintf_querier_addr.gmp_addr, addr->gmp_addr,
        instance->rinst_addrlen);

    /*
     * If we're becoming querier, update the query interval to the
     * configured value.
     */
    if (querier)
	gmpr_intf_update_query_ivl(intf, intf->rintf_local_query_ivl);

    /* Call the callback for any client, if present. */

    FOR_ALL_CIRCULAR_THREAD_ENTRIES(&instance->rinst_client_thread,
				    thread_ptr) {
	client = gmpr_thread_to_client(thread_ptr);
	if (client->rclient_cb_context.rctx_querier_cb)
	    (*client->rclient_cb_context.rctx_querier_cb)(
					  instance->rinst_context,
					  intf->rintf_id,
					  intf->rintf_querier,
					  intf->rintf_querier_addr.gmp_addr);
    }
}
