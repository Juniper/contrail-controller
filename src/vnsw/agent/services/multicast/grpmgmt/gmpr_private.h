/* $Id: gmpr_private.h 514187 2012-05-06 12:25:25Z ib-builder $
 *
 * gmpr_private.h - Private definitions for GMP Router support
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef __GMPR_PRIVATE_H__
#define __GMPR_PRIVATE_H__

/*
 * This module defines the private data structures for GMP router support.
 * This file should only be used by the gmp_router code.
 */


/*
 * Data Structure Overview
 *
 * The primary data structure is the instance.  There are one or more of
 * these.  An instance is a grouping of interfaces under a protocol (IGMP
 * or MLD).  Multiple instances are required if both IGMP and MLD are
 * running.  Multiple instances within a protocol can be used by clients
 * if desired (this is likely to be useful only if there are different
 * clients for some interfaces and they have significantly different needs.)
 *
 * An interface structure is created for each interface.  The same structure
 * is used for both the input (protocol) and output (data forwarding) roles.
 * For generic IGMP, the same interface acts as input and output, but if
 * OIF mapping is taking place, the protocol may run on one interface and
 * the data delivered on another.  The interface is bound to an instance.
 *
 * The group state is kept in several forms.  The input state on an interface
 * corresponds to the aggregation of all host state on an interface.  This
 * is built according to the protocol spec.
 *
 * Output state is kept on the output interface.  This corresponds to
 * the forwarding state being built for the interface.  In generic
 * IGMP this is semantically identical to the input state, but if OIF
 * mapping is used it will differ, and it represents the aggregation
 * of all input state that is mapped to that output interface.
 *
 * Finally, if host tracking is taking place, state is built on a
 * per-host basis.  This is used to allow for immediate leaves and
 * query suppression (since there will be no ambiguity about when
 * state is to be deleted.)
 */

/*
 * Instance entry
 *
 * The instance block contains global storage for GMP router support.  This
 * is primarily to ensure that the code remains reentrant (no fixed storage.)
 * It also allows multiple instances of this code to be running, though the
 * semantics of that are beyond the scope of this code.
 *
 * Note that the tree of interfaces will not be in lexicographic order
 * by interface ID on little-endian machines.
 */
typedef struct gmpr_instance_ {
    uint32_t rinst_magic;             /* Magic number for robustness */
    thread rinst_thread;          /* Link on global instance thread */
    thread rinst_client_thread;        /* Head of client thread */
    thread rinst_startup_intf_thread;    /* Head of intfs starting up */

    gmpx_patroot *rinst_intfs;        /* Tree of interfaces */
    gmpx_patroot *rinst_global_state_root; /* Root of global state tree */
    gmp_proto rinst_proto;        /* Protocol (IGMP/MLD) */
    uint32_t rinst_addrlen;        /* Address length (v4 or v6) */
    gmp_addr_catalog rinst_addr_cat;    /* Address catalog */
    uint32_t rinst_min_max_resp;    /* Min value of max resp field */

    ordinal_handle rinst_ord_handle;    /* Handle for client ordinals */
    void *rinst_context;        /* Client instance context */
    gmpr_instance_context rinst_cb_context; /* Instance callbacks */

    uint32_t rinst_group_timeout;    /* Group timeout count */

    uint32_t rinst_traceflags;        /* Trace flags */

    gmpx_timer *rinst_smear_timer;    /* Timer for query timer smearing */

    boolean rinst_host_tracking;    /* TRUE if doing host tracking */
    boolean rinst_smear_timer_accelerated;  /* TRUE if we're smearing soon */
} gmpr_instance;

THREAD_TO_STRUCT(gmpr_thread_to_instance, gmpr_instance, rinst_thread);

/*
 * Query timer smear values
 *
 * In order to deal with large numbers of interfaces, we periodically smear
 * general query timers to smooth the transmission of queries.  In addition,
 * whenever a new interface is created, we accelerate the smearing so that
 * it happens not long after the new interfaces arrive.
 */
#define GMP_QUERY_SMEAR_IVL (30*60*MSECS_PER_SEC) /* Half hour seems OK */
#define GMP_QUERY_QUICK_SMEAR_IVL (10*MSECS_PER_SEC) /* Quick fix */

#define GMPR_INSTANCE_MAGIC 0x52696E73    /* 'Rins' */

typedef enum {
    GMPR_BELOW_THRESHOLD = 0,
    GMPR_ABOVE_THRESHOLD_BELOW_LIMIT,
    GMPR_ABOVE_LIMIT
}gmpr_limit_state_t;

/*
 * Interface entry
 *
 * One of these entries is created for every interface mentioned by a client.
 * It is stored on a patricia tree in the instance, keyed by interface index.
 *
 * Note that an interface may play one of two roles, or both.  The
 * first role is that of an input interface (one on which IGMP packets
 * are sent and received.)  The other is that of an output interface
 * (one on which multicast traffic is forwarded.)  Normally an
 * interface plays both roles, but when OIF mapping is taking place,
 * the two roles may be on different interfaces on a per-group or
 * per-source basis.
 */
