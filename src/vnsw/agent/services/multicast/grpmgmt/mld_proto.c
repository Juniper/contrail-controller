/* $Id: mld_proto.c 346474 2009-11-14 10:18:58Z ssiano $
 *
 * mld_proto.c - MLD protocol encode/decode routines
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
#include "mld_proto.h"

/*
 * Destination addresses
 */
static u_int8_t mld_all_hosts[IPV6_ADDR_LEN] = 
    {0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};
static u_int8_t mld_all_routers[IPV6_ADDR_LEN] =
    {0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x02};
static u_int8_t mld_all_v2_routers[IPV6_ADDR_LEN] =
    {0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x16};


/*
 * mld_decode_maxresp
 *
 * Decodes the Max Resp field.  Returns the value in native units.
 */
static u_int
mld_decode_maxresp (u_int16_t value)
{
    u_int mant, exp, result;

    /* If the flag is set, decode the value as floating point. */

    if (value & MLD_MAXRSP_FIXFLOAT_FLAG) {
	mant = (value & MLD_MAXRSP_MANT_MASK) >> MLD_MAXRSP_MANT_SHIFT;
	exp = (value & MLD_MAXRSP_EXP_MASK) >> MLD_MAXRSP_EXP_SHIFT;
	result = (mant | MLD_MAXRSP_MANT_HIGHBIT) <<
	    (exp + MLD_MAXRSP_EXP_OFFSET);
    } else {
	result = value;
    }

    return result;
}


/*
 * mld_decode_qqic
 *
 * Decodes the QQIC field.  Returns the value in native units.
 */
static u_int
mld_decode_qqic (u_int8_t value)
{
    u_int mant, exp, result;

    /* If the flag is set, decode the value as floating point. */

    if (value & MLD_QQIC_FIXFLOAT_FLAG) {
	mant = (value & MLD_QQIC_MANT_MASK) >> MLD_QQIC_MANT_SHIFT;
	exp = (value & MLD_QQIC_EXP_MASK) >> MLD_QQIC_EXP_SHIFT;
	result = (mant | MLD_QQIC_MANT_HIGHBIT) << (exp + MLD_QQIC_EXP_OFFSET);
    } else {
	result = value;
    }

    return result;
}


/*
 * mld_encode_maxresp
 *
 * Encodes the Max Resp field, given the value in native units.
 * Returns the encoded value, or zero if the value is too large to
 * encode.
 *
 * The encoded value will be less than or equal to the provided value
 * (since we lose granularity in the encoding.)
 */
static u_int16_t
mld_encode_maxresp (u_int value)
{
    u_int mant, exp;

    /* If the value is small enough, just use it. */

    if (value < MLD_MAXRSP_FIXFLOAT_FLAG)
	return (u_int16_t) value;

    /* If the value is too big, bail. */

    if (value > MLD_MAX_MAXRSP_ENCODABLE)
	return 0;

    /* Big enough to encode.  Do the job. */

    mant = value >> MLD_MAXRSP_EXP_OFFSET;
    for (exp = 0; exp <= MLD_MAXRSP_MAX_EXP; exp++) {

	/* Bail if the mantissa is small enough now. */

	if (mant <= MLD_MAXRSP_MAX_MANT)
	    break;

	/* Shift the mantissa down a bit and try again. */

	mant >>= 1;
    }

    /* Got it.  Form the value and return it. */

    return (MLD_MAXRSP_FIXFLOAT_FLAG | (exp << MLD_MAXRSP_EXP_SHIFT) |
	    (mant & MLD_MAXRSP_MANT_MASK));
}


/*
 * mld_encode_qqic
 *
 * Encodes the QQIC field, given the value in native units.  Returns
 * the encoded value, or zero if the value is too large to encode.
 *
 * The encoded value will be less than or equal to the provided value
 * (since we lose granularity in the encoding.)
 */
static u_int8_t
mld_encode_qqic (u_int value)
{
    u_int mant, exp;

    /* If the value is small enough, just use it. */

    if (value < MLD_QQIC_FIXFLOAT_FLAG)
	return (u_int8_t) value;

    /* If the value is too big, bail. */

    if (value > MLD_MAX_QQIC_ENCODABLE)
	return 0;

    /* Big enough to encode.  Do the job. */

    mant = value >> MLD_QQIC_EXP_OFFSET;
    for (exp = 0; exp <= MLD_QQIC_MAX_EXP; exp++) {

	/* Bail if the mantissa is small enough now. */

	if (mant <= MLD_QQIC_MAX_MANT)
	    break;

	/* Shift the mantissa down a bit and try again. */

	mant >>= 1;
    }

    /* Got it.  Form the value and return it. */

    return (MLD_QQIC_FIXFLOAT_FLAG | (exp << MLD_QQIC_EXP_SHIFT) |
	    (mant & MLD_QQIC_MANT_MASK));
}


/*
 * mld_generic_version
 *
 * Determine the generic version number of an MLD packet
 *
 * Returns the version number, or GMP_VERSION_INVALID if an error occurs.
 *
 * Also returns the generic message type.
 */
