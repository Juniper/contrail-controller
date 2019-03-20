/* $Id: gmpr_client.c 374940 2010-04-20 04:55:18Z weesan $
 *
 * gmpr_client.c - IGMP/MLD Router-Side Client Routines
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/*
 * A note on the notification mechanism
 *
 * In order to keep state bounded, we provide notifications to the
 * clients by threading output groups and/or sources onto those
 * clients' notification threads.  When the client calls
 * gmpr_client_get_notification(), we allocate client notification
 * blocks and pass them back to the client (with at most one client
 * notification block outstanding, so as to not grow memory.)
 *
 * There are two types of notifications--full notifications and
 * deltas.  Full notifications always contain the group and all
 * sources in one structure.  Deltas return per-source changes as
 * appropriate.
 *
 * The gmpr engine assumes that deltas are always in effect, and only
 * threads groups and/or sources that are changing.  If only delta
 * notifications are being requested, these groups and sources are
 * translated one-to-one into notifications in the obvious way.
 *
 * If full notifications are in effect, the group send_full_notif flag
 * is set for the group, regardless of whether the entity being
 * enqueued is a group or a source.  When notifications are generated,
 * this flag is used to trigger the delivery of a full notification.
 *
 * If *only* full notifications are in effect, the group is always
 * enqueued rather than the source, for efficiency (since we don't
 * care about the status of individual sources.)
 *
 * The net result of all of this is that a bunch of deltas enqueued
 * synchronously (on the receipt of a new Report with multiple
 * sources, for instance) will result in only a single full
 * notification being passed.
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

/* Forward references... */

static void gmpr_flush_notifications_client(gmpr_client *client);


/*
 * gmpr_get_client
 * 
 * Return an client pointer, given a client ID.
 *
 * Verifies that the client ID is valid.
 */
gmpr_client *
gmpr_get_client (gmp_client_id client_id)
{
    gmpr_client *client;

    /* Do the (trivial) conversion. */

    client = client_id;

    /* Verify the magic number. */

    gmpx_assert(client->rclient_magic == GMPR_CLIENT_MAGIC);

    return client;
}


/*
 * gmpr_client_startup_expiry
 *
 * Called when the client startup timer expires.  We enqueue everything
 * for the client.
 */
static void
gmpr_client_startup_expiry (gmpx_timer *timer, void *context)
{
    gmpr_client *client;

    client = context;

    gmpx_destroy_timer(timer);
    client->rclient_startup_timer = NULL;

    /* Enqueue everything for the client. */

    gmpr_client_enqueue_all_groups(client, TRUE);
    gmpr_alert_clients(client->rclient_instance);
    gmpr_client_enqueue_all_host_groups(client);
    gmpr_alert_host_clients(client->rclient_instance);
}


/*
 * gmpr_create_client
 *
 * Create a client entry.
 *
 * Returns a pointer to the client entry, or NULL if no memory.
 */
gmpr_client *
gmpr_create_client (gmpr_instance *instance)
{
    gmpr_client *client;
    ordinal_t next_ord;

    /* Grab the next ordinal. */

    next_ord = ord_get_ordinal(instance->rinst_ord_handle);
    if (next_ord == ORD_BAD_ORDINAL)
	return NULL;			/* Out of memory */

    /* If we've got too many clients, bail. */

    if (next_ord >= GMPX_MAX_RTR_CLIENTS) {
	ord_free_ordinal(instance->rinst_ord_handle, next_ord);
	return NULL;			/* Too many clients */
    }

    /* Allocate a client block. */

    client = gmpx_malloc_block(gmpr_client_tag);
    if (!client)			/* No memory */
	return NULL;

    /* Link the client into the instance. */

    client->rclient_magic = GMPR_CLIENT_MAGIC;
    thread_circular_add_top(&instance->rinst_client_thread,
			    &client->rclient_thread);
    client->rclient_instance = instance;
    client->rclient_ordinal = next_ord;

    /* Initialize the notification threads. */

    thread_new_circular_thread(&client->rclient_notif_head);
    thread_new_circular_thread(&client->rclient_host_notif_head);

    /* Initialize the end-of-refresh notification block. */

    client->rclient_refresh_end_notif.gmpr_notify_type =
	GMPR_NOTIFY_REFRESH_END;

    /*
     * Create a timer and launch it with a zero delay.  This is a cheap
     * way of deferring.  The callback will enqueue all notifications for
     * the client.
     */
    client->rclient_startup_timer =
	gmpx_create_timer(instance->rinst_context, "GMP client startup_timer",
			  gmpr_client_startup_expiry, client);
    if (client->rclient_startup_timer)
	gmpx_start_timer(client->rclient_startup_timer, 0, 0);

    return client;
}


