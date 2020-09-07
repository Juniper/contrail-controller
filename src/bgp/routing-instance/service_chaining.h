/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_INSTANCE_SERVICE_CHAINING_H_
#define SRC_BGP_ROUTING_INSTANCE_SERVICE_CHAINING_H_

#include <boost/ptr_container/ptr_map.hpp>
#include <boost/shared_ptr.hpp>
#include <tbb/mutex.h>

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
#include "bgp/evpn/evpn_route.h"
#include "bgp/routing-instance/iservice_chain_mgr.h"

class BgpRoute;
class BgpTable;
class RoutingInstance;
class BgpTable;
class BgpServer;
class InetVpnRoute;
class Inet6VpnRoute;
class SandeshResponse;
class ServiceChainConfig;
class ServiceChainGroup;
class ShowServicechainInfo;

template <typename T> class ServiceChainMgr;

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

class ServiceChainEvpn : public ServiceChainBase<
    EvpnRoute, InetVpnRoute, EvpnPrefix, Ip4Address> {
};

class ServiceChainEvpn6 : public ServiceChainBase<
    EvpnRoute, Inet6VpnRoute, EvpnPrefix, Ip6Address> {
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
        DELETE_ALL_ROUTES,
        STOP_CHAIN_DONE,
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

    ServiceChainRequest(RequestType type, SandeshResponse *resp)
        : type_(type),
          table_(NULL),
          rt_(NULL),
          snh_resp_(resp) {
    }

    RequestType type_;
    BgpTable *table_;
    BgpRoute *rt_;
    PrefixT aggregate_match_;
    ServiceChainPtr info_;
    SandeshResponse *snh_resp_;

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
    typedef ServiceChainMgr<T> ServiceChainMgrT;

    // List of more specific routes resulted in Aggregate route
    typedef std::set<BgpRoute *> RouteList;

    // Map of Virtual Network subnet prefix to List of More Specific routes
    typedef std::map<PrefixT, RouteList> PrefixToRouteListMap;

    // Map of External Connecting route to Service Chain Route
    typedef std::set<BgpRoute *> ExtConnectRouteList;

    // List of path ids for the connected route
    typedef std::set<uint32_t> ConnectedPathIdList;

    ServiceChain(ServiceChainMgrT *manager, ServiceChainGroup *group,
        RoutingInstance *src, RoutingInstance *dest, RoutingInstance *connected,
        const std::vector<std::string> &subnets, AddressT addr, bool head,
        bool retain_as_path);
    Address::Family GetFamily() const { return manager_->GetFamily(); }
    Address::Family GetConnectedFamily() const {
        return manager_->GetConnectedFamily();
    }
    SCAddress::Family GetSCFamily() const { return manager_->GetSCFamily(); }

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
    void UpdateServiceChainRoute(PrefixT prefix, const RouteT *orig_route,
        const ConnectedPathIdList &old_path_ids, bool aggregate);
    void DeleteServiceChainRoute(PrefixT prefix, bool aggregate);

    bool AddMoreSpecific(PrefixT aggregate, BgpRoute *more_specific);
    bool DeleteMoreSpecific(PrefixT aggregate, BgpRoute *more_specific);

    BgpTable *src_table() const;
    BgpTable *connected_table() const;
    BgpTable *dest_table() const;

    ServiceChainGroup *group() const { return group_; }
    void clear_group() { group_ = NULL; }

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
    bool is_sc_head() const { return sc_head_; }
    bool retain_as_path() const { return retain_as_path_; }
    void set_aggregate_enable() { aggregate_ = true; }
    bool group_oper_state_up() const { return group_oper_state_up_; }
    void set_group_oper_state_up(bool up) { group_oper_state_up_ = up; }

private:
    ServiceChainMgrT *manager_;
    ServiceChainGroup *group_;
    RoutingInstance *src_;
    RoutingInstance *dest_;
    RoutingInstance *connected_;
    ConnectedPathIdList connected_path_ids_;
    BgpRoute *connected_route_;
    AddressT service_chain_addr_;
    PrefixToRouteListMap prefix_to_routelist_map_;
    ExtConnectRouteList ext_connect_routes_;
    bool group_oper_state_up_;
    bool connected_table_unregistered_;
    bool dest_table_unregistered_;
    bool aggregate_;  // Whether the host route needs to be aggregated
    bool sc_head_; // Whether this SI is at the head of the chain
    bool retain_as_path_;
    LifetimeRef<ServiceChain> src_table_delete_ref_;
    LifetimeRef<ServiceChain> dest_table_delete_ref_;
    LifetimeRef<ServiceChain> connected_table_delete_ref_;

    // Helper function to match
    bool IsMoreSpecific(BgpRoute *route, PrefixT *aggregate_match) const;
    bool IsAggregate(BgpRoute *route) const;
    bool IsConnectedRoute(BgpRoute *route, bool is_conn_table=false) const;
    bool IsEvpnType5Route(BgpRoute *route) const;
    void GetReplicationFamilyInfo(DBTablePartition *&partition,
        BgpRoute *&route, BgpTable *&table, PrefixT prefix, bool create);
    void ProcessServiceChainPath(uint32_t path_id, BgpPath *path,
        BgpAttrPtr attr, BgpRoute *&route, DBTablePartition *&partition,
        bool aggregate, BgpTable *bgptable);
    void UpdateServiceChainRouteInternal(const RouteT *orig_route,
        const ConnectedPathIdList &old_path_ids, BgpRoute *sc_route,
        DBTablePartition *partition, BgpTable *bgptable, bool aggregate);
    void DeleteServiceChainRouteInternal(BgpRoute *service_chain_route,
                                         DBTablePartition *partition,
                                         BgpTable *bgptable, bool aggregate);

    DISALLOW_COPY_AND_ASSIGN(ServiceChain);
};

