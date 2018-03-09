/* $Id: gmp_host.c 362048 2010-02-09 00:25:11Z builder $
 *
 * gmp_host.c - IGMP/MLD Host-Side Support
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 * This file contains the top-level routines for host-side support for GMP.
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_host.h"
#include "gmp_externs.h"
#include "gmp_private.h"
#include "gmph_private.h"

/*
 * Global storage.  There should be very little here.
*/
gmpx_block_tag gmph_instance_tag;
gmpx_block_tag gmph_client_tag;
gmpx_block_tag gmph_intf_tag;
gmpx_block_tag gmph_group_tag;
gmpx_block_tag gmph_group_rpt_entry_tag;
gmpx_block_tag gmph_client_group_tag;
gmpx_block_tag gmph_client_group_thread_tag;

static boolean gh_initialized;		/* TRUE if we've initialized */


/*
 * gmph_init
 *
 * Initialize GMP Host code
 *
 * Called when the first instance is created.
 *
 * Returns 0 if all OK, or -1 if out of memory.
 */
static int
gmph_init (void)
{
    gmp_proto proto;

    /* Initialize the interface trees. */

    for (proto = 0; proto < GMP_NUM_PROTOS; proto++) {
	gmph_global_intf_tree[proto] =
	    gmpx_patroot_init(sizeof(gmpx_intf_id),
			      GMPX_PATRICIA_OFFSET(gmph_intf,
						   hintf_global_patnode,
						   hintf_id));
	if (!gmph_global_intf_tree[proto])
	    return -1;			/* Out of memory. */
    }

    /* Do common initialization. */

    gmp_common_init();

    /* Create the instance thread. */

    thread_new_circular_thread(&gmph_global_instance_thread);

    /* Create memory blocks. */

    gmph_instance_tag = gmpx_malloc_block_create(sizeof(gmph_instance),
						 "GMP host instance");
    gmph_client_tag = gmpx_malloc_block_create(sizeof(gmph_client),
					       "GMP host client");
    gmph_intf_tag = gmpx_malloc_block_create(sizeof(gmph_intf),
					     "GMP host intf");
    gmph_group_tag = gmpx_malloc_block_create(sizeof(gmph_group),
					      "GMP host group");
    gmph_group_rpt_entry_tag =
	gmpx_malloc_block_create(sizeof(gmph_rpt_msg_addr_entry),
				 "GMP host message address entry");
    gmph_client_group_tag =
	gmpx_malloc_block_create(sizeof(gmph_client_group),
				 "GMP client group");

    return 0;
}


/*
 * gmph_create_instance
 *
 * Create a host-side GMP instance.
 *
 * Returns an instance ID (really a pointer), or zero if out of memory.
 */
gmp_instance_id
gmph_create_instance (gmp_proto proto, void *context)
{
    gmph_instance *instance;

    /* If we're not initialized yet, do it now. */

    if (!gh_initialized) {
	if (gmph_init() < 0)
	    return NULL;		/* Out of memory. */
    }

    gh_initialized = TRUE;

    /* Create the instance. */

    instance = gmph_instance_create(proto, context);

    return instance;
}


/*
 * gmph_destroy_instance
 *
 * Destroy an instance and all things associated with it.
 */
void
gmph_destroy_instance (gmp_instance_id instance_id)
{
    gmph_instance *instance;

    /* Get the instance block. */

    instance = gmph_get_instance(instance_id);

    /* Destroy it. */

    gmph_instance_destroy(instance);
}


/*
 * gmp_register
 *
 * Register a host-side client with GMP.
 *
 * Returns a client ID for the client to identify itself with, or 0 if
 * we're out of memory.
 */
gmp_client_id
gmph_register (gmp_instance_id instance_id)
{
    gmph_client *client;
    gmph_instance *instance;

    /* Get the instance. */

    instance = gmph_get_instance(instance_id);

    /* Create a client. */

    client = gmph_create_client(instance);

    return client;
}


/*
 * gmp_detach
 *
 * A client is going away.  Clean up.
 */
void
gmph_detach (gmp_client_id client_id)
{
    gmph_client *client;

    /* Get the client. */

    client = gmph_get_client(client_id);

    /* Get rid of it. */

    gmph_destroy_client(client);
}


/*
 * gmph_set_intf_version
 *
 * Set the maximum version number on an interface.
 *
 * Returns 0 if OK, -1 if out of memory, or 1 if the interface hasn't
 * been attached.
 */
