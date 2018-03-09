/* $Id: gmp_router.c 514187 2012-05-06 12:25:25Z ib-builder $
 *
 * gmp_router.c - IGMP/MLD Router-Side Support
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 * This module defines the top-level routines for the router-side support
 * for GMP.
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_externs.h"
#include "gmp_private.h"
#include "gmp_router.h"
#include "gmpr_private.h"
#include "gmp_trace.h"
#include "gmpr_trace.h"

/*
 * Global storage.  There should be very little here.
*/
gmpx_block_tag gmpr_instance_tag;
gmpx_block_tag gmpr_client_tag;
gmpx_block_tag gmpr_intf_tag;
gmpx_block_tag gmpr_host_tag;
gmpx_block_tag gmpr_host_group_tag;
gmpx_block_tag gmpr_host_group_addr_tag;
gmpx_block_tag gmpr_group_tag;
gmpx_block_tag gmpr_ogroup_tag;
gmpx_block_tag gmpr_group_addr_entry_tag;
gmpx_block_tag gmpr_ogroup_addr_entry_tag;
gmpx_block_tag gmpr_notification_tag;
gmpx_block_tag gmpr_host_notification_tag;
gmpx_block_tag gmpr_global_group_tag;
gmpx_block_tag gmpr_intf_list_tag;
gmpx_block_tag gmpr_intf_group_tag;
gmpx_block_tag gmpr_intf_host_tag;

static boolean gr_initialized;		/* TRUE if we've initialized */


/*
 * gmpr_init
 *
 * Initialize GMP Router code
 *
 * Called when the first instance is created.
 *
 * Returns 0 if all OK, or -1 if out of memory.
 */
static int
gmpr_init (void)
{
    gmp_proto proto;

    /* Initialize the interface tree. */

    for (proto = 0; proto < GMP_NUM_PROTOS; proto++) {
	gmpr_global_intf_tree[proto] =
	    gmpx_patroot_init(sizeof(gmpx_intf_id),
			      GMPX_PATRICIA_OFFSET(gmpr_intf,
						   rintf_global_patnode,
						   rintf_id));
	if (!gmpr_global_intf_tree[proto])
	    return -1;			/* Out of memory. */
    }

    gr_initialized = TRUE;

    /* Do common initialization. */

    gmp_common_init();

    /* Create the instance thread. */

    thread_new_circular_thread(&gmpr_global_instance_thread);

    /* Create memory blocks. */

    gmpr_instance_tag = gmpx_malloc_block_create(sizeof(gmpr_instance),
						 "GMP router instance");
    gmpr_client_tag = gmpx_malloc_block_create(sizeof(gmpr_client),
					       "GMP router client");
    gmpr_intf_tag = gmpx_malloc_block_create(sizeof(gmpr_intf),
					     "GMP router intf");
    gmpr_host_tag = gmpx_malloc_block_create(sizeof(gmpr_host),
					     "GMP router host");
    gmpr_host_group_tag = gmpx_malloc_block_create(sizeof(gmpr_host_group),
						   "GMP router host group");
    gmpr_host_group_addr_tag =
	gmpx_malloc_block_create(sizeof(gmpr_host_group_addr),
				 "GMP router host group address");
    gmpr_group_tag = gmpx_malloc_block_create(sizeof(gmpr_group),
					      "GMP router group");
    gmpr_ogroup_tag = gmpx_malloc_block_create(sizeof(gmpr_ogroup),
					       "GMP router output group");
    gmpr_group_addr_entry_tag =
	gmpx_malloc_block_create(sizeof(gmpr_group_addr_entry),
				 "GMP router group address entry");
    gmpr_ogroup_addr_entry_tag =
	gmpx_malloc_block_create(sizeof(gmpr_ogroup_addr_entry),
				 "GMP router output group address entry");
    gmpr_notification_tag =
	gmpx_malloc_block_create(sizeof(gmpr_client_notification),
				 "GMP router notification");
    gmpr_host_notification_tag =
	gmpx_malloc_block_create(sizeof(gmpr_client_host_notification),
				 "GMP router host notification");
    gmpr_global_group_tag =
	gmpx_malloc_block_create(sizeof(gmpr_global_group),
				 "GMP router global group");
    gmpr_intf_list_tag =
	gmpx_malloc_block_create(sizeof(gmpr_client_intf_list),
				 "GMP router interface list");
    gmpr_intf_group_tag =
	gmpx_malloc_block_create(sizeof(gmpr_intf_group_entry),
				 "GMP router interface group");
    gmpr_intf_host_tag =
	gmpx_malloc_block_create(sizeof(gmpr_intf_host_entry),
				 "GMP router interface host");
    return 0;
}


/*
 * gmpr_create_instance
 *
 * Create a router-side GMP instance.
 *
 * Returns an instance ID (really a pointer), or zero if out of memory.
 */
gmp_instance_id
gmpr_create_instance (gmp_proto proto, void *inst_context,
		      gmpr_instance_context *cb_context)
{
    gmpr_instance *instance;

    /* If we're not initialized yet, do it now. */

    if (!gr_initialized) {
	if (gmpr_init() < 0)
	    return NULL;		/* Out of memory. */
    }

    gr_initialized = TRUE;

    /* Create the instance. */

    instance = gmpr_instance_create(proto, inst_context);

    /* Copy over the context block. */

    if (cb_context) {
        memmove(&instance->rinst_cb_context, cb_context,
            sizeof(gmpr_instance_context));
    }

    return instance;
}


