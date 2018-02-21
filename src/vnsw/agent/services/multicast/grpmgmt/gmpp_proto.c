/* $Id: gmpp_proto.c 346474 2009-11-14 10:18:58Z ssiano $
 *
 * gmpp_proto.c - GMP generic packet handling
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 * This module handles generic packet I/O for GMP host and router functions.
 * It acts as an interface between the host/router modules on one side and
 * the IGMP/MLD modules on the other side.
 *
 * There are two instantiations of this function, one for each role
 * (Host and Router.)
 *
 * This function has no visibility into host- and router-specific data
 * structures, so that we can operate with only one side compiled in if
 * desired.
 *
 *
 * Registration
 *
 *   The client (host/router) must first register via gmpp_register.
 *   This establishes links between the two sides.
 *
 * Transmission
 *
 *   When a client wishes to transmit a packet, it informs this module
 *   via gmpp_start_xmit.  This module then informs the protocol
 *   module, which ultimately informs the I/O environment.  A callback
 *   percolates back through here to the client xmit_ready callback,
 *   requesting a packet.  The client passes back a pointer to a
 *   generic packet.
 *
 *   This module forms a packet out of some or all of the generic
 *   packet data passed by the client, with the help of the
 *   protocol-specific modules, and passes the packet to the I/O
 *   environment.  We delink each source address from its transmit
 *   list as we process it.  We call back the client group_done
 *   routine as we finish processing the data for each group.
 *
 *   In some cases, a single group may span a packet (an Include list
 *   with a very large number of sources.)  In this case we don't call
 *   the group_done entry and leave some of the source addresses still
 *   enqueued.  When we call back for the next packet, the client will
 *   pass us the same group again and we continue.
 *
 * Reception
 *
 *   Packet reception is driven from the I/O environment.  We receive
 *   an indication from there and parse the packet, with the help of
 *   the protocol modules, and pass the parsed packet to the client's
 *   packet_rcv callback.  The client is expected to process the
 *   entire parsed packet atomically, as we free the parsed packet
 *   immediately.
 *
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_externs.h"
#include "gmp_private.h"
#include "gmpp_private.h"
#include "igmp_protocol.h"
#include "mld_proto.h"

/*
 * Context blocks
 * 
 * Statically allocated, indexed by client role (host/router).
 */
static gmpp_context gmpp_context_block[GMP_NUM_ROLES];
static gmpp_io_context gmpp_io_context_block[GMP_NUM_PROTOS][GMP_NUM_ROLES];
static gmpx_block_tag gmpp_packet_header_tag;
static gmpx_block_tag gmpp_group_record_tag;
static gmpx_block_tag gmpp_io_exception_tag;

static boolean gmpp_initialized;

/*
 * gmpp_init
 *
 * Do basic initialization.
 */
void
gmpp_init (void)
{
    if (!gmpp_initialized) {

	/* Set up memory blocks. */

	gmpp_packet_header_tag =
	    gmpx_malloc_block_create(sizeof(gmp_packet),
				     "GMP generic packet header");
	gmpp_group_record_tag =
	    gmpx_malloc_block_create(sizeof(gmp_report_group_record),
				     "GMP generic packet group record");
	gmpp_io_exception_tag =
	    gmpx_malloc_block_create(sizeof(gmpp_io_exception),
				     "GMP I/O exception record");
	gmpp_initialized = TRUE;
    }
}


/*
 * gmpp_create_group_record
 *
 * Create a group record, initialize it, and link it into the packet.
 *
 * Returns a pointer to the group record, or NULL if out of memory.
 */
gmp_report_group_record *
gmpp_create_group_record (gmp_report_packet *report_packet, void *group_id,
			  const uint8_t *group_addr, uint32_t addr_len)
{
    gmp_report_group_record *group_record;

    /* Allocate the block. */

    group_record = gmpx_malloc_block(gmpp_group_record_tag);
    if (!group_record)
	return NULL;			/* Out of memory */

    /* Initialize it. */

    group_record->gmp_rpt_group_id = group_id;
    memmove(group_record->gmp_rpt_group.gmp_addr, group_addr, addr_len);

    /* Put it into the thread. */

    thread_circular_add_bottom(&report_packet->gmp_report_group_head,
			       &group_record->gmp_rpt_thread);
    report_packet->gmp_report_group_count++;

    return group_record;
}


/*
 * gmpp_create_packet_header
 *
 * Create a packet header.
 *
 * Returns a pointer to the packet header, or NULL if out of memory.
 */
