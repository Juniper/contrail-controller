/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config.h"

#include <vector>

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include "base/task.h"
#include "base/task_annotations.h"
#include "base/util.h"
#include "bgp/bgp_config_listener.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_table.h"

using namespace std;

int BgpConfigManager::config_task_id_ = -1;
const int BgpConfigManager::kConfigTaskInstanceId = 0;
const char *BgpConfigManager::kMasterInstance =
        "default-domain:default-project:ip-fabric:__default__";
const int BgpConfigManager::kDefaultPort = 179;
const as_t BgpConfigManager::kDefaultAutonomousSystem = 64512;

BgpNeighborConfig::AddressFamilyList
    BgpNeighborConfig::default_addr_family_list_;

void BgpNeighborConfig::Initialize() {
    default_addr_family_list_.push_back("inet");
    default_addr_family_list_.push_back("inet-vpn");
}

MODULE_INITIALIZER(BgpNeighborConfig::Initialize);

static string IdentifierParent(const string &identifier) {
    string parent;
    size_t last;
    last = identifier.rfind(':');
    if (last == 0 || last == string::npos) {
        return parent;
    }
    parent = identifier.substr(0, last);
    return parent;
}

template<>
void BgpConfigManager::Notify<BgpInstanceConfig>(
        const BgpInstanceConfig *config, EventType event) {
    if (obs_.instance) {
        (obs_.instance)(config, event);
    }
}

template<>
void BgpConfigManager::Notify<BgpProtocolConfig>(
        const BgpProtocolConfig *config, EventType event) {
    if (obs_.protocol) {
        (obs_.protocol)(config, event);
    }
}

template<>
void BgpConfigManager::Notify<BgpNeighborConfig>(
        const BgpNeighborConfig *config, EventType event) {
    if (obs_.neighbor) {
        (obs_.neighbor)(config, event);
    }
}

void BgpPeeringConfig::SetNodeProxy(IFMapNodeProxy *proxy) {
    if (proxy != NULL) {
        node_proxy_.Swap(proxy);
        name_ = node_proxy_.node()->name();
    }
}

//
// Build map of BgpNeighborConfigs based on the data in autogen::BgpPeering.
//
void BgpPeeringConfig::BuildNeighbors(BgpConfigManager *manager,
        const string &peername, const autogen::BgpRouter *rt_config,
        const autogen::BgpPeering *peering, NeighborMap *map) {

    // If there are one or more autogen::BgpSessions for the peering, use
    // those to create the BgpNeighborConfigs.
    const autogen::BgpPeeringAttributes &attr = peering->data();
    for (autogen::BgpPeeringAttributes::const_iterator iter = attr.begin();
         iter != attr.end(); ++iter) {
        BgpNeighborConfig *neighbor =
                new BgpNeighborConfig(instance_, peername,
                        manager->localname(), rt_config,
                        iter.operator->());
        map->insert(make_pair(neighbor->name(), neighbor));
    }

    // When no sessions are present, create a single BgpNeighborConfig with
    // no per-session configuration.
    if (map->empty()) {
        BgpNeighborConfig *neighbor =
                new BgpNeighborConfig(instance_, peername,
                        manager->localname(),
                        rt_config, NULL);
        map->insert(make_pair(neighbor->name(), neighbor));
    }
}

//
// Update BgpPeeringConfig based on updated autogen::BgpPeering.
//
// This mainly involves building future BgpNeighborConfigs and doing a diff of
// the current and future BgpNeighborConfigs.  Note that the BgpInstanceConfig
// also has references to BgpNeighborConfigs, so it also needs to be updated as
// part of the process.
//
void BgpPeeringConfig::Update(BgpConfigManager *manager,
        const autogen::BgpPeering *peering) {
    IFMapNode *node = node_proxy_.node();
    assert(node != NULL);
    bgp_peering_.reset(peering);

    // Build the future NeighborMap.  The future map should be empty if the
    // bgp-peering is deleted or if the parameters for the remote bgp-router
    // are not available.
    NeighborMap future;
    pair<IFMapNode *, IFMapNode *> routers;
    if (!node->IsDeleted() &&
        GetRouterPair(manager->graph(), manager->localname(), node, &routers)) {
        const autogen::BgpRouter *rt_config =
                static_cast<const autogen::BgpRouter *>(
                        routers.second->GetObject());
        if (rt_config != NULL &&
            rt_config->IsPropertySet(autogen::BgpRouter::PARAMETERS)) {
            BuildNeighbors(manager, routers.second->name(), rt_config,
                    peering, &future);
        }
    }

    // Swap out the NeighborMap in preparation for doing a diff.
    NeighborMap current;
    current.swap(neighbors_);

    // Do a diff on the current and future BgpNeighborConfigs, making sure
    // that the BgpInstanceConfig is updated accordingly.  We add any new
    // BgpNeighborConfigs to our NeighborMap.
    NeighborMap::iterator it1 = current.begin();
    NeighborMap::iterator it2 = future.begin();
    while (it1 != current.end() && it2 != future.end()) {
        if (it1->first < it2->first) {
            BgpNeighborConfig *prev = it1->second;
            instance_->DeleteNeighbor(manager, prev);
            ++it1;
        } else if (it1->first > it2->first) {
            BgpNeighborConfig *neighbor = it2->second;
            instance_->AddNeighbor(manager, neighbor);
            neighbors_.insert(*it2);
            it2->second = NULL;
            ++it2;
        } else {
            BgpNeighborConfig *neighbor = it1->second;
            BgpNeighborConfig *update = it2->second;
            if (*neighbor != *update) {
                neighbor->Update(update);
                instance_->ChangeNeighbor(manager, neighbor);
            }
            neighbors_.insert(*it1);
            it1->second = NULL;
            ++it1;
            ++it2;
        }
    }
    for (; it1 != current.end(); ++it1) {
        BgpNeighborConfig *prev = it1->second;
        instance_->DeleteNeighbor(manager, prev);
    }
    for (; it2 != future.end(); ++it2) {
        BgpNeighborConfig *neighbor = it2->second;
        instance_->AddNeighbor(manager, neighbor);
        neighbors_.insert(*it2);
        it2->second = NULL;
    }

    // Get rid of the current and future NeighborMaps and destroy any mapped
    // BgpNeighborConfigs. Note that we have carefully reset mapped values to
    // NULL above when we don't want a BgpNeighborConfig to get destroyed.
    STLDeleteElements(&current);
    STLDeleteElements(&future);
}

