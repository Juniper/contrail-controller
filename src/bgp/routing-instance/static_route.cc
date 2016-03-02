/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/static_route.h"

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "base/map_util.h"
#include "base/task_annotations.h"
#include "base/task_trigger.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_server.h"
#include "bgp/inet6vpn/inet6vpn_route.h"
#include "bgp/l3vpn/inetvpn_route.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/routepath_replicator.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/static_route_types.h"
#include "net/community_type.h"

using boost::assign::list_of;
using boost::system::error_code;
using std::make_pair;
using std::set;
using std::string;
using std::vector;

class StaticRouteState : public ConditionMatchState {
public:
    explicit StaticRouteState(StaticRoutePtr info) : info_(info) {
    }
    StaticRoutePtr info() {
        return info_;
    }

private:
    StaticRoutePtr info_;
    DISALLOW_COPY_AND_ASSIGN(StaticRouteState);
};

template <typename T>
class StaticRoute : public ConditionMatch {
public:
    typedef typename T::RouteT RouteT;
    typedef typename T::VpnRouteT VpnRouteT;
    typedef typename T::PrefixT PrefixT;
    typedef typename T::AddressT AddressT;
    typedef StaticRouteMgr<T> StaticRouteMgrT;

    // List of Route targets
    typedef set<RouteTarget> RouteTargetList;

    // List of path ids for the Nexthop
    typedef set<uint32_t> NexthopPathIdList;

    enum CompareResult {
        NoChange = 0,
        PrefixChange = 1,
        NexthopChange = 2,
        AttributeChange = 3
    };

    StaticRoute(RoutingInstance *rtinstance, StaticRouteMgrT *manager,
        const PrefixT &static_route, const StaticRouteConfig &config);
    Address::Family GetFamily() const { return manager_->GetFamily(); }
    AddressT GetAddress(IpAddress addr) const {
        return manager_->GetAddress(addr);
    }

    CompareResult CompareConfig(const StaticRouteConfig &config);
    void FillShowInfo(StaticRouteInfo *info) const;

    const PrefixT &prefix() const {
        return prefix_;
    }

    RoutingInstance *routing_instance() const {
        return routing_instance_;
    }

    BgpTable *bgp_table() const {
        return routing_instance_->GetTable(this->GetFamily());
    }

    BgpRoute *nexthop_route() const {
        return nexthop_route_;
    }

    NexthopPathIdList *NexthopPathIds() { return &nexthop_path_ids_; }

    void set_nexthop_route(BgpRoute *nexthop_route) {
        nexthop_route_ = nexthop_route;

        if (!nexthop_route_) {
            nexthop_path_ids_.clear();
            return;
        }

        assert(nexthop_path_ids_.empty());

        for (Route::PathList::iterator it =
             nexthop_route->GetPathList().begin();
             it != nexthop_route->GetPathList().end(); it++) {
            BgpPath *path = static_cast<BgpPath *>(it.operator->());

            // Infeasible paths are not considered
            if (!path->IsFeasible()) break;

            // take snapshot of all ECMP paths
            if (nexthop_route_->BestPath()->PathCompare(*path, true)) break;

            // Use the nexthop attribute of the nexthop path as the path id.
            uint32_t path_id = path->GetAttr()->nexthop().to_v4().to_ulong();
            nexthop_path_ids_.insert(path_id);
        }
    }

    const RouteTargetList &rtarget_list() const {
        return rtarget_list_;
    }

    void UpdateAttributes(const StaticRouteConfig &config);
    void AddStaticRoute(NexthopPathIdList *list);
    void UpdateStaticRoute();
    void RemoveStaticRoute();
    void NotifyRoute();
    bool IsPending() const;

    virtual bool Match(BgpServer *server, BgpTable *table,
                       BgpRoute *route, bool deleted);

    virtual string ToString() const {
        return (string("StaticRoute ") + nexthop_.to_string());
    }

    void set_unregistered() {
        unregistered_ = true;
    }

    bool unregistered() const {
        return unregistered_;
    }

private:
    // Helper function to match
    bool is_nexthop_route(BgpRoute *route) {
        RouteT *ip_route = dynamic_cast<RouteT *>(route);
        return (nexthop_ == ip_route->GetPrefix().addr());
    }

