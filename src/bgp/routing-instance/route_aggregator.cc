/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/routing-instance/route_aggregator.h"

#include <boost/foreach.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "base/lifetime.h"
#include "base/map_util.h"
#include "base/task_annotations.h"
#include "base/task_trigger.h"
#include "bgp/bgp_server.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/path_resolver.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/route_aggregate_types.h"

using std::make_pair;
using std::string;

class RouteAggregatorState : public DBState {
public:
    RouteAggregatorState() : contributor_(false), aggregator_(false) {
    }

    void set_aggregating_info(AggregateRoutePtr aggregator) {
        assert(!aggregating_info_);
        aggregating_info_ = aggregator;
        aggregator_ = true;
    }

    void reset_aggregating_info() {
        assert(aggregating_info_);
        aggregating_info_ = NULL;
        aggregator_ = false;
    }

    void set_contributing_info(AggregateRoutePtr aggregator) {
        assert(!contributing_info_);
        contributing_info_ = aggregator;
        contributor_ = true;
    }

    void reset_contributing_info() {
        assert(contributing_info_);
        contributing_info_ = NULL;
        contributor_ = false;
    }

    AggregateRoutePtr contributing_info() {
        return contributing_info_;
    }

    AggregateRoutePtr aggregating_info() {
        return aggregating_info_;
    }

    bool contributor() const {
        return contributor_;
    }

    bool aggregator() const {
        return aggregator_;
    }

private:
    AggregateRoutePtr contributing_info_;
    bool contributor_;
    AggregateRoutePtr aggregating_info_;
    bool aggregator_;
    DISALLOW_COPY_AND_ASSIGN(RouteAggregatorState);
};

template <typename T>
class AggregateRoute : public ConditionMatch {
public:
    typedef typename T::TableT TableT;
    typedef typename T::RouteT RouteT;
    typedef typename T::PrefixT PrefixT;
    typedef typename T::AddressT AddressT;
    typedef RouteAggregator<T> AggregateRouteMgrT;
    // List of more specific routes resulted in Aggregate route PER PARTITION
    typedef std::set<BgpRoute *> RouteList;
    typedef std::vector<RouteList> ContributingRouteList;

    enum CompareResult {
        NoChange = 0,
        NexthopChange = 1,
    };

    AggregateRoute(RoutingInstance *rtinstance, AggregateRouteMgrT *manager,
        const PrefixT &aggregate_route, IpAddress nexthop);

    virtual ~AggregateRoute() {
        assert(!HasContributingRoutes());
    }

    Address::Family GetFamily() const { return manager_->GetFamily(); }
    AddressT GetAddress(IpAddress addr) const {
        return manager_->GetAddress(addr);
    }

    // Compare config and return whether cfg has updated
    CompareResult CompareConfig(const AggregateRouteConfig &cfg);

    const PrefixT &aggregate_route_prefix() const {
        return aggregate_route_prefix_;
    }

    RoutingInstance *routing_instance() const {
        return routing_instance_;
    }

    BgpTable *bgp_table() const {
        return routing_instance_->GetTable(this->GetFamily());
    }

    BgpRoute *aggregate_route() const {
        return aggregate_route_;
    }

    IpAddress nexthop() const {
        return nexthop_;
    }

    bool IsMoreSpecific(BgpRoute *route) const {
        const RouteT *ip_route = static_cast<RouteT *>(route);
        const PrefixT &ip_prefix = ip_route->GetPrefix();
        if (ip_prefix.addr() != GetAddress(nexthop()) &&
            ip_prefix != aggregate_route_prefix_ &&
            ip_prefix.IsMoreSpecific(aggregate_route_prefix_)) {
            return true;
        }
        return false;
    }

    bool IsOriginVnMatch(BgpRoute *route) const;
    bool IsBestMatch(BgpRoute *route) const;

    virtual bool Match(BgpServer *server, BgpTable *table,
                       BgpRoute *route, bool deleted);

