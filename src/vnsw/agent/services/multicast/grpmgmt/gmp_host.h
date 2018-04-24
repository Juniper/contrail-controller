/* $Id: gmp_host.h 362048 2010-02-09 00:25:11Z builder $
 *
 * gmp_host.h - External definitions for gmp_host
 *
 * Dave Katz, March 2008
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 * This file contains the external definitions for host-side GMP.  It should
 * be included by code calling the host side.
 */

#ifndef __GMP_HOST_H__
#define __GMP_HOST_H__


/*
 * Overview
 *
 * The gmph_* files define the host side of IGMP/MLD.  This part of the
 * protocol initiates requests for listening to multicast groups and
 * responds to incoming Query packets from the router side.
 *
 * This code supports multiple instances.  An instance is simply a
 * grouping of interfaces and clients accessing those interfaces; an
 * interface can be bound to one and only one instance, as is a
 * client.  The use of multiple instances may or may not be desirable
 * depending on the circumstances of the application; combining or
 * separating instances has no semantic impact from the standpoint of
 * this code.
 *
 * Within an instance, one or more interfaces are bound to the instance;  as
 * outlined above, a set of interfaces semantically defines an instance.
 * Interfaces are completely independent of one another.  An interface
 * is described by a gmpx_intf_id, which is an opaque type defined in the
 * application's gmpx_environment.h file.  This code neither knows nor
 * cares about the actual data type (common values are pointers to an
 * application data structure or an IFL index);  the only requirement is
 * that the data type is scalar and that the value is unique per interface.
 * I/O is performed by passing the interface ID to and from the application's
 * supplied I/O routines.
 *
 * Also within an instance, one or more clients are bound.  A client is a
 * single requestor of multicast traffic;  the most typical binding is to
 * a single user process.  This code combines the possibly-overlapping
 * requests from multiple clients to create the GMP interface state in such
 * a way as to guarantee that requested traffic will be delivered.  Note that
 * this means that filtering of received traffic to a client, based on the
 * actual request from that client, must still take place in the data path
 * (since one client may include an (S,G) and another may exclude it, for
 * example.)
 *
 * Clients make Listen calls to request or cancel reception of multicast groups.
 *
 *
 * Application call flow
 *
 * When an application wants to use GMPH services, it must first create an
 * instance with gmph_create_instance().  This returns an opaque instance ID
 * that is used in subsequent calls.
 *
 * At least one client is then created with gmph_register().
 *
 * Interfaces are then bound to the instance with gmph_attach_intf().
 * Interfaces and clients may be created in any order.
 *
 * Clients then may request or cancel multicast groups by calling gmph_listen()
 * for the appropriate interface and group.  Of course, a client may not
 * call gmph_listen() for an interface until that interface is bound to the
 * instance.
 *
 *
 * When tearing down services, clients and interfaces may be torn down
 * in any order.  gmph_detach() is used to get rid of a client.  In
 * this case, gmph_listen() calls to clean up previously requested
 * groups are unnecessary, and appropriate Leave messages will be
 * enqueued for transmission.
 *
 * gmph_detach_intf() will unceremoniously destroy an interface, along with
 * any group state bound to it.  Any pending Leave messages will be
 * discarded.
 *
 * If a graceful shutdown of an interface is desired,
 * gmph_detach_intf_soft() can be called instead.  The caller supplies
 * a pointer to a callback routine, and once all pending messages are
 * sent on the interface, GMP will call back to notify the application
 * that the interface is being destroyed, and then immediately unbinds
 * the interface.  A subsequent call to gmph_attach_intf(), prior to
 * the callback, will reanimate the interface and not shut it down,
 * and no callback will be made.  Similarly, a call to
 * gmph_detach_intf() will unceremoniously and immediately unbind the
 * interface (and there will be no callback).
 *
 * It is up to the application to cause Leaves to be sent for any
 * joined groups by calling gmph_listen() for each of them (or by
 * calling gmph_leave_all_groups()) prior to calling
 * gmph_detach_intf_soft().  Otherwise, any Joins previously sent will
 * not be countermanded.
 *
 * gmph_destroy_instance() is used to get rid of the instance when it
 * is no longer needed.  It will unceremoniously destroy all interface
 * and client state, and will discard any packets pending
 * transmission, so it can be called to single-handedly clean up
 * everything.  On the other hand, if a graceful shutdown is required,
 * gmph_detach_intf_soft() should be called for each interface, and
 * the application should wait for all callbacks, before destroying
 * the intstance.
 *
 *
 * Detailed calls
 *
 * gmph_create_instance(proto, context)
 *
 *  Creates an instance, returning an instance ID that is used in subsequent
 *  calls.  The context value is an opaque value that is passed through
 *  in timer creation calls.
 *
 * gmph_register(instance_id)
 *
 *  Creates a client, returning a client ID that is used in subsequent calls.
 *
 * gmph_attach_intf(instance_id, intf_id)
 *
 *  Binds an interface to an instance.  Once the client is created and the
 *  interface is bound to the instance, listen requests can be made.
 *
 * gmph_listen(client_id, intf_id, group, filter_mode, sources)
 *
 *  Requests or cancels reception of a (*,G) or multiple (S,G)s.  A Leave is
 *  signaled by a listen request with filter mode Include and no sources;
 *  all other combinations are joins.  The request must be idempotent for
 *  the group; in other words, the group state for the client is set to the
 *  contents of the request.  If an additional source is to be added for an
 *  existing group, for instance, all requested sources must be listed on
 *  the request.
 *
 * gmph_detach_intf(instance_id, intf_id)
 *
 *  Unbinds an interface from the instance.  All state on the interface
 *  is destroyed immediately, and no Leave messages will be sent.
 *
 * gmph_detach_intf_soft(instance_id, intf_id, callback, context)
 *
 *  Gracefully unbinds an interface from the instance.  Once all
 *  necessary packets have been sent, the callback is made (with the
 *  opaque context passed by the caller) and the interface is
 *  destroyed immediately thereafter.  If gmph_attach_intf() is called
 *  after this routine, but prior to the callback, it will keep the
 *  interface in existence and no callback will be made.  Any enqueued
 *  packets will still be sent.
 *
 * gmph_detach(client_id)
 *
 *  Detaches a client from the instance.  Any active Listen requests
 *  from the client will be dealt with appropriately (enqueueing Leave
 *  messages if the client being destroyed is the only one interested in
 *  those groups.)
 *
 * gmph_destroy_instance(instance_id)
 *
 *  Destroys the instance and all state contained therein, immediately.  This
 *  will clean up all clients, interfaces, and listen requests, but will not
 *  send appropriate Leave messages.
 *
 * gmph_set_intf_version(instance_id, intf_id, version)
 *
 *  Sets the maximum version number supported by the host.  This is generally
 *  not called, since the code will adapt to the version sent by the querier
 *  automatically.  Setting the version caps it to the specified value.
 *
 * gmph_set_intf_passive(instance_id, intf_id, passive)
 *
 *  Sets the passive state of the interface to the specified value.  If an
 *  interface is passive, the code will not send Report messages under any
 *  circumstances.  This is primarily for the benefit of IGMP snooping
 *  switches, which pass host-generated Reports transparently but must keep
 *  aggregated host state on the upstream interfaces.
 *
 * gmph_send_intf_groups(instance_id, intf_id)
 *
 *  Enqueues all group state on the specified interface for transmission.
 *  This is typically used for refreshing upstream state in proxy switches.
 *
 * gmph_leave_all_groups(client_id, intf_id)
 *
 *  Removes all groups listened to on the specified interface by the
 *  specified client.  This is equivalent to calling gmph_listen(client_id,
 *  intf_id, group, INCLUDE, NULL) for each group originally listend
 *  go, and will resut in Leaves being sent for these groups if no other
 *  client is listening to them.
 *
 * gmph_intf_has_channel(instance_id, intf_id, source, group, exact)
 *
 *  Returns TRUE if the specified (S,G) has been requested on the
 *  interface, or FALSE if not.  If exact matching is requested, it will
 *  return TRUE for a (*,G) call only if a (*,G) request has been made;
 *  if inexact, it will return TRUE for a (*,G) call if one or more (S,G)s
 *  for the same group have been requested.
 */


