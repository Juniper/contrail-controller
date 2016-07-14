/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_ROUTING_INSTANCE_ROUTING_INSTANCE_H_
#define SRC_BGP_ROUTING_INSTANCE_ROUTING_INSTANCE_H_


#include <boost/asio/ip/tcp.hpp>
#include <boost/dynamic_bitset.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <boost/scoped_ptr.hpp>
#include <tbb/mutex.h>

#include <list>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "base/bitset.h"
#include "base/index_map.h"
#include "base/lifetime.h"
#include "bgp/bgp_common.h"
#include "bgp/rtarget/rtarget_address.h"
#include "net/address.h"

class DBTable;
class BgpAttr;
class BgpInstanceConfig;
class BgpNeighborConfig;
class BgpPath;
class BgpRoute;
class BgpServer;
class BgpTable;
class IRouteAggregator;
class IStaticRouteMgr;
class RouteDistinguisher;
class RoutingInstanceMgr;
class RoutingInstanceInfo;
class BgpNeighborResp;
class ExtCommunity;
class LifetimeActor;
class PeerManager;
class ShowRouteTable;
class TaskTrigger;

class RoutingInstance {
public:
    typedef std::set<RouteTarget> RouteTargetList;
    typedef std::map<std::string, BgpTable *> RouteTableList;
    typedef std::map<Address::Family, BgpTable *> RouteTableFamilyList;

    RoutingInstance(std::string name, BgpServer *server,
                    RoutingInstanceMgr *mgr,
                    const BgpInstanceConfig *config);
    virtual ~RoutingInstance();

    RouteTableList &GetTables() { return vrf_tables_by_name_; }
    const RouteTableList &GetTables() const { return vrf_tables_by_name_; }

    void ProcessRoutingPolicyConfig();
    void UpdateRoutingPolicyConfig();
    void ProcessServiceChainConfig();
    void ProcessStaticRouteConfig();
    void UpdateStaticRouteConfig();
    void FlushStaticRouteConfig();
    void UpdateAllStaticRoutes();

    void ProcessRouteAggregationConfig();
    void UpdateRouteAggregationConfig();
    void FlushRouteAggregationConfig();

    void ProcessConfig();
    void UpdateConfig(const BgpInstanceConfig *config);
    void ClearConfig();

    static std::string GetTableName(std::string instance_name,
                                    Address::Family fmly);
    static std::string GetVrfFromTableName(const std::string table);

    BgpTable *GetTable(Address::Family fmly);
    const BgpTable *GetTable(Address::Family fmly) const;

    void AddTable(BgpTable *tbl);

    void RemoveTable(BgpTable *tbl);

    const RouteTargetList &GetImportList() const { return import_; }
    const RouteTargetList &GetExportList() const { return export_; }
    bool HasExportTarget(const ExtCommunity *extcomm) const;

    const RouteDistinguisher *GetRD() const {
        return rd_.get();
    }

    void TriggerTableDelete(BgpTable *table);
    void TableDeleteComplete(BgpTable *table);
    void DestroyDBTable(DBTable *table);

    bool MayDelete() const;
    void ManagedDelete();
    LifetimeActor *deleter();
    const LifetimeActor *deleter() const;
    bool deleted() const;

    void set_index(int index);
    int index() const { return index_; }
    bool always_subscribe() const { return always_subscribe_; }
    bool IsMasterRoutingInstance() const {
        return is_master_;
    }

    const std::string &name() const { return name_; }
    const std::string GetVirtualNetworkName() const;

    const BgpInstanceConfig *config() const { return config_; }
    const std::string virtual_network() const;
    int virtual_network_index() const;
    bool virtual_network_allow_transit() const;
    int vxlan_id() const;

    const RoutingInstanceMgr *manager() const { return mgr_; }
    RoutingInstanceInfo GetDataCollection(const char *operation);

    BgpServer *server() { return server_; }
    const BgpServer *server() const { return server_; }

    // Remove import and export route target
    // and Leave corresponding RtGroup
    void ClearRouteTarget();

    void CreateNeighbors();

    IStaticRouteMgr *static_route_mgr(Address::Family family) {
        if (family == Address::INET)
            return inet_static_route_mgr_.get();
        if (family == Address::INET6)
            return inet6_static_route_mgr_.get();
        assert(false);
        return NULL;
    }

    IStaticRouteMgr *LocateStaticRouteMgr(Address::Family family);

    IRouteAggregator *route_aggregator(Address::Family family) const {
        if (family == Address::INET)
            return inet_route_aggregator_.get();
        if (family == Address::INET6)
            return inet6_route_aggregator_.get();
        assert(false);
        return NULL;
    }

    IRouteAggregator *LocateRouteAggregator(Address::Family family);
    void DestroyRouteAggregator(Address::Family family);

    size_t peer_manager_size() const;
    PeerManager *peer_manager() { return peer_manager_.get(); }
    const PeerManager *peer_manager() const { return peer_manager_.get(); }
    PeerManager *LocatePeerManager();

