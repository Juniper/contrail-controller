/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_CONFIG_H__
#define __BGP_CONFIG_H__

#include <map>
#include <set>

#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/queue_task.h"
#include "base/task_trigger.h"
#include "base/util.h"
#include "bgp/bgp_common.h"
#include "ifmap/ifmap_node_proxy.h"
#include "ifmap/ifmap_config_listener.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

class BgpConfigListener;
typedef struct IFMapConfigListener::ConfigDelta BgpConfigDelta;
class BgpConfigManager;
class BgpInstanceConfig;
class BgpPeeringConfig;
class BgpServer;
class DB;
class DBGraph;

//
// BgpNeighborConfig represents a single session between the local bgp-router
// and a remote bgp-router.  There may be multiple BgpNeighborConfigs for one
// BgpPeeringConfig, though there's typically just one.
//
// Each BgpNeighborConfig causes creation of a BgpPeer. BgpPeer has a pointer
// to the BgpNeighborConfig. This pointer is invalidated and cleared when the
// BgpPeer is undergoing deletion.
//
// A BgpNeighborConfig is on the NeighborMap in the BgpPeeringConfig as well
// as the NeighborMap in the BgpInstanceConfig. The BgpInstanceConfig is the
// routing-instance to which the BgpNeighborConfig belongs.
//
// BgpNeighborConfigs get created/updated/deleted when the BgpPeeringConfig is
// created/updated/deleted.
//
// The peer_config_ field stores a copy of the remote bgp-router's properties.
// The attributes_ field stores a copy of the autogen::BgpSessionAttributes
// for the autogen::BgpSession.
//
// The instance_ field is a back pointer to the BgpInstanceConfig object for
// the routing-instance to which this neighbor belongs.
// The uuid_ field is used is there are multiple sessions to the same remote
// bgp-router i.e. there's multiple BgpNeighborConfigs for a BgpPeeringConfig.
//
// The name_ field contains the fully qualified name for the peer.  If uuid_
// is non-empty i.e. there's multiple sessions to the same remote bgp-router,
// the uuid_ is appended to the remote peer's name to make it unique.
//
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
    as_t local_as() const { return (as_t) local_as_; }
    const std::string local_identifier() const { return local_identifier_; }
    as_t peer_as() const { return (as_t) peer_config_.autonomous_system; }
    std::string peer_address() const { return peer_config_.address; }
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
    int local_as_;
    std::string local_identifier_;

    DISALLOW_COPY_AND_ASSIGN(BgpNeighborConfig);
};

//
// A BgpPeeringConfig corresponds to a bgp-peering link in the schema.  Since
// a bgp-peering is a link with attributes, it's represented as a node in the
// configuration database.  There's a single BgpPeeringConfig between a given
// pair of bgp-routers in a particular routing-instance.
//
// A bgp-peering link is represented in IFMap DB using a autogen::BgpPeering
// object. This object has a vector of autogen::BgpSessions, with each entry
// corresponding to a single session. A BgpNeighborConfig is created for each
// session.
//
// The NeighborMap keeps pointers to all the BgpNeighborConfig objects for a
// BgpPeeringConfig.  There's typically just a single entry in this map, but
// we allow for multiple entries if there's more than 1 autogen::BgpSesssion
// in the autogen::BgpPeering.
//
// A BgpPeeringConfig gets created/updated/deleted when the IFMapNode for the
// bgp-peering is created/updated/deleted.
//
// There's no direct operational object corresponding to BgpPeeringConfig. A
// BgpPeeringConfig results in the creation of one or more BgpNeighborConfigs
// each of which is associated with a BgpPeer.
//
// The instance_ field is a back pointer to the BgpInstanceConfig object for
// the routing-instance to which this bgp-peering belongs.
// The IFMapNodeProxy maintains a reference to the IFMapNode object for the
// bgp-peering in the ifmap metadata table.
//
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
    void Update(BgpConfigManager *manager, const autogen::BgpPeering *peering);
    void Delete(BgpConfigManager *manager);

    static bool GetRouterPair(DBGraph *db_graph, const std::string  &localname,
                              IFMapNode *node,
                              std::pair<IFMapNode *, IFMapNode *> *pair);

    const IFMapNode *node() const { return node_proxy_.node(); }
    BgpInstanceConfig *instance() { return instance_; }
    std::string name() const { return name_; }
    size_t size() const { return neighbors_.size(); }
    const autogen::BgpPeering *bgp_peering() const {
        return bgp_peering_.get();
    }

private:
    void BuildNeighbors(BgpConfigManager *manager, const std::string &peername,
                        const autogen::BgpRouter *rt_config,
                        const autogen::BgpPeering *peering, NeighborMap *map);

    BgpInstanceConfig *instance_;
    std::string name_;
    IFMapNodeProxy node_proxy_;
    NeighborMap neighbors_;
    boost::intrusive_ptr<const autogen::BgpPeering> bgp_peering_;

    DISALLOW_COPY_AND_ASSIGN(BgpPeeringConfig);
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
    const BgpInstanceConfig *instance() { return instance_; }

