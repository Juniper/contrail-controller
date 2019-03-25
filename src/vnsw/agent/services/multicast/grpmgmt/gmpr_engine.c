/* $Id: gmpr_engine.c 514187 2012-05-06 12:25:25Z ib-builder $
 *
 * gmpr_engine.c - IGMP/MLD Router-Side generic protocol engine
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 * This module contains the protocol engine for router-side GMP.
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_externs.h"
#include "gmp_private.h"
#include "gmp_router.h"
#include "gmpr_private.h"
#include "gmpr_trace.h"


/*
 * gmpr_process_query_packet
 *
 * Process a received query packet.
 */
static void
gmpr_process_query_packet(gmpr_intf *intf, gmp_packet *packet)
{
    gmpr_instance *instance;
    gmp_query_packet *query_pkt;
    gmpr_group *group;
    gmp_addr_thread_entry *addr_thread_entry;
    gmp_addr_string *source_addr;
    gmp_addr_cat_entry *cat_entry;
    gmp_addr_list_entry *addr_entry;
    gmpr_group_addr_entry *group_addr;
    int addr_compare;

    /* Bail if this is IGMPv1;  we ignore queries in this case. */

    if (intf->rintf_ver == GMP_VERSION_BASIC)
	return;

    instance = intf->rintf_instance;
    query_pkt = &packet->gmp_packet_contents.gmp_packet_query;

    /* Post a warning if the version does not match. */

    if (intf->rintf_ver != packet->gmp_packet_version ) {
	gmpr_post_event(instance, GMP_VERSION_MISMATCH, intf->rintf_id, 
			gmp_untranslate_version(instance->rinst_proto,
						intf->rintf_ver),
			gmp_untranslate_version(instance->rinst_proto,
						packet->gmp_packet_version));
    }

    /*
     * If the source of the query is all zeroes, ignore it.  It's a snooping
     * switch trying to elicit state from hosts, and is not the elected
     * querier.
     */
    if (gmp_addr_is_zero(&packet->gmp_packet_src_addr,
			 instance->rinst_addrlen)) {
	return;
    }

    /*
     * The source has a real address.  If we have one too, do the election.
     * If we don't have an address, it means that we're a snooping switch
     * and don't do our own queries, so we elect the other guy as querier.
     */
    if (!gmp_addr_is_zero(&intf->rintf_local_addr, instance->rinst_addrlen)) {

	/* Compare addresses with the querier. */

	addr_compare = memcmp(packet->gmp_packet_src_addr.gmp_addr,
			      intf->rintf_local_addr.gmp_addr,
			      instance->rinst_addrlen);

	/* If the querier has a higher address, or is us, bail. */

	if (addr_compare >= 0)
	    return;
    }

    /* Update the querier status. */

    gmpr_update_querier(intf, &packet->gmp_packet_src_addr, FALSE);

    /* Update the robustness variable. */

    gmpr_intf_update_robustness(intf, query_pkt->gmp_query_qrv);

    /* Update the query interval if this packet carries QQI. */

    if (packet->gmp_packet_version == GMP_VERSION_SOURCES)
	gmpr_intf_update_query_ivl(intf, query_pkt->gmp_query_qqi);

    /* Start the other-querier timer now that we've updated the intervals. */

    gmpx_start_timer(intf->rintf_other_querier_present,
		     intf->rintf_other_querier_ivl, 0);

    /*
     * If the suppress-router-side-processing flag is clear and a
     * group is present, take a look at the rest of the packet.
     */
    if (!query_pkt->gmp_query_suppress && query_pkt->gmp_query_group_query) {

	/* Look up the group. */

	group = gmpr_group_lookup(intf, query_pkt->gmp_query_group.gmp_addr);
	if (group) {

	    /* Got a group.  See if this is a GSS query. */

	    if (query_pkt->gmp_query_rcv_srcs) {

		/*
		 * GSS query.  Walk the source address list, updating
		 * the source timers.
		 */
		addr_thread_entry = NULL;
		while (TRUE) {
		    source_addr = gmp_next_addr_thread_addr(
					    query_pkt->gmp_query_rcv_srcs,
					    &addr_thread_entry);
		    if (!source_addr)
			break;

		    /* Got a source address.  Look up the catalog entry. */

		    cat_entry =
			gmp_lookup_addr_cat_entry(&instance->rinst_addr_cat,
						  source_addr->gmp_addr);

		    /*
		     * If there's a catalog entry, look up the source
		     * on the running-timer list.
		     */
		    if (cat_entry) {
			addr_entry = gmp_lookup_addr_entry(
					   &group->rgroup_src_addr_running,
					   cat_entry->adcat_ent_ord);

			/* If the entry is found, update the timer. */

			if (addr_entry) {
			    group_addr =
				gmpr_addr_entry_to_group_entry(addr_entry);
			    gmpx_start_timer(group_addr->rgroup_addr_timer,
					     intf->rintf_lmqt, 0);
			}
		    }
		}

	    } else {

		/* Group-specific only.  Update the group timer. */

		gmpx_start_timer(group->rgroup_group_timer, intf->rintf_lmqt,
				 0);
	    }
	}
    }
}


/*
 * gmpr_enqueue_group_query
 *
 * Enqueues a group query.
 */
static void
gmpr_enqueue_group_query (gmpr_group *group)
{
    gmpr_intf *intf;

    intf = group->rgroup_intf;

    /* Bail if this version doesn't support group queries. */

    if (group->rgroup_compatibility_mode < GMP_VERSION_LEAVES)
	return;

    /* Bail if we're doing fast leaves on this interface. */

    if (intf->rintf_fast_leaves)
	return;

    /* Lower the group timer to LMQT. */

    if (!gmpx_timer_running(group->rgroup_group_timer) || 
	(gmpx_timer_time_remaining(group->rgroup_group_timer) >
	 intf->rintf_lmqt)) {
	gmpx_start_timer(group->rgroup_group_timer, intf->rintf_lmqt, 0);
    }

    /* Bail if we're suppressing GS/GSS queries on this interface. */

    if (intf->rintf_suppress_gs_query)
	return;

    /* Set the retransmission count. */

    group->rgroup_query_rexmit_count = intf->rintf_lmq_count;

    /* Kick the query timer. */

    gmpx_start_timer(group->rgroup_query_timer, 0, 0);
}


/*
 * gmpr_create_running_list_entry
 *
 * Create a new entry for the running-timer list and create a timer
 * for it.
 *
 * Returns a pointer to the new entry, or NULL if out of memory or the
 * channel limit was hit.
 */
static gmpr_group_addr_entry *
gmpr_create_running_list_entry (gmpr_group *group, bv_bitnum_t bitnum)
{
    gmp_addr_list_entry *addr_entry;

    /* Enqueue a new entry. */

    addr_entry = gmp_create_addr_list_entry(&group->rgroup_src_addr_running,
					    bitnum);
    return gmpr_addr_entry_to_group_entry(addr_entry);
}


/*
 * gmpr_copy_reporter
 *
 * Copy the last reporter address from the group to a source.  Whenever
 * a group is mentioned in a report, the reporter address is written into
 * the group entry.  When set operations result, they are peppered with calls
 * to this routine to put the reporter's address into the source.  The group
 * happens to be a convenient place that we know was just updated.
 */
static void
gmpr_copy_reporter (gmpr_group *group,
		    gmpr_group_addr_entry *group_addr_entry)
{
    memmove(group_addr_entry->rgroup_addr_last_reporter.gmp_addr,
        group->rgroup_last_reporter.gmp_addr,
        group->rgroup_intf->rintf_instance->rinst_addrlen);
}


/*
 * gmpr_move_include_cb
 *
 * Vector walk callback to move entries to the include list.  We look
 * up the address in the stopped-timer list, and move it to the
 * running-timer list if it's there.  If the entry is in the
 * running-timer list, we update the timer.  If it's not there, we add
 * it.
 */
