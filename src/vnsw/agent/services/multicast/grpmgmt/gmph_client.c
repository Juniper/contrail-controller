/* $Id: gmph_client.c 362048 2010-02-09 00:25:11Z builder $
 *
 * gmph_client.c - IGMP/MLD Host-Side Client Routines
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 * This module defines the support for host-side clients, which are,
 * roughly speaking, sockets requesting multicast services.
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_host.h"
#include "gmp_private.h"
#include "gmph_private.h"



/*
 * gmph_get_client
 * 
 * Return an client pointer, given a client ID.
 *
 * Verifies that the client ID is valid.
 */
gmph_client *
gmph_get_client (gmp_client_id client_id)
{
    gmph_client *client;

    /* Do the (trivial) conversion. */

    client = client_id;

    /* Verify the magic number. */

    gmpx_assert(client->hclient_magic == GMPH_CLIENT_MAGIC);

    return client;
}


/*
 * gmph_create_client
 *
 * Create a client entry.
 *
 * Returns a pointer to the client entry, or NULL if no memory.
 */
gmph_client *
gmph_create_client (gmph_instance *instance)
{
    gmph_client *client;

    /* Allocate a client block. */

    client = gmpx_malloc_block(gmph_client_tag);
    if (!client)			/* No memory */
	return NULL;

    /* Got one.  Set up the patricia root. */

    client->hclient_group_root =
	gmpx_patroot_init(gmph_client_group_key_len(instance),
			  GMPX_PATRICIA_OFFSET(gmph_client_group,
					       client_group_node,
					       client_group_key));

    /* If there's no memory, clean up and bail. */

    if (!client->hclient_group_root) {
	gmpx_free_block(gmph_client_tag, client);
	return NULL;
    }
					       
    /* Link the client into the instance. */

    client->hclient_magic = GMPH_CLIENT_MAGIC;
    thread_circular_add_top(&instance->hinst_client_thread,
			    &client->hclient_thread);
    client->hclient_instance = instance;

    return client;
}


/*
 * gmph_destroy_client
 *
 * Destroy a client entry.  Destroys any client groups still bound to the
 * client, though the groups themselves live on (possibly attempting to
 * send Leaves if no other clients are interested in the groups.)
 */
void
gmph_destroy_client (gmph_client *client)
{
    gmph_instance *instance;
    gmpx_patnode *node;
    gmph_client_group *client_group;

    instance = client->hclient_instance;

    /* Flush the tree. */

    while (TRUE) {
	node = gmpx_patricia_lookup_least(client->hclient_group_root);
	client_group = gmph_patnode_to_client_group(node);
	if (!client_group)
	    break;
	gmph_destroy_client_group(client_group, TRUE);
    }

    /* Get rid of the tree. */

    gmpx_assert(!gmpx_patricia_lookup_least(client->hclient_group_root));
    gmpx_patroot_destroy(client->hclient_group_root);
    client->hclient_group_root = NULL;

    /* Delink the block and free it. */

    thread_remove(&client->hclient_thread);
    client->hclient_instance = NULL;
    gmpx_free_block(gmph_client_tag, client);
}


/*
 * gmph_destroy_instance_clients
 *
 * Destroy all clients on an instance.  We assume that all of the client
 * groups have already been destroyed.
 */
void
gmph_destroy_instance_clients (gmph_instance *instance)
{
    thread *thread_ptr;
    gmph_client *client;

    /* Walk all clients on the instance. */

    while (TRUE) {
	thread_ptr = thread_circular_top(&instance->hinst_client_thread);
	client = gmph_thread_to_client(thread_ptr);
	if (!thread_ptr)
	    break;

	/* Destroy the client. */

	gmph_destroy_client(client);
    }
}


/*
 * gmph_destroy_client_group
 *
 * Destroy a client group entry.  Tolerant of unlinked entries, to make it
 * easier to back out during memory shortages.
 *
 * reevaluate_group is TRUE if we should reevaluate our group state.
 */
void
gmph_destroy_client_group (gmph_client_group *client_group,
			   boolean reevaluate_group)
{
    gmph_client *client;
    gmph_group *group;

    /* Delink it. */

    group = client_group->client_group_group;
    client = client_group->client_group_client;
    gmpx_patricia_delete(client->hclient_group_root,
			 &client_group->client_group_node);
    thread_remove(&client_group->client_group_thread);

    /* Clean it out. */

    gmp_addr_vect_clean(&client_group->client_addr_vect);

    /* Free the block. */

    gmpx_free_block(gmph_client_group_tag, client_group);

    /* Reevaluate the group if appropriate. */

    if (reevaluate_group)
	gmph_reevaluate_group(group);

    /* Try to free the group. */

    gmph_attempt_group_free(group);
}