    void UpdateNexthop(IpAddress nexthop) {
        nexthop_ = nexthop;
        UpdateAggregateRoute();
    }

    void AddAggregateRoute();
    void UpdateAggregateRoute();
    void RemoveAggregateRoute();

    void set_aggregate_route(BgpRoute *aggregate);

    virtual string ToString() const {
        return (string("AggregateRoute ") +
                aggregate_route_prefix().ToString());
    }

    ContributingRouteList *contribute_route_list() {
        return &contributors_;
    }

    const ContributingRouteList &contribute_route_list() const {
        return contributors_;
    }

    bool HasContributingRoutes() const {
        BOOST_FOREACH(RouteList per_part_contributor, contribute_route_list()) {
            if (!per_part_contributor.empty()) {
                return true;
            }
        }
        return false;
    }

    bool IsContributingRoute(BgpRoute *route) const {
        uint32_t part_id = route->get_table_partition()->index();
        return (contributors_[part_id].find(route) !=
                contributors_[part_id].end());
    }

    void NotifyContributingRoute(BgpRoute *route) {
        DBRequest req;
        req.oper = DBRequest::DB_ENTRY_NOTIFY;
        RouteT *ip_route = static_cast<RouteT *>(route);
        const PrefixT &prefix = ip_route->GetPrefix();
        req.key.reset(new typename TableT::RequestKey(prefix, NULL));
        bgp_table()->Enqueue(&req);
    }

    RouteAggregatorState *LocateRouteState(BgpRoute *route) {
        RouteAggregatorState *state = static_cast<RouteAggregatorState *>
            (route->GetState(bgp_table(), manager_->listener_id()));
        if (state == NULL) {
            state = new RouteAggregatorState();
            route->SetState(bgp_table(), manager_->listener_id(), state);
        }
        return state;
    }

    bool AddContributingRoute(BgpRoute *route) {
        uint32_t part_id = route->get_table_partition()->index();
        contributors_[part_id].insert(route);
        RouteAggregatorState *state = LocateRouteState(route);
        state->set_contributing_info(AggregateRoutePtr(this));
        NotifyContributingRoute(route);
        return (contributors_[part_id].size() == 1);
    }

    void ClearRouteState(BgpRoute *route, RouteAggregatorState *state) {
        if (!state->aggregator() && !state->contributor()) {
            route->ClearState(bgp_table(), manager_->listener_id());
            delete state;
        }
    }

    bool RemoveContributingRoute(BgpRoute *route) {
        uint32_t part_id = route->get_table_partition()->index();
        int num_deleted = contributors_[part_id].erase(route);
        RouteAggregatorState *state = static_cast<RouteAggregatorState *>
            (route->GetState(bgp_table(), manager_->listener_id()));
        if (state) {
            state->reset_contributing_info();
            ClearRouteState(route, state);
            NotifyContributingRoute(route);
        } else {
            assert(num_deleted != 1);
        }
        return contributors_[part_id].empty();
    }

    void FillShowInfo(AggregateRouteInfo *info, bool summary) const;

private:
    RoutingInstance *routing_instance_;
    AggregateRouteMgrT *manager_;
    PrefixT aggregate_route_prefix_;
    IpAddress nexthop_;
    BgpRoute *aggregate_route_;
    ContributingRouteList contributors_;

    DISALLOW_COPY_AND_ASSIGN(AggregateRoute);
};

template <typename T>
AggregateRoute<T>::AggregateRoute(RoutingInstance *rtinstance,
    AggregateRouteMgrT *manager, const PrefixT &aggregate_route,
    IpAddress nexthop)
    : routing_instance_(rtinstance),
      manager_(manager),
      aggregate_route_prefix_(aggregate_route),
      nexthop_(nexthop),
      aggregate_route_(NULL),
      contributors_(ContributingRouteList(DB::PartitionCount())) {
}

