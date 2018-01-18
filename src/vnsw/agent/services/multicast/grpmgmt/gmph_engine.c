/* $Id: gmph_engine.c 493569 2012-01-28 13:26:58Z ib-builder $
 *
 * gmph_engine.c - IGMP/MLD Host-Side generic protocol engine
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 * This module contains the guts of the protocol engine for host-side GMP.
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_host.h"
#include "gmp_externs.h"
#include "gmp_private.h"
#include "gmph_private.h"


/*
 * gmph_process_general_query_packet
 *
 * Process a received general query packet.
 */
static void
gmph_process_general_query_packet (gmph_intf *intf, gmp_version ver,
				   gmp_query_packet *pkt)
{
    gmph_instance *instance;
    u_int32_t old_querier_timeout;
    u_int32_t query_ivl;
    u_int32_t now_time;

    instance = intf->hintf_instance;

    /*
     * Update the querier's query interval.  If one came in the packet
     * (by virtue of being VERSION_SOURCES) we use that.  Otherwise,
     * we calculate it based on the interval since the previous
     * general query.
     */
    query_ivl = 0;			/* Silence the compiler. */
    now_time = gmpx_timer_time_remaining(instance->hinst_master_clock);
    if (pkt->gmp_query_qqi) {
	query_ivl = pkt->gmp_query_qqi;
    } else {
	if (now_time <= intf->hintf_last_query_time) {
	    query_ivl = intf->hintf_last_query_time - now_time;
	} else {

	    /* The clock rolled over.  Deal with it. */

	    query_ivl = 0xffffffff - (now_time - intf->hintf_last_query_time);
	}
    }
    intf->hintf_last_query_time = now_time;

    /* Update the robustness variable if the router sent one. */

    if (pkt->gmp_query_qrv)
	intf->hintf_robustness = pkt->gmp_query_qrv;

    /* Update the old querier timeout based on the parameters. */

    old_querier_timeout = (query_ivl * intf->hintf_robustness) +
	pkt->gmp_query_max_resp;

    /* If this is an older version, start the appropriate querier timer. */

    if (ver == GMP_VERSION_BASIC) {
	gmpx_start_timer(intf->hintf_basic_querier, old_querier_timeout, 0);
    } else if (ver == GMP_VERSION_LEAVES) {
	gmpx_start_timer(intf->hintf_leaves_querier, old_querier_timeout, 0);
    }

    /* Now evaluate the version. */

    gmph_intf_evaluate_version(intf);

    /*
     * Ignore the query if we've already got one pending and it expires
     * soon enough.  Note that we're a little out-of-spec here--we'll
     * accept any pending time that is within the requested interval,
     * instead of finding a new random number and choosing the smallest.
     * Random is random, after all.
     */
    if (gmpx_timer_running(intf->hintf_gen_query_timer) &&
	(gmpx_timer_time_remaining(intf->hintf_gen_query_timer) <
	 pkt->gmp_query_max_resp)) {
	return;
    }

    /* Looks like we have to schedule one.  Just kick the timer. */

    gmph_start_general_query_timer(intf, pkt->gmp_query_max_resp,
				   GMPH_QUERY_REPLY_JITTER);
}


/*
 * gmph_process_group_query_packet
 *
 * Process a received group-specific query packet.
 */
static void
gmph_process_group_query_packet (gmph_intf *intf, gmp_version ver,
				 gmp_query_packet *pkt, gmph_group *group)
{
    /* Ignore the packet if the version is higher than the current one. */

    if (ver > intf->hintf_ver)
	return;

    /*
     * Ignore the query if we already have a group-specific query
     * pending that will expire soon enough.
     */
    if (gmpx_timer_running(group->hgroup_query_timer) &&
	gmp_addr_list_empty(&group->hgroup_query_list) &&
	gmpx_timer_time_remaining(group->hgroup_query_timer) <
	pkt->gmp_query_max_resp) {
	return;
    }

    /*
     * Looks like we need to schedule one.  Wipe out the list of sources
     * for a previous gss query, if any, and start the timer.
     */
    gmp_flush_addr_list(&group->hgroup_query_list);
    gmph_start_query_timer(group, pkt->gmp_query_max_resp,
			   GMPH_QUERY_REPLY_JITTER);
}


