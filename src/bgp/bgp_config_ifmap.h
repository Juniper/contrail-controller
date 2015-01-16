/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef BGP_BGP_CONFIG_IFMAP_H__
#define BGP_BGP_CONFIG_IFMAP_H__

#include <map>
#include <set>
#include <vector>

#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/task_trigger.h"
#include "base/util.h"
#include "bgp/bgp_config.h"
#include "ifmap/ifmap_config_listener.h"
#include "ifmap/ifmap_node_proxy.h"

class BgpConfigListener;
typedef struct IFMapConfigListener::ConfigDelta BgpConfigDelta;
class BgpIfmapConfigManager;
class BgpIfmapInstanceConfig;
class BgpServer;
class DB;
class DBGraph;

namespace autogen {
class BgpPeering;
class BgpRouter;
class RoutingInstance;
struct BgpRouterParams;
}

//
// A BgpIfmapPeeringConfig corresponds to a bgp-peering link in the
// schema. Since a bgp-peering is a link with attributes, it's
// represented as a node in the configuration database. There's a
// single BgpIfmapPeeringConfig between a given pair of bgp-routers in
// a particular routing-instance.
//
// A bgp-peering link is represented in IFMap DB using a autogen::BgpPeering
// object. This object has a vector of autogen::BgpSessions, with each entry
// corresponding to a single session. A BgpNeighborConfig is created for each
// session.
//
// The NeighborMap keeps pointers to all the BgpNeighborConfig objects
// for a BgpIfmapPeeringConfig. There's typically just a single entry
// in this map, but we allow for multiple entries if there's more than
// 1 autogen::BgpSesssion in the autogen::BgpPeering.
//
// A BgpIfmapPeeringConfig gets created/updated/deleted when the
// IFMapNode for the bgp-peering is created/updated/deleted.
//
// There's no direct operational object corresponding to
// BgpIfmapPeeringConfig. A BgpIfmapPeeringConfig results in the
// creation of one or more BgpNeighborConfigs each of which is
// associated with a BgpPeer.
//
// The instance_ field is a back pointer to the BgpInstanceConfig object for
// the routing-instance to which this bgp-peering belongs.
// The IFMapNodeProxy maintains a reference to the IFMapNode object for the
// bgp-peering in the ifmap metadata table.
//
class BgpIfmapPeeringConfig {
public:
    typedef std::map<std::string, BgpNeighborConfig *> NeighborMap;

    explicit BgpIfmapPeeringConfig(BgpIfmapInstanceConfig *instance);
    ~BgpIfmapPeeringConfig();

    void SetNodeProxy(IFMapNodeProxy *proxy);
    void Update(BgpIfmapConfigManager *manager,
                const autogen::BgpPeering *peering);
    void Delete(BgpIfmapConfigManager *manager);

    static bool GetRouterPair(DBGraph *db_graph, const std::string  &localname,
                              IFMapNode *node,
                              std::pair<IFMapNode *, IFMapNode *> *pair);

    const IFMapNode *node() const { return node_proxy_.node(); }
    BgpIfmapInstanceConfig *instance() { return instance_; }
    std::string name() const { return name_; }
    size_t size() const { return neighbors_.size(); }
    const autogen::BgpPeering *bgp_peering() const {
        return bgp_peering_.get();
    }

private:
    void BuildNeighbors(BgpConfigManager *manager, const std::string &peername,
                        const autogen::BgpRouter *rt_config,
                        const autogen::BgpPeering *peering, NeighborMap *map);

    BgpIfmapInstanceConfig *instance_;
    std::string name_;
    IFMapNodeProxy node_proxy_;
    NeighborMap neighbors_;
    boost::intrusive_ptr<const autogen::BgpPeering> bgp_peering_;

    DISALLOW_COPY_AND_ASSIGN(BgpIfmapPeeringConfig);
};

//
// Represents a local IFMap bgp-router node in a specific routing-instance.
//
// A BgpProtocolConfig is created/updated/deleted when the IFMapNode for the
// bgp-router is created/updated/deleted.
//
// The instance_ field is a back pointer to the BgpInstanceConfig object for
// the routing-instance to which this bgp-router belongs.
// The IFMapNodeProxy maintains a reference to the IFMapNode object for the
// bgp-router in the ifmap bgp-router table.
//
class BgpIfmapProtocolConfig {
public:
    explicit BgpIfmapProtocolConfig(BgpIfmapInstanceConfig *instance);
    ~BgpIfmapProtocolConfig();

    void SetNodeProxy(IFMapNodeProxy *proxy);

    void Update(BgpIfmapConfigManager *manager,
                const autogen::BgpRouter *router);
    void Delete(BgpIfmapConfigManager *manager);

    const std::string &InstanceName() const;

    const autogen::BgpRouterParams &router_params() const;
    const autogen::BgpRouter *bgp_router() const { return bgp_router_.get(); }