    CommunityPtr GetCommunity(const StaticRouteConfig &config);
    ExtCommunityPtr UpdateExtCommunity(const BgpAttr *attr) const;

    RoutingInstance *routing_instance_;
    StaticRouteMgrT *manager_;
    PrefixT prefix_;
    IpAddress nexthop_;
    BgpRoute *nexthop_route_;
    NexthopPathIdList nexthop_path_ids_;
    RouteTargetList rtarget_list_;
    CommunityPtr community_;
    bool unregistered_;

    DISALLOW_COPY_AND_ASSIGN(StaticRoute);
};

template <typename T>
StaticRoute<T>::StaticRoute(RoutingInstance *rtinstance,
    StaticRouteMgrT *manager, const PrefixT &prefix,
    const StaticRouteConfig &config)
    : routing_instance_(rtinstance),
      manager_(manager),
      prefix_(prefix),
      nexthop_(config.nexthop),
      nexthop_route_(NULL),
      unregistered_(false) {
    UpdateAttributes(config);
}

//
// Compare the given config against the current state of the StaticRoute.
// Return the appropriate value from CompareResult.
//
template <typename T>
typename StaticRoute<T>::CompareResult StaticRoute<T>::CompareConfig(
    const StaticRouteConfig &config) {
    AddressT address = this->GetAddress(config.address);
    PrefixT prefix(address, config.prefix_length);
    if (prefix_ != prefix) {
        return PrefixChange;
    }
    if (nexthop_ != config.nexthop) {
        return NexthopChange;
    }
    if (rtarget_list_.size() != config.route_targets.size()) {
        return AttributeChange;
    }
    for (vector<string>::const_iterator it = config.route_targets.begin();
         it != config.route_targets.end(); ++it) {
        error_code ec;
        RouteTarget rtarget = RouteTarget::FromString(*it, &ec);
        if (rtarget_list_.find(rtarget) == rtarget_list_.end()) {
            return AttributeChange;
        }
    }
    if (community_ != GetCommunity(config)) {
        return AttributeChange;
    }

    return NoChange;
}

template <typename T>
void StaticRoute<T>::FillShowInfo(StaticRouteInfo *info) const {
    BgpTable *table = bgp_table();
    RouteT rt_key(prefix_);
    const BgpRoute *route = static_cast<const BgpRoute *>(table->Find(&rt_key));
    const BgpPath *path = route ? route->FindPath(BgpPath::StaticRoute) : NULL;

    info->set_prefix(prefix_.ToString());
    info->set_static_rt(path ? true : false);
    info->set_nexthop(nexthop_.to_string());
    if (nexthop_route_) {
        ShowRouteBrief show_route;
        nexthop_route_->FillRouteInfo(table, &show_route);
        info->set_nexthop_rt(show_route);
    }

    vector<string> community_list;
    BOOST_FOREACH(uint32_t value, community_->communities()) {
        community_list.push_back(CommunityType::CommunityToString(value));
    }
    info->set_community_list(community_list);

    vector<string> route_target_list;
    for (RouteTargetList::const_iterator it = rtarget_list_.begin();
        it != rtarget_list_.end(); ++it) {
        route_target_list.push_back(it->ToString());
    }
    info->set_route_target_list(route_target_list);

    if (path) {
        const RoutePathReplicator *replicator = table->server()->replicator(
            Address::VpnFamilyFromFamily(GetFamily()));
        info->set_secondary_tables(
            replicator->GetReplicatedTableNameList(table, route, path));
    }
}

