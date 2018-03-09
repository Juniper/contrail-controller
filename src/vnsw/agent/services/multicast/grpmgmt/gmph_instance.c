/* $Id: gmph_instance.c 346474 2009-11-14 10:18:58Z ssiano $
 *
 * gmph_instance.c - GMP host-side instance support
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_host.h"
#include "gmp_private.h"
#include "gmph_private.h"

thread gmph_global_instance_thread;	/* Thread of instances */


/*
 * gmph_inst_master_clock_expiry
 *
 * Called when the master clock expires.  This will happen once every
 * 49 days.  We just restart it.
 */
static void
gmph_inst_master_clock_expiry (gmpx_timer *timer, void *context GMPX_UNUSED)
{
    gmpx_start_timer(timer, 0xffffffff, 0);
}
    

/*
 * gmph_instance_create
 *
 * Create a host-side GMP instance.
 *
 * Returns a pointer to the instance, or NULL if we're out of memory.
 */
gmph_instance *
gmph_instance_create (gmp_proto proto, void *inst_context)
{
    gmph_instance *instance;
    boolean first_instance;

    /* Note if this is the first instance. */

    first_instance =
	thread_circular_thread_empty(&gmph_global_instance_thread);

    /* Allocate the block. */

    instance = gmpx_malloc_block(gmph_instance_tag);

    if (instance) {			/* Got one */

	/* Set the external context value. */

	instance->hinst_context = inst_context;

	/* Initialize the patricia tree. */

	instance->hinst_intfs = 
	    gmpx_patroot_init(sizeof(gmpx_intf_id),
			      GMPX_PATRICIA_OFFSET(gmph_intf,
						   hintf_inst_patnode,
						   hintf_id));
	if (!instance->hinst_intfs) { /* No memory */
	    gmpx_free_block(gmph_instance_tag, instance);
	    return NULL;
	}

	/* Set the magic number and link it into the list. */

	instance->hinst_magic = GMPH_INSTANCE_MAGIC;
	thread_circular_add_top(&gmph_global_instance_thread,
				&instance->hinst_thread);

	thread_new_circular_thread(&instance->hinst_client_thread);

	/* Set the protocol type. */

	instance->hinst_proto = proto;

	/* Set the address length. */

	switch (proto) {
	  case GMP_PROTO_IGMP:
	    instance->hinst_addrlen = IPV4_ADDR_LEN;
	    break;

	  case GMP_PROTO_MLD:
	    instance->hinst_addrlen = IPV6_ADDR_LEN;
	    break;

	  default:
	    gmpx_assert(FALSE);		/* Invalid protocol type! */
	}

	/* Initialize the address catalog. */

	if (gmp_init_addr_catalog(&instance->hinst_addr_cat,
				  instance->hinst_addrlen) < 0) {
	    return NULL;		/* Out of memory */
	}

	/* Create and start the master timer. */

	instance->hinst_master_clock =
	    gmpx_create_timer(instance->hinst_context, "GMP host master clock",
			      gmph_inst_master_clock_expiry, instance);
	gmpx_start_timer(instance->hinst_master_clock, 0xffffffff, 0);

	/* If this is the first instance, register with the packet handler. */

	if (first_instance)
	    gmph_register_packet_handler();

	/* Register the protocol. */

	gmpp_enab_disab_proto(GMP_ROLE_HOST, proto, TRUE);
    }

    return instance;
}


/*
 * gmph_get_instance
 * 
 * Return an instance pointer, given an instance ID.
 *
 * Verifies that the instance ID is valid.
 */
gmph_instance *
gmph_get_instance (gmp_instance_id instance_id)
{
    gmph_instance *instance;

    /* Do the (trivial) conversion. */

    instance = instance_id;

    /* Verify the magic number. */

    gmpx_assert(instance->hinst_magic == GMPH_INSTANCE_MAGIC);

    return instance;
}


/*
 * gmph_instance_destroy
 *
 * Destroy an instance and all things associated with it.
 */
void
gmph_instance_destroy (gmph_instance *instance)
{
    gmp_proto inst_proto;
    thread *thread_ptr;
    boolean found_proto;

    /* Blast all clients on the instance. */

    gmph_destroy_instance_clients(instance);

    /* Blast all interfaces on the instance. */

    gmph_destroy_instance_intfs(instance);

    inst_proto = instance->hinst_proto;	/* Remember for a bit */

    /* Make some paranoia checks. */

    gmpx_assert(thread_circular_thread_empty(&instance->hinst_client_thread));
    gmpx_assert(!gmpx_patricia_lookup_least(instance->hinst_intfs));

    /* Destroy the address catalog. */

    gmp_destroy_addr_catalog(&instance->hinst_addr_cat);

    /* Toss the interface tree. */

    gmpx_patroot_destroy(instance->hinst_intfs);

    /* Toss the timer. */

    gmpx_destroy_timer(instance->hinst_master_clock);

    /* Delink the block and free it. */

    thread_remove(&instance->hinst_thread);
    gmpx_free_block(gmph_instance_tag, instance);

    /* If this was the last instance for a protocol, disable it. */

    found_proto = FALSE;
    FOR_ALL_CIRCULAR_THREAD_ENTRIES(&gmph_global_instance_thread, thread_ptr) {
	instance = gmph_thread_to_instance(thread_ptr);
	if (instance->hinst_proto == inst_proto) {
	    found_proto = TRUE;
	    break;
	}
    }
    if (!found_proto)
	gmpp_enab_disab_proto(GMP_ROLE_HOST, inst_proto, FALSE);
	
    /* If that was the last instance, deregister with the packet handler. */

    if (thread_circular_thread_empty(&gmph_global_instance_thread))
	gmpp_deregister(GMP_ROLE_HOST);
}
