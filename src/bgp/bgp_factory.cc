/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_factory.h"

template <>
BgpObjectFactory *Factory<BgpObjectFactory>::singleton_ = NULL;

#include "bgp/bgp_export.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, BgpExport, BgpExport);

#include "bgp/bgp_peer.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, BgpPeer, BgpPeer);

#include "bgp/bgp_session_manager.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, BgpSessionManager, BgpSessionManager);

#include "bgp/bgp_peer_membership.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, PeerRibMembershipManager, PeerRibMembershipManager);

#include "bgp/bgp_ribout_updates.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, RibOutUpdates, RibOutUpdates);

#include "bgp/routing-instance/routing_instance.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, RoutingInstance, RoutingInstance);
FACTORY_STATIC_REGISTER(BgpObjectFactory, RoutingInstanceMgr, RoutingInstanceMgr);

#include "bgp/bgp_peer_close.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, PeerCloseManager, PeerCloseManager);

#include "bgp/state_machine.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, StateMachine, StateMachine);

#include "bgp/bgp_multicast.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, McastTreeManager, McastTreeManager);
