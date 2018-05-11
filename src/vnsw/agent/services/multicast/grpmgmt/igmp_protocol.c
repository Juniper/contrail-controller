/* $Id: igmp_proto.c 346474 2009-11-14 10:18:58Z ssiano $
 *
 * igmp_proto.c - IGMP protocol encode/decode routines
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_externs.h"
#include "gmp_private.h"
#include "gmpp_private.h"
#include "gmp_trace.h"
#include "igmp_protocol.h"

/*
 * Destination addresses
 */
static uint8_t igmp_all_hosts[IPV4_ADDR_LEN] = {224, 0, 0, 1};
static uint8_t igmp_all_routers[IPV4_ADDR_LEN] = {224, 0, 0, 2};
static uint8_t igmp_all_v3_routers[IPV4_ADDR_LEN] = {224, 0, 0, 22};



/*
 * igmp_packet_type_string
 *
 * Returns the IGMP packet type string given the packet type.
 */
static const char *
igmp_packet_type_string (igmp_hdr *hdr, uint32_t len)
{
    switch (hdr->igmp_hdr_type) {
      case IGMP_TYPE_QUERY:
	if (len > sizeof(igmp_v1v2_pkt))
	    return "V3 Query";
	if (hdr->igmp_hdr_maxresp)
	    return "V2 Query";
	return "V1 Query";

      case IGMP_TYPE_V1_REPORT:
	return "V1 Report";

      case IGMP_TYPE_V2_REPORT:
	return "V2 Report";

      case IGMP_TYPE_V2_LEAVE:
	return "V2 Leave";

      case IGMP_TYPE_V3_REPORT:
	return "V3 Report";

      default:
	return "Invalid";
    }
}


/*
 * igmp_decode_fixfloat
 *
 * Decodes a fix/float field (Max Resp or QQIC).  Returns the value in
 * native units.
 */
static uint32_t
igmp_decode_fixfloat (uint8_t value)
{
    uint32_t mant, exp, result;

    /* If the flag is set, decode the value as floating point. */

    if (value & IGMP_FIXFLOAT_FLAG) {
	mant = (value & IGMP_FLOAT_MANT_MASK) >> IGMP_FLOAT_MANT_SHIFT;
	exp = (value & IGMP_FLOAT_EXP_MASK) >> IGMP_FLOAT_EXP_SHIFT;
	result = (mant | IGMP_FLOAT_MANT_HIGHBIT) <<
	    (exp + IGMP_FLOAT_EXP_OFFSET);
    } else {
	result = value;
    }

    return result;
}


/*
 * igmp_generic_version
 *
 * Determine the generic version number of an IGMP packet
 *
 * Returns the version number, or GMP_VERSION_INVALID if an error occurs.
 *
 * Also returns the generic message type.
 */
static gmp_version
igmp_generic_version (igmp_packet *pkt, uint32_t pkt_len,
		      gmp_message_type *msg_type)
{
    gmp_version version;

    /* First, branch by packet type. */

    *msg_type = GMP_REPORT_PACKET;	/* Reasonable default. */
    switch (pkt->igmp_pkt_naked.igmp_naked_header_hdr.igmp_hdr_type) {

	/* Do the easy ones first. */

      case IGMP_TYPE_V1_REPORT:
	version = GMP_VERSION_BASIC;
	break;

      case IGMP_TYPE_V2_REPORT:
      case IGMP_TYPE_V2_LEAVE:
	version = GMP_VERSION_LEAVES;
	break;

      case IGMP_TYPE_V3_REPORT:
	version = GMP_VERSION_SOURCES;
	break;

      case IGMP_TYPE_QUERY:

	/*
	 * Now the queries.  Queries are distinguished by length and
	 * the Max Resp field.
	 */
	*msg_type = GMP_QUERY_PACKET;
	if (pkt_len > sizeof(igmp_v1v2_pkt)) /* Long packet */
	    version = GMP_VERSION_SOURCES;
	else if (pkt->igmp_pkt_v1v2.igmp_v1v2_pkt_hdr.igmp_hdr_maxresp != 0)
	    version = GMP_VERSION_LEAVES;
	else
	    version = GMP_VERSION_BASIC;
	break;

      default:
	version = GMP_VERSION_INVALID;
	break;
    }

    return version;
}


/*
 * igmp_max_resp_value
 *
 * Returns the value of the max resp field, in standard units (100 msec.)
 * If the value is zero, it returns the default value (for IGMP v1.)
 */
static uint32_t igmp_max_resp_value (igmp_packet *pkt, gmp_version version)
{
    uint32_t max_resp_field, value;

    max_resp_field =
	pkt->igmp_pkt_naked.igmp_naked_header_hdr.igmp_hdr_maxresp;

    /* Switch by IGMP version. */

    switch (version) {
      case GMP_VERSION_BASIC:
	value = IGMP_MAX_RESP_DEFAULT;
	break;

      case GMP_VERSION_LEAVES:
	value = max_resp_field;
	break;

      case GMP_VERSION_SOURCES:
	value = igmp_decode_fixfloat(max_resp_field);
	break;

      default:
	value = 0;			/* Invalid */
    }

    return value;
}


/*
 * gmp_igmp_trace_bad_pkt
 *
 * Trace a bad IGMP packet
 */
void
gmp_igmp_trace_bad_pkt(uint32_t len, const uint8_t *addr, gmpx_intf_id intf_id,
		       void *trace_context, uint32_t trace_flags)
{
    /* Bail if not tracing bad packets. */

    if (!gmp_trace_set(trace_flags, GMP_TRACE_PACKET_BAD))
	return;

    gmpx_trace(trace_context, "RCV bad IGMP packet, len %u, from %a intf %i",
	       len, addr, intf_id);
}


/*
 * gmp_igmp_trace_pkt
 * 
 * Trace an IGMP packet
 */
