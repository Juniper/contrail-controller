/* $Id: gmpp_private.h 346474 2009-11-14 10:18:58Z ssiano $
 *
 * gmpp_private.h - Private definitions for GMP generic packet handling support
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef __GMPP_PRIVATE_H__
#define __GMPP_PRIVATE_H__

/*
 * Context block
 *
 * There is one of these, statically allocated, for each client role
 * (host/router.)  It tracks all necessary state for each client.
 */
typedef struct gmpp_context_ {
    void *ctx_client_context;        /* Client's context */
    gmp_xmit_callback_func ctx_xmit_cb;    /* Transmit callback */
    gmp_xmit_peek_callback_func ctx_xmit_peek_cb; /* Transmit peek callback */
    gmp_rcv_peek_callback_func ctx_rcv_peek_cb;    /* Receive peek callback */
    gmp_rcv_callback_func ctx_rcv_cb;    /* Receive callback */
    gmp_group_done_callback_func ctx_group_cb; /* Group-done callback */
    gmp_packet_free_callback_func ctx_pkt_free_cb; /* Free-packet callback */
    boolean ctx_proto_active[GMP_NUM_PROTOS]; /* True if protocol is active */
} gmpp_context;


/*
 * I/O context block
 *
 * There is one block per role, per protocol.  It defines the transmit-ready
 * callback used to drive packet transmission.
 *
 * Normally, the same callback is used for all interfaces, but a tree of
 * exceptions exists for specific interfaces needing a different callback.
 */
typedef struct gmpp_io_context_ {
    gmpp_xmit_ready_func io_ctx_xmitready_cb; /* Transmit-ready callback */
    gmpx_patroot *io_ctx_exceptions;    /* Tree of exception interfaces */
} gmpp_io_context;

typedef struct gmpp_io_exception_ {
    gmpx_patnode io_ctx_node;        /* Node on context tree */
    gmpx_intf_id io_ctx_intf;        /* Interface ID  */
    gmpp_xmit_ready_func io_ctx_alt_cb;    /* Callback for this interface */
} gmpp_io_exception;

GMPX_PATNODE_TO_STRUCT(gmpp_patnode_to_io_ex, gmpp_io_exception, io_ctx_node);

#endif /* __GMPP_PRIVATE_H__ */