// Compare config and return whether cfg has updated
template <typename T>
typename AggregateRoute<T>::CompareResult AggregateRoute<T>::CompareConfig(
    const AggregateRouteConfig &cfg) {
    AddressT address = this->GetAddress(cfg.aggregate);
    PrefixT prefix(address, cfg.prefix_length);
    assert(aggregate_route_prefix_ == prefix);
    if (nexthop_ != cfg.nexthop) {
        return NexthopChange;
    }
    return NoChange;
}

template <typename T>
bool AggregateRoute<T>::IsOriginVnMatch(BgpRoute *route) const {
    const BgpPath *path = route->BestPath();
    const BgpAttr *attr = path->GetAttr();
    const ExtCommunity *ext_community = attr->ext_community();
    int vni = 0;
    if (ext_community) {
        BOOST_FOREACH(const ExtCommunity::ExtCommunityValue &comm,
                      ext_community->communities()) {
            if (!ExtCommunity::is_origin_vn(comm)) continue;
            OriginVn origin_vn(comm);
            vni = origin_vn.vn_index();
            break;
        }
    }

    if (!vni && path->IsVrfOriginated())
        vni = routing_instance()->virtual_network_index();

    if (vni == routing_instance()->GetOriginVnForAggregateRoute(GetFamily()))
        return true;

    return false;
}

//
// Calculate all aggregate prefixes to which the route can be contributing.
// We need to calculate the longest prefix to which this route belongs.
// E.g. routing instance is configured with 1/8, 1.1/16 and 1.1.1/24, 1.1.1.1/32
// should match 1.1.1/24. Similarly, 1.1.1/24 should be most specific to 1.1/16
// as so on
//
template <typename T>
bool AggregateRoute<T>::IsBestMatch(BgpRoute *route) const {
    const RouteT *ip_route = static_cast<RouteT *>(route);
    const PrefixT &ip_prefix = ip_route->GetPrefix();
    typename RouteAggregator<T>::AggregateRouteMap::const_iterator it;
    std::set<PrefixT> prefix_list;
    for (it = manager_->aggregate_route_map().begin();
         it != manager_->aggregate_route_map().end(); ++it) {
        if (!it->second->deleted() && ip_prefix != it->first &&
            ip_prefix.IsMoreSpecific(it->first)) {
            prefix_list.insert(it->first);
        }
    }
    // It should match atleast one prefix
    assert(prefix_list.size());
    //
    // Longest prefix matches the aggregate prefix of current AggregateRoute
    // return true to make this route as contributing route
    // Longest prefix is the last prefix in the set
    //
    if (*(prefix_list.rbegin()) == aggregate_route_prefix_) return true;
    return false;
}

// Match function called from BgpConditionListener
// Concurrency : db::DBTable
template <typename T>
bool AggregateRoute<T>::Match(BgpServer *server, BgpTable *table,
                   BgpRoute *route, bool deleted) {
    CHECK_CONCURRENCY("db::DBTable");

    //
    // Only interested routes
    // Should satisfy following conditions
    //   1. Origin VN should match origin VN of aggregated route
    //   2. Route should be more specific
    //
    if ((!deleted && !IsOriginVnMatch(route)) || !IsMoreSpecific(route))
        return false;

    if (!deleted) {
        //
        // If the route is already contributing, check whether it is still
        // most specific aggregate prefix. Else remove the route as contributing
        // route. As part of the notification, route will become contributing to
        // most specific aggregate route prefix.
        //
        if (IsContributingRoute(route)) {
            if (!IsBestMatch(route)) deleted = true;
        } else if (table->IsContributingRoute(route)) {
            //
            // If the route is already contributing route of other aggregate
            // prefix of this bgp-table, ignore it
            //
            return false;
        }
    }

    //
    // Consider route only if it matches most specific aggregate prefix
    // configured on the routing instance. e.g. if routing instance has
    // following prefixes configured, 1/8, 1.1/16 and 1.1.1/24,
    // 1.1.1.1/32 should match to 1.1.1/24 as most specific route.
    //
    if (!deleted && !IsBestMatch(route)) return false;

    BgpConditionListener *listener = server->condition_listener(GetFamily());
    bool state_added = listener->CheckMatchState(table, route, this);
    bool trigger_eval = false;
    if (!deleted) {
        if (!state_added) {
            listener->SetMatchState(table, route, this);
            trigger_eval = AddContributingRoute(route);
        }
    } else {
        if (!state_added) {
            // Not seen ADD ignore DELETE
            return false;
        }
        trigger_eval = RemoveContributingRoute(route);
        listener->RemoveMatchState(table, route, this);
    }

    if (trigger_eval) manager_->EvaluateAggregateRoute(this);
    return true;
}

