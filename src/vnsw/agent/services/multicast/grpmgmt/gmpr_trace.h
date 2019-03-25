/* $Id: gmpr_trace.h 514187 2012-05-06 12:25:25Z ib-builder $
 *
 * gmpr_trace.h - GMP router-side trace support
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/*
 * Conditional tracing
 */
#define gmpr_trace(instance, flag, ...) \
    if (instance->rinst_traceflags & (flag)) \
        gmpx_trace(instance->rinst_context, __VA_ARGS__)

/*
 * Unconditional tracing
 */
#define gmpr_trace_uncond(instance, ...) \
    gmpx_trace(instance->rinst_context, __VA_ARGS__)

/*
 * Tracing in Contrail Agent
 */
#define gmpr_trace_agent(...) \
    gmpx_trace(NULL, __VA_ARGS__)

/*
 * Error event
 */
#define gmpr_post_event(instance, ev, ...) \
    gmpx_post_event(instance->rinst_context, ev, __VA_ARGS__)


/*
 * Event tracing flags.
 *
 * Beware that the low order bits are defined in gmp_trace.h!
 */

#define GMPR_TRACE_GROUP    0x00000100 /* Group activity */
#define GMPR_TRACE_HOST_NOTIFY    0x00000200 /* Host notification */
#define GMPR_TRACE_CLIENT_NOTIFY 0x00000400 /* Client notification */

extern const char *
    gmpr_client_notif_string(gmpr_client_notification_type type);
extern const char *
    gmpr_host_notif_string(gmpr_client_host_notification_type type);