// Match function called from BgpConditionListener
// Concurrency : db::DBTable
template <typename T>
bool StaticRoute<T>::Match(BgpServer *server, BgpTable *table,
                   BgpRoute *route, bool deleted) {
    CHECK_CONCURRENCY("db::DBTable");
    StaticRouteRequest::RequestType type;

    if (is_nexthop_route(route) && !unregistered()) {
        if (deleted)
            type = StaticRouteRequest::NEXTHOP_DELETE;
        else
            type = StaticRouteRequest::NEXTHOP_ADD_CHG;
    } else {
        return false;
    }

    BgpConditionListener *listener = server->condition_listener(GetFamily());
    StaticRouteState *state = static_cast<StaticRouteState *>
        (listener->GetMatchState(table, route, this));
    if (!deleted) {
        // MatchState is added to the Route to ensure that DBEntry is not
        // deleted before the module processes the WorkQueue request.
        if (!state) {
            state = new StaticRouteState(StaticRoutePtr(this));
            listener->SetMatchState(table, route, this, state);
        }
    } else {
        // MatchState is set on all the Routes that matches the conditions
        // Retrieve to check and ignore delete of unseen Add Match
        if (state == NULL) {
            // Not seen ADD ignore DELETE
            return false;
        }
    }

    // The MatchState reference is taken to ensure that the route is not
    // deleted when request is still present in the queue
    // This is to handle the case where MatchState already exists and
    // deleted entry gets reused or reused entry gets deleted.
    state->IncrementRefCnt();

    // Post the Match result to StaticRoute processing task to take Action
    // Nexthop route found in NAT instance ==> Add Static Route
    // and stitch the Path Attribute from nexthop route
    StaticRouteRequest *req =
        new StaticRouteRequest(type, table, route, StaticRoutePtr(this));
    manager_->EnqueueStaticRouteReq(req);

    return true;
}

//
// Build a Community for the given StaticRouteConfig.
// Always add the AcceptOwnNexthop community in addition to the configured
// list.
//
template <typename T>
CommunityPtr StaticRoute<T>::GetCommunity(const StaticRouteConfig &config) {
    CommunitySpec comm_spec;
    comm_spec.communities.push_back(CommunityType::AcceptOwnNexthop);
    for (vector<string>::const_iterator it = config.communities.begin();
         it != config.communities.end(); ++it) {
        uint32_t value = CommunityType::CommunityFromString(*it);
        if (!value)
            continue;
        comm_spec.communities.push_back(value);
    }
    CommunityDB *comm_db = routing_instance()->server()->comm_db();
    return comm_db->Locate(comm_spec);
}

//
// Build an updated ExtCommunity for the static route.
//
// Replace any RouteTargets with the list of RouteTargets for the StaticRoute.
// If the StaticRoute has an empty RouteTarget list, then we infer that this
// is not a snat use case and add the OriginVn as well. We don't want to add
// OriginVn in snat scenario because the route has to be imported into many
// VRFs and we want to set the OriginVn differently for each imported route.
//
template <typename T>
ExtCommunityPtr StaticRoute<T>::UpdateExtCommunity(const BgpAttr *attr) const {
    ExtCommunity::ExtCommunityList export_list;
    for (RouteTargetList::const_iterator it = rtarget_list().begin();
        it != rtarget_list().end(); it++) {
        export_list.push_back(it->GetExtCommunity());
    }

    BgpServer *server = routing_instance()->server();
    ExtCommunityDB *extcomm_db = server->extcomm_db();
    ExtCommunityPtr new_ext_community = extcomm_db->ReplaceRTargetAndLocate(
        attr->ext_community(), export_list);

    int vn_index = routing_instance()->virtual_network_index();
    if (export_list.empty() && vn_index) {
        OriginVn origin_vn(server->autonomous_system(), vn_index);
        new_ext_community = extcomm_db->ReplaceOriginVnAndLocate(
            new_ext_community.get(), origin_vn.GetExtCommunity());
    }
    return new_ext_community;
}

template <typename T>
void StaticRoute<T>::UpdateAttributes(const StaticRouteConfig &config) {
    rtarget_list_.clear();
    for (vector<string>::const_iterator it = config.route_targets.begin();
        it != config.route_targets.end(); ++it) {
        error_code ec;
        RouteTarget rtarget = RouteTarget::FromString(*it, &ec);
        if (ec != 0)
            continue;
        rtarget_list_.insert(rtarget);
    }
    community_ = GetCommunity(config);
}

