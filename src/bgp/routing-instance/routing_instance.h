/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_routing_instance_h
#define ctrlplane_routing_instance_h

#include <map>
#include <list>
#include <set>

#include <tbb/spin_rw_mutex.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/iterator/iterator_facade.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/bitset.h"
#include "base/index_map.h"
#include "base/lifetime.h"
#include "bgp/bgp_condition_listener.h"
#include "bgp/bgp_peer_key.h"
#include "bgp/rtarget/rtarget_address.h"
#include "bgp/ipeer.h"
#include "bgp/inet/inet_route.h"
#include "net/address.h"
#include "sandesh/sandesh_trace.h"
#include "schema/bgp_schema_types.h"

class DBTable;
class BgpInstanceConfig;
class BgpNeighborConfig;
class BgpServer;
class BgpTable;
class RouteDistinguisher;
class RoutingInstanceMgr;
class RoutingInstanceInfo;
class BgpNeighborResp;
class LifetimeActor;
class PeerManager;
class ShowRouteTable;
class StaticRouteMgr;

class RoutingInstance {
public:
    typedef std::set<RouteTarget> RouteTargetList;
    typedef std::map<std::string, BgpTable *> RouteTableList;

    RoutingInstance(std::string name, BgpServer *server,
                    RoutingInstanceMgr *mgr,
                    const BgpInstanceConfig *config);
    virtual ~RoutingInstance();

    const RouteTableList &GetTables() const {
        return vrf_table_;
    }

    void ProcessConfig(BgpServer *server);
    void UpdateConfig(BgpServer *server, const BgpInstanceConfig *config);
    void ClearConfig();

    static std::string GetTableNameFromVrf(std::string name, Address::Family fmly);
    static std::string GetVrfFromTableName(const std::string table);

    BgpTable *GetTable(Address::Family fmly);

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

    bool deleted();

    void set_index(BgpServer *, int index);
    int index() const { return index_; }
    bool IsDefaultRoutingInstance() const {
        return is_default_;
    }

    const std::string &name() const {
        return name_;
    }

    const BgpInstanceConfig *config() const { return config_; }
    const std::string virtual_network() const;
    int virtual_network_index() const;

    const RoutingInstanceMgr *manager() const { return mgr_; }
    RoutingInstanceInfo GetDataCollection(const char *operation);

    BgpServer *server();

    // Remove import and export route target
    // and Leave corresponding RtGroup
    void ClearRouteTarget();

    StaticRouteMgr *static_route_mgr() { return static_route_mgr_.get(); }
    PeerManager *peer_manager() { return peer_manager_.get(); }

private:
    class DeleteActor;

    // Cleanup all the state prior to deletion.
    void Shutdown();

    BgpTable *InetVpnTableCreate(BgpServer *server);
    BgpTable *ErmVpnTableCreate(BgpServer *server);
    BgpTable *EvpnTableCreate(BgpServer *server);
    BgpTable *RTargetTableCreate(BgpServer *server);

    std::string name_;
    int index_;
    std::auto_ptr<RouteDistinguisher> rd_;
    RouteTableList vrf_table_;
    RouteTargetList import_;
    RouteTargetList export_;
    RoutingInstanceMgr *mgr_;
    const BgpInstanceConfig *config_;
    bool is_default_;
    std::string virtual_network_;
    int virtual_network_index_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<RoutingInstance> manager_delete_ref_;
    boost::scoped_ptr<StaticRouteMgr> static_route_mgr_;
    boost::scoped_ptr<PeerManager> peer_manager_;
};


class RoutingInstanceSet : public BitSet {
};

class RoutingInstanceMgr {
public:
    typedef IndexMap<std::string, RoutingInstance, 
            RoutingInstanceSet> RoutingInstanceList;
    typedef RoutingInstanceList::iterator NameIterator;
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

    NameIterator name_begin() { return instances_.begin(); }
    NameIterator name_end() { return instances_.end(); }
    NameIterator name_lower_bound(const std::string &name) {
        return instances_.lower_bound(name);
    }
    RoutingInstance *GetRoutingInstance(const std::string &name) {
        return instances_.Find(name);
    }
    const RoutingInstance *GetRoutingInstance(const std::string &name) const {
        return instances_.Find(name);
    }

    int RegisterInstanceOpCallback(RoutingInstanceCb cb);
    void NotifyInstanceOp(std::string name, Operation deleted);
    void UnregisterInstanceOpCallback(int id);

    RoutingInstance *GetRoutingInstance(int index) {
        return instances_.At(index);
    }
    const RoutingInstance *GetRoutingInstance(int index) const {
        return instances_.At(index);
    }

    const RoutingInstance *GetInstanceByTarget(const RouteTarget &target) const;
    std::string GetVirtualNetworkByVnIndex(int vn_index) const;
    int GetVnIndexByExtCommunity(const ExtCommunity *community) const;

    // called from the BgpServer::ConfigUpdater
    virtual RoutingInstance *CreateRoutingInstance(
                const BgpInstanceConfig *config);
    void UpdateRoutingInstance(const BgpInstanceConfig *config);
    virtual void DeleteRoutingInstance(const std::string &name);

    bool deleted();
    void ManagedDelete();

    void DestroyRoutingInstance(RoutingInstance *rtinstance);

    size_t count() const { return instances_.count(); }
    BgpServer *server() { return server_; }
    LifetimeActor *deleter();
private:
    friend class RoutingInstanceMgrTest;
    class DeleteActor;

    void InstanceTargetAdd(RoutingInstance *rti);
    void InstanceTargetRemove(const RoutingInstance *rti);
    void InstanceVnIndexAdd(RoutingInstance *rti);
    void InstanceVnIndexRemove(const RoutingInstance *rti);

    const RoutingInstance *GetInstanceByVnIndex(int vn_index) const;
    int GetVnIndexByRouteTarget(const RouteTarget &rtarget) const;

    BgpServer *server_;
    RoutingInstanceList instances_;
    InstanceTargetMap target_map_;
    VnIndexMap vn_index_map_;
    boost::scoped_ptr<DeleteActor> deleter_;
    LifetimeRef<RoutingInstanceMgr> server_delete_ref_;
    boost::dynamic_bitset<> bmap_;      // free list.
    tbb::spin_rw_mutex rw_mutex_;
    InstanceOpListenersList callbacks_;
};
#endif
