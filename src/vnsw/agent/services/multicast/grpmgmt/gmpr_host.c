/* $Id: gmpr_host.c 429362 2011-03-09 10:55:35Z ib-builder $
 *
 * gmpr_host.c - IGMP/MLD Router-Side host management
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 * This module contains host management support for router-side GMP.  We do
 * host tracking for accounting purposes, and to allow for query suppression
 * and fast Leaves.
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_private.h"
#include "gmp_router.h"
#include "gmpr_private.h"
#include "gmp_trace.h"
#include "gmpr_trace.h"

/* Forward references */

static void gmpr_host_source_expiry(gmpx_timer *timer, void *context);
static void gmpr_host_group_expiry(gmpx_timer *timer, void *context);
static void gmpr_delete_host_notification(gmpr_notify_block *notification,
					  ordinal_t client_ord,
					  boolean delete_any);
static void gmpr_flush_notifications_host_group(gmpr_host_group *host_group);


/*
 * gmpr_host_source_notifications_active
 *
 * Returns TRUE if there are any active host notifications on this source, or
 * FALSE if not.
 */
static boolean
gmpr_host_source_notifications_active (gmpr_host_group_addr *group_addr)
{
    return gmpr_notifications_active(group_addr->rhga_notify);
}


/*
 * gmpr_host_group_addr_alloc
 *
 * Allocate a host group address entry.
 *
 * Returns NULL if out of memory.
 */
static gmp_addr_list_entry *
gmpr_host_group_addr_alloc (void *context)
{
    gmpr_host_group *host_group;
    gmpr_host_group_addr *hg_addr;
    gmpr_instance *instance;

    host_group = context;
    instance = host_group->rhgroup_host->rhost_intf->rintf_instance;

    /* Allocate a block. */

    hg_addr = gmpx_malloc_block(gmpr_host_group_addr_tag);
    if (!hg_addr)
	return NULL;

    /* Initialize a wee bit. */

    gmpr_set_notification_type(hg_addr->rhga_notify, GMPR_NOTIFY_HOST_SOURCE);
    hg_addr->rhga_host_group = host_group;
    hg_addr->rhga_timer =
	gmpx_create_timer(instance->rinst_context, "GMP router host source",
			  gmpr_host_source_expiry, hg_addr);

    return &hg_addr->rhga_addr_ent;
}


/*
 * gmpr_flush_host_notifications
 *
 * Flush all pending client notifications on a notification block array.
 */
static void
gmpr_flush_host_notifications (gmpr_notify_block *notify_block)
{
    ordinal_t client_ord;

    for (client_ord = 0; client_ord < GMPX_MAX_RTR_CLIENTS; client_ord++) {
	if (thread_node_on_thread(&notify_block->gmpr_notify_thread)) {
	    thread_remove(&notify_block->gmpr_notify_thread);
	}
	notify_block++;
    }
}


/*
 * gmpr_host_group_addr_free
 *
 * Free a host group address entry.
 */
static void
gmpr_host_group_addr_free (gmp_addr_list_entry *addr_entry)
{
    gmpr_host_group_addr *hg_addr;

    hg_addr = gmpr_addr_entry_to_host_group_entry(addr_entry);

    /* Flush the notification list. */

    gmpr_flush_host_notifications(hg_addr->rhga_notify);

    /* Delink it. */

    thread_remove(&hg_addr->rhga_thread);

    /* Free the timer. */

    gmpx_destroy_timer(hg_addr->rhga_timer);

    /* Free the block. */

    gmpx_free_block(gmpr_host_group_addr_tag, addr_entry);
}


/*
 * gmpr_alert_host_clients
 *
 * Call back all clients with pending host notifications.
 */
void
gmpr_alert_host_clients (gmpr_instance *instance)
{
    gmpr_client *client;
    gmpr_client_context *cli_ctx;
    thread *thread_ptr;

    /* Bail if not doing host tracking. */

    if (!instance->rinst_host_tracking)
	return;

    /* Walk all clients. */

    FOR_ALL_CIRCULAR_THREAD_ENTRIES(&instance->rinst_client_thread,
				    thread_ptr) {
	client = gmpr_thread_to_client(thread_ptr);

	/* Process the client if it has a host notification callback. */

	cli_ctx = &client->rclient_cb_context;
	if (cli_ctx->rctx_host_notif_cb) {

	    /* If this is the first notification for the client, wake it up. */

	    if (client->rclient_host_notify) {
		gmpr_trace(instance, GMPR_TRACE_HOST_NOTIFY,
			   "Client %u host callback", client->rclient_ordinal);
		client->rclient_host_notify = FALSE;
		(*cli_ctx->rctx_host_notif_cb)(client->rclient_context);
	    }
	}
    }
}


/*
 * gmpr_host_group_notifications_active
 *
 * Returns TRUE if there are any active notifications on this host_group, or
 * FALSE if not.
 */
static boolean
gmpr_host_group_notifications_active (gmpr_host_group *host_group)
{
    return gmpr_notifications_active(host_group->rhgroup_notify);
}


/*
 * gmpr_destroy_host_group
 *
 * Destroy a host group entry.  If the entry is locked (the lock count is
 * nonzero) we flag the entry as deleted.  This routine will very shortly
 * be called back with the lock removed.
 */
static void
gmpr_destroy_host_group (gmpr_host_group *host_group)
{
    gmpr_host *host;
    gmpr_instance *instance;

    host = host_group->rhgroup_host;
    instance = host->rhost_intf->rintf_instance;

    gmpr_trace_agent("Destroy host group : file : %s, line : %.",
                            __FILE__, __LINE__);

    /* If the refcount is nonzero, flag that we're deleted and bail. */

    if (host_group->rhgroup_lock_count) {
	host_group->rhgroup_is_deleted = TRUE;

	/* Trace it. */

	gmpr_trace(instance, GMPR_TRACE_HOST_NOTIFY,
		   "Host group %a redundant destroy",
		   host_group->rhgroup_addr.gmp_addr);
	return;
    }

    /* Flush the notification lists. */

    gmpr_flush_notifications_host_group(host_group);
    gmpr_flush_host_notifications(host_group->rhgroup_notify);

    /* Flush the address lists. */

    gmp_addr_list_clean(&host_group->rhgroup_addrs);
    gmp_addr_list_clean(&host_group->rhgroup_deleted);

    /* Delink. */

    thread_remove(&host_group->rhgroup_thread);
    gmpx_assert(gmpx_patricia_delete(host->rhost_group_root,
				     &host_group->rhgroup_node));

    /* Free the timer. */

    gmpx_destroy_timer(host_group->rhgroup_timer);

    /* Free the block. */

    gmpx_free_block(gmpr_host_group_tag, host_group);
}