// RemoveStaticRoute
template <typename T>
void StaticRoute<T>::RemoveStaticRoute() {
    CHECK_CONCURRENCY("bgp::StaticRoute");
    RouteT rt_key(prefix_);
    DBTablePartition *partition =
       static_cast<DBTablePartition *>(bgp_table()->GetTablePartition(&rt_key));
    BgpRoute *static_route =
        static_cast<BgpRoute *>(partition->Find(&rt_key));
    if (!static_route || static_route->IsDeleted()) return;

    for (NexthopPathIdList::iterator it = NexthopPathIds()->begin();
         it != NexthopPathIds()->end(); it++) {
        static_route->RemovePath(BgpPath::StaticRoute, NULL, *it);
        BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
            "Removed Static route path " << static_route->ToString() <<
            " path_id " << BgpPath::PathIdString(*it) <<
            " in table " << bgp_table()->name());
    }

    if (!static_route->BestPath()) {
        partition->Delete(static_route);
    } else {
        partition->Notify(static_route);
    }
}

// UpdateStaticRoute
template <typename T>
void StaticRoute<T>::UpdateStaticRoute() {
    CHECK_CONCURRENCY("bgp::Config");
    RouteT rt_key(prefix_);
    DBTablePartition *partition =
       static_cast<DBTablePartition *>(bgp_table()->GetTablePartition(&rt_key));
    BgpRoute *static_route =
        static_cast<BgpRoute *>(partition->Find(&rt_key));
    if (static_route == NULL) return;

    static_route->ClearDelete();

    BgpAttrDB *attr_db = routing_instance()->server()->attr_db();
    for (NexthopPathIdList::iterator it = NexthopPathIds()->begin();
         it != NexthopPathIds()->end(); it++) {
        BgpPath *existing_path =
            static_route->FindPath(BgpPath::StaticRoute, NULL, *it);
        BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
            "Update attributes of StaticRoute path "
            << static_route->ToString() << " path_id "
            << BgpPath::PathIdString(*it) << " in table "
            << bgp_table()->name());

        // Add the RouteTarget and OrignVn in the ExtCommunity attribute.
        ExtCommunityPtr ptr = UpdateExtCommunity(existing_path->GetAttr());
        BgpAttrPtr new_attr = attr_db->ReplaceExtCommunityAndLocate(
            existing_path->GetAttr(), ptr);

        // Use pre-calculated community from the StaticRoute.
        new_attr =
            attr_db->ReplaceCommunityAndLocate(new_attr.get(), community_);

        BgpPath *new_path =
            new BgpPath(*it, BgpPath::StaticRoute, new_attr.get(),
                        existing_path->GetFlags(), existing_path->GetLabel());

        static_route->RemovePath(BgpPath::StaticRoute, NULL, *it);

        static_route->InsertPath(new_path);
    }
    partition->Notify(static_route);
}

