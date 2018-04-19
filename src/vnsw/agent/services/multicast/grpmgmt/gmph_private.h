/* $Id: gmph_private.h 493569 2012-01-28 13:26:58Z ib-builder $
 *
 * gmph_private.h - Private definitions for GMP Host support
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef __GMPH_PRIVATE_H__
#define __GMPH_PRIVATE_H__

/*
 * This module defines the private data structures for GMP host support.
 * This file should only be used by the gmp_host code.
 */

/*
 * Instance entry
 *
 * The instance block contains global storage for GMP host support.  This
 * is primarily to ensure that the code remains reentrant (no fixed storage.)
 * It also allows multiple instances of this code to be running, though the
 * semantics of that are beyond the scope of this code.
 *
 * Note that the tree of interfaces will not be in lexicographic order
 * by interface ID on little-endian machines.
 */
typedef struct gmph_instance_ {
    u_int32_t hinst_magic;             /* Magic number for robustness */
    thread hinst_thread;        /* Link on global instance thread */
    thread hinst_client_thread;        /* Head of client thread */
    gmp_addr_catalog hinst_addr_cat;    /* Address catalog */

    void *hinst_context;        /* External context */

    gmpx_patroot *hinst_intfs;        /* Tree of interfaces */
    gmp_proto hinst_proto;        /* Protocol (IGMP/MLD) */
    u_int hinst_addrlen;        /* Address length (v4 or v6) */
    gmpx_timer *hinst_master_clock;    /* Timer for timekeeping */
} gmph_instance;

THREAD_TO_STRUCT(gmph_thread_to_instance, gmph_instance, hinst_thread);

#define GMPH_INSTANCE_MAGIC 0x48696E73    /* 'Hins' */


/* An enum to define report types. */

typedef enum {
    GMP_CHANGE_RECORD,            /* Change record */
    GMP_CURRENT_STATE            /* Current state */
} gmph_report_type;


/*
 * Interface entry
 *
 * One of these entries is created for every interface mentioned by a client.
 * It is stored on a patricia tree in the instance, keyed by interface index.
 *
 * There are a couple of fields associated with pending transmissions on the
 * interface.  hintf_pending_xmit_count keeps track of the number of groups
 * and sources that will eventually need transmission (this may not happen
 * until the expiration of a timer or somesuch.)  When this goes to zero, the
 * interface is completely quiescent.  hintf_xmit_pending is set when there is
 * a packet pending immediate transmission.
 *
 * For doing soft detachs (waiting for all pending transmissions to go out)
 * the flow is a bit complex.  The hintf_pending_xmit_count field keeps track
 * of the number of groups that have pending transmissions associated with
 * them.  When gmph_detach_intf_soft() is called, we delay detaching the
 * interface (and thus destroying it) until hintf_pending_xmit_count goes
 * to zero.
 *
 * hintf_pending_xmit_count in turn represents the number of groups in which
 * the hgroup_xmit_pending flag is set.  See the comments below in the
 * definition of gmph_group about how this works.
 */
typedef struct gmph_intf_ {
    gmph_instance *hintf_instance;    /* Owning instance */
    gmpx_patnode hintf_inst_patnode;    /* Node on instance tree */
    gmpx_patnode hintf_global_patnode;    /* Node on global tree */
    gmpx_intf_id hintf_id;        /* Interface ID */
    gmp_addr_string hintf_local_addr;    /* Local interface address */

    thread hintf_xmit_head;        /* Head of xmit groups */
    gmph_report_type hintf_last_report_type; /* Last report type sent */

    gmp_version hintf_ver;        /* GMP version running */
    gmp_version hintf_cfg_ver;        /* Configured max version */
    u_int8_t hintf_robustness;        /* Robustness variable */

    u_int32_t hintf_unsol_rpt_ivl;    /* Unsolicited report ivl (msec) */

    gmpx_patroot *hintf_group_root;    /* Root of aggregated group records */

    gmpx_timer *hintf_gen_query_timer;    /* General query response timer */
    gmpx_timer *hintf_basic_querier;    /* Basic version querier present */
    gmpx_timer *hintf_leaves_querier;   /* Leaves version querier present */
    gmpx_timer *hintf_soft_detach_timer; /* Deferral for soft detach */

    u_int32_t hintf_last_query_time;    /* Time of last query */

    gmph_soft_detach_callback hintf_soft_detach_callback;
                    /* Callback after soft detach */
    void *hintf_soft_detach_context;    /* User-supplied context */

    u_int32_t hintf_pending_xmit_count;    /* # of groups and srcs pending xmit */

    boolean hintf_suppress_reports;    /* TRUE if report suppression is on */
    boolean hintf_xmit_pending;        /* TRUE if we have a pending xmit */
    boolean hintf_passive;        /* TRUE if passive (no xmits) */
} gmph_intf;

