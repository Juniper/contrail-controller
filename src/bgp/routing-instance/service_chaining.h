/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_INSTANCE_SERVICE_CHAINING_H_
#define SRC_BGP_ROUTING_INSTANCE_SERVICE_CHAINING_H_

#include <boost/shared_ptr.hpp>

#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/lifetime.h"
#include "base/queue_task.h"
#include "bgp/bgp_condition_listener.h"
#include "bgp/inet/inet_route.h"
#include "bgp/inet6/inet6_route.h"

class BgpRoute;
class BgpTable;
class RoutingInstance;
class BgpTable;
class BgpServer;
class InetVpnRoute;
class Inet6VpnRoute;
class SandeshResponse;
class ServiceChainConfig;
class ShowServicechainInfo;

template <typename T1, typename T2, typename T3, typename T4>
struct ServiceChainBase {
  typedef T1 RouteT;
  typedef T2 VpnRouteT;
  typedef T3 PrefixT;
  typedef T4 AddressT;
};

class ServiceChainInet : public ServiceChainBase<
    InetRoute, InetVpnRoute, Ip4Prefix, Ip4Address> {
};

class ServiceChainInet6 : public ServiceChainBase<
    Inet6Route, Inet6VpnRoute, Inet6Prefix, Ip6Address> {
};

typedef ConditionMatchPtr ServiceChainPtr;

class ServiceChainState : public ConditionMatchState {
public:
    explicit ServiceChainState(ServiceChainPtr info) : info_(info) {
    }
    ServiceChainPtr info() { return info_; }

private:
    ServiceChainPtr info_;
    DISALLOW_COPY_AND_ASSIGN(ServiceChainState);
};

template <typename T>
class ServiceChainRequest {
public:
    typedef typename T::PrefixT PrefixT;

    enum RequestType {
        MORE_SPECIFIC_ADD_CHG,
        MORE_SPECIFIC_DELETE,
        CONNECTED_ROUTE_ADD_CHG,
        CONNECTED_ROUTE_DELETE,
        EXT_CONNECT_ROUTE_ADD_CHG,
        EXT_CONNECT_ROUTE_DELETE,
        UPDATE_ALL_ROUTES,
        STOP_CHAIN_DONE,
        SHOW_SERVICE_CHAIN,
        SHOW_PENDING_CHAIN
    };

    ServiceChainRequest(RequestType type, BgpTable *table, BgpRoute *route,
        PrefixT aggregate_match, ServiceChainPtr info)
        : type_(type),
          table_(table),
          rt_(route),
          aggregate_match_(aggregate_match),
          info_(info),
          snh_resp_(NULL) {
    }

    ServiceChainRequest(RequestType type, SandeshResponse *resp,
        const std::string &search_string)
        : type_(type),
          table_(NULL),
          rt_(NULL),
          snh_resp_(resp),
          search_string_(search_string) {
    }

    RequestType type_;
    BgpTable *table_;
    BgpRoute *rt_;
    PrefixT aggregate_match_;
    ServiceChainPtr info_;
    SandeshResponse *snh_resp_;
    std::string search_string_;

private:
    DISALLOW_COPY_AND_ASSIGN(ServiceChainRequest);
};

template <typename T>
class ServiceChain : public ConditionMatch {
public:
    typedef typename T::RouteT RouteT;
    typedef typename T::VpnRouteT VpnRouteT;
    typedef typename T::PrefixT PrefixT;
    typedef typename T::AddressT AddressT;
    typedef ServiceChainRequest<T> ServiceChainRequestT;

    // List of more specific routes resulted in Aggregate route
    typedef std::set<BgpRoute *> RouteList;

    // Map of Virtual Network subnet prefix to List of More Specific routes
    typedef std::map<PrefixT, RouteList> PrefixToRouteListMap;

    // Map of External Connecting route to Service Chain Route
    typedef std::set<BgpRoute *> ExtConnectRouteList;

    // List of path ids for the connected route
    typedef std::set<uint32_t> ConnectedPathIdList;

    ServiceChain(RoutingInstance *src, RoutingInstance *dest,
                 RoutingInstance *connected,
                 const std::vector<std::string> &subnets, AddressT addr);
    Address::Family GetFamily() const {
        assert(false);
        return Address::UNSPEC;
    }

    // Delete is triggered from configuration, not via LifetimeManager.
    void ManagedDelete() { }

    bool CompareServiceChainConfig(const ServiceChainConfig &config);
    void RemoveMatchState(BgpRoute *route, ServiceChainState *state);

    void SetConnectedRoute(BgpRoute *connected);
    bool IsConnectedRouteValid() const;
    const ConnectedPathIdList &GetConnectedPathIds() {
        return connected_path_ids_;
    }

    BgpRoute *connected_route() const { return connected_route_; }
    RoutingInstance *src_routing_instance() const { return src_; }
    RoutingInstance *connected_routing_instance() const { return connected_; }
    RoutingInstance *dest_routing_instance() const { return dest_; }
    const AddressT &service_chain_addr() const { return service_chain_addr_; }

    void AddServiceChainRoute(PrefixT prefix, const RouteT *orig_route,
        const ConnectedPathIdList &old_path_ids, bool aggregate);
    void RemoveServiceChainRoute(PrefixT prefix, bool aggregate);

    bool AddMoreSpecific(PrefixT aggregate, BgpRoute *more_specific);
    bool DeleteMoreSpecific(PrefixT aggregate, BgpRoute *more_specific);

    BgpTable *src_table() const;
    BgpTable *connected_table() const;
    BgpTable *dest_table() const;