/*
 * gmpr_lock_host_group
 *
 * Temporarily lock a host group to keep it from being deleted.
 * These locks are expected to be very short-lived (within the scope
 * of a single execution) and to never be nested (though we allow
 * a limited amount of nesting.)
 */
static void
gmpr_lock_host_group (gmpr_host_group *host_group)
{
    host_group->rhgroup_lock_count++;
}


/*
 * gmpr_unlock_host_group
 *
 * Unlock the host group.  If the refcount is zero and the deleted flag
 * is set, go ahead and destroy it now.
 */
static void
gmpr_unlock_host_group (gmpr_host_group *host_group)
{
    gmpx_assert(host_group->rhgroup_lock_count);
    host_group->rhgroup_lock_count--;

    if (!host_group->rhgroup_lock_count && host_group->rhgroup_is_deleted)
	gmpr_destroy_host_group(host_group);
}


/*
 * gmpr_destroy_host
 *
 * Destroy a host entry.  Things should be normally be pretty well
 * cleaned up first, but may not be if we're cleaning up with extreme
 * prejudice.
 */
static void
gmpr_destroy_host (gmpr_host *host)
{
    gmpr_host_group *host_group;
    gmpx_patnode *node;

    /* Flush all of the host groups. */

    while (TRUE) {
	node = gmpx_patricia_lookup_least(host->rhost_group_root);
	if (!node)
	    break;
	host_group = gmpr_patnode_to_host_group(node);
	gmpr_destroy_host_group(host_group);
    }
    gmpx_patroot_destroy(host->rhost_group_root);

    /* Delink us from the interface. */

    gmpr_trace_agent("Destroy host : file : %s, line : %.",
                            __FILE__, __LINE__);

    gmpx_assert(gmpx_patricia_delete(host->rhost_intf->rintf_host_root,
				     &host->rhost_node));

    /* Free the block. */

    gmpx_free_block(gmpr_host_tag, host);
}


/*
 * gmpr_destroy_intf_hosts
 *
 * Destroy all hosts on an interface.
 */
void
gmpr_destroy_intf_hosts (gmpr_intf *intf)
{
    gmpr_host *host;
    gmpx_patnode *node;

    /* Walk the host tree, destroying everything in our path. */

    while (TRUE) {
	node = gmpx_patricia_lookup_least(intf->rintf_host_root);
	if (!node)
	    break;
	host = gmpr_patnode_to_host(node);
	gmpr_destroy_host(host);
    }
}


/*
 * gmpr_attempt_host_free
 *
 * Try to free a host.  It will be freed if everything has been cleaned up.
 */
static void
gmpr_attempt_host_free (gmpr_host *host)
{
    /* Bail if there are any groups. */

    gmpr_trace_agent("Attempt host free : file : %s, line : %.",
                            __FILE__, __LINE__);

    if (gmpx_patricia_lookup_least(host->rhost_group_root))
	return;

    /* That was easy.  Destroy the host. */

    gmpr_destroy_host(host);
}


/*
 * gmpr_attempt_host_group_free
 *
 * Attempt to free the host_group entry.  We will do so if there is no
 * client interest in the host_group and there's nothing more to send
 * for the host_group.
 */
static void
gmpr_attempt_host_group_free (gmpr_host_group *host_group)
{
    gmpr_host *host;

    host = host_group->rhgroup_host;

    gmpr_trace_agent("Attempt host group free : file : %s, line : %.",
                            __FILE__, __LINE__);

    /* Bail if the entry is active. */

    if (gmpr_host_group_active(host_group))
	return;

    /* Bail if there are any pending client notifications. */

    if (gmpr_host_group_notifications_active(host_group))
	return;

    /* Bail if there's anything on any of the lists. */

    if (!gmp_addr_list_empty(&host_group->rhgroup_addrs))
	return;
    if (!gmp_addr_list_empty(&host_group->rhgroup_deleted))
	return;

    /* Looks safe.  Destroy the host_group. */

    gmpr_destroy_host_group(host_group);

    /* Try to delete the host. */

    gmpr_attempt_host_free(host);
}


/*
 * gmpr_attempt_free_host_addr_entry
 *
 * Attempt to free a host group address entry.
 *
 * The entry is freed if there are no pending notifications left.
 */
static void
gmpr_attempt_free_host_addr_entry (gmpr_host_group_addr *group_addr)
{
    gmpr_host_group *host_group;

    host_group = group_addr->rhga_host_group;

    /* Do it if there are no active notifications. */

    if (!gmpr_host_source_notifications_active(group_addr))
	gmp_delete_addr_list_entry(&group_addr->rhga_addr_ent);

    /* Try to free the host group. */

    gmpr_attempt_host_group_free(host_group);
}


/*
 * gmpr_delete_host_notification
 *
 * Delete a host notification, it having been removed from the
 * client notification list.  Notifications aren't actually "deleted"
 * since they are embedded in other data structures.  But we do any
 * necessary cleanup.
 *
 * If "delete_any" is set, we will attempt to free any kind of block
 * carrying a source wnotification.  Otherwise, we'll only try to free
 * deleted entries.
 */
static void
gmpr_delete_host_notification (gmpr_notify_block *notification,
			       ordinal_t client_ord, boolean delete_any)
{
    gmpr_host_group *host_group;
    gmpr_host_group_addr *host_group_addr;

    /* See whether it is a host_group or source notification. */

    switch (notification->gmpr_notify_type) {
      case GMPR_NOTIFY_HOST_GROUP:

	/*
	 * Host_Group notification.  Try to free the host_group, as we
	 * may have just cleaned up the last thing keeping the
	 * host_group alive.
	 */
	host_group = gmpr_client_notification_to_host_group(notification,
							    client_ord);
	gmpr_attempt_host_group_free(host_group);
	break;

      case GMPR_NOTIFY_HOST_SOURCE:

	/*
	 * Source notification.  If the entry is on the deleted list
	 * (meaning that we're done with it other than notifications),
	 * try to free the address entry if it is no longer on any
	 * client notification list.
	 */
	host_group_addr =
	    gmpr_client_notification_to_host_group_addr(notification,
							client_ord);
	if (gmpr_host_group_addr_deleted(host_group_addr) || delete_any) {
	    gmpr_attempt_free_host_addr_entry(host_group_addr);
	}
	break;

      default:
	gmpx_assert(FALSE);
    }
}


/*
 * gmpr_flush_notifications_host_group_list
 *
 * Flush all pending client source notifications on a host_group
 * address list.
 */