/*
 * gmpr_destroy_client
 *
 * Destroy a client entry.  Cleans up appropriately.
 */
void
gmpr_destroy_client (gmpr_client *client)
{
    gmpr_instance *instance;

    instance = client->rclient_instance;

    /* Free the ordinal. */

    ord_free_ordinal(instance->rinst_ord_handle, client->rclient_ordinal);

    /* Flush the notification lists. */

    gmpr_flush_notifications_client(client);
    gmpr_flush_host_notifications_client(client);

    /* Destroy any timers. */

    gmpx_destroy_timer(client->rclient_startup_timer);

    /* Delink the block and free it. */

    thread_remove(&client->rclient_thread);
    client->rclient_instance = NULL;
    gmpx_free_block(gmpr_client_tag, client);
}


/*
 * gmpr_destroy_instance_clients
 *
 * Destroy all clients on an instance.
 */
void
gmpr_destroy_instance_clients (gmpr_instance *instance)
{
    thread *thread_ptr;
    gmpr_client *client;

    /* Walk all clients on the instance. */

    while (TRUE) {
	thread_ptr = thread_circular_top(&instance->rinst_client_thread);
	client = gmpr_thread_to_client(thread_ptr);
	if (!thread_ptr)
	    break;

	/* Destroy the client. */

	gmpr_destroy_client(client);
    }
}


/*
 * gmpr_notifications_active
 *
 * Returns TRUE if there are any active notifications on this
 * notification block array, or FALSE if not.
 */
boolean
gmpr_notifications_active (gmpr_notify_block *notify_block)
{
    uint32_t client_ord;

    /* Walk the notification array. */

    for (client_ord = 0; client_ord < GMPX_MAX_RTR_CLIENTS; client_ord++) {
	if (thread_node_on_thread(&notify_block->gmpr_notify_thread))
	    return TRUE;
	notify_block++;
    }

    return FALSE;
}


/*
 * gmpr_source_notifications_active
 *
 * Returns TRUE if there are any active notifications on this source, or
 * FALSE if not.
 */
static boolean
gmpr_source_notifications_active (gmpr_ogroup_addr_entry *group_addr)
{
    return gmpr_notifications_active(group_addr->rogroup_addr_client_thread);
}


/*
 * gmpr_attempt_free_deleted_addr_entry
 *
 * Attempt to free a deleted address entry.  We assume that it is on
 * the group deleted list.
 *
 * The entry is free if there are no pending notifications left.
 *
 * returns TRUE if the gmpr_ogroup_addr_entry was freed. Otherwise FALSE.
 */
static boolean
gmpr_attempt_free_deleted_addr_entry (gmpr_ogroup_addr_entry *group_addr)
{
    gmpr_ogroup *group;
    boolean deleted_addr_entry = FALSE;

    group = group_addr->rogroup_addr_group;

    /* Do it if there are no active notifications. */

    if (!gmpr_source_notifications_active(group_addr)) {
        deleted_addr_entry = TRUE;
	gmp_delete_addr_list_entry(&group_addr->rogroup_addr_entry);

	/* Try to free the group as well. */

	gmpr_attempt_ogroup_free(group);
    }
    return deleted_addr_entry;
}


/*
 * gmpr_delete_notification
 *
 * Delete a client notification, it having been removed from the
 * client notification list.  Notifications aren't actually "deleted"
 * since they are embedded in other data structures.  But we do any
 * necessary cleanup.
 *
 * return true is the gmpr_notify_block memory has been freed.  This will
 * be true if the outer structure that the gmpr_notify_block is embedded
 * in is freed.  Otherwise return false.
 */
static boolean
gmpr_delete_notification (gmpr_notify_block *notification,
			  ordinal_t client_ord)
{
    gmpr_ogroup *group;
    gmpr_ogroup_addr_entry *group_addr;
    boolean notify_block_freed = FALSE;

    /* See whether it is a group or source notification. */

    switch (notification->gmpr_notify_type) {
      case GMPR_NOTIFY_GROUP:

	/*
	 * Group notification.  Try to free the group, as we may have just
	 * cleaned up the last thing keeping the group alive.
	 */
	group = gmpr_client_notification_to_group(notification, client_ord);
	notify_block_freed = gmpr_attempt_ogroup_free(group);
	break;

      case GMPR_NOTIFY_SOURCE:

	/*
	 * Source notification.  If the entry is on the deleted list
	 * (meaning that we're done with it other than notifications),
	 * try to free the address entry if it is no longer on any
	 * client notification list.
	 */
	group_addr = gmpr_client_notification_to_addr_entry(notification,
							    client_ord);
	if (gmpr_group_addr_deleted(group_addr)) {
	     notify_block_freed = 
                 gmpr_attempt_free_deleted_addr_entry(group_addr);
	}
	break;

      case GMPR_NOTIFY_REFRESH_END:

	/* Refresh end.  Nothing to do. */

	break;

      default:
	gmpx_assert(FALSE);
    }
    return notify_block_freed;
}