static gmp_version
mld_generic_version (mld_packet *pkt, u_int pkt_len,
		     gmp_message_type *msg_type)
{
    gmp_version version;

    /* First, branch by packet type. */

    *msg_type = GMP_REPORT_PACKET;	/* Reasonable default. */
    switch (pkt->mld_pkt_naked.mld_naked_header_hdr.mld_hdr_type) {

	/* Do the easy ones first. */

      case MLD_TYPE_V1_REPORT:
      case MLD_TYPE_V1_LEAVE:
	version = GMP_VERSION_LEAVES;
	break;

      case MLD_TYPE_V2_REPORT:
	version = GMP_VERSION_SOURCES;
	break;

      case MLD_TYPE_QUERY:

	/* Now the queries.  Queries are distinguished by length. */

	*msg_type = GMP_QUERY_PACKET;
	if (pkt_len > sizeof(mld_v1_pkt)) /* Long packet */
	    version = GMP_VERSION_SOURCES;
	else
	    version = GMP_VERSION_LEAVES;
	break;

      default:
	version = GMP_VERSION_INVALID;
	break;
    }

    return version;
}


/*
 * mld_max_resp_value
 *
 * Returns the value of the max resp field, in standard units (1 msec.)
 */
static u_int mld_max_resp_value (mld_packet *pkt, gmp_version version)
{
    u_int max_resp_field, value;

    /* Get the field.  It's in the same place for V1 and V2. */

    max_resp_field =
	get_short(&pkt->mld_pkt_naked.mld_naked_max_resp);

    /* Switch by MLD version. */

    switch (version) {
      case GMP_VERSION_LEAVES:
	value = max_resp_field;
	break;

      case GMP_VERSION_SOURCES:
	value = mld_decode_maxresp(max_resp_field);
	break;

      default:
	gmpx_assert(FALSE);
	value = 0;			/* Quiet the compiler */
    }

    return value;
}


/*
 * mld_packet_type_string
 *
 * Returns the MLD packet type string given the packet type.
 */
static const char *
mld_packet_type_string (mld_hdr *hdr, u_int len)
{
    switch (hdr->mld_hdr_type) {
      case MLD_TYPE_QUERY:
	if (len > sizeof(mld_v1_pkt))
	    return "V2 Query";
	return "V1 Query";

      case MLD_TYPE_V1_REPORT:
	return "V1 Report";

      case MLD_TYPE_V1_LEAVE:
	return "V1 Leave";

      case MLD_TYPE_V2_REPORT:
	return "V2 Report";

      default:
	return "Invalid";
    }
}


/*
 * gmp_mld_trace_bad_pkt
 *
 * Trace a bad MLD packet
 */
void
gmp_mld_trace_bad_pkt(u_int len, const u_int8_t *addr, gmpx_intf_id intf_id,
		      void *trace_context, u_int32_t trace_flags)
{
    /* Bail if not tracing bad packets. */

    if (!gmp_trace_set(trace_flags, GMP_TRACE_PACKET_BAD))
	return;

    gmpx_trace(trace_context, "RCV bad MLD packet, len %u, from %a intf %i",
	       len, addr, intf_id);
}


/*
 * gmp_mld_trace_pkt
 * 
 * Trace an MLD packet
 */
