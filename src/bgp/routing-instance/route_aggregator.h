/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_INSTANCE_ROUTE_AGGREGATOR_H_
#define SRC_BGP_ROUTING_INSTANCE_ROUTE_AGGREGATOR_H_

#include <tbb/mutex.h>

#include <map>
#include <set>

#include "bgp/routing-instance/iroute_aggregator.h"

#include "bgp/bgp_condition_listener.h"
#include "bgp/bgp_config.h"
#include "bgp/inet/inet_route.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_route.h"
#include "bgp/inet6/inet6_table.h"

class AggregateRouteConfig;

template <typename T> class AggregateRoute;

template <typename T1, typename T2, typename T3, typename T4>
struct AggregateRouteBase {
  typedef T1 TableT;
  typedef T2 RouteT;
  typedef T3 PrefixT;
  typedef T4 AddressT;
};

class AggregateInetRoute : public AggregateRouteBase<
    InetTable, InetRoute, Ip4Prefix, Ip4Address> {
};

class AggregateInet6Route : public AggregateRouteBase<
    Inet6Table, Inet6Route, Inet6Prefix, Ip6Address> {
};

typedef ConditionMatchPtr AggregateRoutePtr;

//
// RouteAggregator
// ================
//
// This class implements the route aggregation for control node. It provides
// APIs to create/delete/update route aggregation config for a routing instance
// An object of this class for the address families that supports route
// aggregation is hooked to routing instance. Currently route aggregation is
// supported for INET and INET6 address family. Support for multiple address
// family is implemented with template for each address family
//
// RouteAggregator uses BgpConditionListener to track more specific routes of
// the configured aggregate prefix and PathResolver to resolve nexthop for
// aggregate route
//
// RouteAggregator uses AddMatchCondition method of BgpConditionListener when
// new route-aggregate prefix is created on routing instance.
//
// AggregateRoute
// ================
//
// AggregateRoute class implements the MatchCondition for BgpConditionListener
// and implements the Match() to detect the more specific route.
// RouteAggregator stores the match object, AggregateRoute, in
// aggregate_route_map_.
//
// AggregateRoute class stores the contributing routes in "contributors_" list.
// Match is executed in db::DBTable task in each partition context.
// The contributing routes are maintained per partition to ensure concurrent
// access to contributing routes.
//
// On the successful match, AggregateRoute calls AddContributingRoute or
// RemoveContributingRoute based the state and puts the AggregateRoute object
// in update_aggregate_list_ and trigger update_list_trigger_
// task trigger to process the aggregate route.
//
// In task trigger method for update_list_trigger_ is
// responsible for creating and deleting the Aggregate route.
// Aggregate route is added when first contributing route is added to
// contributors_ and removed when last contributing route is removed
//
// RouteAggregator creates the aggregate route with Aggregate as
// path source and ResolveNexthop as flags. The BgpAttribute on the aggregate
// route contains all the property as specified in the config. Currently,
// config supports specifying only the nexthop for the aggregate route.
// The "ResolveNexthop" flag on the route triggers the PathResolver module to
// resolve the corresponding nexthop and create path with all forwarding info
// based on nexthop specified by config.
//
// AggregateRoute also stores the resulting aggregate route in the object.
//
// Update of the route-aggregate config:
// ====================================
//
// AggregateRoute provides "UpdateNexthop" method. This method is invoked when
// nexthop is updated in route-aggregate config object. This method deletes the
// executing RouteAggregation path(and stop path resolution) and invokes path
// resolution after updating the BgpAttribute with new nexthop
//
// Delete of the route-aggregate config:
// ====================================
//
// When route-aggregate is removed from the routing instance for a given prefix,
// RouteAggregator invokes RemoveMatchCondition to initiate the delete process.
// StopAggregateRouteDone callback indicates the RouteAggregator about
// completion of remove process and it triggers unregister_list_trigger_ to
// unregister the match condition. StopAggregateRouteDone callback puts the
// AggregateRoute object in unregister_aggregate_list_.
//
// When the route-aggregate for a given prefix is in delete process, the
// AggregateRoute is still maintained in the aggregate_route_map_. This will
// avoid/ensure new route-aggregate config with same object is handled only
// after successful delete completion of previous AggregateRoute match object
// unregister_list_trigger_ is executed in bgp::Config task and walks the
// unregister_aggregate_list_ to complete the deletion process by calling
// UnregisterMatchCondition(). It also triggers ProcessAggregateRouteConfig to
// apply new config for pending delete prefixes.
//
// DBState: RouteAggregatorState:
// ============================
//
// RouteAggregator registers with the BgpTable to set the DBState.
// The RouteAggregatorState implements the DBState.
// The DBState is added on both matching/contributing route and aggregate route.
// A route can be both contributing and aggregating route at the same time.
// So two boolean fields are used to indicate the state of the route.
// IsContributingRoute()/IsAggregateRoute API exposed by the RouteAggregator
// access the DBState on the BgpRoute to return the state.
// In addition to the boolean, a reference to the match object is stored in the
// DBState. "aggregating_info_" is the match condition object valid for
// aggregating route and "contributing_info_" refers to the match condition to
// which the route is contributing.
//
// Lifetime management
// ===================
//
// RouteAggregator takes a delete reference to the parent routing instance and
// implements a DeleteActor to manage deletion
// MayDelete() method of DeleteActor for RouteAggregator returns false till
//    1. aggregate_route_map_ is not empty [To check whether config is deleted]
//    2. update_aggregate_list_ is not empty [contributing routes are processed]
//    3. unregister_aggregate_list_ is not empty [unregister of Match condition
//       is complete]
// Task triggers update_list_trigger_ and unregister_list_trigger_ will
// call RetryDelete() to complete the delete RouteAggregator object
//
// Concurrency
// ===========
// bgp::RouteAggregation task runs in exclusion to any task that adds/deletes
// path from route. i.e. db::DBTable, bgp::ServiceChain, bgp::StaticRoute and
// bgp::ResolverPath.
// bgp::RouteAggregation runs in exclusion to bgp::Config task
// Match() function of the AggregateRoute class is run in per partition
// db::DBTable task. Hence the "contributors_" maintains the contributing routes
// in per partition list to allow concurrent access
//
template <typename T>
class RouteAggregator : public IRouteAggregator {
public:
    typedef typename T::RouteT RouteT;
    typedef typename T::PrefixT PrefixT;
    typedef typename T::AddressT AddressT;
    typedef AggregateRoute<T> AggregateRouteT;