int
gmph_set_intf_version (gmp_instance_id instance_id, gmpx_intf_id intf_id,
		       u_int version)
{
    gmph_instance *instance;
    gmph_intf *intf;
    gmp_version internal_version;

    /* Get the instance. */

    instance = gmph_get_instance(instance_id);

    /* Translate the version number. */

    if (!version) {
	internal_version = GMP_VERSION_SOURCES;
    } else {
	internal_version = gmp_translate_version(instance->hinst_proto,
						 version);
	gmpx_assert(internal_version != GMP_VERSION_INVALID);
    }

    /* Get the interface.  */

    intf = gmph_intf_lookup(instance, intf_id);
    if (!intf)
	return 1;			/* No interface. */

    intf->hintf_cfg_ver = internal_version;

    /* Do any necessary processing. */

    gmph_intf_evaluate_version(intf);

    return 0;
}


/*
 * gmph_attach_intf
 *
 * Bind to an interface, in preparation for later interest.
 *
 * Returns 0 if OK, -1 if out of memory, or 1 if the interface already
 * is bound.
 */
int
gmph_attach_intf (gmp_instance_id instance_id, gmpx_intf_id intf_id)
{
    gmph_instance *instance;

    /* Go do the work. */

    instance = gmph_get_instance(instance_id);
    return gmph_attach_intf_internal(instance, intf_id);
}


/*
 * gmph_detach_intf
 *
 * Unbind an interface.  This flushes all previous listen requests
 * on the interface.
 *
 * Returns 0 if all OK, or 1 if the interface doesn't exist.
 */
int
gmph_detach_intf (gmp_instance_id instance_id, gmpx_intf_id intf_id)
{
    gmph_instance *instance;

    /* Go do the work. */

    instance = gmph_get_instance(instance_id);
    return gmph_detach_intf_internal(instance, intf_id, NULL, NULL);
}


/*
 * gmph_detach_intf_soft
 *
 * Softly unbind an interface.  This causes GMP to wait until all
 * pending packets are sent before deleting the interface, and
 * calls the provided callback routine when all of the packets have
 * been transmitted.
 *
 * If the caller wishes to clean up state, it must call gmph_listen()
 * or gmph_leave_all_groups() to send Leaves on all groups before
 * calling this routine.  The code will dutifully wait for all of the
 * Leaves to go out before unbinding.
 *
 * On the other hand, if the caller wishes to leave state, it can simply call
 * this routine, which will wait for any pending Joins (or any other activity)
 * to quiesce before shutting down.
 *
 * Returns 0 if all OK, or 1 if the interface doesn't exist.
 */
int
gmph_detach_intf_soft (gmp_instance_id instance_id, gmpx_intf_id intf_id,
		       gmph_soft_detach_callback callback, void *context)
{
    gmph_instance *instance;

    /* Go do the work. */

    gmpx_assert(callback);
    instance = gmph_get_instance(instance_id);
    return gmph_detach_intf_internal(instance, intf_id, callback, context);
}


/*
 * gmph_listen
 *
 * Listen for multicast traffic for a particular (S,G) or (*,G) on an
 * interface.
 *
 * "addr_thread" is NULL if this is a (*,G) join.
 *
 * "addr_thread" is NULL and the filter mode is INCLUDE if this is a leave.
 *
 * "addr_thread" is a pointer to an address_thread of sources if this is a
 * (S,G) join, or NULL if this is a (*,G) join or leave.
 *
 * The sources and filter mode override any existing request for this
 * group from this client.
 *
 * Returns 0 if all is well, -1 if we ran out of memory, or 1 if the
 * interface hasn't been attached.
 */
int
gmph_listen (gmp_client_id client_id, gmpx_intf_id intf_id,
	     const u_int8_t *group_addr, gmp_filter_mode filter_mode,
	     gmp_addr_thread *addr_thread)
{
    gmph_instance *instance;
    gmph_client *client;
    gmph_client_group *old_clnt_group;
    gmph_client_group *new_clnt_group;
    gmph_group *group;
    gmph_intf *intf;

    /* Get the client. */

    client = gmph_get_client(client_id);

    /* Get the instance. */

    instance = client->hclient_instance;

    /* Get the interface.  Bail if it's not there. */

    intf = gmph_intf_lookup(instance, intf_id);
    if (!intf)
	return 1;			/* No interface. */

    /*
     * Look up any existing client group entry.  If it's there, delete it.
     * We suppress doing the group reevaluation, since we're going to do
     * it below, and we don't want to touch it until the client state is
     * replaced.
     */
    old_clnt_group = gmph_lookup_client_group(client, intf_id, group_addr);
    if (old_clnt_group) {
	gmph_destroy_client_group(old_clnt_group, FALSE);
	old_clnt_group = NULL;
    }

    /* Get the interface group entry, or create it if there isn't one yet. */

    group = gmph_group_lookup_create(intf, group_addr);
    if (!group)
	return -1;			/* No memory */

    /*
     * If the new request isn't a Leave, create a client group entry
     * and link it.
     */
    if (filter_mode == GMP_FILTER_MODE_EXCLUDE || addr_thread) {

	/* Not a Leave. */

	new_clnt_group = 
	    gmph_create_client_group(intf, client, group, group_addr,
				     filter_mode, addr_thread);
	if (!new_clnt_group)
	    return -1;			/* No memory */
    }

    /* Now reevaluate the changed group. */

    if (gmph_reevaluate_group(group) < 0)
	return -1;			/* No memory */

    return 0;
}


