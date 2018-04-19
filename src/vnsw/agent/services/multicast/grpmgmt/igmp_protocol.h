/* $Id: igmp_proto.h 346474 2009-11-14 10:18:58Z ssiano $
 *
 * igmp_proto.h - IGMP protocol message definitions
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef __IGMP_PROTO_H__
#define __IGMP_PROTO_H__

/*
 * This module defines the message formats for IGMP, as well as some
 * helper routines.
 */


/*
 * IGMP packet header format
 *
 * The IGMP header qualifies the contents of the rest of the packet.
 */
typedef struct igmp_hdr_ {
    uint8_t igmp_hdr_type;        /* packet type */
    uint8_t igmp_hdr_maxresp;        /* Max resp code or reserved */
    uint16_t igmp_hdr_cksum;        /* Checksum */
} igmp_hdr;

#define IGMP_TYPE_QUERY        0x11    /* Query (all versions) */
#define IGMP_TYPE_V1_REPORT    0x12    /* Version 1 Report */
#define IGMP_TYPE_V2_REPORT    0x16    /* Version 2 Report */
#define IGMP_TYPE_V2_LEAVE    0x17    /* Version 2 Leave */
#define IGMP_TYPE_V3_REPORT    0x22    /* Version 3 Report */

/*
 * Version 1 and version 2 packets
 *
 * All IGMP v1 and v2 packets have the same format.
 */
typedef struct igmp_v1v2_pkt_ {
    igmp_hdr igmp_v1v2_pkt_hdr;        /* Packet header */
    uint8_t igmp_v1v2_pkt_group[IPV4_ADDR_LEN]; /* Group address */
} igmp_v1v2_pkt;


/*
 * Version 3 Query packet
 */
typedef struct igmp_v3_query_ {
    igmp_hdr igmp_v3_query_hdr;        /* Packet header */
    uint8_t igmp_v3_query_group[IPV4_ADDR_LEN]; /* Group address */
    uint8_t igmp_v3_query_s_qrv;    /* S/QRV fields */
    uint8_t igmp_v3_query_qqic;    /* QQIC */
    uint16_t igmp_v3_query_num_srcs;    /* Number of sources */
    uint8_t igmp_v3_query_source[0];    /* Array of sources */
} igmp_v3_query;

#define IGMP_SUPP_RTR_PROC_MASK 0x8    /* "S" bit in s_qrv field */
#define IGMP_QRV_MASK 0x7        /* QRV value in s_qrv field */


/*
 * Version 3 Report packet
 *
 * Version 3 reports have one or more group records.
 */
typedef struct igmp_v3_rpt_rcrd_ {
    uint8_t igmp_v3_rpt_rec_type;    /* Record type */
    uint8_t igmp_v3_rpt_aux_len;    /* Auxiliary data length */
    uint16_t igmp_v3_rpt_num_srcs;    /* Number of sources */
    uint8_t igmp_v3_rpt_group[IPV4_ADDR_LEN]; /* Group address */
} igmp_v3_rpt_rcrd;

/* Array of sources */
static inline uint8_t* get_igmp_v3_rpt_source(igmp_v3_rpt_rcrd *ptr) {
    return (uint8_t*)(ptr + 1);
}

typedef struct igmp_v3_report_ {
    igmp_hdr igmp_v3_report_hdr;    /* Packet header */
    uint16_t igmp_v3_report_rsvd;    /* Reserved */
    uint16_t igmp_v3_report_num_rcrds;    /* Number of records */
    igmp_v3_rpt_rcrd igmp_v3_report_rcrd[0]; /* Set records */
} igmp_v3_report;


/*
 * Naked header
 *
 * This is a hack to access the header to determine the packet type.
 */
typedef struct igmp_naked_header_ {
    igmp_hdr igmp_naked_header_hdr;    /* Packet header */
} igmp_naked_header;


/*
 * IGMP Packet
 *
 * This is a union of all types to aid in type coercion.
 */
typedef union igmp_packet_ {
    igmp_naked_header igmp_pkt_naked;    /* Naked header */
    igmp_v1v2_pkt igmp_pkt_v1v2;    /* V1/V2 packet */
    igmp_v3_query igmp_pkt_v3_query;    /* V3 query */
    igmp_v3_report igmp_pkt_v3_rpt;    /* V3 report */
} igmp_packet;


/*
 * Max Resp and QQIC fields
 *
 * The Max Resp and QQIC fields in version 3 can be fixed or floating point.
 */
#define IGMP_FIXFLOAT_FLAG 0x80        /* Set if floating point */
#define IGMP_FLOAT_EXP_MASK 0x70    /* Floating point exponent mask */
#define IGMP_FLOAT_EXP_SHIFT 4        /* Shift count for exponent */
#define IGMP_FLOAT_MANT_MASK 0x0F    /* Floating point mantissa mask */
#define IGMP_FLOAT_MANT_HIGHBIT 0x10    /* High bit of mantissa */
#define IGMP_FLOAT_MAX_MANT 0x1F    /* Max mantissa portion */
#define IGMP_FLOAT_MANT_SHIFT 0        /* Shift count for mantissa */
#define IGMP_FLOAT_EXP_OFFSET 3        /* Offset bits to add */
#define IGMP_FLOAT_MAX_EXP 0x07        /* Maximum exponent value */
#define IGMP_MAX_FLOAT_ENCODABLE \
    ((IGMP_FLOAT_MANT_MASK | IGMP_FLOAT_MANT_HIGHBIT) << \
     (IGMP_FLOAT_MAX_EXP + IGMP_FLOAT_EXP_OFFSET))
                    /* Maximum encodable value */


#define IGMP_MAX_RESP_MSEC 100        /* Max Resp is in units of 100 msec */
#define IGMP_MAX_RESP_DEFAULT 100    /* Default Max Resp val (* 100 msec) */
#define IGMP_V2_MAX_MAX_RESP 0xFF    /* Maximum Max Resp value for V2 */


/*
 * igmp_addr_is_mcast
 *
 * Returns TRUE if the address is multicast, or FALSE if not.
 */
static inline boolean
igmp_addr_is_mcast (const uint8_t *addr)
{
    return ((*addr & 0xf0) == 0xe0);
}

#endif /* __IGMP_PROTO_H__ */