private:
    BgpInstanceConfig *instance_;
    IFMapNodeProxy node_proxy_;
    boost::intrusive_ptr<const autogen::BgpRouter> bgp_router_;

    DISALLOW_COPY_AND_ASSIGN(BgpProtocolConfig);
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
// BgpInstanceConfig is deleted only when all references to it are gone. The
// IFMapNode for the routing-instance must be deleted, any BgpProtcolConfig
// for the local bgp-router in the routing-instance must be deleted and all
// the BgpNeighborConfigs and BgpPeeringConfigs in the routing-instance must
// be deleted.
//
// The protocol_ field is a pointer to the BgpProtocolConfig object for the
// local bgp-router object, if any, in the routing-instance.
// The NeighborMap keeps pointers to all the BgpNeighborConfig objects in a
// BgpInstanceConfig.
// The PeeringMap keeps pointers to all the BgpPeeringConfig objects in the
// BgpInstanceConfig.
//
class BgpInstanceConfig {
public:
    typedef std::map<std::string, BgpNeighborConfig *> NeighborMap;
    typedef std::map<std::string, BgpPeeringConfig *> PeeringMap;
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

    const BgpProtocolConfig *protocol_config() const { return protocol_.get(); }
    BgpProtocolConfig *protocol_config_mutable() { return protocol_.get(); }
    BgpProtocolConfig *LocateProtocol();
    void ResetProtocol();

    void AddNeighbor(BgpConfigManager *manager, BgpNeighborConfig *neighbor);
    void ChangeNeighbor(BgpConfigManager *manager, BgpNeighborConfig *neighbor);
    void DeleteNeighbor(BgpConfigManager *manager, BgpNeighborConfig *neighbor);
    const BgpNeighborConfig *FindNeighbor(std::string name) const;

    const NeighborMap &neighbors() const {
        return neighbors_;
    }

    void AddPeering(BgpPeeringConfig *peering);
    void DeletePeering(BgpPeeringConfig *peering);

    const std::string &virtual_network() const { return virtual_network_; }
    int virtual_network_index() const { return virtual_network_index_; }
    bool virtual_network_allow_transit() const {
        return virtual_network_allow_transit_;
    }

    IFMapNode *node() { return node_proxy_.node(); }
    const std::string &name() const { return name_; }

    const autogen::RoutingInstance *instance_config() const {
        return instance_config_.get();
    }

private:
    friend class BgpConfigManagerTest;
    friend class BgpInstanceConfigTest;

    std::string name_;
    IFMapNodeProxy node_proxy_;
    boost::scoped_ptr<BgpProtocolConfig> protocol_;
    NeighborMap neighbors_;
    PeeringMap peerings_;

    RouteTargetList import_list_;
    RouteTargetList export_list_;
    std::string virtual_network_;
    int virtual_network_index_;
    bool virtual_network_allow_transit_;

    boost::intrusive_ptr<const autogen::RoutingInstance> instance_config_;

    DISALLOW_COPY_AND_ASSIGN(BgpInstanceConfig);
};

//
// BgpConfigData contains all the configuration data that's relevant to a
// node. The BgpConfigManager has a pointer to BgpConfigData.
//
// The BgpInstanceMap stores pointers to the BgpInstanceConfigs that have
// been created for all the routing-instances.
// The BgpPeeringMap stores pointers to the BgpPeeringConfigs that have
// been created for all the bgp-peerings.
//
class BgpConfigData {
public:
    typedef std::map<std::string, BgpInstanceConfig *> BgpInstanceMap;
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
    BgpPeeringConfig *FindPeering(const std::string &name);
    const BgpPeeringConfig *FindPeering(const std::string &name) const;
    const BgpPeeringMap &peerings() const { return peerings_; }

private:
    BgpInstanceMap instances_;
    BgpPeeringMap peerings_;

    DISALLOW_COPY_AND_ASSIGN(BgpConfigData);
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
class BgpConfigManager : public IFMapConfigListener::ConfigManager {
public:
    static const char *kMasterInstance;
    static const int kDefaultPort;
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

    BgpConfigManager(BgpServer *server);
    virtual ~BgpConfigManager();

    void Initialize(DB *db, DBGraph *db_graph, const std::string &localname);
    void Terminate();

    void DefaultBgpRouterParams(autogen::BgpRouterParams &param);
    void OnChange();

    template <typename BgpConfigObject>
    void Notify(const BgpConfigObject *, EventType);

    void RegisterObservers(const Observers &obs) { obs_ = obs; }

    DB *database() { return db_; }
    DBGraph *graph() { return db_graph_; }
    const BgpConfigData &config() const { return *config_; }
    const std::string &localname() const { return localname_; }
    const BgpServer *server() { return server_; }

private:
    friend class BgpConfigListenerTest;
    friend class BgpConfigManagerTest;

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

    static int config_task_id_;
    static const int kConfigTaskInstanceId;

    DB *db_;
    DBGraph *db_graph_;
    BgpServer *server_;
    std::string localname_;
    IdentifierMap id_map_;
    Observers obs_;
    TaskTrigger trigger_;
    boost::scoped_ptr<BgpConfigListener> listener_;
    boost::scoped_ptr<BgpConfigData> config_;

    DISALLOW_COPY_AND_ASSIGN(BgpConfigManager);
};

#endif