static boolean
gmpr_move_include_cb (void *context, bv_bitnum_t bitnum,
		      boolean new_val GMPX_UNUSED,
		      boolean old_val GMPX_UNUSED)
{
    gmpr_group *group;
    gmp_addr_list_entry *addr_entry;
    gmpr_group_addr_entry *group_addr_entry;

    group = context;

    /* See if the address is present on the stopped-timer list. */

    if (gmp_addr_in_list(&group->rgroup_src_addr_stopped, bitnum)) {

	/* Entry is in the stopped list list.  Look it up. */

	addr_entry = gmp_lookup_addr_entry(&group->rgroup_src_addr_stopped,
					   bitnum);
	gmpx_assert(addr_entry);

	/* Move it to the running list. */

	gmp_move_addr_list_entry(&group->rgroup_src_addr_running, addr_entry);

	/* Update the OIF, as the source is no longer excluded. */

	group_addr_entry = gmpr_addr_entry_to_group_entry(addr_entry);
	gmpr_update_source_oif(group_addr_entry, OIF_DELETE);

    } else {

	/*
	 * Not in the stopped-timer list.  See if the address is
	 * present in the running-timer list.  If it is, look it up.
	 * If not, allocate a new entry and put it into the list.
	 */
	if (!gmp_addr_in_list(&group->rgroup_src_addr_running, bitnum)) {

	    /* Not in the list.  Allocate a new one and put it in there. */

	    group_addr_entry = gmpr_create_running_list_entry(group, bitnum);
	    if (!group_addr_entry)
		return FALSE;		/* Out of memory or limit hit */

	} else {

	    /* Entry is in the list.  Look it up. */

	    addr_entry = gmp_lookup_addr_entry(&group->rgroup_src_addr_running,
					       bitnum);
	    gmpx_assert(addr_entry);
	    group_addr_entry = gmpr_addr_entry_to_group_entry(addr_entry);
	}
    }

    /* Bump the timer. */

    gmpx_start_timer(group_addr_entry->rgroup_addr_timer,
		     group->rgroup_intf->rintf_group_membership_ivl, 0);

    /* Copy the reporter address from the group. */

    gmpr_copy_reporter(group, group_addr_entry);

    return FALSE;
}


/*
 * gmpr_move_include
 *
 * Move the source list from the stopped-timer to the running-timer
 * list, creating new entries for any sources not present on either list.
 *
 * Bumps the source timer of each source listed in the record.
 */
static void
gmpr_move_include (gmpr_group *group, gmp_addr_vect *source_vect)
{
    /* Walk the vector.  The callback does all the work. */

    gmp_addr_vect_walk(source_vect, gmpr_move_include_cb, group);
}


/*
 * gmpr_delete_include_cb
 *
 * Vector walk for culling the running-timer list.  We delete any
 * entry that we're passed.
 */
static boolean
gmpr_delete_include_cb (void *context, bv_bitnum_t bitnum,
			boolean new_val GMPX_UNUSED,
			boolean old_val GMPX_UNUSED)
{
    gmpr_group *group;
    gmp_addr_list_entry *addr_entry;

    group = context;

    /* Look up the entry. */

    addr_entry = gmp_lookup_addr_entry(&group->rgroup_src_addr_running,
				       bitnum);
    gmpx_assert(addr_entry);

    /* Delete it.  The callback will free the timer. */

    gmp_delete_addr_list_entry(addr_entry);

    return FALSE;
}


/*
 * gmpr_delete_exclude_cb
 *
 * Vector walk for culling the stopped-timer list.  We move any
 * entry we're passed to the deleted list and notify the clients,
 * since the source is no longer being excluded.
 */
static boolean
gmpr_delete_exclude_cb (void *context, bv_bitnum_t bitnum,
			boolean new_val GMPX_UNUSED,
			boolean old_val GMPX_UNUSED)
{
    gmpr_group *group;
    gmp_addr_list_entry *addr_entry;
    gmpr_group_addr_entry *group_addr;

    group = context;

    /* Look up the entry. */

    addr_entry = gmp_lookup_addr_entry(&group->rgroup_src_addr_stopped,
				       bitnum);
    gmpx_assert(addr_entry);

    /* Delink any OIF entry. */

    group_addr = gmpr_addr_entry_to_group_entry(addr_entry);
    gmpr_update_source_oif(group_addr, OIF_DELETE);

    /* Delete the source. */

    gmp_delete_addr_list_entry(addr_entry);

    return FALSE;
}


/*
 * gmpr_add_exclude_cb
 *
 * Vector walk callback for adding a source to the exclude
 * (stopped-timer) list.  We assume that this is a new source.
 */
static boolean
gmpr_add_exclude_cb (void *context, bv_bitnum_t bitnum,
		     boolean new_val GMPX_UNUSED,
		     boolean old_val GMPX_UNUSED)
{
    gmpr_group *group;
    gmp_addr_list_entry *addr_entry;
    gmpr_group_addr_entry *group_addr;

    group = context;

    /* Allocate a new entry and put it in the stopped list. */

    addr_entry = gmp_create_addr_list_entry(&group->rgroup_src_addr_stopped,
					    bitnum);
    if (!addr_entry)
	return FALSE;			/* No memory or limit hit */

    /* Copy the reporter address from the group. */

    group_addr = gmpr_addr_entry_to_group_entry(addr_entry);
    gmpr_copy_reporter(group, group_addr);

    return FALSE;
}


/*
 * gmpr_add_include_cb
 *
 * Vector walk callback for adding a set of sources to the include
 * list.  We look up the address in the timer-running list, add it if
 * it's not there, and bump up the timer.
 */
static boolean
gmpr_add_include_cb (void *context, bv_bitnum_t bitnum,
		     boolean new_val GMPX_UNUSED,
		     boolean old_val GMPX_UNUSED)
{
    gmpr_group *group;
    gmp_addr_list_entry *addr_entry;
    gmpr_group_addr_entry *group_addr_entry;

    group = context;

    /*
     * See if the address is present in the running-timer list.  If it is,
     * look it up.  If not, allocate a new entry and put it into the list.
     */
    if (!gmp_addr_in_list(&group->rgroup_src_addr_running, bitnum)) {

	/* Not in the list.  Allocate a new one and put it in there. */

	group_addr_entry = gmpr_create_running_list_entry(group, bitnum);
	if (!group_addr_entry)
	    return FALSE;		/* Out of memory or limit hit */
	addr_entry = &group_addr_entry->rgroup_addr_entry;

	/* Update the OIF. */

	gmpr_update_source_oif(group_addr_entry, OIF_UPDATE);

    } else {

	/* Entry is in the list.  Look it up. */

	addr_entry = gmp_lookup_addr_entry(&group->rgroup_src_addr_running,
					   bitnum);
	gmpx_assert(addr_entry);
    }

    group_addr_entry = gmpr_addr_entry_to_group_entry(addr_entry);

    /* Bump the timer. */

    gmpx_start_timer(group_addr_entry->rgroup_addr_timer,
		     group->rgroup_intf->rintf_group_membership_ivl, 0);

    /* Copy the reporter address from the group. */

    gmpr_copy_reporter(group, group_addr_entry);

    return FALSE;
}


/*
 * gmpr_enqueue_gss_query
 *
 * Do the work to set up a GSS query for a (group,source) pair.
 */
static void
gmpr_enqueue_gss_query (gmpr_group *group,
			gmpr_group_addr_entry *group_addr_entry)
{
    gmpr_intf *intf;

    intf = group->rgroup_intf;

    /* Drop the source timer if appropriate. */

    if (!gmpx_timer_running(group_addr_entry->rgroup_addr_timer) ||
	(gmpx_timer_time_remaining(group_addr_entry->rgroup_addr_timer) >
				  intf->rintf_lmqt)) {
	gmpx_start_timer(group_addr_entry->rgroup_addr_timer,
			 intf->rintf_lmqt, 0);
    }

    /* Bail if the current version doesn't support GSS queries. */

    if (group->rgroup_compatibility_mode < GMP_VERSION_SOURCES)
	return;

    /* Bail if we're doing fast leaves on this interface. */

    if (intf->rintf_fast_leaves)
	return;

    /* Bail if we're suppressing GS and GSS queries. */

    if (intf->rintf_suppress_gs_query)
	return;

    /* Bump the rexmit count back up. */

    group_addr_entry->rgroup_addr_rexmit_count = intf->rintf_lmq_count;

    /* Set the GSS timer to expire immediately if it's not already running. */

    if (!gmpx_timer_running(group->rgroup_gss_query_timer))
	gmpx_start_timer(group->rgroup_gss_query_timer, 0, 0);
}