/*
 * gmpr_destroy_instance
 *
 * Destroy an instance and all things associated with it.
 */
void
gmpr_destroy_instance (gmp_instance_id instance_id)
{
    gmpr_instance *instance;

    /* Get the instance block. */

    instance = gmpr_get_instance(instance_id);

    /* Destroy it. */

    gmpr_instance_destroy(instance);
}


/*
 * gmpr_register
 *
 * Register a router-side client with GMP.
 *
 * Returns a client ID for the client to identify itself with, or 0 if
 * we're maxed out on clients.
 */
gmp_client_id
gmpr_register (gmp_instance_id instance_id, void *client_context,
	       gmpr_client_context *cb_context)
{
    gmpr_client *client;
    gmpr_instance *instance;

    /* Get the instance. */

    instance = gmpr_get_instance(instance_id);

    /* Create a client. */

    client = gmpr_create_client(instance);
    client->rclient_context = client_context;

    /* Copy over the context block. */

    memmove(&client->rclient_cb_context, cb_context,
        sizeof(gmpr_client_context));

    /* If the client supplied a host callback, turn on host tracking. */

    if (cb_context->rctx_host_notif_cb)
	instance->rinst_host_tracking = TRUE;

    return client;
}


/*
 * gmpr_detach
 *
 * A client is going away.  Clean up.
 */
void
gmpr_detach (gmp_client_id client_id)
{
    gmpr_client *client;

    /* Get the client. */

    client = gmpr_get_client(client_id);

    /* Get rid of it. */

    gmpr_destroy_client(client);
}


/*
 * gmpr_refresh
 *
 * Refresh all group state.  This enqueues all state for the client.
 *
 * If flush is TRUE, we first flush out all source notifications, which
 * has the effect of getting rid of any pending source deletions.
 */
void
gmpr_refresh (gmp_client_id client_id, boolean flush)
{
    gmpr_client *client;
    gmpr_instance *instance;

    /* Get the client. */

    client = gmpr_get_client(client_id);
    instance = client->rclient_instance;

    /* Trace it. */

    gmpr_trace(instance, GMPR_TRACE_CLIENT_NOTIFY,
	       "Client %u refresh", client->rclient_ordinal);

    /* Enqueue everything. */

    gmpr_client_enqueue_all_groups(client, flush);

    /* Enqueue the end-of-refresh marker. */

    gmpr_enqueue_refresh_end(client);

    /* Kick the clients. */

    gmpr_alert_clients(client->rclient_instance);
}


/*
 * gmpr_refresh_intf
 *
 * Refresh all group state for a single interface.  This enqueues all
 * state for the client.
 *
 * If flush is TRUE, we first flush out any pending source notifications,
 * which has the effect of getting rid of any pending source deletions.
 */
void
gmpr_refresh_intf (gmp_client_id client_id, gmpx_intf_id intf_id,
		   boolean flush)
{
    gmpr_intf *intf;
    gmpr_client *client;

    /* Get the client. */

    client = gmpr_get_client(client_id);

    /* Look up the interface. */

    intf = gmpr_intf_lookup(client->rclient_instance, intf_id);
    if (!intf)
	return;

    /* Enqueue everything. */

    gmpr_client_enqueue_all_intf_groups(client, intf, flush);
    gmpr_alert_clients(client->rclient_instance);
}


/*
 * gmpr_refresh_host_state
 *
 * Refresh all host group state.  This enqueues all state for the client.
 */
void
gmpr_refresh_host_state (gmp_client_id client_id)
{
    gmpr_client *client;

    /* Get the client. */

    client = gmpr_get_client(client_id);

    /* Enqueue everything. */

    gmpr_client_enqueue_all_host_groups(client);
    gmpr_alert_host_clients(client->rclient_instance);
}


/*
 * gmpr_set_intf_params
 *
 * Set parameters for an interface.
 *
 * Returns 0 if OK, -1 if out of memory, or 1 if the interface hasn't
 * been attached.
 */
int
gmpr_set_intf_params (gmp_instance_id instance_id, gmpx_intf_id intf_id,
		      gmpr_intf_params *params)
{
    gmpr_instance *instance;

    /* Get the instance. */

    instance = gmpr_get_instance(instance_id);

    /* Go do the work. */

    return gmpr_intf_set_params(instance, intf_id, params);
}

void 
gmpr_chk_grp_limit (gmp_instance_id instance_id, 
                    gmpx_intf_id intf_id)
{
   gmpr_intf *intf;
   // gmpr_instance *instance;
 
   // instance = gmpr_get_instance(instance_id);
   intf = gmpr_intf_lookup(instance_id, intf_id);
   gmpr_check_grp_limit(intf, FALSE);
}

/*
 * gmpr_immediate_leave_intf_count
 */
static int
gmpr_immediate_leave_intf_count (gmpr_instance *instance)
{
    int intf_count = 0;
    gmpr_intf *intf = NULL;
    
    intf = gmpr_next_instance_intf(instance, NULL);
    while (intf) {
	if (intf->rintf_fast_leaves) {
	    intf_count++;
	}
	intf = gmpr_next_instance_intf(instance, intf);
    }
    return intf_count;
}