typedef struct gmpr_intf_ {

    /* Fundamental parameters */

    gmp_version rintf_ver;        /* GMP version running */
    gmp_addr_string rintf_local_addr;    /* Our address */
    gmp_addr_string rintf_querier_addr;    /* Querier address */

    /* Linkages */

    gmpr_instance *rintf_instance;    /* Owning instance */
    gmpx_patnode rintf_inst_patnode;    /* Node on instance tree */
    gmpx_patnode rintf_global_patnode;    /* Node on global tree */
    gmpx_patroot *rintf_group_root;    /* Root of aggregated group records */
    uint32_t rintf_group_count;    /* Number of groups */
    gmpx_patroot *rintf_oif_group_root;    /* Root of output group records */
    uint32_t rintf_oif_group_count;    /* Number of output groups */
    gmpx_intf_id rintf_id;        /* Interface ID */
    gmpx_patroot *rintf_host_root;    /* Root of host entries */
    thread rintf_startup_thread;    /* Entry on instance startup thread */

    /* Query stuff */

    gmpx_timer *rintf_query_timer;    /* General query timer */
    uint32_t rintf_query_ivl;        /* General query timer interval */
    uint32_t rintf_local_query_ivl;    /* Local general query interval */
    uint32_t rintf_query_resp_ivl;    /* Query response interval */
    uint32_t rintf_local_query_resp_ivl; /* Local query response interval */
    uint32_t rintf_robustness;        /* Robustness variable */
    uint32_t rintf_local_robustness;    /* Local robustness variable */
    uint32_t rintf_lmq_ivl;        /* Last member query interval */
    uint32_t rintf_lmq_count;        /* Last member query count */
    uint32_t rintf_group_membership_ivl; /* Group membership interval */
    uint32_t rintf_lmqt;        /* Last member query time */
    uint32_t rintf_other_querier_ivl;    /* Other querier present interval */
    gmpx_timer *rintf_other_querier_present; /* Other querier present timer */
    boolean rintf_suppress_gen_query;    /* Suppress general queries */
    boolean rintf_gen_query_requested;    /* General query seq was requested */
    boolean rintf_first_query_pending;    /* First query in sequence ready */
    boolean rintf_suppress_gs_query;    /* Suppress GS/GSS queries */

    uint32_t rintf_startup_query_count;    /* Count of initial queries */

    uint32_t rintf_channel_limit;    /* Max number of channels */
    uint32_t rintf_channel_threshold;  /* Percentage of maximum at which to
                           start generating warnings */
    uint32_t rintf_log_interval;       /* Time between consecutive limit log msgs */
    time_t last_log_time;               /* Time last group limit related message
                       was logged */
    uint32_t rintf_channel_count;    /* Current channel count */
    uint32_t rintf_chan_limit_drops;    /* Count of drops due to chan limit */
    gmpr_limit_state_t rintf_limit_state; /* if count is above/below chan limit/threshold */

    /* Transmission stuff */

    thread rintf_xmit_head;        /* Head of xmit groups */
    boolean rintf_xmit_pending;        /* TRUE if we have a pending xmit */
    boolean rintf_send_gen_query;    /* TRUE if we should send gen query */

    boolean rintf_up;            /* TRUE if interface is up */
    boolean rintf_fast_leaves;        /* TRUE if doing fast leaves */
    boolean rintf_querier_enabled;    /* TRUE if allowed to be V1 querier */
    boolean rintf_querier;        /* TRUE if we are querier */

    /* Receive stuff */

    boolean rintf_passive_receive;    /* Passive receive */
} gmpr_intf;

GMPX_PATNODE_TO_STRUCT(gmpr_inst_patnode_to_intf, gmpr_intf,
               rintf_inst_patnode);
GMPX_PATNODE_TO_STRUCT(gmpr_global_patnode_to_intf, gmpr_intf,
               rintf_global_patnode);

#define GMPR_QUERY_IVL_DEFAULT 125000    /* Default query interval */
#define GMPR_QUERY_RESP_IVL_DEFAULT 10000 /* Default query response interval */
#define GMPR_INIT_FIRST_QUERY_IVL 100    /* Initial query delay */
#define GMPR_INIT_LATER_QUERY_IVL 2000    /* Later startup query delay */
#define GMPR_LMQI_DEFAULT 1000        /* Default LMQI value */


/*
 * Notification block
 *
 * This block contains linkages for notifications to clients about
 * changes to GMP state.  It is embedded in groups (for notification
 * of group-wide changes) and address entries (for notification of
 * source changes.)  The same mechanism is used for host-related
 * notifications, and is embedded in host group and host source
 * entries in the same way.
 *
 * The REFRESH_END type is used to mark the end of a client refresh
 * sequence; this block is embeded in the client structure itself.
 *
 * The type field provides the ability to obtain the address of the
 * enclosing data structure.
 *
 * In order to minimize the number of calls to the client callbacks,
 * we keep per-client indications of whether a notification callback
 * should be made.  This is set when the first notification of the
 * type (regular or host) is enqueued, cleared when the callback is made,
 * and not set again until after the queue is drained.
 */
typedef enum {
    GMPR_NOTIFY_GROUP = 1,        /* Group notification */
    GMPR_NOTIFY_SOURCE,            /* Source notification */
    GMPR_NOTIFY_REFRESH_END,        /* End of refresh sequence */
    GMPR_NOTIFY_HOST_GROUP,        /* Host group notification */
    GMPR_NOTIFY_HOST_SOURCE,        /* Host source notification */
} gmpr_notification_type;

typedef struct gmpr_notify_block_ {
    gmpr_notification_type gmpr_notify_type; /* Notification type */
    thread gmpr_notify_thread;        /* Entry on client notify thread */
} gmpr_notify_block;

THREAD_TO_STRUCT(gmpr_thread_to_notify_block, gmpr_notify_block,
         gmpr_notify_thread);


/*
 * Input interface group entry
 *
 * This data structure defines the group record for an input interface.
 * This represents a group requested by one or more hosts connected to
 * the interface.
 *
 * Source addresses are kept in two lists, one for those with source
 * timers running, and the other for those with source timers stopped.
 * In Include mode, the running-timer list constitutes the included
 * sources.  In Exclude mode, the stopped-timer list constitutes the
 * excluded sources.
 *
 * Two lists are maintained to help with sending queries.  One list has
 * all of the sources to be queried that have timers greater than LMQT,
 * and the other has the sources to be queried that have timers less than
 * LMQT.
 *
 * Input groups are linked to OIF groups if the input group is a (*,G)
 * entry.  Otherwise, the input group is not linked (but the individual
 * sources are.)
 */