GMPX_PATNODE_TO_STRUCT(gmph_inst_patnode_to_intf, gmph_intf,
               hintf_inst_patnode);
GMPX_PATNODE_TO_STRUCT(gmph_global_patnode_to_intf, gmph_intf,
               hintf_global_patnode);


/*
 * Report message address entry
 *
 * This structure represents a single source address on the interface
 * Allow and Block lists.  It consists of an address entry structure
 * plus a retransmission count, so we can keep track of how many times
 * the individual address needs to be mentioned in a Report message.
 */
typedef struct gmph_rpt_msg_addr_entry_
{
    gmp_addr_list_entry msg_addr_entry;    /* Address entry */
    u_int msg_rexmit_count;        /* Retransmission count */
} gmph_rpt_msg_addr_entry;

EMBEDDED_STRUCT_TO_STRUCT(gmph_addr_list_to_group_list,
              gmph_rpt_msg_addr_entry, gmp_addr_list_entry,
              msg_addr_entry);


/*
 * Group entry
 *
 * This data structure defines the group record for an interface.  This
 * is the aggregated state from the individual client groups, and should
 * not be confused with them.
 *
 * There are several address lists attached to the group entry.  They are:
 *
 * hgroup_src_addr_list
 *    The list of addresses currently being excluded or included on the
 *    interface;  together with the filter mode they represent the group
 *    state on the interface.  The address entries are generic.
 *
 *    The transmit thread through this list contains all sources pending
 *    transmission in response to a general or GS query.
 *
 * hgroup_allow_list
 *    The list of addresses to be transmitted in ALLOW or TO_IN records.
 *    Any source entry that remains to be (re)transmitted will be found
 *    here.  The address entries are of type gmph_rpt_msg_addr_entry, as
 *    they contain the retransmit count along with the address itself.
 *
 *    The transmit thread through this list contains all sources pending
 *    transmission in a state-change report.
 *
 * hgroup_block_list
 *    Same as hintf_allow_list, except for BLOCK or TO_EX records.
 *
 * hgroup_query_list
 *    The list of addresses being queried by the querier.  The transmit
 *    thread represents sources to be contained in replies to GSS
 *    queries.  The address entries are generic.
 *
 *
 * Flags
 *
 * hgroup_change_msg_due
 *    This flag indicates that a change message is due now for a
 *    group.  The flag is set when the change timer expires for the
 *    group, and is cleared when all sources have been put into an
 *    outgoing packet for the group (if there are many sources, they
 *    may be split across multiple packets).
 *
 * hgroup_reply_due
 *    This flag indicates that a reply to a query is due now for a group.
 *    The flag is set when the query response timer expires for the group,
 *    and is cleared when everything for a group has been put into an
 *    outgoing packet.
 *
 * hgroup_gss_reply_due
 *    This flag indicates that a reply to a group-and-source-specific
 *    query is now due for a group.  The flag is set when the group
 *    query response timer expires for the group, and is cleared when
 *    everything requested by the query has been put into an outgoing
 *    packet.  The hgroup_reply_due flag is always set if this flag is
 *    also set.
 *
 * hgroup_xmit_pending
 *    This flag indicates that some kind of packet transmission is pending
 *    for this group (though it may be deferred by a timer.)  This flag
 *    also notes that the interface hintf_xmit_pending_count has been
 *    incremented for the group.  It is set whenever an action takes place
 *    that triggers a packet transmission, and is cleared when the group
 *    is completely quiescent (which causes hintf_xmit_pending_count to be
 *    decremented).
 *
 *
 * Pending Transmissions
 *
 * gmph_detach_intf_soft() waits for hintf_pending_xmit_count to go to
 * zero.  This field represents the number of groups that have the
 * hgroup_xmit_pending flag set.
 *
 * hgroup_xmit_pending is set if any of the following are true:
 *
 *  . hgroup_query_timer is running (we will be replying to a GS/GSS query)
 *  . hgroup_change_rpt_timer is running (we will be sending a change message)
 *  . hgroup_change_msg_due is set (we will be sending a change message shortly)
 *  . hgroup_reply_due is set (we need to send a reply to a GS query)
 *  . The group is on the interface xmit thread (we're about to send a report)
 *
 * hgroup_xmit_pending remains set until *none* of the above conditions are
 * true (the group has gone quiescent.)  At that point it will be cleared,
 * and hintf_pending_xmit_count will be decremented.
 *
 * We do limited refcounting to avoid double-freeing this structure
 * as a side effect of transmitting change reports.
 */