/*
 * gmpr_intf_disable_host_tracking
 *
 * Disable host tracking for an interface
 *
 * Returns 0 if OK, or 1 if the interface hasn't
 * been attached.
 */
int
gmpr_disable_host_tracking (gmp_instance_id instance_id,
			    gmpx_intf_id intf_id)
{
    gmpr_instance *instance;
    gmpr_intf *intf;

    /* Get the instance. */
    instance = gmpr_get_instance(instance_id);
    intf = gmpr_intf_lookup(instance, intf_id);
    
    /* If interface doesn't exist. */
    if (!intf)
	return 1;

    /* Blast all of the hosts on the interface. */
    gmpr_destroy_intf_hosts(intf);
    
    /* Disable host tracking if this is the last interface */
    if (gmpr_immediate_leave_intf_count(instance) == 0)
        instance->rinst_host_tracking = FALSE;
    
    return 0;
}

/*
 * gmpr_attach_intf
 *
 * Bind to an interface, in preparation for later interest.
 *
 * Returns 0 if OK, -1 if out of memory, or 1 if the interface already
 * is bound.
 */
int
gmpr_attach_intf (gmp_instance_id instance_id, gmpx_intf_id intf_id)
{
    gmpr_instance *instance;

    /* Go do the work. */

    instance = gmpr_get_instance(instance_id);
    return gmpr_attach_intf_internal(instance, intf_id);
}


/*
 * gmpr_detach_intf
 *
 * Unbind an interface.  This flushes all previous listen requests
 * on the interface.
 *
 * Returns 0 if all OK, or 1 if the interface doesn't exist.
 */
int
gmpr_detach_intf (gmp_instance_id instance_id, gmpx_intf_id intf_id)
{
    gmpr_instance *instance;

    /* Go do the work. */

    instance = gmpr_get_instance(instance_id);
    return gmpr_detach_intf_internal(instance, intf_id);
}


/*
 * gmpr_get_notification
 *
 * Get the next notification for this client.
 *
 * Returns a pointer to the notification block, or NULL if no more
 * notifications.
 *
 * If last_notification isn't NULL, the client is returning a previous
 * notification block, which we can re-use if we like.
 */
gmpr_client_notification *
gmpr_get_notification (gmp_client_id client_id,
		       gmpr_client_notification *last_notification)
{
    gmpr_client *client;

     /* Get the client. */

    client = gmpr_get_client(client_id);

    /* Do the work. */

    return gmpr_client_get_notification(client, last_notification);
}


/*
 * gmpr_get_host_notification
 *
 * Get the next host notification for this client.
 *
 * Returns a pointer to the notification block, or NULL if no more
 * notifications.
 *
 * If last_notification isn't NULL, the client is returning a previous
 * notification block, which we can re-use if we like.
 */
gmpr_client_host_notification *
gmpr_get_host_notification (gmp_client_id client_id,
			    gmpr_client_host_notification *last_notification)
{
    gmpr_client *client;

     /* Get the client. */

    client = gmpr_get_client(client_id);

    /* Do the work. */

    return gmpr_client_get_host_notification(client, last_notification);
}


/*
 * gmpr_return_notification
 *
 * Free up a previously-delivered notification.
 */
void
gmpr_return_notification (gmpr_client_notification *notification)
{

    /* Do the work. */

    gmpr_free_notification(notification);
}


/*
 * gmpr_return_host_notification
 * 
 * Free up a previously-delivered host notification.
 */
void
gmpr_return_host_notification (gmpr_client_host_notification *host_notif)
{

    /* No-brainer. */

    gmpr_client_free_host_notification(host_notif);
}


/*
 * gmpr_notification_last_sg
 *
 * Return the value of notif_last_sg given a notification.
 */
boolean
gmpr_notification_last_sg (gmpr_client_notification *notification)
{
    if (notification && notification->notif_last_sg) {
	return TRUE;
    } else {
	return FALSE;
    }
}

/*
 * gmpr_add_intf_list_entry
 *
 * Add an interface index to an interface list.
 *
 * May allocate further entries as necessary.
 *
 * Returns a pointer to the current list entry, or NULL if out of memory.
 */
static gmpr_client_intf_list *
gmpr_add_intf_list_entry(gmpr_client_intf_list *cur_intf_list,
			 gmpx_intf_id intf_id)
{
    gmpr_client_intf_list *next_list;

    /* See if the entry will fit in the current entry. */

    if (cur_intf_list->gci_intf_count >= GMPR_INTF_LIST_SIZE) {

	/* Entry is full.  Allocate a new one and link 'em. */

	next_list = gmpx_malloc_block(gmpr_intf_list_tag);
	if (!next_list)
	    return NULL;		/* Out of memory */

	cur_intf_list->gci_next = next_list;
	cur_intf_list = next_list;
    }

    /* Add the entry to the array. */

    cur_intf_list->gci_intfs[cur_intf_list->gci_intf_count++] = intf_id;

    return cur_intf_list;
}


/*
 * gmpr_get_intf_list
 *
 * Get the list of interfaces corresponding to a (S,G) or (*,G).
 *
 * If a (*,G) is specified (by virtue of a NULL source address pointer)
 * we will return an interface only if its state is Exclude{}.  If an (S,G)
 * is specified, we return an interface if either the source is included in
 * an Include list, or is not listed in an Exclude list.
 *
 * If the search type is LOOSE, we will match a (*,G) for any group in
 * Exclude mode, whether or not there are any sources.
 *
 * Returns a pointer to an interface list, or NULL if out of memory.
 */