/*
 * gmpr_flush_notifications
 *
 * Flush all pending client notifications on a notification block array.
 *
 * If just_delink is TRUE, we simply delink the entries.  If FALSE, we
 * call gmpr_delete_notification to try to clean up whatever the
 * client has embedded.
 */
void
gmpr_flush_notifications (gmpr_notify_block *notify_block, boolean just_delink)
{
    ordinal_t client_ord;
    boolean nb_freed;

    for (client_ord = 0; client_ord < GMPX_MAX_RTR_CLIENTS; client_ord++) {
	if (thread_node_on_thread(&notify_block->gmpr_notify_thread)) {
	    thread_remove(&notify_block->gmpr_notify_thread);
	    if (!just_delink){
	        nb_freed = gmpr_delete_notification(notify_block, client_ord);
                if (nb_freed){
		    return;
		}
            }
	}
	notify_block++;
    }
}


/*
 * gmpr_flush_notifications_group_list
 *
 * Flush all pending client source notifications on a group address list.
 */
static void
gmpr_flush_notifications_group_list (gmp_addr_list *addr_list)
{
    gmp_addr_list_entry *addr_entry, *addr_entry_next;
    gmpr_ogroup_addr_entry *group_addr;

    addr_entry = gmp_addr_list_next_entry(addr_list, NULL);
    addr_entry_next = NULL;
    /* Walk the list. */

    while (TRUE) {
        if (addr_entry)
	    addr_entry_next = gmp_addr_list_next_entry(addr_list, addr_entry);

	group_addr = gmpr_addr_entry_to_ogroup_entry(addr_entry);
	if (!group_addr)
	    break;

	/* Got an entry.  Delink it from each client. */

	gmpr_flush_notifications(group_addr->rogroup_addr_client_thread,
				 FALSE);
        addr_entry = addr_entry_next;
    }
}


/*
 * gmpr_flush_notifications_group
 *
 * Flush all pending client source notifications for a group.  Note that it
 * does not remove the group itself from any notification list if it happens
 * to be there.
 */
void
gmpr_flush_notifications_group (gmpr_ogroup *ogroup)
{
    /* Flush each of the lists where notifications may lie. */

    gmpr_flush_notifications_group_list(&ogroup->rogroup_incl_src_addr);
    gmpr_flush_notifications_group_list(&ogroup->rogroup_excl_src_addr);
    gmpr_flush_notifications_group_list(&ogroup->rogroup_src_addr_deleted);
}


/*
 * gmpr_flush_notifications_client
 *
 * Flush all pending notifications for a client.
 */
static void
gmpr_flush_notifications_client (gmpr_client *client)
{
    gmpr_notify_block *notification;

    thread *thread_ptr;

    /* Walk the client notification list. */

    while (TRUE) {
	thread_ptr = thread_circular_dequeue_top(&client->rclient_notif_head);
	notification = gmpr_thread_to_notify_block(thread_ptr);
	if (!notification)
	    break;

	/* Got a notification.  Delete it. */

	gmpr_delete_notification(notification, client->rclient_ordinal);
    }
}


/*
 * gmpr_update_client_notify
 *
 * Update the notify-client flag in advance of starting to enqueue
 * notifications.  We set it if it was previously clear, and if the
 * notification queue is currently empty.  The net effect is that we
 * set it when enqueueing the first notification.
 */
static void
gmpr_update_client_notify (gmpr_client *client)
{
    if (!client->rclient_notify) {
	client->rclient_notify =
	    thread_circular_thread_empty(&client->rclient_notif_head);
	gmpr_trace(client->rclient_instance, GMPR_TRACE_CLIENT_NOTIFY,
		   "Client %u notify set to %u", client->rclient_ordinal,
		   client->rclient_notify);
    }
}


/*
 * gmpr_enqueue_refresh_end
 *
 * Enqueue a refresh end marker for a client.  We use a notification
 * block embedded in the client block to carry it.  We dequeue it and
 * move it to the end, in the off chance that it is already enqueued.
 */
void
gmpr_enqueue_refresh_end (gmpr_client *client)
{
    gmpr_notify_block *notif;

    /* Grab the block, based on the client ID. */

    notif = &client->rclient_refresh_end_notif;

    /* Dequeue it, just in case. */

    thread_remove(&notif->gmpr_notify_thread);

    /* Update the notification flag. */

    gmpr_update_client_notify(client);

    /* Enqueue it. */

    thread_circular_add_bottom(&client->rclient_notif_head,
			       &notif->gmpr_notify_thread);
}