/*
 * gmpr_send_gss_query_cb
 *
 * Vector callback to send a group-and-source specific query.
 */
static boolean
gmpr_send_gss_query_cb (void *context, bv_bitnum_t bitnum,
			boolean new_val GMPX_UNUSED,
			boolean old_val GMPX_UNUSED)
{
    gmpr_group *group;
    gmp_addr_list_entry *addr_entry;
    gmpr_group_addr_entry *group_addr_entry;
    gmpr_intf *intf;

    group = context;
    intf = group->rgroup_intf;

    /* Look up the entry in the running-timer list.  It better be there. */

    addr_entry = gmp_lookup_addr_entry(&group->rgroup_src_addr_running,
				       bitnum);
    gmpx_assert(addr_entry);
    group_addr_entry = gmpr_addr_entry_to_group_entry(addr_entry);

    /* Process the source if the source timer is greater than LMQT. */

    if (gmpx_timer_time_remaining(group_addr_entry->rgroup_addr_timer) >
	intf->rintf_lmqt) {

	/* Enqueue the source for the GSS query. */

	gmpr_enqueue_gss_query(group, group_addr_entry);
    }

    /* Copy the reporter address from the group. */

    gmpr_copy_reporter(group, group_addr_entry);

    return FALSE;
}


/*
 * gmpr_process_state_chg_ex_in
 *
 * Process a state-change record of TO_IN type with a filter state of
 * Exclude.
 *
 * We do a bunch of set math.
 */
static void
gmpr_process_state_chg_ex_in (gmpr_group *group, gmp_addr_vect *source_vect)
{
    /*
     * First, form the set (X-A) and send a query out on each member.
     * These are all of the addresses on the running-timer list that
     * were not mentioned in the record.
     */
    if (gmp_addr_vect_minus(&group->rgroup_src_addr_running.addr_vect,
			    source_vect, NULL, gmpr_send_gss_query_cb, group,
			    BV_CALL_SET) < 0)
	return;				/* Out of memory */

    /*
     * Now walk the source list, moving any matching entry from the
     * stopped-timer list to the running-timer list, and creating
     * new entries for anything not found in either list.
     */
    gmp_addr_vect_walk(source_vect, gmpr_move_include_cb, group);

    /* Finally, enqueue a group-specific query. */

    gmpr_enqueue_group_query(group);
}


/*
 * gmpr_chg_ex_ex_cb
 *
 * Vector walk to determine new running-timer entries for the
 * Exclude/Exclude state-change case.  We are given an entry on the
 * new source list, and add it to the running-timer list if it is not
 * found in either the running-timer or stopped-timer lists.
 *
 * This does not trigger a state change, since the source will still be
 * received whether the entry is in the running-timer list or it is not
 * in any list.
 */
static boolean
gmpr_chg_ex_ex_cb (void *context, bv_bitnum_t bitnum,
		   boolean new_val GMPX_UNUSED, boolean old_val GMPX_UNUSED)
{
    gmpr_group *group;
    gmpr_group_addr_entry *group_addr_entry;

    group = context;

    /* See if the address is present in the running-timer list. */

    if (!gmp_addr_in_list(&group->rgroup_src_addr_running, bitnum)) {

	/*
	 * Not in the running-timer list.  See if it's in the
	 * stopped-timer list.
	 */
	if (!gmp_addr_in_list(&group->rgroup_src_addr_stopped, bitnum)) {

	    /*
	     * Not in either list.  Allocate a new one and put it in
	     * the running-timer list.
	     */
	    group_addr_entry = gmpr_create_running_list_entry(group, bitnum);
	    if (!group_addr_entry)
		return FALSE;		/* Out of memory or limit hit */
	    gmpx_start_timer(group_addr_entry->rgroup_addr_timer,
		     gmpx_timer_time_remaining(group->rgroup_group_timer), 0);

	    /* Copy the reporter address from the group. */

	    gmpr_copy_reporter(group, group_addr_entry);
	}
    }

    return FALSE;
}


/*
 * gmpr_process_state_chg_ex_ex
 *
 * Process a state-change record of TO_EX type with a filter state of
 * Exclude.
 *
 * We do a bunch of set math.
 */
static void
gmpr_process_state_chg_ex_ex (gmpr_group *group, gmp_addr_vect *source_vect)
{
    /*
     * OK, this one is really nutty.  By the spec, set A is the set of
     * addresses in the new report, X is the set in the running-timer
     * list, and Y is the set in the stopped-timer list.  By definition,
     * X and Y are non-overlapping.
     *
     * First, we delete X-A.  This eliminates all addresses from the
     * running-timer list, except for (A*X).
     */
    if (gmp_addr_vect_minus(&group->rgroup_src_addr_running.addr_vect,
			    source_vect, NULL, gmpr_delete_include_cb, group,
			    BV_CALL_SET) < 0)
	return;				/* Out of memory */

    /*
     * Now, delete Y-A.  This eliminates all addresses from the stopped-timer
     * list, except for (A*Y).
     */
    if (gmp_addr_vect_minus(&group->rgroup_src_addr_stopped.addr_vect,
			    source_vect, NULL, gmpr_delete_exclude_cb, group,
			    BV_CALL_SET) < 0)
	return;				/* Out of memory */

    /*
     * Next, walk A, adding to the running list any address not found
     * on what remains of the running and stopped lists.  This forms the
     * set (A-X-Y).  The net result is that the running list contains
     * (A-X-Y) + (A*X), which is the same as (A-Y).  Whew.
     */
    gmp_addr_vect_walk(source_vect, gmpr_chg_ex_ex_cb, group);

    /* Now send a query on everything on the running list, which is (A-Y). */

    gmp_addr_vect_walk(&group->rgroup_src_addr_running.addr_vect,
		       gmpr_send_gss_query_cb, group);

    /* Finally, restart the group timer. */

    gmpx_start_timer(group->rgroup_group_timer,
		     group->rgroup_intf->rintf_group_membership_ivl, 0);
}


/*
 * gmpr_state_chg_ex_block_cb
 *
 * Vector callback for the receipt of a BLOCK record in Exclude state.
 *
 * We're called for each source in the BLOCK list that is not in the
 * stopped-timer list.
 */
static boolean
gmpr_state_chg_ex_block_cb (void *context, bv_bitnum_t bitnum,
			    boolean new_val GMPX_UNUSED,
			    boolean old_val GMPX_UNUSED)
{
    gmpr_group *group;
    gmp_addr_list_entry *addr_entry;
    gmpr_group_addr_entry *group_addr_entry;
    gmpr_intf *intf;

    group = context;
    intf = group->rgroup_intf;

    /* If the entry is not in the running-timer list, create it. */

    if (!gmp_addr_in_list(&group->rgroup_src_addr_running, bitnum)) {

	/*
	 * Not in the list.  Allocate a new one and put it in the
	 * running-timer list.
	 */
	group_addr_entry = gmpr_create_running_list_entry(group, bitnum);
	if (!group_addr_entry)
	    return FALSE;		/* Out of memory or limit hit */
	addr_entry = &group_addr_entry->rgroup_addr_entry;
	gmpx_start_timer(group_addr_entry->rgroup_addr_timer,
			 gmpx_timer_time_remaining(group->rgroup_group_timer),
			 0);

    } else {

	/* Look up the entry in the running-timer list.  It better be there. */

	addr_entry = gmp_lookup_addr_entry(&group->rgroup_src_addr_running,
					   bitnum);
	gmpx_assert(addr_entry);
	group_addr_entry = gmpr_addr_entry_to_group_entry(addr_entry);
    }

    /* Send a query if the source if the source timer is greater than LMQT. */

    if (gmpx_timer_time_remaining(group_addr_entry->rgroup_addr_timer) >
	intf->rintf_lmqt) {

	/* Enqueue the source for the GSS query. */

	gmpr_enqueue_gss_query(group, group_addr_entry);
    }

    /* Copy the reporter address from the group. */

    gmpr_copy_reporter(group, group_addr_entry);

    return FALSE;
}


/*
 * gmpr_process_state_chg_ex_block
 *
 * Process a state-change record of type BLOCK with a filter state of
 * Exclude.
 */