/*
 * gmph_process_gss_query_packet
 *
 * Process a received group-and-source-specific query packet.
 */
static void
gmph_process_gss_query_packet (gmph_intf *intf, gmp_version ver,
			       gmp_query_packet *pkt, gmph_group *group)
{
    gmp_addr_vect source_vect;

    /* Ignore the packet if the version is higher than the current one. */

    if (ver > intf->hintf_ver)
	return;

    /*
     * Ignore the query if there we already have a group-specific
     * query pending that will expire soon enough.
     */
    if (gmpx_timer_running(group->hgroup_query_timer) &&
	gmp_addr_list_empty(&group->hgroup_query_list) &&
	gmpx_timer_time_remaining(group->hgroup_query_timer) <
	pkt->gmp_query_max_resp) {
	return;
    }

    /*
     * Looks like we need to send a response to this GSS query.  Merge
     * the address list with the existing one.
     */
    gmp_init_addr_vector(&source_vect, &intf->hintf_instance->hinst_addr_cat);
    if (gmp_addr_vect_fill(&source_vect, pkt->gmp_query_rcv_srcs) < 0)
	return;				/* Out of memory */
    gmp_addr_vect_union(&group->hgroup_query_list.addr_vect,
			&source_vect, NULL, gmp_addr_list_copy_cb,
			&group->hgroup_query_list, BV_CALL_SET);
    gmp_addr_vect_clean(&source_vect);

    /* If the timer doesn't expire soon enough, make it do so sooner. */

    if (!gmpx_timer_running(group->hgroup_query_timer) ||
	gmpx_timer_time_remaining(group->hgroup_query_timer) >
	pkt->gmp_query_max_resp) {
	gmph_start_query_timer(group, pkt->gmp_query_max_resp,
			       GMPH_QUERY_REPLY_JITTER);
    }
}


/*
 * gmph_process_query_packet
 *
 * Process a received query packet.
 */
static void
gmph_process_query_packet (gmph_intf *intf, gmp_version ver,
			   gmp_query_packet *pkt)
{
    gmph_group *group;

    /* Bail if we're passive. */

    if (intf->hintf_passive)
	return;

    /* Bail if a soft detach is in process. */

    if (gmph_intf_shutting_down(intf))
	return;

    /* If it's a general query, process it. */

    if (!pkt->gmp_query_group_query) {	/* General... */

	gmph_process_general_query_packet(intf, ver, pkt);

    } else {

	/*
	 * Not a general query.  Look up the group, and process the
	 * query if we care about the group.
	 */
	group = gmph_group_lookup(intf, pkt->gmp_query_group.gmp_addr);
	if (group) {

	    /* Split on group- or group-and-source-specific queries. */

	    if (pkt->gmp_query_rcv_srcs) {

		gmph_process_gss_query_packet(intf, ver, pkt, group);

	    } else {

		gmph_process_group_query_packet(intf, ver, pkt, group);
	    }
	}
    }
}


/*
 * gmph_process_report_packet
 *
 * Process a received report packet.
 */
static void
gmph_process_report_packet (gmph_intf *intf, gmp_version ver,
			    gmp_report_packet *pkt)
{
    gmph_group *group;
    thread *first_entry;
    gmp_report_group_record *group_rec;

    /*
     * We normally ignore report packets, except when doing report
     * suppression.  We only do report suppression on older GMP versions,
     * and only if it is administratively allowed.
     */
    if ((ver == GMP_VERSION_BASIC || ver == GMP_VERSION_LEAVES) &&
	intf->hintf_suppress_reports) {

	/*
	 * Suppression is possible.  Get the first (should be only)
	 * group record.
	 */
	first_entry = thread_circular_top(&pkt->gmp_report_group_head);
	group_rec = gmp_thread_to_report_group_record(first_entry);
	if (group_rec) {
	    group = gmph_group_lookup(intf, group_rec->gmp_rpt_group.gmp_addr);
	    if (group) {

		/* Got a group.  If the timer is running, stop it. */

		if (gmpx_timer_running(group->hgroup_query_timer)) {
		    gmpx_stop_timer(group->hgroup_query_timer);
		    gmph_unmark_pending_group_xmit(group, FALSE);
		    group->hgroup_last_reporter = FALSE;
		}
	    }
	}
    }
}


