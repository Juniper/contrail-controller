/* $Id: gmpr_instance.c 346474 2009-11-14 10:18:58Z ssiano $
 *
 * gmpr_instance.c - GMP Router-side instance support
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */
#include "gmpx_basic_types.h"
#include "gmp.h"
#include "gmpx_environment.h"
#include "gmp_private.h"
#include "gmp_router.h"
#include "gmpr_private.h"
#include "igmp_protocol.h"
#include "mld_proto.h"

thread gmpr_global_instance_thread;	/* Thread of instances */


/*
 * gmpr_query_smear_expiry
 * 
 * The query timer smear timer has expired.  Smear the query timers and
 * restart the smear timer.
 */
static void
gmpr_query_smear_expiry (gmpx_timer *timer, void *context)
{
    gmpr_instance *instance;

    instance = context;

    /* Smear the timers. */

    gmpx_smear_timer_group(instance->rinst_proto, GMP_TIMER_GROUP_GEN_QUERY);

    /* Restart the timer. */

    gmpx_start_timer(timer, GMP_QUERY_SMEAR_IVL, 0);

    /* We're no longer accelerated. */

    instance->rinst_smear_timer_accelerated = FALSE;
}


/*
 * gmpr_accelerate_query_smear
 *
 * Schedule a smearing of general query timers soon, if this has not already
 * been done recently.
 */
void
gmpr_accelerate_query_smear (gmpr_instance *instance)
{
#if 0
    /* Do it if it's not already scheduled. */

    if (!instance->rinst_smear_timer_accelerated) {
	gmpx_start_timer(instance->rinst_smear_timer,
			 GMP_QUERY_QUICK_SMEAR_IVL, 0);
	instance->rinst_smear_timer_accelerated = TRUE;
    }
#endif
}


/*
 * gmpr_instance_create
 *
 * Create a router-side GMP instance.
 *
 * Returns a pointer to the instance, or NULL if we're out of memory.
 */
gmpr_instance *
gmpr_instance_create (gmp_proto proto, void *inst_context)
{
    gmpr_instance *instance;
    boolean first_instance;

    /* Note if this is the first instance. */

    first_instance =
	thread_circular_thread_empty(&gmpr_global_instance_thread);

    /* Allocate the block. */

    instance = gmpx_malloc_block(gmpr_instance_tag);

    if (instance) {			/* Got one */

	/* Save the context. */

	instance->rinst_context = inst_context;

	/* Set the protocol type. */

	instance->rinst_proto = proto;

	/* Set the address length and minimum max resp field value. */

	switch (proto) {
	  case GMP_PROTO_IGMP:
	    instance->rinst_addrlen = IPV4_ADDR_LEN;
	    instance->rinst_min_max_resp = IGMP_MAX_RESP_MSEC;
	    break;

	  case GMP_PROTO_MLD:
	    instance->rinst_addrlen = IPV6_ADDR_LEN;
	    instance->rinst_min_max_resp = MLD_MAX_RESP_MSEC;
	    break;

	  default:
	    gmpx_assert(FALSE);		/* Invalid protocol type! */
	}

	/* Initialize the patricia trees. */

	instance->rinst_intfs = 
	    gmpx_patroot_init(sizeof(gmpx_intf_id),
			      GMPX_PATRICIA_OFFSET(gmpr_intf,
						   rintf_inst_patnode,
						   rintf_id));
	if (!instance->rinst_intfs) { /* No memory */
	    gmpx_free_block(gmpr_instance_tag, instance);
	    return NULL;
	}
	instance->rinst_global_state_root =
	    gmpx_patroot_init(instance->rinst_addrlen,
			      GMPX_PATRICIA_OFFSET(gmpr_global_group,
						   global_group_node,
						   global_group_addr));
	if (!instance->rinst_global_state_root) { /* No memory */
	    gmpx_patroot_destroy(instance->rinst_intfs);
	    gmpx_free_block(gmpr_instance_tag, instance);
	    return NULL;
	}

	/* Set the magic number and link it into the list. */

	instance->rinst_magic = GMPR_INSTANCE_MAGIC;
	thread_circular_add_top(&gmpr_global_instance_thread,
				&instance->rinst_thread);

	/* Initialize thread heads. */

	thread_new_circular_thread(&instance->rinst_client_thread);
	thread_new_circular_thread(&instance->rinst_startup_intf_thread);

	/* Create the client ordinal context. */

	instance->rinst_ord_handle = ord_create_context(ORD_COMPACT);
	if (!instance->rinst_ord_handle)
	    return NULL;		/* Out of memory */

	/* Initialize the address catalog. */

	if (gmp_init_addr_catalog(&instance->rinst_addr_cat,
				  instance->rinst_addrlen) < 0) {
	    return NULL;		/* Out of memory */
	}

	/* Create and start the smear timer. */

	instance->rinst_smear_timer =
	    gmpx_create_timer(instance->rinst_context, "GMP query smear timer",
			      gmpr_query_smear_expiry, instance);
	gmpr_accelerate_query_smear(instance);

	/* If this is the first instance, register with the packet handler. */

	if (first_instance)
	    gmpr_register_packet_handler();

	/* Register the protocol. */

	gmpp_enab_disab_proto(GMP_ROLE_ROUTER, proto, TRUE);
    }

    return instance;
}