// AddAggregateRoute
template <typename T>
void AggregateRoute<T>::AddAggregateRoute() {
    CHECK_CONCURRENCY("bgp::RouteAggregation");

    RouteT rt_key(aggregate_route_prefix());
    DBTablePartition *partition =
       static_cast<DBTablePartition *>(bgp_table()->GetTablePartition(&rt_key));
    BgpRoute *aggregate_route =
        static_cast<BgpRoute *>(partition->Find(&rt_key));

    if (aggregate_route == NULL) {
        aggregate_route = new RouteT(aggregate_route_prefix());
        partition->Add(aggregate_route);
    } else {
        aggregate_route->ClearDelete();
    }

    BgpPath *existing_path = aggregate_route->FindPath(BgpPath::Aggregate, 0);
    assert(existing_path == NULL);

    BgpAttrSpec attrs;
    BgpAttrNextHop attr_nexthop(this->GetAddress(nexthop()));
    attrs.push_back(&attr_nexthop);
    ExtCommunitySpec extcomm_spec;
    OriginVn origin_vn(routing_instance()->server()->autonomous_system(),
        routing_instance()->GetOriginVnForAggregateRoute(GetFamily()));
    extcomm_spec.communities.push_back(origin_vn.GetExtCommunityValue());
    attrs.push_back(&extcomm_spec);
    BgpAttrPtr attr = routing_instance()->server()->attr_db()->Locate(attrs);
    BgpPath *new_path = new BgpPath(BgpPath::Aggregate,
                                    attr.get(), BgpPath::ResolveNexthop, 0);
    bgp_table()->path_resolver()->StartPathResolution(partition->index(),
                                                     new_path, aggregate_route);
    aggregate_route->InsertPath(new_path);
    partition->Notify(aggregate_route);
    set_aggregate_route(aggregate_route);
}

// UpdateAggregateRoute
template <typename T>
void AggregateRoute<T>::UpdateAggregateRoute() {
    CHECK_CONCURRENCY("bgp::Config");

    if (aggregate_route_ == NULL) return;

    DBTablePartition *partition = static_cast<DBTablePartition *>
        (bgp_table()->GetTablePartition(aggregate_route_));

    aggregate_route_->ClearDelete();

    BgpPath *existing_path = aggregate_route_->FindPath(BgpPath::Aggregate, 0);
    if (existing_path)
        bgp_table()->path_resolver()->StopPathResolution(partition->index(),
                                                         existing_path);
    aggregate_route_->RemovePath(BgpPath::Aggregate);

    BgpAttrSpec attrs;
    BgpAttrNextHop attr_nexthop(this->GetAddress(nexthop()));
    attrs.push_back(&attr_nexthop);
    ExtCommunitySpec extcomm_spec;
    OriginVn origin_vn(routing_instance()->server()->autonomous_system(),
        routing_instance()->GetOriginVnForAggregateRoute(GetFamily()));
    extcomm_spec.communities.push_back(origin_vn.GetExtCommunityValue());
    attrs.push_back(&extcomm_spec);
    BgpAttrPtr attr = routing_instance()->server()->attr_db()->Locate(attrs);
    BgpPath *new_path = new BgpPath(BgpPath::Aggregate,
                                    attr.get(), BgpPath::ResolveNexthop, 0);
    bgp_table()->path_resolver()->StartPathResolution(partition->index(),
                                                    new_path, aggregate_route_);
    aggregate_route_->InsertPath(new_path);

    partition->Notify(aggregate_route_);
}