gmp_packet *
gmpp_create_packet_header (gmp_version version, gmp_message_type message_type,
			   gmp_proto proto)
{
    gmp_packet *packet;
    gmp_report_packet *report_packet;

    packet = gmpx_malloc_block(gmpp_packet_header_tag);
    if (packet) {

	/* Initialize it. */

	packet->gmp_packet_version = version;
	packet->gmp_packet_type = message_type;
	packet->gmp_packet_proto = proto;

	switch (message_type) {

	  case GMP_QUERY_PACKET:
	    break;

	  case GMP_REPORT_PACKET:
	    report_packet = &packet->gmp_packet_contents.gmp_packet_report;
	    thread_new_circular_thread(&report_packet->gmp_report_group_head);
	    break;

	  default:
	    gmpx_assert(FALSE);
	}
    }

    return packet;
}


/*
 * gmpp_destroy_packet
 *
 * Destroy a generic packet.  We clean up the contents and free it.
 *
 * Note that the address list pointers in query packets and group records
 * point to address lists owned elsewhere, and are not freed.  The address
 * threads pointed to are destroyed, however.
 */
void
gmpp_destroy_packet (gmp_packet *packet)
{
    gmp_report_group_record *group_record;
    gmp_report_packet *report_packet;
    gmp_query_packet *query_packet;
    thread *thread_ptr;

    /* Clean up based on the packet type. */

    switch (packet->gmp_packet_type) {

      case GMP_QUERY_PACKET:

	/* Query packet.  Delete the address thread. */

	query_packet = &packet->gmp_packet_contents.gmp_packet_query;
	gmp_destroy_addr_thread(query_packet->gmp_query_rcv_srcs);
	break;
	
      case GMP_REPORT_PACKET:

	/* Report packet, toss all of the group records. */

	report_packet = &packet->gmp_packet_contents.gmp_packet_report;

	/* Walk all group records, and free them. */

	while (TRUE) {
	    thread_ptr =
		thread_circular_top(&report_packet->gmp_report_group_head);
	    if (!thread_ptr)
		break;
	    thread_remove(thread_ptr);
	    group_record = gmp_thread_to_report_group_record(thread_ptr);
	    gmp_destroy_addr_thread(group_record->gmp_rpt_rcv_srcs);
	    gmpx_free_block(gmpp_group_record_tag, group_record);
	}
	break;

      default:
	gmpx_assert(FALSE);
	break;
    }

    /* Now toss the packet header and we're done. */

    gmpx_free_block(gmpp_packet_header_tag, packet);
}

    
/*
 * gmpp_start_xmit
 * 
 * Initiate transmission on an interface.
 */
void
gmpp_start_xmit (gmp_role role, gmp_proto proto, gmpx_intf_id intf_id)
{
    gmpp_io_context *io_ctx;
    gmpp_io_exception *io_except;
    patnode *node;

    /* Validate the parameters. */

    gmpx_assert(role < GMP_NUM_ROLES);
    gmpx_assert(proto < GMP_NUM_PROTOS);

    /* Call the registered callback. */

    io_ctx = &gmpp_io_context_block[proto][role];
    gmpx_assert(io_ctx->io_ctx_xmitready_cb);

    /* See if there's an exception for this interface.  If so, use it. */

    node = gmpx_patricia_lookup(io_ctx->io_ctx_exceptions, &intf_id);
    io_except = gmpp_patnode_to_io_ex(node);

    if (io_except) {
	(*io_except->io_ctx_alt_cb)(role, proto, intf_id);
    } else {

	/* No exception.  use the general callback. */

	(*io_ctx->io_ctx_xmitready_cb)(role, proto, intf_id);
    }
}


/*
 * gmpp_register
 *
 * Register a client.
 */
void
gmpp_register (gmp_role role, gmp_xmit_callback_func xmit_callback,
	       gmp_rcv_callback_func rcv_callback,
	       gmp_group_done_callback_func group_done_callback,
	       gmp_packet_free_callback_func packet_free_callback)
{
    gmpp_context *ctx;

    /* Make sure the role is in range. */

    gmpx_assert(role < GMP_NUM_ROLES);

    ctx = &gmpp_context_block[role];

    /* Make sure we're not getting any duplicates. */

    gmpx_assert(ctx->ctx_xmit_cb == NULL);

    /* Squirrel away all of the context. */

    ctx->ctx_xmit_cb = xmit_callback;
    ctx->ctx_rcv_cb = rcv_callback;
    ctx->ctx_group_cb = group_done_callback;
    ctx->ctx_pkt_free_cb = packet_free_callback;
}


/*
 * gmpp_deregister
 *
 * Deregister a client.
 */
void
gmpp_deregister (gmp_role role)
{
    gmpp_context *ctx;

    /* Make sure the role is in range. */

    gmpx_assert(role < GMP_NUM_ROLES);

    ctx = &gmpp_context_block[role];

    /* It better have been registered. */

    gmpx_assert(ctx->ctx_xmit_cb);

    /* Zap the context block. */

    memset(ctx, 0, sizeof(gmpp_context));
}