    PrefixToRouteListMap *prefix_to_route_list_map() {
        return &prefix_to_routelist_map_;
    }
    const PrefixToRouteListMap *prefix_to_route_list_map() const {
        return &prefix_to_routelist_map_;
    }

    virtual bool Match(BgpServer *server, BgpTable *table, BgpRoute *route,
        bool deleted);
    virtual std::string ToString() const;

    void FillServiceChainInfo(ShowServicechainInfo *info) const;

    void set_connected_table_unregistered() {
        connected_table_unregistered_ = true;
    }
    void set_dest_table_unregistered() {
        dest_table_unregistered_ = true;
    }
    bool dest_table_unregistered() const {
        return dest_table_unregistered_;
    }
    bool connected_table_unregistered() const {
        return connected_table_unregistered_;
    }
    bool unregistered() const {
        return connected_table_unregistered_ && dest_table_unregistered_;
    }

    const ExtConnectRouteList &ext_connecting_routes() const {
        return ext_connect_routes_;
    }
    ExtConnectRouteList *ext_connecting_routes() {
        return &ext_connect_routes_;
    }

    bool aggregate_enable() const { return aggregate_; }
    void set_aggregate_enable() { aggregate_ = true; }

private:
    RoutingInstance *src_;
    RoutingInstance *dest_;
    RoutingInstance *connected_;
    ConnectedPathIdList connected_path_ids_;
    BgpRoute *connected_route_;
    AddressT service_chain_addr_;
    PrefixToRouteListMap prefix_to_routelist_map_;
    ExtConnectRouteList ext_connect_routes_;
    bool connected_table_unregistered_;
    bool dest_table_unregistered_;
    bool aggregate_;  // Whether the host route needs to be aggregated
    LifetimeRef<ServiceChain> src_table_delete_ref_;

    // Helper function to match
    bool IsMoreSpecific(BgpRoute *route, PrefixT *aggregate_match) const;
    bool IsAggregate(BgpRoute *route) const;
    bool IsConnectedRoute(BgpRoute *route) const;

    DISALLOW_COPY_AND_ASSIGN(ServiceChain);
};

template <typename T>
class ServiceChainMgr {
public:
    typedef typename T::RouteT RouteT;
    typedef typename T::PrefixT PrefixT;
    typedef typename T::AddressT AddressT;
    typedef ServiceChain<T> ServiceChainT;
    typedef ServiceChainRequest<T> ServiceChainRequestT;

    // Set of service chains created in the system
    typedef std::map<RoutingInstance *, ServiceChainPtr> ServiceChainMap;

    // At the time of processing, service chain request, all required
    // routing instance may not be created. Create a list of service chain
    // waiting for a routing instance to get created
    typedef std::set<RoutingInstance *> UnresolvedServiceChainList;

    explicit ServiceChainMgr(BgpServer *server);
    ~ServiceChainMgr();

    Address::Family GetFamily() const {
        assert(false);
        return Address::UNSPEC;
    }
    bool RequestHandler(ServiceChainRequestT *req);

    // Creates a new service chain between two Virtual network
    // If the two routing instance is already connected, it updates the
    // connected route address for existing service chain
    bool LocateServiceChain(RoutingInstance *src,
        const ServiceChainConfig &config);

    // Remove the existing service chain between from routing instance
    void StopServiceChain(RoutingInstance *src);
    void StopServiceChainDone(BgpTable *table, ConditionMatch *info);
    ServiceChainT *FindServiceChain(const std::string &src);
    ServiceChainT *FindServiceChain(RoutingInstance *rtinstance);

    void AddPendingServiceChain(RoutingInstance *rtinstance) {
        pending_chain_.insert(rtinstance);
    }
    void DeletePendingServiceChain(RoutingInstance *rtinstance) {
        pending_chain_.erase(rtinstance);
    }

    const ServiceChainMap &chain_set() const { return chain_set_; }
    const UnresolvedServiceChainList &pending_chains() const {
        return pending_chain_;
    }

    void StartResolve();
    bool ResolvePendingServiceChain();
    size_t PendingQueueSize() const { return pending_chain_.size(); }
    size_t ResolvedQueueSize() const { return chain_set_.size(); }
    uint32_t GetDownServiceChainCount() const;

    void Enqueue(ServiceChainRequestT *req);

    bool IsQueueEmpty() const { return process_queue_->IsQueueEmpty(); }

    BgpServer *server() { return server_; }
    bool aggregate_host_route() const {
        return aggregate_host_route_;
    }
    void set_aggregate_host_route(bool value) {
        aggregate_host_route_= value;
    }

private:
    friend class ServiceChainTest;

    // All service chain related actions are performed in the context
    // of this task. This task has exclusion with db::DBTable task.
    static int service_chain_task_id_;

    void RoutingInstanceCallback(std::string name, int op);
    void PeerRegistrationCallback(IPeer *peer, BgpTable *table,
        bool unregister);

    ServiceChainMap chain_set_;
    int id_;
    int registration_id_;
    UnresolvedServiceChainList pending_chain_;
    BgpServer *server_;
    BgpConditionListener *listener_;

    // Work Queue to handle requests posted from Match function, called
    // in the context of db::DBTable task.
    // The actions are performed in the bgp::ServiceChain task context.
    void DisableQueue() { process_queue_->set_disable(true); }
    void EnableQueue() { process_queue_->set_disable(false); }
    WorkQueue<ServiceChainRequestT *> *process_queue_;

    // Task trigger to resolve pending dependencies.
    boost::scoped_ptr<TaskTrigger> resolve_trigger_;

    bool aggregate_host_route_;

    DISALLOW_COPY_AND_ASSIGN(ServiceChainMgr);
};

#endif  // SRC_BGP_ROUTING_INSTANCE_SERVICE_CHAINING_H_
