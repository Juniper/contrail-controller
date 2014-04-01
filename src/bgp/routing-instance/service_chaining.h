/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_service_chaining_h
#define ctrlplane_service_chaining_h

#include <list>
#include <map>
#include <set>

#include <boost/shared_ptr.hpp>
#include "base/lifetime.h"
#include <base/queue_task.h>

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_trace.h>

#include "bgp/bgp_condition_listener.h"
#include "bgp/bgp_config.h"
#include "bgp/inet/inet_route.h"

#include "bgp/routing-instance/service_chaining_types.h"

class BgpRoute;
class BgpTable;
class RoutingInstance;
class BgpTable;
class BgpServer;

class ServiceChain : public ConditionMatch {
public:
    //
    // List of more specific routes resulted in Aggregate route
    //
    typedef std::set<BgpRoute *> RouteList;
    //
    // Map of Virtual Network subnet prefix to List of More Specific routes 
    //
    typedef std::map<Ip4Prefix, RouteList> PrefixToRouteListMap;
    //
    // Map of External Connecting route to Service Chain Route
    //
    typedef std::set<BgpRoute *> ExtConnectRouteList;
    //
    // List of path ids for the connected route
    //
    typedef std::set<uint32_t> ConnectedPathIdList;

    ServiceChain(RoutingInstance *src, RoutingInstance *dest, 
                 RoutingInstance *connected,
                 const std::vector<std::string> &subnets, IpAddress addr);

    // Compare config and return whether cfg has updated
    bool CompareServiceChainCfg(const autogen::ServiceChainInfo &cfg);

    const IpAddress &service_chain_addr() const {
        return service_chain_addr_;
    }

    void set_connected_route(BgpRoute *connected) {
        connected_route_ = connected;

        if (!connected_route_) {
            connected_path_ids_.clear();
            return;
        }

        assert(connected_path_ids_.empty());

        for (Route::PathList::iterator it = connected->GetPathList().begin();
             it != connected->GetPathList().end(); it++) {
            BgpPath *path = static_cast<BgpPath *>(it.operator->());

            // Infeasible paths are not considered
            if (!path->IsFeasible()) break;

            // take snapshot of all ECMP paths
            if (connected_route_->BestPath()->PathCompare(*path, true)) break;

            // Use the nexthop attribute of the connected path as the path id.
            uint32_t path_id = path->GetAttr()->nexthop().to_v4().to_ulong();
            connected_path_ids_.insert(path_id);
        }
    }

    RoutingInstance *src_routing_instance() const {
        return src_;
    }

    RoutingInstance *connected_routing_instance() const {
        return connected_;
    }

    RoutingInstance *dest_routing_instance() const {
        return dest_;
    }

    bool connected_route_valid() const {
        return (connected_route_ && !connected_route_->IsDeleted() && 
                connected_route_->BestPath() && 
                connected_route_->BestPath()->IsFeasible());
    }

    BgpRoute *connected_route() const {
        return connected_route_;
    }

    ConnectedPathIdList *ConnectedPathIds() { return &connected_path_ids_; }

    void AddServiceChainRoute(Ip4Prefix prefix, InetRoute *orig_route, 
                              ConnectedPathIdList *list, bool aggregate);
    void RemoveServiceChainRoute(Ip4Prefix prefix, bool aggregate);

    bool add_more_specific(Ip4Prefix aggregate, BgpRoute *more_specific) {
        PrefixToRouteListMap::iterator it = 
            prefix_to_routelist_map_.find(aggregate);
        assert(it != prefix_to_routelist_map_.end());
        bool ret = false;
        if (it->second.empty()) {
            // Add the aggregate for the first time
            ret = true;
        }
        it->second.insert(more_specific);
        return ret;
    }

    bool delete_more_specific(Ip4Prefix aggregate, BgpRoute *more_specific) {
        PrefixToRouteListMap::iterator it = 
            prefix_to_routelist_map_.find(aggregate);
        assert(it != prefix_to_routelist_map_.end());
        it->second.erase(more_specific);
        return it->second.empty(); 
    }

    BgpTable *src_table() const;

    BgpTable *connected_table() const;

    BgpTable *dest_table() const;

    const PrefixToRouteListMap &prefix_to_route_list_map() const {
        return prefix_to_routelist_map_;
    }

    PrefixToRouteListMap *prefix_to_route_list_map() {
        return &prefix_to_routelist_map_;
    }

    virtual bool Match(BgpServer *server, BgpTable *table, 
                       BgpRoute *route, bool deleted);

    void FillServiceChainInfo(ShowServicechainInfo &info) const; 

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

    bool aggregate_enable() const {
        return aggregate_;
    }

    void set_aggregate_enable() {
        aggregate_ = true;
    }

    void ManagedDelete() {
        // Trigger of service chain delete is from config
    }

private:
    RoutingInstance *src_;
    RoutingInstance *dest_;
    RoutingInstance *connected_;
    ConnectedPathIdList connected_path_ids_;
    BgpRoute *connected_route_;
    IpAddress service_chain_addr_;
    PrefixToRouteListMap prefix_to_routelist_map_;
    // List of routes from Destination VN for external connectivity
    ExtConnectRouteList ext_connect_routes_;
    bool connected_table_unregistered_;
    bool dest_table_unregistered_;
    bool aggregate_; // Whether the host route needs to be aggregated
    LifetimeRef<ServiceChain> src_table_delete_ref_;

