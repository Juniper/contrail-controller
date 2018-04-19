/* $Id: gmp_externs.h 346474 2009-11-14 10:18:58Z ssiano $
 *
 * gmp_externs.h - External definitions for GMP support
 *
 * Dave Katz, July 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 * This file defines general, common external definitions for both
 * host-side and router-side GMP, and is expected to be included in
 * external code that uses the GMP toolkit.
 */

#ifndef __GMP_EXTERNS_H__
#define __GMP_EXTERNS_H__


/*
 * Transmit ready callback type
 *
 * This defines the callback made from the generic protocol handler to the
 * I/O environment when a packet is ready to transmit.
 */
typedef void (*gmpp_xmit_ready_func) (gmp_role role, gmp_proto proto,
                      gmpx_intf_id intf_id);


/*
 * Packet interface receive peek callback type
 *
 * This defines the callback made from the generic packet I/O routines
 * when a generic packet has been received to be passed to the client.
 */
typedef void (*gmp_rcv_peek_callback_func)(gmpx_intf_id intf_id,
                       gmp_proto proto,
                       gmp_packet *gen_packet);


/*
 * Packet interface transmit peek callback type
 *
 * This defines the callback made from the generic packet I/O routines
 * when a generic packet has been formatted for transmission by the client.
 */
typedef void (*gmp_xmit_peek_callback_func)(gmpx_intf_id intf_id,
                        gmp_proto proto,
                        gmp_packet *gen_packet);


/* Externals */

/* igmp_proto.c */

extern uint32_t igmp_next_xmit_packet(gmp_role role, gmpx_intf_id intf_id,
                   void *packet, uint8_t *dest_addr,
                   uint32_t packet_len, void *trace_context,
                   uint32_t trace_flags);
extern boolean igmp_process_pkt(void *rcv_pkt, const uint8_t *src_addr,
                const uint8_t *dest_addr,
                uint32_t packet_len, gmpx_intf_id intf_id,
                gmpx_packet_attr attrib, void *trace_context,
                uint32_t trace_flags);
extern void
    gmp_igmp_trace_pkt(void *pkt, uint32_t len, const uint8_t *addr,
               gmpx_intf_id intf_id, boolean receive,
               void *trace_context, uint32_t trace_flags);
extern void
    gmp_igmp_trace_bad_pkt(uint32_t len, const uint8_t *addr,
               gmpx_intf_id intf_id, void *trace_context,
               uint32_t trace_flags);

/* mld_proto.c */

extern uint32_t mld_next_xmit_packet(gmp_role role, gmpx_intf_id intf_id,
                  void *packet, uint8_t *dest_addr,
                  uint32_t packet_len, void *trace_context,
                  uint32_t trace_flags);
extern boolean mld_process_pkt(void *rcv_pkt, const uint8_t *src_addr,
                   const uint8_t *dest_addr, uint32_t packet_len,
                   gmpx_intf_id intf_id, gmpx_packet_attr attrib,
                   void *trace_context, uint32_t trace_flags);
extern void gmp_mld_trace_pkt(void *pkt, uint32_t len, const uint8_t *addr,
                  gmpx_intf_id intf_id, boolean receive,
                  void *trace_context, uint32_t trace_flags);
extern void gmp_mld_trace_bad_pkt(uint32_t len, const uint8_t *addr,
                  gmpx_intf_id intf_id, void *trace_context,
                  uint32_t trace_flags);

/* gmp_addrlist.c */

extern gmp_addr_thread *gmp_alloc_addr_thread(void);
extern int gmp_enqueue_addr_thread_addr(gmp_addr_thread *addr_thread,
                    uint8_t *addr, uint32_t addr_len);
extern gmp_addr_string
    *gmp_next_addr_thread_addr(gmp_addr_thread *addr_thread,
                   gmp_addr_thread_entry **entry_ptr);
extern void gmp_destroy_addr_thread(gmp_addr_thread *addr_thread);
extern uint32_t gmp_addr_thread_count(gmp_addr_thread *addr_thread);


/* gmpp_proto.c */

extern void gmp_register_io(gmp_role role, gmp_proto proto,
                gmpp_xmit_ready_func xmit_ready);
extern void gmp_register_io_exception(gmp_role role, gmp_proto proto,
                      gmpx_intf_id intf_id,
                      gmpp_xmit_ready_func xmit_ready);
extern void gmp_register_peek_function(gmp_role role,
                       gmp_xmit_peek_callback_func xm_peek_cb,
                       gmp_rcv_peek_callback_func rc_peek_cb);

#endif /* __GMP_EXTERNS_H__ */