typedef struct gmpr_group_ {

    /* Linkages */

    gmpr_intf *rgroup_intf;        /* Pointer back to interface */
    gmpx_patnode rgroup_intf_patnode;    /* Node on interface tree of groups */
    thread rgroup_host_group_head;    /* Head of host group thread */
    thread rgroup_oif_thread;        /* Entry on OIF group thread */
    struct gmpr_ogroup_ *rgroup_oif_group; /* OIF group pointer */

    /* Group state */

    gmp_addr_string rgroup_addr;    /* Group address */
    gmp_filter_mode rgroup_filter_mode;    /* Include/Exclude */
    gmp_addr_list rgroup_src_addr_running; /* Sources with running timers */
    gmp_addr_list rgroup_src_addr_stopped; /* Sources with stopped timers */
    gmpx_timer *rgroup_group_timer;    /* Group timer */
    gmp_addr_string rgroup_last_reporter; /* Address of last reporting host */

    /* Query stuff */

    gmpx_timer *rgroup_query_timer;    /* For sending group queries */
    gmpx_timer *rgroup_gss_query_timer;    /* For sending GSS queries */
    uint8_t rgroup_query_rexmit_count;    /* Rexmit count for group queries */
    gmp_addr_list rgroup_query_lo_timers; /* List of sources with lo timers */
    gmp_addr_list rgroup_query_hi_timers; /* List of sources with hi timers */

    /* Version compatibility stuff */

    gmp_version rgroup_compatibility_mode; /* Group version */
    gmpx_timer *rgroup_basic_host_present; /* Basic version host present */
    gmpx_timer *rgroup_leaves_host_present; /* Leaves version host present */

    /* Transmission stuff */

    thread rgroup_xmit_thread;        /* Entry on intf transmit list */
    boolean rgroup_send_gss_query;    /* We need to send a GSS query */
    boolean rgroup_send_group_query;    /* We need to send a group query */
} gmpr_group;

GMPX_PATNODE_TO_STRUCT(gmpr_intf_patnode_to_group, gmpr_group,
               rgroup_intf_patnode);
THREAD_TO_STRUCT(gmpr_xmit_thread_to_group, gmpr_group, rgroup_xmit_thread);
THREAD_TO_STRUCT(gmpr_oif_thread_to_group, gmpr_group, rgroup_oif_thread);


/*
 * Input interface group member address entry
 *
 * This structure represents a single source address in an input
 * interface group.  It consists of an address entry structure plus a
 * timer, used to age the address entry, and a retransmission counter,
 * used for counting GSS queries sent with this address.  It also
 * carries a group pointer, so we can get back to the group as
 * necessary, and client notification linkages.
 *
 * The address entry is linked to the corresponding output entry, which
 * may be bound to a different interface if OIF mapping is in use.
 */
typedef struct gmpr_group_addr_entry_
{
    gmp_addr_list_entry rgroup_addr_entry;    /* Address entry */
    gmpr_group *rgroup_addr_group;    /* Owning group */
    gmpx_timer *rgroup_addr_timer;    /* Timer */
    uint8_t rgroup_addr_rexmit_count;    /* Query retransmission count */
    thread rgroup_addr_oif_thread;    /* Entry on OIF thread */
    struct gmpr_ogroup_addr_entry_ *rgroup_addr_oif_addr; /* OIF address */

    /* Host stuff */

    thread rgroup_host_addr_head;    /* Head of host address thread */

    /* Wasted space */

    gmp_addr_string rgroup_addr_last_reporter; /* Last host to mention us */

} gmpr_group_addr_entry;

THREAD_TO_STRUCT(gmpr_oif_thread_to_group_addr_entry, gmpr_group_addr_entry,
         rgroup_addr_oif_thread);
EMBEDDED_STRUCT_TO_STRUCT(gmpr_addr_entry_to_group_entry,
              gmpr_group_addr_entry, gmp_addr_list_entry,
              rgroup_addr_entry);


/*
 * Output interface group entry
 *
 * This structure represents a group on an output interface.  For
 * generic IGMP/MLD, this will map one-to-one with the input interface
 * group entry and will be semantically the same.  When OIF mapping is
 * taking place, this entry may have contributors from multiple input
 * interface groups.
 *
 * This structure contains two address lists, one for Include
 * contributions and one for Exclude contributions.  This is necessary
 * because the contributing input interfaces may be in different
 * states (Include or Exclude.)  If the output group is in Include
 * state, the set of sources is that on the Include list.  If the
 * output group is in Exclude state, the set of sources is that on the
 * Exclude list, *minus* the ones on the Include list (since those are
 * not to be excluded.)  Note that the same source address may be present
 * on both the include and exclude lists.
 *
 * If the output group is a (*,G), it has a thread of all contributing
 * input groups.  If there are sources, the contributing input sources
 * are bound to the individual output sources.
 *
 * The output group is bound to the output interface.
 *
 * Client notifications are done with output groups and sources, since they
 * represent the actual forwarding state.
 */