    RoutingPolicyAttachList *routing_policies() { return &routing_policies_; }
    const RoutingPolicyAttachList &routing_policies() const {
        return routing_policies_;
    }

    void AddRoutingPolicy(RoutingPolicyPtr policy);

    bool ProcessRoutingPolicy(const BgpRoute *route, BgpPath *path) const;

    // Check whether the route is aggregate route
    bool IsAggregateRoute(const BgpTable *table, const BgpRoute *route) const;

    // Check whether the route is contributing route to aggregate route
    bool IsContributingRoute(const BgpTable *table,
                             const BgpRoute *route) const;

    int GetOriginVnForAggregateRoute(Address::Family family) const;

private:
    friend class RoutingInstanceMgr;
    class DeleteActor;

    void AddRTargetRoute(as4_t asn, const RouteTarget &rtarget);
    void DeleteRTargetRoute(as4_t asn, const RouteTarget &rtarget);
    void InitAllRTargetRoutes(as4_t asn);
    void FlushAllRTargetRoutes(as4_t asn);

    void AddRouteTarget(bool import, std::vector<std::string> *change_list,
        RouteTargetList::const_iterator it);
    void DeleteRouteTarget(bool import, std::vector<std::string> *change_list,
        RouteTargetList::iterator it);

    // Cleanup all the state prior to deletion.
    void Shutdown();

    BgpTable *VpnTableCreate(Address::Family vpn_family);
    BgpTable *RTargetTableCreate();
    BgpTable *VrfTableCreate(Address::Family vrf_family,
                             Address::Family vpn_family);
    void ClearFamilyRouteTarget(Address::Family vrf_family,
                                Address::Family vpn_family);

    std::string name_;
    int index_;
    std::auto_ptr<RouteDistinguisher> rd_;
    RouteTableList vrf_tables_by_name_;
    RouteTableFamilyList vrf_tables_by_family_;
    RouteTargetList import_;
    RouteTargetList export_;
    BgpServer *server_;
    RoutingInstanceMgr *mgr_;
    const BgpInstanceConfig *config_;
    bool is_master_;
    bool always_subscribe_;
    std::string virtual_network_;
    int virtual_network_index_;
    bool virtual_network_allow_transit_;
    int vxlan_id_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<RoutingInstance> manager_delete_ref_;
    boost::scoped_ptr<IStaticRouteMgr> inet_static_route_mgr_;
    boost::scoped_ptr<IStaticRouteMgr> inet6_static_route_mgr_;
    boost::scoped_ptr<IRouteAggregator> inet_route_aggregator_;
    boost::scoped_ptr<IRouteAggregator> inet6_route_aggregator_;
    boost::scoped_ptr<PeerManager> peer_manager_;
    RoutingPolicyAttachList routing_policies_;
};


class RoutingInstanceSet : public BitSet {
};

//
// This class is responsible for life cycle management of RoutingInstances.
//
// In order to speed up creation of RoutingInstances at startup, multiple
// bgp::ConfigHelper tasks running in parallel are used instead of creating
// from the bgp::Config task directly.  A hash of the RoutingInstance name
// is calculated and the name is added to one of the instance_config_lists_.
// Each list is processed independently in a bgp::ConfigHelper task.
//
// As RoutingInstance creation doesn't happen in-line from bgp::Config task,
// creation of BgpPeers in non-master RoutingInstances needs to be deferred
// till after RoutingInstance itself is created.  This is handled by adding
// the RoutingInstance name to neighbor_config_list_ and creating BgpPeers
// from bgp::Config task. Assumption is that there will be a small number of
// RoutingInstances with BgpPeers.
//
// As RoutingInstances can be created from parallel bgp::ConfigHelper tasks,
// a mutex is used to serialize access to shared data structures such as the
// instances_ map, target_map_ and vn_index_map_.
//
class RoutingInstanceMgr {
public:
    typedef std::set<std::string> RoutingInstanceConfigList;
    typedef IndexMap<std::string, RoutingInstance,
            RoutingInstanceSet> RoutingInstanceList;
    typedef RoutingInstanceList::iterator name_iterator;
    typedef RoutingInstanceList::const_iterator const_name_iterator;
    typedef std::multimap<RouteTarget, RoutingInstance *> InstanceTargetMap;
    typedef std::multimap<int, RoutingInstance *> VnIndexMap;

    typedef boost::function<void(std::string, int)> RoutingInstanceCb;
    typedef std::vector<RoutingInstanceCb> InstanceOpListenersList;

    enum Operation {
        INSTANCE_ADD = 1,
        INSTANCE_UPDATE = 2,
        INSTANCE_DELETE = 3
    };
    class RoutingInstanceIterator
        : public boost::iterator_facade<RoutingInstanceIterator,
                                        RoutingInstance,
                                        boost::forward_traversal_tag> {
    public:
        explicit RoutingInstanceIterator(const RoutingInstanceList &indexmap,
                          const RoutingInstanceSet &set, size_t index)
            : indexmap_(indexmap), set_(set), index_(index) {
        }
        size_t index() const { return index_; }

    private:
        friend class boost::iterator_core_access;

        void increment() {
            index_ = set_.find_next(index_);
        }
        bool equal(const RoutingInstanceIterator &rhs) const {
            return index_ == rhs.index_;
        }
        RoutingInstance &dereference() const {
            return *indexmap_.At(index_);
        }
        const RoutingInstanceList &indexmap_;
        const RoutingInstanceSet &set_;
        size_t index_;
    };