// RemoveAggregateRoute
template <typename T>
void AggregateRoute<T>::RemoveAggregateRoute() {
    CHECK_CONCURRENCY("bgp::RouteAggregation");
    BgpRoute *aggregate_route = aggregate_route_;
    if (!aggregate_route) return;

    DBTablePartition *partition = static_cast<DBTablePartition *>
        (bgp_table()->GetTablePartition(aggregate_route_));

    BgpPath *existing_path =
        aggregate_route->FindPath(BgpPath::Aggregate, 0);
    assert(existing_path != NULL);

    bgp_table()->path_resolver()->StopPathResolution(partition->index(),
                                                     existing_path);
    aggregate_route->RemovePath(BgpPath::Aggregate);

    if (!aggregate_route->BestPath()) {
        partition->Delete(aggregate_route);
    } else {
        partition->Notify(aggregate_route);
    }
    set_aggregate_route(NULL);
}

template <typename T>
void AggregateRoute<T>::set_aggregate_route(BgpRoute *aggregate) {
    if (aggregate) {
        assert(aggregate_route_ == NULL);
        RouteAggregatorState *state = LocateRouteState(aggregate);
        state->set_aggregating_info(AggregateRoutePtr(this));
    } else {
        assert(aggregate_route_ != NULL);
        RouteAggregatorState *state = static_cast<RouteAggregatorState *>
            (aggregate_route_->GetState(bgp_table(), manager_->listener_id()));
        assert(state);
        state->reset_aggregating_info();
        ClearRouteState(aggregate_route_, state);
    }
    aggregate_route_ = aggregate;
}

template <typename T>
void AggregateRoute<T>::FillShowInfo(AggregateRouteInfo *info,
    bool summary) const {
    BgpTable *table = bgp_table();
    info->set_deleted(deleted());
    info->set_prefix(aggregate_route_prefix_.ToString());
    if (aggregate_route_) {
        ShowRouteBrief show_route;
        aggregate_route_->FillRouteInfo(table, &show_route);
        info->set_aggregate_rt(show_route);
    }

    info->set_nexthop(nexthop_.to_string());

    if (summary)
        return;

    std::vector<string> contributor_list;
    BOOST_FOREACH(const RouteList &list, contribute_route_list()) {
        BOOST_FOREACH(BgpRoute *rt, list) {
            contributor_list.push_back(rt->ToString());
        }
    }
    info->set_contributors(contributor_list);
}

template <typename T>
class RouteAggregator<T>::DeleteActor : public LifetimeActor {
public:
    explicit DeleteActor(RouteAggregator *aggregator) :
    LifetimeActor(aggregator->routing_instance()->server()->lifetime_manager()),
    aggregator_(aggregator) {
    }
    virtual ~DeleteActor() {
    }

    virtual bool MayDelete() const {
        return aggregator_->MayDelete();
    }

    virtual void Destroy() {
        aggregator_->routing_instance()->DestroyRouteAggregator(
                                                    aggregator_->GetFamily());
    }

private:
    RouteAggregator *aggregator_;
};

template <typename T>
RouteAggregator<T>::RouteAggregator(RoutingInstance *rtinstance)
  : rtinstance_(rtinstance),
    condition_listener_(rtinstance_->server()->condition_listener(GetFamily())),
    listener_id_(DBTableBase::kInvalidId),
    update_list_trigger_(new TaskTrigger(
        boost::bind(&RouteAggregator::ProcessUpdateList, this),
        TaskScheduler::GetInstance()->GetTaskId("bgp::RouteAggregation"),
        0)),
    unregister_list_trigger_(new TaskTrigger(
        boost::bind(&RouteAggregator::ProcessUnregisterList, this),
        TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0)),
    deleter_(new DeleteActor(this)),
    instance_delete_ref_(this, rtinstance->deleter()) {
}