/*
 * gmph_destroy_group_client_groups
 *
 * Destroy all client groups associated with a group.
 */
void
gmph_destroy_group_client_groups (gmph_group *group)
{
    thread *thread_ptr;
    gmph_client_group *client_group;

    /* Walk all client groups associated with the group. */

    while (TRUE) {
	thread_ptr = thread_circular_top(&group->hgroup_client_thread);
	client_group = gmph_thread_to_client_group(thread_ptr);
	if (!client_group)
	    break;

	/* Got the client group.  Destroy it. */

	gmph_destroy_client_group(client_group, TRUE);
    }
}


/*
 * gmph_destroy_intf_client_groups
 *
 * Destroy all client groups associated with a client on an interface.
 */
void
gmph_destroy_intf_client_groups (gmph_client *client, gmph_intf *intf)
{
    gmph_instance *instance;
    gmph_client_group *client_group;
    gmpx_patnode *node;

    gmph_client_group_key key;

    instance = client->hclient_instance;

    /* Set up the key. */

    memset(&key, 0, sizeof(key));
    key.group_key_intf_id = intf->hintf_id;

    /* Loop until we run out of client groups on this interface. */

    while (TRUE) {

	/* Look up the earliest entry. */

	node = gmpx_patricia_lookup_geq(client->hclient_group_root, &key);
	client_group = gmph_patnode_to_client_group(node);

	/* If there's nothing left, bail. */

	if (!client_group)
	    break;

	/* If there's nothing left for this interface, bail. */

	if (client_group->client_group_intf_id != intf->hintf_id)
	    break;

	/* Got a live one.  Destroy it. */

	gmph_destroy_client_group(client_group, TRUE);
    }
}


/*
 * gmph_create_client_group
 *
 * Create a client group entry based on the passed data.
 *
 * Returns a pointer to the client group entry, or NULL if no memory.
 */
gmph_client_group *
gmph_create_client_group (gmph_intf *intf, gmph_client *client,
			  gmph_group *group, const u_int8_t *group_addr,
			  gmp_filter_mode filter_mode,
			  gmp_addr_thread *addr_thread)
{
    gmph_client_group *client_group;

    gmph_instance *instance;

    instance = client->hclient_instance;

    /* Create the group block. */

    client_group = gmpx_malloc_block(gmph_client_group_tag);
    if (!client_group)
	return NULL;			/* No memory */

    /* Initialize the address vector. */

    gmp_init_addr_vector(&client_group->client_addr_vect,
			 &instance->hinst_addr_cat);

    /* Create a source list if sources are present. */

    if (addr_thread) {

	if (gmp_addr_vect_fill(&client_group->client_addr_vect,
			       addr_thread) < 0) {

	    /* Out of memory. */

	    gmpx_free_block(gmph_client_group_tag, client_group);
	    return NULL;
	}
    }

    /* Fill in the other fields. */

    client_group->client_group_intf_id = intf->hintf_id;
    client_group->client_group_group = group;
    client_group->client_filter_mode = filter_mode;
    memmove(client_group->client_group_addr.gmp_addr, group_addr, instance->hinst_addrlen);

    /* Link the group entry into the client tree. */

    client_group->client_group_client = client;
    gmpx_assert(gmpx_patricia_add(client->hclient_group_root,
				  &client_group->client_group_node));

    /* Thread the client group entry onto the interface group entry. */

    thread_circular_add_top(&group->hgroup_client_thread,
			    &client_group->client_group_thread);

    return client_group;
}


/*
 * gmph_lookup_client_group
 *
 * Look up a client group entry based on the client, interface, and group
 * address.
 *
 * Returns a pointer to the client group entry, or NULL if it's not there.
 */
gmph_client_group *
gmph_lookup_client_group (gmph_client *client, gmpx_intf_id intf_id,
			  const u_int8_t *group)
{
    gmph_instance *instance;
    gmph_client_group *client_group;
    gmpx_patnode *node;

    gmph_client_group_key key;

    instance = client->hclient_instance;

    /* Set up the key. */

    memset(&key, 0, sizeof(key));
    key.group_key_intf_id = intf_id;
    memmove(key.group_key_addr.gmp_addr, group, instance->hinst_addrlen);

    /* Look up the entry. */

    node = gmpx_patricia_lookup(client->hclient_group_root, &key);
    client_group = gmph_patnode_to_client_group(node);

    return client_group;
}
