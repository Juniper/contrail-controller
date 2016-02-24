/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_INSTANCE_STATIC_ROUTE_H_
#define SRC_BGP_ROUTING_INSTANCE_STATIC_ROUTE_H_

#include <map>
#include <set>

#include "bgp/routing-instance/istatic_route_mgr.h"

#include "base/queue_task.h"
#include "bgp/bgp_condition_listener.h"
#include "bgp/bgp_config.h"
#include "bgp/inet/inet_route.h"
#include "bgp/inet6/inet6_route.h"

class InetVpnRoute;
class Inet6VpnRoute;
class StaticRouteConfig;

template <typename T> class StaticRoute;

template <typename T1, typename T2, typename T3, typename T4>
struct StaticRouteBase {
  typedef T1 RouteT;
  typedef T2 VpnRouteT;
  typedef T3 PrefixT;
  typedef T4 AddressT;
};

class StaticRouteInet : public StaticRouteBase<
    InetRoute, InetVpnRoute, Ip4Prefix, Ip4Address> {
};

class StaticRouteInet6 : public StaticRouteBase<
    Inet6Route, Inet6VpnRoute, Inet6Prefix, Ip6Address> {
};

typedef ConditionMatchPtr StaticRoutePtr;

struct StaticRouteRequest {
    enum RequestType {
        NEXTHOP_ADD_CHG,
        NEXTHOP_DELETE
    };

    StaticRouteRequest(RequestType type, BgpTable *table, BgpRoute *route,
                        StaticRoutePtr info)
        : type_(type), table_(table), rt_(route), info_(info) {
    }

    RequestType type_;
    BgpTable *table_;
    BgpRoute *rt_;
    StaticRoutePtr info_;

private:
    DISALLOW_COPY_AND_ASSIGN(StaticRouteRequest);
};

template <typename T>
class StaticRouteMgr : public IStaticRouteMgr {
public:
    typedef typename T::RouteT RouteT;
    typedef typename T::VpnRouteT VpnRouteT;
    typedef typename T::PrefixT PrefixT;
    typedef typename T::AddressT AddressT;
    typedef StaticRoute<T> StaticRouteT;

    // Map of Static Route prefix to the StaticRoute match object
    typedef std::map<PrefixT, StaticRoutePtr> StaticRouteMap;

    explicit StaticRouteMgr(RoutingInstance *instance);
    ~StaticRouteMgr();

    // Config
    virtual void ProcessStaticRouteConfig();
    virtual void UpdateStaticRouteConfig();
    virtual void FlushStaticRouteConfig();

    void EnqueueStaticRouteReq(StaticRouteRequest *req);
    const StaticRouteMap &static_route_map() const { return static_route_map_; }

    virtual void NotifyAllRoutes();
    virtual void UpdateAllRoutes();
    virtual uint32_t GetRouteCount() const;
    virtual uint32_t GetDownRouteCount() const;
    virtual bool FillStaticRouteInfo(RoutingInstance *rtinstance,
                                     StaticRouteEntriesInfo *info) const;

    Address::Family GetFamily() const;
    AddressT GetAddress(IpAddress addr) const;

private:
    template <typename U> friend class StaticRouteTest;
    typedef std::set<StaticRoutePtr> StaticRouteProcessList;
    typedef BgpInstanceConfig::StaticRouteList StaticRouteConfigList;

    // All static route related actions are performed in the context
    // of this task. This task has exclusion with db::DBTable task.
    static int static_route_task_id_;

    int CompareStaticRoute(typename StaticRouteMap::iterator loc,
        StaticRouteConfigList::iterator it);
    void AddStaticRoute(StaticRouteConfigList::iterator it);
    void DelStaticRoute(typename StaticRouteMap::iterator loc);
    void UpdateStaticRoute(typename StaticRouteMap::iterator loc,
        StaticRouteConfigList::iterator it);

    void LocateStaticRoutePrefix(const StaticRouteConfig &config);
    void RemoveStaticRoutePrefix(const PrefixT &static_route);
    void StopStaticRouteDone(BgpTable *table, ConditionMatch *info);
    void UnregisterAndResolveStaticRoute(StaticRoutePtr entry);
    bool StaticRouteEventCallback(StaticRouteRequest *req);

    bool ProcessUnregisterList();

    virtual void DisableUnregisterTrigger();
    virtual void EnableUnregisterTrigger();

    virtual void DisableQueue() { static_route_queue_->set_disable(true); }
    virtual void EnableQueue() { static_route_queue_->set_disable(false); }
    virtual bool IsQueueEmpty() { return static_route_queue_->IsQueueEmpty(); }
    RoutingInstance *routing_instance() { return rtinstance_; }

    RoutingInstance *rtinstance_;
    BgpConditionListener *listener_;
    StaticRouteMap  static_route_map_;
    WorkQueue<StaticRouteRequest *> *static_route_queue_;
    tbb::mutex mutex_;
    StaticRouteProcessList unregister_static_route_list_;
    boost::scoped_ptr<TaskTrigger> unregister_list_trigger_;

    DISALLOW_COPY_AND_ASSIGN(StaticRouteMgr);
};

typedef StaticRouteMgr<StaticRouteInet> StaticRouteMgrInet;
typedef StaticRouteMgr<StaticRouteInet6> StaticRouteMgrInet6;

#endif  // SRC_BGP_ROUTING_INSTANCE_STATIC_ROUTE_H_