template <typename T>
RouteAggregator<T>::~RouteAggregator() {
    if (listener_id_ != DBTableBase::kInvalidId)
        bgp_table()->Unregister(listener_id_);
    listener_id_ = DBTableBase::kInvalidId;
}

template <typename T>
void RouteAggregator<T>::ProcessAggregateRouteConfig() {
    CHECK_CONCURRENCY("bgp::Config");
    const AggregateRouteConfigList &list =
        routing_instance()->config()->aggregate_routes(GetFamily());
    typedef AggregateRouteConfigList::const_iterator iterator_t;
    for (iterator_t iter = list.begin(); iter != list.end(); ++iter) {
        LocateAggregateRoutePrefix(*iter);
    }
}

bool CompareAggregateRouteConfig(const AggregateRouteConfig &lhs,
                                 const AggregateRouteConfig &rhs) {
    BOOL_KEY_COMPARE(lhs.aggregate, rhs.aggregate);
    BOOL_KEY_COMPARE(lhs.prefix_length, rhs.prefix_length);
    return false;
}

template <typename T>
void RouteAggregator<T>::UpdateAggregateRouteConfig() {
    CHECK_CONCURRENCY("bgp::Config");
    AggregateRouteConfigList config_list =
        routing_instance()->config()->aggregate_routes(GetFamily());
    sort(config_list.begin(), config_list.end(), CompareAggregateRouteConfig);

    map_difference(&aggregate_route_map_,
        config_list.begin(), config_list.end(),
        boost::bind(&RouteAggregator<T>::CompareAggregateRoute, this, _1, _2),
        boost::bind(&RouteAggregator<T>::AddAggregateRoute, this, _1),
        boost::bind(&RouteAggregator<T>::DelAggregateRoute, this, _1),
        boost::bind(&RouteAggregator<T>::UpdateAggregateRoute, this, _1, _2));
}

template <typename T>
void RouteAggregator<T>::FlushAggregateRouteConfig() {
    CHECK_CONCURRENCY("bgp::Config");
    for (typename AggregateRouteMap::iterator it = aggregate_route_map_.begin();
         it != aggregate_route_map_.end(); it++) {
        RemoveAggregateRoutePrefix(it->first);
    }
}

template <>
Address::Family RouteAggregator<AggregateInetRoute>::GetFamily() const {
    return Address::INET;
}

template <>
Address::Family RouteAggregator<AggregateInet6Route>::GetFamily() const {
    return Address::INET6;
}

template <>
Ip4Address RouteAggregator<AggregateInetRoute>::GetAddress(IpAddress addr)
    const {
    assert(addr.is_v4());
    return addr.to_v4();
}

template <>
Ip6Address RouteAggregator<AggregateInet6Route>::GetAddress(IpAddress addr)
    const {
    assert(addr.is_v6());
    return addr.to_v6();
}

template <typename T>
BgpTable *RouteAggregator<T>::bgp_table() const {
    return rtinstance_->GetTable(GetFamily());
}

template <typename T>
void RouteAggregator<T>::Initialize() {
    // Register to the table before adding first match condition
    listener_id_ = bgp_table()->Register(
         boost::bind(&RouteAggregator::RouteListener, this, _1, _2),
         "RouteAggregator");
}

template <typename T>
bool RouteAggregator<T>::MayDelete() const {
    if (!aggregate_route_map_.empty())
        return false;
    if (!update_aggregate_list_.empty())
        return false;
    if (!unregister_aggregate_list_.empty())
        return false;
    return true;
}

// Cascade delete from RoutingInstance delete_ref to self.
template <typename T>
void RouteAggregator<T>::ManagedDelete() {
    deleter_->Delete();
}

// Attempt to enqueue a delete for the RouteAggregator.
template <typename T>
void RouteAggregator<T>::RetryDelete() {
    if (!deleter_->IsDeleted())
        return;
    deleter_->RetryDelete();
}