static void
gmpr_flush_notifications_host_group_list (gmp_addr_list *addr_list)
{
    gmp_addr_list_entry *addr_entry;
    gmpr_host_group_addr *host_group_addr;

    addr_entry = NULL;

    /* Walk the list. */

    while (TRUE) {
	addr_entry = gmp_addr_list_next_entry(addr_list, addr_entry);
	host_group_addr = gmpr_addr_entry_to_host_group_entry(addr_entry);
	if (!host_group_addr)
	    break;

	/* Got an entry.  Delink it from each client. */

	gmpr_flush_host_notifications(host_group_addr->rhga_notify);
    }
}


/*
 * gmpr_flush_notifications_host_group
 *
 * Flush all pending client source notifications for a host_group.
 * Note that it does not remove the host_group itself from any
 * notification list if it happens to be there.
 */
static void
gmpr_flush_notifications_host_group (gmpr_host_group *host_group)
{
    /* Flush each of the lists where notifications may lie. */

    gmpr_flush_notifications_host_group_list(&host_group->rhgroup_addrs);
    gmpr_flush_notifications_host_group_list(&host_group->rhgroup_deleted);
}


/*
 * gmpr_flush_host_notifications_client
 *
 * Flush all pending notifications for a client.  This is done when the
 * client is being destroyed.
 */
void
gmpr_flush_host_notifications_client (gmpr_client *client)
{
    gmpr_notify_block *notification;

    thread *thread_ptr;

    /* Walk the client notification list. */

    while (TRUE) {
	thread_ptr =
	    thread_circular_dequeue_top(&client->rclient_host_notif_head);
	notification = gmpr_thread_to_notify_block(thread_ptr);
	if (!notification)
	    break;

	/* Got a notification.  Delete it. */

	gmpr_delete_host_notification(notification, client->rclient_ordinal,
				      TRUE);
    }
}


/*
 * gmpr_update_client_host_notify
 *
 * Update the notify-client flag in advance of starting to enqueue
 * host notifications.  We set it if it was previously clear, and if
 * the notification queue is currently empty.  The net effect is that
 * we set it when enqueueing the first notification.
 */
static void
gmpr_update_client_host_notify (gmpr_client *client)
{
    if (!client->rclient_host_notify) {
	client->rclient_host_notify =
	    thread_circular_thread_empty(&client->rclient_host_notif_head);
	gmpr_trace(client->rclient_instance, GMPR_TRACE_HOST_NOTIFY,
		   "Client %u host notify set to %u",
		   client->rclient_ordinal, client->rclient_host_notify);
    }
}


/*
 * gmpr_client_enqueue_host_group
 *
 * Enqueue one host_group onto a client notification thread.
 *
 * If it was already enqueued, it is delinked and moved to the end.
 */
static void
gmpr_client_enqueue_host_group (gmpr_client *client,
				gmpr_host_group *host_group)
{
    ordinal_t client_ord;

    /* Bail if there is no host notification callback for this client. */

    if (!client->rclient_cb_context.rctx_host_notif_cb)
	return;

    /*
     * Bail if the client startup timer is running.  We'll be doing a full
     * state enqueue when it expires.
     */
    if (client->rclient_startup_timer)
	return;

    /* Update the notification flag. */

    gmpr_update_client_host_notify(client);

    /*
     * Delink the host_group from the client thread, in case it was already
     * on there, and then requeue it at the end.
     */
    client_ord = client->rclient_ordinal;
    thread_remove(&host_group->rhgroup_notify[client_ord].gmpr_notify_thread);
    thread_circular_add_bottom(&client->rclient_host_notif_head,
	       &host_group->rhgroup_notify[client_ord].gmpr_notify_thread);
}


/*
 * gmpr_client_enqueue_host_source
 *
 * Enqueue one source address onto a client notification thread.
 *
 * If it was already enqueued, it is delinked and moved to the end.
 */
static void
gmpr_client_enqueue_host_source (gmpr_client *client,
				 gmpr_host_group_addr *host_group_addr)
{
    ordinal_t client_ord;

    /* Bail if there is no host notification callback for this client. */

    if (!client->rclient_cb_context.rctx_host_notif_cb)
	return;

    /*
     * Bail if the client startup timer is running.  We'll be doing a full
     * state enqueue when it expires.
     */
    if (client->rclient_startup_timer)
	return;

    /* Update the client host notify flag. */

    gmpr_update_client_host_notify(client);

    /*
     * Delink the host_group from the client thread, in case it was already
     * on there, and then requeue it at the end.
     */
    client_ord = client->rclient_ordinal;
    thread_remove(
	  &host_group_addr->rhga_notify[client_ord].gmpr_notify_thread);
    thread_circular_add_bottom(&client->rclient_host_notif_head,
	&host_group_addr->rhga_notify[client_ord].gmpr_notify_thread);
}


/*
 * gmpr_host_group_notify_clients
 *
 * Enqueue a host_group notification for all clients.
 *
 * The host_group is threaded onto the notification thread for each client,
 * and a notification callback is made if the thread was previously
 * empty.
 */
static void
gmpr_host_group_notify_clients (gmpr_host_group *host_group)
{
    gmpr_instance *instance;
    gmpr_client *client;
    thread *thread_ptr;

    instance = host_group->rhgroup_host->rhost_intf->rintf_instance;

    /* Walk all clients. */

    FOR_ALL_CIRCULAR_THREAD_ENTRIES(&instance->rinst_client_thread,
				    thread_ptr) {
	client = gmpr_thread_to_client(thread_ptr);

	/* Enqueue the host_group. */

	gmpr_client_enqueue_host_group(client, host_group);
    }
}


/*
 * gmpr_host_source_notify_clients
 *
 * Notify all clients that a source has changed state.
 *
 * The source is threaded onto the notification thread for each client,
 * and a notification callback is made if the thread was previously
 * empty.
 */
static void
gmpr_host_source_notify_clients (gmpr_host_group_addr *host_group_addr)
{
    gmpr_instance *instance;
    gmpr_client *client;
    thread *thread_ptr;
    gmpr_host_group *host_group;

    host_group = host_group_addr->rhga_host_group;
    instance = host_group->rhgroup_host->rhost_intf->rintf_instance;

    /* Walk all clients. */

    FOR_ALL_CIRCULAR_THREAD_ENTRIES(&instance->rinst_client_thread,
				    thread_ptr) {
	client = gmpr_thread_to_client(thread_ptr);

	/* Enqueue the notification. */

	gmpr_client_enqueue_host_source(client, host_group_addr);
    }

    /*
     * Attempt to free the entry if it was deleted.  If we actually
     * enqueued a notification, this will do nothing.
     */
    if (gmpr_host_group_addr_deleted(host_group_addr)) {
	gmpr_attempt_free_host_addr_entry(host_group_addr);
    }
}