//
// Delete all state for the given BgpPeeringConfig.
//
// This mainly involves getting rid of BgpNeighborConfigs in the NeighborMap.
//
void BgpPeeringConfig::Delete(BgpConfigManager *manager) {
    NeighborMap current;
    current.swap(neighbors_);
    for (NeighborMap::iterator iter = current.begin();
         iter != current.end(); ++iter) {
        instance_->DeleteNeighbor(manager, iter->second);
    }
    STLDeleteElements(&current);
    bgp_peering_.reset();
}

//
// Find the IFMapNodes for a bgp-peering.
//
// The "node" is the IFMapNode for the bgp-peering link.  The bgp-peering is
// interesting only if one of the bgp-routers is the local node.
//
// Return true if both bgp-routers for the bgp-peering exist and one of them
// is the local one. Also fill in the local and remote IFMapNode pointers if
// we return true.
//
bool BgpPeeringConfig::GetRouterPair(DBGraph *db_graph,
        const string  &localname, IFMapNode *node,
        pair<IFMapNode *, IFMapNode *> *pair) {
    IFMapNode *local = NULL;
    IFMapNode *remote = NULL;

    for (DBGraphVertex::adjacency_iterator iter = node->begin(db_graph);
         iter != node->end(db_graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (strcmp(adj->table()->Typename(), "bgp-router") != 0)
            continue;
        string instance_name(IdentifierParent(adj->name()));
        string name = adj->name().substr(instance_name.size() + 1);
        if (name == localname) {
            local = adj;
        } else {
            remote = adj;
        }
    }
    if (local == NULL || remote == NULL) {
        return false;
    }

    pair->first = local;
    pair->second = remote;
    return true;
}

//
// Constructor for BgpNeighborConfig.
//
// Note that we do not add ourselves to the BgpInstanceConfig here. That will
// done if necessary by the caller.
//
BgpNeighborConfig::BgpNeighborConfig(const BgpInstanceConfig *instance,
                                     const string &remote_name,
                                     const string &local_name,
                                     const autogen::BgpRouter *router,
                                     const autogen::BgpSession *session)
        : instance_(instance), local_as_(0) {

    // If the autogen::BgpSession has a uuid, we append it to the remote
    // bgp-router's name to make the BgpNeighborConfig's name unique.
    if (session && !session->uuid.empty()) {
        uuid_ = session->uuid;
        name_ = remote_name + ":" + session->uuid;
    } else {
        name_ = remote_name;
    }

    // Store a copy of the remote bgp-router's autogen::BgpRouterParams and
    // derive the autogen::BgpSessionAttributes for the session.
    const autogen::BgpRouterParams &params = router->parameters();
    peer_config_.Copy(params);
    attributes_.Clear();
    if (session != NULL) {
        SetSessionAttributes(local_name, session);
    }

    // Get the local identifier and local as from the protocol config.
    const BgpProtocolConfig *protocol = instance->protocol_config();
    if (protocol && protocol->bgp_router()) {
        const autogen::BgpRouterParams &params = protocol->router_params();
        local_identifier_ = params.identifier;
        local_as_ = params.autonomous_system;
    }
}

//
// Destructor for BgpNeighborConfig.
//
BgpNeighborConfig::~BgpNeighborConfig() {
}

//
// Set the autogen::BgpSessionAttributes for this BgpNeighborConfig.
//
// The autogen::BgpSession will have up to 3 session attributes - one that
// applies to the local router, one that applies the remote router and one
// that applies to both.
//
void BgpNeighborConfig::SetSessionAttributes(const string &localname,
        const autogen::BgpSession *session) {
    typedef vector<autogen::BgpSessionAttributes> AttributeVec;
    const autogen::BgpSessionAttributes *common = NULL;
    const autogen::BgpSessionAttributes *local = NULL;
    for (AttributeVec::const_iterator iter = session->attributes.begin();
         iter != session->attributes.end(); ++iter) {
        const autogen::BgpSessionAttributes *attr = iter.operator->();
        if (attr->bgp_router.empty()) {
            common = attr;
        } else if (attr->bgp_router == localname) {
            local = attr;
        }
    }

    // TODO: local should override rather than replace common.
    if (common != NULL) {
        attributes_ = *common;
    }
    if (local != NULL) {
        attributes_ = *local;
    }
}

//
// Get the address family list for the BgpNeighborConfig.
//
// If there's address family configuration specific to this session use that.
// Otherwise, the local bgp-router's address config if available.
// Otherwise, use the hard coded default.
//
const BgpNeighborConfig::AddressFamilyList &
BgpNeighborConfig::address_families() const {
    const autogen::BgpSessionAttributes &attr = session_attributes();
    if (!attr.address_families.family.empty()) {
        return attr.address_families.family;
    }

    const BgpProtocolConfig *protocol = instance_->protocol_config();
    if (protocol && protocol->bgp_router()) {
        const autogen::BgpRouterParams &params = protocol->router_params();
        if (!params.address_families.family.empty()) {
            return params.address_families.family;
        }
    }

    return default_addr_family_list_;
}

// TODO: compare peer_config_ and attributes_
bool BgpNeighborConfig::operator!=(const BgpNeighborConfig &rhs) const {
    return true;
}

//
// Update BgpNeighborConfig from the supplied value.
//
void BgpNeighborConfig::Update(const BgpNeighborConfig *rhs) {
    peer_config_ = rhs->peer_config_;
    attributes_ = rhs->attributes_;
    local_as_ = rhs->local_as_;
    local_identifier_ = rhs->local_identifier_;
}

string BgpNeighborConfig::InstanceName() const {
    return instance_->name();
}

//
// Constructor for BgpProtocolConfig.
//
BgpProtocolConfig::BgpProtocolConfig(BgpInstanceConfig *instance)
    : instance_(instance) {
}

//
// Destructor for BgpProtocolConfig.
//
BgpProtocolConfig::~BgpProtocolConfig() {
}

//
// Set the IFMapNodeProxy for the BgpProtocolConfig.
//
void BgpProtocolConfig::SetNodeProxy(IFMapNodeProxy *proxy) {
    if (proxy != NULL) {
        node_proxy_.Swap(proxy);
    }
}

//
// Update autogen::BgpRouter object for this BgpProtocolConfig.
//
void BgpProtocolConfig::Update(BgpConfigManager *manager,
                               const autogen::BgpRouter *router) {
    bgp_router_.reset(router);
}

//
// Delete autogen::BgpRouter object for this BgpProtocolConfig.
//
void BgpProtocolConfig::Delete(BgpConfigManager *manager) {
    manager->Notify(this, BgpConfigManager::CFG_DELETE);
    bgp_router_.reset();
}

const string &BgpProtocolConfig::InstanceName() const {
    return instance_->name();
}

//
// Constructor for BgpInstanceConfig.
//
BgpInstanceConfig::BgpInstanceConfig(const string &name)
    : name_(name),
      protocol_(NULL),
      virtual_network_index_(0),
      virtual_network_allow_transit_(false) {
}

//
// Destructor for BgpInstanceConfig.
//
BgpInstanceConfig::~BgpInstanceConfig() {
}

//
// Set the IFMapNodeProxy for the BgpInstanceConfig.
//
void BgpInstanceConfig::SetNodeProxy(IFMapNodeProxy *proxy) {
    if (proxy != NULL) {
        node_proxy_.Swap(proxy);
    }
}

//
// Get the BgpProtocolConfig for this BgpInstanceConfig, create it if needed.
//
BgpProtocolConfig *BgpInstanceConfig::LocateProtocol() {
    if (protocol_.get() == NULL) {
        protocol_.reset(new BgpProtocolConfig(this));
    }
    return protocol_.get();
}

//
// Delete the BgpProtocolConfig for this BgpInstanceConfig.
//
void BgpInstanceConfig::ResetProtocol() {
    protocol_.reset();
}

//
// Get the route-target for an instance-target. The input IFMapNode is the
// midnode that represents the instance-target link. We traverse the graph
// the graph edges till we find the adjacency to the route-target.
//
// Return true and fill in the target string if we find the route-target.
//
static bool GetInstanceTargetRouteTarget(DBGraph *graph, IFMapNode *node,
        string *target) {
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (strcmp(adj->table()->Typename(), "route-target") == 0) {
            *target = adj->name();
            return true;
        }
    }
    return false;
}