/*
 * gmpp_enab_disab_proto
 *
 * Enable or disable protocol processing for a client.
 */
void
gmpp_enab_disab_proto (gmp_role role, gmp_proto proto, boolean enabled)
{
    /* Ensure that everything is in range. */

    gmpx_assert(role < GMP_NUM_ROLES && proto < GMP_NUM_PROTOS);

    /* Set the flag appropriately. */

    gmpp_context_block[role].ctx_proto_active[proto] = enabled;
}


/*
 * gmp_register_io
 *
 * Register packet I/O.  This is called by the protocol formatting routines
 * to register themselves.
 */
void
gmp_register_io (gmp_role role, gmp_proto proto,
		 gmpp_xmit_ready_func xmit_ready)
{
    gmpp_io_context *io_ctx;

    /* Validate the parameters. */

    gmpx_assert(role < GMP_NUM_ROLES);
    gmpx_assert(proto < GMP_NUM_PROTOS);

    /* Set the context appropriately. */

    io_ctx = &gmpp_io_context_block[proto][role];

    /* Set the transmit-ready callback. */

    io_ctx->io_ctx_xmitready_cb = xmit_ready;

    /* Set up the patricia tree for exception interfaces. */

    if (!io_ctx->io_ctx_exceptions) {
	io_ctx->io_ctx_exceptions =
	    gmpx_patroot_init(sizeof(gmpx_intf_id),
			      GMPX_PATRICIA_OFFSET(gmpp_io_exception,
						   io_ctx_node, io_ctx_intf));
    }
}


/*
 * gmp_register_io_exception
 *
 * Register an exception to the I/O transmit callback for a particular
 * interface.  Transmit-ready notifications on the indicated interface
 * will go to the exception routine instead of the normal one.
 */
void
gmp_register_io_exception (gmp_role role, gmp_proto proto,
			   gmpx_intf_id intf_id,
			   gmpp_xmit_ready_func xmit_ready)
{
    gmpp_io_context *io_ctx;
    gmpp_io_exception *io_except;
    gmpx_patnode *node;

    /*
     * We may get called before initialization has taken place.  Cheat
     * and try to initialize now (it'll be a no-op if this has already
     * taken place.)
     */
    gmpp_init();

    /* Validate the parameters. */

    gmpx_assert(role < GMP_NUM_ROLES);
    gmpx_assert(proto < GMP_NUM_PROTOS);

    /* Get the context block. */

    io_ctx = &gmpp_io_context_block[proto][role];

    /* We better have already initialized it. */

    gmpx_assert(io_ctx->io_ctx_exceptions);

    /* Look up the interface.  Use it if it's there. */

    node = gmpx_patricia_lookup(io_ctx->io_ctx_exceptions, &intf_id);
    io_except = gmpp_patnode_to_io_ex(node);

    /* If there's no exception block, allocate it and link it in. */

    if (!io_except) {
	io_except = gmpx_malloc_block(gmpp_io_exception_tag);
	io_except->io_ctx_intf = intf_id;
	gmpx_assert(gmpx_patricia_add(io_ctx->io_ctx_exceptions,
				      &io_except->io_ctx_node));
    }

    /* Fill in the field. */

    io_except->io_ctx_alt_cb = xmit_ready;
}


/*
 * gmp_register_peek_function
 *
 * Register a transmit peek function for a client.  This routine is called
 * whenever the client engine generates a generic packet to send.
 */
void
gmp_register_peek_function (gmp_role role,
			    gmp_xmit_peek_callback_func xmit_peek_cb,
			    gmp_rcv_peek_callback_func rcv_peek_cb)
{
    gmpp_context_block[role].ctx_xmit_peek_cb = xmit_peek_cb;
    gmpp_context_block[role].ctx_rcv_peek_cb = rcv_peek_cb;
}


/*
 * gmpp_next_xmit_packet
 *
 * Get the next generic packet to transmit, given the interface, protocol,
 * and role.  Returns a pointer to the generic packet, or NULL if there's
 * nothing to send.
 *
 * The buffer length is passed to the transmit callback to help it figure
 * out how many groups can fit within a packet.  Zero values are tolerated,
 * meaning that the buffer size is unknown;  the callback routines will always
 * return a single group in that case.
 */
