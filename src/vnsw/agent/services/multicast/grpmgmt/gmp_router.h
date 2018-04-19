/* $Id: gmp_router.h 514187 2012-05-06 12:25:25Z ib-builder $
 *
 * gmp_router.h - External definitions for gmp_router
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef __GMP_ROUTER_H__
#define __GMP_ROUTER_H__

/*
 * This file defines the interface between the GMP toolkit and
 * router-side clients.
 *
 * The client must first create an instance by calling
 * gmpr_create_instance().  An instance is effectively a set of
 * interfaces; an interface must be referred to by only one instance.
 *
 * Each instance has at least one client.  A client is an entity that
 * will receive notifications about changes in group state.  The
 * number of clients is limited to GMPR_MAX_RTR_CLIENTS.  This number
 * is expected to be small, since brute force and arrays are used with
 * this expectation.
 *
 * An interface is bound to an instance by calling gmpr_attach_intf.
 * The interface ID is a 32 bit opaque value defined in the
 * environment file; the GMP toolkit doesn't attempt to dereference it
 * in any way, so the environment can be creative as to what it
 * actually is.
 */

/*
 * Notifications
 *
 * Notifications to clients are done on a pull model.  When a
 * notification is enqueued for a client, the client notification
 * callback is made, passing only the client context.  When the client
 * is ready to consume notifications, it calls gmpr_get_notification()
 * to get the next notification.  No additional notification callbacks
 * will be made until the notification queue is drained (and
 * gmpr_get_notification() returns NULL), so the client must either
 * drain the queue when called, or keep track of whether the queue is
 * drained while deferring.
 *
 * Notifications to clients are done on a delta basis.  A notification
 * consists of a type, an interface, a group, and possibly one source.
 * The types are:
 *
 *   GMPR_NOTIF_GROUP_ADD_EXCL:  Add a group in Exclude mode
 *   GMPR_NOTIF_GROUP_ADD_INCL:  Add a group in Include mode
 *   GMPR_NOTIF_GROUP_DELETE:  Delete a group
 *   GMPR_NOTIF_ALLOW_SOURCE:  Allow reception of a source
 *   GMPR_NOTIF_BLOCK_SOURCE:  Block reception of a source
 *
 * If either ADD_INCL or ADD_EXCL is received, this means all previous
 * sources for the group must be discarded.  Once the mode is
 * established, ALLOW and BLOCK calls are used to modify the source
 * address set.
 *
 * Note that an ADD_INCL notification is guaranteed to be followed by
 * at least one ALLOW; group deletions are handled by GROUP_DELETE,
 * and there are no null Includes.  The client can create group state
 * with confidence that an ALLOW will follow (or potentially a DELETE
 * due to state compression.)
 *
 * Note that, depending on timing and the kinds of messages sent by
 * hosts, the client may see a GROUP_DELETE without BLOCK_SOURCE
 * messages (indicating that all sources should be deleted along with
 * the group), or it may see a series of BLOCK_SOURCE messages that
 * delete all sources in a group without seeing a GROUP_DELETE
 * message.  These are semantically identical, and the client must
 * deal with both cases.
 *
 * If the client wishes full state instead of (or along with) deltas, a
 * notification of GMPR_NOTIF_GROUP_STATE is passed that contains the
 * full group state (filter mode and all sources) whenever an ADD_INCL,
 * ADD_EXCL, ALLOW_SOURCE, or BLOCK_SOURCE notification would be passed.
 * GROUP_DELETE is used in this case to indicate the group deletion.
 *
 * When a client is registered, all state is enqueued for it.
 *
 * If desired, a client can refresh its state by calling
 * gmpr_refresh().  It will receive all state in the form of
 * notifications.  In addition, a special notification of type
 * GMPR_NOTIF_REFRESH_END will be enqueued at the end of the enqueued
 * state.  This allows a client that wants to do soft refresh
 * (updating state before deleting any old state) to clean up old
 * state knowing that everything has been refreshed.
 *
 * Note that redundant notifications can be received because of state
 * compression.  For example, if a group is active, then is deleted,
 * then becomes active again, but the client did not read
 * notifications after the first ADD_INCL, it will receive another
 * ADD_INCL without ever seeing the DELETE (because the deletion was
 * overtaken by the add.)  This is clean semantically because the ADD
 * notifications cause old state to be discarded.
 *
 * Similar things can happen with ALLOWs and BLOCKs.
 *
 *
 * Host Notifications
 *
 * A similar system exists for notifications of host events.  Note that
 * host notifications are only defined for receive interest in groups
 * and sources;  in particular, non-null Exclude lists are not currently
 * expressed in this API.
 *
 * When the first notification is enqueued for a client, the host notification
 * callback is made.  The client then calls gmpr_get_host_notification()
 * at its leisure to pick up each notification.  The callback will not be
 * made again until the notification queue is drained.
 *
 * The notifications are of the following types:
 *    GMPR_NOTIF_HOST_JOIN:  Join a *,G or S,G
 *    GMPR_NOTIF_HOST_LEAVE:  Leave a *,G or S,G
 *    GMPR_NOTIF_HOST_TIMEOUT:  Leave due to a host timeout
 *    GMPR_NOTIF_HOST_IFDOWN:  Leave due to interface down
 *
 * State compression is done;  if a host leaves a group and then rejoins it
 * before the client reads the first notification, it will see only a JOIN,
 * and if it joins and then leaves quickly, the client will see only a LEAVE.
 *
 *
 * OIF remapping
 *
 * If the OIF remapping callback is defined, it is called for each received
 * interest in a (*,G) or (S,G).  The callback is expected to return the
 * remapped interface ID corresponding to the input interface and (S,G).
 */