//
// Fill in all the export route targets for a routing-instance.  The input
// IFMapNode represents the routing-instance.  We traverse the graph edges
// and look for instance-target adjacencies. If the instance-target has is
// an export target i.e. it's not import-only, add the route-target to the
// vector.
//
static void GetRoutingInstanceExportTargets(DBGraph *graph, IFMapNode *node,
        vector<string> *target_list) {
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        string target;
        if ((strcmp(adj->table()->Typename(), "instance-target") == 0) &&
            (GetInstanceTargetRouteTarget(graph, adj, &target))) {
            const autogen::InstanceTarget *itarget =
                    dynamic_cast<autogen::InstanceTarget *>(adj->GetObject());
            assert(itarget);
            const autogen::InstanceTargetType &itt = itarget->data();
            if (itt.import_export != "import")
                target_list->push_back(target);
        }
    }
}

//
// Get the network id for a virtual-network.  The input IFMapNode represents
// the virtual-network.
//
static int GetVirtualNetworkIndex(DBGraph *graph, IFMapNode *node) {
    const autogen::VirtualNetwork *vn =
        static_cast<autogen::VirtualNetwork *>(node->GetObject());
    if (vn && vn->IsPropertySet(autogen::VirtualNetwork::PROPERTIES))
        return vn->properties().network_id;
    return 0;
}