void
gmp_igmp_trace_pkt (void *packet, uint32_t len, const uint8_t *addr,
		    gmpx_intf_id intf_id, boolean receive,
		    void *trace_context, uint32_t trace_flags)
{
    const char *direction;
    const char *op;
    igmp_hdr *hdr;
    igmp_v1v2_pkt *v1v2_pkt;
    igmp_v3_query *v3_query;
    igmp_v3_report *v3_report;
    igmp_v3_rpt_rcrd *rpt_rcrd;
    gmp_version version;
    gmp_message_type msg_type;
    uint8_t *byte_ptr;
    uint32_t source_count;
    uint32_t record_count;
    boolean detail;
    igmp_packet *pkt;

    pkt = packet;

    /* See if this packet should be traced at all. */

    if (!gmp_trace_set(trace_flags, GMP_TRACE_PACKET))
	return;

    hdr = &pkt->igmp_pkt_naked.igmp_naked_header_hdr;
    detail = FALSE;
    switch (hdr->igmp_hdr_type) {
      case IGMP_TYPE_QUERY:
	if (!gmp_trace_set(trace_flags, GMP_TRACE_QUERY))
	    return;
	if (gmp_trace_set(trace_flags, GMP_TRACE_QUERY_DETAIL))
	    detail = TRUE;
	break;

      case IGMP_TYPE_V2_LEAVE:
	if (!gmp_trace_set(trace_flags, GMP_TRACE_LEAVE))
	    return;
	if (gmp_trace_set(trace_flags, GMP_TRACE_LEAVE_DETAIL))
	    detail = TRUE;
	break;

      case IGMP_TYPE_V1_REPORT:
      case IGMP_TYPE_V2_REPORT:
      case IGMP_TYPE_V3_REPORT:
	if (!gmp_trace_set(trace_flags, GMP_TRACE_REPORT))
	    return;
	if (gmp_trace_set(trace_flags, GMP_TRACE_REPORT_DETAIL))
	    detail = TRUE;
	break;

      default:
	if (!gmp_trace_set(trace_flags, GMP_TRACE_PACKET_BAD))
	    return;
	break;
    }

    /*
     * If we've gotten this far, we're doing basic tracing of the packet.
     * Do so.
     */
    v1v2_pkt = &pkt->igmp_pkt_v1v2;
    v3_query = &pkt->igmp_pkt_v3_query;
    v3_report = &pkt->igmp_pkt_v3_rpt;
    version = igmp_generic_version(pkt, len, &msg_type);

    if (receive) {
	direction = "from";
	op = "RCV";
    } else {
	direction = "to";
	op = "XMT";
    }
    gmpx_trace(trace_context, "%s IGMP %s len %u %s %a intf %i", op,
	       igmp_packet_type_string(hdr, len), len, direction, addr,
	       intf_id);

    /* Bail if no detailed tracing going on. */

    if (!detail)
	return;

    /* Do any detailed tracing based on packet type. */

    switch (hdr->igmp_hdr_type) {
      case IGMP_TYPE_QUERY:
	gmpx_trace(trace_context, "  Max_resp 0x%x (%u), group %a",
		   hdr->igmp_hdr_maxresp, igmp_max_resp_value(pkt, version),
		   v1v2_pkt->igmp_v1v2_pkt_group);
	if (version == GMP_VERSION_SOURCES) {
	    source_count = get_short(&v3_query->igmp_v3_query_num_srcs);
	    gmpx_trace(trace_context,
		       "  S %u, QRV %u, QQIC 0x%x (%u), sources %u",
		       ((v3_query->igmp_v3_query_s_qrv &
			 IGMP_SUPP_RTR_PROC_MASK) != 0),
		       v3_query->igmp_v3_query_s_qrv & IGMP_QRV_MASK,
		       v3_query->igmp_v3_query_qqic,
		       igmp_decode_fixfloat(v3_query->igmp_v3_query_qqic),
		       source_count);
	    byte_ptr = v3_query->igmp_v3_query_source;
	    while (source_count--) {
		gmpx_trace(trace_context, "    Source %a", byte_ptr);
		byte_ptr += IPV4_ADDR_LEN;
	    }
	}
	break;

      case IGMP_TYPE_V1_REPORT:
      case IGMP_TYPE_V2_REPORT:
      case IGMP_TYPE_V2_LEAVE:
	gmpx_trace(trace_context, "  Group %a", v1v2_pkt->igmp_v1v2_pkt_group);
	break;

      case IGMP_TYPE_V3_REPORT:
	record_count = get_short(&v3_report->igmp_v3_report_num_rcrds);
	gmpx_trace(trace_context, "  Records %u", record_count);

	rpt_rcrd = v3_report->igmp_v3_report_rcrd;
	while (record_count--) {
	    source_count = get_short(&rpt_rcrd->igmp_v3_rpt_num_srcs);
	    gmpx_trace(trace_context,
		       "    Group %a, type %s, aux_len %u, sources %u",
		       rpt_rcrd->igmp_v3_rpt_group,
		       gmp_report_type_string(rpt_rcrd->igmp_v3_rpt_rec_type),
		       rpt_rcrd->igmp_v3_rpt_aux_len, source_count);
	    byte_ptr = get_igmp_v3_rpt_source(rpt_rcrd);
	    while (source_count--) {
		gmpx_trace(trace_context, "      Source %a", byte_ptr);
		byte_ptr += IPV4_ADDR_LEN;
	    }
	    byte_ptr += rpt_rcrd->igmp_v3_rpt_aux_len;
	    rpt_rcrd = (igmp_v3_rpt_rcrd *) byte_ptr;
	}
	break;

    }
}


/*
 * igmp_encode_fixfloat
 *
 * Encodes a fix/float field (Max Resp or QQIC), given the value in native
 * units.  Returns the encoded value, or zero if the value is too large
 * to encode.
 *
 * The encoded value will be less than or equal to the provided value (since
 * we lose granularity in the encoding.)
 */
static uint8_t
igmp_encode_fixfloat (uint32_t value)
{
    uint32_t mant, exp;

    /* If the value is small enough, just use it. */

    if (value < IGMP_FIXFLOAT_FLAG)
	return (uint8_t) value;

    /* If the value is too big, bail. */

    if (value > IGMP_MAX_FLOAT_ENCODABLE)
	return 0;

    /* Big enough to encode.  Do the job. */

    mant = value >> IGMP_FLOAT_EXP_OFFSET;
    for (exp = 0; exp <= IGMP_FLOAT_MAX_EXP; exp++) {

	/* Bail if the mantissa is small enough now. */

	if (mant <= IGMP_FLOAT_MAX_MANT)
	    break;

	/* Shift the mantissa down a bit and try again. */

	mant >>= 1;
    }

    /* Got it.  Form the value and return it. */

    return (IGMP_FIXFLOAT_FLAG | (exp << IGMP_FLOAT_EXP_SHIFT) |
	    (mant & IGMP_FLOAT_MANT_MASK));
}


/*
 * igmp_format_v1_packet
 *
 * Format an IGMP V1 packet.
 *
 * Returns the formatted packet length, or 0 if nothing to send.
 */