gmpr_client_intf_list *
gmpr_get_intf_list (gmp_instance_id instance_id, uint8_t *group_addr,
		    uint8_t *source_addr, gmpr_intf_list_match match_type)
{
    gmpr_instance *instance;
    gmpr_global_group *global_group;
    gmpr_client_intf_list *intf_list_head, *cur_intf_list;
    thread *thread_ptr;
    gmpr_ogroup *group;
    gmp_addr_cat_entry *cat_entry;

    /* Allocate the block we're returning. */

    intf_list_head = gmpx_malloc_block(gmpr_intf_list_tag);
    if (!intf_list_head)
	return NULL;			/* Out of memory */
    cur_intf_list = intf_list_head;

    /* Get the instance. */

    instance = gmpr_get_instance(instance_id);

    /* Look up the global group entry. */

    global_group = gmpr_lookup_global_group(instance, group_addr);

    /* Bail if we don't know of the group. */

    if (!global_group)
	return intf_list_head;

    /*
     * Look up the address catalog entry for the source address.  It may
     * or may not be there.
     */
    cat_entry = NULL;
    if (source_addr) {
	cat_entry = gmp_lookup_addr_cat_entry(&instance->rinst_addr_cat,
					      source_addr);
    }

    /* Walk the group thread. */

    thread_ptr = NULL;
    while (TRUE) {
	thread_ptr = thread_circular_thread_next(
			 &global_group->global_group_head, thread_ptr);
	group = gmpr_global_thread_to_group(thread_ptr);
	if (!group)
	    break;

	/*
	 * Found a group.  See if there was no source address
	 * specified (we're looking for a (*,G) entry).
	 */
	if (!source_addr) {

	    /*
	     * No source address--we're looking for (*,G).  Bail if the
	     * group isn't in Exclude mode.
	     */
	    if ((group->rogroup_filter_mode != GMP_FILTER_MODE_EXCLUDE)) {
		continue;
	    }

	    /*
	     * Exclude mode.  Bail if there are sources present,
	     * unless we're doing a loose search.
	     */
	    if (!gmp_addr_list_empty(&group->rogroup_excl_src_addr) &&
		(match_type == INTF_LIST_STRICT)) {
		continue;
	    }

	} else {

	    /* (S,G) specified.  Skip if the source isn't active. */

	    if (!cat_entry ||
		!gmpr_source_ord_is_active(group, cat_entry->adcat_ent_ord)) {
		continue;
	    }
	}

	/* Looks like we should report the interface.  Add it to the list. */

	cur_intf_list =
	    gmpr_add_intf_list_entry(cur_intf_list,
				     group->rogroup_intf->rintf_id);
    }

    return intf_list_head;
}


/*
 * gmpr_free_intf_list
 *
 * Free a previously-allocated interface list.
 */
void
gmpr_free_intf_list (gmpr_client_intf_list *intf_list)
{
    gmpr_client_intf_list *next;

    /* Simple linked list traversal. */

    while (intf_list) {
	next = intf_list->gci_next;
	gmpx_free_block(gmpr_intf_list_tag, intf_list);
	intf_list = next;
    }
}


/*
 * gmpr_is_forwarding_channel
 *
 * Returns TRUE if GMP is recommending forwarding of the specified (S,G)
 * channel on the interface, or FALSE if not.  If the source pointer is
 * NULL, we test for (*,G).
 *
 * If "exact" is TRUE, we return TRUE only if the channel as specified is
 * to be forwarded.  If FALSE, we return TRUE if *any* (S,G) is being
 * forwarded when a (*,G) request is made.
 */
boolean
gmpr_is_forwarding_channel (gmp_instance_id instance_id, gmpx_intf_id intf_id,
			    const uint8_t *source_addr,
			    const uint8_t *group_addr, boolean exact)
{
    gmpr_instance *instance;
    gmpr_intf *intf;
    gmpr_ogroup *group;

    /* Get the instance and interface. */

    instance = gmpr_get_instance(instance_id);
    intf = gmpr_intf_lookup(instance, intf_id);
    if (!intf)
	return FALSE;

    /* Get the group. */

    group = gmpr_ogroup_lookup(intf, group_addr);
    if (!group)
	return FALSE;

    /* If the group is a (*,G) join, we match anything. */

    if (gmpr_group_forwards_all_sources(group))
	return TRUE;

    /*
     * If we're doing an inexact (*,G) match and any part of the group
     * is being forwarded, we have a match.
     */
    if (!exact && !source_addr && gmpr_ogroup_is_active(group))
	return TRUE;

    /* If we're doing a (*,G) test, we didn't match. */

    if (!source_addr)
	return FALSE;

    /* See if we're forwarding the specified source. */

    return gmpr_group_forwards_source(group, source_addr);
}


/*
 * gmpr_update_intf_state
 *
 * Update the interface state.  If the address is NULL, the interface
 * is going down.
 */