typedef struct gmpr_ogroup_ {
    gmpr_intf *rogroup_intf;        /* Pointer back to interface */
    gmpx_patnode rogroup_intf_patnode;    /* Node on interface tree of groups */
    gmpr_notify_block rogroup_client_thread[GMPX_MAX_RTR_CLIENTS];
                    /* Notification blocks */
    boolean rogroup_send_full_notif[GMPX_MAX_RTR_CLIENTS];
                        /* TRUE if we should send full notif */
    thread rogroup_global_thread;    /* Entry on global state thread */
    thread rogroup_oif_head;        /* Head of input groups contributing */

    /* Group state */

    gmp_addr_string rogroup_addr;    /* Group address */
    gmp_filter_mode rogroup_filter_mode; /* Include/Exclude */
    gmp_addr_list rogroup_incl_src_addr; /* Active include sources */
    gmp_addr_list rogroup_excl_src_addr; /* Active exclude sources */
    gmp_addr_list rogroup_src_addr_deleted; /* Deleted sources for notify */
} gmpr_ogroup;

GMPX_PATNODE_TO_STRUCT(gmpr_oif_patnode_to_group, gmpr_ogroup,
               rogroup_intf_patnode);
THREAD_TO_STRUCT(gmpr_global_thread_to_group, gmpr_ogroup,
         rogroup_global_thread);


/*
 * Output interface group member address entry
 *
 * This structure represents a single source address in an output
 * interface group.  It represents a single source being included
 * or excluded on the output interface.
 *
 * Note that the same source address may be present on both the exclude
 * and include lists on the output group.
 *
 * A thread of all contributing input group sources is included.
 */
typedef struct gmpr_ogroup_addr_entry_
{
    gmp_addr_list_entry rogroup_addr_entry;    /* Address entry */
    gmpr_ogroup *rogroup_addr_group;    /* Owning group */
    thread rogroup_addr_oif_head;    /* Head of contributing sources */

    /* Notification stuff */

    gmpr_notify_block rogroup_addr_client_thread[GMPX_MAX_RTR_CLIENTS];
                    /* Notification blocks */
    boolean rogroup_notify;        /* TRUE if notification required */
} gmpr_ogroup_addr_entry;


EMBEDDED_STRUCT_TO_STRUCT(gmpr_addr_entry_to_ogroup_entry,
              gmpr_ogroup_addr_entry, gmp_addr_list_entry,
              rogroup_addr_entry);


/*
 * Client entry
 *
 * Each client has an associated data structure that tracks its associated
 * state.
 */
typedef struct gmpr_client_ {
    uint32_t rclient_magic;        /* Magic number for robustness */
    gmpr_instance *rclient_instance;    /* Owning instance */
    ordinal_t rclient_ordinal;        /* Client ordinal */
    thread rclient_thread;        /* Link on instance client thread */
    void *rclient_context;        /* Client context */

    thread rclient_notif_head;        /* Head of group notifications */
    thread rclient_host_notif_head;    /* Head of host notifications */
    gmpr_client_context rclient_cb_context; /* Client callback */
    gmpr_notify_block rclient_refresh_end_notif; /* End-refresh notification */

    gmpx_timer *rclient_startup_timer;    /* Startup timer */
    boolean rclient_notify;        /* TRUE if we need to notify */
    boolean rclient_host_notify;    /* Ditto for host notifications */
} gmpr_client;

THREAD_TO_STRUCT(gmpr_thread_to_client, gmpr_client, rclient_thread);

#define GMPR_CLIENT_MAGIC 0x52636C69    /* 'Rcli' */


/*
 * Host entry
 *
 * If host tracking is active, this structure exists for each host seen
 * in the protocol.  Host entries are used for two purposes--accounting
 * and fast leaves.
 *
 * Host entries are added to a tree rooted at the interface and keyed
 * by the host address.
 */
typedef struct gmpr_host_ {
    gmpx_patnode rhost_node;        /* Node on interface tree */
    gmp_addr_string rhost_addr;        /* Host address */
    gmpr_intf *rhost_intf;        /* Interface pointer */
    gmpx_patroot *rhost_group_root;    /* Root of group tree */
} gmpr_host;

GMPX_PATNODE_TO_STRUCT(gmpr_patnode_to_host, gmpr_host, rhost_node);


/*
 * Host group
 *
 * This structure represents a group from the perspective of a host.  It
 * lives on a tree rooted at the host entry, and on a thread rooted
 * at the main group entry.
 *
 * We only keep track of active groups and sources;  in particular, if the
 * host requests a non-null Exclude list, we do not track those sources
 * here (in that case it would look like a group join.)
 *
 * If the rhgroup_group pointer is NULL, the entry is inactive (has no
 * active source or group state) and is not linked to the main group
 * entry.  If the pointer is set and there are no sources present, the
 * group is active for all sources.
 *
 * Note that we don't keep a filter mode (include/exclude) here, because
 * we know by implication--a group with no sources is a (*,G), and a
 * group with sources is an Include-mode set of (S,G)s.
 *
 * We do limited refcounting to avoid double-freeing this structure
 * as a side effect of deleting source addresses.
 */
typedef struct gmpr_host_group_ {
    gmpx_patnode rhgroup_node;        /* Node on tree */
    gmp_addr_string rhgroup_addr;    /* Group address */
    gmpr_host *rhgroup_host;        /* Host pointer */
    gmp_addr_list rhgroup_addrs;    /* List of sources */
    gmp_addr_list rhgroup_deleted;    /* Deleted sources awaiting notify  */
    thread rhgroup_thread;        /* Entry on main group thread */
    gmpr_group *rhgroup_group;        /* Pointer to main group entry */
    gmpx_timer *rhgroup_timer;        /* Host group timer */
    gmpr_notify_block rhgroup_notify[GMPX_MAX_RTR_CLIENTS];
                    /* Notification block */
    boolean rhgroup_is_deleted;        /* Deleted, but locked */
    uint8_t rhgroup_lock_count;    /* Lock count */
} gmpr_host_group;

THREAD_TO_STRUCT(gmpr_thread_to_host_group, gmpr_host_group, rhgroup_thread);
GMPX_PATNODE_TO_STRUCT(gmpr_patnode_to_host_group, gmpr_host_group,
               rhgroup_node);