static uint32_t
igmp_format_v1_packet (gmp_role role, gmp_packet *gen_packet,
		       igmp_packet *packet, uint32_t packet_len)
{
    igmp_v1v2_pkt *v1v2_pkt;
    igmp_hdr *pkt_hdr;
    gmp_query_packet *query_pkt;
    thread *thread_ptr;
    gmp_report_group_record *group_record;
    gmp_report_packet *report_pkt;
    uint32_t formatted_len;

    /* Version 1 packet.  Pretty straightforward. */

    gmpx_assert(packet_len >= sizeof(igmp_v1v2_pkt));
    v1v2_pkt = &packet->igmp_pkt_v1v2;
    pkt_hdr = &v1v2_pkt->igmp_v1v2_pkt_hdr;
    memset(pkt_hdr, 0, sizeof(igmp_hdr));
    formatted_len = sizeof(igmp_v1v2_pkt);

    switch (gen_packet->gmp_packet_type) {
      case GMP_QUERY_PACKET:

	/* Format a query packet. */

	pkt_hdr->igmp_hdr_type = IGMP_TYPE_QUERY;
	query_pkt = &gen_packet->gmp_packet_contents.gmp_packet_query;
	memset(v1v2_pkt->igmp_v1v2_pkt_group, 0, IPV4_ADDR_LEN);
        memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, igmp_all_hosts, IPV4_ADDR_LEN);
	/*
	 * Flush the source list.  We may end up here with a GSS query due
	 * to a race condition while changing versions.
	 */
	gmp_flush_xmit_list(query_pkt->gmp_query_xmit_srcs);
	break;

      case GMP_REPORT_PACKET:
	    
	/*
	 * Format a report packet.  Note that there may be multiple
	 * group records on this (since V3 supports that).  We just
	 * pull off the first one and format it.  We'll be called back
	 * later to send more packets for other groups.
	 */
	pkt_hdr->igmp_hdr_type = IGMP_TYPE_V1_REPORT;
	report_pkt = &gen_packet->gmp_packet_contents.gmp_packet_report;
	gmpx_assert(report_pkt->gmp_report_group_count >= 1);
	thread_ptr = thread_circular_top(&report_pkt->gmp_report_group_head);
	group_record = gmp_thread_to_report_group_record(thread_ptr);
	gmpx_assert(group_record);

	/*
	 * Don't send anything if this is a leave-equivalent (IS_IN or TO_IN
	 * with a null list, or a BLOCK.
	 */
	if (group_record->gmp_rpt_type == GMP_RPT_BLOCK ||
	    ((group_record->gmp_rpt_type == GMP_RPT_IS_IN ||
	      group_record->gmp_rpt_type == GMP_RPT_TO_IN) &&
	     gmp_addr_list_empty(group_record->gmp_rpt_xmit_srcs))) {
	    formatted_len = 0;
	}
        memmove(v1v2_pkt->igmp_v1v2_pkt_group, group_record->gmp_rpt_group.gmp_addr, IPV4_ADDR_LEN);
        memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, group_record->gmp_rpt_group.gmp_addr, IPV4_ADDR_LEN);

	/* Flush the source list and flag that we're done with this group. */

	gmp_flush_xmit_list(group_record->gmp_rpt_xmit_srcs);
	gmpp_group_done(role, GMP_PROTO_IGMP, group_record->gmp_rpt_group_id);
	break;

      default:
	gmpx_assert(FALSE);
    }

    return formatted_len;
}


/*
 * igmp_format_v2_packet
 *
 * Format an IGMP V2 packet.
 *
 * Returns the formatted packet length, or 0 if nothing to send.
 */
static uint32_t
igmp_format_v2_packet (gmp_role role, gmp_packet *gen_packet,
		       igmp_packet *packet, uint32_t packet_len)
{
    igmp_v1v2_pkt *v1v2_pkt;
    igmp_hdr *pkt_hdr;
    gmp_query_packet *query_pkt;
    gmp_report_packet *report_pkt;
    thread *thread_ptr;
    gmp_report_group_record *group_record;
    uint32_t max_resp;
    uint32_t formatted_len;

    /* Initialize and idiot-proof. */

    gmpx_assert(packet_len >= sizeof(igmp_v1v2_pkt));
    v1v2_pkt = &packet->igmp_pkt_v1v2;
    pkt_hdr = &v1v2_pkt->igmp_v1v2_pkt_hdr;
    memset(pkt_hdr, 0, sizeof(igmp_hdr));
    formatted_len = sizeof(igmp_v1v2_pkt);

    switch (gen_packet->gmp_packet_type) {

      case GMP_QUERY_PACKET:

	/* Format a query packet. */

	pkt_hdr->igmp_hdr_type = IGMP_TYPE_QUERY;
	query_pkt = &gen_packet->gmp_packet_contents.gmp_packet_query;

	/*
	 * If this is a group query, copy in the group address.  Otherwise,
	 * zero it.  Set the destination address accordingly.
	 */
	if (query_pkt->gmp_query_group_query) {
            memmove(v1v2_pkt->igmp_v1v2_pkt_group, query_pkt->gmp_query_group.gmp_addr,
                IPV4_ADDR_LEN);
            memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, query_pkt->gmp_query_group.gmp_addr,
                IPV4_ADDR_LEN);
	} else {
            memset(v1v2_pkt->igmp_v1v2_pkt_group, 0, IPV4_ADDR_LEN);
            memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, igmp_all_hosts, IPV4_ADDR_LEN);
	}

	/* Store the max resp value. */

	max_resp = query_pkt->gmp_query_max_resp / IGMP_MAX_RESP_MSEC;
	if (max_resp > IGMP_V2_MAX_MAX_RESP)
	    max_resp = IGMP_V2_MAX_MAX_RESP;
	pkt_hdr->igmp_hdr_maxresp = max_resp;

	/*
	 * Flush the source list.  We may end up here with a GSS query due
	 * to a race condition while changing versions.
	 */
	gmp_flush_xmit_list(query_pkt->gmp_query_xmit_srcs);
  
	if (query_pkt->gmp_query_group_query) {
	    gmpp_group_done(role, GMP_PROTO_IGMP,
			    query_pkt->gmp_query_group_id);
	}

	break;

      case GMP_REPORT_PACKET:
	    
	/*
	 * Format a report packet.  Note that there may be multiple
	 * group records on this (since V3 supports that).  We just
	 * pull off the first one and format it.  We'll be called back
	 * later to send more packets for other groups.
	 */
	report_pkt = &gen_packet->gmp_packet_contents.gmp_packet_report;
	gmpx_assert(report_pkt->gmp_report_group_count >= 1);
	thread_ptr = thread_circular_top(&report_pkt->gmp_report_group_head);
	group_record = gmp_thread_to_report_group_record(thread_ptr);
	gmpx_assert(group_record);

	/*
	 * If the record type is TO_IN or IS_IN with an empty address
	 * list, this is a Leave.  Otherwise, it is a report.  Set the
	 * destination address accordingly.
	 */
	if ((group_record->gmp_rpt_type == GMP_RPT_TO_IN ||
	     group_record->gmp_rpt_type == GMP_RPT_IS_IN) &&
	    gmp_addr_list_empty(group_record->gmp_rpt_xmit_srcs)) {
	    pkt_hdr->igmp_hdr_type = IGMP_TYPE_V2_LEAVE;
            memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, igmp_all_routers, IPV4_ADDR_LEN);
	} else {
	    pkt_hdr->igmp_hdr_type = IGMP_TYPE_V2_REPORT;
            memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, group_record->gmp_rpt_group.gmp_addr,
                IPV4_ADDR_LEN);
	}

        memmove(v1v2_pkt->igmp_v1v2_pkt_group, group_record->gmp_rpt_group.gmp_addr, IPV4_ADDR_LEN);

	/* If this is a BLOCK record, don't send anything. */

	if (group_record->gmp_rpt_type == GMP_RPT_BLOCK)
	    formatted_len = 0;

	/* Flush the source list and flag that we're done with this group. */

	gmp_flush_xmit_list(group_record->gmp_rpt_xmit_srcs);
	gmpp_group_done(role, GMP_PROTO_IGMP, group_record->gmp_rpt_group_id);
	break;

      default:
	gmpx_assert(FALSE);
    }

    return formatted_len;
}