/*
 * gmpr_client_enqueue_group
 *
 * Enqueue one output group onto a client notification thread.
 *
 * If it was already enqueued, it is delinked and moved to the end.
 */
static void
gmpr_client_enqueue_group (gmpr_client *client, gmpr_ogroup *group)
{
    ordinal_t client_ord;
    thread *thread_ptr;

    /*
     * Bail if the client startup timer is running.  We'll be doing a full
     * state enqueue when it expires.
     */
    if (client->rclient_startup_timer)
	return;

    /* Note if the client's notification thread was empty. */

    gmpr_update_client_notify(client);

    /*
     * Delink the group from the client thread, in case it was already
     * on there, and then requeue it at the end.
     */
    client_ord = client->rclient_ordinal;
    thread_ptr = &group->rogroup_client_thread[client_ord].gmpr_notify_thread;
    thread_remove(thread_ptr);
    thread_circular_add_bottom(&client->rclient_notif_head, thread_ptr);

    /* If we're doing full notifications, flag that we need one. */

    if (client->rclient_cb_context.rctx_full_notifications)
	group->rogroup_send_full_notif[client->rclient_ordinal] = TRUE;
}


/*
 * gmpr_client_enqueue_source
 *
 * Enqueue one source address onto a client notification thread.
 *
 * If it was already enqueued, it is delinked and moved to the end.
 *
 * If we're doing only full notifications (and no deltas) we instead
 * enqueue the group.  The net result of this is that the notification
 * thread will consist solely of group entries (and no sources.)
 */
static void
gmpr_client_enqueue_source (gmpr_client *client,
			    gmpr_ogroup_addr_entry *group_addr)
{
    gmpr_ogroup *group;
    ordinal_t client_ord;
    thread *thread_ptr;

    /*
     * If we're only doing full notifications, enqueue the group instead
     * and bail.
     */
    if (client->rclient_cb_context.rctx_full_notifications &&
	(!client->rclient_cb_context.rctx_delta_notifications)) {
	gmpr_client_enqueue_group(client, group_addr->rogroup_addr_group);
	return;
    }

    /*
     * Bail if the client startup timer is running.  We'll be doing a full
     * state enqueue when it expires.
     */
    if (client->rclient_startup_timer)
	return;

    /* Note if the client's notification thread was empty. */

    gmpr_update_client_notify(client);

    /*
     * Delink the group from the client thread, in case it was already
     * on there, and then requeue it at the end.
     */
    client_ord = client->rclient_ordinal;
    thread_ptr =
	&group_addr->rogroup_addr_client_thread[client_ord].gmpr_notify_thread;
    thread_remove(thread_ptr);
    thread_circular_add_bottom(&client->rclient_notif_head, thread_ptr);

    /* If we're doing full notifications, flag that we need one. */

    if (client->rclient_cb_context.rctx_full_notifications) {
	group = group_addr->rogroup_addr_group;
	group->rogroup_send_full_notif[client->rclient_ordinal] = TRUE;
    }
}


/*
 * gmpr_group_notify_clients
 *
 * Enqueue a group notification for all clients.
 *
 * The group is threaded onto the notification thread for each client,
 * and a notification callback is made if the thread was previously
 * empty.
 */
void
gmpr_group_notify_clients (gmpr_ogroup *group)
{
    gmpr_instance *instance;
    gmpr_client *client;
    thread *thread_ptr;

    instance = group->rogroup_intf->rintf_instance;

    /* Walk all clients. */

    FOR_ALL_CIRCULAR_THREAD_ENTRIES(&instance->rinst_client_thread,
				    thread_ptr) {
	client = gmpr_thread_to_client(thread_ptr);

	/* Enqueue the group. */

	gmpr_client_enqueue_group(client, group);
    }
}


/*
 * gmpr_source_notify_clients
 *
 * Notify all clients that a source has changed state.
 *
 * The "flag" parameter says if the notification is conditional (based
 * on the setting of the rogroup_notify flag) or unconditional.
 *
 * The source is threaded onto the notification thread for each client,
 * and a notification callback is made if the thread was previously
 * empty.
 */