/*
 * Client notification callback type
 *
 * This defines the callback to a client when a notification is ready.
 */
typedef void (*gmpr_notif_cb)(void *cli_context);


/*
 * OIF mapping callback type
 *
 * This defines the callback to a client to find out how to map an
 * interface and (S,G) to an outgoing interface.
 *
 * Returns TRUE if the output_if parameter is filled in with an output
 * interface, or FALSE if there is no output interface (it is blocked
 * by policy.)
 */
typedef boolean (*gmpr_oif_map_cb)(void *inst_context,
                   gmpx_intf_id rcv_if,
                   uint8_t *group_addr,
                   uint8_t *source_addr,
                   gmpx_intf_id *output_if);

/*
 * Policy callback type
 *
 * This defines the callback to a client to perform a policy check given
 * an interface, group and (potentially) source.  It returns TRUE if the
 * report is allowed, or FALSE if not.
 */
typedef boolean (*gmpr_policy_cb)(void *inst_context,
                  gmpx_intf_id rcv_if,
                  uint8_t *group_addr,
                  uint8_t *source_addr,
                  gmpx_packet_attr attribute);

/*
 * SSM check callback type
 *
 * This defines the callback to a client to verify if a group address is
 * allowed to be used in a (*,G) join.  This is used to detect attempts to
 * join an SSM group without a source.  It returns TRUE if the join is
 * allowed, or FALSE if not.
 */
typedef boolean (*gmpr_ssm_check_cb)(void *inst_context,
                     gmpx_intf_id rcv_if,
                     uint8_t *group_addr);

/*
 * Querier change callback type
 *
 * This defines the callback to a client when the querier status changes.
 */
typedef void (*gmpr_querier_cb)(void *cli_context, gmpx_intf_id intf,
                boolean querier, uint8_t *querier_addr);

/*
 * Router-side instance context block
 */
typedef struct gmpr_instance_context_ {
    gmpr_oif_map_cb rctx_oif_map_cb;    /* OIF mapping callback */
    gmpr_policy_cb rctx_policy_cb;    /* Multicast policy callback */
    gmpr_ssm_check_cb rctx_ssm_check_cb; /* SSM check callback */
} gmpr_instance_context;

/*
 * Router-side client context block
 *
 * This data structure carries linkage and other context information
 * from a client of router-side GMP.  This consists of a set of callbacks
 * for various conditions.  In addition, two booleans are used to say
 * whether the client wishes to see full notifications, deltas, or both.
 * At least one of these must be set.
 */
typedef struct gmpr_client_context_ {
    gmpr_notif_cb rctx_notif_cb;    /* Notification callback */
    gmpr_notif_cb rctx_host_notif_cb;    /* Host notification callback */
    gmpr_querier_cb rctx_querier_cb;    /* Querier change callback */
    boolean rctx_delta_notifications;    /* TRUE if client wants deltas */
    boolean rctx_full_notifications;    /* TRUE if client wants full state */
} gmpr_client_context;


/*
 * Router-side client notification
 *
 * This data structure carries a single notification for a client.  A
 * notification consists of a single (intf, group) tuple, possibly
 * with a source address (in the case of Allow and Block.)  This is
 * used to communicated aggregated group state to the upper layers.
 *
 * Normally, the addition and deletion of sources is done as a set of
 * deltas (ALLOW/BLOCK of individual sources.)  However, a client may
 * optionally ask for complete group state upon each change; this is
 * delvered in a GROUP_STATE notification.
 */
