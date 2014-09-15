/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_factory.h"

template <>
BgpObjectFactory *Factory<BgpObjectFactory>::singleton_ = NULL;

#include "bgp/bgp_config_listener.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, BgpConfigListener, BgpConfigListener);

#include "bgp/bgp_config.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, BgpConfigManager, BgpConfigManager);

#include "bgp/bgp_evpn.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, EvpnManager, EvpnManager);

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

#include "bgp/routing-instance/peer_manager.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, PeerManager, PeerManager);

#include "bgp/routing-instance/routing_instance.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, RoutingInstance, RoutingInstance);
FACTORY_STATIC_REGISTER(BgpObjectFactory, RoutingInstanceMgr, RoutingInstanceMgr);

#include "bgp/routing-instance/rtarget_group_mgr.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, RTargetGroupMgr, RTargetGroupMgr);

#include "bgp/bgp_peer_close.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, PeerCloseManager, PeerCloseManager);

#include "bgp/scheduling_group.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, SchedulingGroup, SchedulingGroup);

#include "bgp/state_machine.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, StateMachine, StateMachine);

#include "bgp/bgp_multicast.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, McastTreeManager, McastTreeManager);