void
gmpr_source_notify_clients (gmpr_ogroup_addr_entry *group_addr,
			    gmpr_source_notify_flag flag)
{
    gmpr_instance *instance;
    gmpr_client *client;
    thread *thread_ptr;
    boolean client_found;

    instance = group_addr->rogroup_addr_group->rogroup_intf->rintf_instance;

    /* Do it if we're supposed to. */

    client_found = FALSE;
    if ((flag == NOTIFY_UNCONDITIONAL) || group_addr->rogroup_notify) {

	/* Walk all clients. */

	FOR_ALL_CIRCULAR_THREAD_ENTRIES(&instance->rinst_client_thread,
					thread_ptr) {
	    client = gmpr_thread_to_client(thread_ptr);
	    client_found = TRUE;

	    /* Enqueue the notification. */

	    gmpr_client_enqueue_source(client, group_addr);
	}
    }
    group_addr->rogroup_notify = FALSE;

    /*
     * If there were no clients, go ahead and attempt to free the
     * entry if it was deleted.  This is a paranoia check, since a
     * lingering deleted entry will block ever freeing the group.
     */
    if (!client_found && gmpr_group_addr_deleted(group_addr)) {
	gmpr_attempt_free_deleted_addr_entry(group_addr);
    }
}


/*
 * gmpr_enqueue_all_source_notifications
 *
 * Enqueue all appropriate source notifications for an output group.
 *
 * We ignore the deleted list, and only look at the include or exclude
 * list as appropriate.
 *
 * If client is non-NULL, the notifications are enqueued only for that
 * client.  Otherwise they are enqueued for all clients.
 */
void
gmpr_enqueue_all_source_notifications (gmpr_ogroup *group, gmpr_client *client)
{
    gmp_addr_list *addr_list;
    gmpr_ogroup_addr_entry *group_addr;
    gmp_addr_list_entry *addr_entry;

    /* Select the address list based on the group filter mode. */

    addr_list = gmpr_ogroup_source_list(group);

    /* Walk the address list, enqueueing each active entry. */

    addr_entry = NULL;
    while (TRUE) {
	addr_entry = gmp_addr_list_next_entry(addr_list, addr_entry);
	group_addr = gmpr_addr_entry_to_ogroup_entry(addr_entry);
	if (!group_addr)
	    break;
	if (gmpr_source_is_active(group, group_addr)) {
	    if (client) {
		gmpr_client_enqueue_source(client, group_addr);
	    } else {
		gmpr_source_notify_clients(group_addr, NOTIFY_UNCONDITIONAL);
	    }
	}
    }
}


/*
 * gmpr_mode_change_notify_clients
 *
 * Notify all clients about an interface mode change for a group.  This
 * consists of flushing all pending notifications for that group, and then
 * enqueueing the group and any sources.
 */
void
gmpr_mode_change_notify_clients (gmpr_ogroup *group)
{
    /* Enqueue the group. */

    gmpr_group_notify_clients(group);

    /* Flush any pending notifications. */

    gmpr_flush_notifications_group(group);

    /* Enqueue all appropriate source notifications. */

    gmpr_enqueue_all_source_notifications(group, NULL);
}


/*
 * gmpr_client_enqueue_all_intf_groups
 *
 * Enqueue all groups and sources associated with a single interface onto
 * a client notification thread.
 *
 * If flush is TRUE, we flush out all notifications for the groups's
 * sources first.  This cleans up any lingering source deletions.
 */
void
gmpr_client_enqueue_all_intf_groups (gmpr_client *client, gmpr_intf *intf,
				     boolean flush)
{
    gmpr_ogroup *group;

    /* Walk all groups on the interface. */

    group = NULL;

    while (TRUE) {
	group = gmpr_next_oif_group(intf, group);

	/* Bail if done. */

	if (!group)
	    break;

	/*
	 * If we're asked to, flush any notifications for this group
	 * first.  This serves to remove any pending source delete
	 * notifications (which will be freed as a side effect).
	 */
	if (flush)
	    gmpr_flush_notifications_group(group);

	/* Got a group.  Enqueue it. */

	gmpr_client_enqueue_group(client, group);

	/* Enqueue all of the sources as well. */

	gmpr_enqueue_all_source_notifications(group, client);
    }
}


/*
 * gmpr_client_enqueue_all_groups
 *
 * Enqueue all groups and sources onto a client notification thread.
 * We call this when a new client appears.  Clients also use this to
 * refresh their state if they have to.
 *
 * If flush is TRUE, we flush out all source notifications first.  This
 * gets rid of any pending source deletions.
 */
void
gmpr_client_enqueue_all_groups (gmpr_client *client, boolean flush)
{
    gmpr_instance *instance;
    gmpr_intf *intf;

    instance = client->rclient_instance;

    /* Walk all interfaces on the instance. */

    intf = NULL;

    while (TRUE) {
	intf = gmpr_next_instance_intf(instance, intf);
	if (!intf)
	    break;

	/* Enqueue everything on the interface. */

	gmpr_client_enqueue_all_intf_groups(client, intf, flush);
    }
}


/*
 * gmpr_free_notification
 *
 * Free up a notification.
 */