typedef enum {
    GMPR_NOTIF_GROUP_DELETE,        /* Delete group */
    GMPR_NOTIF_GROUP_ADD_EXCL,        /* Add group in Exclude mode */
    GMPR_NOTIF_GROUP_ADD_INCL,        /* Add group in Include mode */
    GMPR_NOTIF_ALLOW_SOURCE,        /* Allow traffic for source */
    GMPR_NOTIF_BLOCK_SOURCE,        /* Block traffic to source */
    GMPR_NOTIF_REFRESH_END,        /* End of refresh stream */
    GMPR_NOTIF_GROUP_STATE,        /* Complete group state */
} gmpr_client_notification_type;

typedef struct gmpr_client_notification_ {
    gmpx_intf_id notif_intf_id;        /* Interface ID */
    gmp_addr_string notif_group_addr;    /* Group address */
    gmpr_client_notification_type notif_type;    /* Notification type */
    gmp_addr_string notif_source_addr;    /* Source address if applicable */
    gmp_filter_mode notif_filter_mode;    /* Current group filter mode */
    gmp_addr_thread *notif_addr_thread;    /* Address thread if GROUP_STATE */
    boolean notif_last_sg;              /* Last (s,g) for the same group? */
} gmpr_client_notification;


/*
 * Router-side client host notification
 *
 * This data structure carries a single host notification for a client.  A
 * notification consists of a single (intf, host, group) tuple, possibly
 * with a source address (in the case of Allow and Block.)  This is used
 * to communicate host request state to the upper layers if host tracking
 * is enabled.
 */
typedef enum {
    GMPR_NOTIF_HOST_UNKNOWN,        /* We don't know yet */
    GMPR_NOTIF_HOST_JOIN,        /* Join a *,G or S,G */
    GMPR_NOTIF_HOST_LEAVE,        /* Leave a *,G or S,G */
    GMPR_NOTIF_HOST_TIMEOUT,        /* Leave due to a host timeout */
    GMPR_NOTIF_HOST_IFDOWN,        /* Leave due to interface down */
} gmpr_client_host_notification_type;

typedef struct gmpr_client_host_notification_ {
    gmpr_client_host_notification_type host_notif_type; /* Notification type */
    gmpx_intf_id host_notif_intf_id;    /* Interface ID */
    gmp_addr_string host_notif_group_addr; /* Group address */
    gmp_addr_string host_notif_source_addr; /* Source address or 0 */
    boolean host_notif_source_present;    /* TRUE if source address present */
    gmp_addr_string host_notif_host_addr; /* Host address */
} gmpr_client_host_notification;


/*
 * Interface list
 *
 * This structure is returned by gmpr_get_intf_list(), and must be freed
 * by gmpr_free_intf_list() when it is processed.
 */

#define GMPR_INTF_LIST_SIZE 10        /* Number of interfaces per entry */

typedef struct gmpr_client_intf_list_ {
    struct gmpr_client_intf_list_ *gci_next; /* Next entry */
    uint32_t gci_intf_count;        /* Number of entries */
    gmpx_intf_id gci_intfs[GMPR_INTF_LIST_SIZE]; /* Array of entries */
} gmpr_client_intf_list;


/*
 * Group entry
 *
 * A pointer to this structure is returned by gmpr_get_intf_groups().
 * It represents the state of a single group, and is linked to further
 * groups.
 *
 * The caller is expected to pass it back via gmpr_destroy_intf_group().
 */
typedef struct gmpr_intf_group_entry_ {
    struct gmpr_intf_group_entry_ *gig_next; /* Next entry */
    gmp_addr_string gig_group_addr;    /* Group address */
    gmp_filter_mode gig_filter_mode;    /* Filter mode */
    gmp_addr_thread *gig_sources;    /* Source list, or NULL */
} gmpr_intf_group_entry;


/*
 * Host entry
 *
 * A pointer to this structure is returned by gmpr_get_intf_hosts().
 * It represents a single host that has INCLUDE join state present and
 * is linked to further hosts.
 *
 * The caller is expected to pass it back via gmpr_destroy_intf_hosts().
 */
typedef struct gmpr_intf_host_entry_ {
    struct gmpr_intf_host_entry_ *gih_next; /* Next entry */
    gmp_addr_string gih_host_addr;    /* Host address */
} gmpr_intf_host_entry;