void
gmpr_update_intf_state (gmp_instance_id instance_id, gmpx_intf_id intf_id,
			const uint8_t *intf_addr)
{
    gmpr_instance *instance;
    gmpr_intf *intf;
    boolean was_up;

    /* Get the instance and interface. */

    instance = gmpr_get_instance(instance_id);
    intf = gmpr_intf_lookup(instance, intf_id);
    if (!intf)
	return;

    /*
     * If the address is there, mark the interface up.  Store the new
     * address and make ourselves querier if the address has changed
     * or the interface has just come up.
     */
    was_up = intf->rintf_up;
    if (intf_addr) {
	intf->rintf_up = TRUE;

        if (!was_up || memcmp(intf_addr, intf->rintf_local_addr.gmp_addr,
            instance->rinst_addrlen)) {

	    /* Make ourselves querier. */

            memmove(intf->rintf_local_addr.gmp_addr, intf_addr, instance->rinst_addrlen);
	    gmpr_update_querier(intf, &intf->rintf_local_addr, TRUE);
	}

    } else {
        /* No address.  Mark the interface down and zap the querier address */

        intf->rintf_up = FALSE;
        intf->rintf_querier = FALSE;
        memset(intf->rintf_local_addr.gmp_addr, 0, instance->rinst_addrlen);
    }

    /* If the up/down status changed, update any associated output groups. */

    if (was_up != intf->rintf_up)
	gmpr_update_intf_output_groups(intf);

    /*
     * If the interface went down, blast any input groups on the
     * interface, and note that we no longer have any transmissions
     * pending.
     */

    if (was_up && !intf->rintf_up) {
	gmpr_flush_intf_input_groups(intf);
	intf->rintf_xmit_pending = FALSE;
    }

    /*
     * If the interface came up, kick the transmitter in case
     * something is waiting to go.  Also send out startup queries,
     * since the interface just came back up.
     */
    if (!was_up && intf->rintf_up) {
        gmpr_setup_initial_query_timer(intf);
	gmpr_kick_xmit(intf);
    }
}


/*
 * Context structure for gmpr_build_group_cb
 */
typedef struct gmpr_build_group_cb_context_ {
    gmpr_instance *gbg_instance;	/* Instance pointer */
    gmpr_ogroup *gbg_ogroup;		/* Output group */
    gmpr_intf_group_entry *gbg_group_ent; /* Group entry */
} gmpr_build_group_cb_context;


/*
 * gmpr_build_group_cb
 *
 * Callback from vector walk to build a list of sources for a group.
 */
static boolean
gmpr_build_group_cb (void *context, bv_bitnum_t bitnum,
		     boolean new_val GMPX_UNUSED,
		     boolean old_val GMPX_UNUSED)
{
    gmpr_instance *instance;
    gmpr_build_group_cb_context *ctx;
    gmpr_intf_group_entry *group_entry;
    gmp_addr_cat_entry *cat_entry;
    gmpr_ogroup *group;

    ctx = context;
    instance = ctx->gbg_instance;
    group_entry = ctx->gbg_group_ent;
    group = ctx->gbg_ogroup;

    /* Look up the address catalog entry. */

    cat_entry = gmp_get_addr_cat_by_ordinal(&instance->rinst_addr_cat, bitnum);
    gmpx_assert(cat_entry);

    /* Bail if the source isn't active. */

    if (!gmpr_source_ord_is_active(group, bitnum))
	return FALSE;

    /* Stick the source address into the address thread. */

    gmp_enqueue_addr_thread_addr(group_entry->gig_sources,
				 cat_entry->adcat_ent_addr.gmp_addr,
				 instance->rinst_addrlen);

    return FALSE;
}


/*
 * gmpr_get_intf_groups
 *
 * Get a list of all groups and sources on an interface.  Returns a pointer
 * to a group entry, to which other group entries are linked.  Returns NULL
 * if there are no groups on the interface.
 *
 * The pointer should be returned via gmpr_destroy_intf_group() when the
 * caller is done.
 *
 * **** THIS IS NONSCALABLE AND HEINOUS AND SHOULD BE REMOVED AS SOON
 * **** AS PIM WORKS REASONABLY!!!!
 */
gmpr_intf_group_entry *
gmpr_get_intf_groups (gmp_instance_id instance_id, gmpx_intf_id intf_id)
{
    gmpr_instance *instance;
    gmpr_intf *intf;
    gmpr_ogroup *group;
    gmpr_intf_group_entry *group_list;
    gmpr_intf_group_entry *cur_group;
    gmp_addr_list *addr_list;
    gmpr_build_group_cb_context ctx;

    /* Get the instance and interface. */

    instance = gmpr_get_instance(instance_id);
    intf = gmpr_intf_lookup(instance, intf_id);
    if (!intf)
	return NULL;

    /* Walk all groups on the interface. */

    group_list = NULL;
    group = NULL;
    while (TRUE) {

	/* Get the next one.  Bail out if done. */

	group = gmpr_next_oif_group(intf, group);
	if (!group)
	    break;

	/* Got a group.  Allocate an entry. */

	cur_group = gmpx_malloc_block(gmpr_intf_group_tag);
	if (!cur_group)
	    break;			/* Out of memory */

	/* Set the group address and filter mode. */

        memmove(cur_group->gig_group_addr.gmp_addr, group->rogroup_addr.gmp_addr,
            instance->rinst_addrlen);
	cur_group->gig_filter_mode = group->rogroup_filter_mode;

	/* If there are sources, copy them in as well. */

	addr_list = gmpr_ogroup_source_list(group);
	if (!gmp_addr_list_empty(addr_list)) {

	    /* Got sources.  Create the address thread. */

	    cur_group->gig_sources = gmp_alloc_addr_thread();
	    ctx.gbg_instance = instance;
	    ctx.gbg_group_ent = cur_group;
	    ctx.gbg_ogroup = group;
	    gmp_addr_vect_walk(&addr_list->addr_vect, gmpr_build_group_cb,
			       &ctx);
	}

	/* Link the new entry to the list. */

	if (group_list)
	    cur_group->gig_next = group_list;
	group_list = cur_group;
    }

    return group_list;
}