static void
gmpr_process_state_chg_ex_block (gmpr_group *group, gmp_addr_vect *source_vect)
{
    /*
     * Form the set (A-Y) (all addresses mentioned in the BLOCK that are
     * not in the stopped-timer list.)  Add these to the running-timer list,
     * setting the timers on those not already in that list.  Also, send
     * a query on each of them.
     */
    if (gmp_addr_vect_minus(source_vect,
			    &group->rgroup_src_addr_stopped.addr_vect,
			    NULL, gmpr_state_chg_ex_block_cb, group,
			    BV_CALL_SET) < 0)
	return;				/* Out of memory */
}


/*
 * gmpr_process_state_chg_in_ex
 *
 * Process a state-change record of type TO_EX with a filter state of
 * Include.
 */
static void
gmpr_process_state_chg_in_ex (gmpr_group *group, gmp_addr_vect *source_vect)
{
    gmpx_assert(gmp_addr_list_empty(&group->rgroup_src_addr_stopped));

    /* Change the filter mode. */

    group->rgroup_filter_mode = GMP_FILTER_MODE_EXCLUDE;

    /*
     * Form the set (B-A) (all newly-mentioned sources) and stick them
     * straight into the stopped-timer list.
     */
    if (gmp_addr_vect_minus(source_vect,
			    &group->rgroup_src_addr_running.addr_vect, NULL,
			    gmpr_add_exclude_cb, group, BV_CALL_SET) < 0)
	return;				/* Out of memory */

    /*
     * Form the set (A-B) (all addresses mentioned in the Include set
     * that are not in the TO_IN set) and delete them.
     */
    if (gmp_addr_vect_minus(&group->rgroup_src_addr_running.addr_vect,
			    source_vect, NULL, gmpr_delete_include_cb, group,
			    BV_CALL_SET) < 0)
	return;				/* Out of memory */

    /*
     * Send a query for (A*B), which is what's left on the running-timer
     * list.
     */
    gmp_addr_vect_walk(&group->rgroup_src_addr_running.addr_vect,
		       gmpr_send_gss_query_cb, group);

    /* Restart the group timer. */

    gmpx_start_timer(group->rgroup_group_timer,
		     group->rgroup_intf->rintf_group_membership_ivl, 0);

    /* Update the OIF. */

    gmpr_update_oif_mode_change(group);
}


/*
 * gmpr_process_state_chg_in_in
 *
 * Process a state-change record of type TO_IN with a filter state of
 * Include.
 */
static void
gmpr_process_state_chg_in_in (gmpr_group *group, gmp_addr_vect *source_vect)
{
    gmpx_assert(gmp_addr_list_empty(&group->rgroup_src_addr_stopped));

    /* Add any new sources to the Include (running-timer) list. */

    gmp_addr_vect_walk(source_vect, gmpr_add_include_cb, group);

    /*
     * Form the set (A-B) (all addresses mentioned in the Include set
     * that are not in the TO_IN set) and send a Query on them.
     */
    if (gmp_addr_vect_minus(&group->rgroup_src_addr_running.addr_vect,
			    source_vect, NULL, gmpr_send_gss_query_cb, group,
			    BV_CALL_SET) < 0)
	return;				/* Out of memory */
}


/*
 * gmpr_process_state_chg_in_block
 *
 * Process a state-change record of type BLOCK with a filter state of
 * Include.
 */
static void
gmpr_process_state_chg_in_block (gmpr_group *group, gmp_addr_vect *source_vect)
{
    /*
     * Form the set (A*B) (all addresses mentioned in the BLOCK that are
     * currently in the Include set) and send a Query on them.
     */
    gmpx_assert(gmp_addr_list_empty(&group->rgroup_src_addr_stopped));
    if (gmp_addr_vect_inter(&group->rgroup_src_addr_running.addr_vect,
			    source_vect, NULL, gmpr_send_gss_query_cb, group,
			    BV_CALL_SET) < 0)
	return;				/* Out of memory */
}


/*
 * gmpr_add_include
 *
 * Process a current-state record of type IS_IN or a state-change
 * record of ALLOW with a filter state of Include.
 *
 * Update the timer-running list to include any new sources in the
 * record, and bump the source timer of each source listed in the
 * record.
 */
static void
gmpr_add_include (gmpr_group *group, gmp_addr_vect *source_vect)
{
    /* Walk the vector.  The callback does all the work. */

    gmpx_assert(gmp_addr_list_empty(&group->rgroup_src_addr_stopped));
    gmp_addr_vect_walk(source_vect, gmpr_add_include_cb, group);
}


/*
 * gmpr_process_state_chg_rcrd
 *
 * Process a state-change record.
 */
static void
gmpr_process_state_chg_rcrd (gmpr_group *group,
			     gmp_report_rectype rec_type,
			     gmp_addr_vect *source_vect)
{
    /*
     * Process based on our current filter state, combined with the
     * received record type.
     */
    switch (group->rgroup_filter_mode) {
      case GMP_FILTER_MODE_INCLUDE:
	switch (rec_type) {
	  case GMP_RPT_ALLOW:
	    gmpr_add_include(group, source_vect);
	    break;

	  case GMP_RPT_BLOCK:
	    gmpr_process_state_chg_in_block(group, source_vect);
	    break;

	  case GMP_RPT_TO_IN:
	    gmpr_process_state_chg_in_in(group, source_vect);
	    break;

	  case GMP_RPT_TO_EX:
	    gmpr_process_state_chg_in_ex(group, source_vect);
	    break;

	  default:
	    gmpx_assert(FALSE);
	    break;
	}
	break;

      case GMP_FILTER_MODE_EXCLUDE:
	switch (rec_type) {
	  case GMP_RPT_ALLOW:
	    gmpr_move_include(group, source_vect);
	    break;

	  case GMP_RPT_BLOCK:
	    gmpr_process_state_chg_ex_block(group, source_vect);
	    break;

	  case GMP_RPT_TO_IN:
	    gmpr_process_state_chg_ex_in(group, source_vect);
	    break;

	  case GMP_RPT_TO_EX:
	    gmpr_process_state_chg_ex_ex(group, source_vect);
	    break;

	  default:
	    gmpx_assert(FALSE);
	    break;
	}
	break;

      default:
	gmpx_assert(FALSE);
    }
}


/*
 * gmpr_ex_ex_new_cb
 *
 * Vector walk to determine new running-timer entries for the
 * Exclude/Exclude case.  We are given an entry on the new source
 * list, and add it to the running-timer list if it is not found in
 * either the running-timer or stopped-timer lists.
 *
 * This does not trigger a state change, since the source will still be
 * received whether the entry is in the running-timer list or it is not
 * in any list.
 */
static boolean
gmpr_ex_ex_new_cb (void *context, bv_bitnum_t bitnum,
		   boolean new_val GMPX_UNUSED, boolean old_val GMPX_UNUSED)
{
    gmpr_group *group;
    gmpr_group_addr_entry *group_addr_entry;

    group = context;

    /* See if the address is present in the running-timer list. */

    if (!gmp_addr_in_list(&group->rgroup_src_addr_running, bitnum)) {

	/*
	 * Not in the running-timer list.  See if it's in the
	 * stopped-timer list.
	 */
	if (!gmp_addr_in_list(&group->rgroup_src_addr_stopped, bitnum)) {

	    /*
	     * Not in either list.  Allocate a new one and put it in
	     * the running-timer list.
	     */
	    group_addr_entry = gmpr_create_running_list_entry(group, bitnum);
	    if (!group_addr_entry)
		return FALSE;		/* Out of memory or limit hit */
	    gmpx_start_timer(group_addr_entry->rgroup_addr_timer,
			     group->rgroup_intf->rintf_group_membership_ivl,
			     0);

	    /* Copy the reporter address from the group. */

	    gmpr_copy_reporter(group, group_addr_entry);
	}
    }

    return FALSE;
}


/*
 * gmpr_process_cur_state_ex_ex
 *
 * Process a current-state record of IS_EX type with a filter state of
 * Exclude.
 *
 * We do a bunch of set math.
 */