void
gmp_mld_trace_pkt (void *packet, u_int len, const u_int8_t *addr,
		   gmpx_intf_id intf_id, boolean receive,
		   void *trace_context, u_int32_t trace_flags)
{
    const char *direction;
    const char *op;
    mld_hdr *hdr;
    mld_v1_pkt *v1_pkt;
    mld_v2_query *v2_query;
    mld_v2_report *v2_report;
    mld_v2_rpt_rcrd *rpt_rcrd;
    gmp_version version;
    gmp_message_type msg_type;
    u_int8_t *byte_ptr;
    u_int source_count;
    u_int record_count;
    boolean detail;
    mld_packet *pkt;

    pkt = packet;

    /* See if this packet should be traced at all. */

    if (!gmp_trace_set(trace_flags, GMP_TRACE_PACKET))
	return;

    hdr = &pkt->mld_pkt_naked.mld_naked_header_hdr;
    detail = FALSE;
    switch (hdr->mld_hdr_type) {
      case MLD_TYPE_QUERY:
	if (!gmp_trace_set(trace_flags, GMP_TRACE_QUERY))
	    return;
	if (gmp_trace_set(trace_flags, GMP_TRACE_QUERY_DETAIL))
	    detail = TRUE;
	break;

      case MLD_TYPE_V1_LEAVE:
	if (!gmp_trace_set(trace_flags, GMP_TRACE_LEAVE))
	    return;
	if (gmp_trace_set(trace_flags, GMP_TRACE_LEAVE_DETAIL))
	    detail = TRUE;
	break;

      case MLD_TYPE_V1_REPORT:
      case MLD_TYPE_V2_REPORT:
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

    v1_pkt = &pkt->mld_pkt_v1;
    v2_query = &pkt->mld_pkt_v2_query;
    v2_report = &pkt->mld_pkt_v2_rpt;
    version = mld_generic_version(pkt, len, &msg_type);

    if (receive) {
	direction = "from";
	op = "RCV";
    } else {
	direction = "to";
	op = "XMT";
    }
    gmpx_trace(trace_context, "%s MLD %s len %u %s %a intf %i", op,
	       mld_packet_type_string(hdr, len), len, direction, addr,
	       intf_id);

    /* Bail if no detailed tracing going on. */

    if (!detail)
	return;

    /* Do any detailed tracing based on packet type. */

    switch (hdr->mld_hdr_type) {
      case MLD_TYPE_QUERY:
	gmpx_trace(trace_context, "  Max_resp 0x%x (%u), group %a",
		   v1_pkt->mld_v1_max_resp, mld_max_resp_value(pkt, version),
		   v1_pkt->mld_v1_pkt_group);
	if (version == GMP_VERSION_SOURCES) {
	    source_count = get_short(&v2_query->mld_v2_query_num_srcs);
	    gmpx_trace(trace_context,
		       "  S %u, QRV %u, QQIC 0x%x (%u), sources %u",
		       ((v2_query->mld_v2_query_s_qrv &
			 MLD_SUPP_RTR_PROC_MASK) != 0),
		       v2_query->mld_v2_query_s_qrv & MLD_QRV_MASK,
		       v2_query->mld_v2_query_qqic,
		       mld_decode_qqic(v2_query->mld_v2_query_qqic),
		       source_count);
	    byte_ptr = v2_query->mld_v2_query_source;
	    while (source_count--) {
		gmpx_trace(trace_context, "    Source %a", byte_ptr);
		byte_ptr += IPV6_ADDR_LEN;
	    }
	}
	break;

      case MLD_TYPE_V1_REPORT:
      case MLD_TYPE_V1_LEAVE:
	gmpx_trace(trace_context, "  Group %a", v1_pkt->mld_v1_pkt_group);
	break;

      case MLD_TYPE_V2_REPORT:
	record_count = get_short(&v2_report->mld_v2_report_num_rcrds);
	gmpx_trace(trace_context, "  Records %u", record_count);

	rpt_rcrd = v2_report->mld_v2_report_rcrd;
	while (record_count--) {
	    source_count = get_short(&rpt_rcrd->mld_v2_rpt_num_srcs);
	    gmpx_trace(trace_context,
		       "    Group %a, type %s, aux_len %u, sources %u",
		       rpt_rcrd->mld_v2_rpt_group,
		       gmp_report_type_string(rpt_rcrd->mld_v2_rpt_rec_type),
		       rpt_rcrd->mld_v2_rpt_aux_len, source_count);
	    byte_ptr = get_mld_v2_rpt_source(rpt_rcrd);
	    while (source_count--) {
		gmpx_trace(trace_context, "      Source %a", byte_ptr);
		byte_ptr += IPV6_ADDR_LEN;
	    }
	    byte_ptr += rpt_rcrd->mld_v2_rpt_aux_len;
	    rpt_rcrd = (mld_v2_rpt_rcrd *) byte_ptr;
	}
	break;

    }
}


/*
 * mld_format_v1_packet
 *
 * Format an MLD V1 packet.
 *
 * Returns the formatted packet length, or 0 if nothing to send.
 */
static u_int
mld_format_v1_packet (gmp_role role, gmp_packet *gen_packet,
		       mld_packet *packet, u_int packet_len)
{
    mld_v1_pkt *v1_pkt;
    mld_hdr *pkt_hdr;
    gmp_query_packet *query_pkt;
    gmp_report_packet *report_pkt;
    thread *thread_ptr;
    gmp_report_group_record *group_record;
    u_int32_t max_resp;
    u_int formatted_len;

    /* Initialize and idiot-proof. */

    gmpx_assert(packet_len >= sizeof(mld_v1_pkt));
    v1_pkt = &packet->mld_pkt_v1;
    pkt_hdr = &v1_pkt->mld_v1_pkt_hdr;
    memset(pkt_hdr, 0, sizeof(mld_hdr));
    formatted_len = sizeof(mld_v1_pkt);

    switch (gen_packet->gmp_packet_type) {

      case GMP_QUERY_PACKET:

	/* Format a query packet. */

	pkt_hdr->mld_hdr_type = MLD_TYPE_QUERY;
	query_pkt = &gen_packet->gmp_packet_contents.gmp_packet_query;

	/*
	 * If this is a group query, copy in the group address.  Otherwise,
	 * zero it.  Set the destination address accordingly.
	 */
	if (query_pkt->gmp_query_group_query) {
            memmove(v1_pkt->mld_v1_pkt_group, query_pkt->gmp_query_group.gmp_addr, IPV6_ADDR_LEN);
            memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, query_pkt->gmp_query_group.gmp_addr,
                IPV6_ADDR_LEN);
	} else {
            memset(v1_pkt->mld_v1_pkt_group, 0, IPV6_ADDR_LEN);
            memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, mld_all_hosts, IPV6_ADDR_LEN);
	}

	/* Store the max resp value. */

	max_resp = query_pkt->gmp_query_max_resp / MLD_MAX_RESP_MSEC;
	if (max_resp > MLD_V1_MAX_MAX_RESP)
	    max_resp = MLD_V1_MAX_MAX_RESP;
	put_short(&v1_pkt->mld_v1_max_resp, max_resp);
	if (query_pkt->gmp_query_group_query) {
	    gmpp_group_done(role, GMP_PROTO_MLD,
			    query_pkt->gmp_query_group_id);
	}

	break;

      case GMP_REPORT_PACKET:
	    
	/*
	 * Format a report packet.  Note that there may be multiple
	 * group records on this (since V2 supports that).  We just
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
	    pkt_hdr->mld_hdr_type = MLD_TYPE_V1_LEAVE;
            memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, mld_all_routers, IPV6_ADDR_LEN);
	} else {
	    pkt_hdr->mld_hdr_type = MLD_TYPE_V1_REPORT;
            memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, group_record->gmp_rpt_group.gmp_addr,
                IPV6_ADDR_LEN);
	}

        memmove(v1_pkt->mld_v1_pkt_group, group_record->gmp_rpt_group.gmp_addr, IPV6_ADDR_LEN);

	/* If this is a BLOCK record, don't send anything. */

	if (group_record->gmp_rpt_type == GMP_RPT_BLOCK)
	    formatted_len = 0;

	/* Flush the source list and flag that we're done with this group. */

	gmp_flush_xmit_list(group_record->gmp_rpt_xmit_srcs);
	gmpp_group_done(role, GMP_PROTO_MLD, group_record->gmp_rpt_group_id);
	break;

      default:
	gmpx_assert(FALSE);
    }

    return formatted_len;
}