/*
 * gmpr_get_host_groups
 *
 * Get a list of all groups and sources on an interface for the specified
 * host.  Returns a pointer to a group entry, to which other group entries
 * are linked.  Returns NULL if there are no groups on the interface.
 *
 * The pointer should be returned via gmpr_destroy_intf_group() when the
 * caller is done.
 *
 * While the per-interface version of this code does not scale and is 
 * considered to be heinous, a single host should not have too much join
 * state, so this should be OK.
 */
gmpr_intf_group_entry *
gmpr_get_host_groups (gmp_instance_id instance_id,
                      gmpx_intf_id intf_id,
                      const uint8_t *host_addr)
{
    gmpr_instance *instance;
    gmpr_intf *intf;
    gmpr_intf_group_entry *group_list;
    gmpr_intf_group_entry *cur_group;
    gmp_addr_list *addr_list;
    gmpr_host *host;
    gmpx_patnode *host_group_node;
    gmpr_host_group *host_group;
    gmp_addr_cat_entry *cat_entry;
    gmpr_host_group_addr *host_group_addr;
    gmp_addr_list_entry *addr_entry;

    /* Get the instance. */

    instance = gmpr_get_instance(instance_id);

    /* If no matching interface, return NULL */

    intf = gmpr_intf_lookup(instance, intf_id);
    if (!intf)
	return NULL;

    /* If no matching host, return NULL */

    host = gmpr_lookup_host(intf, host_addr);
    if (!host)
        return NULL;

    /* Got a host.  Walk all host groups on the host. */

    group_list = NULL;
    host_group_node = NULL;
    
    while (TRUE) {
        host_group_node = gmpx_patricia_get_next(host->rhost_group_root,
                                                 host_group_node);
        host_group = gmpr_patnode_to_host_group(host_group_node);

        /* Bail if done. */

        if (!host_group)
            break;

        /* Only want the active ones */

        if (!gmpr_host_group_active(host_group))
            continue;

	/* Got a group.  Allocate an entry. */

	cur_group = gmpx_malloc_block(gmpr_intf_group_tag);
	if (!cur_group)
	    break;			/* Out of memory */

	/* Set the group address. */

        memmove(cur_group->gig_group_addr.gmp_addr,
            host_group->rhgroup_addr.gmp_addr,
            instance->rinst_addrlen);

	/* If there are sources, copy them in as well. */

	addr_list = &host_group->rhgroup_addrs;
	if (!gmp_addr_list_empty(addr_list)) {
            /* Walk address list */

	    /* Got sources.  Create the address thread. */

	    cur_group->gig_sources = gmp_alloc_addr_thread();

            addr_entry = NULL;
            while (TRUE) {
                addr_entry = gmp_addr_list_next_entry(addr_list, addr_entry);
                host_group_addr =
                    gmpr_addr_entry_to_host_group_entry(addr_entry);
                if (!host_group_addr || !host_group_addr->rhga_source)
                    break;

                cat_entry =
                    gmp_get_addr_cat_by_ordinal(&instance->rinst_addr_cat,
                                                host_group_addr->rhga_source->
                                                rgroup_addr_entry.addr_ent_ord);
                gmpx_assert(cat_entry);

                gmp_enqueue_addr_thread_addr(cur_group->gig_sources,
                                             cat_entry->adcat_ent_addr.gmp_addr,
                                             instance->rinst_addrlen);
            }
        }

	/* Link the new entry to the list. */

	if (group_list)
	    cur_group->gig_next = group_list;
	group_list = cur_group;
    }

    return group_list;
}


/*
 * gmpr_host_is_active
 *
 * Determine if a host has active groups.
 */
static boolean
gmpr_host_is_active (gmpr_host *host)
{
    gmpx_patnode *host_group_node;
    gmpr_host_group *host_group;

    /* Walk all host groups on the host. */
    host_group_node = NULL;

    while (TRUE) {
        host_group_node = gmpx_patricia_get_next(host->rhost_group_root,
                                                 host_group_node);
        host_group = gmpr_patnode_to_host_group(host_group_node);

        /* Bail if done. */

        if (!host_group)
            break;

        /* Found an active one, so the host is active */

        if (gmpr_host_group_active(host_group))
            return TRUE;
    }

    /* Host has no active groups */

    return FALSE;
}


/*
 * gmpr_get_intf_hosts
 *
 * Get a list of all active hosts on the specified interface.  Returns a
 * pointer  to a host entry, to which other host entries are linked.  
 * Returns NULL* if there are no active hosts on the interface.
 *
 * The pointer should be returned via gmpr_destroy_host_group() when the
 * caller is done.
 *
 * This could be heinous and does not scale if there are lots of hosts
 * on the interface.
 */
