/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_gmp_intf_h
#define vnsw_agent_gmp_intf_h

/*
 * GMP interface handle
 *
 * This structure represents an interface from the GMP toolkit's point of
 * view--a pointer to it is defined as the abstract interface ID.
 *
 * The GMP toolkit uses the same type to represent an interface for both
 * the host and router sides.  rpd needs to bind different structures in
 * the host case (the mgm_instance) and the router case (the gmp_intf).
 * So we embed this common structure within each.  The structure carries
 * a boolean that defines whether we're looking at a host
 * or a router interface;  with that knowledge we can use the wrapper inlines
 * to extract the mgm_inst or gmp_intf structures as appropriate.
 *
 * The structure also has a thread entry for the transmit interface thread;
 * this is used for both host and router interfaces.
 */
typedef struct gmp_intf_handle_ {
    boolean gmpifh_host;        /* TRUE if this is a host interface */
    thread gmpifh_xmit_thread;      /* Entry on transmit thread */
} gmp_intf_handle;

THREAD_TO_STRUCT(gmp_xmit_thread_to_handle, gmp_intf_handle,
         gmpifh_xmit_thread);

/*
 * gmp_intf
 *
 * This structure is rpd's manifestation of an interface for use by
 * GMPR.  It is created upon the first reference to an interface in
 * configuration (whether or not the physical interface exists or is
 * up) and is destroyed when there are no longer any references to the
 * interface.  gmp_intfs are bound to mgm_ifs (when they are up) and
 * are keyed by ifae and protocol.
 */

typedef struct gmp_intf_ {
    patnode gmpif_patnode;      /* Patricia node */
    gmp_proto gmpif_proto;      /* Protocol (MLD or IGMP) */
    gmp_intf_handle gmpif_handle;   /* GMP interface handle */
    uint32_t gmpif_refcount;       /* Reference count for locking */
    void *vm_interface;
    gmpr_intf_params params;
} gmp_intf;

PATNODE_TO_STRUCT(gmp_intf_patnode_to_intf, gmp_intf, gmpif_patnode);
MEMBER_TO_STRUCT(gmp_handle_to_gif, gmp_intf, gmp_intf_handle, gmpif_handle);

/* Inlines */

/*
 * gmp_intf_is_local
 *
 * Returns TRUE if the gmp_intf pointer represents the local "interface"
 * (is NULL) or FALSE if not.
 */
static inline boolean
gmp_intf_is_local (gmp_intf *gif)
{
    return (gif == NULL);
}


/*
 * gmp_gif_to_handle
 *
 * Convert a gmp_intf pointer to a gmp_intf_handle pointer.  If the gmp_intf
 * pointer is NULL, we return a NULL handle.
 */
static inline gmp_intf_handle *
gmp_gif_to_handle (gmp_intf *gif)
{
    if (gif)
        return &gif->gmpif_handle;
    else
        return NULL;
}

#endif /* vnsw_agent_gmp_intf_h */