typedef struct gmph_group_ {

    /* Linkages */

    thread hgroup_client_thread;    /* Head of thread of client groups */
    gmph_intf *hgroup_intf;        /* Pointer back to interface */
    gmpx_patnode hgroup_intf_patnode;    /* Node on interface tree of groups */

    /* Group state */

    gmp_addr_string hgroup_addr;    /* Group address */
    gmp_filter_mode hgroup_filter_mode;    /* Include/Exclude */
    gmp_addr_list hgroup_src_addr_list;    /* Source address list */

    /* State-change report stuff */

    gmp_addr_list hgroup_allow_list;    /* List of Allow entries to send */
    gmp_addr_list hgroup_block_list;    /* List of Block entries to send */

    gmpx_timer *hgroup_change_rpt_timer; /* Timer for sending change reports */
    u_int hgroup_mode_change_rexmit_count; /* Mode change retransmit count */

    /* Query reply stuff */

    gmpx_timer *hgroup_query_timer;    /* For replying to group queries */
    gmp_addr_list hgroup_query_list;    /* Addresses to which we must reply */
    boolean hgroup_last_reporter;    /* TRUE if we were last to report */

    /* Transmission stuff */

    thread hgroup_xmit_thread;        /* Entry on intf transmit list */
    boolean hgroup_change_msg_due;    /* TRUE if we need to send chg msg  */
    boolean hgroup_reply_due;        /* TRUE if we need to send a reply */
    boolean hgroup_gss_reply_due;    /* TRUE if sending a GSS reply */
    boolean hgroup_xmit_pending;    /* TRUE if pending transmission */

    /* Ref counting */
    boolean hgroup_is_deleted;        /* Deleted, but locked */
    u_int8_t hgroup_lock_count;        /* Lock count */
} gmph_group;

GMPX_PATNODE_TO_STRUCT(gmph_intf_patnode_to_group, gmph_group,
               hgroup_intf_patnode);
THREAD_TO_STRUCT(gmph_xmit_thread_to_group, gmph_group, hgroup_xmit_thread);

#define GMPH_INITIAL_REPORT_JITTER 0    /* Percent jitter for initial packet */
#define GMPH_REPORT_REXMIT_JITTER 100    /* Percent jitter for retransmission */
#define GMPH_QUERY_REPLY_JITTER 100    /* Percent jitter for replies */


/*
 * Group set operation context block
 *
 * This block contains the context for address list set operations.
 */
typedef struct gmph_group_set_context_ {
    gmph_group *ctx_group;        /* Group pointer */
    gmp_addr_list *ctx_add_list;    /* List to add to */
    gmp_addr_list *ctx_del_list;    /* List to delete from */
} gmph_group_set_context;


/*
 * Client entry
 *
 * Each client has an associated data structure that tracks its associated
 * state.
 *
 * Note that the patricia tree will not be in lexicographic order by interface
 * ID on little-endian machines.
 */
typedef struct gmph_client_ {
    u_int32_t hclient_magic;        /* Magic number for robustness */
    gmph_instance *hclient_instance;    /* Owning instance */
    thread hclient_thread;        /* Link on instance client thread */
    gmpx_patroot *hclient_group_root;    /* Root of client group requests */
} gmph_client;