    // Map of AggregateRoute prefix to the AggregateRoute match object
    typedef std::map<PrefixT, AggregateRoutePtr> AggregateRouteMap;

    explicit RouteAggregator(RoutingInstance *instance);
    ~RouteAggregator();

    virtual void Initialize();

    // Config
    virtual void ProcessAggregateRouteConfig();
    virtual void UpdateAggregateRouteConfig();
    virtual void FlushAggregateRouteConfig();

    virtual uint32_t GetAggregateRouteCount() const {
        return aggregate_route_map_.size();
    }
    const AggregateRouteMap &aggregate_route_map() const {
        return aggregate_route_map_;
    }

    Address::Family GetFamily() const;
    AddressT GetAddress(IpAddress addr) const;
    BgpTable *bgp_table() const;
    DBTableBase::ListenerId listener_id() const {
        return listener_id_;
    }

    bool MayDelete() const;
    void ManagedDelete();
    void RetryDelete();

    void EvaluateAggregateRoute(AggregateRoutePtr entry);
    void UnregisterAndResolveRouteAggregate(AggregateRoutePtr entry);

    virtual bool IsAggregateRoute(const BgpRoute *route) const;
    virtual bool IsContributingRoute(const BgpRoute *route) const;

    virtual bool FillAggregateRouteInfo(AggregateRouteEntriesInfo *info,
        bool summary) const;

private:
    class DeleteActor;
    typedef std::set<AggregateRoutePtr> AggregateRouteProcessList;
    typedef BgpInstanceConfig::AggregateRouteList AggregateRouteConfigList;

    int CompareAggregateRoute(typename AggregateRouteMap::iterator loc,
        AggregateRouteConfigList::iterator it);
    void AddAggregateRoute(AggregateRouteConfigList::iterator it);
    void DelAggregateRoute(typename AggregateRouteMap::iterator loc);
    void UpdateAggregateRoute(typename AggregateRouteMap::iterator loc,
        AggregateRouteConfigList::iterator it);

    void LocateAggregateRoutePrefix(const AggregateRouteConfig &cfg);
    void RemoveAggregateRoutePrefix(const PrefixT &static_route);
    void StopAggregateRouteDone(BgpTable *table, ConditionMatch *info);

    bool ProcessUnregisterList();
    bool ProcessUpdateList();

    bool RouteListener(DBTablePartBase *root, DBEntryBase *entry);

    RoutingInstance *routing_instance() { return rtinstance_; }

    // Enable/Disable task triggers
    virtual void DisableRouteAggregateUpdate();
    virtual void EnableRouteAggregateUpdate();
    virtual size_t GetUpdateAggregateListSize() const;

    virtual void DisableUnregResolveTask();
    virtual void EnableUnregResolveTask();
    virtual size_t GetUnregResolveListSize() const;

    RoutingInstance *rtinstance_;
    BgpConditionListener *condition_listener_;
    DBTableBase::ListenerId listener_id_;
    AggregateRouteMap  aggregate_route_map_;
    boost::scoped_ptr<TaskTrigger> update_list_trigger_;
    boost::scoped_ptr<TaskTrigger> unregister_list_trigger_;
    tbb::mutex mutex_;
    AggregateRouteProcessList update_aggregate_list_;
    AggregateRouteProcessList unregister_aggregate_list_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<RouteAggregator> instance_delete_ref_;

    DISALLOW_COPY_AND_ASSIGN(RouteAggregator);
};

typedef RouteAggregator<AggregateInetRoute> RouteAggregatorInet;
typedef RouteAggregator<AggregateInet6Route> RouteAggregatorInet6;

#endif  // SRC_BGP_ROUTING_INSTANCE_ROUTE_AGGREGATOR_H_