static void
gmpr_process_cur_state_ex_ex (gmpr_group *group, gmp_addr_vect *source_vect)
{
    /*
     * OK, this one is really nutty.  By the spec, set A is the set of
     * addresses in the new report, X is the set in the running-timer
     * list, and Y is the set in the stopped-timer list.  By definition,
     * X and Y are non-overlapping.
     *
     * First, we delete X-A.  This eliminates all addresses from the
     * running-timer list, except for (A*X).
     */
    if (gmp_addr_vect_minus(&group->rgroup_src_addr_running.addr_vect,
			    source_vect, NULL, gmpr_delete_include_cb, group,
			    BV_CALL_SET) < 0)
	return;				/* Out of memory */

    /*
     * Now, delete Y-A.  This eliminates all addresses from the stopped-timer
     * list, except for (A*Y).
     */
    if (gmp_addr_vect_minus(&group->rgroup_src_addr_stopped.addr_vect,
			    source_vect, NULL, gmpr_delete_exclude_cb, group,
			    BV_CALL_SET) < 0)
	return;				/* Out of memory */

    /*
     * Next, walk A, adding to the running list any address not found
     * on what remains of the running and stopped lists.  This forms the
     * set (A-X-Y).  The net result is that the running list contains
     * (A-X-Y) + (A*X), which is the same as (A-Y).  Whew.
     */
    gmp_addr_vect_walk(source_vect, gmpr_ex_ex_new_cb, group);

    /* Finally, restart the group timer. */

    gmpx_start_timer(group->rgroup_group_timer,
		     group->rgroup_intf->rintf_group_membership_ivl, 0);
}


/*
 * gmpr_in_ex_running_cb
 *
 * Vector walk callback for Include/Exclude current-state.  We're getting
 * called for any entry on the running-timer list that's not on the exclude
 * list.  We delete such entries from the running-timer list.
 */
static boolean
gmpr_in_ex_running_cb (void *context, bv_bitnum_t bitnum,
		       boolean new_val GMPX_UNUSED,
		       boolean old_val GMPX_UNUSED)
{
    gmpr_group *group;
    gmp_addr_list_entry *addr_entry;

    group = context;

    /* Look up the entry. */

    addr_entry = gmp_lookup_addr_entry(&group->rgroup_src_addr_running,
				       bitnum);
    gmpx_assert(addr_entry);

    /* Delete it.  The callback will free the timer. */

    gmp_delete_addr_list_entry(addr_entry);

    return FALSE;
}


/*
 * gmpr_process_cur_state_in_ex
 *
 * Process a current-state record of IS_EX type with a filter state of
 * Include.
 *
 * We switch filter modes from Include to Exclude and do a bunch of
 * set math.
 */
static void
gmpr_process_cur_state_in_ex (gmpr_group *group, gmp_addr_vect *source_vect)
{
    /* Change the filter mode to Exclude. */

    group->rgroup_filter_mode = GMP_FILTER_MODE_EXCLUDE;

    /*
     * Calculate (B-A), where B is the set of sources in the new record
     * and A is the set of sources in the running-timer list.  These
     * new entries are put into the stopped-timer list.
     */
    gmpx_assert(gmp_addr_list_empty(&group->rgroup_src_addr_stopped));
    if (gmp_addr_vect_minus(source_vect,
			    &group->rgroup_src_addr_running.addr_vect, NULL,
			    gmpr_add_exclude_cb, group, BV_CALL_SET) < 0)
	return;				/* Out of memory */

    /*
     * Now calculate (A-B).  The resultant entries are deleted from
     * the running list.
     */
    if (gmp_addr_vect_minus(&group->rgroup_src_addr_running.addr_vect,
			    source_vect, NULL, gmpr_in_ex_running_cb, group,
			    BV_CALL_SET) < 0)
	return;				/* Out of memory */

    /* Restart the group timer. */

    gmpx_start_timer(group->rgroup_group_timer,
		     group->rgroup_intf->rintf_group_membership_ivl, 0);

    /* Update the OIF. */

    gmpr_update_oif_mode_change(group);
}


/*
 * gmpr_process_cur_state_rcrd
 *
 * Process a current-state record.
 */
static void
gmpr_process_cur_state_rcrd (gmpr_group *group,
			     gmp_report_rectype rec_type,
			     gmp_addr_vect *source_vect)
{
    /*
     * Process based on our current filter state, combined with the
     * received record type.
     */
    switch (group->rgroup_filter_mode) {
      case GMP_FILTER_MODE_INCLUDE:

	switch (rec_type) {
	  case GMP_RPT_IS_IN:
	    gmpr_add_include(group, source_vect);
	    break;

	  case GMP_RPT_IS_EX:
	    gmpr_process_cur_state_in_ex(group, source_vect);
	    break;

	  default:
	    gmpx_assert(FALSE);
	    break;
	}
	break;

      case GMP_FILTER_MODE_EXCLUDE:

	switch (rec_type) {
	  case GMP_RPT_IS_IN:
	    gmpr_move_include(group, source_vect);
	    break;

	  case GMP_RPT_IS_EX:
	    gmpr_process_cur_state_ex_ex(group, source_vect);
	    break;

	  default:
	    gmpx_assert(FALSE);
	    break;
	}
	break;

      default:
	gmpx_assert(FALSE);
    }
}


/*
 * gmpr_update_version_compatibility_mode
 *
 * Update the version compatibility mode for this group based on the
 * received packet.
 */
static void
gmpr_update_version_compatibility_mode (gmpr_group *group, gmp_version ver)
{
    gmpr_intf *intf;
    uint32_t old_host_ivl;

    intf = group->rgroup_intf;

    /* Calculate the older-host-present interval. */

    old_host_ivl = (intf->rintf_robustness * intf->rintf_query_ivl) +
	intf->rintf_query_resp_ivl;

    /* Start the appropriate timer. */

    if (ver == GMP_VERSION_BASIC) {
	gmpx_start_timer(group->rgroup_basic_host_present, old_host_ivl, 0);
    } else if (ver == GMP_VERSION_LEAVES) {
	gmpx_start_timer(group->rgroup_leaves_host_present, old_host_ivl, 0);
    }

    /* Evaluate the group version. */

    gmpr_evaluate_group_version(group);
}


/*
 * gmpr_harmonize_report_version
 *
 * Harmonize the report based on the current group compatibility version.
 * We may modify the report contents, or even discard it.
 *
 * Returns TRUE if the group record should continue to be processed, or
 * FALSE if it should be ignored.
 */
static boolean
gmpr_harmonize_report_version (gmp_version group_version,
			       gmp_report_group_record *group_rcrd)
{
    /* Bail if we're running the latest. */

    if (group_version == GMP_VERSION_SOURCES)
	return TRUE;

    /* LEAVES or BASIC version.  Ignore any BLOCK messages. */

    if (group_rcrd->gmp_rpt_type == GMP_RPT_BLOCK)
	return FALSE;

    /*
     * If the record is a non-null TO_IN or IS_IN or ALLOW, change it to a null
     * TO_EX.
     */
    if (group_rcrd->gmp_rpt_rcv_srcs &&
	(group_rcrd->gmp_rpt_type == GMP_RPT_TO_IN ||
	 group_rcrd->gmp_rpt_type == GMP_RPT_IS_IN ||
	 group_rcrd->gmp_rpt_type == GMP_RPT_ALLOW)) {
	gmp_destroy_addr_thread(group_rcrd->gmp_rpt_rcv_srcs);
	group_rcrd->gmp_rpt_rcv_srcs = NULL;
	group_rcrd->gmp_rpt_type = GMP_RPT_TO_EX;
    }

    /* If the record is a non-null TO_EX or IS_EX, strip the sources. */

    if (group_rcrd->gmp_rpt_rcv_srcs &&
	(group_rcrd->gmp_rpt_type == GMP_RPT_TO_EX ||
	 group_rcrd->gmp_rpt_type == GMP_RPT_IS_EX)) {
	gmp_destroy_addr_thread(group_rcrd->gmp_rpt_rcv_srcs);
	group_rcrd->gmp_rpt_rcv_srcs = NULL;
    }

    /* If we're using the BASIC version, ignore any TO_IN or IS_IN messages. */

    if (group_version == GMP_VERSION_BASIC) {
	if (group_rcrd->gmp_rpt_type == GMP_RPT_TO_IN ||
	    group_rcrd->gmp_rpt_type == GMP_RPT_IS_IN) {
	    return FALSE;
	}
    }

    return TRUE;
}