THREAD_TO_STRUCT(gmph_thread_to_client, gmph_client, hclient_thread);

#define GMPH_CLIENT_MAGIC 0x48636A69    /* 'Hcli' */


/*
 * Client group entry
 *
 * This data structure represents the requested group state for an individual
 * client.  The combined state of a group address across all clients is
 * what gets advertised in the protocol.
 *
 * This structure is accessed two ways.  First, it is on a patricia tree
 * rooted at the client, indexed by interface ID and group address.  Second,
 * it is on a thread of entries for the same group from different clients.
 *
 * The set of addresses is represented by a bit vector.
 */
typedef struct gmph_client_group_key_ {
    gmpx_intf_id group_key_intf_id;    /* Interface ID */
    gmp_addr_string group_key_addr;    /* Group address */
} gmph_client_group_key;

typedef struct gmph_client_group_ {
    gmph_client *client_group_client;    /* Pointer back to owning client */
    gmph_group *client_group_group;    /* Pointer to group entry */
    gmpx_patnode client_group_node;    /* Node on client tree of groups */
    thread client_group_thread;          /* Node on group thread of cli grps */
    gmph_client_group_key client_group_key; /* Patricia key */
    gmp_filter_mode client_filter_mode; /* Include/Exclude */
    gmp_addr_vect client_addr_vect;    /* Source address vector */
} gmph_client_group;

GMPX_PATNODE_TO_STRUCT(gmph_patnode_to_client_group, gmph_client_group,
               client_group_node);
THREAD_TO_STRUCT(gmph_thread_to_client_group, gmph_client_group,
         client_group_thread);

#define client_group_intf_id client_group_key.group_key_intf_id
#define client_group_addr client_group_key.group_key_addr


/*
 * gmph_client_group_key_len
 *
 * Returns the client group key length, which is based on the protocol
 * in use (IGMP or MLD).
 */
static inline u_int
gmph_client_group_key_len (gmph_instance *instance)
{
    u_int length;

    if (instance->hinst_proto == GMP_PROTO_IGMP)
    length = sizeof(gmph_client_group_key) -
        (IPV6_ADDR_LEN - IPV4_ADDR_LEN);
    else
    length = sizeof(gmph_client_group_key);

    return length;
}


/*
 * gmph_group_is_active
 *
 * Returns TRUE if the group is active, or FALSE if not.
 */
static inline boolean
gmph_group_is_active (gmph_group *group)
{
    /* Active if in Exclude mode. */

    if (group->hgroup_filter_mode == GMP_FILTER_MODE_EXCLUDE)
    return TRUE;

    /* Active if there are any sources. */

    if (!gmp_addr_list_empty(&group->hgroup_src_addr_list))
    return TRUE;

    return FALSE;
}


/*
 * gmph_intf_shutting_down
 *
 * Returns TRUE if the interface is in the process of a soft detach, or
 * FALSE if not.
 */
static inline boolean
gmph_intf_shutting_down (gmph_intf *intf)
{
    return (intf->hintf_soft_detach_callback != NULL);
}


/*
 * Externals
 */
extern gmpx_block_tag gmph_instance_tag;
extern gmpx_block_tag gmph_client_tag;
extern gmpx_block_tag gmph_intf_tag;
extern gmpx_block_tag gmph_group_tag;
extern gmpx_block_tag gmph_group_rpt_entry_tag;
extern gmpx_block_tag gmph_client_group_tag;
extern gmpx_block_tag gmph_client_group_thread_tag;
extern thread gmph_global_instance_thread;
extern gmpx_patroot *gmph_global_intf_tree[];


/* gmph_instance.c */

extern gmph_instance *gmph_instance_create(gmp_proto proto,
                       void *inst_context);
extern gmph_instance *gmph_get_instance(gmp_instance_id instance_id);
extern void gmph_instance_destroy(gmph_instance *instance);

/* gmph_client.c */

extern gmph_client *gmph_create_client(gmph_instance *instance);
extern gmph_client *gmph_get_client(gmp_client_id client_id);
extern void gmph_destroy_client(gmph_client *client);
extern gmph_client_group *gmph_lookup_client_group(gmph_client *client,
                           gmpx_intf_id intf_id,
                           const u_int8_t *group);