//
// This represents a service chain group within a ServiceChainMgr. All the
// individual ServiceChains that are part of the same logical service chain
// in the configuration are part of a given ServiceChainGroup.
//
// A ServiceChainGroup maintains operational state for itself based on the
// operational state of the individual ServiceChains that are part of the
// group. This is used to force fate sharing for all the ServiceChains in
// the group. If the operational status of the group is down, re-originated
// routes for all ServiceChainTs are deleted/withdrawn.
//
// A ServiceChainGroup gets created when the ServiceChainMgr processes a
// ServiceChainConfig with a non-empty service_chain_id. It gets deleted
// when there are no more ServiceChainTs or pending chains that belong to
// the ServiceChainGroup.
//
// The set of member chains is tracked using RoutingInstance pointers (as
// opposed to ServiceChainT pointers) so that the state of pending chains
// can also be tracked. Note that ServiceChainT objects are not allocated
// for pending chains.
//
// The membership of RoutingInstances in a ServiceChainGroup is updated as
// the group in the ServiceChainConfig for the RoutingInstances is updated.
//
// The operational state of the group is updated whenever a RoutingInstance
// is added or deleted to/from the ServiceChainGroup and when the connected
// route for the ServiceChainT is added/updated/deleted.
//
class ServiceChainGroup {
public:
    ServiceChainGroup(IServiceChainMgr *manager, const std::string &name);
    ~ServiceChainGroup();

    void AddRoutingInstance(RoutingInstance *rtinstance);
    void DeleteRoutingInstance(RoutingInstance *rtinstance);
    void UpdateOperState();
    std::string name() const { return name_; }
    bool empty() const { return chain_set_.empty(); }
    bool oper_state_up() const { return oper_state_up_; }

private:
    typedef std::set<RoutingInstance *> ServiceChainSet;

    IServiceChainMgr *manager_;
    std::string name_;
    ServiceChainSet chain_set_;
    bool oper_state_up_;
};

template <typename T>
class ServiceChainMgr : public IServiceChainMgr {
public:
    typedef typename T::RouteT RouteT;
    typedef typename T::PrefixT PrefixT;
    typedef typename T::AddressT AddressT;
    typedef ServiceChain<T> ServiceChainT;
    typedef ServiceChainRequest<T> ServiceChainRequestT;

    explicit ServiceChainMgr(BgpServer *server);
    virtual ~ServiceChainMgr();

    void Terminate();
    void ManagedDelete();
    bool MayDelete() const;
    void RetryDelete();

    // Creates a new service chain between two Virtual network
    // If the two routing instance is already connected, it updates the
    // connected route address for existing service chain
    virtual bool LocateServiceChain(RoutingInstance *rtinstance,
        const ServiceChainConfig &config);

    // Remove the existing service chain between from routing instance
    virtual void StopServiceChain(RoutingInstance *rtinstance);
    virtual void UpdateServiceChain(RoutingInstance *rtinstance,
        bool group_oper_state_up);
    void UpdateServiceChainGroup(ServiceChainGroup *group);

    virtual size_t PendingQueueSize() const { return pending_chains_.size(); }
    virtual size_t ResolvedQueueSize() const { return chain_set_.size(); }
    virtual uint32_t GetDownServiceChainCount() const;
    virtual bool IsQueueEmpty() const { return process_queue_->IsQueueEmpty(); }
    virtual bool ServiceChainIsPending(RoutingInstance *rtinstance,
        std::string *reason = NULL) const;
    virtual bool ServiceChainIsUp(RoutingInstance *rtinstance) const;

    Address::Family GetFamily() const;
    Address::Family GetConnectedFamily() const;
    SCAddress::Family GetSCFamily() const;
    void Enqueue(ServiceChainRequestT *req);
    virtual bool FillServiceChainInfo(RoutingInstance *rtinstance,
                                      ShowServicechainInfo *info) const;
    virtual BgpConditionListener *GetListener();
private:
    template <typename U> friend class ServiceChainIntegrationTest;
    template <typename U> friend class ServiceChainTest;
    class DeleteActor;

