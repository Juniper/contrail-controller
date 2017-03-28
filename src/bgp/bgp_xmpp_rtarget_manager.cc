/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */
#include "bgp/bgp_xmpp_rtarget_manager.h"

#include <utility>
#include <vector>

#include <boost/foreach.hpp>

#include "sandesh/sandesh.h"

#include "bgp/ipeer.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_xmpp_channel.h"
#include "routing-instance/routing_instance.h"

using std::make_pair;
using std::pair;
using std::string;
using std::vector;

BgpXmppRTargetManager::BgpXmppRTargetManager(BgpXmppChannel *bgp_xmpp_channel) :
        bgp_xmpp_channel_(bgp_xmpp_channel) {
}

BgpXmppRTargetManager::~BgpXmppRTargetManager() {
}

bool BgpXmppRTargetManager::IsSubscriptionEmpty() const {
    return bgp_xmpp_channel_->IsSubscriptionEmpty();
}

bool BgpXmppRTargetManager::IsSubscriptionGrStale(
        RoutingInstance *instance) const {
    return bgp_xmpp_channel_->IsSubscriptionGrStale(instance);
}

bool BgpXmppRTargetManager::IsSubscriptionLlgrStale(
        RoutingInstance *instance) const {
    return bgp_xmpp_channel_->IsSubscriptionLlgrStale(instance);
}

bool BgpXmppRTargetManager::delete_in_progress() const {
    return bgp_xmpp_channel_->delete_in_progress();
}

const IPeer *BgpXmppRTargetManager::Peer() const {
    return bgp_xmpp_channel_->Peer();
}

void BgpXmppRTargetManager::Enqueue(DBRequest *req) const {
    RoutingInstanceMgr *instance_mgr =
        bgp_xmpp_channel_->bgp_server()->routing_instance_mgr();
    RoutingInstance *master = instance_mgr->GetDefaultRoutingInstance();
    BgpTable *table = master->GetTable(Address::RTARGET);
    assert(table);
    table->Enqueue(req);
}

BgpAttrPtr BgpXmppRTargetManager::GetRouteTargetRouteAttr() const {
    BgpAttrSpec attrs;
    BgpAttrNextHop nexthop(bgp_xmpp_channel_->bgp_server()->bgp_identifier());
    attrs.push_back(&nexthop);
    BgpAttrOrigin origin(BgpAttrOrigin::IGP);
    attrs.push_back(&origin);
    return bgp_xmpp_channel_->bgp_server()->attr_db()->Locate(attrs);
}

int BgpXmppRTargetManager::local_autonomous_system() const {
    return bgp_xmpp_channel_->bgp_server()->local_autonomous_system();
}

const BgpXmppRTargetManager::RouteTargetList &
BgpXmppRTargetManager::GetSubscribedRTargets(RoutingInstance *instance) const {
    return bgp_xmpp_channel_->GetSubscribedRTargets(instance);
}

uint32_t BgpXmppRTargetManager::GetRTargetRouteFlag(
        const RouteTarget &rtarget) const {
    PublishedRTargetRoutes::const_iterator rt_loc =
        rtarget_routes_.find(rtarget);
    if (rt_loc == rtarget_routes_.end() || rt_loc->second.empty())
        return 0;

    // Route is [llgr-]stale only if it is stale for all instances in the set.
    uint32_t flags = BgpPath::Stale | BgpPath::LlgrStale;
    BOOST_FOREACH(RoutingInstance *routing_instance, rt_loc->second) {
        if (!IsSubscriptionGrStale(routing_instance))
            flags &= ~BgpPath::Stale;
        if (!IsSubscriptionLlgrStale(routing_instance))
            flags &= ~BgpPath::LlgrStale;
        if (!flags)
            break;
    }

    return flags;
}

void BgpXmppRTargetManager::RTargetRouteOp(as4_t asn,
                                           const RouteTarget &rtarget,
                                           BgpAttrPtr attr, bool add_change,
                                           uint32_t flags) const {
    if (add_change && delete_in_progress())
        return;

    DBRequest req;
    RTargetPrefix rt_prefix(asn, rtarget);
    req.key.reset(new RTargetTable::RequestKey(rt_prefix, Peer()));
    if (add_change) {
        // Find correct rtarget route flags if not already known.
        if (!flags)
            flags = GetRTargetRouteFlag(rtarget);
        req.data.reset(new RTargetTable::RequestData(attr, flags, 0, 0, 0));
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
    } else {
        req.oper = DBRequest::DB_ENTRY_DELETE;
    }
    Enqueue(&req);
}

void BgpXmppRTargetManager::ASNUpdateCallback(as_t old_asn,
                                              as_t old_local_asn) const {
    if (local_autonomous_system() == old_local_asn)
        return;
    if (IsSubscriptionEmpty())
        return;

    // Delete the route and add with new local ASN
    BgpAttrPtr attr = GetRouteTargetRouteAttr();
    for (PublishedRTargetRoutes::const_iterator it = rtarget_routes_.begin();
            it != rtarget_routes_.end(); it++) {
        RTargetRouteOp(old_local_asn, it->first, NULL, false);
        RTargetRouteOp(local_autonomous_system(), it->first, attr, true);
    }
}

void BgpXmppRTargetManager::IdentifierUpdateCallback(
        Ip4Address old_identifier) const {
    if (IsSubscriptionEmpty())
        return;
    BgpAttrPtr attr = GetRouteTargetRouteAttr();

    // Update the route with new nexthop
    for (PublishedRTargetRoutes::const_iterator it = rtarget_routes_.begin();
            it != rtarget_routes_.end(); it++) {
        RTargetRouteOp(local_autonomous_system(), it->first, attr, true);
    }
}