template <typename T>
void RouteAggregator<T>::EvaluateAggregateRoute(AggregateRoutePtr entry) {
    tbb::mutex::scoped_lock lock(mutex_);
    update_aggregate_list_.insert(entry);
    update_list_trigger_->Set();
}

template <typename T>
void RouteAggregator<T>::UnregisterAndResolveRouteAggregate(
                                                    AggregateRoutePtr entry) {
    tbb::mutex::scoped_lock lock(mutex_);
    unregister_aggregate_list_.insert(entry);
    unregister_list_trigger_->Set();
}

template <typename T>
bool RouteAggregator<T>::IsAggregateRoute(const BgpRoute *route) const {
    RouteAggregatorState *state = static_cast<RouteAggregatorState *>
        (route->GetState(bgp_table(), listener_id()));
    if (state) {
        return (state->aggregator());
    }
    return false;
}

template <typename T>
bool RouteAggregator<T>::IsContributingRoute(const BgpRoute *route) const {
    RouteAggregatorState *state = static_cast<RouteAggregatorState *>
        (route->GetState(bgp_table(), listener_id()));
    if (state) {
        return state->contributor();
    }
    return false;
}

template <typename T>
bool RouteAggregator<T>::FillAggregateRouteInfo(AggregateRouteEntriesInfo *info,
    bool summary) const {
    if (aggregate_route_map().empty())
        return false;

    info->set_name(rtinstance_->name());
    for (typename AggregateRouteMap::const_iterator it =
         aggregate_route_map_.begin(); it != aggregate_route_map_.end(); it++) {
        AggregateRouteT *aggregate =
            static_cast<AggregateRouteT *>(it->second.get());
        AggregateRouteInfo aggregate_info;
        aggregate->FillShowInfo(&aggregate_info, summary);
        info->aggregate_route_list.push_back(aggregate_info);
    }
    return true;
}

template <typename T>
int RouteAggregator<T>::CompareAggregateRoute(
    typename AggregateRouteMap::iterator loc,
    AggregateRouteConfigList::iterator it) {
    AddressT address = this->GetAddress(it->aggregate);
    PrefixT prefix(address, it->prefix_length);
    KEY_COMPARE(loc->first, prefix);
    return 0;
}

template <typename T>
void RouteAggregator<T>::AddAggregateRoute(
    AggregateRouteConfigList::iterator it) {
    LocateAggregateRoutePrefix(*it);
}

template <typename T>
void RouteAggregator<T>::DelAggregateRoute(
    typename AggregateRouteMap::iterator loc) {
    RemoveAggregateRoutePrefix(loc->first);
}

template <typename T>
void RouteAggregator<T>::UpdateAggregateRoute(
    typename AggregateRouteMap::iterator loc,
    AggregateRouteConfigList::iterator it) {
    LocateAggregateRoutePrefix(*it);
}

template <typename T>
void RouteAggregator<T>::LocateAggregateRoutePrefix(const AggregateRouteConfig
                                                    &cfg) {
    CHECK_CONCURRENCY("bgp::Config");
    AddressT address = this->GetAddress(cfg.aggregate);
    PrefixT prefix(address, cfg.prefix_length);

    // Verify whether the entry already exists
    typename AggregateRouteMap::iterator it = aggregate_route_map_.find(prefix);
    if (it != aggregate_route_map_.end()) {
        // Wait for the delete complete cb
        if (it->second->deleted()) return;

        AggregateRouteT *match =
            static_cast<AggregateRouteT *>(it->second.get());
        // Check whether the config has got updated
        typename AggregateRouteT::CompareResult change =
            match->CompareConfig(cfg);
        // No change..
        if (change == AggregateRouteT::NoChange) return;

        if (change == AggregateRouteT::NexthopChange)
            match->UpdateNexthop(cfg.nexthop);
        return;
    }

    AggregateRouteT *match =
        new AggregateRouteT(routing_instance(), this, prefix, cfg.nexthop);
    AggregateRoutePtr aggregate_route_match = AggregateRoutePtr(match);
    aggregate_route_map_.insert(make_pair(prefix, aggregate_route_match));

    condition_listener_->AddMatchCondition(match->bgp_table(),
           aggregate_route_match.get(), BgpConditionListener::RequestDoneCb());
    return;
}