/*
 * gmph_packet_rcv_callback
 *
 * Callback from generic packet handling to process a packet, provided
 * in the generic form.  All syntax checking has already occurred.
 */
static void
gmph_packet_rcv_callback (gmpx_intf_id intf_id, gmp_packet *packet)
{
    gmp_query_packet *query_packet;
    gmp_report_packet *report_packet;
    gmph_intf *intf;

    /* Look up the interface. */

    intf = gmph_intf_lookup_global(packet->gmp_packet_proto, intf_id);

    /* Bail if no interface. */

    if (!intf)
	return;

    /* Tease them apart by type. */

    switch (packet->gmp_packet_type) {
     case GMP_QUERY_PACKET:
	query_packet = &packet->gmp_packet_contents.gmp_packet_query;
	gmph_process_query_packet(intf, packet->gmp_packet_version,
				  query_packet);
	break;

      case GMP_REPORT_PACKET:
	report_packet = &packet->gmp_packet_contents.gmp_packet_report;
	gmph_process_report_packet(intf, packet->gmp_packet_version,
				   report_packet);
	break;

      default:
	gmpx_assert(FALSE);
    }
}


/*
 * gmph_packet_free_callback
 *
 * Callback from the packet handler when it is done with a packet structure.
 */
static void
gmph_packet_free_callback (gmp_packet *packet)
{
    /* We should only be freeing Report packets. */

    gmpx_assert(packet->gmp_packet_type == GMP_REPORT_PACKET);
    gmpp_destroy_packet(packet);
}


/*
 * gmph_cleanup_allow_block_list
 *
 * Clean up a group Allow or Block list.  We decrement the retransmit
 * count on any entry that was just transmitted, and delete any entries
 * for which the retransmit count has gone to zero.
 */
static void
gmph_cleanup_allow_block_list (gmp_addr_list *addr_list)
{
    gmp_addr_list_entry *addr_entry, *next_addr_entry;
    gmph_rpt_msg_addr_entry *report_entry;

    /* Walk all entries on the address list. */

    addr_entry = gmp_addr_list_next_entry(addr_list, NULL);
    while (addr_entry) {
	next_addr_entry = gmp_addr_list_next_entry(addr_list, addr_entry);
	report_entry = gmph_addr_list_to_group_list(addr_entry);

	/* See if the entry is still on the transmit list. */

	if (!thread_node_on_thread(&addr_entry->addr_ent_xmit_thread)) {

	    /*
	     * Not on the transmit list.  This means that it has been
	     * transmitted.  Decrement the retransmit count.
	     */
	    gmpx_assert(report_entry->msg_rexmit_count);
	    report_entry->msg_rexmit_count--;

	    /* If we're done transmitting the entry, toss it. */

	    if (!report_entry->msg_rexmit_count)
		gmph_delete_rpt_addr_entry(report_entry);
	}
	addr_entry = next_addr_entry;
    }
}


/*
 * gmph_group_done_callback
 *
 * Callback from the packet handler when it is done processing a group.
 */