void BgpXmppRTargetManager::AddNewRTargetRoute(RoutingInstance *rtinstance,
        const RouteTarget &rtarget, BgpAttrPtr attr) {
    PublishedRTargetRoutes::iterator rt_loc = rtarget_routes_.find(rtarget);
    if (rt_loc == rtarget_routes_.end()) {
        pair<PublishedRTargetRoutes::iterator, bool> ret =
            rtarget_routes_.insert(make_pair(rtarget, RoutingInstanceList()));

        rt_loc = ret.first;

        // Send rtarget route ADD
        RTargetRouteOp(local_autonomous_system(), rtarget, attr, true);
    }
    rt_loc->second.insert(rtinstance);
}

void BgpXmppRTargetManager::DeleteRTargetRoute(
        RoutingInstance *rtinstance, const RouteTarget &rtarget) {
    PublishedRTargetRoutes::iterator rt_loc = rtarget_routes_.find(rtarget);
    assert(rt_loc != rtarget_routes_.end());
    assert(rt_loc->second.erase(rtinstance));
    if (rt_loc->second.empty()) {
        rtarget_routes_.erase(rtarget);
        // Send rtarget route DELETE
        RTargetRouteOp(local_autonomous_system(), rtarget, NULL, false);
    }
}

void BgpXmppRTargetManager::RoutingInstanceCallback(
        RoutingInstance *rt_instance, RouteTargetList *targets) {
    // Import list in the routing instance
    const RouteTargetList &new_list = rt_instance->GetImportList();

    // Previous route target list for which the rtarget route was added
    RouteTargetList *current = targets;
    RouteTargetList::iterator cur_next_it, cur_it;
    cur_it = cur_next_it = current->begin();
    RouteTargetList::const_iterator new_it = new_list.begin();

    pair<RouteTargetList::iterator, bool> r;
    BgpAttrPtr attr = GetRouteTargetRouteAttr();
    while (cur_it != current->end() && new_it != new_list.end()) {
        if (*new_it < *cur_it) {
            r = current->insert(*new_it);
            assert(r.second);
            AddNewRTargetRoute(rt_instance, *new_it, attr);
            new_it++;
        } else if (*new_it > *cur_it) {
            cur_next_it++;
            DeleteRTargetRoute(rt_instance, *cur_it);
            current->erase(cur_it);
            cur_it = cur_next_it;
        } else {
            // Update
            cur_it++;
            new_it++;
        }
        cur_next_it = cur_it;
    }
    for (; new_it != new_list.end(); ++new_it) {
        r = current->insert(*new_it);
        assert(r.second);
        AddNewRTargetRoute(rt_instance, *new_it, attr);
    }
    for (cur_next_it = cur_it;
         cur_it != current->end();
         cur_it = cur_next_it) {
        cur_next_it++;
        DeleteRTargetRoute(rt_instance, *cur_it);
        current->erase(cur_it);
    }
}

void BgpXmppRTargetManager::UpdateRouteTargetRouteFlag(
        RoutingInstance *routing_instance, const RouteTargetList &targets,
        uint32_t flags) const {
    BgpAttrPtr attr = GetRouteTargetRouteAttr();
    BOOST_FOREACH(RouteTarget rtarget, targets) {
        // Update route target route [llgr-]stale flag status.
        RTargetRouteOp(local_autonomous_system(), rtarget, attr, true, flags);
    }
}

void BgpXmppRTargetManager::Close() {
    if (rtarget_routes_.empty())
        return;

    for (PublishedRTargetRoutes::iterator it = rtarget_routes_.begin();
            it != rtarget_routes_.end(); it++) {
        RTargetRouteOp(local_autonomous_system(), it->first, NULL, false);
    }
    rtarget_routes_.clear();
}

void BgpXmppRTargetManager::Stale(const RouteTargetList &targets) const {
    BgpAttrPtr attr = GetRouteTargetRouteAttr();

    // Update route targets to clear STALE flag.
    BOOST_FOREACH(RouteTarget rtarget, targets) {
        PublishedRTargetRoutes::const_iterator rt_loc =
            rtarget_routes_.find(rtarget);
        assert(rt_loc != rtarget_routes_.end());

        // Send rtarget route ADD
        RTargetRouteOp(local_autonomous_system(), rtarget, attr, true);
    }
}

// Add/Delete rtarget route for import route target of the routing instance.
void BgpXmppRTargetManager::PublishRTargetRoute(RoutingInstance *rt_instance,
                                                bool add_change) {
    if (IsSubscriptionEmpty())
        return;

    if (add_change) {
        BOOST_FOREACH(RouteTarget rtarget, GetSubscribedRTargets(rt_instance)) {
            AddNewRTargetRoute(rt_instance, rtarget, GetRouteTargetRouteAttr());
        }
    } else {
        BOOST_FOREACH(RouteTarget rtarget, GetSubscribedRTargets(rt_instance)) {
            DeleteRTargetRoute(rt_instance, rtarget);
        }
    }
}

void BgpXmppRTargetManager::FillInfo(BgpNeighborRoutingInstance *instance,
                                     const RouteTargetList &targets) const {
    vector<string> import_targets;
    BOOST_FOREACH(RouteTarget rt, targets) {
        import_targets.push_back(rt.ToString());
    }
    instance->set_import_targets(import_targets);
}