/*
 * gmpr_process_report_packet
 *
 * Process a received report packet.
 */
static void
gmpr_process_report_packet(gmpr_intf *intf, gmp_packet *packet)
{
    gmpr_instance *instance;
    gmpr_group *group;
    gmp_report_packet *rpt_pkt;
    gmp_report_group_record *group_rcrd;
    thread *thread_ptr;
    gmp_addr_vect source_vect;
    gmp_addr_thread_entry *thread_entry;
    gmp_addr_string *addr;
    uint8_t *group_addr;
    gmpr_instance_context *ctx;
    gmp_version group_version;
    boolean got_sources;

    instance = intf->rintf_instance;
    ctx = &instance->rinst_cb_context;
    rpt_pkt = &packet->gmp_packet_contents.gmp_packet_report;
    gmp_init_addr_vector(&source_vect, &instance->rinst_addr_cat);

    gmpr_trace_agent("Process report packet : file : %s, line : %.",
                            __FILE__, __LINE__);

    /* Walk all of the groups in the report. */

    FOR_ALL_CIRCULAR_THREAD_ENTRIES(&rpt_pkt->gmp_report_group_head,
				    thread_ptr) {
	group_rcrd = gmp_thread_to_report_group_record(thread_ptr);

	/* Look up the group.  It may not be there. */

	group_addr = group_rcrd->gmp_rpt_group.gmp_addr;
	group = gmpr_group_lookup(intf, group_addr);

 	/*
 	 * Modify the received report based on the compatibility mode.
 	 * We will skip the record entirely if it says to.
 	 */
 	group_version = gmpr_group_version(intf, group);
 	if (gmpr_harmonize_report_version(group_version, group_rcrd)) {

	    /*
	     * We didn't toss the record.  If there are any sources
	     * present, walk them.
	     */
	    if (group_rcrd->gmp_rpt_rcv_srcs &&
		group_rcrd->gmp_rpt_rcv_srcs->gmp_addr_thread_count) {

		thread_entry = NULL;
		while (TRUE) {
		    addr =
			gmp_next_addr_thread_addr(group_rcrd->gmp_rpt_rcv_srcs,
						  &thread_entry);
		    if (!addr)
			break;

		    /* See if the group and source pass policy. */

		    if (ctx->rctx_policy_cb) {
			if (!(*ctx->rctx_policy_cb)(instance->rinst_context,
						    intf->rintf_id, group_addr,
						    addr->gmp_addr,
						    packet->gmp_packet_attr)) {
			    continue;
			}
		    }

		    /* Got an address.  Add it to the vector. */

		    if (gmp_addr_vect_set(&source_vect, addr) < 0)
			return;		/* Out of memory */
		}

		/*
		 * If we get here without any sources, it means that they
		 * were all blocked by policy, so we should skip the
		 * record and go on.
		 */
		if (gmp_addr_vect_empty(&source_vect))
		    continue;

	    } else {

		/* No sources.  See if the group passes policy. */

		if (ctx->rctx_policy_cb) {
		    if (!(*ctx->rctx_policy_cb)(instance->rinst_context,
						intf->rintf_id, group_addr,
						NULL,
						packet->gmp_packet_attr)) {
			continue;
		    }
		}

		/*
		 * Ignore the group if this is a join and the group is
		 * SSM-only.
		 */
		if (group_rcrd->gmp_rpt_type == GMP_RPT_TO_EX ||
		    group_rcrd->gmp_rpt_type == GMP_RPT_IS_EX) {
		    if (ctx->rctx_ssm_check_cb) {
			if (!(*ctx->rctx_ssm_check_cb)(instance->rinst_context,
						       intf->rintf_id,
						       group_addr)) {
			    continue;
			}
		    }
		}
	    }

	    /*
	     * If we get here, the record is valid.  If there is no group,
	     * and the record is some kind of leave, skip to the next
	     * record without creating anything.
	     */
	    got_sources = !gmp_addr_vect_empty(&source_vect);
	    if (!group &&
		((group_rcrd->gmp_rpt_type == GMP_RPT_TO_IN && !got_sources) ||
		 (group_rcrd->gmp_rpt_type == GMP_RPT_IS_IN && !got_sources) ||
		 (group_rcrd->gmp_rpt_type == GMP_RPT_BLOCK))) {
		goto next_record;
	    }

	    /* Create a group if we don't have one yet. */

	    if (!group)
		group = gmpr_group_create(intf, &group_rcrd->gmp_rpt_group);

	    /*
	     * Now check to see if we have a group.  If we do, process
	     * the record.  If not, skip it, as we are out of memory or have
	     * hit the group limit for the interface.
	     */
	    if (group) {

		/* Update the version compatibility mode. */

		gmpr_update_version_compatibility_mode(group,
					       packet->gmp_packet_version);

		/* Note the reporter's address. */

		memmove(group->rgroup_last_reporter.gmp_addr,
            packet->gmp_packet_src_addr.gmp_addr,
            instance->rinst_addrlen);
 
		/* See if this is a current-state or state-change record. */

		if (group_rcrd->gmp_rpt_type == GMP_RPT_IS_IN ||
		    group_rcrd->gmp_rpt_type == GMP_RPT_IS_EX) {

		    /* Current-state record.  Process it. */

		    gmpr_process_cur_state_rcrd(group, group_rcrd->gmp_rpt_type,
						&source_vect);

		} else {

		    /* State-change record.  Process it. */

		    gmpr_process_state_chg_rcrd(group, group_rcrd->gmp_rpt_type,
						&source_vect);
		}

		/* Do host processing, if appropriate. */

		gmpr_host_process_report(packet->gmp_packet_src_addr.gmp_addr,
					 group_rcrd->gmp_rpt_type, group,
					 &source_vect);
	    }

	  next_record:

	    /* Clean up the source vector. */

	    gmp_addr_vect_clean(&source_vect);
	}
    }
}


/*
 * gmpr_packet_rcv_callback
 *
 * Callback from generic packet handling to process a packet, provided
 * in the generic form.  All syntax checking has already occurred.
 */
static void
gmpr_packet_rcv_callback (gmpx_intf_id intf_id, gmp_packet *packet)
{
    gmpr_intf *intf;

    /* Look up the interface. */

    intf = gmpr_intf_lookup_global(packet->gmp_packet_proto, intf_id);

    /* Bail if no interface. */

    if (!intf)
	return;

    /* Return if passive receive. */

    if (intf->rintf_passive_receive)
	return;

    /* Tease them apart by type. */

    switch (packet->gmp_packet_type) {
      case GMP_QUERY_PACKET:
	gmpr_process_query_packet(intf, packet);
	break;

      case GMP_REPORT_PACKET:
	gmpr_process_report_packet(intf, packet);
	break;

      default:
	gmpx_assert(FALSE);
    }

    /* Pass along any notifications. */

    gmpr_alert_clients(intf->rintf_instance);
    gmpr_alert_host_clients(intf->rintf_instance);
}


/*
 * gmpr_send_gss_query
 *
 * Send a GSS query on an interface, if appropriate.
 *
 * Returns a pointer to the packet, or NULL if out of memory.
 */