/*
 * gmpr_host_group_active
 *
 * Returns TRUE if the host group is active (is bound to a group) or
 * FALSE otherwise.
 */
static inline boolean
gmpr_host_group_active (gmpr_host_group *host_group)
{
    /* Tolerate NULL pointers. */

    if (!host_group)
    return FALSE;

    /* The host group is active if it's bound to a group. */

    return (host_group->rhgroup_group != NULL);
}


/*
 * Host group address entry
 *
 * This structure represents a single source address on a host group entry.
 */
typedef struct gmpr_host_group_addr_ {
    gmp_addr_list_entry rhga_addr_ent;    /* Address entry */
    thread rhga_thread;            /* Entry on main addr entry thread */
    gmpr_group_addr_entry *rhga_source;    /* Pointer to main addr entry */
    gmpr_notify_block rhga_notify[GMPX_MAX_RTR_CLIENTS];
                    /* Notification block */
    gmpr_host_group *rhga_host_group;    /* Pointer to owning host group */
    gmpx_timer *rhga_timer;        /* Host source timer */
} gmpr_host_group_addr;

EMBEDDED_STRUCT_TO_STRUCT(gmpr_addr_entry_to_host_group_entry,
              gmpr_host_group_addr, gmp_addr_list_entry,
              rhga_addr_ent);
THREAD_TO_STRUCT(gmpr_thread_to_host_group_addr_entry, gmpr_host_group_addr,
         rhga_thread);


/*
 * Global table entries
 *
 * We keep a global linkage into the state across all interfaces and
 * groups.  Looking up in the global table based on group leads to a
 * thread of all group entries within that instance that match the
 * group address.  This is done so that the
 * gmp_router_which_interfaces() call can be implemented; it returns
 * all of the interfaces on which the entry was heard.
 *
 * This is an abomination and should be ripped out (at considerable memory
 * savings) once PIM becomes smart enough to not lose track of the interfaces
 * provided in the notifications.
 */
typedef struct gmpr_global_group_ {
    patnode global_group_node;        /* Entry on global tree */
    gmp_addr_string global_group_addr;    /* Group address */
    thread global_group_head;        /* Head of thread of groups */
} gmpr_global_group;

GMPX_PATNODE_TO_STRUCT(gmpr_patnode_to_global_group, gmpr_global_group,
               global_group_node);

/*
 * Inlines...
 */

/*
 * gmpr_client_notification_to_group
 *
 * Return the address of an output group, given a pointer to the
 * client notification and the client ordinal.
 *
 * Returns NULL with a null pointer.
 */
static inline gmpr_ogroup *
gmpr_client_notification_to_group (gmpr_notify_block *notify_block,
                   ordinal_t ordinal)
{
    uint8_t *byte_ptr;
    gmpr_ogroup *group;

    if (!notify_block)
    return NULL;

    gmpx_assert(ordinal < GMPX_MAX_RTR_CLIENTS);

    /*
     * Do the grody pointer math.  First, adjust for the client
     * ordinal offset.
     */
    notify_block -= ordinal;

    /* Now back up to the start of the group. */

    byte_ptr = (uint8_t *) notify_block;
    byte_ptr -= offsetof(gmpr_ogroup, rogroup_client_thread);

    group = (gmpr_ogroup *) byte_ptr;

    return group;
}


/*
 * gmpr_client_notification_to_addr_entry
 *
 * Return the address of a group member address entry, given a pointer
 * to the client notification and the client ordinal.
 *
 * Returns NULL with a null pointer.
 */
static inline gmpr_ogroup_addr_entry *
gmpr_client_notification_to_addr_entry (gmpr_notify_block *notify_block,
                    ordinal_t ordinal)
{
    uint8_t *byte_ptr;
    gmpr_ogroup_addr_entry *group_addr;

    gmpx_assert(ordinal < GMPX_MAX_RTR_CLIENTS);

    /* Bail if the ordinal is out of range. */

    if (ordinal >= GMPX_MAX_RTR_CLIENTS)
    return NULL;

    /* Adjust for the client ordinal offset. */

    notify_block -= ordinal;

    /* Now back up to the start of the address entry. */

    byte_ptr = (uint8_t *) notify_block;
    byte_ptr -= offsetof(gmpr_ogroup_addr_entry, rogroup_addr_client_thread);

    group_addr = (gmpr_ogroup_addr_entry *) byte_ptr;

    return group_addr;
}

/*
 * gmpr_client_notification_to_host_group
 *
 * Return the address of a host group, given a pointer to the client
 * notification and the client ordinal.
 *
 * Returns NULL with a null pointer.
 */
static inline gmpr_host_group *
gmpr_client_notification_to_host_group (gmpr_notify_block *notify_block,
                    ordinal_t ordinal)
{
    uint8_t *byte_ptr;
    gmpr_host_group *host_group;

    if (!notify_block)
    return NULL;

    gmpx_assert(ordinal < GMPX_MAX_RTR_CLIENTS);

    /*
     * Do the grody pointer math.  First, adjust for the client
     * ordinal offset.
     */
    notify_block -= ordinal;

    /* Now back up to the start of the group. */

    byte_ptr = (uint8_t *) notify_block;
    byte_ptr -= offsetof(gmpr_host_group, rhgroup_notify);

    host_group = (gmpr_host_group *) byte_ptr;

    return host_group;
}


/*
 * gmpr_client_notification_to_host_group_addr
 *
 * Return the address of a host group address entry, given a pointer
 * to the client notification and the client ordinal.
 *
 * Returns NULL with a null pointer.
 */