void
gmpr_free_notification (gmpr_client_notification *notification)
{
    gmp_destroy_addr_thread(notification->notif_addr_thread);
    gmpx_free_block(gmpr_notification_tag, notification);
}


/*
 * gmpr_build_full_notification
 *
 * Build a full notification.  We build an address thread from the source
 * addresses, if any.
 */
static void
gmpr_build_full_notification (gmpr_instance *instance,
			      gmpr_client_notification *client_notif,
			      gmpr_ogroup *group)
{
    gmp_addr_list *addr_list;
    gmp_addr_thread *addr_thread;
    gmp_addr_list_entry *addr_entry;
    gmpr_ogroup_addr_entry *group_addr;
    gmp_addr_cat_entry *cat_entry;

    /* Set the notification type. */

    client_notif->notif_type = GMPR_NOTIF_GROUP_STATE;

    /* Create an address thread with all active sources, if present. */

    addr_list = gmpr_ogroup_source_list(group);
    if (!gmp_addr_list_empty(addr_list)) {

	/* List is there.  Create an address thread. */

	addr_thread = gmp_alloc_addr_thread();
	client_notif->notif_addr_thread = addr_thread;

	/* Walk the address list. */

	addr_entry = NULL;

	while (TRUE) {
	    addr_entry = gmp_addr_list_next_entry(addr_list, addr_entry);
	    group_addr = gmpr_addr_entry_to_ogroup_entry(addr_entry);
	    if (!group_addr)
		break;

	    /*
	     * Got a source address entry.  Stick it in the thread if
	     * it's active.
	     */
	    if (gmpr_source_is_active(group, group_addr)) {
		cat_entry =
		    gmp_get_addr_cat_by_ordinal(&instance->rinst_addr_cat,
						addr_entry->addr_ent_ord);
		gmpx_assert(cat_entry);
		gmp_enqueue_addr_thread_addr(addr_thread,
					 cat_entry->adcat_ent_addr.gmp_addr,
					 instance->rinst_addrlen);
	    }
	}
    }
}


/*
 * gmpr_build_delta_notification
 * 
 * Build a delta notification.
 */
static void
gmpr_build_delta_notification (gmpr_instance *instance,
			       gmpr_notify_block *notification,
			       gmpr_client_notification *client_notif,
			       gmpr_ogroup *group,
			       gmpr_ogroup_addr_entry *group_addr)
{
    gmp_addr_list_entry *addr_entry;
    gmp_addr_cat_entry *cat_entry;

   /* Switch based on notification type. */

    switch (notification->gmpr_notify_type) {
      case GMPR_NOTIFY_GROUP:

	/*
	 * We've got a group notification.  If the group state is
	 * Include {}, or the interface is down, we're deleting the
	 * group.  Otherwise, we're adding the group in either Include
	 * or Exclude state.
	 */
	if (gmpr_ogroup_is_active(group) && group->rogroup_intf->rintf_up) {
	    if (group->rogroup_filter_mode == GMP_FILTER_MODE_INCLUDE) {
		client_notif->notif_type = GMPR_NOTIF_GROUP_ADD_INCL;
	    } else {
		client_notif->notif_type = GMPR_NOTIF_GROUP_ADD_EXCL;
	    }
	} else {
	    client_notif->notif_type = GMPR_NOTIF_GROUP_DELETE;
	}
	break;

      case GMPR_NOTIFY_SOURCE:

	/*
	 * We've got a client notification.  Look up the source
	 * address and copy it to the client notification.
	 */
	addr_entry = &group_addr->rogroup_addr_entry;
	cat_entry = gmp_get_addr_cat_by_ordinal(&instance->rinst_addr_cat,
						addr_entry->addr_ent_ord);
	gmpx_assert(cat_entry);
	memmove(client_notif->notif_source_addr.gmp_addr,
        cat_entry->adcat_ent_addr.gmp_addr,
        instance->rinst_addrlen);

	/*
	 * Now split out in each combination of the current filter
	 * mode, and which list the address entry is on in order to
	 * determine the notification type.  We treat a source as deleted
	 * if the output interface is down.
	 */
	switch (group->rogroup_filter_mode) {
	  case GMP_FILTER_MODE_INCLUDE:

	    /* Include mode.  See which list the entry is on. */

	    if (gmpr_group_addr_deleted(group_addr) ||
		!group->rogroup_intf->rintf_up) {

		/* Deleted.  Send a BLOCK event. */

		client_notif->notif_type = GMPR_NOTIF_BLOCK_SOURCE;

	    } else if (gmpr_group_addr_included(group_addr)) {

		/* Included.  Send an ALLOW event. */

		client_notif->notif_type = GMPR_NOTIF_ALLOW_SOURCE;

	    } else {

		/*
		* There can't be anything on the Exclude list in
		* Include mode.
		*/
		gmpx_assert(FALSE);
	    }
	    break;

	  case GMP_FILTER_MODE_EXCLUDE:

	    /* Exclude mode.  See which list the entry is on. */

	    if (gmpr_group_addr_deleted(group_addr) ||
		gmpr_group_addr_included(group_addr) ||
		!group->rogroup_intf->rintf_up) {

		/*
		 * Deleted or included or interface down.  Send an
		 * ALLOW event.
		 */
		client_notif->notif_type = GMPR_NOTIF_ALLOW_SOURCE;

	    } else {

		/* Excluded.  Send a BLOCK event. */

		client_notif->notif_type = GMPR_NOTIF_BLOCK_SOURCE;
	    }
	    break;

	  default:
	    gmpx_assert(FALSE);
	    break;
	}

	break;

      case GMPR_NOTIFY_REFRESH_END:

	/* Refresh end marker.  Pass a refresh end notification. */

	client_notif->notif_type = GMPR_NOTIF_REFRESH_END;
	break;

      default:
	gmpx_assert(FALSE);
	break;
    }
}