gmpr_intf_host_entry *
gmpr_get_intf_hosts (gmp_instance_id instance_id, gmpx_intf_id intf_id)
{
    gmpr_instance *instance;
    gmpr_host *host;
    gmpr_intf *intf;
    gmpr_intf_host_entry *host_list;
    gmpr_intf_host_entry *cur_host;
    gmpx_patnode *host_node;

    /* Get the instance. */

    instance = gmpr_get_instance(instance_id);

    /* If no matching interface, return NULL */

    intf = gmpr_intf_lookup(instance, intf_id);
    if (!intf)
	return NULL;

    /* Walk all hosts on the interface */
    host_list = NULL;
    host_node = NULL;

    while (TRUE) {
        host_node = gmpx_patricia_get_next(intf->rintf_host_root, host_node);
        host = gmpr_patnode_to_host(host_node);

        /* Bail if done. */

        if (!host)
            break;

        /* Ignore hosts that are not active. */

        if (!gmpr_host_is_active(host)) {
            continue;
        }
        
	/* Got an active host.  Allocate an entry. */

	cur_host = gmpx_malloc_block(gmpr_intf_host_tag);
	if (!cur_host)
	    break;			/* Out of memory */

        /* Set the host address */

        memmove(cur_host->gih_host_addr.gmp_addr, host->rhost_addr.gmp_addr,
            instance->rinst_addrlen);

        /*
         * Link the new entry to the list.
         */
        if (host_list) 
            cur_host->gih_next = host_list;
        host_list = cur_host;
    }

    return host_list;
}


/*
 * gmpr_destroy_intf_host
 *
 * Destroy the structure returned by gmpr_get_intf_hosts.
 *
 * Tolerates NULL pointers.
 */
void
gmpr_destroy_intf_host (gmpr_intf_host_entry *host_list)
{
    gmpr_intf_host_entry *next;

    /* Walk the list. */

    while (host_list) {
	next = host_list->gih_next;

	/* Free the entry. */

	gmpx_free_block(gmpr_intf_host_tag, host_list);

	host_list = next;
    }
}


/*
 * gmpr_destroy_intf_group
 *
 * Destroy the structure returned by gmpr_get_intf_groups.
 *
 * Tolerates NULL pointers.
 */
void
gmpr_destroy_intf_group (gmpr_intf_group_entry *group_list)
{
    gmpr_intf_group_entry *next;

    /* Walk the list. */

    while (group_list) {
	next = group_list->gig_next;

	/* Destroy the address thread, if there. */

	gmp_destroy_addr_thread(group_list->gig_sources);

	/* Free the entry. */

	gmpx_free_block(gmpr_intf_group_tag, group_list);

	group_list = next;
    }
}


/*
 * gmpr_is_initialized
 *
 * Returns TRUE if GMP router support has been initialized, or FALSE if not.
 *
 * This lets external callbacks know if it's safe to do stuff in here.
 */
boolean
gmpr_is_initialized (void)
{
    return gr_initialized;
}


/*
 * gmpr_timeout_group_range
 *
 * Prematurely age a range of groups on an interface.
 *
 * send_query is TRUE if we should immediately send a general query on the
 * interface.
 */
void
gmpr_timeout_group_range (gmp_instance_id instance_id, gmpx_intf_id intf_id,
			  const uint8_t *group_addr, uint32_t pfx_len,
			  boolean send_query)
{
    gmpr_instance *instance;
    gmpr_intf *intf;
    gmpr_group *group;
    gmpr_group *next_group;
    uint8_t last_byte_mask;
    uint32_t pfx_full_bytes;
    uint32_t pfx_len_bytes;
    uint32_t extra_bits;
    uint32_t bit_ix;

    /* Get the instance and interface. */

    instance = gmpr_get_instance(instance_id);
    intf = gmpr_intf_lookup(instance, intf_id);
    if (!intf)
	return;

    /* Normalize the prefix length to bytes. */

    pfx_len_bytes = (pfx_len + 7) / 8;
    gmpx_assert(pfx_len_bytes <= instance->rinst_addrlen);
    pfx_full_bytes = pfx_len / 8;

    /* If the last byte isn't full, set up a mask for it. */

    extra_bits = pfx_len % 8;
    last_byte_mask = 0;
    if (extra_bits) {
	for (bit_ix = 0; bit_ix < extra_bits; bit_ix++) {
	    last_byte_mask |= (0x80 >> bit_ix);
	}
    }

    /* Walk all groups on the interface. */

    group = gmpr_next_intf_group(intf, NULL);
    while (group) {
	next_group = gmpr_next_intf_group(intf, group);

	/* Compare the full bytes. */

        if (!memcmp(group_addr, group->rgroup_addr.gmp_addr, pfx_full_bytes)) {

	    /* The full bytes match.  If there's a partial byte, try that. */

	    if (!extra_bits ||
		(group_addr[pfx_full_bytes] ==
		 (group->rgroup_addr.gmp_addr[pfx_full_bytes] &
		 last_byte_mask))) {

		/* If we got here, we've got a match.  Blast the timers. */

		gmpr_timeout_group(group);
	    }
	}

	group = next_group;
    }

    /* If we're supposed to send an immediate query, whack the timer. */

    if (send_query)
	gmpx_start_timer(intf->rintf_query_timer, 0, 0);
}


