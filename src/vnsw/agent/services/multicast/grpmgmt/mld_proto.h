/* $Id: mld_proto.h 346474 2009-11-14 10:18:58Z ssiano $
 *
 * mld_proto.h - MLD protocol message definitions
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef __MLD_PROTO_H__
#define __MLD_PROTO_H__

/*
 * This module defines the message formats for MLD, as well as some
 * helper routines.
 */


/*
 * MLD packet header format
 *
 * The MLD header qualifies the contents of the rest of the packet.
 */
typedef struct mld_hdr_ {
    uint8_t mld_hdr_type;        /* packet type */
    uint8_t mld_hdr_resv;        /* Reserved */
    uint16_t mld_hdr_cksum;        /* Checksum */
} mld_hdr;

#define MLD_TYPE_QUERY        130    /* Query (all versions) */
#define MLD_TYPE_V1_REPORT    131    /* Version 1 Report */
#define MLD_TYPE_V1_LEAVE    132    /* Version 1 Leave */
#define MLD_TYPE_V2_REPORT    143    /* Version 2 Report */

/*
 * Version 1 packets
 *
 * All MLD v1 packets have the same format.
 */
typedef struct mld_v1_pkt_ {
    mld_hdr mld_v1_pkt_hdr;        /* Packet header */
    uint16_t mld_v1_max_resp;        /* Maximum Response Delay */
    uint16_t mld_v1_resv;        /* Reserved */
    uint8_t mld_v1_pkt_group[IPV6_ADDR_LEN]; /* Group address */
} mld_v1_pkt;


/*
 * Version 2 Query packet
 */
typedef struct mld_v2_query_ {
    mld_hdr mld_v2_query_hdr;        /* Packet header */
    uint16_t mld_v2_max_resp;        /* Max resp code */
    uint16_t mld_v2_resv;        /* Reserved */
    uint8_t mld_v2_query_group[IPV6_ADDR_LEN]; /* Group address */
    uint8_t mld_v2_query_s_qrv;    /* S/QRV fields */
    uint8_t mld_v2_query_qqic;    /* QQIC */
    uint16_t mld_v2_query_num_srcs;    /* Number of sources */
    uint8_t mld_v2_query_source[0];    /* Array of sources */
} mld_v2_query;

#define MLD_SUPP_RTR_PROC_MASK 0x8    /* "S" bit in s_qrv field */
#define MLD_QRV_MASK 0x7        /* QRV value in s_qrv field */


/*
 * Version 2 Report packet
 *
 * Version 2 reports have one or more group records.
 */
typedef struct mld_v2_rpt_rcrd_ {
    uint8_t mld_v2_rpt_rec_type;    /* Record type */
    uint8_t mld_v2_rpt_aux_len;    /* Auxiliary data length */
    uint16_t mld_v2_rpt_num_srcs;    /* Number of sources */
    uint8_t mld_v2_rpt_group[IPV6_ADDR_LEN]; /* Group address */
} mld_v2_rpt_rcrd;

/* Array of sources */
static inline uint8_t* get_mld_v2_rpt_source(mld_v2_rpt_rcrd *ptr) {
    return (uint8_t*)(ptr + 1);
}

typedef struct mld_v2_report_ {
    mld_hdr mld_v2_report_hdr;    /* Packet header */
    uint16_t mld_v2_report_rsvd;    /* Reserved */
    uint16_t mld_v2_report_num_rcrds;    /* Number of records */
    mld_v2_rpt_rcrd mld_v2_report_rcrd[0]; /* Set records */
} mld_v2_report;


/*
 * Naked header
 *
 * This is a hack to access the header to determine the packet type.
 */
typedef struct mld_naked_header_ {
    mld_hdr mld_naked_header_hdr;    /* Packet header */
    uint16_t mld_naked_max_resp;    /* Max resp code */
} mld_naked_header;


/*
 * MLD Packet
 *
 * This is a union of all types to aid in type coercion.
 */
typedef union mld_packet_ {
    mld_naked_header mld_pkt_naked;    /* Naked header */
    mld_v1_pkt mld_pkt_v1;        /* V1 packet */
    mld_v2_query mld_pkt_v2_query;    /* V2 query */
    mld_v2_report mld_pkt_v2_rpt;    /* V2 report */
} mld_packet;


/*
 *  Max Resp field
 *
 * The Max Resp field in version 2 can be fixed or floating point.
 */
#define MLD_MAXRSP_FIXFLOAT_FLAG 0x8000    /* Set if floating point */
#define MLD_MAXRSP_EXP_MASK 0x7000      /* Floating point exponent mask */
#define MLD_MAXRSP_EXP_SHIFT 12        /* Shift count for exponent */
#define MLD_MAXRSP_MANT_MASK 0x0FFF     /* Floating point mantissa mask */
#define MLD_MAXRSP_MANT_HIGHBIT 0x1000    /* High bit of mantissa */
#define MLD_MAXRSP_MAX_MANT 0x1FFF    /* Max mantissa portion */
#define MLD_MAXRSP_MANT_SHIFT 0        /* Shift count for mantissa */
#define MLD_MAXRSP_EXP_OFFSET 3        /* Offset bits to add */
#define MLD_MAXRSP_MAX_EXP 0x07        /* Maximum exponent value */
#define MLD_MAX_MAXRSP_ENCODABLE \
    ((MLD_MAXRSP_MANT_MASK | MLD_MAXRSP_MANT_HIGHBIT) << \
     (MLD_MAXRSP_MAX_EXP + MLD_MAXRSP_EXP_OFFSET))
                    /* Maximum encodable value */

/*
 *  QQIC field
 *
 * The QQIC field in version 2 can be fixed or floating point.
 */
#define MLD_QQIC_FIXFLOAT_FLAG 0x80    /* Set if floating point */
#define MLD_QQIC_EXP_MASK 0x70            /* Floating point exponent mask */
#define MLD_QQIC_EXP_SHIFT 4        /* Shift count for exponent */
#define MLD_QQIC_MANT_MASK 0x0F            /* Floating point mantissa mask */
#define MLD_QQIC_MANT_HIGHBIT 0x10    /* High bit of mantissa */
#define MLD_QQIC_MAX_MANT 0x1F        /* Max mantissa portion */
#define MLD_QQIC_MANT_SHIFT 0        /* Shift count for mantissa */
#define MLD_QQIC_EXP_OFFSET 3        /* Offset bits to add */
#define MLD_QQIC_MAX_EXP 0x07        /* Maximum exponent value */
#define MLD_MAX_QQIC_ENCODABLE \
    ((MLD_QQIC_MANT_MASK | MLD_QQIC_MANT_HIGHBIT) << \
     (MLD_QQIC_MAX_EXP + MLD_QQIC_EXP_OFFSET))
                    /* Maximum encodable value */


#define MLD_MAX_RESP_MSEC 1        /* Max Resp is in units of msec */
#define MLD_MAX_RESP_DEFAULT 10000    /* Default Max Resp val msec) */
#define MLD_V1_MAX_MAX_RESP 0xFFFF    /* Maximum Max Resp value for V1 */



#endif /* __MLD_PROTO_H__ */