/*
 * mld_format_v2_query
 *
 * Format an MLDv2 Query packet.
 *
 * Returns the formatted packet length, or 0 if nothing to send.
 */
static u_int
mld_format_v2_query (gmp_role role, gmp_packet *gen_packet,
		      mld_packet *packet, u_int packet_len)
{
    mld_v2_query *v2_query_pkt;
    mld_hdr *pkt_hdr;
    gmp_query_packet *query_pkt;
    u_int formatted_len;
    u_int source_count;
    gmp_addr_list_entry *addr_entry;
    gmp_addr_list *addr_list;
    u_int8_t *addr_ptr;
    gmp_addr_cat_entry *cat_entry;
    boolean gss_query;
    boolean group_done;

    /* Format a query packet. */

    gmpx_assert(packet_len >= sizeof(mld_v2_query) + IPV6_ADDR_LEN);
    query_pkt = &gen_packet->gmp_packet_contents.gmp_packet_query;
    v2_query_pkt = &packet->mld_pkt_v2_query;
    memset(v2_query_pkt, 0, sizeof(mld_v2_query));
    pkt_hdr = &v2_query_pkt->mld_v2_query_hdr;

    /* Set up the header. */

    pkt_hdr->mld_hdr_type = MLD_TYPE_QUERY;
    put_short(&v2_query_pkt->mld_v2_max_resp,
	mld_encode_maxresp(query_pkt->gmp_query_max_resp / MLD_MAX_RESP_MSEC));

    /* Note whether this is a GSS query. */

    gss_query = query_pkt->gmp_query_group_query &&
	query_pkt->gmp_query_xmit_srcs;

    /*
     * If this is a group query, copy in the group address.  Otherwise,
     * zero it.  Set the destination address accordingly.
     */
    if (query_pkt->gmp_query_group_query) {
        memmove(v2_query_pkt->mld_v2_query_group, query_pkt->gmp_query_group.gmp_addr,
            IPV6_ADDR_LEN);
        memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, query_pkt->gmp_query_group.gmp_addr,
            IPV6_ADDR_LEN);
    } else {
        memset(v2_query_pkt->mld_v2_query_group, 0, IPV6_ADDR_LEN);
        memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, mld_all_hosts, IPV6_ADDR_LEN);
    }

    /* Set the S and QRV fields. */

    v2_query_pkt->mld_v2_query_s_qrv = query_pkt->gmp_query_qrv;
    if (query_pkt->gmp_query_suppress)
	v2_query_pkt->mld_v2_query_s_qrv |= MLD_SUPP_RTR_PROC_MASK;

    /* Set the QQIC field. */

    v2_query_pkt->mld_v2_query_qqic =
	mld_encode_qqic(query_pkt->gmp_query_qqi / MSECS_PER_SEC);

    formatted_len = sizeof(mld_v2_query);
    packet_len -= sizeof(mld_v2_query);
    source_count = 0;
    addr_ptr = v2_query_pkt->mld_v2_query_source;

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

	    if (packet_len < IPV6_ADDR_LEN) {
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
            memmove(addr_ptr, cat_entry->adcat_ent_addr.gmp_addr, IPV6_ADDR_LEN);

	    /* Delink the address entry. */

	    gmp_dequeue_xmit_addr_entry(addr_entry);

	    /* Do housekeeping. */

	    addr_ptr += IPV6_ADDR_LEN;
	    formatted_len += IPV6_ADDR_LEN;
	    packet_len -= IPV6_ADDR_LEN;
	    source_count++;
	}

	/* If we're done with the group, call back. */

	if (group_done) {
	    gmpp_group_done(role, GMP_PROTO_MLD,
			    query_pkt->gmp_query_group_id);
	}

    }

    /* Update the source count. */

    put_short(&v2_query_pkt->mld_v2_query_num_srcs, source_count);

    return formatted_len;
}