static void
gmph_group_done_callback (void *group_id)
{
    gmph_group *group;

    group = group_id;

    /* See if we've been sending change messages. */

    if (group->hgroup_change_msg_due) {

	/*
	 * Sending change messages.  See if we're sending mode change
	 * messages.
	 */
	if (group->hgroup_mode_change_rexmit_count) {

	    /*
	     * Sending mode change messages.  Decrement the count.  If it
	     * goes to zero, this has the side effect of no longer sending
	     * mode change messages.
	     */
	    group->hgroup_mode_change_rexmit_count--;
	}

	/*
	 * Clean up the Allow and Block lists.  This will decrement
	 * retransmission counts and possibly free list entries.
	 */
	gmph_cleanup_allow_block_list(&group->hgroup_allow_list);
	gmph_cleanup_allow_block_list(&group->hgroup_block_list);

	/*
	 * If both the Allow and Block transmit lists are empty, we're
	 * done sending change messages.
	 */
	if (gmp_xmit_addr_list_empty(&group->hgroup_allow_list) &&
	    gmp_xmit_addr_list_empty(&group->hgroup_block_list)) {
	    group->hgroup_change_msg_due = FALSE;
	    gmph_unmark_pending_group_xmit(group, FALSE);
	}
    }

    /* See if we've been sending query replies. */

    if (group->hgroup_reply_due) {

	/* See if we've been sending GSS query replies. */

	if (group->hgroup_gss_reply_due) {

	    /*
	     * Sending GSS replies.  If the transmit list for the group
	     * query list is empty, we're done sending replies.
	     */
	    if (gmp_xmit_addr_list_empty(&group->hgroup_query_list)) {
		gmp_flush_addr_list(&group->hgroup_query_list);
		group->hgroup_reply_due = FALSE;
		group->hgroup_gss_reply_due = FALSE;
		gmph_unmark_pending_group_xmit(group, FALSE);
	    }

	} else {

	    /*
	     * Sending GS or general replies.  If the transmit list
	     * for the group is empty, we're done sending replies.
	     */
	    if (gmp_xmit_addr_list_empty(&group->hgroup_src_addr_list)) {
		group->hgroup_reply_due = FALSE;
		gmph_unmark_pending_group_xmit(group, FALSE);
	    }
	}
    }

    /*
     * If there's nothing left to do for this group, dequeue it from the
     * interface transmit list.
     */
    if (!group->hgroup_reply_due && !group->hgroup_change_msg_due)
	gmph_dequeue_group_xmit(group);

    /*
     * Try to free the group.  This will happen if there's no longer any
     * interest in this group and we've sent all of the necessary messages.
     */
    gmph_attempt_group_free(group);
}


/*
 * gmph_send_query_response
 *
 * Format a message to send in response to a query.
 *
 * Returns a pointer to the packet, or NULL if nothing to send.
 *
 * We form a packet with all groups that have pending query responses to send.
 */
static gmp_packet *
gmph_send_query_response (gmph_intf *intf, u_int buffer_len)
{
    gmph_instance *instance;
    gmp_packet *packet;
    gmp_report_packet *report;
    gmp_report_group_record *group_record;
    gmph_group *group;
    gmph_group *next_group;
    boolean packet_has_something;
    boolean group_has_something;
    u_int max_group_count;

    /* Get a packet header and initialize it. */

    instance = intf->hintf_instance;
    packet = gmpp_create_packet_header(intf->hintf_ver, GMP_REPORT_PACKET,
				       instance->hinst_proto);
    if (!packet)
	return NULL;			/* Out of memory */

    report = &packet->gmp_packet_contents.gmp_packet_report;
    packet_has_something = FALSE;

    /* Determine the max number of groups we can put in this packet. */

    max_group_count =
	gmpp_max_group_count(instance->hinst_proto, intf->hintf_ver,
			     GMP_REPORT_PACKET, buffer_len);

    /*
     * Walk all of the groups on the transmit list.  We need to be careful
     * because the group may be deleted from the transmit list while we're
     * working on it.  The number of groups we look at is bounded by the
     * max group count.
     */
    group = gmph_first_group_xmit(intf);
    while (max_group_count) {
	if (!group)
	    break;			/* All done */

	next_group = gmph_next_group_xmit(group);

	/* If the group has the reply flag set, try building a record. */

	if (group->hgroup_reply_due) {

	    group_has_something = FALSE;

	    /* See if we're sending a GSS reply. */

	    if (group->hgroup_gss_reply_due) {

		/* See if there's actually anything to send. */

		if (!gmp_xmit_addr_list_empty(&group->hgroup_query_list)) {
		    /*
		     * Sending a GSS reply.  Send the contents of the query
		     * list in an IS_IN record if there's anything there.
		     */
		    group_record =
			gmpp_create_group_record(report, group,
						 group->hgroup_addr.gmp_addr,
						 instance->hinst_addrlen);
		    if (!group_record)
			break;			/* Out of memory */
		    
		    /* Set the record type and the address list pointer. */

		    group_record->gmp_rpt_type = GMP_RPT_IS_IN;
		    group_record->gmp_rpt_xmit_srcs =
			&group->hgroup_query_list;
		    group_has_something = TRUE;
		    packet_has_something = TRUE;
		    max_group_count--;
		}

	    } else {

		/* Not sending a GSS reply.  See if the group is active. */

		if (gmph_group_is_active(group)) {

		    /*
		     * The group is active.  Send the contents of the
		     * group address transmit list as either IS_IN or
		     * IS_EX, depending on the filter mode.
		     */
		    group_record =
			gmpp_create_group_record(report, group,
						 group->hgroup_addr.gmp_addr,
						 instance->hinst_addrlen);
		    if (!group_record)
			break;			/* Out of memory */

		    /* Set the record type and the address list pointer. */

		    if (group->hgroup_filter_mode == GMP_FILTER_MODE_EXCLUDE) {
			group_record->gmp_rpt_type = GMP_RPT_IS_EX;
		    } else {
			group_record->gmp_rpt_type = GMP_RPT_IS_IN;
		    }
		    group_record->gmp_rpt_xmit_srcs =
			&group->hgroup_src_addr_list;
		    group_has_something = TRUE;
		    packet_has_something = TRUE;
		    max_group_count--;
		}
	    }

	    /*
	     * See if we've created any group records.  If not, emulate
	     * the group_done callback, which will clean things up.
	     */
	    if (!group_has_something)
		gmph_group_done_callback(group);
	}
	group = next_group;
    }

    /*
     * We've processed all of the groups.  If we ended up not putting anything
     * in the packet, toss it.
     */
    if (!packet_has_something) {
	gmph_packet_free_callback(packet);
	packet = NULL;
    }

    return packet;
}


