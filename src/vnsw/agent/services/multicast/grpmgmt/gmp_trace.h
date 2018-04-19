/* $Id: gmp_trace.h 346474 2009-11-14 10:18:58Z ssiano $
 *
 * gmp_trace.h - GMP role-independent trace support
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/* Packet tracing flags */

#define GMP_TRACE_QUERY        0x00000001 /* Query packets */
#define GMP_TRACE_QUERY_DETAIL    0x00000002 /* Detailed query packets */
#define GMP_TRACE_REPORT    0x00000004 /* Report packets */
#define GMP_TRACE_REPORT_DETAIL 0x00000008 /* Detailed report packets */
#define GMP_TRACE_LEAVE        0x00000010 /* Leave packets */
#define GMP_TRACE_LEAVE_DETAIL    0x00000020 /* Detailed leave packets */
#define GMP_TRACE_PACKET_BAD    0x00000040 /* Bad packets */
#define GMP_TRACE_PACKET (GMP_TRACE_QUERY | GMP_TRACE_REPORT | \
              GMP_TRACE_LEAVE) /* Packets */
#define GMP_TRACE_PACKET_DETAIL (GMP_TRACE_QUERY_DETAIL | \
                 GMP_TRACE_REPORT_DETAIL | \
                 GMP_TRACE_LEAVE_DETAIL) /* Detail packets */

/*
 * gmp_trace_set
 *
 * Returns TRUE if the specified trace flag is in use, or FALSE if not.
 */
static inline boolean
gmp_trace_set (uint32_t traceflags, uint32_t tracebits)
{
    return ((traceflags & tracebits) != 0);
}

/*
 * gmp_trace
 *
 * Conditionally trace.
 */
#define gmp_trace(context, traceflags, flag, ...) \
    if (gmp_trace_set((traceflags), (flag))) \
    gmpx_trace((context), __VA_ARGS__)

extern const char *gmp_proto_string(gmp_proto proto);
extern const char *gmp_filter_mode_string(gmp_filter_mode mode);
extern const char *gmp_generic_version_string(gmp_version ver);
extern const char *gmp_report_type_string(gmp_report_rectype type);