template <typename T>
void RouteAggregator<T>::RemoveAggregateRoutePrefix(const PrefixT &aggregate) {
    CHECK_CONCURRENCY("bgp::Config");
    typename AggregateRouteMap::iterator it =
        aggregate_route_map_.find(aggregate);
    if (it == aggregate_route_map_.end()) return;
    if (it->second->deleted()) return;

    BgpConditionListener::RequestDoneCb callback =
        boost::bind(&RouteAggregator::StopAggregateRouteDone, this, _1, _2);

    AggregateRouteT *match = static_cast<AggregateRouteT *>(it->second.get());
    condition_listener_->RemoveMatchCondition(match->bgp_table(),
                                              match, callback);
}

template <typename T>
void RouteAggregator<T>::StopAggregateRouteDone(BgpTable *table,
                                             ConditionMatch *info) {
    CHECK_CONCURRENCY("db::DBTable");
    UnregisterAndResolveRouteAggregate(info);
    return;
}

template <typename T>
bool RouteAggregator<T>::ProcessUnregisterList() {
    CHECK_CONCURRENCY("bgp::Config");

    for (AggregateRouteProcessList::iterator
         it = unregister_aggregate_list_.begin();
         it != unregister_aggregate_list_.end(); ++it) {
        AggregateRouteT *aggregate = static_cast<AggregateRouteT *>(it->get());
        aggregate_route_map_.erase(aggregate->aggregate_route_prefix());
        condition_listener_->UnregisterMatchCondition(aggregate->bgp_table(),
                                                      aggregate);
    }

    unregister_aggregate_list_.clear();

    if (!routing_instance()->deleted() && routing_instance()->config())
        ProcessAggregateRouteConfig();

    if (MayDelete()) RetryDelete();
    return true;
}

template <typename T>
bool RouteAggregator<T>::ProcessUpdateList() {
    CHECK_CONCURRENCY("bgp::RouteAggregation");

    for (AggregateRouteProcessList::iterator
         it = update_aggregate_list_.begin();
         it != update_aggregate_list_.end(); ++it) {
        AggregateRouteT *aggregate = static_cast<AggregateRouteT *>(it->get());
        if (aggregate->aggregate_route()) {
            if (!aggregate->HasContributingRoutes())
                aggregate->RemoveAggregateRoute();
        } else {
            if (aggregate->HasContributingRoutes())
                aggregate->AddAggregateRoute();
        }
    }

    update_aggregate_list_.clear();

    if (MayDelete()) RetryDelete();
    return true;
}

// Need this to store the aggregate info in aggregated route as DBState
template <typename T>
bool RouteAggregator<T>::RouteListener(DBTablePartBase *root,
                                       DBEntryBase *entry) {
    return true;
}

// Enable/Disable task triggers
template <typename T>
void RouteAggregator<T>::DisableRouteAggregateUpdate() {
    update_list_trigger_->set_disable();
}

template <typename T>
void RouteAggregator<T>::EnableRouteAggregateUpdate() {
    update_list_trigger_->set_enable();
}

template <typename T>
size_t RouteAggregator<T>::GetUpdateAggregateListSize() const {
    return update_aggregate_list_.size();
}

template <typename T>
void RouteAggregator<T>::DisableUnregResolveTask() {
    unregister_list_trigger_->set_disable();
}

template <typename T>
void RouteAggregator<T>::EnableUnregResolveTask() {
    unregister_list_trigger_->set_enable();
}

template <typename T>
size_t RouteAggregator<T>::GetUnregResolveListSize() const {
    return unregister_aggregate_list_.size();
}

// Explicit instantiation of RouteAggregator for INET and INET6.
template class RouteAggregator<AggregateInetRoute>;
template class RouteAggregator<AggregateInet6Route>;