/*
 * gmpr_get_instance
 * 
 * Return an instance pointer, given an instance ID.
 *
 * Verifies that the instance ID is valid.
 */
gmpr_instance *
gmpr_get_instance (gmp_instance_id instance_id)
{
    gmpr_instance *instance;

    /* Do the (trivial) conversion. */

    instance = instance_id;

    /* Verify the magic number. */

    gmpx_assert(instance->rinst_magic == GMPR_INSTANCE_MAGIC);

    return instance;
}


/*
 * gmpr_instance_destroy
 *
 * Destroy an instance and all things associated with it.
 */
void
gmpr_instance_destroy (gmpr_instance *instance)
{
    gmp_proto inst_proto;
    thread *thread_ptr;
    boolean found_proto;

    /* Blast all clients on the instance. */

    gmpr_destroy_instance_clients(instance);

    /* Blast all interfaces on the instance. */

    gmpr_destroy_instance_intfs(instance);

    inst_proto = instance->rinst_proto;	/* Remember for a bit */

    /* Make some paranoia checks. */

    gmpx_assert(thread_circular_thread_empty(&instance->rinst_client_thread));
    gmpx_assert(
	thread_circular_thread_empty(&instance->rinst_startup_intf_thread));
    gmpx_assert(!gmpx_patricia_lookup_least(instance->rinst_intfs));
    gmpx_assert(
	!gmpx_patricia_lookup_least(instance->rinst_global_state_root));

    /* Toss the trees. */

    gmpx_patroot_destroy(instance->rinst_intfs);
    gmpx_patroot_destroy(instance->rinst_global_state_root);

    /* Toss the client ordinals. */

    ord_destroy_context(instance->rinst_ord_handle);

    /* Destroy the address catalog. */

    gmp_destroy_addr_catalog(&instance->rinst_addr_cat);

    /* Destroy the smear timer. */

    gmpx_destroy_timer(instance->rinst_smear_timer);

    /* Delink the block and free it. */

    thread_remove(&instance->rinst_thread);
    gmpx_free_block(gmpr_instance_tag, instance);

    /* If this was the last instance for a protocol, disable it. */

    found_proto = FALSE;
    FOR_ALL_CIRCULAR_THREAD_ENTRIES(&gmpr_global_instance_thread, thread_ptr) {
	instance = gmpr_thread_to_instance(thread_ptr);
	if (instance->rinst_proto == inst_proto) {
	    found_proto = TRUE;
	    break;
	}
    }
    if (!found_proto)
	gmpp_enab_disab_proto(GMP_ROLE_ROUTER, inst_proto, FALSE);
	
    /* If that was the last instance, deregister with the packet handler. */

    if (thread_circular_thread_empty(&gmpr_global_instance_thread))
	gmpp_deregister(GMP_ROLE_ROUTER);
}