extern void gmph_destroy_client_group(gmph_client_group *client_group,
                      boolean evaluate_group);
extern void gmph_destroy_intf_client_groups(gmph_client *client,
                        gmph_intf *intf);
extern gmph_client_group *
gmph_create_client_group(gmph_intf *intf, gmph_client *client,
             gmph_group *group, const u_int8_t *group_addr,
             gmp_filter_mode filter_mode,
             gmp_addr_thread *addr_thread);
extern void gmph_destroy_group_client_groups(gmph_group *group);
extern void gmph_destroy_instance_clients(gmph_instance *instance);

/* gmph_intf.c */

extern void gmph_intf_evaluate_version(gmph_intf *intf);
extern gmph_intf *gmph_intf_lookup(gmph_instance *instance,
                   gmpx_intf_id intf_id);
extern gmph_intf *gmph_intf_lookup_global(gmp_proto proto,
                      gmpx_intf_id intf_id);
extern void gmph_kick_xmit(gmph_intf *intf);
extern int gmph_attach_intf_internal(gmph_instance *instance,
                     gmpx_intf_id intf_id);
extern int gmph_detach_intf_internal(gmph_instance *instance,
                     gmpx_intf_id intf_id,
                     gmph_soft_detach_callback callback,
                     void *context);
extern void gmph_attempt_intf_free(gmph_intf *intf);
extern void gmph_destroy_instance_intfs(gmph_instance *instance);
extern void gmph_start_general_query_timer(gmph_intf *intf, u_int32_t ivl,
                       u_int jitter_pct);
extern void gmph_intf_increment_pending_xmit_count(gmph_intf *);
extern void gmph_intf_decrement_pending_xmit_count(gmph_intf *);

/* gmph_group.c */

extern int gmph_reevaluate_group(gmph_group *group);
extern gmph_group *gmph_group_lookup_create(gmph_intf *,
                        const u_int8_t *group);
extern gmph_group *gmph_group_lookup(gmph_intf *intf, const u_int8_t *group);
extern gmph_group *gmph_group_lookup_first(gmph_intf *intf);
extern gmph_group *gmph_group_create(gmph_intf *intf, const u_int8_t *group);
extern gmpx_timer *gmph_create_change_report_timer(gmph_group *group);
extern gmph_group *gmph_first_group_xmit(gmph_intf *intf);
extern gmph_group *gmph_next_group_xmit(gmph_group *group);
extern void gmph_dequeue_group_xmit(gmph_group *group);
extern void gmph_delete_rpt_addr_entry(gmph_rpt_msg_addr_entry *report_entry);
extern void gmph_attempt_group_free(gmph_group *group);
extern void gmph_destroy_intf_groups(gmph_intf *intf);
extern void gmph_enqueue_group_xmit(gmph_group *group);
extern boolean gmph_group_xmit_pending(gmph_group *group);
extern void gmph_set_report_entry_rexmit(gmph_group *group,
                     gmp_addr_list_entry *addr_entry);
extern boolean gmph_group_source_requested(gmph_group *group,
                       const u_int8_t *source_addr);
extern void gmph_mark_pending_group_xmit(gmph_group *group);
extern void gmph_unmark_pending_group_xmit(gmph_group *group, boolean force);
extern void gmph_start_change_rpt_timer(gmph_group *group, u_int32_t ivl,
                    u_int jitter_pct);
extern void gmph_start_query_timer(gmph_group *group, u_int32_t ivl,
                   u_int jitter_pct);

extern void gmph_lock_group (gmph_group *group);
extern boolean gmph_unlock_group (gmph_group *group);

/* gmph_engine.c */

extern void gmph_register_packet_handler(void);
extern void gmph_version_changed(gmph_instance *instance, gmph_intf *intf);
extern void gmph_group_general_query_timer_expiry(gmph_group *group);
extern void gmph_group_change_report_timer_expiry(gmpx_timer *timer,
                          void *context);
extern void gmph_group_query_timer_expiry(gmpx_timer *timer, void *context);

#endif /* __GMPH_PRIVATE_H__ */