/*
 * igmp_format_v3_query
 *
 * Format an IGMPv3 Query packet.
 *
 * Returns the formatted packet length, or 0 if nothing to send.
 */
static uint32_t
igmp_format_v3_query (gmp_role role, gmp_packet *gen_packet,
		      igmp_packet *packet, uint32_t packet_len)
{
    igmp_v3_query *v3_query_pkt;
    igmp_hdr *pkt_hdr;
    gmp_query_packet *query_pkt;
    uint32_t formatted_len;
    uint32_t source_count;
    gmp_addr_list_entry *addr_entry;
    gmp_addr_list *addr_list;
    uint8_t *addr_ptr;
    gmp_addr_cat_entry *cat_entry;
    // boolean gss_query;
    boolean group_done;

    /* Format a query packet. */

    gmpx_assert(packet_len >= sizeof(igmp_v3_query) + IPV4_ADDR_LEN);
    query_pkt = &gen_packet->gmp_packet_contents.gmp_packet_query;
    v3_query_pkt = &packet->igmp_pkt_v3_query;
    memset(v3_query_pkt, 0, sizeof(igmp_v3_query));
    pkt_hdr = &v3_query_pkt->igmp_v3_query_hdr;

    /* Set up the header. */

    pkt_hdr->igmp_hdr_type = IGMP_TYPE_QUERY;
    pkt_hdr->igmp_hdr_maxresp =
	igmp_encode_fixfloat(query_pkt->gmp_query_max_resp /
			     IGMP_MAX_RESP_MSEC);

    /* Note whether this is a GSS query. */

    // gss_query = query_pkt->gmp_query_group_query &&
	// query_pkt->gmp_query_xmit_srcs;

    /*
     * If this is a group query, copy in the group address.  Otherwise,
     * zero it.  Set the destination address accordingly.
     */
    if (query_pkt->gmp_query_group_query) {
        memmove(v3_query_pkt->igmp_v3_query_group, query_pkt->gmp_query_group.gmp_addr,
            IPV4_ADDR_LEN);
        memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, query_pkt->gmp_query_group.gmp_addr,
            IPV4_ADDR_LEN);
    } else {
        memset(v3_query_pkt->igmp_v3_query_group, 0, IPV4_ADDR_LEN);
        memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, igmp_all_hosts, IPV4_ADDR_LEN);
    }

    /* Set the S and QRV fields. */

    v3_query_pkt->igmp_v3_query_s_qrv = query_pkt->gmp_query_qrv;
    if (query_pkt->gmp_query_suppress)
	v3_query_pkt->igmp_v3_query_s_qrv |= IGMP_SUPP_RTR_PROC_MASK;

    /* Set the QQIC field. */

    v3_query_pkt->igmp_v3_query_qqic =
	igmp_encode_fixfloat(query_pkt->gmp_query_qqi / MSECS_PER_SEC);

    formatted_len = sizeof(igmp_v3_query);
    packet_len -= sizeof(igmp_v3_query);
    source_count = 0;
    addr_ptr = v3_query_pkt->igmp_v3_query_source;

    /* Add as may sources as possible if they are present. */

    addr_list = query_pkt->gmp_query_xmit_srcs;
    if (addr_list) {

	/*
	 * Top of source loop.  Add sources and delink them from the
	 * transmit chain until either there are no more sources or
	 * we've run out of space in the packet.
	 */
	group_done = TRUE;
	while (TRUE) {

	    /* Bail if out of space in the packet. */

	    if (packet_len < IPV4_ADDR_LEN) {
		group_done = FALSE;
		break;
	    }

	    /* Got space.  Grab the next address. */

	    addr_entry = gmp_first_xmit_addr_entry(addr_list);

	    /* Bail if there's nothing there. */

	    if (!addr_entry)
		break;

	    /*
	     * We've got an address entry.  Look up the catalog entry
	     * and copy the address into the packet.
	     */
	    cat_entry =
		gmp_get_addr_cat_by_ordinal(addr_list->addr_vect.av_catalog,
					    addr_entry->addr_ent_ord);
	    gmpx_assert(cat_entry);
            memmove(addr_ptr, cat_entry->adcat_ent_addr.gmp_addr, IPV4_ADDR_LEN);

	    /* Delink the address entry. */

	    gmp_dequeue_xmit_addr_entry(addr_entry);

	    /* Do housekeeping. */

	    addr_ptr += IPV4_ADDR_LEN;
	    formatted_len += IPV4_ADDR_LEN;
	    packet_len -= IPV4_ADDR_LEN;
	    source_count++;
	}

	/* If we're done with the group, call back. */

	if (group_done) {
	    gmpp_group_done(role, GMP_PROTO_IGMP,
			    query_pkt->gmp_query_group_id);
	}

    }

    /* Update the source count. */

    put_short(&v3_query_pkt->igmp_v3_query_num_srcs, source_count);

    return formatted_len;
}

/*
 * igmp_format_v3_report
 *
 * Format an IGMPv3 Report packet.
 *
 * Returns the formatted packet length, or 0 if nothing to send.
 */
