/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_XMPP_RTARGET_MANAGER_H_
#define SRC_BGP_BGP_XMPP_RTARGET_MANAGER_H_

#include <map>
#include <set>
#include <string>

#include "bgp/rtarget/rtarget_table.h"

class BgpNeighborRoutingInstance;
class BgpXmppChannel;
class DBRequest;
class IPeer;
class RoutingInstance;

class BgpXmppRTargetManager {
public:
    typedef std::set<RouteTarget> RouteTargetList;

    explicit BgpXmppRTargetManager(BgpXmppChannel *bgp_xmpp_channel);
    virtual ~BgpXmppRTargetManager();
    void RoutingInstanceCallback(RoutingInstance *rt_instance,
                                 RouteTargetList *targets);
    void PublishRTargetRoute(RoutingInstance *rt_instance,
                             bool add_change);
    void Close();

    void ASNUpdateCallback(as_t old_asn, as_t old_local_asn) const;
    void FillInfo(BgpNeighborRoutingInstance *instance,
                  const RouteTargetList &targets) const;
    void Stale(const RouteTargetList &targets) const;
    void UpdateRouteTargetRouteFlag(RoutingInstance *routing_instance,
             const RouteTargetList &targets, uint32_t flags) const;

protected:
    virtual void RTargetRouteOp(as_t asn, const RouteTarget &rtarget,
                                BgpAttrPtr attr, bool add_change,
                                uint32_t flags = 0) const;

private:
    typedef std::set<RoutingInstance *> RoutingInstanceList;
    typedef std::map<RouteTarget, RoutingInstanceList> PublishedRTargetRoutes;

    void AddNewRTargetRoute(RoutingInstance *rtinstance,
                            const RouteTarget &rtarget, BgpAttrPtr attr);
    void DeleteRTargetRoute(RoutingInstance *rtinstance,
                            const RouteTarget &rtarget);

    BgpTable *GetRouteTargetTable() const;
    uint32_t GetRTargetRouteFlag(const RouteTarget &rtarget) const;

    virtual BgpAttrPtr GetRouteTargetRouteAttr() const;
    virtual bool IsSubscriptionEmpty() const;
    virtual bool IsSubscriptionGrStale(RoutingInstance *instance) const;
    virtual bool IsSubscriptionLlgrStale(RoutingInstance *instance) const;
    virtual bool delete_in_progress() const;
    virtual const IPeer *Peer() const;
    virtual const RouteTargetList &GetSubscribedRTargets(
            RoutingInstance *instance) const;
    virtual void Enqueue(DBRequest *req) const;
    virtual int local_autonomous_system() const;

    PublishedRTargetRoutes rtarget_routes_;
    BgpXmppChannel *bgp_xmpp_channel_;
};

#endif  // SRC_BGP_BGP_XMPP_RTARGET_MANAGER_H_