//
// Check if a virtual-network allows transit. The input IFMapNode represents
// the virtual-network.
//
static bool GetVirtualNetworkAllowTransit(DBGraph *graph, IFMapNode *node) {
    const autogen::VirtualNetwork *vn =
        static_cast<autogen::VirtualNetwork *>(node->GetObject());
    if (vn && vn->IsPropertySet(autogen::VirtualNetwork::PROPERTIES))
        return vn->properties().allow_transit;
    return false;
}

//
// Update BgpInstanceConfig based on a new autogen::RoutingInstance object.
//
// Rebuild the import and export route target lists and update the virtual
// network information.
//
// Targets that are configured on this routing-instance (which corresponds
// to all adjacencies to instance-target) are added to the import list or
// export list or both depending on the import_export attribute.
//
// Export targets for all other routing-instances that we are connected to
// are added to our import list.
//
void BgpInstanceConfig::Update(BgpConfigManager *manager,
                               const autogen::RoutingInstance *config) {
    import_list_.clear();
    export_list_.clear();

    DBGraph *graph = manager->graph();
    IFMapNode *node = node_proxy_.node();
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (strcmp(adj->table()->Typename(), "instance-target") == 0) {
            string target;
            if (GetInstanceTargetRouteTarget(graph, adj, &target)) {
                const autogen::InstanceTarget *itarget =
                    dynamic_cast<autogen::InstanceTarget *>(adj->GetObject());
                assert(itarget);
                const autogen::InstanceTargetType &itt = itarget->data();
                if (itt.import_export == "import") {
                    import_list_.insert(target);
                } else if (itt.import_export == "export") {
                    export_list_.insert(target);
                } else {
                    import_list_.insert(target);
                    export_list_.insert(target);
                }
            }
        } else if (strcmp(adj->table()->Typename(), "routing-instance") == 0) {
            vector<string> target_list;
            GetRoutingInstanceExportTargets(graph, adj, &target_list);
            BOOST_FOREACH(string target, target_list) {
                import_list_.insert(target);
            }
        } else if (strcmp(adj->table()->Typename(), "virtual-network") == 0) {
            virtual_network_ = adj->name();
            virtual_network_index_ = GetVirtualNetworkIndex(graph, adj);
            virtual_network_allow_transit_ =
                GetVirtualNetworkAllowTransit(graph, adj);
        }
    }

    instance_config_.reset(config);
}

//
// Reset IFMap related state in the BgpInstanceConfig.
//
void BgpInstanceConfig::ResetConfig() {
    instance_config_.reset();
    node_proxy_.Clear();
}

//
// Return true if the BgpInstanceConfig is ready to be deleted.  The caller is
// responsible for actually deleting it.
//
bool BgpInstanceConfig::DeleteIfEmpty(BgpConfigManager *manager) {
    if (name_ == BgpConfigManager::kMasterInstance) {
        return false;
    }
    if (node() != NULL || protocol_.get() != NULL) {
        return false;
    }
    if (!neighbors_.empty() || !peerings_.empty()) {
        return false;
    }

    manager->Notify(this, BgpConfigManager::CFG_DELETE);
    return true;
}

//
// Add a BgpNeighborConfig to this BgpInstanceConfig.
//
// The BgpNeighborConfig is added to the NeighborMap and the BgpConfigManager
// is notified.
//
void BgpInstanceConfig::AddNeighbor(BgpConfigManager *manager,
                                    BgpNeighborConfig *neighbor) {
    vector<string> families(
        neighbor->session_attributes().address_families.begin(),
        neighbor->session_attributes().address_families.end());
    BGP_CONFIG_LOG_NEIGHBOR(Create, manager->server(), neighbor,
        SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
        neighbor->local_identifier(), neighbor->local_as(),
        neighbor->peer_address(), neighbor->peer_as(), families);
    neighbors_.insert(make_pair(neighbor->name(), neighbor));
    manager->Notify(neighbor, BgpConfigManager::CFG_ADD);
}