// AddStaticRoute
template <typename T>
void StaticRoute<T>::AddStaticRoute(NexthopPathIdList *old_path_ids) {
    CHECK_CONCURRENCY("bgp::StaticRoute");

    RouteT rt_key(prefix_);
    DBTablePartition *partition =
       static_cast<DBTablePartition *>(bgp_table()->GetTablePartition(&rt_key));
    BgpRoute *static_route =
        static_cast<BgpRoute *>(partition->Find(&rt_key));

    if (static_route == NULL) {
        static_route = new RouteT(prefix_);
        partition->Add(static_route);
    } else {
        static_route->ClearDelete();
    }

    BgpAttrDB *attr_db = routing_instance()->server()->attr_db();
    for (Route::PathList::iterator it = nexthop_route()->GetPathList().begin();
         it != nexthop_route()->GetPathList().end(); it++) {
        BgpPath *nexthop_route_path = static_cast<BgpPath *>(it.operator->());

        // Infeasible paths are not considered
        if (!nexthop_route_path->IsFeasible()) break;

        // take snapshot of all ECMP paths
        if (nexthop_route()->BestPath()->PathCompare(*nexthop_route_path, true))
            break;

        // Skip paths with duplicate forwarding information.  This ensures
        // that we generate only one path with any given next hop and label
        // when there are multiple nexthop paths from the original source
        // received via different peers e.g. directly via XMPP and via BGP.
        if (nexthop_route()->DuplicateForwardingPath(nexthop_route_path))
            continue;

        // Add the route target in the ExtCommunity attribute.
        ExtCommunityPtr ptr = UpdateExtCommunity(nexthop_route_path->GetAttr());
        BgpAttrPtr new_attr = attr_db->ReplaceExtCommunityAndLocate(
            nexthop_route_path->GetAttr(), ptr);

        // Use pre-calculated community from the static route.
        new_attr =
            attr_db->ReplaceCommunityAndLocate(new_attr.get(), community_);

        // Strip aspath. This is required when the nexthop route is learnt
        // via BGP.
        new_attr = attr_db->ReplaceAsPathAndLocate(new_attr.get(), AsPathPtr());

        // Replace the source rd if the nexthop path is a secondary path
        // of a primary path in the l3vpn table. Use the RD of the primary.
        if (nexthop_route_path->IsReplicated()) {
            const BgpSecondaryPath *spath =
                static_cast<const BgpSecondaryPath *>(nexthop_route_path);
            const RoutingInstance *ri = spath->src_table()->routing_instance();
            if (ri->IsMasterRoutingInstance()) {
                const VpnRouteT *vpn_route =
                    static_cast<const VpnRouteT *>(spath->src_rt());
                new_attr = attr_db->ReplaceSourceRdAndLocate(new_attr.get(),
                    vpn_route->GetPrefix().route_distinguisher());
            }
        }

        // Check whether we already have a path with the associated path id.
        uint32_t path_id =
            nexthop_route_path->GetAttr()->nexthop().to_v4().to_ulong();
        BgpPath *existing_path = static_route->FindPath(BgpPath::StaticRoute,
                                                        NULL, path_id);
        bool is_stale = false;
        if (existing_path != NULL) {
            if ((new_attr.get() != existing_path->GetAttr()) ||
                (nexthop_route_path->GetLabel() != existing_path->GetLabel())) {
                // Update Attributes and notify (if needed)
                is_stale = existing_path->IsStale();
                static_route->RemovePath(BgpPath::StaticRoute, NULL, path_id);
            } else {
                continue;
            }
        }

        BgpPath *new_path =
            new BgpPath(path_id, BgpPath::StaticRoute, new_attr.get(),
                nexthop_route_path->GetFlags(), nexthop_route_path->GetLabel());
        if (is_stale)
            new_path->SetStale();

        static_route->InsertPath(new_path);
        partition->Notify(static_route);

        BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
            "Added Static Route path " << static_route->ToString() <<
            " path_id " << BgpPath::PathIdString(path_id) <<
            " in table " << bgp_table()->name());
    }

    if (!old_path_ids) return;

    for (NexthopPathIdList::iterator it = old_path_ids->begin();
         it != old_path_ids->end(); it++) {
        if (NexthopPathIds()->find(*it) != NexthopPathIds()->end())
            continue;
        static_route->RemovePath(BgpPath::StaticRoute, NULL, *it);
        partition->Notify(static_route);

        BGP_LOG_STR(BgpMessage, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_TRACE,
            "Removed StaticRoute path " << static_route->ToString() <<
            " path_id " << BgpPath::PathIdString(*it) <<
            " in table " << bgp_table()->name());
    }
}

template <typename T>
void StaticRoute<T>::NotifyRoute() {
    RouteT rt_key(prefix_);
    DBTablePartition *partition =
       static_cast<DBTablePartition *>(bgp_table()->GetTablePartition(&rt_key));
    BgpRoute *static_route = static_cast<BgpRoute *>(partition->Find(&rt_key));
    if (!static_route)
        return;
    partition->Notify(static_route);
}

template <typename T>
bool StaticRoute<T>::IsPending() const {
    RouteT rt_key(prefix_);
    DBTablePartition *partition =
       static_cast<DBTablePartition *>(bgp_table()->GetTablePartition(&rt_key));
    const BgpRoute *route = static_cast<BgpRoute *>(partition->Find(&rt_key));
    return (!route || !route->FindPath(BgpPath::StaticRoute));
}

template <>
int StaticRouteMgr<StaticRouteInet>::static_route_task_id_ = -1;
template <>
int StaticRouteMgr<StaticRouteInet6>::static_route_task_id_ = -1;

template <typename T>
StaticRouteMgr<T>::StaticRouteMgr(RoutingInstance *rtinstance)
    : rtinstance_(rtinstance),
      listener_(rtinstance_->server()->condition_listener(GetFamily())),
      unregister_list_trigger_(new TaskTrigger(
        boost::bind(&StaticRouteMgr::ProcessUnregisterList, this),
        TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0)) {
    if (static_route_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        static_route_task_id_ = scheduler->GetTaskId("bgp::StaticRoute");
    }

    static_route_queue_ = new WorkQueue<StaticRouteRequest *>
        (static_route_task_id_, 0,
         boost::bind(&StaticRouteMgr::StaticRouteEventCallback, this, _1));
}

