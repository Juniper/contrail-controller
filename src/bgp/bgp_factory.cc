/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_factory.h"

template <>
BgpObjectFactory *Factory<BgpObjectFactory>::singleton_ = NULL;

#include "bgp/bgp_evpn.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, EvpnManager, EvpnManager);

#include "bgp/bgp_export.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, BgpExport, BgpExport);

#include "bgp/bgp_lifetime.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, BgpLifetimeManager,
    BgpLifetimeManager);

#include "bgp/bgp_membership.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, BgpMembershipManager,
    BgpMembershipManager);

#include "bgp/bgp_peer.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, BgpPeer, BgpPeer);

#include "bgp/bgp_session_manager.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, BgpSessionManager, BgpSessionManager);

#include "bgp/bgp_ribout_updates.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, RibOutUpdates, RibOutUpdates);

#include "bgp/routing-instance/peer_manager.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, PeerManager, PeerManager);

#include "bgp/routing-instance/routing_instance.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, RoutingInstance, RoutingInstance);
FACTORY_STATIC_REGISTER(BgpObjectFactory, RoutingInstanceMgr,
    RoutingInstanceMgr);

#include "bgp/routing-policy/routing_policy.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, RoutingPolicy, RoutingPolicy);
FACTORY_STATIC_REGISTER(BgpObjectFactory, RoutingPolicyMgr, RoutingPolicyMgr);

#include "bgp/routing-instance/rtarget_group_mgr.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, RTargetGroupMgr, RTargetGroupMgr);

FACTORY_STATIC_REGISTER(BgpObjectFactory, PeerCloseManager, PeerCloseManager);

#include "bgp/scheduling_group.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, SchedulingGroup, SchedulingGroup);

FACTORY_STATIC_REGISTER(BgpObjectFactory, StateMachine, StateMachine);

#include "bgp/bgp_multicast.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, McastTreeManager, McastTreeManager);

#include "bgp/bgp_message_builder.h"
FACTORY_STATIC_REGISTER(BgpObjectFactory, BgpMessageBuilder, BgpMessageBuilder);

#include "bgp/routing-instance/route_aggregator.h"
FACTORY_PARAM_STATIC_REGISTER(BgpObjectFactory, IRouteAggregator,
    Address::INET, RouteAggregatorInet);
FACTORY_PARAM_STATIC_REGISTER(BgpObjectFactory, IRouteAggregator,
    Address::INET6, RouteAggregatorInet6);

#include "bgp/routing-instance/service_chaining.h"
FACTORY_PARAM_STATIC_REGISTER(BgpObjectFactory, IServiceChainMgr,
    Address::INET, ServiceChainMgrInet);
FACTORY_PARAM_STATIC_REGISTER(BgpObjectFactory, IServiceChainMgr,
    Address::INET6, ServiceChainMgrInet6);

#include "bgp/routing-instance/static_route.h"
FACTORY_PARAM_STATIC_REGISTER(BgpObjectFactory, IStaticRouteMgr,
    Address::INET, StaticRouteMgrInet);
FACTORY_PARAM_STATIC_REGISTER(BgpObjectFactory, IStaticRouteMgr,
    Address::INET6, StaticRouteMgrInet6);