/*
 * gmph_send_change_msg
 *
 * Format a change message to send.
 *
 * Returns a pointer to the packet, or NULL if nothing to send.
 *
 * Note that, although we could send change records intermixed with query
 * replies, we don't do so because we want to be conservative about buggy
 * receivers.
 *
 * We build a packet with records for all groups on the transmit list
 * that have the group_change_msg_due flag set.
 */
static gmp_packet *
gmph_send_change_msg (gmph_intf *intf, u_int buffer_len)
{
    gmph_instance *instance;
    gmp_packet *packet;
    gmp_report_packet *report;
    gmp_report_group_record *group_record;
    gmph_group *group;
    gmph_group *next_group;
    gmp_addr_list *addr_list;
    gmp_report_rectype record_type;
    boolean packet_has_something;
    boolean group_has_something;
    u_int max_group_count;

    /* Get a packet header and initialize it. */

    instance = intf->hintf_instance;
    packet = gmpp_create_packet_header(intf->hintf_ver, GMP_REPORT_PACKET,
				       instance->hinst_proto);
    if (!packet)
	return NULL;			/* Out of memory */

    report = &packet->gmp_packet_contents.gmp_packet_report;
    thread_new_circular_thread(&report->gmp_report_group_head);
    packet_has_something = FALSE;

    /* Determine the max number of groups we can put in this packet. */

    max_group_count =
	gmpp_max_group_count(instance->hinst_proto, intf->hintf_ver,
			     GMP_REPORT_PACKET, buffer_len);

    /*
     * Walk all of the groups on the transmit list.  We need to be careful
     * because the group may be deleted from the transmit list while we're
     * working on it.  The number of groups we look at is bounded by the
     * max group count.
     */
    group = gmph_first_group_xmit(intf);
    while (max_group_count) {
	if (!group)
	    break;			/* All done */

	next_group = gmph_next_group_xmit(group);

	/*
	 * If the group has the change message flag set, try building
	 * a record.
	 */
	if (group->hgroup_change_msg_due) {

	    group_has_something = FALSE;

	    /* See if we're sending mode change messages. */

	    if (group->hgroup_mode_change_rexmit_count) {

		/*
		 * Sending mode change messages.  If we're in Exclude
		 * mode, we want to send a TO_EX record with the
		 * contents of the Block list.  Otherwise, we want to
		 * send a TO_IN record with the contents of the Allow
		 * list.
		 */
		if (group->hgroup_filter_mode == GMP_FILTER_MODE_EXCLUDE) {
		    addr_list = &group->hgroup_block_list;
		    record_type = GMP_RPT_TO_EX;
		} else {
		    addr_list = &group->hgroup_allow_list;
		    record_type = GMP_RPT_TO_IN;
		}

		/* Build the group record.  The address list may be empty. */

		group_record =
		    gmpp_create_group_record(report, group,
					     group->hgroup_addr.gmp_addr,
					     instance->hinst_addrlen);
		if (!group_record)
		    break;			/* Out of memory */

		/* Set the record type and the address list pointer. */

		group_record->gmp_rpt_type = record_type;
		group_record->gmp_rpt_xmit_srcs = addr_list;
		group_has_something = TRUE;
		packet_has_something = TRUE;
		max_group_count--;

	    } else {

		/*
		 * Not sending a mode change.  Try building list change
		 * records.
		 */
		if (!gmp_xmit_addr_list_empty(&group->hgroup_allow_list)) {

		    /* Something on the Allow list.  Build a record. */

		    group_record =
			gmpp_create_group_record(report, group,
						 group->hgroup_addr.gmp_addr,
						 instance->hinst_addrlen);
		    if (!group_record)
			break;			/* Out of memory */

		    /* Set the record type and the address list pointer. */

		    group_record->gmp_rpt_type = GMP_RPT_ALLOW;
		    group_record->gmp_rpt_xmit_srcs =
			&group->hgroup_allow_list;
		    group_has_something = TRUE;
		    packet_has_something = TRUE;
		    max_group_count--;
		}

		if (!gmp_xmit_addr_list_empty(&group->hgroup_block_list)) {

		    /* Something on the Block list.  Build a record. */

		    group_record =
			gmpp_create_group_record(report, group,
						 group->hgroup_addr.gmp_addr,
						 instance->hinst_addrlen);
		    if (!group_record)
			break;			/* Out of memory */

		    /* Set the record type and the address list pointer. */

		    group_record->gmp_rpt_type = GMP_RPT_BLOCK;
		    group_record->gmp_rpt_xmit_srcs =
			&group->hgroup_block_list;
		    group_has_something = TRUE;
		    packet_has_something = TRUE;
		    max_group_count--;
		}
	    }

	    /*
	     * See if we've created any group records.  If not, emulate
	     * the group_done callback, which will have the side effect
	     * of clearing the change_message flag.
	     */
	    if (!group_has_something)
		gmph_group_done_callback(group);
	}
	group = next_group;
    }

    /*
     * We've processed all of the groups.  If we ended up not putting anything
     * in the packet, toss it.
     */
    if (!packet_has_something) {
	gmph_packet_free_callback(packet);
	packet = NULL;
    }

    return packet;
}