    IFMapNode *node() { return node_proxy_.node(); }
    const BgpIfmapInstanceConfig *instance() { return instance_; }

    const BgpProtocolConfig *protocol_config() const { return &data_; }
private:
    BgpIfmapInstanceConfig *instance_;
    IFMapNodeProxy node_proxy_;
    BgpProtocolConfig data_;
    boost::intrusive_ptr<const autogen::BgpRouter> bgp_router_;

    DISALLOW_COPY_AND_ASSIGN(BgpIfmapProtocolConfig);
};

//
// Internal representation of routing-instance.
//
// A BgpInstanceConfig causes creation of a RoutingInstance. RoutingInstance
// has a pointer to the BgpInstanceConfig. This pointer gets invalidated and
// cleared when the RoutingInstance is undergoing deletion.
//
// The IFMapNodeProxy maintains a reference to the IFMapNode object for the
// routing-instance in the ifmap routing-instance table.
//
// The BgpInstanceConfig for the master instance is manually created when the
// BgpConfigManager is initialized. It gets deleted when the BgpConfigManager
// is terminated.
//
// For other routing-instances, a BgpInstanceConfig is created when we first
// see the IFMapNode for the routing-instance or when we see the IFMapNode for
// the local bgp-router in the routing-instance.
//
// BgpInstanceConfig is deleted only when all references to it are
// gone. The IFMapNode for the routing-instance must be deleted, any
// BgpProtcolConfig for the local bgp-router in the routing-instance
// must be deleted and all the BgpNeighborConfigs and
// BgpIfmapPeeringConfigs in the routing-instance must be deleted.
//
// The protocol_ field is a pointer to the BgpProtocolConfig object
// for the local bgp-router object, if any, in the routing-instance.
// The NeighborMap keeps pointers to all the BgpNeighborConfig objects
// in a BgpInstanceConfig.  The PeeringMap keeps pointers to all the
// BgpIfmapPeeringConfig objects in the BgpInstanceConfig.
//
class BgpIfmapInstanceConfig {
public:
    typedef std::map<std::string, BgpNeighborConfig *> NeighborMap;
    typedef std::map<std::string, BgpIfmapPeeringConfig *> PeeringMap;
    typedef BgpInstanceConfig::RouteTargetList RouteTargetList;

    explicit BgpIfmapInstanceConfig(const std::string &name);
    ~BgpIfmapInstanceConfig();

    void SetNodeProxy(IFMapNodeProxy *proxy);

    void Update(BgpIfmapConfigManager *manager,
                const autogen::RoutingInstance *config);

    // The corresponding if-map node has been deleted.
    void ResetConfig();
    bool DeleteIfEmpty(BgpConfigManager *manager);

    const BgpIfmapProtocolConfig *protocol_config() const {
        return protocol_.get();
    }
    BgpIfmapProtocolConfig *protocol_config_mutable() {
        return protocol_.get();
    }
    BgpIfmapProtocolConfig *LocateProtocol();
    void ResetProtocol();

    void AddNeighbor(BgpConfigManager *manager, BgpNeighborConfig *neighbor);
    void ChangeNeighbor(BgpConfigManager *manager, BgpNeighborConfig *neighbor);
    void DeleteNeighbor(BgpConfigManager *manager, BgpNeighborConfig *neighbor);
    const BgpNeighborConfig *FindNeighbor(const std::string &name) const;

    BgpConfigManager::NeighborMapRange NeighborMapItems() const;

    const NeighborMap &neighbors() const {
        return neighbors_;
    }

    void AddPeering(BgpIfmapPeeringConfig *peering);
    void DeletePeering(BgpIfmapPeeringConfig *peering);

    IFMapNode *node() { return node_proxy_.node(); }
    const std::string &name() const { return name_; }

    BgpInstanceConfig *instance_config() { return &data_; }
    const BgpInstanceConfig *instance_config() const { return &data_; }

    const RouteTargetList &import_list() const {
        return data_.import_list();
    }
    const RouteTargetList &export_list() const {
        return data_.export_list();
    }
    const std::string &virtual_network() const {
        return data_.virtual_network();
    }
    int virtual_network_index() const { return data_.virtual_network_index(); }

private:
    friend class BgpConfigManagerTest;

    std::string name_;
    IFMapNodeProxy node_proxy_;
    BgpInstanceConfig data_;
    boost::scoped_ptr<BgpIfmapProtocolConfig> protocol_;
    NeighborMap neighbors_;
    PeeringMap peerings_;

    DISALLOW_COPY_AND_ASSIGN(BgpIfmapInstanceConfig);
};