/*
 * gmpr_fill_client_notification
 *
 * Fill in the non-common client notification fields based on our internal
 * notification type and other state information.
 *
 * Returns a pointer to the group entry.
 */
static gmpr_ogroup *
gmpr_fill_client_notification (gmpr_instance *instance,
			       gmpr_notify_block *notification,
			       gmpr_client *client,
			       gmpr_client_notification *client_notif)
{
    gmpr_ogroup *group;
    gmpr_ogroup_addr_entry *group_addr;

    /* First, figure out the group. */

    switch (notification->gmpr_notify_type) {
      case GMPR_NOTIFY_GROUP: 	
	group_addr = NULL;
	group = gmpr_client_notification_to_group(notification,
						  client->rclient_ordinal);
	break;

      case GMPR_NOTIFY_SOURCE:
	group_addr =
	    gmpr_client_notification_to_addr_entry(notification,
						   client->rclient_ordinal);
	group = group_addr->rogroup_addr_group;
	break;

      case GMPR_NOTIFY_REFRESH_END:
	group_addr = NULL;
	group = NULL;
	break;

      default:
	gmpx_assert(FALSE);
	group = NULL;			/* Quiet the compiler. */
	group_addr = NULL;
	break;

    }

    /*
     * Got the group.  If the send_full_notif flag is set for this
     * client,bthe group is active, and the interface is up, build a
     * full notification with all of the trimmings.
     */
    if (group && gmpr_ogroup_is_active(group) &&
	group->rogroup_intf->rintf_up &&
	group->rogroup_send_full_notif[client->rclient_ordinal]) {

	/* Clear the flag to show we're doing it. */

	group->rogroup_send_full_notif[client->rclient_ordinal] = FALSE;

	/* Build the notification. */

	gmpr_build_full_notification(instance, client_notif, group);

    } else {

	/* Not sending a full notification.  Build a delta (or deletion). */

	gmpr_build_delta_notification(instance, notification, client_notif,
				      group, group_addr);
    }

    return group;
}


/*
 * gmpr_alert_clients
 *
 * Process pending notifications for all clients.  This consists of
 * calling the client callback for any clients flagged as needing to be
 * called.
 *
 * We do this step separately so that we don't end up calling back the
 * client from deep within our message processing.
 */
void
gmpr_alert_clients (gmpr_instance *instance)
{
    gmpr_client *client;
    gmpr_client_context *cli_ctx;
    thread *thread_ptr;

    gmpr_trace_agent("Alert Client : file : %s, line : %.",
                            __FILE__, __LINE__);

    /* Walk all clients. */

    FOR_ALL_CIRCULAR_THREAD_ENTRIES(&instance->rinst_client_thread,
				    thread_ptr) {
	client = gmpr_thread_to_client(thread_ptr);

	/* If this is the first notification for the client, wake it up. */

	if (client->rclient_notify) {
	    gmpr_trace(client->rclient_instance, GMPR_TRACE_CLIENT_NOTIFY,
		       "Client %u callback", client->rclient_ordinal);
	    client->rclient_notify = FALSE;
	    cli_ctx = &client->rclient_cb_context;
	    (*cli_ctx->rctx_notif_cb)(client->rclient_context);
	}
    }
}


/*
 * gmpr_client_get_notification
 *
 * Get the next notification for a client.
 *
 * Returns a pointer to the notification block, or NULL if there's nothing
 * there.
 *
 * We do some funny games here if the client is interested in both
 * delta and full notifications.  Regardless of the notification type
 * (group or source) we look at the group send_full_notif flag, and if
 * TRUE, we send a full notification to the client, clear the flag,
 * and leave the notification entry on the thread.  The next time we
 * get called we'll deliver the deltas.
 */