/*
 * gmph_xmit_callback
 *
 * Callback from the packet handler when it is ready to send a packet.
 *
 * Returns a pointer to a generic packet to send, or NULL if there's nothing
 * to send.
 *
 * Also returns a pointer to a packet to send.
 */
static gmp_packet *
gmph_xmit_callback (gmpx_intf_id intf_id, gmp_proto proto, u_int buffer_len)
{
    gmph_intf *intf;
    gmp_packet *packet;
    gmp_report_packet *report_packet;
    thread *thread_ptr;
    gmp_report_group_record *group_record;

    /* Look up the interface. */

    intf = gmph_intf_lookup_global(proto, intf_id);
    if (!intf)				/* No interface! */
	return NULL;

    /*
     * Got an interface.  By default, turn off the "xmit pending" flag.
     * We'll turn it back on if we end up returning a real packet.
     */
    packet = NULL;
    intf->hintf_xmit_pending = FALSE;

    /*
     * If the interface is passive, we've gotten here because stuff was
     * enqueued while the interface was active.  For simplicity's sake,
     * we generate packets and then discard them until there's nothing
     * left to send.
     *
     * If the interface is active, we grab one packet and then exit the
     * loop.
     */
    while (TRUE) {

	/* Try to send a change message. */

	packet = gmph_send_change_msg(intf, buffer_len);

	/* If no packet yet, try sending a response to a query. */

	if (!packet)
	    packet = gmph_send_query_response(intf, buffer_len);

	/* If we're not passive, break out of the loop to send what we have. */

	if (!intf->hintf_passive)
	    break;

	/* We're passive.  Bail if there's nothing left to send. */

	if (!packet)
	    break;

	/*
	 * There was a packet.  Walk each group in the packet and call the
	 * group_done routine to clean up.  Then free the packet.
	 */
	gmpx_assert(packet->gmp_packet_type == GMP_REPORT_PACKET);
	report_packet = &packet->gmp_packet_contents.gmp_packet_report;
	thread_ptr =
	    thread_circular_top(&report_packet->gmp_report_group_head);
	while (thread_ptr) {
	    group_record = gmp_thread_to_report_group_record(thread_ptr);
	    thread_ptr = thread_circular_thread_next(
				     &report_packet->gmp_report_group_head,
				     thread_ptr);
	    gmpp_group_done(GMP_ROLE_HOST, intf->hintf_instance->hinst_proto,
			    group_record->gmp_rpt_group_id);
	}
	gmph_packet_free_callback(packet);
    }

    /*
     * If we're actually returning a packet, note that we still have
     * a transmission pending.
     */
    if (packet)
	intf->hintf_xmit_pending = TRUE;

    return packet;
}