/*
 * mld_format_v2_report
 *
 * Format an MLDv2 Report packet.
 *
 * Returns the formatted packet length, or 0 if nothing to send.
 */
static u_int
mld_format_v2_report (gmp_role role, gmp_packet *gen_packet,
		       mld_packet *packet, u_int packet_len)
{
    mld_v2_report *v2_rpt_pkt;
    mld_v2_rpt_rcrd *v2_group_rcrd;
    mld_hdr *pkt_hdr;
    gmp_report_packet *report_pkt;
    gmp_report_group_record *group_record;
    u_int formatted_len;
    u_int source_count;
    u_int group_count;
    u_int groups_remaining;
    u_int group_space;
    gmp_addr_list_entry *addr_entry;
    gmp_addr_list *addr_list;
    u_int8_t *byte_ptr;
    gmp_addr_cat_entry *cat_entry;
    thread *thread_ptr;
    boolean group_done;

    /* Format a report packet. */

    gmpx_assert(packet_len >= sizeof(mld_v2_report) +
		sizeof(mld_v2_rpt_rcrd) + IPV6_ADDR_LEN);
    report_pkt = &gen_packet->gmp_packet_contents.gmp_packet_report;
    v2_rpt_pkt = &packet->mld_pkt_v2_rpt;
    memset(v2_rpt_pkt, 0, sizeof(mld_v2_report));
    pkt_hdr = &v2_rpt_pkt->mld_v2_report_hdr;

    /* Set up the header. */

    pkt_hdr->mld_hdr_type = MLD_TYPE_V2_REPORT;

    packet_len -= sizeof(mld_v2_report);
    formatted_len = sizeof(mld_v2_report);
    group_count = 0;
    v2_group_rcrd = v2_rpt_pkt->mld_v2_report_rcrd;
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
	group_space = sizeof(mld_v2_rpt_rcrd);
	if (group_record->gmp_rpt_xmit_srcs) {
	    group_space += group_record->gmp_rpt_xmit_srcs->xmit_addr_count *
		IPV6_ADDR_LEN;
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

	memset(v2_group_rcrd, 0, sizeof(mld_v2_rpt_rcrd));
	v2_group_rcrd->mld_v2_rpt_rec_type = group_record->gmp_rpt_type;
        memmove(v2_group_rcrd->mld_v2_rpt_group, group_record->gmp_rpt_group.gmp_addr,
            IPV6_ADDR_LEN);
	packet_len -= sizeof(mld_v2_rpt_rcrd);
	formatted_len += sizeof(mld_v2_rpt_rcrd);
	byte_ptr = get_mld_v2_rpt_source(v2_group_rcrd);
	group_count++;

	/* Now walk the sources, if any, and add them to the record. */

	source_count = 0;
	addr_list = group_record->gmp_rpt_xmit_srcs;
	group_done = TRUE;
	if (addr_list) {
	    while (TRUE) {

		/* Bail if out of space in the packet. */

		if (packet_len < IPV6_ADDR_LEN)
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
                memmove(byte_ptr, cat_entry->adcat_ent_addr.gmp_addr, IPV6_ADDR_LEN);

		/* Delink the address entry. */

		gmp_dequeue_xmit_addr_entry(addr_entry);

		/* Do housekeeping. */

		byte_ptr += IPV6_ADDR_LEN;
		formatted_len += IPV6_ADDR_LEN;
		packet_len -= IPV6_ADDR_LEN;
		source_count++;
	    }

	    /* Update the source count in the group record. */

	    put_short(&v2_group_rcrd->mld_v2_rpt_num_srcs, source_count);

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
	    gmpp_group_done(role, GMP_PROTO_MLD,
			    group_record->gmp_rpt_group_id);
	}

	/* Advance the group record pointer. */

	v2_group_rcrd = (mld_v2_rpt_rcrd *) byte_ptr;
	groups_remaining--;
    }

    /* Set the destination address. */

    memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, mld_all_v2_routers, IPV6_ADDR_LEN);

    /* Update the group count in the packet header. */

    put_short(&v2_rpt_pkt->mld_v2_report_num_rcrds, group_count);

    return formatted_len;
}


/*
 * mld_format_v2_packet
 *
 * Format an MLD V2 packet.
 *
 * Returns the formatted packet length, or 0 if nothing to send.
 */
static u_int
mld_format_v2_packet (gmp_role role, gmp_packet *gen_packet,
		       mld_packet *packet, u_int packet_len)
{
    u_int formatted_len;

    /* See what kind of packet we're sending. */

    switch (gen_packet->gmp_packet_type) {

      case GMP_QUERY_PACKET:
	formatted_len = mld_format_v2_query(role, gen_packet, packet,
					     packet_len);

	break;


      case GMP_REPORT_PACKET:
	formatted_len = mld_format_v2_report(role, gen_packet, packet,
					      packet_len);
	break;

      default:
	gmpx_assert(FALSE);
	formatted_len = 0;		/* Quiet the compiler. */
    }

    return formatted_len;
}