static uint32_t
igmp_format_v3_report (gmp_role role, gmp_packet *gen_packet,
		       igmp_packet *packet, uint32_t packet_len)
{
    igmp_v3_report *v3_rpt_pkt;
    igmp_v3_rpt_rcrd *v3_group_rcrd;
    igmp_hdr *pkt_hdr;
    gmp_report_packet *report_pkt;
    gmp_report_group_record *group_record;
    uint32_t formatted_len;
    uint32_t source_count;
    uint32_t group_count;
    uint32_t groups_remaining;
    uint32_t group_space;
    gmp_addr_list_entry *addr_entry;
    gmp_addr_list *addr_list;
    uint8_t *byte_ptr;
    gmp_addr_cat_entry *cat_entry;
    thread *thread_ptr;
    boolean group_done;

    /* Format a report packet. */

    gmpx_assert(packet_len >= sizeof(igmp_v3_report) +
		sizeof(igmp_v3_rpt_rcrd) + IPV4_ADDR_LEN);
    report_pkt = &gen_packet->gmp_packet_contents.gmp_packet_report;
    v3_rpt_pkt = &packet->igmp_pkt_v3_rpt;
    memset(v3_rpt_pkt, 0, sizeof(igmp_v3_report));
    pkt_hdr = &v3_rpt_pkt->igmp_v3_report_hdr;

    /* Set up the header. */

    pkt_hdr->igmp_hdr_type = IGMP_TYPE_V3_REPORT;

    packet_len -= sizeof(igmp_v3_report);
    formatted_len = sizeof(igmp_v3_report);
    group_count = 0;
    v3_group_rcrd = v3_rpt_pkt->igmp_v3_report_rcrd;
    groups_remaining = report_pkt->gmp_report_group_count;
    thread_ptr = NULL;

    /*
     * Top of the group loop.  Walk the groups until we run out of
     * groups, or run out of space in the packet.
     */
    while (TRUE) {

	/* Bail if there are no more groups. */

	if (!groups_remaining)
	    break;

	/* Grab the next group. */

	thread_ptr =
	    thread_circular_thread_next(&report_pkt->gmp_report_group_head,
					thread_ptr);
	group_record = gmp_thread_to_report_group_record(thread_ptr);
	gmpx_assert(group_record);

	/*
	 * Got the group.  Calculate the size of the group record we'll
	 * generate, and see if it will fit.
	 */
	group_space = sizeof(igmp_v3_rpt_rcrd);
	if (group_record->gmp_rpt_xmit_srcs) {
	    group_space += group_record->gmp_rpt_xmit_srcs->xmit_addr_count *
		IPV4_ADDR_LEN;
	}

	if (group_space > packet_len) {

	    /*
	     * It's not going to fit.  If this is not the first group,
	     * bail.  Otherwise, go ahead with it anyhow.  We'll end
	     * up truncating the list of sources in this case.
	     */
	    if (group_count)
		break;
	}

	/* Create the group record header. */

	memset(v3_group_rcrd, 0, sizeof(igmp_v3_rpt_rcrd));
	v3_group_rcrd->igmp_v3_rpt_rec_type = group_record->gmp_rpt_type;
        memmove(v3_group_rcrd->igmp_v3_rpt_group, group_record->gmp_rpt_group.gmp_addr,
            IPV4_ADDR_LEN);
	packet_len -= sizeof(igmp_v3_rpt_rcrd);
	formatted_len += sizeof(igmp_v3_rpt_rcrd);
	byte_ptr = get_igmp_v3_rpt_source(v3_group_rcrd);
	group_count++;

	/* Now walk the sources, if any, and add them to the record. */

	source_count = 0;
	addr_list = group_record->gmp_rpt_xmit_srcs;
	group_done = TRUE;
	if (addr_list) {
	    while (TRUE) {

		/* Bail if out of space in the packet. */

		if (packet_len < IPV4_ADDR_LEN)
		    break;

		/* Got space.  Grab the next address. */

		addr_entry = gmp_first_xmit_addr_entry(addr_list);

		/* Bail if there's nothing there. */

		if (!addr_entry)
		    break;

		/*
		 * We've got an address entry.  Look up the catalog
		 * entry and copy the address into the packet.
		 */
		cat_entry =
		    gmp_get_addr_cat_by_ordinal(
					addr_list->addr_vect.av_catalog,
					addr_entry->addr_ent_ord);
		gmpx_assert(cat_entry);
                memmove(byte_ptr, cat_entry->adcat_ent_addr.gmp_addr, IPV4_ADDR_LEN);

		/* Delink the address entry. */

		gmp_dequeue_xmit_addr_entry(addr_entry);

		/* Do housekeeping. */

		byte_ptr += IPV4_ADDR_LEN;
		formatted_len += IPV4_ADDR_LEN;
		packet_len -= IPV4_ADDR_LEN;
		source_count++;
	    }

	    /* Update the source count in the group record. */

	    put_short(&v3_group_rcrd->igmp_v3_rpt_num_srcs, source_count);

	    /*
	     * We've either run out of addresses or run out of space.  If
	     * we've run out of space, and the record is of type IS_EX or
	     * TO_EX, we flush the address list, which effectively truncates
	     * the list in the packet.  Otherwise, we leave it alone, and
	     * we'll send more sources in the next packet.
	     */
	    if (!gmp_xmit_addr_list_empty(addr_list)) {

		/* Not empty.  Flush if the record is IS_EX or TO_EX. */

		if (group_record->gmp_rpt_type == GMP_RPT_IS_EX ||
		    group_record->gmp_rpt_type == GMP_RPT_TO_EX) {
		    gmp_flush_xmit_list(addr_list); /* Flush it. */
		} else {
		    group_done = FALSE;	/* More to go! */
		}
	    }
	}

	/* If we finished that group, notify the authorities. */

	if (group_done) {
	    gmpp_group_done(role, GMP_PROTO_IGMP,
			    group_record->gmp_rpt_group_id);
	}

	/* Advance the group record pointer. */

	v3_group_rcrd = (igmp_v3_rpt_rcrd *) byte_ptr;
	groups_remaining--;
    }

    /* Set the destination address. */

    memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, igmp_all_v3_routers, IPV4_ADDR_LEN);

    /* Update the group count in the packet header. */

    put_short(&v3_rpt_pkt->igmp_v3_report_num_rcrds, group_count);

    return formatted_len;
}


/*
 * igmp_format_v3_packet
 *
 * Format an IGMP V3 packet.
 *
 * Returns the formatted packet length, or 0 if nothing to send.
 */
static uint32_t
igmp_format_v3_packet (gmp_role role, gmp_packet *gen_packet,
		       igmp_packet *packet, uint32_t packet_len)
{
    uint32_t formatted_len;

    /* See what kind of packet we're sending. */

    switch (gen_packet->gmp_packet_type) {

      case GMP_QUERY_PACKET:
	formatted_len = igmp_format_v3_query(role, gen_packet, packet,
					     packet_len);

	break;


      case GMP_REPORT_PACKET:
	formatted_len = igmp_format_v3_report(role, gen_packet, packet,
					      packet_len);
	break;

      default:
	gmpx_assert(FALSE);
	formatted_len = 0;		/* Quiet the compiler. */
    }

    return formatted_len;
}