static gmp_packet *
gmpr_send_gss_query (gmpr_group *group)
{
    gmpr_instance *instance;
    gmpr_intf *intf;
    gmp_addr_list *addr_list;
    gmp_packet *packet;
    gmp_query_packet *query_packet;
    boolean hi_timer;

    intf = group->rgroup_intf;

    /* Bail if we don't need to send a GSS query. */

    if (!group->rgroup_send_gss_query)
	return NULL;

    /* Bail if we're not the querier. */

    if (!intf->rintf_querier) {
	group->rgroup_send_gss_query = FALSE;
	return NULL;
    }

    /*
     * There are two lists of sources to query, the lo-timer list and the
     * hi-timer list.  Take a look at the lo-timer list first.
     */
    addr_list = &group->rgroup_query_lo_timers;
    if (!gmp_xmit_addr_list_empty(addr_list)) {

	/* Something on the low timer list.  Flag it. */

	hi_timer = FALSE;

    } else {

	/* Try the hi-timer list. */

	addr_list = &group->rgroup_query_hi_timers;

	if (!gmp_xmit_addr_list_empty(addr_list)) {

	    /* Something on the high timer list.  Flag it. */

	    hi_timer = TRUE;

	} else {

	    /* Nothing found.  Bail. */

	    group->rgroup_send_gss_query = FALSE;
	    return NULL;
	}
    }
    
    /* Something to send.  Get a packet header and initialize it. */

    instance = intf->rintf_instance;
    packet = gmpp_create_packet_header(group->rgroup_compatibility_mode,
				       GMP_QUERY_PACKET,
				       instance->rinst_proto);
    if (!packet)
	return NULL;			/* Out of memory */

    /*
     * Fill in the packet.  We set the S bit if we're sending the high-timer
     * list.
     */
    query_packet = &packet->gmp_packet_contents.gmp_packet_query;
    query_packet->gmp_query_max_resp = intf->rintf_lmq_ivl;
    query_packet->gmp_query_group_query = TRUE;
    memmove(&query_packet->gmp_query_group, &group->rgroup_addr, sizeof(gmp_addr_string));
    query_packet->gmp_query_qrv = intf->rintf_robustness;
    query_packet->gmp_query_qqi = intf->rintf_query_ivl;
    query_packet->gmp_query_suppress = hi_timer;
    query_packet->gmp_query_xmit_srcs = addr_list;
    query_packet->gmp_query_group_id = group;

    return packet;
}


/*
 * gmpr_send_group_query
 *
 * Send a group query on an interface, if appropriate.
 *
 * Returns a pointer to the packet, or NULL if out of memory.
 */
static gmp_packet *
gmpr_send_group_query (gmpr_group *group)
{
    gmpr_instance *instance;
    gmpr_intf *intf;
    gmp_packet *packet;
    gmp_query_packet *query_packet;

    intf = group->rgroup_intf;

    /* Bail if we don't need to send a group query. */

    if (!group->rgroup_send_group_query)
	return NULL;

    group->rgroup_send_group_query = FALSE;

    /* Bail if we're not the querier. */

    if (!intf->rintf_querier)
	return NULL;
    
    /* Something to send.  Get a packet header and initialize it. */

    instance = intf->rintf_instance;
    packet = gmpp_create_packet_header(group->rgroup_compatibility_mode,
				       GMP_QUERY_PACKET,
				       instance->rinst_proto);
    if (!packet)
	return NULL;			/* Out of memory */

    /* Fill in the packet. */

    query_packet = &packet->gmp_packet_contents.gmp_packet_query;
    query_packet->gmp_query_max_resp = intf->rintf_lmq_ivl;
    query_packet->gmp_query_group_query = TRUE;
    memmove(&query_packet->gmp_query_group, &group->rgroup_addr, sizeof(gmp_addr_string));
    query_packet->gmp_query_qrv = intf->rintf_robustness;
    query_packet->gmp_query_qqi = intf->rintf_query_ivl;
    query_packet->gmp_query_group_id = group;

    /* Set the S bit if the group timer is larger than LMQT. */

    query_packet->gmp_query_suppress =
	(gmpx_timer_time_remaining(group->rgroup_group_timer) >
				   intf->rintf_lmqt);

    return packet;
}


/*
 * gmpr_send_gen_query
 *
 * Send a general query packet on an interface, if appropriate.
 *
 * Returns a pointer to the packet, or NULL if out of memory.
 */
static gmp_packet *
gmpr_send_gen_query (gmpr_intf *intf)
{
    gmpr_instance *instance;
    gmp_packet *packet;
    gmp_query_packet *query_packet;

    packet = NULL;

    /* Do so only if we need to. */

    if (intf->rintf_send_gen_query) {

	/*
	 * See if we're supposed to be querier.  This is the case
	 * the other_querier_present timer is stopped, and if we're not
	 * disabled and running IGMP version 1.
	 */
	if (intf->rintf_querier &&
	    !(intf->rintf_ver == GMP_VERSION_BASIC &&
	      !intf->rintf_querier_enabled)) {

	    /* Guess we need to.  Get a packet header and initialize it. */

	    instance = intf->rintf_instance;
	    packet = gmpp_create_packet_header(intf->rintf_ver,
					       GMP_QUERY_PACKET,
					       instance->rinst_proto);
	    if (!packet)
		return NULL;		/* Out of memory */

	    query_packet = &packet->gmp_packet_contents.gmp_packet_query;
	    query_packet->gmp_query_max_resp = intf->rintf_query_resp_ivl;
	    query_packet->gmp_query_group_query = FALSE;
	    query_packet->gmp_query_qrv = intf->rintf_robustness;
	    query_packet->gmp_query_qqi = intf->rintf_query_ivl;
	}

    }
    intf->rintf_send_gen_query = FALSE;
    return packet;
}


/*
 * gmpr_packet_free_callback
 *
 * Callback from the packet handler when it is done with a packet structure.
 */
static void
gmpr_packet_free_callback (gmp_packet *packet)
{
    /* Free the packet. */

    gmpp_destroy_packet(packet);
}


/*
 * gmpr_group_done_callback
 *
 * Callback from the packet handler when it is done processing a group.
 */
static void
gmpr_group_done_callback (void *group_id)
{
    gmpr_group *group;

    group = group_id;

    /* Flush the query lists if there's nothing left to send. */

    if (gmp_xmit_addr_list_empty(&group->rgroup_query_hi_timers))
	gmp_flush_addr_list(&group->rgroup_query_hi_timers);
    if (gmp_xmit_addr_list_empty(&group->rgroup_query_lo_timers))
	gmp_flush_addr_list(&group->rgroup_query_lo_timers);

    /*
     * Try to free the group.  This will happen if there's no longer any
     * interest in this group and we've sent all of the necessary messages.
     */
    gmpr_attempt_group_free(group);
}


/*
 * gmpr_xmit_callback
 *
 * Callback from the packet handler when it is ready to send a packet.
 *
 * Returns a pointer to a generic packet to send, or NULL if there's nothing
 * to send.
 *
 * Also returns a pointer to a packet to send.
 */
static gmp_packet *
gmpr_xmit_callback (gmpx_intf_id intf_id, gmp_proto proto,
		    uint32_t buffer_len GMPX_UNUSED)
{
    gmpr_intf *intf;
    gmpr_group *group;
    gmp_packet *packet;

    /* Look up the interface. */

    intf = gmpr_intf_lookup_global(proto, intf_id);
    if (!intf)				/* No interface! */
	return NULL;

    /*
     * Got an interface.  By default, turn off the "xmit pending" flag.
     * We'll turn it back on if we end up returning a real packet.
     */
    packet = NULL;
    intf->rintf_xmit_pending = FALSE;

    /* If we need to send a general query, do so. */

    packet = gmpr_send_gen_query(intf);

    /* If nothing yet, take a look at the group transmit list. */

    if (!packet) {

	/*
	 * Start pulling groups off of the interface transmit list.  Normally
	 * we will use the first one, but it's possible for a group to be
	 * overtaken by events and no longer have anything to say.
	 */
	while (TRUE) {

	    /* Grab the first group off of the transmit list. */

	    group = gmpr_first_group_xmit(intf);
	    if (!group)
		break;

	    /* Got a group.  See if we need to send a gss query. */

	    packet = gmpr_send_gss_query(group);

	    /* If nothing yet, see if we need to send a group query. */

	    if (!packet)
		packet = gmpr_send_group_query(group);

	    /*
	     * If we've got a packet, bail from the loop.  Otherwise,
	     * it looks like there was nothing for this group after all,
	     * so we dequeue it and try the next one.
	     */
	    if (packet)
		break;
	    gmpr_dequeue_group_xmit(group);
	}
    }

    /*
     * If we're actually returning a packet, note that we still have
     * a transmission pending.
     */
    if (packet)
	intf->rintf_xmit_pending = TRUE;

    return packet;
}


/*
 * gmpr_register_packet_handler
 *
 * Register us with the generic packet handler.
 */