static inline gmpr_host_group_addr *
gmpr_client_notification_to_host_group_addr (gmpr_notify_block *notify_block,
                         ordinal_t ordinal)
{
    uint8_t *byte_ptr;
    gmpr_host_group_addr *group_addr;

    gmpx_assert(ordinal < GMPX_MAX_RTR_CLIENTS);

    /* Bail if the ordinal is out of range. */

    if (ordinal >= GMPX_MAX_RTR_CLIENTS)
    return NULL;

    /* Adjust for the client ordinal offset. */

    notify_block -= ordinal;

    /* Now back up to the start of the address entry. */

    byte_ptr = (uint8_t *) notify_block;
    byte_ptr -= offsetof(gmpr_host_group_addr, rhga_notify);

    group_addr = (gmpr_host_group_addr *) byte_ptr;

    return group_addr;
}


/*
 * gmpr_group_addr_deleted
 *
 * Returns TRUE if the address entry is on the group deleted list.
 */
static inline boolean
gmpr_group_addr_deleted (gmpr_ogroup_addr_entry *group_addr)
{
    gmp_addr_list_entry *addr_entry;
    gmpr_ogroup *group;

    addr_entry = &group_addr->rogroup_addr_entry;
    group = group_addr->rogroup_addr_group;
    return (addr_entry->addr_ent_list == &group->rogroup_src_addr_deleted);
}


/*
 * gmpr_group_addr_included
 *
 * Returns TRUE if the address entry is on the group include list.
 */
static inline boolean
gmpr_group_addr_included (gmpr_ogroup_addr_entry *group_addr)
{
    gmp_addr_list_entry *addr_entry;
    gmpr_ogroup *group;

    addr_entry = &group_addr->rogroup_addr_entry;
    group = group_addr->rogroup_addr_group;
    return (addr_entry->addr_ent_list == &group->rogroup_incl_src_addr);
}


/*
 * gmpr_group_addr_mode
 *
 * Returns the filter mode (include/exclude) associated with an output
 * group source entry.
 */
static inline gmp_filter_mode
gmpr_group_addr_mode (gmpr_ogroup_addr_entry *group_addr)
{
    gmp_filter_mode mode;

    gmpx_assert(!gmpr_group_addr_deleted(group_addr));
    mode = GMP_FILTER_MODE_EXCLUDE;
    if (gmpr_group_addr_included(group_addr))
    mode =  GMP_FILTER_MODE_INCLUDE;

    return mode;
}


/*
 * gmpr_group_addr_excluded
 *
 * Returns TRUE if the address entry is on the group exclude list.
 */
static inline boolean
gmpr_group_addr_excluded (gmpr_ogroup_addr_entry *group_addr)
{
    gmp_addr_list_entry *addr_entry;
    gmpr_ogroup *group;

    addr_entry = &group_addr->rogroup_addr_entry;
    group = group_addr->rogroup_addr_group;
    return (addr_entry->addr_ent_list == &group->rogroup_excl_src_addr);
}


/*
 * gmpr_host_group_addr_deleted
 *
 * Returns TRUE if the address entry is on the host group deleted list.
 */
static inline boolean
gmpr_host_group_addr_deleted (gmpr_host_group_addr *group_addr)
{
    gmp_addr_list_entry *addr_entry;
    gmpr_host_group *host_group;

    addr_entry = &group_addr->rhga_addr_ent;
    host_group = group_addr->rhga_host_group;
    return (addr_entry->addr_ent_list == &host_group->rhgroup_deleted);
}


/*
 * gmpr_group_is_active
 *
 * Returns TRUE if the input group is active (in Exclude mode, or a
 * non-null source list) or FALSE if not.
 */
static inline boolean
gmpr_group_is_active (gmpr_group *group)
{
    if (group->rgroup_filter_mode == GMP_FILTER_MODE_EXCLUDE)
    return TRUE;
    if (!gmp_addr_list_empty(&group->rgroup_src_addr_running))
    return TRUE;
    return FALSE;
}


/*
 * gmpr_ogroup_is_active
 *
 * Returns TRUE if the output group is active (in Exclude mode, or a
 * non-null source list) or FALSE if not.
 */
static inline boolean
gmpr_ogroup_is_active (gmpr_ogroup *ogroup)
{
    if (ogroup->rogroup_filter_mode == GMP_FILTER_MODE_EXCLUDE)
    return TRUE;
    if (!gmp_addr_list_empty(&ogroup->rogroup_incl_src_addr))
    return TRUE;
    return FALSE;
}


/*
 * gmpr_ogroup_source_list_by_mode
 *
 * Returns a pointer to the appropriate source list for an output group
 * (the include list if in Include mode, or the exclude if not) based
 * on the supplied filter mode.
 */
static inline gmp_addr_list *
gmpr_ogroup_source_list_by_mode (gmpr_ogroup *group, gmp_filter_mode mode)
{
    if (mode == GMP_FILTER_MODE_INCLUDE) {
    return &group->rogroup_incl_src_addr;
    } else {
    return &group->rogroup_excl_src_addr;
    }
}


/*
 * gmpr_ogroup_source_list
 *
 * Returns a pointer to the appropriate source list for an output group
 * (the include list if in Include mode, or the exclude if not.)
 */
static inline gmp_addr_list *
gmpr_ogroup_source_list (gmpr_ogroup *group)
{
    return gmpr_ogroup_source_list_by_mode(group, group->rogroup_filter_mode);
}


/*
 * gmpr_all_group_lists_empty
 *
 * Returns TRUE if there are no sources anywhere on a group, or FALSE if not.
 */
static inline boolean
gmpr_all_group_lists_empty (gmpr_group *group)
{
    return (gmp_addr_list_empty(&group->rgroup_src_addr_running) &&
        gmp_addr_list_empty(&group->rgroup_src_addr_stopped));
}