/*
 * mld_next_xmit_packet
 *
 * Format the next MLD packet to transmit for a role (host/router).
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
u_int
mld_next_xmit_packet (gmp_role role, gmpx_intf_id intf_id, void *packet,
		      u_int8_t *dest_addr, u_int packet_len,
		      void *trace_context, u_int32_t trace_flags)
{
    gmp_packet *gen_packet;
    mld_packet *mld_pkt;
    u_int formatted_len;

    mld_pkt = packet;
    formatted_len = 0;

    while (TRUE) {

	/* Get the next generic packet. */

	gen_packet = gmpp_next_xmit_packet(role, GMP_PROTO_MLD, intf_id,
					   packet_len);

	/* Bail if nothing to send. */

	if (!gen_packet)
	    break;

	gmpx_assert(gen_packet->gmp_packet_type < GMP_NUM_PACKET_TYPES);

	/* Got a packet.  Split based on the version number. */

	switch (gen_packet->gmp_packet_version) {

	  case GMP_VERSION_LEAVES:
	    formatted_len = mld_format_v1_packet(role, gen_packet, mld_pkt,
						 packet_len);
	    break;

	  case GMP_VERSION_SOURCES:
	    formatted_len = mld_format_v2_packet(role, gen_packet, mld_pkt,
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

	gmpp_packet_done(role, GMP_PROTO_MLD, gen_packet);
    }

    /*
     * Fill in the destination address field.  We let the I/O wrapper
     * actually fill in the checksum, since we don't really know the
     * source address, which is part of the v6 checksum.
     */
    if (formatted_len && dest_addr) {
        memmove(dest_addr, gen_packet->gmp_packet_dest_addr.gmp_addr, IPV6_ADDR_LEN);

	/* Trace the packet. */

	gmp_mld_trace_pkt(mld_pkt, formatted_len, dest_addr, intf_id, FALSE,
			  trace_context, trace_flags);
    }

    /* Notify that we're done with the packet. */

    if (gen_packet)
	gmpp_packet_done(role, GMP_PROTO_MLD, gen_packet);

    return formatted_len;
}


/*
 * mld_parse_v1_packet
 *
 * Parse an MLD V1 packet.
 *
 * Returns TRUE if the packet parsed OK, or FALSE if not.
 *
 * Fills in the generic packet structure as appropriate.
 */