void
gmpr_register_packet_handler (void)
{
    /* Call the packet handler registration routines with the right stuff. */

    gmpp_register(GMP_ROLE_ROUTER, gmpr_xmit_callback,
		  gmpr_packet_rcv_callback, gmpr_group_done_callback,
		  gmpr_packet_free_callback);
}


/*
 * gmpr_group_timer_expiry
 *
 * Called when a group timer expires.
 */
void
gmpr_group_timer_expiry (gmpx_timer *timer, void *context)
{
    gmpr_instance *instance;
    gmpr_group *group;

    group = context;
    instance = group->rgroup_intf->rintf_instance;
    gmpx_stop_timer(timer);

    gmpr_trace_agent("Group Timer Expiry : file : %s, line : %.",
                            __FILE__, __LINE__);

    /*
     * A group timer expiry means that there are no more active
     * Exclude callers out there.  We flush the stopped-timer list,
     * switch to Include mode, and update the OIF.  If the
     * running-timer list is empty, the group will be deleted after
     * the OIF update.
     */
    gmp_flush_addr_list(&group->rgroup_src_addr_stopped);
    group->rgroup_filter_mode = GMP_FILTER_MODE_INCLUDE;
    gmpr_update_oif_mode_change(group);
    gmpr_alert_clients(instance);
    instance->rinst_group_timeout++;
}


/*
 * gmpr_source_timer_expiry
 *
 * Called when a source timer expires.
 */
void
gmpr_source_timer_expiry (gmpx_timer *timer, void *context)
{
    gmpr_group *group;
    gmpr_group_addr_entry *group_addr_entry;
    gmp_addr_list_entry *addr_entry;
    gmpr_instance *instance;

    gmpx_stop_timer(timer);
    group_addr_entry = context;
    addr_entry = &group_addr_entry->rgroup_addr_entry;
    group = group_addr_entry->rgroup_addr_group;
    instance = group->rgroup_intf->rintf_instance;

    /* Check the filter mode. */

    gmpr_trace_agent("Source Timer Expiry : file : %s, line : %.",
                            __FILE__, __LINE__);

    if (group->rgroup_filter_mode == GMP_FILTER_MODE_INCLUDE) {

	/*
	 * Include mode.  The source is going away.  Update the OIF and
	 * delete the entry.
	 */
	gmpr_update_source_oif(group_addr_entry, OIF_DELETE);
	gmp_delete_addr_list_entry(addr_entry);

	/* Now see if there's anything left in the running list. */

	if (gmp_addr_list_empty(&group->rgroup_src_addr_running)) {

	    /*
	     * The running list is now empty.  This means that the last source
	     * went away, and we should instead delete the whole group.
	     */
	    gmpr_update_group_oif(group, OIF_DELETE);
	    gmpr_attempt_group_free(group);
	}

    } else {

	/*
	 * Exclude mode.  Move the entry from the running timer list
	 * to the stopped timer list.
	 */
	gmp_move_addr_list_entry(&group->rgroup_src_addr_stopped, addr_entry);
	gmpr_update_source_oif(group_addr_entry, OIF_UPDATE);
    }

    gmpr_alert_clients(instance);
}


/*
 * gmpr_gss_query_timer_expiry
 *
 * Called when a GSS query transmission timer expires.
 */
void
gmpr_gss_query_timer_expiry (gmpx_timer *timer, void *context)
{
    gmpr_intf *intf;
    gmpr_group *group;
    gmp_addr_list *addr_list;
    gmp_addr_list *running_list;
    gmp_addr_list_entry *addr_entry, *new_addr_entry;
    gmpr_group_addr_entry *group_addr;
    boolean found_something;

    gmpx_stop_timer(timer);
    group = context;
    intf = group->rgroup_intf;

    /*
     * Flush the low-timer and high-timer query lists.  They're probably
     * empty anyhow.
     */
    gmp_flush_addr_list(&group->rgroup_query_lo_timers);
    gmp_flush_addr_list(&group->rgroup_query_hi_timers);

    /*
     * Walk the running-timer list, enqueueing entries onto the low-timer
     * or high-timer query lists for each entry with a nonzero retransmit
     * count.
     */
    found_something = FALSE;
    running_list = &group->rgroup_src_addr_running;
    addr_entry = NULL;
    while (TRUE) {
	addr_entry = gmp_addr_list_next_entry(running_list, addr_entry);
	group_addr = gmpr_addr_entry_to_group_entry(addr_entry);
	if (!group_addr)
	    break;

	/* Process entries with nonzero retransmit counts. */

	if (group_addr->rgroup_addr_rexmit_count) {

	    found_something = TRUE;

	    /* Decrement the retransmission count. */

	    group_addr->rgroup_addr_rexmit_count--;

	    /*
	     * Stick the entry into the low or high timer lists,
	     * depending on the remaining time compared to LMQT.
	     */
	    if (gmpx_timer_time_remaining(group_addr->rgroup_addr_timer) <=
		intf->rintf_lmqt) {
		addr_list = &group->rgroup_query_lo_timers;
	    } else {
		addr_list = &group->rgroup_query_hi_timers;
	    }
	    new_addr_entry =
		gmp_create_addr_list_entry(addr_list,
					   addr_entry->addr_ent_ord);
	    gmp_enqueue_xmit_addr_entry(new_addr_entry);
	}
    }

    /*
     * Enqueue the group, set the flag, kick the transmitter if we
     * found something.
     */
    if (found_something) {
	gmpr_enqueue_group_xmit(group);
	group->rgroup_send_gss_query = TRUE;
	gmpr_kick_xmit(group->rgroup_intf);
	gmpx_start_timer(group->rgroup_gss_query_timer,
			 group->rgroup_intf->rintf_lmq_ivl, 0);
    }

    /*
     * Try tossing the group.  We may have just cleaned up the last
     * bits keeping it alive by flushing the lo and hi timer lists.
     */
    gmpr_attempt_group_free(group);
}


/*
 * gmpr_group_query_timer_expiry
 *
 * Called when a group query transmission timer expires.
 */
void
gmpr_group_query_timer_expiry (gmpx_timer *timer, void *context)
{
    gmpr_group *group;
    gmpr_intf *intf;

    gmpx_stop_timer(timer);
    group = context;
    intf = group->rgroup_intf;

    /* Decrement the retransmit count. */

    gmpx_assert(group->rgroup_query_rexmit_count);
    group->rgroup_query_rexmit_count--;

    /*
     * Enqueue the group, set the flag and kick the transmitter.  When
     * we get called back, we'll actually form the packet.
     */
    gmpr_enqueue_group_xmit(group);
    group->rgroup_send_group_query = TRUE;
    gmpr_kick_xmit(intf);

    /* Restart the timer if the rexmit count is still nonzero. */

    if (group->rgroup_query_rexmit_count)
	gmpx_start_timer(group->rgroup_query_timer, intf->rintf_lmq_ivl, 0);
}


/*
 * gmpr_last_host_addr_ref_gone
 *
 * Called when the last host reference to a (S,G) is going away and we're
 * doing fast leave processing.  We act as if the source timer has expired.
 * We can get away with this because we only track host sources when in
 * Include mode.
 */
void
gmpr_last_host_addr_ref_gone (gmpr_group_addr_entry *group_addr_entry)
{
    /* Just set the source timer to expire immediately. */

    gmpx_start_timer(group_addr_entry->rgroup_addr_timer, 0, 0);
}


/*
 * gmpr_last_host_group_ref_gone
 *
 * Called when the last host reference to a group is going away and we're
 * doing fast leave processing.  We switch to Include mode, flush the
 * source lists, and post the group notification.
 */
void
gmpr_last_host_group_ref_gone (gmpr_group *group)
{
    gmpr_instance *instance;

    /* Bail if the group is already gone. */

    if (!gmpr_group_is_active(group))
	return;

    instance = group->rgroup_intf->rintf_instance;

    /* Force the group to Include{} state. */

    gmp_flush_addr_list(&group->rgroup_src_addr_running);
    gmp_flush_addr_list(&group->rgroup_src_addr_stopped);
    group->rgroup_filter_mode = GMP_FILTER_MODE_INCLUDE;
    gmpx_stop_timer(group->rgroup_group_timer);

    /* Update the OIF and try to delete the group. */

    gmpr_update_oif_mode_change(group);
    gmpr_alert_clients(instance);
}