/*
 * gmpr_group_source_list
 *
 * Returns a pointer to the appropriate source list for an input group
 * (the running list if in Include mode, or the stopped list if not.)
 */
static inline gmp_addr_list *
gmpr_group_source_list (gmpr_group *group)
{
    if (group->rgroup_filter_mode == GMP_FILTER_MODE_INCLUDE) {
    return &group->rgroup_src_addr_running;
    } else {
    return &group->rgroup_src_addr_stopped;
    }
}


/*
 * gmpr_source_ord_is_active
 *
 * Returns TRUE if the source corresponding to an address ordinal is
 * active on an output group, or FALSE if not.
 *
 * The source is active if it is in the include list and the group is
 * in Include mode, or if it is in the exclude list and *not* in the
 * include list if the group is in Exclude mode.
 */
static inline boolean
gmpr_source_ord_is_active (gmpr_ogroup *group, ordinal_t ord)
{
    boolean in_include;

    /* See if it's in the include list. */

    in_include = gmp_addr_in_list(&group->rogroup_incl_src_addr, ord);

    /* If the group is in Include mode, it's active if in the include list. */

    if (group->rogroup_filter_mode == GMP_FILTER_MODE_INCLUDE)
    return in_include;

    /*
     * Exclude mode.  The source is active if it's in the exclude list and
     * not in the include list.
     */
    return (gmp_addr_in_list(&group->rogroup_excl_src_addr, ord) &&
        !in_include);
}


/*
 * gmpr_source_is_active
 *
 * Returns TRUE if a source is active on an output group, or FALSE if not.
 *
 * The source is assumed to be from the list matching the filter state
 * (include or exclude.)  The source is active if the group is in
 * Include mode (since all sources are, by definition.)  If the group
 * is in Exclude mode, the source is active (excluded) only if the
 * source is *not* in the Include list.
 */
static inline boolean
gmpr_source_is_active (gmpr_ogroup *group, gmpr_ogroup_addr_entry *group_addr)
{
    /* Active if we're in include mode. */

    if (group->rogroup_filter_mode == GMP_FILTER_MODE_INCLUDE)
    return TRUE;

    /*
     * Active if the excluded source is *not* in the Include list, or
     * inactive if it is.
     */
    return (!gmp_addr_in_list(&group->rogroup_incl_src_addr,
                  group_addr->rogroup_addr_entry.addr_ent_ord));
}


/*
 * oif_update_type
 *
 * Enum for gmpr_update_group_oif and gmpr_update_source_oif
 */
typedef enum {
    OIF_DELETE,                /* Deleting component */
    OIF_UPDATE,                /* Updating component */
} oif_update_type;


/*
 * Externals
 */
extern gmpx_block_tag gmpr_instance_tag;
extern gmpx_block_tag gmpr_client_tag;
extern gmpx_block_tag gmpr_intf_tag;
extern gmpx_block_tag gmpr_host_tag;
extern gmpx_block_tag gmpr_host_group_tag;
extern gmpx_block_tag gmpr_host_group_addr_tag;
extern gmpx_block_tag gmpr_group_tag;
extern gmpx_block_tag gmpr_ogroup_tag;
extern gmpx_block_tag gmpr_group_addr_entry_tag;
extern gmpx_block_tag gmpr_ogroup_addr_entry_tag;
extern gmpx_block_tag gmpr_notification_tag;
extern gmpx_block_tag gmpr_host_notification_tag;
extern gmpx_block_tag gmpr_global_group_tag;
extern gmpx_block_tag gmpr_intf_list_tag;
extern gmpx_block_tag gmpr_intf_group_tag;
extern thread gmpr_global_instance_thread;
extern gmpx_patroot *gmpr_global_intf_tree[];


/* gmpr_instance.c */

extern gmpr_instance *gmpr_instance_create(gmp_proto proto,
                       void *inst_context);
extern gmpr_instance *gmpr_get_instance(gmp_instance_id instance_id);
extern void gmpr_instance_destroy(gmpr_instance *instance);
extern void gmpr_accelerate_query_smear(gmpr_instance *instance);

/* gmpr_client.c */

extern gmpr_client *gmpr_create_client(gmpr_instance *instance);
extern gmpr_client *gmpr_get_client(gmp_client_id client_id);
extern void gmpr_destroy_client(gmpr_client *client);
extern void gmpr_destroy_instance_clients(gmpr_instance *instance);
typedef enum {NOTIFY_CONDITIONAL, NOTIFY_UNCONDITIONAL}
    gmpr_source_notify_flag;
extern void gmpr_source_notify_clients(gmpr_ogroup_addr_entry *group_addr,
                       gmpr_source_notify_flag flag);
extern void gmpr_client_enqueue_all_groups(gmpr_client *client, boolean flush);
extern void gmpr_client_enqueue_all_intf_groups(gmpr_client *client,
                        gmpr_intf *intf,
                        boolean flush);
extern gmpr_client_notification *
    gmpr_client_get_notification(gmpr_client *client,
                 gmpr_client_notification *last_notification);
extern void gmpr_free_notification(gmpr_client_notification *notification);
extern void gmpr_mode_change_notify_clients(gmpr_ogroup *group);
extern void gmpr_group_notify_clients(gmpr_ogroup *group);
extern void gmpr_alert_clients(gmpr_instance *instance);
extern boolean gmpr_notifications_active(gmpr_notify_block *notify_block);
extern void gmpr_set_notification_type(gmpr_notify_block *notify_block,
                       gmpr_notification_type notify_type);
extern void gmpr_flush_notifications(gmpr_notify_block *notify_block,
                     boolean just_delink);
extern void gmpr_enqueue_refresh_end(gmpr_client *client);

/* gmpr_intf.c */