/*
 * gmpr_sg_is_excluded
 *
 * Returns TRUE if the (intf, s, g) tuple is on an Exclude list, or FALSE
 * if not.
 */
boolean
gmpr_sg_is_excluded (gmp_instance_id instance_id, gmpx_intf_id intf_id,
		     const uint8_t *group_addr, const uint8_t *source_addr)
{
    gmpr_instance *instance;
    gmpr_intf *intf;
    gmpr_group *group;
    gmp_addr_cat_entry *cat_entry;

    /* Get the instance and interface. */

    instance = gmpr_get_instance(instance_id);
    intf = gmpr_intf_lookup(instance, intf_id);
    if (!intf)
	return FALSE;

    /* Look up the address catalog entry for the source address. */

    cat_entry = gmp_lookup_addr_cat_entry(&instance->rinst_addr_cat,
					  source_addr);
    if (!cat_entry)
	return FALSE;

    /* Look up the group. */

    group = gmpr_group_lookup(intf, group_addr);
    if (!group)
	return FALSE;

    /* If the source is in the stopped-timer list, it is being excluded. */

    return gmp_addr_in_list(&group->rgroup_src_addr_stopped,
			    cat_entry->adcat_ent_ord);
}


/*
 * gmpr_update_trace_flags
 *
 * Update trace flags for an instance.
 */
void
gmpr_update_trace_flags (gmp_instance_id instance_id, uint32_t trace_flags)
{
    gmpr_instance *instance;

    /* Get the instance. */

    instance = gmpr_get_instance(instance_id);

    /* Update the trace flags. */

    instance->rinst_traceflags = trace_flags;
}


/*
 * gmpr_query_sequence_internal
 *
 * Trigger an initial query sequence.  "force" is TRUE if we should force
 * the queries even if query transmission is suppressed.
 */
static void
gmpr_query_sequence_internal (gmp_instance_id instance_id, gmpx_intf_id intf_id,
			      boolean force)
{
    gmpr_instance *instance;
    gmpr_intf *intf;

    /* Get the instance and interface. */

    instance = gmpr_get_instance(instance_id);
    intf = gmpr_intf_lookup(instance, intf_id);
    if (intf) {

	/*
	 * Set the flag that overrides query suppression according to
	 * the force parameter.
	 */
	intf->rintf_gen_query_requested = force;

	/* Launch the queries. */

	gmpr_setup_initial_query_timer(intf);
    }
}


/*
 * gmpr_force_general_queries
 *
 * Trigger a sequence of general queries to be sent on an interface.
 * The queries are sent regardless of whether queries are normally
 * suppressed on the interface.  This is typically only used when
 * general queries are otherwise suppressed.
 */
void
gmpr_force_general_queries (gmp_instance_id instance_id, gmpx_intf_id intf_id)
{
    gmpr_query_sequence_internal(instance_id, intf_id, TRUE);
}


/*
 * gmpr_request_general_queries
 *
 * Trigger a general query sequence on an interface.  The queries will
 * not be sent if queries are suppressed on the interface.
 */
void
gmpr_request_general_queries (gmp_instance_id instance_id, gmpx_intf_id intf_id)
{
    gmpr_query_sequence_internal(instance_id, intf_id, FALSE);
}


/*
 * gmpr_one_query_internal
 *
 * Trigger the transmission of a single general query on an interface.
 * "force" is TRUE if we should force the query even if query
 * transmission is suppressed.
 */
static void
gmpr_one_query_internal (gmp_instance_id instance_id, gmpx_intf_id intf_id,
			 boolean force)
{
    gmpr_instance *instance;
    gmpr_intf *intf;

    /* Get the instance and interface. */

    instance = gmpr_get_instance(instance_id);
    intf = gmpr_intf_lookup(instance, intf_id);
    if (intf) {

	/* Set the flag and trigger the transmission. */

	intf->rintf_gen_query_requested = force;
	gmpr_trigger_one_query(intf);
    }
}


/*
 * gmpr_force_one_general_query
 *
 * Trigger the transmission of a single general query on an interface.
 * The query is sent regardless of whether queries are normally
 * suppressed on the interface.  This is typically only used when
 * general queries are otherwise suppressed.
 */
void
gmpr_force_one_general_query (gmp_instance_id instance_id, gmpx_intf_id intf_id)
{
    gmpr_one_query_internal(instance_id, intf_id, TRUE);
}


/*
 * gmpr_request_one_general_query
 *
 * Trigger the transmission of a single general query on an interface.
 * The query will not be sent if queries are suppressed on the
 * interface.
 */
void
gmpr_request_one_general_query (gmp_instance_id instance_id,
				gmpx_intf_id intf_id)
{
    gmpr_one_query_internal(instance_id, intf_id, FALSE);
}


/*
 * gmpr_notify_oif_map_change
 *
 * Update the OIF mappings for an input interface.  This is done when the
 * OIF map for that interface changes, or any other circumstance where
 * the input->output interface mapping needs to be updated.
 */
void
gmpr_notify_oif_map_change (gmp_proto proto, gmpx_intf_id intf_id)
{
    gmpr_intf *intf;

    /* Get the interface. */

    intf = gmpr_intf_lookup_global(proto, intf_id);

    /* Bail if there's no interface. */

    if (!intf)
	return;

    /* Do the deed. */

    gmpr_notify_oif_map_change_internal(intf);
}