/*
 * gmpr_enqueue_all_source_notifications
 *
 * Enqueue all appropriate source notifications for a host_group.
 *
 * We assume that the deleted list for the host_group is empty at this point,
 * so we only look at the running and deleted lists.
 *
 * If client is non-NULL, the notifications are enqueued only for that
 * client.  Otherwise they are enqueued for all clients.
 */
static void
gmpr_enqueue_all_host_source_notifications (gmpr_host_group *host_group,
					    gmpr_client *client)
{
    gmp_addr_list *addr_list;
    gmpr_host_group_addr *host_group_addr;
    gmp_addr_list_entry *addr_entry;

    addr_list = &host_group->rhgroup_addrs;

    /* Walk the address list, enqueueing each entry. */

    addr_entry = NULL;
    while (TRUE) {
	addr_entry = gmp_addr_list_next_entry(addr_list, addr_entry);
	host_group_addr = gmpr_addr_entry_to_host_group_entry(addr_entry);
	if (host_group_addr)
	    break;
	if (client) {
	    gmpr_client_enqueue_host_source(client, host_group_addr);
	} else {
	    gmpr_host_source_notify_clients(host_group_addr);
	}
    }
}


/*
 * gmpr_client_enqueue_all_host_groups
 *
 * Enqueue all host_groups and sources onto a client notification thread.
 * We call this when a new client appears.  Clients also use this to
 * refresh their state if they have to.
 */
void
gmpr_client_enqueue_all_host_groups (gmpr_client *client)
{
    gmpr_instance *instance;
    gmpr_host *host;
    gmpr_host_group *host_group;
    gmpr_intf *intf;
    gmpx_patnode *host_node, *host_group_node;

    instance = client->rclient_instance;

    /* Bail if there is no host notification callback for this client. */

    if (!client->rclient_cb_context.rctx_host_notif_cb)
	return;

    /* Walk all interfaces on the instance. */

    intf = NULL;

    while (TRUE) {
	intf = gmpr_next_instance_intf(instance, intf);

	/* Bail if done. */

	if (!intf)
	    break;

	/* Walk all hosts on the interface. */

	host_node = NULL;

	while (TRUE) {
	    host_node = gmpx_patricia_get_next(intf->rintf_host_root,
					       host_node);
	    host = gmpr_patnode_to_host(host_node);

	    /* Bail if done. */

	    if (!host)
		break;

	    /* Got a host.  Walk all host groups on the host. */

	    host_group_node = NULL;

	    while (TRUE) {
		host_group_node =
		    gmpx_patricia_get_next(host->rhost_group_root,
					   host_group_node);
		host_group = gmpr_patnode_to_host_group(host_group_node);

		/* Bail if done. */

		if (!host_group)
		    break;

		/* Got a host group.  See if it is active. */

		if (gmpr_host_group_active(host_group)) {

		    /*
		     * It's active.  If it has sources, just enqueue
		     * them.  Otherwise, enqueue the group.
		     */
		    if (gmp_addr_list_empty(&host_group->rhgroup_addrs)) {
			gmpr_client_enqueue_host_group(client, host_group);
		    } else {
			gmpr_enqueue_all_host_source_notifications(host_group,
								   client);
		    }
		}
	    }
	}
    }
}


/*
 * gmpr_fill_client_host_notif
 *
 * Fill in the non-common client notification fields based on our internal
 * notification type and other state information.
 *
 * Returns a pointer to the host_group entry.
 */
static gmpr_host_group *
gmpr_fill_client_host_notif (gmpr_instance *instance,
			     gmpr_notify_block *notification,
			     gmpr_client *client,
			     gmpr_client_host_notification *client_notif)
{
    gmpr_host_group *host_group;
    gmp_addr_cat_entry *cat_entry;
    gmpr_host_group_addr *host_group_addr;
    gmp_addr_list_entry *addr_entry;
    // gmpr_host *host;

    /* Switch based on notification type. */

    switch (notification->gmpr_notify_type) {
      case GMPR_NOTIFY_HOST_GROUP:

	/*
	 * We've got a host_group notification.  If the host group
	 * isn't active, we're deleting the host_group.
	 */
	host_group =
	    gmpr_client_notification_to_host_group(notification,
						   client->rclient_ordinal);
	// host = host_group->rhgroup_host;
	if (!gmpr_host_group_active(host_group)) {

	    /*
	     * Host group is being deleted.  If the host group timer
	     * isn't running, the group has timed out.
	     */
	    if (!gmpx_timer_running(host_group->rhgroup_timer)) {
		client_notif->host_notif_type = GMPR_NOTIF_HOST_TIMEOUT;
	    } else {

		/* Not a timeout.  If the interface is down, say so. */

		if (!host_group->rhgroup_host->rhost_intf->rintf_up) {
		    client_notif->host_notif_type = GMPR_NOTIF_HOST_IFDOWN;
		} else {
		    client_notif->host_notif_type = GMPR_NOTIF_HOST_LEAVE;
		}
	    }

	} else {

	    /* Not deleted.  It's a join. */

	    client_notif->host_notif_type = GMPR_NOTIF_HOST_JOIN;
	}
	client_notif->host_notif_source_present = FALSE;
	break;

      case GMPR_NOTIFY_HOST_SOURCE:

	/*
	 * We've got a client notification.  Look up the source
	 * address and copy it to the client notification.
	 */
	host_group_addr =
	    gmpr_client_notification_to_host_group_addr(notification,
						client->rclient_ordinal);
	host_group = host_group_addr->rhga_host_group;
	// host = host_group->rhgroup_host;
	addr_entry = &host_group_addr->rhga_addr_ent;
	cat_entry = gmp_get_addr_cat_by_ordinal(&instance->rinst_addr_cat,
						addr_entry->addr_ent_ord);
	gmpx_assert(cat_entry);
	memmove(client_notif->host_notif_source_addr.gmp_addr,
        cat_entry->adcat_ent_addr.gmp_addr,
        instance->rinst_addrlen);
	client_notif->host_notif_source_present = TRUE;

	/* Set the notification type. */

	if (gmpr_host_group_addr_deleted(host_group_addr)) {

	    /*
	     * Host group address is being deleted.  If the host group
	     * source timer isn't running, the group has timed out.
	     */
	    if (!gmpx_timer_running(host_group_addr->rhga_timer)) {
		client_notif->host_notif_type = GMPR_NOTIF_HOST_TIMEOUT;
	    } else {

		/* Not a timeout.  If the interface is down, say so. */

		if (!host_group->rhgroup_host->rhost_intf->rintf_up) {
		    client_notif->host_notif_type = GMPR_NOTIF_HOST_IFDOWN;
		} else {
		    client_notif->host_notif_type = GMPR_NOTIF_HOST_LEAVE;
		}
	    }

	} else {

	    /* Not deleted.  It's a join. */

	    client_notif->host_notif_type = GMPR_NOTIF_HOST_JOIN;
	}
	break;

      default:
	gmpx_assert(FALSE);
	host_group = NULL;		/* Quiet the compiler */
	break;
    }	    

    return host_group;
}


