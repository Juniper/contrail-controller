/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_INSTANCE_ROUTE_AGGREGATE_H_
#define SRC_BGP_ROUTING_INSTANCE_ROUTE_AGGREGATE_H_

#include <tbb/mutex.h>

#include <map>
#include <set>

#include "bgp/routing-instance/iroute_aggregator.h"

#include "bgp/bgp_condition_listener.h"
#include "bgp/inet/inet_route.h"
#include "bgp/inet6/inet6_route.h"

class AggregateRouteConfig;

template <typename T> class AggregateRoute;

template <typename T1, typename T2, typename T3>
struct AggregateRouteBase {
  typedef T1 RouteT;
  typedef T2 PrefixT;
  typedef T3 AddressT;
};

class AggregateInetRoute : public AggregateRouteBase<
    InetRoute, Ip4Prefix, Ip4Address> {
};

class AggregateInet6Route : public AggregateRouteBase<
    Inet6Route, Inet6Prefix, Ip6Address> {
};

typedef ConditionMatchPtr AggregateRoutePtr;

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

    // Config
    virtual void ProcessAggregateRouteConfig();
    virtual void UpdateAggregateRouteConfig();
    virtual void FlushAggregateRouteConfig();

    const AggregateRouteMap &aggregate_route_map() const {
        return aggregate_route_map_;
    }

    Address::Family GetFamily() const;
    AddressT GetAddress(IpAddress addr) const;
    BgpTable *bgp_table() const;
    DBTableBase::ListenerId listener_id();

    bool MayDelete() const;
    void ManagedDelete();
    void RetryDelete();

    void EvaluateRouteAggregate(AggregateRoutePtr entry);
    void UnregisterAndResolveRouteAggregate(AggregateRoutePtr entry);

private:
    template <typename U> friend class AggregateRouteTest;
    class DeleteActor;
    typedef std::set<AggregateRoutePtr> AggregateRouteProcessList;

    void LocateAggregateRoutePrefix(const AggregateRouteConfig &cfg);
    void RemoveAggregateRoutePrefix(const PrefixT &static_route);
    void StopAggregateRouteDone(BgpTable *table, ConditionMatch *info);

    bool ProcessUnregisterResolveConfig();
    bool ProcessRouteAggregateUpdate();

    bool RouteListener(DBTablePartBase *root, DBEntryBase *entry);

    RoutingInstance *routing_instance() { return rtinstance_; }

    RoutingInstance *rtinstance_;
    BgpConditionListener *condition_listener_;
    DBTableBase::ListenerId listener_id_;
    AggregateRouteMap  aggregate_route_map_;
    boost::scoped_ptr<TaskTrigger> add_remove_contributing_route_trigger_;
    boost::scoped_ptr<TaskTrigger> resolve_trigger_;
    tbb::mutex mutex_;
    AggregateRouteProcessList update_aggregate_list_;
    AggregateRouteProcessList unregister_aggregate_list_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<RouteAggregator> instance_delete_ref_;

    DISALLOW_COPY_AND_ASSIGN(RouteAggregator);
};

typedef RouteAggregator<AggregateInetRoute> RouteAggregatorInet;
typedef RouteAggregator<AggregateInet6Route> RouteAggregatorInet6;

#endif  // SRC_BGP_ROUTING_INSTANCE_ROUTE_AGGREGATE_H_