/*
 * igmp_next_xmit_packet
 *
 * Format the next IGMP packet to transmit for a role (host/router).
 *
 * Returns the length of the formatted packet, or 0 if there was
 * nothing to send.
 *
 * Also returns a pointer to the destination address to send the packet to.
 *
 * The packet is formatted in the supplied buffer, which is assumed to be
 * large enough to carry at least a minimum packet of any type.
 *
 * Source and destination addresses are written through the supplied pointers.
 */
uint32_t
igmp_next_xmit_packet (gmp_role role, gmpx_intf_id intf_id,
		       void *packet, uint8_t *dest_addr, uint32_t packet_len,
		       void *trace_context, uint32_t trace_flags)
{
    gmp_packet *gen_packet;
    igmp_packet *igmp_pkt;
    igmp_hdr *pkt_hdr;
    uint32_t formatted_len;
    uint16_t checksum;

    igmp_pkt = packet;
    formatted_len = 0;

    while (TRUE) {

	/* Get the next generic packet. */

	gen_packet = gmpp_next_xmit_packet(role, GMP_PROTO_IGMP, intf_id,
					   packet_len);

	/* Bail if nothing to send. */

	if (!gen_packet)
	    break;

	gmpx_assert(gen_packet->gmp_packet_type < GMP_NUM_PACKET_TYPES);

	/* Got a packet.  Split based on the version number. */

	switch (gen_packet->gmp_packet_version) {

	  case GMP_VERSION_BASIC:
	    formatted_len = igmp_format_v1_packet(role, gen_packet, igmp_pkt,
						  packet_len);
	    break;

	  case GMP_VERSION_LEAVES:
	    formatted_len = igmp_format_v2_packet(role, gen_packet, igmp_pkt,
						  packet_len);
	    break;

	  case GMP_VERSION_SOURCES:
	    formatted_len = igmp_format_v3_packet(role, gen_packet, igmp_pkt,
						  packet_len);
	    break;

	  default:
	    gmpx_assert(FALSE);
	    break;
	}

	/*
	 * If the formatting routines returned a length of zero, it means
	 * that the packet was suppressed.  Loop back to try another one
	 * in that case, or break out if not.
	 */
	if (formatted_len)
	    break;

	/* Toss the packet that didn't build. */

	gmpp_packet_done(role, GMP_PROTO_IGMP, gen_packet);
    }

    /*
     * Checksum the packet and fill in the destination address field
     * if it's there.
     */
    if (formatted_len) {
	pkt_hdr = &igmp_pkt->igmp_pkt_naked.igmp_naked_header_hdr;
	checksum = gmpx_calculate_cksum(packet, formatted_len);
	pkt_hdr->igmp_hdr_cksum = checksum;
        memmove(dest_addr, gen_packet->gmp_packet_dest_addr.gmp_addr, IPV4_ADDR_LEN);

	/* Trace the packet. */

	gmp_igmp_trace_pkt(igmp_pkt, formatted_len, dest_addr, intf_id, FALSE,
			   trace_context, trace_flags);
    }

    /* Notify that we're done with the packet. */

    if (gen_packet)
	gmpp_packet_done(role, GMP_PROTO_IGMP, gen_packet);

    return formatted_len;
}


/*
 * igmp_parse_v1_packet
 *
 * Parse an IGMP V1 packet.
 *
 * Returns TRUE if the packet parsed OK, or FALSE if not.
 *
 * Fills in the generic packet structure as appropriate.
 */
static boolean
igmp_parse_v1_packet(igmp_packet *packet, gmp_packet *gen_packet,
		     uint32_t packet_len GMPX_UNUSED,
		     gmp_message_type msg_type)
{
    gmp_query_packet *query_pkt;
    gmp_report_packet *report_pkt;
    gmp_report_group_record *group_rcrd;
    igmp_v1v2_pkt *v1v2_pkt;

    /* Switch based on the message type. */

    v1v2_pkt = &packet->igmp_pkt_v1v2;

    switch (msg_type) {

      case GMP_QUERY_PACKET:

	/* Query packet.  Not much to do here except get the max resp value. */

	query_pkt = &gen_packet->gmp_packet_contents.gmp_packet_query;
	query_pkt->gmp_query_max_resp =
	    (igmp_max_resp_value(packet, GMP_VERSION_BASIC) *
	     IGMP_MAX_RESP_MSEC);
	break;

      case GMP_REPORT_PACKET:

	/*
	 * Report packet.  Grab a group record and fill it in.  We use
	 * an IS_EX record with a null address list to signify a Join.
	 */
	report_pkt = &gen_packet->gmp_packet_contents.gmp_packet_report;
	group_rcrd = gmpp_create_group_record(report_pkt, NULL,
					      v1v2_pkt->igmp_v1v2_pkt_group,
					      IPV4_ADDR_LEN);
	if (!group_rcrd)
	    return FALSE;
	group_rcrd->gmp_rpt_type = GMP_RPT_IS_EX;

	/* Complain if the group address is wrong. */

	if (!igmp_addr_is_mcast(group_rcrd->gmp_rpt_group.gmp_v4_addr))
	    return FALSE;

	break;

      default:
	gmpx_assert(FALSE);
    }

    return TRUE;
}


/*
 * igmp_parse_v2_packet
 *
 * Parse an IGMP V2 packet.
 *
 * Returns TRUE if the packet parsed OK, or FALSE if not.
 *
 * Fills in the generic packet structure as appropriate.
 */