//
// BgpConfigData contains all the configuration data that's relevant to a
// node. The BgpConfigManager has a pointer to BgpConfigData.
//
// The IfmapInstanceMap stores pointers to the BgpIfmapInstanceConfigs
// that have been created for all the routing-instances.
// The IfmapPeeringMap stores pointers to the BgpIfmapPeeringConfigs
// that have been created for all the bgp-peerings.
//
class BgpIfmapConfigData {
public:
    typedef BgpConfigManager::InstanceMap BgpInstanceMap;
    typedef std::map<std::string, BgpIfmapInstanceConfig *> IfmapInstanceMap;
    typedef std::map<std::string, BgpIfmapPeeringConfig *> IfmapPeeringMap;

    BgpIfmapConfigData();
    ~BgpIfmapConfigData();

    BgpIfmapInstanceConfig *LocateInstance(const std::string &name);
    void DeleteInstance(BgpIfmapInstanceConfig *rti);
    BgpIfmapInstanceConfig *FindInstance(const std::string &name);
    const BgpIfmapInstanceConfig *FindInstance(const std::string &name) const;

    BgpIfmapPeeringConfig *CreatePeering(BgpIfmapInstanceConfig *rti,
                                         IFMapNodeProxy *proxy);
    void DeletePeering(BgpIfmapPeeringConfig *peer);
    BgpIfmapPeeringConfig *FindPeering(const std::string &name);
    const BgpIfmapPeeringConfig *FindPeering(const std::string &name) const;
    int PeeringCount() const { return peerings_.size(); }

    BgpConfigManager::InstanceMapRange InstanceMapItems() const;

    const IfmapInstanceMap &instances() const { return instances_; }

private:
    IfmapInstanceMap instances_;
    BgpInstanceMap instance_config_map_;
    IfmapPeeringMap peerings_;

    DISALLOW_COPY_AND_ASSIGN(BgpIfmapConfigData);
};

//
// This class is responsible for managing all bgp related configuration for
// a BgpServer. A BgpServer allocates an instance of a BgpConfigManager.
//
// BgpConfigManager listens to updates for IFMap objects which are relevant
// to BGP.  This is accomplished using an instance of the BgpConfigListener.
// The BgpConfigListener is used to build a ChangeList of BgpConfigDeltas
// that are then processed by the BgpConfigManager.
//
// Internal representations of interesting IFMap objects are generated and
// updated as BgpConfigDeltas are processed.  An instance of BgpConfigData
// is used to store the internal representations (the BgpXyzConfig classes).
//
// Notifications of changes to the internal representations are sent to the
// registered Observers. The BgpServer::ConfigUpdater is the only client that
// registers observers.
//
class BgpIfmapConfigManager : public BgpConfigManager,
    public IFMapConfigListener::ConfigManager {
public:
    BgpIfmapConfigManager(BgpServer *server);
    virtual ~BgpIfmapConfigManager();

    void Initialize(DB *db, DBGraph *db_graph, const std::string &localname);

    /*
     * begin: BgpConfigManager Interface
     */
    virtual void Terminate();
    virtual const std::string &localname() const { return localname_; }

    virtual InstanceMapRange InstanceMapItems() const;
    virtual NeighborMapRange NeighborMapItems(
        const std::string &instance_name) const;

    virtual int NeighborCount(const std::string &instance_name) const;

    virtual const BgpInstanceConfig *FindInstance(
        const std::string &name) const;
    virtual const BgpProtocolConfig *GetProtocolConfig(
        const std::string &instance_name) const;
    virtual const BgpNeighborConfig *FindNeighbor(
        const std::string &instance_name, const std::string &name) const;
    // end: BgpConfigManager

    void DefaultBgpRouterParams(autogen::BgpRouterParams &param);
    void OnChange();

    DB *database() { return db_; }
    DBGraph *graph() { return db_graph_; }
    const BgpIfmapConfigData &config() const { return *config_; }

private:
    friend class BgpConfigListenerTest;

    typedef std::vector<BgpConfigDelta> ChangeList;
    typedef std::map<std::string,
        boost::function<void(const BgpConfigDelta &)> >IdentifierMap;

    void IdentifierMapInit();
    void DefaultConfig();

    void ProcessChanges(const ChangeList &change_list);
    void ProcessRoutingInstance(const BgpConfigDelta &change);
    void ProcessBgpRouter(const BgpConfigDelta &change);
    void ProcessBgpProtocol(const BgpConfigDelta &change);
    void ProcessBgpPeering(const BgpConfigDelta &change);

    bool ConfigHandler();

    int config_task_id_;
    static const int kConfigTaskInstanceId;

    DB *db_;
    DBGraph *db_graph_;
    std::string localname_;
    IdentifierMap id_map_;

    TaskTrigger trigger_;
    boost::scoped_ptr<BgpConfigListener> listener_;
    boost::scoped_ptr<BgpIfmapConfigData> config_;

    DISALLOW_COPY_AND_ASSIGN(BgpIfmapConfigManager);
};

#endif