/*
 * gmph_leave_all_groups
 *
 * Stops listening to all previously joined groups (via
 * gmph_listen()).  This is handy for cleaning up prior to calling
 * gmph_detach_intf_soft().
 *
 * Returns 0 if all is well, -1 if we ran out of memory, or 1 if the
 * interface hasn't been attached.
 */
int
gmph_leave_all_groups (gmp_client_id client_id, gmpx_intf_id intf_id)
{
    gmph_instance *instance;
    gmph_client *client;
    gmph_intf *intf;

    /* Get the client. */

    client = gmph_get_client(client_id);

    /* Get the instance. */

    instance = client->hclient_instance;

    /* Get the interface.  Bail if it's not there. */

    intf = gmph_intf_lookup(instance, intf_id);
    if (!intf)
	return 1;			/* No interface. */

    /* Got everything.  Do the deed. */

    gmph_destroy_intf_client_groups(client, intf);

    return 0;
}


/*
 * gmph_send_intf_groups
 *
 * Send all group information for an interface.  This is equivalent to
 * the receipt of a general query on the interface, and can be used to
 * rapidly push state out to any routers thereon.
 */
void
gmph_send_intf_groups (gmp_instance_id instance_id, gmpx_intf_id intf_id)
{
    gmph_instance *instance;
    gmph_intf *intf;

    /* Get the instance. */

    instance = gmph_get_instance(instance_id);

    /* Get the interface.  Bail if it's not there. */

    intf = gmph_intf_lookup(instance, intf_id);
    if (!intf)
	return;				/* No interface. */

    /* Make the general query timer expire immediately to do the work. */

    gmph_start_general_query_timer(intf, 0, 0);
}


/*
 * gmph_set_intf_passive
 *
 * Set or clear passive status on an interface.  A passive interface never
 * sends report messages under any circumstances.
 *
 * When switching from active to passive, nothing gets cleaned up, but
 * gmph_xmit_callback will discard any packets it tries to build (and going
 * forward we won't try to build any.)
 *
 * Nothing is sent when switching from passive to active.  The application
 * will need to call gmph_send_intf_groups() above, or wait for a query to
 * arrive.
 */
void
gmph_set_intf_passive (gmp_instance_id instance_id, gmpx_intf_id intf_id,
		       boolean passive)
{
    gmph_instance *instance;
    gmph_intf *intf;

    /* Get the instance. */

    instance = gmph_get_instance(instance_id);

    /* Get the interface.  Bail if it's not there. */

    intf = gmph_intf_lookup(instance, intf_id);
    if (!intf)
	return;				/* No interface. */

    /* Set the passivity state. */

    intf->hintf_passive = passive;
}


/*
 * gmph_intf_has_channel
 *
 * Returns TRUE if the specified (S,G) has been requested on the
 * interface, or FALSE if not.  If the source pointer is NULL, we test
 * for (*,G).
 *
 * If "exact" is TRUE, we return TRUE only if the channel as specified is
 * to be forwarded.  If FALSE, we return TRUE if *any* (S,G) is being
 * forwarded when a (*,G) request is made.
 */
boolean
gmph_intf_has_channel (gmp_instance_id instance_id, gmpx_intf_id intf_id,
		       const u_int8_t *source_addr, const u_int8_t *group_addr,
		       boolean exact)
{
    gmph_instance *instance;
    gmph_intf *intf;
    gmph_group *group;

    /* Get the instance and interface. */

    instance = gmph_get_instance(instance_id);
    intf = gmph_intf_lookup(instance, intf_id);
    if (!intf)
	return FALSE;

    /* Get the group. */

    group = gmph_group_lookup(intf, group_addr);
    if (!group)
	return FALSE;

    /* If the group isn't active, it doesn't match. */

    if (!gmph_group_is_active(group))
	return FALSE;

    /* If the group is a (*,G) join, we match anything. */

    if (group->hgroup_filter_mode == GMP_FILTER_MODE_EXCLUDE &&
	gmp_addr_list_empty(&group->hgroup_src_addr_list)) {
	return TRUE;
    }

    /* If we're doing an inexact (*,G) match, we have a match. */

    if (!exact && !source_addr)
	return TRUE;

    /*
     * If we're doing a (*,G) test (and exact mode is requested), we
     * didn't match.
     */
    if (!source_addr)
	return FALSE;

    /* See if the source address is in the group. */

    return gmph_group_source_requested(group, source_addr);
}