    explicit RoutingInstanceMgr(BgpServer *server);
    virtual ~RoutingInstanceMgr();

    RoutingInstanceIterator begin() {
        return RoutingInstanceIterator(instances_, instances_.bits(),
                                       instances_.bits().find_first());
    }

    RoutingInstanceIterator end() {
        return RoutingInstanceIterator(instances_, instances_.bits(),
                                       RoutingInstanceSet::npos);
    }

    name_iterator name_begin() { return instances_.begin(); }
    name_iterator name_end() { return instances_.end(); }
    name_iterator name_lower_bound(const std::string &name) {
        return instances_.lower_bound(name);
    }
    const_name_iterator name_cbegin() { return instances_.cbegin(); }
    const_name_iterator name_cend() { return instances_.cend(); }
    const_name_iterator name_clower_bound(const std::string &name) {
        return instances_.lower_bound(name);
    }

    int RegisterInstanceOpCallback(RoutingInstanceCb cb);
    void NotifyInstanceOp(std::string name, Operation deleted);
    void UnregisterInstanceOpCallback(int id);

    const RoutingInstance *GetInstanceByTarget(const RouteTarget &target) const;
    std::string GetVirtualNetworkByVnIndex(int vn_index) const;
    int GetVnIndexByExtCommunity(const ExtCommunity *community) const;

    RoutingInstance *GetDefaultRoutingInstance();
    const RoutingInstance *GetDefaultRoutingInstance() const;
    RoutingInstance *GetRoutingInstance(const std::string &name);
    const RoutingInstance *GetRoutingInstance(const std::string &name) const;
    RoutingInstance *GetRoutingInstanceLocked(const std::string &name);
    void InsertRoutingInstance(RoutingInstance *rtinstance);
    void LocateRoutingInstance(const BgpInstanceConfig *config);
    void LocateRoutingInstance(const std::string &name);
    RoutingInstance *CreateRoutingInstance(const BgpInstanceConfig *config);
    void UpdateRoutingInstance(RoutingInstance *rtinstance,
        const BgpInstanceConfig *config);
    virtual void DeleteRoutingInstance(const std::string &name);
    void DestroyRoutingInstance(RoutingInstance *rtinstance);

    bool deleted();
    bool MayDelete() const;
    void ManagedDelete();
    void Shutdown();

    void CreateRoutingInstanceNeighbors(const BgpInstanceConfig *config);

    size_t count() const { return instances_.count(); }
    BgpServer *server() { return server_; }
    const BgpServer *server() const { return server_; }
    LifetimeActor *deleter();
    tbb::mutex &mutex() { return mutex_; }

    uint32_t deleted_count() const { return deleted_count_; }
    void increment_deleted_count() { deleted_count_++; }
    void decrement_deleted_count() { deleted_count_--; }

private:
    friend class BgpConfigTest;
    friend class RoutingInstanceMgrTest;
    class DeleteActor;

    bool ProcessInstanceConfigList(int idx);
    bool ProcessNeighborConfigList();

    void InstanceTargetAdd(RoutingInstance *rti);
    void InstanceTargetRemove(const RoutingInstance *rti);
    void InstanceVnIndexAdd(RoutingInstance *rti);
    void InstanceVnIndexRemove(const RoutingInstance *rti);

    const RoutingInstance *GetInstanceByVnIndex(int vn_index) const;
    int GetVnIndexByRouteTarget(const RouteTarget &rtarget) const;

    void ASNUpdateCallback(as_t old_asn, as_t old_local_asn);
    void IdentifierUpdateCallback(Ip4Address old_identifier);

    void DisableInstanceConfigListProcessing();
    void EnableInstanceConfigListProcessing();
    void DisableNeighborConfigListProcessing();
    void EnableNeighborConfigListProcessing();

    BgpServer *server_;
    mutable tbb::mutex mutex_;
    std::vector<RoutingInstanceConfigList> instance_config_lists_;
    std::vector<TaskTrigger *> instance_config_triggers_;
    RoutingInstanceConfigList neighbor_config_list_;
    boost::scoped_ptr<TaskTrigger> neighbor_config_trigger_;
    RoutingInstance *default_rtinstance_;
    RoutingInstanceList instances_;
    InstanceTargetMap target_map_;
    VnIndexMap vn_index_map_;
    uint32_t deleted_count_;
    int asn_listener_id_;
    int identifier_listener_id_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<RoutingInstanceMgr> server_delete_ref_;
    boost::dynamic_bitset<> bmap_;      // free list.
    InstanceOpListenersList callbacks_;
};

#endif  // SRC_BGP_ROUTING_INSTANCE_ROUTING_INSTANCE_H_