/*
 * Soft interface detach callback
 *
 * This typedef defines the callback passed as a parameter to
 * gmph_detach_intf_soft().  This routine will be called after any necessary
 * packets are transmitted.  The interface will cease to exist immediately
 * after the callback is made.
 */
typedef void (*gmph_soft_detach_callback)(gmp_proto proto,
                      gmpx_intf_id intf_id,
                      void *context);

/*
 * External references
 */
extern gmp_client_id gmph_register(gmp_instance_id instance_id);
extern void gmph_detach(gmp_client_id client_id);
extern int gmph_listen(gmp_client_id client_id, gmpx_intf_id intf_id,
               const u_int8_t *group, gmp_filter_mode filter_mode,
               gmp_addr_thread *addr_thread);
extern gmp_instance_id gmph_create_instance(gmp_proto proto, void *context);
extern void gmph_destroy_instance(gmp_instance_id instance_id);
extern int gmph_set_intf_version(gmp_instance_id instance_id,
                 gmpx_intf_id intf_id, u_int version);
extern int gmph_attach_intf(gmp_instance_id instance_id, gmpx_intf_id intf_id);
extern int gmph_detach_intf(gmp_instance_id instance_id, gmpx_intf_id intf_id);
extern int gmph_detach_intf_soft(gmp_instance_id instance_id,
                 gmpx_intf_id intf_id,
                 gmph_soft_detach_callback callback,
                 void *context);
extern int gmph_leave_all_groups(gmp_client_id client_id, gmpx_intf_id intf_id);
extern void gmph_send_intf_groups(gmp_instance_id instance_id,
                  gmpx_intf_id intf_id);
extern void gmph_set_intf_passive(gmp_instance_id instance_id,
                  gmpx_intf_id intf_id, boolean passive);
extern boolean gmph_intf_has_channel(gmp_instance_id instance_id,
                     gmpx_intf_id intf_id,
                     const u_int8_t *source_addr,
                     const u_int8_t *group_addr,
                     boolean exact);

#endif /* __GMP_HOST_H__ */