gmpr_client_notification *
gmpr_client_get_notification (gmpr_client *client,
			      gmpr_client_notification *last_notification)
{
    thread *thread_ptr;
    gmpr_ogroup *group;
    gmpr_instance *instance;
    gmpr_client_notification *client_notif;
    gmpr_notify_block *notification;
    boolean both_notifs;

    instance = client->rclient_instance;

    /* If there is an old client notification there, reuse it. */

    client_notif = NULL;
    if (last_notification) {
	client_notif = last_notification;
	gmp_destroy_addr_thread(client_notif->notif_addr_thread);
	memset(client_notif, 0, sizeof(gmpr_client_notification));
    }

    /* Note whether we're doing both kinds of notifications. */

    both_notifs = (client->rclient_cb_context.rctx_delta_notifications &&
		   client->rclient_cb_context.rctx_full_notifications);

    /* Pick up the top of the notification thread. */
 
    thread_ptr = thread_circular_top(&client->rclient_notif_head);
    notification = gmpr_thread_to_notify_block(thread_ptr);

    /* Bail if there's nothing there. */

    if (!notification) {

	/* Free any old client notification. */

	if (client_notif)
	    gmpr_free_notification(client_notif);

	return NULL;
    }

    /* If we don't have a client notification block, get one now. */

    if (!client_notif) {
	client_notif = gmpx_malloc_block(gmpr_notification_tag);
	if (!client_notif)
	    return NULL;		/* Out of memory */
    }

    /* Fill in the non-common fields. */
    
    group = gmpr_fill_client_notification(instance, notification, client,
					  client_notif);

    /* Fill in the common fields. */

    if (group) {
	client_notif->notif_intf_id = group->rogroup_intf->rintf_id;
	memmove(client_notif->notif_group_addr.gmp_addr,
        group->rogroup_addr.gmp_addr,
        instance->rinst_addrlen);
	client_notif->notif_filter_mode = group->rogroup_filter_mode;
    }

    /*
     * If the client has requested both kinds of notifications and we're
     * delivering a full notification, we leave the notification block
     * on the thread (so that we'll generate the delta the next time around.)
     * Otherwise, we dequeue the notification block and attempt to free
     * its contents.
     */
    if (!both_notifs || client_notif->notif_type != GMPR_NOTIF_GROUP_STATE) {
	thread_circular_dequeue_top(&client->rclient_notif_head);
	gmpr_delete_notification(notification, client->rclient_ordinal);
    }

    /*
     * A couple of notes for PR 509013:
     * 1. We add logic here to give hints to the clients, such as
     *    IGMP, about the last notification of the same group so that
     *    the clients would know to process a few more notifications
     *    even when its quantum has been reached.
     * 2. This block of code is deliberately separated from the code
     *    above to maintain its independence and modularity.
     * 3. mcsnoopd is a client that asks for both kinds of
     *    notifications.  The fix here should work for mcsnoopd as
     *    well.  However, in this set of fix, only IGMP and MLD are
     *    changed to take advantage of this.  A separate PR is needed
     *    in order to add similar logic to mcsnoopd.
     */

    /*
     * Take a peek at the next notification to see if it is
     * for a new (*,g).  If yes, mark the current notification as the
     * last (s,g).
     */
    client_notif->notif_last_sg = FALSE;
    thread_ptr = thread_circular_top(&client->rclient_notif_head);
    /*
     * If the client has requested both kinds of notifications, the
     * notification block is not popped from the top of the thread
     * (see comments above), as such, we need to look further to its
     * next block in the thread.
     */
    if (thread_ptr &&
	both_notifs && client_notif->notif_type == GMPR_NOTIF_GROUP_STATE) {
	thread_ptr = thread_circular_thread_next(&client->rclient_notif_head,
						 thread_ptr);
    }
    if (thread_ptr) {
	notification = gmpr_thread_to_notify_block(thread_ptr);
	if (notification && 
	    notification->gmpr_notify_type == GMPR_NOTIFY_GROUP) {
	    client_notif->notif_last_sg = TRUE;
	}
    } else {
	/* Last notification will always be the last (s,g). */
	client_notif->notif_last_sg = TRUE;
    }

    /* Trace it. */

    gmpr_trace(instance, GMPR_TRACE_CLIENT_NOTIFY,
	       "Client %u notif %i %a %s %a",
	       client->rclient_ordinal, client_notif->notif_intf_id,
	       client_notif->notif_group_addr.gmp_addr,
	       gmpr_client_notif_string(client_notif->notif_type),
	       client_notif->notif_source_addr.gmp_addr);

    return client_notif;
}
