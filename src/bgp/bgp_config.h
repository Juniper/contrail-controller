/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_CONFIG_H__
#define __BGP_CONFIG_H__

#include <list>
#include <map>
#include <set>

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/ptr_container/ptr_map.hpp>

#include "base/util.h"
#include "base/queue_task.h"
#include "base/task_trigger.h"
#include "bgp/bgp_common.h"
#include "bgp/bgp_peer_key.h"
#include "ifmap/ifmap_node_proxy.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

class BgpConfigListener;
class BgpConfigManager;
class BgpInstanceConfig;
class BgpPeeringConfig;
class DB;
class DBTable;
class DBGraph;
class IFMapNodeProxy;
class IPeer;
class RoutingInstance;
class RoutingInstanceMgr;

typedef boost::shared_ptr<IFMapNodeProxy> IFMapNodeRef;

struct BgpConfigDelta {
    BgpConfigDelta();
    BgpConfigDelta(const BgpConfigDelta &rhs);
    ~BgpConfigDelta();
    std::string id_type;
    std::string id_name;
    IFMapNodeRef node;
    IFMapObjectRef obj;
};

// A BgpNeighbor corresponds to a session between the local bgp-router and
// a remote bgp-router.
class BgpNeighborConfig {
public:
    typedef std::vector<std::string> AddressFamilyList;
    BgpNeighborConfig(const BgpInstanceConfig *instance,
                      const std::string &remote_name,
                      const std::string &local_name,
                      const autogen::BgpRouter *router,
                      const autogen::BgpSession *session);
    BgpNeighborConfig(const BgpInstanceConfig *instance,
                      const std::string &remote_name,
                      const std::string &local_name,
                      const autogen::BgpRouter *router);
    ~BgpNeighborConfig();

    static void Initialize();

    bool operator!=(const BgpNeighborConfig &rhs) const;

    void Update(const BgpNeighborConfig *rhs);

    std::string InstanceName() const;

    const AddressFamilyList &address_families() const;

    const std::string &name() const { return name_; }
    const std::string &uuid() const { return uuid_; }
    as_t peer_as() const {
        return (as_t) peer_config_.autonomous_system;
    }
    const std::string &vendor() const { return peer_config_.vendor; }
    const autogen::BgpRouterParams &peer_config() const { return peer_config_; }
    const autogen::BgpSessionAttributes &session_attributes() const {
        return attributes_;
    }

private:
    void SetSessionAttributes(const std::string &localname, 
                              const autogen::BgpSession *session);

    const BgpInstanceConfig *instance_;
    autogen::BgpRouterParams peer_config_;
    autogen::BgpSessionAttributes attributes_;
    static AddressFamilyList default_addr_family_list_;
    std::string name_;
    std::string uuid_;

    DISALLOW_COPY_AND_ASSIGN(BgpNeighborConfig);
};

class BgpPeeringConfig {
public:
    typedef std::map<std::string, BgpNeighborConfig *> NeighborMap;
    explicit BgpPeeringConfig(BgpInstanceConfig *instance)
        : instance_(instance) {
    }
    ~BgpPeeringConfig() {
        STLDeleteElements(&neighbors_);
    }

    void SetNodeProxy(IFMapNodeProxy *proxy);
    void BuildNeighbors(BgpConfigManager *manager, const std::string &peername,
                        const autogen::BgpRouter *rt_config,
                        const autogen::BgpPeering *peering, NeighborMap *map);
    void Update(BgpConfigManager *manager, const autogen::BgpPeering *peering);
    void Delete(BgpConfigManager *manager);

    static bool GetRouterPair(DBGraph *db_graph, const std::string  &localname,
                              IFMapNode *node,
                              std::pair<IFMapNode *, IFMapNode *> *pair);

    const IFMapNode *node() const { return node_proxy_.node(); }
    BgpInstanceConfig *instance() { return instance_; }
    std::string name() { return name_; }
    size_t size() const { return neighbors_.size(); }
    const autogen::BgpPeering *bgp_peering() const {
        return bgp_peering_.get();
    }

private:
    BgpInstanceConfig *instance_;
    std::string name_;
    IFMapNodeProxy node_proxy_;
    NeighborMap neighbors_;
    boost::intrusive_ptr<const autogen::BgpPeering> bgp_peering_;

    DISALLOW_COPY_AND_ASSIGN(BgpPeeringConfig);
};

// Corresponds to a local ifmap "bgp-router" node in a specific routing-instance.
class BgpProtocolConfig {
public:
    explicit BgpProtocolConfig(BgpInstanceConfig *instance);
    ~BgpProtocolConfig();

    void SetNodeProxy(IFMapNodeProxy *proxy);

    void Update(BgpConfigManager *manager, const autogen::BgpRouter *router);
    void Delete(BgpConfigManager *manager);

    const std::string &InstanceName() const;

    const autogen::BgpRouterParams &router_params() const {
        return bgp_router_->parameters();
    }
    const autogen::BgpRouter *bgp_router() const { return bgp_router_.get(); }

    IFMapNode *node() { return node_proxy_.node(); }

private:
    BgpInstanceConfig *instance_;
    IFMapNodeProxy node_proxy_;
    boost::intrusive_ptr<const autogen::BgpRouter> bgp_router_;

    DISALLOW_COPY_AND_ASSIGN(BgpProtocolConfig);
};

// Internal representation of routing instance
class BgpInstanceConfig {
public:
    typedef std::map<std::string, BgpNeighborConfig *> NeighborMap;
    typedef std::set<std::string> RouteTargetList;
    explicit BgpInstanceConfig(const std::string &name);
    ~BgpInstanceConfig();