/*
 * gmpr_client_free_host_notification
 * 
 * Free a host notification block.
 */
void
gmpr_client_free_host_notification (gmpr_client_host_notification *host_notif)
{
    if (host_notif)
	gmpx_free_block(gmpr_host_notification_tag, host_notif);
}


/*
 * gmpr_client_host_notif_string
 *
 * Returns a string for the host notification type, given the type.
 */
static const char *
gmpr_client_host_notif_string (gmpr_client_host_notification_type type)
{
    switch (type) {
      case GMPR_NOTIF_HOST_JOIN:
	return "Join";
      case GMPR_NOTIF_HOST_LEAVE:
	return "Leave";
      case GMPR_NOTIF_HOST_TIMEOUT:
	return "Timeout";
      case GMPR_NOTIF_HOST_IFDOWN:
	return "Ifdown";
      default:
	return "Unknown";
    }
}


/*
 * gmpr_client_get_host_notification
 *
 * Get the next host notification for a client.
 *
 * Returns a pointer to the notification block, or NULL if there's nothing
 * there.
 */
gmpr_client_host_notification *
gmpr_client_get_host_notification (gmpr_client *client,
			   gmpr_client_host_notification *last_notification)
{
    thread *thread_ptr;
    gmpr_host_group *host_group;
    gmpr_host *host;
    gmpr_instance *instance;
    gmpr_client_host_notification *client_notif;
    gmpr_notify_block *notification;
    gmpr_intf *intf;

    instance = client->rclient_instance;

    /* If there is an old client notification there, reuse it. */

    client_notif = NULL;
    if (last_notification) {
	client_notif = last_notification;
	memset(client_notif, 0, sizeof(gmpr_client_host_notification));
    }
	
    /* Dequeue the top of the notification thread. */
 
    thread_ptr = thread_circular_dequeue_top(&client->rclient_host_notif_head);
    notification = gmpr_thread_to_notify_block(thread_ptr);

    /* Bail if there's nothing there. */

    if (!notification) {

	/* Free any old client notification. */

	gmpr_client_free_host_notification(client_notif);

	return NULL;
    }

    /* If we don't have a client notification block, get one now. */

    if (!client_notif) {
	client_notif = gmpx_malloc_block(gmpr_host_notification_tag);
	if (!client_notif)
	    return NULL;		/* Out of memory */
    }

    /* Fill in the non-common fields. */
    
    host_group = gmpr_fill_client_host_notif(instance, notification,
					     client, client_notif);
    host = host_group->rhgroup_host;

    /* Fill in the common fields. */

    intf = host->rhost_intf;
    client_notif->host_notif_intf_id = intf->rintf_id;
    memmove(client_notif->host_notif_group_addr.gmp_addr,
        host_group->rhgroup_addr.gmp_addr,
        instance->rinst_addrlen);
    memmove(client_notif->host_notif_host_addr.gmp_addr,
        host_group->rhgroup_host->rhost_addr.gmp_addr,
        instance->rinst_addrlen);

    /* Trace it. */

    gmpr_trace(instance, GMPR_TRACE_HOST_NOTIFY,
	       "Client %u host notif %s %i (%a, %a) %s host %a",
	       client->rclient_ordinal,
	       gmpr_client_host_notif_string(client_notif->host_notif_type),
	       client_notif->host_notif_intf_id,
	       client_notif->host_notif_group_addr.gmp_addr,
	       client_notif->host_notif_source_addr.gmp_addr,
	       (client_notif->host_notif_source_present ? "(SP)":""),
	       client_notif->host_notif_host_addr.gmp_addr);

    /*
     * Delete the notification, which cleans up a bunch of stuff and
     * may free the address entry, host group, and host.
     */
    gmpr_delete_host_notification(notification, client->rclient_ordinal,
				  FALSE);

    return client_notif;
}


/*
 * gmpr_lookup_host_group
 *
 * Look up a host group entry, given the host and group address.
 *
 * Returns a pointer to the host group entry, or NULL if not found.
 */
static gmpr_host_group *
gmpr_lookup_host_group (gmpr_host *host, gmp_addr_string *group_addr)
{
    gmpr_host_group *host_group;
    gmpx_patnode *node;

    /* Look up the host group in the host tree. */

    node = gmpx_patricia_lookup(host->rhost_group_root, group_addr);
    host_group = gmpr_patnode_to_host_group(node);

    return host_group;
}


/*
 * gmpr_create_host_group
 *
 * Create a host_group entry.
 *
 * Returns a pointer to the host_group entry, or NULL if out of memory.
 */
static gmpr_host_group *
gmpr_create_host_group (gmpr_host *host, gmp_addr_string *group_addr)
{
    gmpr_host_group *host_group;
    gmpr_instance *instance;

    instance = host->rhost_intf->rintf_instance;
    host_group = gmpx_malloc_block(gmpr_host_group_tag);
    if (!host_group)
	return NULL;			/* Out of memory */

    /* Got the block.  Initialize it. */

    memmove(host_group->rhgroup_addr.gmp_addr, group_addr->gmp_addr, instance->rinst_addrlen);
    gmp_addr_list_init(&host_group->rhgroup_addrs, &instance->rinst_addr_cat,
		       gmpr_host_group_addr_alloc, gmpr_host_group_addr_free,
		       host_group);
    gmp_addr_list_init(&host_group->rhgroup_deleted, &instance->rinst_addr_cat,
		       gmpr_host_group_addr_alloc, gmpr_host_group_addr_free,
		       host_group);
    host_group->rhgroup_host = host;
    gmpr_set_notification_type(host_group->rhgroup_notify,
			       GMPR_NOTIFY_HOST_GROUP);
    host_group->rhgroup_timer =
	gmpx_create_timer(instance->rinst_context, "GMP router host group",
			  gmpr_host_group_expiry, host_group);

    /* Put it into the tree. */

    gmpx_assert(gmpx_patricia_add(host->rhost_group_root,
				  &host_group->rhgroup_node));

    return host_group;
}