/*
 * gmph_register_packet_handler
 *
 * Register us with the generic packet handler.
 */
void
gmph_register_packet_handler (void)
{
    /* Call the packet handler registration routines with the right stuff. */

    gmpp_register(GMP_ROLE_HOST, gmph_xmit_callback, gmph_packet_rcv_callback,
		  gmph_group_done_callback, gmph_packet_free_callback);
}


/*
 * gmph_version_changed
 *
 * Called if the GMP version has changed on an interface.  We do a bunch
 * of cleaning up.
 */
void
gmph_version_changed (gmph_instance *instance GMPX_UNUSED,
		      gmph_intf *intf GMPX_UNUSED)
{
}


/*
 * gmph_group_change_report_timer_expiry
 *
 * Called when a group change report transmission timer expires.
 */
void
gmph_group_change_report_timer_expiry (gmpx_timer *timer, void *context)
{
    gmph_group *group;

    group = context;
    gmpx_stop_timer(timer);

    /*
     * Set up to send the change message only if there is no pending
     * transmission of this group.  We may have a pending transmission
     * of part of a longer, earlier change message, or it could be
     * part of a state report in reply to some kind of query.  We want
     * to let that finish before sending change reports.
     */
    if (!gmph_group_xmit_pending(group)) {

	/*
	 * Nothing pending.  Enqueue all of the address list entries
	 * on the Allow and Block lists, as we're getting ready to
	 * send them.  Enqueue the group for transmission.
	 */
	gmp_enqueue_xmit_addr_list(&group->hgroup_allow_list);
	gmp_enqueue_xmit_addr_list(&group->hgroup_block_list);

	/* Note that we're sending change messages. */

	group->hgroup_change_msg_due = TRUE;
	gmph_mark_pending_group_xmit(group);

	/* Kick the transmission machinery. */

	gmph_enqueue_group_xmit(group);

	/*
	 * Lock the group, so that it won't be released out from
	 * under us as a side effect of change report transmission.
	 */

	gmph_lock_group(group);

	gmph_kick_xmit(group->hgroup_intf);


	/* Undo the lock. This might free host group, in which case bail out
	 * now.
	 */

	if (gmph_unlock_group(group) == FALSE)
	    return;
    }

    /* Restart the timer if there's anything left to send. */

    if (!gmp_addr_list_empty(&group->hgroup_allow_list) ||
	!gmp_addr_list_empty(&group->hgroup_block_list) ||
	group->hgroup_mode_change_rexmit_count) {
	gmph_start_change_rpt_timer(group,
				    group->hgroup_intf->hintf_unsol_rpt_ivl,
				    GMPH_REPORT_REXMIT_JITTER);
    } else {

	/* Nothing to send.  We're not contributing to a pending xmit. */

	gmph_unmark_pending_group_xmit(group, FALSE);
    }
}