gmp_packet *
gmpp_next_xmit_packet (gmp_role role, gmp_proto proto, gmpx_intf_id intf_id,
		       uint32_t buffer_len)
{
    gmpp_context *ctx;
    gmp_packet *packet;

    gmpx_assert(role < GMP_NUM_ROLES);
    gmpx_assert(proto < GMP_NUM_PROTOS);

    /* Look up the context block. */

    ctx = &gmpp_context_block[role];
    gmpx_assert(ctx->ctx_proto_active[proto]);

    /* Call the callback to do the job. */

    packet = (*ctx->ctx_xmit_cb)(intf_id, proto, buffer_len);

    /* If there is a transmit peek function, let it peek at the packet. */

    if (packet && ctx->ctx_xmit_peek_cb)
	(*ctx->ctx_xmit_peek_cb)(intf_id, proto, packet);

    return packet;
}


/*
 * gmpp_group_done
 *
 * Called by the protocol-specific routine to indicate that it is done
 * processing a group.  We call the client engine callback routine in turn.
 */
void
gmpp_group_done (gmp_role role, gmp_proto proto, void *group_id)
{
    gmpp_context *ctx;

    gmpx_assert(role < GMP_NUM_ROLES);
    gmpx_assert(proto < GMP_NUM_PROTOS);

    ctx = &gmpp_context_block[role];
    gmpx_assert(ctx->ctx_proto_active[proto]);

    (*ctx->ctx_group_cb)(group_id);
}


/*
 * gmpp_packet_done
 *
 * Called by the protocol-specific routine to indicate that it is done
 * with a packet.  We call the client engine callback routine in turn.
 */
void
gmpp_packet_done (gmp_role role, gmp_proto proto, gmp_packet *packet)
{
    gmpp_context *ctx;

    gmpx_assert(role < GMP_NUM_ROLES);
    gmpx_assert(proto < GMP_NUM_PROTOS);

    ctx = &gmpp_context_block[role];
    gmpx_assert(ctx->ctx_proto_active[proto]);

    (*ctx->ctx_pkt_free_cb)(packet);
}


/*
 * gmpp_process_rcv_packet
 *
 * Called by the protocol-specific routine when a packet is received.
 * The packet parsed cleanly, and we're passed a generic packet.  We
 * pass it to each client, and to any registered peek routine.
 */
void
gmpp_process_rcv_packet (gmp_packet *packet, gmpx_intf_id intf_id)
{
    gmp_role role;
    gmpp_context *ctx;

    gmpx_assert(packet->gmp_packet_proto < GMP_NUM_PROTOS);

    for (role = 0; role < GMP_NUM_ROLES; role++) {
	ctx = &gmpp_context_block[role];
	if (ctx->ctx_proto_active[packet->gmp_packet_proto]) {
	    if (ctx->ctx_rcv_peek_cb) {
		(*ctx->ctx_rcv_peek_cb)(intf_id, packet->gmp_packet_proto,
					packet);
	    }
	    (*ctx->ctx_rcv_cb)(intf_id, packet);
	}
    }
}


/*
 * gmpp_max_group_count
 *
 * Get the max group count possible for a packet, given the protocol,
 * version, packet type, and buffer length.
 *
 * This is calculated based on having no sources, so it may well return
 * a number larger than what the packet can actually carry.  This is OK,
 * as this is simply providing an upper bound for the packet building code,
 * which will only put into the packet what will fit.
 *
 * This doesn't exactly belong here, since it is multi-protocol specific
 * rather than generic, but by putting it here we can still compile only
 * MLD or IGMP without difficulty.
 *
 * If the buffer length is zero, we always return one group.
 */
uint32_t
gmpp_max_group_count (gmp_proto proto, gmp_version version,
		      gmp_message_type msg_type, uint32_t buffer_len)
{
    uint32_t overhead;
    uint32_t group_len;
    uint32_t max_group_count;

    /*
     * The only packets that carry more than one group are IGMPv3/
     * MLDv2 Report packets.  Bail on the others first, and bail if
     * the buffer length is zero (meaning that we should always return
     * one group.)
     */
    if (version != GMP_VERSION_SOURCES || msg_type != GMP_REPORT_PACKET ||
	!buffer_len) {
	return 1;
    }

    /*
     * It's a sources-version report packet.  Load up the overhead and
     * per-group cost based on the protocol.
     */
    switch (proto) {
      case GMP_PROTO_IGMP:
	overhead = sizeof(igmp_v3_report);
	group_len = sizeof(igmp_v3_rpt_rcrd);
	break;

      case GMP_PROTO_MLD:
	overhead = sizeof(mld_v2_report);
	group_len = sizeof(mld_v2_rpt_rcrd);
	break;

      default:
	overhead = 0;			/* Satisfy the compiler. */
	group_len = 0;
	gmpx_assert(FALSE);
	break;
    }

    /* Got the parameters.  Do the simple calculation. */

    gmpx_assert(buffer_len >= group_len + overhead);
    max_group_count = (buffer_len - overhead) / group_len;

    return max_group_count;
}