    // All service chain related actions are performed in the context
    // of this task. This task has exclusion with db::DBTable task.
    static int service_chain_task_id_;

    struct PendingChainState {
        PendingChainState() : group(NULL) {
        }
        PendingChainState(ServiceChainGroup *group, std::string reason)
            : group(group), reason(reason) {
        }
        ServiceChainGroup *group;
        std::string reason;
    };

    // Set of service chains created in the system
    typedef std::map<RoutingInstance *, ServiceChainPtr> ServiceChainMap;

    // At the time of processing, service chain request, all required info
    // may not be available (e.g. dest routing instance may not be created,
    // or marked deleted etc). Create a list of pending service chains that
    // are waiting to get created and maintain a reason string for why the
    // service chain is on the pending list.
    typedef std::map<RoutingInstance *, PendingChainState> PendingChainList;

    typedef boost::ptr_map<std::string, ServiceChainGroup> GroupMap;
    typedef std::set<ServiceChainGroup *> GroupSet;

    ServiceChainGroup *FindServiceChainGroup(RoutingInstance *rtinstance);
    ServiceChainGroup *FindServiceChainGroup(const std::string &group_name);
    ServiceChainGroup *LocateServiceChainGroup(const std::string &group_name);
    bool ProcessServiceChainGroups();

    bool RequestHandler(ServiceChainRequestT *req);
    void StopServiceChainDone(BgpTable *table, ConditionMatch *info);
    ServiceChainT *FindServiceChain(const std::string &instance) const;
    ServiceChainT *FindServiceChain(RoutingInstance *rtinstance) const;

    void AddPendingServiceChain(RoutingInstance *rtinstance,
        ServiceChainGroup *group, std::string reason) {
        PendingChainState state(group, reason);
        pending_chains_.insert(std::make_pair(rtinstance, state));
    }
    void DeletePendingServiceChain(RoutingInstance *rtinstance) {
        pending_chains_.erase(rtinstance);
    }
    PendingChainState GetPendingServiceChain(RoutingInstance *rtinstance) {
        typename PendingChainList::const_iterator loc =
            pending_chains_.find(rtinstance);
        if (loc != pending_chains_.end()) {
            return loc->second;
        } else {
            return PendingChainState();
        }
    }

    void UpdateServiceChainRoutes(ServiceChainT *chain,
        const typename ServiceChainT::ConnectedPathIdList &old_path_ids);
    void DeleteServiceChainRoutes(ServiceChainT *chain);

    void StartResolve();
    bool ResolvePendingServiceChain();
    void RoutingInstanceCallback(std::string name, int op);
    void PeerRegistrationCallback(IPeer *peer, BgpTable *table,
        bool unregister);

    bool aggregate_host_route() const { return aggregate_host_route_; }
    virtual void set_aggregate_host_route(bool value) {
        aggregate_host_route_ = value;
    }

    virtual void DisableResolveTrigger();
    virtual void EnableResolveTrigger();
    virtual void DisableGroupTrigger();
    virtual void EnableGroupTrigger();

    // Work Queue to handle requests posted from Match function, called
    // in the context of db::DBTable task.
    // The actions are performed in the bgp::ServiceChain task context.
    virtual void DisableQueue() { process_queue_->set_disable(true); }
    virtual void EnableQueue() { process_queue_->set_disable(false); }

    // Mutex is used to serialize access from multiple bgp::ConfigHelper tasks.
    BgpServer *server_;
    tbb::mutex mutex_;
    BgpConditionListener *listener_;
    boost::scoped_ptr<TaskTrigger> resolve_trigger_;
    boost::scoped_ptr<WorkQueue<ServiceChainRequestT *> > process_queue_;
    ServiceChainMap chain_set_;
    PendingChainList pending_chains_;
    boost::scoped_ptr<TaskTrigger> group_trigger_;
    GroupMap group_map_;
    GroupSet group_set_;
    bool aggregate_host_route_;
    int id_;
    int registration_id_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<ServiceChainMgr> server_delete_ref_;

    DISALLOW_COPY_AND_ASSIGN(ServiceChainMgr);
};

typedef ServiceChainMgr<ServiceChainInet> ServiceChainMgrInet;
typedef ServiceChainMgr<ServiceChainInet6> ServiceChainMgrInet6;
typedef ServiceChainMgr<ServiceChainEvpn> ServiceChainMgrEvpn;
typedef ServiceChainMgr<ServiceChainEvpn6> ServiceChainMgrEvpn6;

#endif  // SRC_BGP_ROUTING_INSTANCE_SERVICE_CHAINING_H_