/*
 * gmpr_lookup_host
 *
 * Look up a host entry, given the interface and host address.
 *
 * Returns a pointer to the host entry, or NULL if not found.
 */
gmpr_host *
gmpr_lookup_host (gmpr_intf *intf, const uint8_t *host_addr)
{
    gmpr_host *host;
    gmpx_patnode *node;

    /* Look up the host in the tree. */

    node = gmpx_patricia_lookup(intf->rintf_host_root, host_addr);
    host = gmpr_patnode_to_host(node);

    return host;
}


/*
 * gmpr_create_host
 *
 * Create a host entry.
 *
 * Returns a pointer to the host entry, or NULL if out of memory.
 */
static gmpr_host *
gmpr_create_host (gmpr_intf *intf, uint8_t *host_addr)
{
    gmpr_host *host;
    gmpr_instance *instance;

    instance = intf->rintf_instance;
    host = gmpx_malloc_block(gmpr_host_tag);
    if (!host)
	return NULL;			/* Out of memory */

    /* Got the block.  Initialize it. */

    memmove(host->rhost_addr.gmp_addr, host_addr, instance->rinst_addrlen);

    host->rhost_group_root =
	gmpx_patroot_init(instance->rinst_addrlen,
			  GMPX_PATRICIA_OFFSET(gmpr_host_group, rhgroup_node,
					       rhgroup_addr));
    host->rhost_intf = intf;

    /* Put it into the tree. */

    gmpx_assert(gmpx_patricia_add(intf->rintf_host_root, &host->rhost_node));

    return host;
}


/*
 * gmpr_delete_host_source
 *
 * Delete a source from a host group.  We don't actually delete it, but
 * instead move it to the deleted list, do associated cleanup, and enqueue
 * a notification for the clients.
 */
static void
gmpr_delete_host_source (gmpr_host_group_addr *hg_addr)
{
    gmpr_host *host; 
    gmpr_host_group *host_group;
    gmpr_group_addr_entry *group_addr_entry;

    host_group = hg_addr->rhga_host_group;
    host = host_group->rhgroup_host;

    /* Delink us from the main address entry. */

    group_addr_entry = hg_addr->rhga_source;
    thread_remove(&hg_addr->rhga_thread);
    hg_addr->rhga_source = NULL;

    /*
     * If we are doing fast leaves and we just deleted the last
     * host group address from the group address, it's time to
     * stop forwarding from this source.
     */
    if (group_addr_entry && host->rhost_intf->rintf_fast_leaves &&
	thread_circular_thread_empty(
			     &group_addr_entry->rgroup_host_addr_head)) {
	gmpr_last_host_addr_ref_gone(group_addr_entry);
    }

    /* Move the entry to the deleted list. */

    gmp_move_addr_list_entry(&host_group->rhgroup_deleted,
			     &hg_addr->rhga_addr_ent);

    /* Enqueue the notification. */

    gmpr_host_source_notify_clients(hg_addr);
}


/*
 * gmpr_delete_source_cb
 *
 * Vector callback to delete a source from the host group entry.
 */
static boolean
gmpr_delete_source_cb (void *context, bv_bitnum_t bitnum,
		       boolean new_val GMPX_UNUSED,
		       boolean old_val GMPX_UNUSED)
{
    gmpr_host_group *host_group;
    gmpr_host_group_addr *hg_addr;
    gmp_addr_list_entry *host_addr_entry;
    gmp_addr_list *active_list;

    host_group = context;
    active_list = &host_group->rhgroup_addrs;

    /* See if the source is there. */

    if (gmp_addr_in_list(active_list, bitnum)) {

	/* Source is there.  Get the address entry. */

	host_addr_entry =
	    gmp_lookup_addr_entry(active_list, bitnum);
	hg_addr = gmpr_addr_entry_to_host_group_entry(host_addr_entry);
	gmpx_assert(hg_addr);

	/* Delete the host address. */

	gmpr_delete_host_source(hg_addr);
    }

    return FALSE;
}


/*
 * gmpr_add_source_cb
 *
 * Vector callback to add a source to the host group entry.
 * The source may already be on the list.
 */
static boolean
gmpr_add_source_cb (void *context, bv_bitnum_t bitnum,
		    boolean new_val GMPX_UNUSED, boolean old_val GMPX_UNUSED)
{
    // gmpr_instance *instance;
    gmpr_host_group *host_group;
    gmpr_host_group_addr *hg_addr;
    gmp_addr_list_entry *host_addr_entry;
    gmp_addr_list_entry *main_addr_entry;
    gmpr_group_addr_entry *group_addr_entry;
    gmpr_host *host;
    gmpr_group *group;
    gmp_addr_list *active_list;
    gmp_addr_list *delete_list;

    host_group = context;
    host = host_group->rhgroup_host;
    // instance = host->rhost_intf->rintf_instance;
    active_list = &host_group->rhgroup_addrs;
    delete_list = &host_group->rhgroup_deleted;

    /* See if the source is already present. */

    if (gmp_addr_in_list(active_list, bitnum)) {

	/* Already there.  Get the address. */

	host_addr_entry =
	    gmp_lookup_addr_entry(active_list, bitnum);
	hg_addr = gmpr_addr_entry_to_host_group_entry(host_addr_entry);
	gmpx_assert(hg_addr);

    } else {

	/* Not there.  See if it's on the delete list. */

	if (gmp_addr_in_list(delete_list, bitnum)) {

	    /* On the delete list.  Move it to the active list. */

	    host_addr_entry =
		gmp_lookup_addr_entry(delete_list, bitnum);
	    hg_addr = gmpr_addr_entry_to_host_group_entry(host_addr_entry);
	    gmpx_assert(hg_addr);

	    gmp_move_addr_list_entry(active_list, host_addr_entry);

	} else {

	    /* Not there.  Allocate a new one. */

	    host_addr_entry =
		gmp_create_addr_list_entry(active_list, bitnum);
	    hg_addr = gmpr_addr_entry_to_host_group_entry(host_addr_entry);
	    if (!hg_addr)
		return TRUE;		/* Out of memory */
	}
    }

    /*
     * If the host address entry isn't linked to a main address entry,
     * do so now.  This can happen for existing host addresses if the
     * interface channel limit was previously hit but the channel has
     * just been created.
     */
    if (!thread_node_on_thread(&hg_addr->rhga_thread)) {

	/*
	 * Look up the address entry on the main group running list and
	 * add this entry to the thread.  This builds a list of host
	 * contributions to each running timer entry, so we can do fast
	 * leave processing when all of the contributions go away.
	 */
	group = host_group->rhgroup_group;
	if (group) {
	    main_addr_entry =
		gmp_lookup_addr_entry(&group->rgroup_src_addr_running, bitnum);
	    group_addr_entry = gmpr_addr_entry_to_group_entry(main_addr_entry);
	    hg_addr->rhga_source = group_addr_entry;
	    if (group_addr_entry) {

		/* Found the entry.  Add the new entry to the thread. */

		thread_circular_add_bottom(
				   &group_addr_entry->rgroup_host_addr_head,
				   &hg_addr->rhga_thread);
	    }
	}

	/* Enqueue the notification. */

	gmpr_host_source_notify_clients(hg_addr);
    }

    /* (Re)start the source timer. */

    gmpx_start_timer(hg_addr->rhga_timer,
		     host->rhost_intf->rintf_group_membership_ivl, 0);

    return FALSE;
}


