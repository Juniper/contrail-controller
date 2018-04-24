/* $Id: gmp.h 514187 2012-05-06 12:25:25Z ib-builder $
 *
 * gmp.h - General definitions for GMP support
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 * This file defines general, common definitions for both host-side and
 * router-side GMP, and is expected to be included in external code that
 * uses the GMP toolkit.
 */

#ifndef __GMP_H__
#define __GMP_H__

/*
 * Basic address lengths
 *
 * This will never change, and we don't have to pull in a bunch of IP gorp
 * to use it.
 */
#define IPV4_ADDR_LEN 4            /* Length of a v4 address */
#define IPV6_ADDR_LEN 16        /* Length of a v6 address */


/*
 * Instance ID.  Used to identify an instance of gmp_host or gmp_router.
 * This is actually a pointer to the associated instance block.
 */
typedef void *gmp_instance_id;


/*
 * Client ID.  Used to identify clients of gmp_host and gmp_router.  This
 * is actually a pointer to the associated client block.
 */
typedef void *gmp_client_id;        /* Client ID */


/*
 * Protocol.  Used to identify which GMP protocol is in use.
 */
typedef enum {
    GMP_PROTO_IGMP,            /* IGMP */
    GMP_PROTO_MLD,            /* MLD */
    GMP_NUM_PROTOS            /* Number of protocols */
} gmp_proto;


/*
 * GMP roles.  They are HOST and ROUTER.  We can support both at the
 * same time.
 */
typedef enum {
    GMP_ROLE_HOST,            /* Host-side GMP */
    GMP_ROLE_ROUTER,            /* Router-side GMP */
    GMP_NUM_ROLES            /* Number of roles */
} gmp_role;


/*
 * Filter mode.  Identifies whether a source list inclusive or exclusive.
 */
typedef enum {GMP_FILTER_MODE_EXCLUDE, GMP_FILTER_MODE_INCLUDE}
    gmp_filter_mode;


/*
 * Address string
 *
 * This is an address, either IPv4 or IPv6, in network byte order.  The
 * address type is contextualized by its environment (IGMP vs. MLD).
 */
typedef union gmp_addrstring_ {
    uint8_t gmp_v4_addr[IPV4_ADDR_LEN]; /* IPv4 address */
    uint8_t gmp_v6_addr[IPV6_ADDR_LEN]; /* IPv6 address */
    uint8_t gmp_addr[1];        /* Generic address pointer */
} gmp_addr_string;


/*
 * GMP host interface parameters
 *
 * Passed via the gmph_set_intf_params call
 */
typedef struct gmph_intf_params_ {
    uint8_t gmph_version;        /* Protocol version */
} gmph_intf_params;

/*
 * GMP router interface parameters
 *
 * Passed via the gmpr_set_intf_params call
 */
typedef struct gmpr_intf_params_ {
    uint8_t gmpr_ifparm_version;    /* Protocol version */
    uint8_t gmpr_ifparm_robustness;    /* Robustness value */
    uint32_t gmpr_ifparm_qivl;        /* Query interval */
    uint32_t gmpr_ifparm_qrivl;    /* Query response interval */
    uint32_t gmpr_ifparm_lmqi;        /* Last member query interval */
    uint32_t gmpr_ifparm_chan_limit;    /* Channel limit */
    uint32_t gmpr_ifparm_chan_threshold; /* Channel threshold */
    uint32_t gmpr_ifparm_log_interval;  /* Time between consecutive similar limit log events */
    boolean gmpr_ifparm_fast_leave;    /* Fast leaves */
    boolean gmpr_ifparm_querier_enabled; /* Allowed to be V1 querier */
    boolean gmpr_ifparm_passive_receive; /* Passive receive */
    boolean gmpr_ifparm_suppress_gen_query; /* Suppress general queries */
    boolean gmpr_ifparm_suppress_gs_query; /* Suppress GS/GSS queries */
} gmpr_intf_params;

/*
 * IGMP versions
 */
typedef enum {
    GMP_IGMP_VERSION_UNSPEC = 0,
    GMP_IGMP_VERSION_1 = 1,
    GMP_IGMP_VERSION_2 = 2,
    GMP_IGMP_VERSION_3 = 3
} igmp_version;


/*
 * MLD versions
 */
typedef enum {
    GMP_MLD_VERSION_UNSPEC = 0,
    GMP_MLD_VERSION_1 = 1,
    GMP_MLD_VERSION_2 = 2
} mld_version;


/*
 * Timer groups
 *
 * Timers in GMP are organized into groups by type.  This allows for a group
 * to be treated as an equivalence class by the environment, for such things
 * as timer smearing.
 */
typedef enum {
    GMP_TIMER_GROUP_DEFAULT,        /* Default */
    GMP_TIMER_GROUP_GEN_QUERY,        /* General query timers */

    GMP_NUM_TIMER_GROUPS        /* Number of timer groups */
} gmp_timer_group;


/*
 * Address thread, address thread entry
 *
 * These are opaque types that carry a thread of addresses.  The
 * actual definitions are hidden.
 */
typedef struct gmp_addr_thread_ gmp_addr_thread;
typedef struct gmp_addr_thread_entry_ gmp_addr_thread_entry;
typedef struct gmp_packet_ gmp_packet;

#endif /* __GMP_H__ */