template <>
Address::Family StaticRouteMgr<StaticRouteInet>::GetFamily() const {
    return Address::INET;
}

template <>
Address::Family StaticRouteMgr<StaticRouteInet6>::GetFamily() const {
    return Address::INET6;
}

template <>
Ip4Address StaticRouteMgr<StaticRouteInet>::GetAddress(IpAddress addr) const {
    assert(addr.is_v4());
    return addr.to_v4();
}

template <>
Ip6Address StaticRouteMgr<StaticRouteInet6>::GetAddress(IpAddress addr) const {
    assert(addr.is_v6());
    return addr.to_v6();
}

template <typename T>
void StaticRouteMgr<T>::EnqueueStaticRouteReq(StaticRouteRequest *req) {
    static_route_queue_->Enqueue(req);
}

template <typename T>
bool StaticRouteMgr<T>::StaticRouteEventCallback(StaticRouteRequest *req) {
    CHECK_CONCURRENCY("bgp::StaticRoute");
    BgpTable *table = req->table_;
    BgpRoute *route = req->rt_;
    StaticRouteT *info = static_cast<StaticRouteT *>(req->info_.get());

    StaticRouteState *state = NULL;
    if (route) {
        state = static_cast<StaticRouteState *>
            (listener_->GetMatchState(table, route, info));
    }

    switch (req->type_) {
        case StaticRouteRequest::NEXTHOP_ADD_CHG: {
            assert(state);
            state->reset_deleted();
            if (route->IsDeleted() || !route->BestPath() ||
                !route->BestPath()->IsFeasible())  {
                break;
            }

            // Store the old path list
            typename StaticRouteT::NexthopPathIdList path_ids;
            path_ids.swap(*(info->NexthopPathIds()));

            // Populate the Nexthop PathID
            info->set_nexthop_route(route);

            info->AddStaticRoute(&path_ids);
            break;
        }
        case StaticRouteRequest::NEXTHOP_DELETE: {
            assert(state);
            info->RemoveStaticRoute();
            if (info->deleted() || route->IsDeleted()) {
                state->set_deleted();
            }
            info->set_nexthop_route(NULL);
            break;
        }
        default: {
            assert(0);
            break;
        }
    }

    if (state) {
        state->DecrementRefCnt();
        if (state->refcnt() == 0 && state->deleted()) {
            listener_->RemoveMatchState(table, route, info);
            delete state;
            if (!info->num_matchstate() && info->unregistered()) {
                UnregisterAndResolveStaticRoute(info);
            }
        }
    }

    delete req;
    return true;
}

template <typename T>
bool StaticRouteMgr<T>::ProcessUnregisterList() {
    CHECK_CONCURRENCY("bgp::Config");

    for (StaticRouteProcessList::iterator
         it = unregister_static_route_list_.begin();
         it != unregister_static_route_list_.end(); ++it) {
        StaticRouteT *info = static_cast<StaticRouteT *>(it->get());
        listener_->UnregisterMatchCondition(info->bgp_table(), info);
        static_route_map_.erase(info->prefix());
    }

    unregister_static_route_list_.clear();

    if (static_route_map_.empty()) {
        rtinstance_->server()->RemoveStaticRouteMgr(this);
    }

    if (!routing_instance()->deleted() &&
        routing_instance()->config()) {
        ProcessStaticRouteConfig();
    }
    return true;
}

template <typename T>
void StaticRouteMgr<T>::UnregisterAndResolveStaticRoute(StaticRoutePtr entry) {
    tbb::mutex::scoped_lock lock(mutex_);
    unregister_static_route_list_.insert(entry);
    unregister_list_trigger_->Set();
}