static boolean
mld_parse_v1_packet(mld_packet *packet, gmp_packet *gen_packet,
		     u_int32_t packet_len GMPX_UNUSED,
		     gmp_message_type msg_type)
{
    gmp_query_packet *query_pkt;
    gmp_report_packet *report_pkt;
    gmp_report_group_record *group_rcrd;
    mld_v1_pkt *v1_pkt;
    u_int byte_offset;

    /* Switch based on the message type. */

    v1_pkt = &packet->mld_pkt_v1;

    switch (msg_type) {

      case GMP_QUERY_PACKET:

	/* Query packet.  Get the max resp value. */

	query_pkt = &gen_packet->gmp_packet_contents.gmp_packet_query;
	query_pkt->gmp_query_max_resp =
	    (mld_max_resp_value(packet, GMP_VERSION_LEAVES) *
	     MLD_MAX_RESP_MSEC);

	/* Copy the group address. */

        memmove(query_pkt->gmp_query_group.gmp_addr, v1_pkt->mld_v1_pkt_group, IPV6_ADDR_LEN);
	
	/*
	 * If the group address is nonzero, flag that we've got a
	 * group query.
	 */
	for (byte_offset = 0; byte_offset < IPV6_ADDR_LEN; byte_offset ++) {
	    if (v1_pkt->mld_v1_pkt_group[byte_offset]) {
		query_pkt->gmp_query_group_query = TRUE;
		break;
	    }
	}

	break;

      case GMP_REPORT_PACKET:

	/* Report packet.  Grab a group record and fill it in. */

	report_pkt = &gen_packet->gmp_packet_contents.gmp_packet_report;
	group_rcrd = gmpp_create_group_record(report_pkt, NULL,
					      v1_pkt->mld_v1_pkt_group,
					      IPV6_ADDR_LEN);
	if (!group_rcrd)
	    return FALSE;

	/*
	 * If this is a Leave, use record type TO_IN with a null
	 * address list.  Otherwise (it's a Join) use IS_EX with a
	 * null address list.
	 */
	if (v1_pkt->mld_v1_pkt_hdr.mld_hdr_type == MLD_TYPE_V1_LEAVE) {
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
 * mld_parse_v2_report_packet
 *
 * Parse an MLD V2 Report packet.
 *
 * Returns TRUE if the packet parsed OK, or FALSE if not.
 *
 * Fills in the generic packet structure as appropriate.
 */
static boolean
mld_parse_v2_report_packet(mld_packet *packet, gmp_packet *gen_packet,
			    u_int32_t packet_len)
{
    mld_v2_report *v2_rpt_pkt;
    mld_v2_rpt_rcrd *v2_rpt_rcrd;
    gmp_report_packet *report_pkt;
    gmp_report_group_record *group_rcrd;
    u_int group_count;
    u_int source_count;
    u_int record_length;
    u_int8_t *byte_ptr;
    u_int8_t *addr_ptr;
    gmp_addr_thread *addr_thread;

    /* Bail if the packet is too small. */

    if (packet_len < sizeof(mld_v2_report))
	return FALSE;

    v2_rpt_pkt = &packet->mld_pkt_v2_rpt;
    report_pkt = &gen_packet->gmp_packet_contents.gmp_packet_report;

    /* Extract the group count. */

    group_count = get_short(&v2_rpt_pkt->mld_v2_report_num_rcrds);

    /* Loop for each group in the report. */

    packet_len -= sizeof(mld_v2_report);
    v2_rpt_rcrd = v2_rpt_pkt->mld_v2_report_rcrd;
    byte_ptr = (u_int8_t *) v2_rpt_rcrd;
    while (group_count--) {

	/*
	 * Looks like we have a group record.  First make sure that there's
	 * enough left in the packet to examine the header.
	 */
	if (packet_len < sizeof(mld_v2_rpt_rcrd))
	    return FALSE;

	/* Validate the record type. */

	if (v2_rpt_rcrd->mld_v2_rpt_rec_type < GMP_RPT_MIN ||
	    v2_rpt_rcrd->mld_v2_rpt_rec_type > GMP_RPT_MAX)
	    return FALSE;

	/* Extract the source count. */

	source_count = get_short(&v2_rpt_rcrd->mld_v2_rpt_num_srcs);

	/* Make sure there's enough packet left to hold the whole record. */

	record_length = sizeof(mld_v2_rpt_rcrd) +
	    (source_count * IPV6_ADDR_LEN) + v2_rpt_rcrd->mld_v2_rpt_aux_len;
	if (packet_len < record_length)
	    return FALSE;

	/* Enough space.  Create a group record. */

	group_rcrd = gmpp_create_group_record(report_pkt, NULL,
					      v2_rpt_rcrd->mld_v2_rpt_group,
					      IPV6_ADDR_LEN);
	if (!group_rcrd)
	    return FALSE;		/* Out of memory */

	/* Stash the record type. */

	group_rcrd->gmp_rpt_type = v2_rpt_rcrd->mld_v2_rpt_rec_type;

	/* If there are any sources, add them to the record. */

	if (source_count) {
	    addr_ptr = get_mld_v2_rpt_source(v2_rpt_rcrd);

	    /* Allocate an address thread. */

	    addr_thread = gmp_alloc_addr_thread();
	    if (!addr_thread)
		return FALSE;		/* Out of memory */

	    while (source_count--) {

		/* Enqueue an address thread entry for the next address. */

		if (gmp_enqueue_addr_thread_addr(addr_thread, addr_ptr,
						 IPV6_ADDR_LEN) < 0)
		    return FALSE;	/* Out of memory */
		addr_ptr += IPV6_ADDR_LEN;
	    }
	    group_rcrd->gmp_rpt_rcv_srcs = addr_thread;
	}

	/* Advance to the next group. */

	packet_len -= record_length;
	byte_ptr += record_length;
	v2_rpt_rcrd = (mld_v2_rpt_rcrd *) byte_ptr;
    }

    return TRUE;
}


/*
 * mld_parse_v2_query_packet
 *
 * Parse an MLD V2 Query packet.
 *
 * Returns TRUE if the packet parsed OK, or FALSE if not.
 *
 * Fills in the generic packet structure as appropriate.
 */
static boolean
mld_parse_v2_query_packet(mld_packet *packet, gmp_packet *gen_packet,
			   u_int32_t packet_len)
{
    mld_v2_query *v2_query_pkt;
    gmp_query_packet *query_pkt;
    u_int byte_offset;
    gmp_addr_thread *addr_thread;
    u_int source_count;
    u_int8_t *addr_ptr;

    /* Bail if the packet is too small. */

    if (packet_len < sizeof(mld_v2_query))
	return FALSE;

    v2_query_pkt = &packet->mld_pkt_v2_query;
    query_pkt = &gen_packet->gmp_packet_contents.gmp_packet_query;

    /* Query packet.  Get the max resp value. */

    query_pkt->gmp_query_max_resp =
	mld_max_resp_value(packet, GMP_VERSION_SOURCES) * MLD_MAX_RESP_MSEC;

    /* Copy the group address. */

    memmove(query_pkt->gmp_query_group.gmp_addr, v2_query_pkt->mld_v2_query_group, IPV6_ADDR_LEN);
	
    /*
     * If the group address is nonzero, flag that we've got a
     * group query.
     */
    for (byte_offset = 0; byte_offset < IPV6_ADDR_LEN; byte_offset ++) {
	if (v2_query_pkt->mld_v2_query_group[byte_offset]) {
	    query_pkt->gmp_query_group_query = TRUE;
	    break;
	}
    }

    /* Parse the S and QRV fields. */

    query_pkt->gmp_query_suppress =
	((v2_query_pkt->mld_v2_query_s_qrv & MLD_SUPP_RTR_PROC_MASK) != 0);
    query_pkt->gmp_query_qrv =
	v2_query_pkt->mld_v2_query_s_qrv & MLD_QRV_MASK;

    /* Parse the QQIC field. */

    query_pkt->gmp_query_qqi =
	mld_decode_qqic(v2_query_pkt->mld_v2_query_qqic) * MSECS_PER_SEC;

    /*
     * Parse the source count field.  If nonzero and this is a general query,
     * flag a parse error.
     */
    source_count = get_short(&v2_query_pkt->mld_v2_query_num_srcs);
    if (source_count && !query_pkt->gmp_query_group_query)
	return FALSE;

    packet_len -= sizeof(mld_v2_query);
    
    /*
     * If the remaining packet length doesn't match the count of sources, flag
     * a parse error.
     */
    if (packet_len != (source_count * IPV6_ADDR_LEN))
	return FALSE;

    /*
     * Looks kosher.  Now pull the list of source addresses into an
     * address thread, if they are there.
     */
    if (source_count) {
	addr_ptr = v2_query_pkt->mld_v2_query_source;
	addr_thread = gmp_alloc_addr_thread();
	query_pkt->gmp_query_rcv_srcs = addr_thread;
	if (!addr_thread)
	    return FALSE;		/* Out of memory */
	while (source_count--) {
	    if (gmp_enqueue_addr_thread_addr(addr_thread, addr_ptr,
					     IPV6_ADDR_LEN) < 0) {
		return FALSE;		/* Out of memory */
	    }
	    addr_ptr += IPV6_ADDR_LEN;
	}
    }

    return TRUE;
}


/*
 * mld_parse_v2_packet
 *
 * Parse an MLD V2 packet.
 *
 * Returns TRUE if the packet parsed OK, or FALSE if not.
 *
 * Fills in the generic packet structure as appropriate.
 */
static boolean
mld_parse_v2_packet(mld_packet *packet, gmp_packet *gen_packet,
		     u_int32_t packet_len, gmp_message_type msg_type)
{
    boolean result;

    /* Switch based on message type. */

    switch (msg_type) {

      case GMP_QUERY_PACKET:
	result = mld_parse_v2_query_packet(packet, gen_packet, packet_len);
	break;


      case GMP_REPORT_PACKET:
	result = mld_parse_v2_report_packet(packet, gen_packet, packet_len);
	break;

      default:
	gmpx_assert(FALSE);
	result = FALSE;			/* Quiet the compiler. */
	break;

    }

    return result;
}


/*
 * mld_process_pkt
 *
 * Process a received MLD packet.  We parse it into generic form and pass it
 * to the clients.
 *
 * Returns TRUE if the packet parsed OK, or FALSE if there was a problem.
 */
boolean
mld_process_pkt (void *rcv_pkt, const u_int8_t *src_addr,
		 const u_int8_t *dest_addr, u_int32_t packet_len,
		 gmpx_intf_id intf_id, gmpx_packet_attr attrib,
		 void *trace_context, u_int32_t trace_flags)
{
    mld_packet *packet;
    gmp_packet *gen_packet;
    gmp_version version;
    gmp_message_type msg_type;
    boolean parse_ok;

    packet = rcv_pkt;
    parse_ok = TRUE;

    /* Bail if the packet is too small. */

    if (packet_len < sizeof(mld_naked_header)) {
	parse_ok = FALSE;
    }

    /*
     * Bail if the version is bogus or the packet is otherwise
     * unrecognizable.
     */
    version = mld_generic_version(packet, packet_len, &msg_type);
    if (version == GMP_VERSION_INVALID) {
	parse_ok = FALSE;
    }

    /*
     * Looks like we have a recognizable header, message type, and version.
     * Create a basic generic packet of the appropriate type.
     */
    gen_packet = gmpp_create_packet_header(version, msg_type, GMP_PROTO_MLD);
    if (!gen_packet)
	return FALSE;			/* Out of memory */

    /* Set up the address fields. */

    if (src_addr) {
        memmove(gen_packet->gmp_packet_src_addr.gmp_addr, src_addr, IPV6_ADDR_LEN);
    }
    if (dest_addr) {
        memmove(gen_packet->gmp_packet_dest_addr.gmp_addr, dest_addr, IPV6_ADDR_LEN);
    }

    /* If the packet looks OK so far, parse it further. */

    if (parse_ok) {

	/* Save the attribute. */

	gen_packet->gmp_packet_attr = attrib;

	/* Split off by version and by packet type for further parsing. */

	switch (version) {
	  case GMP_VERSION_LEAVES:

	    /* MLDv1.  Parse it. */

	    parse_ok = mld_parse_v1_packet(packet, gen_packet, packet_len,
					   msg_type);
	    break;

	  case GMP_VERSION_SOURCES:

	    /* MLDv2.  Parse it. */

	    parse_ok = mld_parse_v2_packet(packet, gen_packet, packet_len,
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
	gmp_mld_trace_pkt(packet, packet_len, src_addr, intf_id, TRUE,
			  trace_context, trace_flags);
	gmpp_process_rcv_packet(gen_packet, intf_id);
    } else {
	gmp_mld_trace_bad_pkt(packet_len, src_addr, intf_id, trace_context,
			      trace_flags);
    }

    /* Toss the parsed packet. */

    gmpp_destroy_packet(gen_packet);

    return parse_ok;
}