    // Helper function to match 
    bool is_more_specific(BgpRoute *route, Ip4Prefix *aggregate_match);
    bool is_aggregate(BgpRoute *route);

    bool is_connected_route(BgpRoute *route) {
        InetRoute *inet_route = dynamic_cast<InetRoute *>(route);
        if (service_chain_addr() == inet_route->GetPrefix().ip4_addr())
            return true;
        return false;
    }

    DISALLOW_COPY_AND_ASSIGN(ServiceChain);
};

typedef ConditionMatchPtr ServiceChainPtr;

class ServiceChainState : public ConditionMatchState {
public:
    ServiceChainState(ServiceChainPtr info) : info_(info) {
    }
    ServiceChainPtr info() {
        return info_;
    }
private:
    ServiceChainPtr info_;
    DISALLOW_COPY_AND_ASSIGN(ServiceChainState);
};

struct ServiceChainRequest {
    enum RequestType {
        MORE_SPECIFIC_ADD_CHG,
        MORE_SPECIFIC_DELETE,
        CONNECTED_ROUTE_ADD_CHG,
        CONNECTED_ROUTE_DELETE,
        EXT_CONNECT_ROUTE_ADD_CHG,
        EXT_CONNECT_ROUTE_DELETE,
        STOP_CHAIN_DONE,
        SHOW_SERVICE_CHAIN,
        SHOW_PENDING_CHAIN
    };

    ServiceChainRequest(RequestType type, BgpTable *table, BgpRoute *route,
                        Ip4Prefix aggregate_match, ServiceChainPtr info) 
        : type_(type), table_(table), rt_(route), 
          aggregate_match_(aggregate_match), info_(info) {
    }

    ServiceChainRequest(RequestType type, SandeshResponse *resp) 
        : type_(type), snh_resp_(resp) {
    }
    RequestType type_;
    BgpTable    *table_;
    BgpRoute    *rt_;
    Ip4Prefix   aggregate_match_;
    ServiceChainPtr info_;
    SandeshResponse *snh_resp_;
    DISALLOW_COPY_AND_ASSIGN(ServiceChainRequest);
};

class ServiceChainMgr {
public:
    //
    // Set of service chains created in the system
    //
    typedef std::map<RoutingInstance *, ServiceChainPtr> ServiceChainMap;

    //
    // At the time of processing, service chain request, all required 
    // routing instance may not be created. Create a list of service chain
    // waiting for a routing instance to get created
    //
    typedef std::set<RoutingInstance *> UnresolvedServiceChainList;

    ServiceChainMgr(BgpServer *server);
    ~ServiceChainMgr();

    // Creates a new service chain between two Virtual network
    // If the two routing instance is already connected, it updates the
    // connected route address for existing service chain
    bool LocateServiceChain(RoutingInstance *src, 
                            const autogen::ServiceChainInfo &cfg);

    // Remove the existing service chain between from routing instance
    void StopServiceChain(RoutingInstance *src);

    bool RequestHandler(ServiceChainRequest *req);

    void StopServiceChainDone(BgpTable *table, ConditionMatch *info);

    BgpServer *server() {
        return server_;
    }

    void AddPendingServiceChain(RoutingInstance *rtinstance) {
        pending_chain_.insert(rtinstance);
    }

    const ServiceChainMap &chain_set() const {
        return chain_set_;
    }

    const UnresolvedServiceChainList &pending_chains() const {
        return pending_chain_;
    }

    void RoutingInstanceCallback(std::string name, int op);

    void StartResolve();
    bool ResolvePendingServiceChain();

    void Enqueue(ServiceChainRequest *req);

    bool IsQueueEmpty() { return process_queue_->IsQueueEmpty(); }

    ServiceChain *FindServiceChain(const std::string &src);

    bool aggregate_host_route() const {
        return aggregate_host_route_;
    }

    void set_aggregate_host_route(bool value) {
        aggregate_host_route_= value;
    }
private:
    //
    // All service chain related actions are performed in the context 
    // of this task. This task has exclusion with DBTable task
    //
    friend class ServiceChainTest;
    static int service_chain_task_id_;

    ServiceChainMap chain_set_;
    int id_;
    UnresolvedServiceChainList pending_chain_;
    BgpServer *server_;
    // 
    // Work Queue to handle requests posted from Match function(DBTable ctx)
    // The actions are performed in the ServiceChain task ctx
    //
    void DisableQueue() { process_queue_->set_disable(true); }
    void EnableQueue() { process_queue_->set_disable(false); }

    WorkQueue<ServiceChainRequest *> *process_queue_;

    //
    // Task trigger to resolve pending dependencies
    //
    boost::scoped_ptr<TaskTrigger> resolve_trigger_;

    bool aggregate_host_route_;
    DISALLOW_COPY_AND_ASSIGN(ServiceChainMgr);
};
#endif // ctrlplane_service_chaining_h