/*
 * gmpr_delink_host_group
 *
 * Delink a host group from the main group entry, which makes it inactive.
 * Tolerates already being unlinked.
 */
static void
gmpr_delink_host_group (gmpr_host_group *host_group)
{
    gmpr_trace_agent("Delink host group : file : %s, line : %.",
                            __FILE__, __LINE__);

    host_group->rhgroup_group = NULL;
    thread_remove(&host_group->rhgroup_thread);
}


/*
 * gmpr_host_process_report
 *
 * Do host-level processing on a report group record.
 *
 * We're doing host tracking for accounting purposes, and to potentially
 * speed up leaves by not waiting for query timeouts.  In both cases, we
 * don't care about non-null Exclude state.
 */
void
gmpr_host_process_report (uint8_t *src_addr, gmp_report_rectype rec_type,
			  gmpr_group *group, gmp_addr_vect *source_vect)
{
    // gmpr_instance *instance;
    gmpr_host *host;
    gmpr_host_group *host_group;
    gmpr_intf *intf;
    boolean delete_group;
    boolean notify_group;
    boolean was_empty;
    boolean new_group;
    boolean group_was_active;

    gmpx_assert(group);
    // instance = group->rgroup_intf->rintf_instance;
    intf = group->rgroup_intf;

    gmpr_trace_agent("Host process report : file : %s, line : %.",
                            __FILE__, __LINE__);


    /* Don't bother if we're not doing host processing. */

    if (!intf->rintf_instance->rinst_host_tracking)
	return;

    /* Look up the host. */

    host = gmpr_lookup_host(intf, src_addr);

    /* If not there, create one. */

    if (!host) {
	host = gmpr_create_host(intf, src_addr);
	if (!host)
	    return;			/* Out of memory */
    }

    /* Look up the host group. */

    host_group = gmpr_lookup_host_group(host, &group->rgroup_addr);

    /* If not there, create one. */

    new_group = !host_group;
    if (!host_group) {
	host_group = gmpr_create_host_group(host, &group->rgroup_addr);
	if (!host_group)
	    return;			/* Out of memory */
    }

    /*
     * If the entry isn't active (either we just created it or we
     * reanimated an inactive entry) link to the main group, which
     * makes it active.  We may undo this later.
     */
    group_was_active = gmpr_host_group_active(host_group);
    if (!group_was_active) {
	gmpx_assert(!thread_node_on_thread(&host_group->rhgroup_thread));
	host_group->rhgroup_group = group;
	thread_circular_add_bottom(&group->rgroup_host_group_head,
				   &host_group->rhgroup_thread);
    }

    /* Fork based on the report type. */

    delete_group = FALSE;
    notify_group = FALSE;
    was_empty = gmp_addr_list_empty(&host_group->rhgroup_addrs);
    switch (rec_type) {

      case GMP_RPT_IS_IN:
      case GMP_RPT_TO_IN:

	/*
	 * Host is either in include mode or is switching to it.  If we're
	 * switching, the record contains all sources for the group and we
	 * can delete any not listed.  If we're not switching, the record is
	 * not idempotent, so we can't delete unlisted sources.
	 *
	 * Note that if everything is working the way it's supposed to, there
	 * will be no sources when switching from Exclude to Include mode
	 * since we don't track Exclude sources.  And when a redundant TO_IN
	 * is received, nothing will change with all of the machinations
	 * below.  But we go through the effort in case the host is broken
	 * or we've lost a bunch of host reports or something.
	 *
	 * First, if we're switching, let's delete those unlisted sources
	 * by forming the set A-B and deleting all addresses in the result.
	 */
	if (rec_type == GMP_RPT_TO_IN) {
	    if (gmp_addr_vect_minus(&host_group->rhgroup_addrs.addr_vect,
				    source_vect, NULL,
				    gmpr_delete_source_cb, host_group,
				    BV_CALL_SET) < 0)
		return;			/* Out of memory */
	}

	/*
	 * Now walk all sources listed in the report and add them to the
	 * group or refresh the source timer as appropriate.
	 */
	if (gmp_addr_vect_walk(source_vect, gmpr_add_source_cb,
			       host_group) < 0) {
	    return;			/* Out of memory */
	}

	/* Now see if there are any more sources left. */

	if (gmp_addr_list_empty(&host_group->rhgroup_addrs)) {

	    /*
	     * Nothing left in the host group, which means we got a
	     * Leave or its equivalent.  Flag it for deletion.  If
	     * there were no sources in the group before, and the
	     * group wasn't just created, flag it for notification.
	     * (If there were sources, the client will be getting
	     * notifications for each source, so we don't need to
	     * notify for the group.)  The new_group check keeps us
	     * from generating a host notification when the host sends
	     * a redundant Leave (which it is supposed to do.)
	     */
	    delete_group = TRUE;
	    if (was_empty && !new_group) {
		notify_group = TRUE;
	    }
	}
	break;

      case GMP_RPT_IS_EX:
      case GMP_RPT_TO_EX:

	/*
	 * Host is in Exclude mode.  We treat these as equivalent to (*,G)
	 * joins, so we clear all sources.  We flush the delete list as
	 * well, since the (*,G) join subsumes any source blocks.
	 */
	gmp_flush_addr_list(&host_group->rhgroup_addrs);
	gmp_flush_addr_list(&host_group->rhgroup_deleted);

	/*
	 * Notify the clients if either the list wasn't empty before
	 * (meaning we've gone from (S,G) to (*,G)) or the group wasn't
	 * active before (meaning that it's a new group join.)
	 */
	if (!was_empty || !group_was_active)
	    notify_group = TRUE;
	break;

      case GMP_RPT_ALLOW:

	/*
	 * Host wants to allow traffic for some sources.  If the
	 * address list is empty, we're already listening to all
	 * sources (in exclude mode) so we ignore it.  Otherwise we
	 * add the sources to the list (and send source notifications
	 * as necessary.)
	 */
	if (!group_was_active ||
	    !gmp_addr_list_empty(&host_group->rhgroup_addrs)) {

	    /* Something there (or the group is inactive.)  Add the sources. */

	    if (gmp_addr_vect_walk(source_vect, gmpr_add_source_cb,
				   host_group) < 0) {
		return;			/* Out of memory */
	    }

	}
	break;

      case GMP_RPT_BLOCK:

	/*
	 * Host wants to block traffic for some source.  If the group
	 * is inactive, we're already blocking everything (just
	 * waiting for the notification to go out) so we ignore it.
	 * If active but the address list is empty, we're listening to
	 * all sources in Exclude mode and ignore it (since we don't
	 * track non-null exclude lists.)  Otherwise, the host is in
	 * Include mode, and we delete entries on the list.
	 */
	if (group_was_active &&
	    !gmp_addr_list_empty(&host_group->rhgroup_addrs)) {

	    /* Something there.  Delete the sources. */

	    if (gmp_addr_vect_inter(source_vect,
				    &host_group->rhgroup_addrs.addr_vect, NULL,
				    gmpr_delete_source_cb, host_group,
				    BV_CALL_SET) < 0)
		return;			/* Out of memory */
	}

	/*
	 * If the list is now empty, delete the group.  No need for a
	 * group notification, since we've sent source notifications.
	 * This also catches the case where we've received a redundant
	 * BLOCK after the group has been deleted (we would have
	 * created an empty group above.)
	 */
	if (gmp_addr_list_empty(&host_group->rhgroup_addrs)) {
	    delete_group = TRUE;
	}

	break;

      default:
	gmpx_assert(FALSE);
    }

    /* See if we're deleting the group. */

    if (delete_group) {

	/*
	 * Deleting the group.  Delink the host group from the main group,
	 * which makes it inactive.
	 */
	gmpr_delink_host_group(host_group);

	/*
	 * If we're doing fast leaves, and there are no host groups
	 * associated with this group any longer, trigger a deletion
	 * of the main group.
	 */
	if (intf->rintf_fast_leaves &&
	    thread_circular_thread_empty(&group->rgroup_host_group_head)) {
	    gmpr_last_host_group_ref_gone(group);
	}

    } else {

	/*
	 * The group is still alive.  Start the group timer if there
	 * are no sources, or stop the group timer if there are
	 * sources.
	 */
	if (gmp_addr_list_empty(&host_group->rhgroup_addrs)) {
	    gmpx_start_timer(host_group->rhgroup_timer,
			     intf->rintf_group_membership_ivl, 0);
	} else {
	    gmpx_stop_timer(host_group->rhgroup_timer);
	}
    }

    /* If we're supposed to do a group notification, do so. */

    if (notify_group)
	gmpr_host_group_notify_clients(host_group);

    /* Try to free it (we probably can't just yet, but maybe.) */

    gmpr_attempt_host_group_free(host_group);
}