static boolean
igmp_parse_v2_packet(igmp_packet *packet, gmp_packet *gen_packet,
		     uint32_t packet_len GMPX_UNUSED,
		     gmp_message_type msg_type)
{
    gmp_query_packet *query_pkt;
    gmp_report_packet *report_pkt;
    gmp_report_group_record *group_rcrd;
    igmp_v1v2_pkt *v1v2_pkt;
    uint32_t byte_offset;

    /* Switch based on the message type. */

    v1v2_pkt = &packet->igmp_pkt_v1v2;

    switch (msg_type) {

      case GMP_QUERY_PACKET:

	/* Query packet.  Get the max resp value. */

	query_pkt = &gen_packet->gmp_packet_contents.gmp_packet_query;
	query_pkt->gmp_query_max_resp =
	    (igmp_max_resp_value(packet, GMP_VERSION_LEAVES) *
	     IGMP_MAX_RESP_MSEC);

	/* Copy the group address. */

        memmove(query_pkt->gmp_query_group.gmp_addr, v1v2_pkt->igmp_v1v2_pkt_group, IPV4_ADDR_LEN);
	
	/*
	 * If the group address is nonzero, flag that we've got a
	 * group query.
	 */
	for (byte_offset = 0; byte_offset < IPV4_ADDR_LEN; byte_offset ++) {
	    if (v1v2_pkt->igmp_v1v2_pkt_group[byte_offset]) {
		query_pkt->gmp_query_group_query = TRUE;
		break;
	    }
	}

	break;

      case GMP_REPORT_PACKET:

	/* Report packet.  Grab a group record and fill it in. */

	report_pkt = &gen_packet->gmp_packet_contents.gmp_packet_report;
	group_rcrd = gmpp_create_group_record(report_pkt, NULL,
					      v1v2_pkt->igmp_v1v2_pkt_group,
					      IPV4_ADDR_LEN);
	if (!group_rcrd)
	    return FALSE;

	/* Complain if the group address is wrong. */

	if (!igmp_addr_is_mcast(group_rcrd->gmp_rpt_group.gmp_v4_addr))
	    return FALSE;

	/*
	 * If this is a Leave, use record type TO_IN with a null
	 * address list.  Otherwise (it's a Join) use IS_EX with a
	 * null address list.
	 */
	if (v1v2_pkt->igmp_v1v2_pkt_hdr.igmp_hdr_type == IGMP_TYPE_V2_LEAVE) {
	    group_rcrd->gmp_rpt_type = GMP_RPT_TO_IN;
	} else {
	    group_rcrd->gmp_rpt_type = GMP_RPT_IS_EX;
	}
	break;

      default:
	gmpx_assert(FALSE);
    }

    return TRUE;
}


/*
 * igmp_parse_v3_report_packet
 *
 * Parse an IGMP V3 Report packet.
 *
 * Returns TRUE if the packet parsed OK, or FALSE if not.
 *
 * Fills in the generic packet structure as appropriate.
 */
static boolean
igmp_parse_v3_report_packet(igmp_packet *packet, gmp_packet *gen_packet,
			    uint32_t packet_len)
{
    igmp_v3_report *v3_rpt_pkt;
    igmp_v3_rpt_rcrd *v3_rpt_rcrd;
    gmp_report_packet *report_pkt;
    gmp_report_group_record *group_rcrd;
    uint32_t group_count;
    uint32_t source_count;
    uint32_t record_length;
    uint8_t *byte_ptr;
    uint8_t *addr_ptr;
    gmp_addr_thread *addr_thread;

    /* Bail if the packet is too small. */

    if (packet_len < sizeof(igmp_v3_report))
	return FALSE;

    v3_rpt_pkt = &packet->igmp_pkt_v3_rpt;
    report_pkt = &gen_packet->gmp_packet_contents.gmp_packet_report;

    /* Extract the group count. */

    group_count = get_short(&v3_rpt_pkt->igmp_v3_report_num_rcrds);

    /* Loop for each group in the report. */

    packet_len -= sizeof(igmp_v3_report);
    v3_rpt_rcrd = v3_rpt_pkt->igmp_v3_report_rcrd;
    byte_ptr = (uint8_t *) v3_rpt_rcrd;
    while (group_count--) {

	/*
	 * Looks like we have a group record.  First make sure that there's
	 * enough left in the packet to examine the header.
	 */
	if (packet_len < sizeof(igmp_v3_rpt_rcrd))
	    return FALSE;

	/* Validate the record type. */

	if (v3_rpt_rcrd->igmp_v3_rpt_rec_type < GMP_RPT_MIN ||
	    v3_rpt_rcrd->igmp_v3_rpt_rec_type > GMP_RPT_MAX)
	    return FALSE;

	/* Extract the source count. */

	source_count = get_short(&v3_rpt_rcrd->igmp_v3_rpt_num_srcs);

	/* Make sure there's enough packet left to hold the whole record. */

	record_length = sizeof(igmp_v3_rpt_rcrd) +
	    (source_count * IPV4_ADDR_LEN) + v3_rpt_rcrd->igmp_v3_rpt_aux_len;
	if (packet_len < record_length)
	    return FALSE;

	/* Enough space.  Create a group record. */

	group_rcrd = gmpp_create_group_record(report_pkt, NULL,
					      v3_rpt_rcrd->igmp_v3_rpt_group,
					      IPV4_ADDR_LEN);
	if (!group_rcrd)
	    return FALSE;		/* Out of memory */

	/* Complain if the group address is wrong. */

	if (!igmp_addr_is_mcast(group_rcrd->gmp_rpt_group.gmp_v4_addr))
	    return FALSE;

	/* Stash the record type. */

	group_rcrd->gmp_rpt_type = v3_rpt_rcrd->igmp_v3_rpt_rec_type;

	/* If there are any sources, add them to the record. */

	if (source_count) {
	    addr_ptr = get_igmp_v3_rpt_source(v3_rpt_rcrd);

	    /* Allocate an address thread. */

	    addr_thread = gmp_alloc_addr_thread();
	    if (!addr_thread)
		return FALSE;		/* Out of memory */

	    while (source_count--) {

		/* Enqueue an address thread entry for the next address. */

		if (gmp_enqueue_addr_thread_addr(addr_thread, addr_ptr,
						 IPV4_ADDR_LEN) < 0) {
                    free(addr_thread);
		    return FALSE;	/* Out of memory */
                }
		addr_ptr += IPV4_ADDR_LEN;
	    }
	    group_rcrd->gmp_rpt_rcv_srcs = addr_thread;
	}

	/* Advance to the next group. */

	packet_len -= record_length;
	byte_ptr += record_length;
	v3_rpt_rcrd = (igmp_v3_rpt_rcrd *) byte_ptr;
    }

    return TRUE;
}


/*
 * igmp_parse_v3_query_packet
 *
 * Parse an IGMP V3 Query packet.
 *
 * Returns TRUE if the packet parsed OK, or FALSE if not.
 *
 * Fills in the generic packet structure as appropriate.
 */