template <typename T>
void StaticRouteMgr<T>::LocateStaticRoutePrefix(
    const StaticRouteConfig &config) {
    CHECK_CONCURRENCY("bgp::Config");
    AddressT address = this->GetAddress(config.address);
    PrefixT prefix(address, config.prefix_length);

    // Verify whether the entry already exists
    typename StaticRouteMap::iterator it = static_route_map_.find(prefix);
    if (it != static_route_map_.end()) {
        // Wait for the delete complete cb
        if (it->second->deleted()) return;

        StaticRouteT *match =
            static_cast<StaticRouteT *>(it->second.get());
        // Check whether the config has got updated
        typename StaticRouteT::CompareResult change =
            match->CompareConfig(config);

        // StaticRoutePrefix is the key, it can't change.
        assert(change != StaticRouteT::PrefixChange);

        // Skip if there's no change.
        if (change == StaticRouteT::NoChange)
            return;

        // Update the attributes and any existing BgpPaths.
        if (change == StaticRouteT::AttributeChange) {
            match->UpdateAttributes(config);
            match->UpdateStaticRoute();
            return;
        }

        // If the nexthop changes, remove the static route, if already added.
        // To do this, remove match condition and wait for remove completion.
        BgpConditionListener::RequestDoneCb callback =
            boost::bind(&StaticRouteMgr::StopStaticRouteDone, this, _1, _2);
        listener_->RemoveMatchCondition(
            match->bgp_table(), it->second.get(), callback);
        return;
    }

    StaticRouteT *match =
        new StaticRouteT(routing_instance(), this, prefix, config);
    StaticRoutePtr static_route_match = StaticRoutePtr(match);

    if (static_route_map_.empty())
        rtinstance_->server()->InsertStaticRouteMgr(this);
    static_route_map_.insert(make_pair(prefix, static_route_match));

    listener_->AddMatchCondition(match->bgp_table(), static_route_match.get(),
                                BgpConditionListener::RequestDoneCb());
}

template <typename T>
void StaticRouteMgr<T>::StopStaticRouteDone(BgpTable *table,
                                             ConditionMatch *info) {
    CHECK_CONCURRENCY("db::DBTable");
    StaticRoute<T> *match = static_cast<StaticRoute<T> *>(info);
    match->set_unregistered();
    if (!match->num_matchstate() && match->unregistered()) {
        UnregisterAndResolveStaticRoute(match);
    }
    return;
}

template <typename T>
void StaticRouteMgr<T>::RemoveStaticRoutePrefix(const PrefixT &static_route) {
    CHECK_CONCURRENCY("bgp::Config");
    typename StaticRouteMap::iterator it = static_route_map_.find(static_route);
    if (it == static_route_map_.end()) return;

    if (it->second->deleted()) return;

    BgpConditionListener::RequestDoneCb callback =
        boost::bind(&StaticRouteMgr::StopStaticRouteDone, this, _1, _2);

    StaticRouteT *match = static_cast<StaticRouteT *>(it->second.get());
    listener_->RemoveMatchCondition(match->bgp_table(), match, callback);
}

template <typename T>
void StaticRouteMgr<T>::ProcessStaticRouteConfig() {
    CHECK_CONCURRENCY("bgp::Config");
    if (routing_instance()->deleted() || !routing_instance()->config()) return;
    const BgpInstanceConfig::StaticRouteList &list =
        routing_instance()->config()->static_routes(GetFamily());
    typedef BgpInstanceConfig::StaticRouteList::const_iterator iterator_t;
    for (iterator_t iter = list.begin(); iter != list.end(); ++iter) {
        LocateStaticRoutePrefix(*iter);
    }
}

template <typename T>
StaticRouteMgr<T>::~StaticRouteMgr() {
    if (static_route_queue_)
        delete static_route_queue_;
}

bool CompareStaticRouteConfig(const StaticRouteConfig &lhs,
                              const StaticRouteConfig &rhs) {
    BOOL_KEY_COMPARE(lhs.address, rhs.address);
    BOOL_KEY_COMPARE(lhs.prefix_length, rhs.prefix_length);
    return false;
}

template <typename T>
void StaticRouteMgr<T>::UpdateStaticRouteConfig() {
    CHECK_CONCURRENCY("bgp::Config");
    StaticRouteConfigList config_list =
        routing_instance()->config()->static_routes(GetFamily());
    sort(config_list.begin(), config_list.end(), CompareStaticRouteConfig);

    map_difference(&static_route_map_,
        config_list.begin(), config_list.end(),
        boost::bind(&StaticRouteMgr<T>::CompareStaticRoute, this, _1, _2),
        boost::bind(&StaticRouteMgr<T>::AddStaticRoute, this, _1),
        boost::bind(&StaticRouteMgr<T>::DelStaticRoute, this, _1),
        boost::bind(&StaticRouteMgr<T>::UpdateStaticRoute, this, _1, _2));
}