/*
 * gmpr_host_group_expiry
 *
 * Process a host group timer expiration.
 */
static void
gmpr_host_group_expiry (gmpx_timer *timer, void *context)
{
    gmpr_instance *instance;
    gmpr_host_group *host_group;

    host_group = context;
    instance = host_group->rhgroup_host->rhost_intf->rintf_instance;

    /* Stop the timer. */

    gmpx_stop_timer(timer);

    /* There better not be any sources. */

    gmpx_assert(gmp_addr_list_empty(&host_group->rhgroup_addrs));

    /* Delink the host group from the main group (making it inactive). */

    gmpr_delink_host_group(host_group);

    /* Notify the clients. */

    gmpr_host_group_notify_clients(host_group);

    /* Try to free the host group.  Probably won't work, but it might. */

    gmpr_attempt_host_group_free(host_group);

    /* Alert the clients. */

    gmpr_alert_host_clients(instance);
}


/*
 * gmpr_host_source_expiry
 *
 * Process a host source address timer expiration.
 */
static void
gmpr_host_source_expiry (gmpx_timer *timer, void *context)
{
    gmpr_instance *instance;
    gmpr_host *host;
    gmpr_host_group *host_group;
    gmpr_host_group_addr *hg_addr;

    hg_addr = context;
    host_group = hg_addr->rhga_host_group;
    host = host_group->rhgroup_host;
    instance = host->rhost_intf->rintf_instance;

    /* Stop the timer. */

    gmpx_stop_timer(timer);

    /*
     * Lock the host group, so that it won't be released out from
     * under us as a side effect of deleting the source.
     */
    gmpr_lock_host_group(host_group);

    /* Mark the source as deleted. */

    gmpr_delete_host_source(hg_addr);

    /* Try to free the host group (we probably can't just yet, but maybe.) */

    gmpr_attempt_host_group_free(host_group);

    /* Undo the lock.  This might free the host group. */

    gmpr_unlock_host_group(host_group);

    /* Alert the clients. */

    gmpr_alert_host_clients(instance);
}


/*
 * gmpr_host_notify_oif_map_change
 *
 * An oif-map changed on a group, so notify the affected hosts. Add either
 * the (*,g) or all the (s,g) entries to the host notification thread for
 * each client.
 */
void
gmpr_host_notify_oif_map_change(gmpr_group *group)
{
    thread *group_thread_ptr;
    gmpr_host_group *host_group;
    gmp_addr_list_entry *addr_entry;
    gmpr_host_group_addr *host_group_addr;

    /* Walk the host group thread to create the notifications. */

    group_thread_ptr = NULL;
    while (TRUE) {
	group_thread_ptr = thread_circular_thread_next(
			&group->rgroup_host_group_head, group_thread_ptr);
	host_group = gmpr_thread_to_host_group(group_thread_ptr);
	if (!host_group)
	    break;

	/*
	 * Found a host group. If there are no sources, enqueue the group. If
	 * there are sources, enqueue the list of sources (and not the group).
	 */
	if (gmp_addr_list_empty(&host_group->rhgroup_addrs)) {
	    gmpr_host_group_notify_clients(host_group);
	} else {
	    addr_entry = NULL;
	    while (TRUE) {
		addr_entry = gmp_addr_list_next_entry(
				&host_group->rhgroup_addrs, addr_entry);
		host_group_addr = gmpr_addr_entry_to_host_group_entry(
				addr_entry);
		if (!host_group_addr)
		    break;

		/*
		 * Found a host group address. Enqueue the source.
		 */
		gmpr_host_source_notify_clients(host_group_addr);
	    }
	}
    }
}