/*
 * External references
 */
extern gmp_instance_id gmpr_create_instance(gmp_proto proto,
                        void *inst_context,
                        gmpr_instance_context *context);
extern void gmpr_destroy_instance(gmp_instance_id instance_id);
extern gmp_client_id gmpr_register(gmp_instance_id instance_id,
                   void *cli_context,
                   gmpr_client_context *context);
extern void gmpr_detach(gmp_client_id client_id);
extern void gmpr_refresh(gmp_client_id client_id, boolean flush);
extern void gmpr_refresh_intf(gmp_client_id client_id, gmpx_intf_id intf_id,
                  boolean flush);
extern void gmpr_refresh_host_state(gmp_client_id client_id);
extern int gmpr_attach_intf(gmp_instance_id instance_id, gmpx_intf_id intf_id);
extern int gmpr_detach_intf(gmp_instance_id instance_id,
                 gmpx_intf_id intf_id);
extern int gmpr_set_intf_params(gmp_instance_id instance_id,
                gmpx_intf_id intf_id,
                gmpr_intf_params *params);
extern void gmpr_chk_grp_limit(gmp_instance_id instance_id,
                               gmpx_intf_id intf_id);
extern int gmpr_disable_host_tracking(gmp_instance_id instance_id,
                      gmpx_intf_id intf_id);
extern gmpr_client_notification *
gmpr_get_notification(gmp_client_id client_id,
              gmpr_client_notification *last_notification);
extern void gmpr_return_notification(gmpr_client_notification *notification);
extern gmpr_client_host_notification *
    gmpr_get_host_notification(gmp_client_id client_id,
               gmpr_client_host_notification *last_notification);
extern void gmpr_return_host_notification(
                  gmpr_client_host_notification *host_notif);
extern boolean
gmpr_notification_last_sg(gmpr_client_notification *notification);

typedef enum {INTF_LIST_LOOSE, INTF_LIST_STRICT} gmpr_intf_list_match;
extern gmpr_client_intf_list *gmpr_get_intf_list(gmp_instance_id instance_id,
                         uint8_t *group_addr,
                         uint8_t *source_addr,
                         gmpr_intf_list_match type);
extern void gmpr_free_intf_list(gmpr_client_intf_list *intf_list);
extern boolean
    gmpr_is_forwarding_channel(gmp_instance_id instance_id,
                   gmpx_intf_id intf_id,
                   const uint8_t *source_addr,
                   const uint8_t *group_addr, boolean exact);
extern void gmpr_update_intf_state(gmp_instance_id instance_id,
                   gmpx_intf_id intf_id,
                   const uint8_t *intf_addr);
extern gmpr_intf_group_entry *gmpr_get_intf_groups(gmp_instance_id instance_id,
                           gmpx_intf_id intf_id);
extern gmpr_intf_group_entry *gmpr_get_host_groups(gmp_instance_id instance_id,
                                                   gmpx_intf_id intf_id,
                                                   const uint8_t *host_addr);
extern gmpr_intf_host_entry *gmpr_get_intf_hosts(gmp_instance_id instance_id,
                                                 gmpx_intf_id intf_id);
extern void gmpr_destroy_intf_group(gmpr_intf_group_entry *group_list);
extern void gmpr_destroy_intf_host(gmpr_intf_host_entry *host_list);
extern boolean gmpr_is_initialized(void);
extern void gmpr_timeout_group_range(gmp_instance_id instance_id,
                     gmpx_intf_id intf_id,
                     const uint8_t *group_addr,
                     uint32_t pfx_len, boolean send_query);
extern boolean gmpr_sg_is_excluded(gmp_instance_id instance_id,
                gmpx_intf_id intf_id,
                const uint8_t *group_addr,
                const uint8_t *source_addr);
extern void gmpr_update_trace_flags(gmp_instance_id instance_id,
                    uint32_t trace_flags);
extern void gmpr_force_general_queries(gmp_instance_id instance_id,
                       gmpx_intf_id intf_id);
extern void gmpr_request_general_queries(gmp_instance_id instance_id,
                     gmpx_intf_id intf_id);
extern void gmpr_force_one_general_query(gmp_instance_id instance_id,
                     gmpx_intf_id intf_id);
extern void gmpr_request_one_general_query(gmp_instance_id instance_id,
                       gmpx_intf_id intf_id);
extern void gmpr_notify_oif_map_change (gmp_proto proto, gmpx_intf_id intf_id);

#endif /* __GMP_ROUTER_H__ */