static boolean
igmp_parse_v3_query_packet(igmp_packet *packet, gmp_packet *gen_packet,
			   uint32_t packet_len)
{
    igmp_v3_query *v3_query_pkt;
    gmp_query_packet *query_pkt;
    uint32_t byte_offset;
    gmp_addr_thread *addr_thread;
    uint32_t source_count;
    uint8_t *addr_ptr;

    /* Bail if the packet is too small. */

    if (packet_len < sizeof(igmp_v3_query))
	return FALSE;

    v3_query_pkt = &packet->igmp_pkt_v3_query;
    query_pkt = &gen_packet->gmp_packet_contents.gmp_packet_query;

    /* Query packet.  Get the max resp value. */

    query_pkt->gmp_query_max_resp =
	igmp_max_resp_value(packet, GMP_VERSION_SOURCES) * IGMP_MAX_RESP_MSEC;

    /* Copy the group address. */

    memmove(query_pkt->gmp_query_group.gmp_addr, v3_query_pkt->igmp_v3_query_group, IPV4_ADDR_LEN);

    /*
     * If the group address is nonzero, flag that we've got a
     * group query.
     */
    for (byte_offset = 0; byte_offset < IPV4_ADDR_LEN; byte_offset ++) {
	if (v3_query_pkt->igmp_v3_query_group[byte_offset]) {
	    query_pkt->gmp_query_group_query = TRUE;
	    break;
	}
    }

    /* Parse the S and QRV fields. */

    query_pkt->gmp_query_suppress =
	((v3_query_pkt->igmp_v3_query_s_qrv & IGMP_SUPP_RTR_PROC_MASK) != 0);
    query_pkt->gmp_query_qrv =
	v3_query_pkt->igmp_v3_query_s_qrv & IGMP_QRV_MASK;

    /* Parse the QQIC field. */

    query_pkt->gmp_query_qqi =
	igmp_decode_fixfloat(v3_query_pkt->igmp_v3_query_qqic) * MSECS_PER_SEC;

    /*
     * Parse the source count field.  If nonzero and this is a general query,
     * flag a parse error.
     */
    source_count = get_short(&v3_query_pkt->igmp_v3_query_num_srcs);
    if (source_count && !query_pkt->gmp_query_group_query)
	return FALSE;

    packet_len -= sizeof(igmp_v3_query);
    
    /*
     * If the remaining packet length doesn't match the count of sources, flag
     * a parse error.
     */
    if (packet_len != (source_count * IPV4_ADDR_LEN))
	return FALSE;

    /*
     * Looks kosher.  Now pull the list of source addresses into an
     * address thread, if they are there.
     */
    if (source_count) {
	addr_ptr = v3_query_pkt->igmp_v3_query_source;
	addr_thread = gmp_alloc_addr_thread();
	query_pkt->gmp_query_rcv_srcs = addr_thread;
	if (!addr_thread)
	    return FALSE;		/* Out of memory */
	while (source_count--) {
	    if (gmp_enqueue_addr_thread_addr(addr_thread, addr_ptr,
					     IPV4_ADDR_LEN) < 0) {
		return FALSE;		/* Out of memory */
	    }
	    addr_ptr += IPV4_ADDR_LEN;
	}
    }

    return TRUE;
}


/*
 * igmp_parse_v3_packet
 *
 * Parse an IGMP V3 packet.
 *
 * Returns TRUE if the packet parsed OK, or FALSE if not.
 *
 * Fills in the generic packet structure as appropriate.
 */
static boolean
igmp_parse_v3_packet(igmp_packet *packet, gmp_packet *gen_packet,
		     uint32_t packet_len, gmp_message_type msg_type)
{
    boolean result;

    /* Switch based on message type. */

    switch (msg_type) {

      case GMP_QUERY_PACKET:
	result = igmp_parse_v3_query_packet(packet, gen_packet, packet_len);
	break;


      case GMP_REPORT_PACKET:
	result = igmp_parse_v3_report_packet(packet, gen_packet, packet_len);
	break;

      default:
	gmpx_assert(FALSE);
	result = FALSE;			/* Quiet the compiler. */
	break;

    }

    return result;
}


/*
 * igmp_process_pkt
 *
 * Process a received IGMP packet.  We parse it into generic form and pass it
 * to the clients.
 *
 * Returns TRUE if the packet parsed OK, or FALSE if there was a problem.
 */
boolean
igmp_process_pkt (void *rcv_pkt, const uint8_t *src_addr,
		  const uint8_t *dest_addr, uint32_t packet_len,
		  gmpx_intf_id intf_id, gmpx_packet_attr attrib,
		  void *trace_context, uint32_t trace_flags)
{
    igmp_packet *packet;
    gmp_packet *gen_packet;
    gmp_version version;
    gmp_message_type msg_type;
    boolean parse_ok;

    packet = rcv_pkt;
    parse_ok = TRUE;

    /* Bail if the packet is too small. */

    if (packet_len < sizeof(igmp_naked_header)) {
	parse_ok = FALSE;
    }

    /*
     * Bail if the version is bogus or the packet is otherwise
     * unrecognizable.
     */
    version = igmp_generic_version(packet, packet_len, &msg_type);
    if (version == GMP_VERSION_INVALID) {
	parse_ok = FALSE;
    }

    /*
     * Looks like we have a recognizable header, message type, and version.
     * Create a basic generic packet of the appropriate type.
     */
    gen_packet = gmpp_create_packet_header(version, msg_type, GMP_PROTO_IGMP);
    if (!gen_packet)
	return FALSE;			/* Out of memory */

    /* Set up the address fields. */

    if (src_addr) {
        memmove(gen_packet->gmp_packet_src_addr.gmp_addr, src_addr, IPV4_ADDR_LEN);
    }

    if (dest_addr) {
        memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, dest_addr, IPV4_ADDR_LEN);
    }

    /* If the packet looks OK so far, parse it further. */

    if (parse_ok) {

	/* Save the attribute. */

	gen_packet->gmp_packet_attr = attrib;

	/* Split off by version and by packet type for further parsing. */

	switch (version) {
	  case GMP_VERSION_BASIC:

	    /* IGMPv1.  Parse it. */

	    parse_ok = igmp_parse_v1_packet(packet, gen_packet, packet_len,
					    msg_type);
	    break;

	  case GMP_VERSION_LEAVES:

	    /* IGMPv2.  Parse it. */

	    parse_ok = igmp_parse_v2_packet(packet, gen_packet, packet_len,
					    msg_type);
	    break;

	  case GMP_VERSION_SOURCES:

	    /* IGMPv3.  Parse it. */

	    parse_ok = igmp_parse_v3_packet(packet, gen_packet, packet_len,
					    msg_type);
	    break;

	  default:
	    gmpx_assert(FALSE);
	    break;
	}
    }

    /*
     * If the packet parsed OK, trace it and then pass it along.
     * Otherwise, complain.
     */
    if (parse_ok) {
	gmp_igmp_trace_pkt(packet, packet_len, src_addr, intf_id, TRUE,
			   trace_context, trace_flags);
	gmpp_process_rcv_packet(gen_packet, intf_id);
    } else {
	gmp_igmp_trace_bad_pkt(packet_len, src_addr, intf_id, trace_context,
			       trace_flags);
    }

    /* Toss the parsed packet. */

    gmpp_destroy_packet(gen_packet);

    return parse_ok;
}