/*
 * gss_query_callback
 *
 * Called back by the bit vector code for each address to be sent in
 * response to a group-and-source-specific query.
 */
static boolean
gss_query_callback (void *context, bv_bitnum_t bitnum,
		    boolean new_val GMPX_UNUSED, boolean old_val GMPX_UNUSED)
{
    gmph_group *group;
    gmp_addr_list_entry *addr_entry;
    gmp_addr_list *addr_list;

    group = context;
    addr_list = &group->hgroup_query_list;

    /* Look up the entry in the query list. */

    addr_entry = gmp_lookup_addr_entry(addr_list, bitnum);
    gmpx_assert(addr_entry);
    
    /* Enqueue the entry on the transmit list. */

    gmp_enqueue_xmit_addr_entry(addr_entry);

    return FALSE;
}


/*
 * gmph_group_query_timer_expiry
 *
 * Called when a group query response transmission timer expires.
 *
 */
void
gmph_group_query_timer_expiry (gmpx_timer *timer, void *context)
{
    gmph_group *group;

    group = context;
    gmpx_stop_timer(timer);

    /* See if this is group-specific or group-and-source-specific. */

    if (!gmp_addr_list_empty(&group->hgroup_query_list)) {

	/*
	 * Group-and-source-specific.  Flush the transmit list, just
	 * to be sure.
	 */
	gmp_flush_xmit_list(&group->hgroup_query_list);

	/*
	 * Form the set of sources to be sent based on the filter
	 * mode and query list, and enqueue those entries to be
	 * sent.
	 */
	if (group->hgroup_filter_mode == GMP_FILTER_MODE_INCLUDE) {

	    /*
	     * Include mode.  Send the intersection of the group list
	     * and the list from the query.
	     */
	    gmp_addr_vect_inter(&group->hgroup_query_list.addr_vect,
				&group->hgroup_src_addr_list.addr_vect, NULL,
				gss_query_callback, group, BV_CALL_SET);
	} else {

	    /* Exclude mode.  Send the query list, minus the group list. */

	    gmp_addr_vect_minus(&group->hgroup_query_list.addr_vect,
				&group->hgroup_src_addr_list.addr_vect, NULL,
				gss_query_callback, group, BV_CALL_SET);
	}

	/* Flag that we're sending a reply. */

	group->hgroup_reply_due = TRUE;
	gmph_mark_pending_group_xmit(group);

	/*
	 * Flag that we are sending a GSS reply.  This is necessary because
	 * source addresses are enqueued from the query list in this case.
	 */
	group->hgroup_gss_reply_due = TRUE;

	/* Kick the transmission machinery. */

	gmph_enqueue_group_xmit(group);
	gmph_kick_xmit(group->hgroup_intf);

    } else {

	/*
	 * Group-specific query response.  We need to send a response with
	 * all of the sources.  We flush the query list just in case.
	 */
	gmp_enqueue_xmit_addr_list(&group->hgroup_src_addr_list);
	gmp_flush_addr_list(&group->hgroup_query_list);
	group->hgroup_gss_reply_due = FALSE;
	group->hgroup_reply_due = TRUE;
	gmph_mark_pending_group_xmit(group);

	/* Flush any pending GSS query list. */

	gmp_flush_addr_list(&group->hgroup_query_list);

	/* Enqueue the group and kick the transmitter. */

	gmph_enqueue_group_xmit(group);
	gmph_kick_xmit(group->hgroup_intf);
    }

    /* Remove the pending xmit liability since the timer is stopped. */

    gmph_unmark_pending_group_xmit(group, FALSE);
}


/*
 * gmph_group_general_query_timer_expiry
 *
 * Called from the interface code when a general query timer expires.  We
 * are called for each group tied to the interface.
 */
void
gmph_group_general_query_timer_expiry (gmph_group *group)
{
    /*
     * General query response.  We need to send a response with all of
     * the sources.
     */
    gmp_enqueue_xmit_addr_list(&group->hgroup_src_addr_list);
    group->hgroup_reply_due = TRUE;
    gmph_mark_pending_group_xmit(group);
    gmph_enqueue_group_xmit(group);
}