extern int gmpr_intf_set_params(gmpr_instance *instance, gmpx_intf_id intf_id,
                gmpr_intf_params *params);
extern gmpr_intf *gmpr_intf_lookup(gmpr_instance *instance,
                   gmpx_intf_id intf_id);
extern gmpr_intf *gmpr_intf_lookup_global(gmp_proto proto,
                      gmpx_intf_id intf_id);
extern void gmpr_kick_xmit(gmpr_intf *intf);
extern int gmpr_attach_intf_internal(gmpr_instance *instance,
                     gmpx_intf_id intf_id);
extern int gmpr_detach_intf_internal(gmpr_instance *instance,
                     gmpx_intf_id intf_id);
extern void gmpr_destroy_instance_intfs(gmpr_instance *instance);
extern void gmpr_intf_update_robustness(gmpr_intf *intf, uint32_t robustness);
extern void gmpr_intf_update_query_ivl(gmpr_intf *intf, uint32_t query_ivl);
extern void gmpr_update_querier(gmpr_intf *intf, gmp_addr_string *addr,
                boolean querier);
extern void gmpr_setup_initial_query_timer(gmpr_intf *intf);
extern void gmpr_trigger_one_query(gmpr_intf *intf);
extern gmpr_intf *gmpr_next_instance_intf(gmpr_instance *instance,
                      gmpr_intf *prev_intf);

/* gmpr_group.c */

extern gmpr_group *gmpr_group_create(gmpr_intf *intf,
                     const gmp_addr_string *group_addr);
extern gmpr_group *gmpr_group_lookup(gmpr_intf *intf, const uint8_t *group);
extern gmpr_ogroup *gmpr_ogroup_lookup(gmpr_intf *intf, const uint8_t *group);
extern gmpx_timer *gmpr_create_change_report_timer(gmpr_group *group);
extern gmpr_group *gmpr_first_group_xmit(gmpr_intf *intf);
extern void gmpr_dequeue_group_xmit(gmpr_group *group);
extern void gmpr_attempt_group_free(gmpr_group *group);
extern boolean gmpr_attempt_ogroup_free(gmpr_ogroup *ogroup);
extern void gmpr_destroy_intf_groups(gmpr_intf *intf);
extern void gmpr_enqueue_group_xmit(gmpr_group *group);
extern void gmpr_init_group_addr_entry(gmpr_group *group,
                       gmp_addr_list_entry *addr_entry);
extern gmpr_global_group *gmpr_link_global_group(gmpr_ogroup *group);
extern void gmpr_delink_global_group(gmpr_ogroup *group);
extern gmpr_global_group *gmpr_lookup_global_group(gmpr_instance *instance,
                           uint8_t *group_addr);
extern void gmpr_evaluate_group_version(gmpr_group *group);
extern gmp_version gmpr_group_version(gmpr_intf *intf, gmpr_group *group);
extern boolean gmpr_group_forwards_all_sources(gmpr_ogroup *group);
extern boolean gmpr_group_forwards_source(gmpr_ogroup *group,
                      const uint8_t *source);
extern void gmpr_timeout_group(gmpr_group *group);
extern void gmpr_update_group_oif(gmpr_group *group, oif_update_type type);
extern void gmpr_update_source_oif(gmpr_group_addr_entry *group_addr,
                   oif_update_type type);
extern void gmpr_update_oif_mode_change(gmpr_group *group);
extern void gmpr_notify_oif_map_change_internal(gmpr_intf *intf);
extern gmpr_group *gmpr_next_intf_group(gmpr_intf *intf, gmpr_group *group);
extern gmpr_ogroup *gmpr_next_oif_group(gmpr_intf *oif,    gmpr_ogroup *group);
extern void gmpr_update_intf_output_groups(gmpr_intf *intf);
extern void gmpr_flush_intf_input_groups(gmpr_intf *intf);
extern void gmpr_enqueue_all_source_notifications(gmpr_ogroup *ogroup,
                          gmpr_client *client);
extern void gmpr_flush_notifications_group(gmpr_ogroup *ogroup);
extern boolean gmpr_check_grp_limit(gmpr_intf *intf, boolean incr);

/* gmpr_engine.c */

extern void gmpr_register_packet_handler(void);
extern void gmpr_group_query_timer_expiry (gmpx_timer *timer, void *context);
extern void gmpr_group_timer_expiry (gmpx_timer *timer, void *context);
extern void gmpr_gss_query_timer_expiry (gmpx_timer *timer, void *context);
extern void gmpr_source_timer_expiry (gmpx_timer *timer, void *context);
extern void
    gmpr_last_host_addr_ref_gone(gmpr_group_addr_entry *group_addr_entry);
extern void gmpr_last_host_group_ref_gone(gmpr_group *group);

/* gmpr_host.c */

extern void
    gmpr_host_process_report(uint8_t *src_addr, gmp_report_rectype rec_type,
                 gmpr_group *group, gmp_addr_vect *source_vect);
extern gmpr_client_host_notification *
    gmpr_client_get_host_notification(gmpr_client *client,
              gmpr_client_host_notification *last_notification);
extern void gmpr_client_free_host_notification(
                   gmpr_client_host_notification *host_notif);
extern void gmpr_alert_host_clients(gmpr_instance *instance);
extern void gmpr_client_enqueue_all_host_groups(gmpr_client *client);
extern void gmpr_flush_host_notifications_client(gmpr_client *client);
extern void gmpr_destroy_intf_hosts(gmpr_intf *intf);
extern void gmpr_host_notify_oif_map_change(gmpr_group *group);
extern gmpr_host *gmpr_lookup_host(gmpr_intf *intf, const uint8_t *host_addr);

#endif /* __GMPR_PRIVATE_H__ */