    void SetNodeProxy(IFMapNodeProxy *proxy);
    
    void Update(BgpConfigManager *manager,
                const autogen::RoutingInstance *config);

    // The corresponding if-map node has been deleted.
    void ResetConfig();
    bool DeleteIfEmpty(BgpConfigManager *manager);

    const RouteTargetList &import_list() const { return import_list_; }
    const RouteTargetList &export_list() const { return export_list_; }

    const BgpProtocolConfig *bgp_config() const { return bgp_router_.get(); }
    BgpProtocolConfig *bgp_config_mutable() { return bgp_router_.get(); }
    BgpProtocolConfig *BgpConfigLocate();
    void BgpConfigReset();

    void NeighborAdd(BgpConfigManager *manager, BgpNeighborConfig *neighbor);
    void NeighborChange(BgpConfigManager *manager, BgpNeighborConfig *neighbor);
    void NeighborDelete(BgpConfigManager *manager, BgpNeighborConfig *neighbor);
    const BgpNeighborConfig *NeighborFind(std::string name) const;

    const NeighborMap &neighbors() const {
        return neighbors_;
    }

    const std::string &virtual_network() const { return virtual_network_; }
    int virtual_network_index() const { return virtual_network_index_; }

    IFMapNode *node() { return node_proxy_.node(); }
    const std::string &name() const { return name_; }

    const autogen::RoutingInstance *instance_config() const {
        return instance_config_.get();
    }

private:
    friend class BgpInstanceConfigTest;

    std::string name_;
    IFMapNodeProxy node_proxy_;
    // The local configuration parameters in this instance.
    boost::scoped_ptr<BgpProtocolConfig> bgp_router_;
    NeighborMap neighbors_;

    RouteTargetList import_list_;
    RouteTargetList export_list_;
    std::string virtual_network_;
    int virtual_network_index_;

    boost::intrusive_ptr<const autogen::RoutingInstance> instance_config_;

    DISALLOW_COPY_AND_ASSIGN(BgpInstanceConfig);
};

class BgpConfigData {
public:
    typedef boost::ptr_map<std::string, BgpInstanceConfig> BgpInstanceMap;
    typedef std::map<std::string, BgpPeeringConfig *> BgpPeeringMap;

    BgpConfigData();
    ~BgpConfigData();

    BgpInstanceConfig *LocateInstance(const std::string &name);
    void DeleteInstance(BgpInstanceConfig *rti);
    BgpInstanceConfig *FindInstance(const std::string &name);
    const BgpInstanceConfig *FindInstance(const std::string &name) const;
    const BgpInstanceMap &instances() const { return instances_; }

    BgpPeeringConfig *CreatePeering(BgpInstanceConfig *rti,
                                    IFMapNodeProxy *proxy);
    void DeletePeering(BgpPeeringConfig *peer);

    const BgpPeeringMap &peerings() const { return peering_map_; }
    BgpPeeringMap *peerings_mutable() { return &peering_map_; }

private:
    BgpInstanceMap instances_;
    BgpPeeringMap peering_map_;

    DISALLOW_COPY_AND_ASSIGN(BgpConfigData);
};

class BgpConfigManager {
public:
    static const char *kMasterInstance;
    static const int kDefaultPort;
    static const int kConfigTaskInstanceId = 0;
    static const as_t kDefaultAutonomousSystem;
    enum EventType {
        CFG_NONE,
        CFG_ADD,
        CFG_CHANGE,
        CFG_DELETE
    };

    typedef boost::function<void(const BgpInstanceConfig *, EventType)>
        BgpInstanceObserver;
    typedef boost::function<void(const BgpProtocolConfig *, EventType)>
        BgpProtocolObserver;
    typedef boost::function<void(const BgpNeighborConfig *, EventType)>
        BgpNeighborObserver;

    struct Observers {
        BgpInstanceObserver instance;
        BgpProtocolObserver protocol;
        BgpNeighborObserver neighbor;
    };

    BgpConfigManager();
    ~BgpConfigManager();
    void Initialize(DB *db, DBGraph *db_graph, const std::string &localname);

    template <typename BgpConfigObject>
    void Notify(const BgpConfigObject *, EventType);

    void RegisterObservers(const Observers &obs) { obs_ = obs; }

    DB *database() { return db_; }
    DBGraph *graph() { return db_graph_; }
    const BgpConfigData &config() const { return *config_; }
    void DefaultBgpRouterParams(autogen::BgpRouterParams &param);

    void OnChange();

    const std::string &localname() const { return localname_; }

    void Terminate();

private:
    friend class BgpConfigListenerTest;
    friend class BgpConfigManagerTest;

    typedef std::vector<BgpConfigDelta> ChangeList;
    typedef std::map<std::string,
        boost::function<void(const BgpConfigDelta &)> >IdentifierMap;

    void IdentifierMapInit();
    void DefaultConfig();
    void ProcessChanges(const ChangeList &change_list);
    void ProcessVirtualNetwork(const BgpConfigDelta &change);
    void ProcessRoutingInstance(const BgpConfigDelta &change);
    void ProcessBgpRouter(const BgpConfigDelta &change);
    void ProcessBgpProtocol(const BgpConfigDelta &change);
    void ProcessBgpPeering(const BgpConfigDelta &change);

    bool ConfigHandler();
    static int config_task_id_;

    DB *db_;
    DBGraph *db_graph_;
    std::string localname_;
    IdentifierMap id_map_;
    Observers obs_;
    TaskTrigger trigger_;
    boost::scoped_ptr<BgpConfigListener> listener_;
    boost::scoped_ptr<BgpConfigData> config_;

    DISALLOW_COPY_AND_ASSIGN(BgpConfigManager);
};

#endif