template <typename T>
void StaticRouteMgr<T>::FlushStaticRouteConfig() {
    CHECK_CONCURRENCY("bgp::Config");
    for (typename StaticRouteMap::iterator it = static_route_map_.begin();
         it != static_route_map_.end(); it++) {
        RemoveStaticRoutePrefix(it->first);
    }
}

template <typename T>
int StaticRouteMgr<T>::CompareStaticRoute(
    typename StaticRouteMap::iterator loc,
    StaticRouteConfigList::iterator it) {
    AddressT address = this->GetAddress(it->address);
    PrefixT prefix(address, it->prefix_length);
    KEY_COMPARE(loc->first, prefix);
    return 0;
}

template <typename T>
void StaticRouteMgr<T>::AddStaticRoute(StaticRouteConfigList::iterator it) {
    LocateStaticRoutePrefix(*it);
}

template <typename T>
void StaticRouteMgr<T>::DelStaticRoute(typename StaticRouteMap::iterator loc) {
    RemoveStaticRoutePrefix(loc->first);
}

template <typename T>
void StaticRouteMgr<T>::UpdateStaticRoute(typename StaticRouteMap::iterator loc,
    StaticRouteConfigList::iterator it) {
    LocateStaticRoutePrefix(*it);
}

template <typename T>
void StaticRouteMgr<T>::NotifyAllRoutes() {
    CHECK_CONCURRENCY("bgp::Config");
    for (typename StaticRouteMap::iterator it = static_route_map_.begin();
         it != static_route_map_.end(); ++it) {
        StaticRouteT *static_route =
             static_cast<StaticRouteT *>(it->second.get());
        static_route->NotifyRoute();
    }
}

template <typename T>
void StaticRouteMgr<T>::UpdateAllRoutes() {
    CHECK_CONCURRENCY("bgp::Config");
    for (typename StaticRouteMap::iterator it = static_route_map_.begin();
         it != static_route_map_.end(); ++it) {
        StaticRouteT *static_route =
             static_cast<StaticRouteT *>(it->second.get());
        static_route->UpdateStaticRoute();
    }
}

template <typename T>
void StaticRouteMgr<T>::DisableUnregisterTrigger() {
    unregister_list_trigger_->set_disable();
}

template <typename T>
void StaticRouteMgr<T>::EnableUnregisterTrigger() {
    unregister_list_trigger_->set_enable();
}

template <typename T>
uint32_t StaticRouteMgr<T>::GetRouteCount() const {
    CHECK_CONCURRENCY("bgp::Config");
    return static_route_map_.size();
}

template <typename T>
uint32_t StaticRouteMgr<T>::GetDownRouteCount() const {
    CHECK_CONCURRENCY("bgp::Config");
    uint32_t count = 0;
    for (typename StaticRouteMap::const_iterator it = static_route_map_.begin();
         it != static_route_map_.end(); ++it) {
        const StaticRouteT *static_route =
             static_cast<const StaticRouteT *>(it->second.get());
        if (static_route->IsPending())
            count++;
    }
    return count;
}

template <typename T> bool StaticRouteMgr<T>::FillStaticRouteInfo(
    RoutingInstance *ri, StaticRouteEntriesInfo *info) const {
    if (static_route_map_.empty())
        return false;

    info->set_ri_name(ri->name());
    for (typename StaticRouteMgr<T>::StaticRouteMap::const_iterator it =
         static_route_map_.begin(); it != static_route_map_.end(); ++it) {
        StaticRoute<T> *match =
            static_cast<StaticRoute<T> *>(it->second.get());
        StaticRouteInfo static_info;
        match->FillShowInfo(&static_info);
        info->static_route_list.push_back(static_info);
    }
    return true;
}

// Explicit instantiation of StaticRouteMgr for INET and INET6.
template class StaticRouteMgr<StaticRouteInet>;
template class StaticRouteMgr<StaticRouteInet6>;