//
// Change a BgpNeighborConfig that's already in this BgpInstanceConfig.
//
void BgpInstanceConfig::ChangeNeighbor(BgpConfigManager *manager,
                                       BgpNeighborConfig *neighbor) {
    vector<string> families(
        neighbor->session_attributes().address_families.begin(),
        neighbor->session_attributes().address_families.end());
    BGP_CONFIG_LOG_NEIGHBOR(Update, manager->server(), neighbor,
        SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
        neighbor->local_identifier(), neighbor->local_as(),
        neighbor->peer_address(), neighbor->peer_as(), families);
    manager->Notify(neighbor, BgpConfigManager::CFG_CHANGE);
}

//
// Delete a BgpNeighborConfig from this BgpInstanceConfig.
//
// The BgpConfigManager is notified and BgpNeighborConfig is removed from the
// NeighborMap. Note that the caller is responsible for actually deleting the
// BgpNeighborConfig object.
//
void BgpInstanceConfig::DeleteNeighbor(BgpConfigManager *manager,
                                       BgpNeighborConfig *neighbor) {
    BGP_CONFIG_LOG_NEIGHBOR(Delete, manager->server(), neighbor,
        SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
    manager->Notify(neighbor, BgpConfigManager::CFG_DELETE);
    neighbors_.erase(neighbor->name());
}

//
// Find the BgpNeighborConfig by name in this BgpInstanceConfig.
//
const BgpNeighborConfig *BgpInstanceConfig::FindNeighbor(string name) const {
    NeighborMap::const_iterator loc = neighbors_.find(name);
    return loc != neighbors_.end() ? loc->second : NULL;
}

//
// Add a BgpPeeringConfig to this BgpInstanceConfig.
//
void BgpInstanceConfig::AddPeering(BgpPeeringConfig *peering) {
    peerings_.insert(make_pair(peering->name(), peering));
}

//
// Delete a BgpPeeringConfig from this BgpInstanceConfig.
//
void BgpInstanceConfig::DeletePeering(BgpPeeringConfig *peering) {
    peerings_.erase(peering->name());
}

//
// Constructor for BgpConfigData.
//
BgpConfigData::BgpConfigData() {
}

//
// Destructor for BgpConfigData.
//
BgpConfigData::~BgpConfigData() {
    STLDeleteElements(&instances_);
    STLDeleteElements(&peerings_);
}

//
// Locate the BgpInstanceConfig by name, create it if not found.  The newly
// created BgpInstanceConfig gets added to the BgpInstanceMap.
//
// Note that we do not have the IFMapNode representing the routing-instance
// at this point.
//
BgpInstanceConfig *BgpConfigData::LocateInstance(const string &name) {
    BgpInstanceConfig *rti = FindInstance(name);
    if (rti == NULL) {
        rti = new BgpInstanceConfig(name);
        pair<BgpInstanceMap::iterator, bool> result =
                instances_.insert(make_pair(name, rti));
        assert(result.second);
    }
    return rti;
}

//
// Remove the given BgpInstanceConfig from the BgpInstanceMap and delete it.
//
void BgpConfigData::DeleteInstance(BgpInstanceConfig *rti) {
    BgpInstanceMap::iterator loc = instances_.find(rti->name());
    assert(loc != instances_.end());
    instances_.erase(loc);
    delete rti;
}

//
// Find the BgpInstanceConfig by name.
//
BgpInstanceConfig *BgpConfigData::FindInstance(const string &name) {
    BgpInstanceMap::iterator loc = instances_.find(name);
    if (loc != instances_.end()) {
        return loc->second;
    }
    return NULL;
}

//
// Find the BgpInstanceConfig by name.
// Const version.
//
const BgpInstanceConfig *BgpConfigData::FindInstance(const string &name) const {
    BgpInstanceMap::const_iterator loc = instances_.find(name);
    if (loc != instances_.end()) {
        return loc->second;
    }
    return NULL;
}

//
// Create a new BgpPeeringConfig.
//
// The IFMapNodeProxy is a proxy for the IFMapNode which is the midnode that
// represents the bgp-peering. The newly created BgpPeeringConfig gets added
// to the BgpPeeringMap.
//
BgpPeeringConfig *BgpConfigData::CreatePeering(BgpInstanceConfig *rti,
                                               IFMapNodeProxy *proxy) {
    BgpPeeringConfig *peering = new BgpPeeringConfig(rti);
    peering->SetNodeProxy(proxy);
    pair<BgpPeeringMap::iterator, bool> result =
            peerings_.insert(make_pair(peering->node()->name(), peering));
    assert(result.second);
    peering->instance()->AddPeering(peering);
    return peering;
}

//
// Delete a BgpPeeringConfig.
//
// The BgpPeeringConfig is removed from the BgpPeeringMap and then deleted.
// Note that the reference to the IFMapNode for the bgp-peering gets released
// via the destructor when the IFMapNodeProxy is destroyed.
//
void BgpConfigData::DeletePeering(BgpPeeringConfig *peering) {
    peering->instance()->DeletePeering(peering);
    peerings_.erase(peering->node()->name());
    delete peering;
}

//
// Find the BgpPeeringConfig by name.
//
BgpPeeringConfig *BgpConfigData::FindPeering(const string &name) {
    BgpPeeringMap::iterator loc = peerings_.find(name);
    if (loc != peerings_.end()) {
        return loc->second;
    }
    return NULL;
}

//
// Find the BgpPeeringConfig by name.
// Const version.
//
const BgpPeeringConfig *BgpConfigData::FindPeering(const string &name) const {
    BgpPeeringMap::const_iterator loc = peerings_.find(name);
    if (loc != peerings_.end()) {
        return loc->second;
    }
    return NULL;
}

//
// Constructor for BgpConfigManager.
//
BgpConfigManager::BgpConfigManager(BgpServer *server)
        : db_(NULL), db_graph_(NULL), server_(server),
          trigger_(boost::bind(&BgpConfigManager::ConfigHandler, this),
                   TaskScheduler::GetInstance()->GetTaskId("bgp::Config"),
                   kConfigTaskInstanceId),
          listener_(BgpObjectFactory::Create<BgpConfigListener>(this)),
          config_(new BgpConfigData()) {
    IdentifierMapInit();

    if (config_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        config_task_id_ = scheduler->GetTaskId("bgp::Config");
    }
}

//
// Destructor for BgpConfigManager.
//
BgpConfigManager::~BgpConfigManager() {
}

//
// Initialize the BgpConfigManager.
//
void BgpConfigManager::Initialize(DB *db, DBGraph *db_graph,
                                  const string &localname) {
    db_ = db;
    db_graph_ = db_graph;
    localname_ = localname;
    listener_->Initialize();
    DefaultConfig();
}

//
// Used to trigger a build and subsequent evaluation of the ChangeList.
//
void BgpConfigManager::OnChange() {
    CHECK_CONCURRENCY("db::DBTable");
    trigger_.Set();
}

//
// Initialize autogen::BgpRouterParams with default values.
//
void BgpConfigManager::DefaultBgpRouterParams(autogen::BgpRouterParams &param) {
    param.Clear();
    param.autonomous_system = BgpConfigManager::kDefaultAutonomousSystem;
    param.port = BgpConfigManager::kDefaultPort;
}

//
// Create BgpInsatnceConfig for master routing-instance.  This includes the
// BgpProtocolConfig for the local bgp-router in the master routing-instance.
//
void BgpConfigManager::DefaultConfig() {
    BgpInstanceConfig *rti = config_->LocateInstance(kMasterInstance);
    auto_ptr<autogen::BgpRouter> router(new autogen::BgpRouter());
    autogen::BgpRouterParams param;
    DefaultBgpRouterParams(param);
    router->SetProperty("bgp-router-parameters", &param);
    BgpProtocolConfig *protocol = rti->LocateProtocol();
    protocol->Update(this, router.release());
    Notify(rti, BgpConfigManager::CFG_ADD);

    vector<string> import_rt;
    vector<string> export_rt;
    BGP_CONFIG_LOG_INSTANCE(Create, server_, rti,
        SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
        import_rt, export_rt,
        rti->virtual_network(), rti->virtual_network_index());

    vector<string> families(
        protocol->router_params().address_families.begin(),
        protocol->router_params().address_families.end());
    BGP_CONFIG_LOG_PROTOCOL(Create, server_, protocol,
        SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
        protocol->router_params().autonomous_system,
        protocol->router_params().identifier,
        protocol->router_params().address,
        protocol->router_params().hold_time,
        families);
}

//
// Initialize IdentifierMap with handlers for interesting identifier types.
//
// The IdentifierMap is used when processing BgpConfigDeltas generated by
// the BgpConfigListener.
//
void BgpConfigManager::IdentifierMapInit() {
    id_map_.insert(make_pair("routing-instance",
            boost::bind(&BgpConfigManager::ProcessRoutingInstance, this, _1)));
    id_map_.insert(make_pair("bgp-router",
            boost::bind(&BgpConfigManager::ProcessBgpRouter, this, _1)));
    id_map_.insert(make_pair("bgp-peering",
            boost::bind(&BgpConfigManager::ProcessBgpPeering, this, _1)));
}

//
// Handler for routing-instance objects.
//
// Note that the BgpInstanceConfig object for the master instance is created
// before we have received any configuration for it i.e. there's no IFMapNode
// or autogen::RoutingInstance for it. However, we will eventually receive a
// delta for the master.  The IFMapNodeProxy and autogen::RoutingInstance are
// set at that time.
//
// For other routing-instances the BgpConfigInstance can get created before we
// see the IFMapNode for the routing-instance if we see the IFMapNode for the
// local bgp-router in the routing-instance.  In this case, the IFMapNodeProxy
// and autogen::RoutingInstance are set when we later see the IFMapNode for the
// routing-instance.
//
// In all other cases a BgpConfigInstance is created when we see the IFMapNode
// for the routing-instance.  The IFMapNodeProxy and autogen::RoutingInstance
// are set right away.
//
// References to the IFMapNode and the autogen::RoutingInstance are released
// when the IFMapNode is marked deleted.  However the BgpInstanceConfig does
// not get deleted till the NeighborMap is empty and the BgpProtocolConfig is
// gone.
//
void BgpConfigManager::ProcessRoutingInstance(const BgpConfigDelta &delta) {
    CHECK_CONCURRENCY("bgp::Config");

    BgpConfigManager::EventType event = BgpConfigManager::CFG_CHANGE;
    string instance_name = delta.id_name;
    BgpInstanceConfig *rti = config_->FindInstance(instance_name);
    if (rti == NULL) {
        IFMapNodeProxy *proxy = delta.node.get();
        if (proxy == NULL) {
            return;
        }
        IFMapNode *node = proxy->node();
        if (node == NULL || node->IsDeleted()) {
            return;
        }
        event = BgpConfigManager::CFG_ADD;
        rti = config_->LocateInstance(instance_name);
        rti->SetNodeProxy(proxy);
    } else {
        IFMapNode *node = rti->node();
        if (node == NULL) {
            IFMapNodeProxy *proxy = delta.node.get();
            if (proxy == NULL) {
                return;
            }
            rti->SetNodeProxy(proxy);
        } else if (node->IsDeleted()) {
            rti->ResetConfig();
            if (rti->DeleteIfEmpty(this)) {
                BGP_CONFIG_LOG_INSTANCE(Delete, server_, rti,
                    SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
                config_->DeleteInstance(rti);
            }
            return;
        }
    }

    autogen::RoutingInstance *rti_config =
        static_cast<autogen::RoutingInstance *>(delta.obj.get());
    rti->Update(this, rti_config);
    Notify(rti, event);

    vector<string> import_rt(rti->import_list().begin(),
                             rti->import_list().end());
    vector<string> export_rt(rti->export_list().begin(),
                             rti->export_list().end());
    if (event == BgpConfigManager::CFG_ADD) {
        BGP_CONFIG_LOG_INSTANCE(Create, server_, rti,
            SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            import_rt, export_rt,
            rti->virtual_network(), rti->virtual_network_index());
    } else {
        BGP_CONFIG_LOG_INSTANCE(Update, server_, rti,
            SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            import_rt, export_rt,
            rti->virtual_network(), rti->virtual_network_index());
    }
}

//
// Handler for bgp protocol config.
//
// Note that there's no underlying IFMap object for the bgp protocol config.
// This is called by the handler for bgp-router objects. The BgpConfigDelta
// is a delta for the bgp-router object.
//
void BgpConfigManager::ProcessBgpProtocol(const BgpConfigDelta &delta) {
    CHECK_CONCURRENCY("bgp::Config");

    BgpConfigManager::EventType event = BgpConfigManager::CFG_CHANGE;
    string instance_name(IdentifierParent(delta.id_name));
    BgpInstanceConfig *rti = config_->FindInstance(instance_name);
    BgpProtocolConfig *protocol = NULL;
    if (rti != NULL) {
        protocol = rti->protocol_config_mutable();
    }

    if (protocol == NULL) {
        IFMapNodeProxy *proxy = delta.node.get();
        if (proxy == NULL) {
            return;
        }
        // ignore identifier with no properties
        if (delta.obj.get() == NULL) {
            return;
        }
        IFMapNode *node = proxy->node();
        if (node == NULL || node->IsDeleted()) {
            return;
        }
        event = BgpConfigManager::CFG_ADD;
        if (rti == NULL) {
            rti = config_->LocateInstance(instance_name);
            Notify(rti, BgpConfigManager::CFG_ADD);

            vector<string> import_rt(rti->import_list().begin(),
                                     rti->import_list().end());
            vector<string> export_rt(rti->export_list().begin(),
                                     rti->export_list().end());
            BGP_CONFIG_LOG_INSTANCE(Create, server_, rti,
                SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
                import_rt, export_rt,
                rti->virtual_network(), rti->virtual_network_index());
        }
        protocol = rti->LocateProtocol();
        protocol->SetNodeProxy(proxy);
    } else {
        IFMapNode *node = protocol->node();
        if (node == NULL) {
            // The master instance creates a BgpRouter node internally. Ignore
            // an update that doesn't specify any content.
            if (delta.obj.get() == NULL) {
                return;
            }
            protocol->SetNodeProxy(delta.node.get());
        } else if (node->IsDeleted()) {
            BGP_CONFIG_LOG_PROTOCOL(Delete, server_, protocol,
                SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
            protocol->Delete(this);
            rti->ResetProtocol();
            if (rti->DeleteIfEmpty(this)) {
                BGP_CONFIG_LOG_INSTANCE(Delete, server_, rti,
                    SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
                config_->DeleteInstance(rti);
            }
            return;
        }
    }

    autogen::BgpRouter *rt_config =
        static_cast<autogen::BgpRouter *>(delta.obj.get());
    protocol->Update(this, rt_config);
    Notify(protocol, event);

    if (!rt_config) {
        return;
    }

    vector<string> families(
        protocol->router_params().address_families.begin(),
        protocol->router_params().address_families.end());
    if (event == BgpConfigManager::CFG_ADD) {
        BGP_CONFIG_LOG_PROTOCOL(Create, server_, protocol,
            SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            protocol->router_params().autonomous_system,
            protocol->router_params().identifier,
            protocol->router_params().address,
            protocol->router_params().hold_time,
            families);
    } else {
        BGP_CONFIG_LOG_PROTOCOL(Update, server_, protocol,
            SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            protocol->router_params().autonomous_system,
            protocol->router_params().identifier,
            protocol->router_params().address,
            protocol->router_params().hold_time,
            families);
    }
}

//
// Handler for bgp-router objects.
//
// Note that we don't need to explicitly re-evaluate any bgp-peerings as the
// BgpConfigListener::DependencyTracker adds any relevant bgp-peerings to the
// change list.
//
void BgpConfigManager::ProcessBgpRouter(const BgpConfigDelta &delta) {
    CHECK_CONCURRENCY("bgp::Config");

    string instance_name(IdentifierParent(delta.id_name));
    if (instance_name.empty()) {
        return;
    }

    // Ignore if this change is not for the local router.
    string name = delta.id_name.substr(instance_name.size() + 1);
    if (name != localname_) {
        return;
    }

    ProcessBgpProtocol(delta);
}

//
// Handler for bgp-peering objects.
//
// We are only interested in this bgp-peering if it is adjacent to the local
// router node.
//
// The BgpPeeringConfig is created the first time we see the bgp-peering.  It
// is updated on subsequent changes and is deleted when the IFMapNode midnode
// for the bgp-peering is deleted.
//
// Note that we are guaranteed that the IFMapNodes for the 2 bgp-routers for
// the bgp-peering already exist when we see the bgp-peering.  IFMap creates
// the nodes for the bgp-routers before creating the midnode.  This in turn
// guarantees that the BgpInstanceConfig for the routing-instance also exists
// since we create the BgpInstanceConfig before creating the BgpProtocolConfig
// for a local bgp-router.
//
void BgpConfigManager::ProcessBgpPeering(const BgpConfigDelta &delta) {
    CHECK_CONCURRENCY("bgp::Config");

    BgpConfigManager::EventType event = BgpConfigManager::CFG_CHANGE;
    BgpPeeringConfig *peering = config_->FindPeering(delta.id_name);
    if (peering == NULL) {
        IFMapNodeProxy *proxy = delta.node.get();
        if (proxy == NULL) {
            return;
        }
        IFMapNode *node = proxy->node();
        if (node == NULL) {
            return;
        }

        pair<IFMapNode *, IFMapNode *> routers;
        if (!BgpPeeringConfig::GetRouterPair(db_graph_, localname_, node,
                                             &routers)) {
            return;
        }

        event = BgpConfigManager::CFG_ADD;
        string instance_name(IdentifierParent(routers.first->name()));
        BgpInstanceConfig *rti = config_->FindInstance(instance_name);
        assert(rti != NULL);
        peering = config_->CreatePeering(rti, proxy);
    } else {
        const IFMapNode *node = peering->node();
        assert(node != NULL);
        if (node->IsDeleted()) {
            BGP_CONFIG_LOG_PEERING(Delete, server_, peering,
                SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
            BgpInstanceConfig *rti = peering->instance();
            peering->Delete(this);
            config_->DeletePeering(peering);
            if (rti->DeleteIfEmpty(this)) {
                BGP_CONFIG_LOG_INSTANCE(Delete, server_, rti,
                    SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
                config_->DeleteInstance(rti);
            }
            return;
        }
    }

    if (event == BgpConfigManager::CFG_ADD) {
        BGP_CONFIG_LOG_PEERING(Create, server_, peering,
            SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
    } else {
        BGP_CONFIG_LOG_PEERING(Update, server_, peering,
            SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
    }
    autogen::BgpPeering *peering_config =
            static_cast<autogen::BgpPeering *>(delta.obj.get());
    peering->Update(this, peering_config);
}

//
// Process the BgpConfigDeltas on the change list. We simply call the handler
// for each delta based on the object's identifier type.
//
void BgpConfigManager::ProcessChanges(const ChangeList &change_list) {
    CHECK_CONCURRENCY("bgp::Config");

    for (ChangeList::const_iterator iter = change_list.begin();
         iter != change_list.end(); ++iter) {
        IdentifierMap::iterator loc = id_map_.find(iter->id_type);
        if (loc != id_map_.end()) {
            (loc->second)(*iter);
        }
    }
}

//
// Build and process the change list of BgpConfigDeltas.  The logic to build
// the list is in BgpConfigListener and BgpConfigListener::DependencyTracker.
//
bool BgpConfigManager::ConfigHandler() {
    CHECK_CONCURRENCY("bgp::Config");

    BgpConfigListener::ChangeList change_list;
    listener_->GetChangeList(&change_list);
    ProcessChanges(change_list);
    return true;
}

//
// Terminate the BgpConfigManager.
//
void BgpConfigManager::Terminate() {
    listener_->Terminate();
    config_.reset();
}
