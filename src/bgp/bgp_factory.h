/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP__BGP_FACTORY_H__
#define __BGP__BGP_FACTORY_H__

#include <boost/function.hpp>
#include "base/factory.h"

class BgpConfigListener;
class BgpConfigManager;
class BgpExport;
class BgpInstanceConfig;
class BgpNeighborConfig;
class BgpPeer;
class BgpServer;
class BgpSessionManager;
class EventManager;
class EvpnManager;
class EvpnTable;
class ErmVpnTable;
class IPeer;
class McastTreeManager;
class PeerManager;
class PeerCloseManager;
class PeerRibMembershipManager;
class RibOut;
class RibOutUpdates;
class RoutingInstance;
class RoutingInstanceMgr;
class RTargetGroupMgr;
class SchedulingGroup;
class StateMachine;

class BgpObjectFactory : public Factory<BgpObjectFactory> {
    FACTORY_TYPE_N0(BgpObjectFactory, SchedulingGroup);
    FACTORY_TYPE_N1(BgpObjectFactory, BgpConfigListener, BgpConfigManager *);
    FACTORY_TYPE_N1(BgpObjectFactory, BgpConfigManager, BgpServer *);
    FACTORY_TYPE_N1(BgpObjectFactory, BgpExport, RibOut *);
    FACTORY_TYPE_N1(BgpObjectFactory, EvpnManager, EvpnTable *);
    FACTORY_TYPE_N1(BgpObjectFactory, McastTreeManager, ErmVpnTable *);
    FACTORY_TYPE_N1(BgpObjectFactory, PeerCloseManager, IPeer *);
    FACTORY_TYPE_N1(BgpObjectFactory, PeerManager, RoutingInstance *);
    FACTORY_TYPE_N1(BgpObjectFactory, PeerRibMembershipManager, BgpServer *);
    FACTORY_TYPE_N1(BgpObjectFactory, RibOutUpdates, RibOut *);
    FACTORY_TYPE_N1(BgpObjectFactory, RoutingInstanceMgr, BgpServer *);
    FACTORY_TYPE_N1(BgpObjectFactory, RTargetGroupMgr, BgpServer *);
    FACTORY_TYPE_N1(BgpObjectFactory, StateMachine, BgpPeer *);
    FACTORY_TYPE_N2(BgpObjectFactory, BgpSessionManager,
                    EventManager *, BgpServer *);
    FACTORY_TYPE_N3(BgpObjectFactory, BgpPeer,
                    BgpServer *, RoutingInstance *, const BgpNeighborConfig *);
    FACTORY_TYPE_N4(BgpObjectFactory, RoutingInstance,
                    std::string, BgpServer *, RoutingInstanceMgr *,
                    const BgpInstanceConfig *);
};

#endif
