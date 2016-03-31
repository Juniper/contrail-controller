/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config_ifmap.h"

#include <boost/bind.hpp>
#include <boost/foreach.hpp>

#include "base/task.h"
#include "base/task_annotations.h"
#include "bgp/bgp_config_listener.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "control-node/control_node.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_table.h"

#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using namespace std;
using boost::iequals;

const int BgpIfmapConfigManager::kConfigTaskInstanceId = 0;
int BgpIfmapConfigManager::config_task_id_ = -1;

static BgpNeighborConfig::AddressFamilyList default_addr_family_list;

void DefaultAddressFamilyInit() {
    default_addr_family_list.push_back("inet");
    default_addr_family_list.push_back("inet-vpn");
}

MODULE_INITIALIZER(DefaultAddressFamilyInit);

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

static uint32_t IpAddressToBgpIdentifier(const IpAddress &address) {
    return htonl(address.to_v4().to_ulong());
}

static std::string BgpIdentifierToString(uint32_t identifier) {
    Ip4Address addr(ntohl(identifier));
    return addr.to_string();
}

BgpIfmapPeeringConfig::BgpIfmapPeeringConfig(BgpIfmapInstanceConfig *instance)
        : instance_(instance) {
}

BgpIfmapPeeringConfig::~BgpIfmapPeeringConfig() {
    STLDeleteElements(&neighbors_);
}

void BgpIfmapPeeringConfig::SetNodeProxy(IFMapNodeProxy *proxy) {
    if (proxy != NULL) {
        node_proxy_.Swap(proxy);
        name_ = node_proxy_.node()->name();
    }
}

static AuthenticationData::KeyType KeyChainType(const std::string &value) {
    // Case-insensitive comparison
    if (boost::iequals(value, "md5")) {
        return AuthenticationData::MD5;
    }
    return AuthenticationData::NIL;
}

static void BuildKeyChain(BgpNeighborConfig *neighbor,
                          const autogen::AuthenticationData &values) {
    AuthenticationData keydata;
    keydata.set_key_type(KeyChainType(values.key_type));

    AuthenticationKey key;
    for (std::vector<autogen::AuthenticationKeyItem>::const_iterator iter =
            values.key_items.begin(); iter != values.key_items.end(); ++iter) {
        key.id = iter->key_id;
        key.value = iter->key;
        key.start_time = 0;
        keydata.AddKeyToKeyChain(key);
    }
    neighbor->set_keydata(keydata);
}

//
// Set the autogen::BgpSessionAttributes for this BgpNeighborConfig.
//
// The autogen::BgpSession will have up to 3 session attributes - one that
// applies to the local router, one that applies the remote router and one
// that applies to both.
//
static void NeighborSetSessionAttributes(
    BgpNeighborConfig *neighbor, const string &localname,
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
    const autogen::BgpSessionAttributes *attributes = NULL;
    if (common != NULL) {
        attributes = common;
    } else if (local != NULL) {
        attributes = local;
    }
    if (attributes != NULL) {
        neighbor->set_address_families(attributes->address_families.family);
        BuildKeyChain(neighbor, attributes->auth_data);
    }
}

static BgpNeighborConfig *MakeBgpNeighborConfig(
    const BgpIfmapInstanceConfig *instance,
    const string &remote_name,
    const string &local_name,
    const autogen::BgpRouter *local_router,
    const autogen::BgpRouter *remote_router,
    const autogen::BgpSession *session) {
    BgpNeighborConfig *neighbor = new BgpNeighborConfig();

    neighbor->set_instance_name(instance->name());

    // If the autogen::BgpSession has a uuid, we append it to the remote
    // bgp-router's name to make the BgpNeighborConfig's name unique.
    if (session && !session->uuid.empty()) {
        neighbor->set_uuid(session->uuid);
        neighbor->set_name(remote_name + ":" + session->uuid);
    } else {
        neighbor->set_name(remote_name);
    }

    // Store a copy of the remote bgp-router's autogen::BgpRouterParams and
    // derive the autogen::BgpSessionAttributes for the session.
    const autogen::BgpRouterParams &params = remote_router->parameters();
    if (params.local_autonomous_system) {
        neighbor->set_peer_as(params.local_autonomous_system);
    } else {
        neighbor->set_peer_as(params.autonomous_system);
    }
    boost::system::error_code err;
    neighbor->set_peer_address(
        IpAddress::from_string(params.address, err));
    if (err) {
        BGP_LOG_STR(BgpConfig, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                    "Invalid peer address " << params.address <<
                    " for neighbor " << neighbor->name());
    }
    Ip4Address identifier = Ip4Address::from_string(params.identifier, err);
    if (err) {
        BGP_LOG_STR(BgpConfig, SandeshLevel::SYS_WARN, BGP_LOG_FLAG_ALL,
                    "Invalid peer identifier " << params.identifier <<
                    " for neighbor " << neighbor->name());
    }

    neighbor->set_peer_identifier(IpAddressToBgpIdentifier(identifier));

    neighbor->set_port(params.port);
    neighbor->set_hold_time(params.hold_time);

    if (session != NULL) {
        NeighborSetSessionAttributes(neighbor, local_name, session);
    }

    // Get the local identifier and local as from the protocol config.
    const BgpIfmapProtocolConfig *protocol = instance->protocol_config();
    if (protocol && protocol->bgp_router()) {
        const autogen::BgpRouterParams &params = protocol->router_params();
        Ip4Address localid = Ip4Address::from_string(params.identifier, err);
        if (err == 0) {
            neighbor->set_local_identifier(IpAddressToBgpIdentifier(localid));
        }
        if (params.local_autonomous_system) {
            neighbor->set_local_as(params.local_autonomous_system);
        } else {
            neighbor->set_local_as(params.autonomous_system);
        }

        if (neighbor->address_families().empty()) {
            neighbor->set_address_families(params.address_families.family);
        }
        if (neighbor->auth_data().Empty()) {
            const autogen::BgpRouterParams &lp = local_router->parameters();
            BuildKeyChain(neighbor, lp.auth_data);
        }
    }

    if (neighbor->address_families().empty()) {
        neighbor->set_address_families(default_addr_family_list);
    }

    return neighbor;
}

//
// Build map of BgpNeighborConfigs based on the data in autogen::BgpPeering.
//
void BgpIfmapPeeringConfig::BuildNeighbors(BgpConfigManager *manager,
        const autogen::BgpRouter *local_rt_config,
        const string &peername, const autogen::BgpRouter *remote_rt_config,
        const autogen::BgpPeering *peering, NeighborMap *map) {

    // If there are one or more autogen::BgpSessions for the peering, use
    // those to create the BgpNeighborConfigs.
    const autogen::BgpPeeringAttributes &attr = peering->data();
    for (autogen::BgpPeeringAttributes::const_iterator iter = attr.begin();
         iter != attr.end(); ++iter) {
        BgpNeighborConfig *neighbor = MakeBgpNeighborConfig(
            instance_, peername, manager->localname(), local_rt_config,
            remote_rt_config, iter.operator->());
        map->insert(make_pair(neighbor->name(), neighbor));
    }

    // When no sessions are present, create a single BgpNeighborConfig with
    // no per-session configuration.
    if (map->empty()) {
        BgpNeighborConfig *neighbor = MakeBgpNeighborConfig(
            instance_, peername, manager->localname(), local_rt_config,
            remote_rt_config, NULL);
        map->insert(make_pair(neighbor->name(), neighbor));
    }
}

//
// Update BgpPeeringConfig based on updated autogen::BgpPeering.
//
// This mainly involves building future BgpNeighborConfigs and doing a diff of
// the current and future BgpNeighborConfigs.  Note that the BgpIfmapInstanceConfig
// also has references to BgpNeighborConfigs, so it also needs to be updated as
// part of the process.
//
void BgpIfmapPeeringConfig::Update(BgpIfmapConfigManager *manager,
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
        const autogen::BgpRouter *local_rt_config =
                static_cast<const autogen::BgpRouter *>(
                    routers.first->GetObject());
        const autogen::BgpRouter *remote_rt_config =
                static_cast<const autogen::BgpRouter *>(
                    routers.second->GetObject());
        if (local_rt_config && 
            local_rt_config->IsPropertySet(autogen::BgpRouter::PARAMETERS) &&
            remote_rt_config &&
            remote_rt_config->IsPropertySet(autogen::BgpRouter::PARAMETERS)) {
            BuildNeighbors(manager, local_rt_config, routers.second->name(),
                           remote_rt_config, peering, &future);
        }
    }

    // Swap out the NeighborMap in preparation for doing a diff.
    NeighborMap current;
    current.swap(neighbors_);

    // Do a diff on the current and future BgpNeighborConfigs, making sure
    // that the BgpIfmapInstanceConfig is updated accordingly.  We add any new
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
                instance_->ChangeNeighbor(manager, update);
                neighbors_.insert(*it2);
                it2->second = NULL;
            } else {
                neighbors_.insert(*it1);
                it1->second = NULL;
            }
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
void BgpIfmapPeeringConfig::Delete(BgpIfmapConfigManager *manager) {
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
bool BgpIfmapPeeringConfig::GetRouterPair(DBGraph *db_graph,
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
// Constructor for BgpIfmapProtocolConfig.
//
BgpIfmapProtocolConfig::BgpIfmapProtocolConfig(BgpIfmapInstanceConfig *instance)
        : instance_(instance),
          data_(instance->name()) {
}

//
// Destructor for BgpIfmapProtocolConfig.
//
BgpIfmapProtocolConfig::~BgpIfmapProtocolConfig() {
}

const autogen::BgpRouterParams &BgpIfmapProtocolConfig::router_params() const {
    return bgp_router_->parameters();
}

//
// Set the IFMapNodeProxy for the BgpIfmapProtocolConfig.
//
void BgpIfmapProtocolConfig::SetNodeProxy(IFMapNodeProxy *proxy) {
    if (proxy != NULL) {
        node_proxy_.Swap(proxy);
    }
}

//
// Update autogen::BgpRouter object for this BgpIfmapProtocolConfig.
//
void BgpIfmapProtocolConfig::Update(BgpIfmapConfigManager *manager,
                                    const autogen::BgpRouter *router) {
    bgp_router_.reset(router);
    const autogen::BgpRouterParams &params = router->parameters();
    data_.set_autonomous_system(params.autonomous_system);
    data_.set_local_autonomous_system(params.local_autonomous_system);
    boost::system::error_code err;
    IpAddress identifier = IpAddress::from_string(params.identifier, err);
    if (err == 0) {
        data_.set_identifier(IpAddressToBgpIdentifier(identifier));
    }
    data_.set_hold_time(params.hold_time);
}

//
// Delete autogen::BgpRouter object for this BgpIfmapProtocolConfig.
//
void BgpIfmapProtocolConfig::Delete(BgpIfmapConfigManager *manager) {
    manager->Notify(&data_, BgpConfigManager::CFG_DELETE);
    bgp_router_.reset();
}

const string &BgpIfmapProtocolConfig::InstanceName() const {
    return instance_->name();
}

//
// Constructor for BgpIfmapInstanceConfig.
//
BgpIfmapInstanceConfig::BgpIfmapInstanceConfig(const string &name)
    : name_(name),
      data_(name),
      protocol_(NULL) {
}

//
// Destructor for BgpIfmapInstanceConfig.
//
BgpIfmapInstanceConfig::~BgpIfmapInstanceConfig() {
}

//
// Set the IFMapNodeProxy for the BgpIfmapInstanceConfig.
//
void BgpIfmapInstanceConfig::SetNodeProxy(IFMapNodeProxy *proxy) {
    if (proxy != NULL) {
        node_proxy_.Swap(proxy);
    }
}

//
// Get the BgpIfmapProtocolConfig for this BgpIfmapInstanceConfig, create
// it if needed.
//
BgpIfmapProtocolConfig *BgpIfmapInstanceConfig::LocateProtocol() {
    if (protocol_.get() == NULL) {
        protocol_.reset(new BgpIfmapProtocolConfig(this));
    }
    return protocol_.get();
}

//
// Delete the BgpIfmapProtocolConfig for this BgpIfmapInstanceConfig.
//
void BgpIfmapInstanceConfig::ResetProtocol() {
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
            if (!itarget)
                continue;
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
    if (vn && vn->IsPropertySet(autogen::VirtualNetwork::NETWORK_ID))
        return vn->network_id();
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
// Get the vxlan id for a virtual-network.  The input IFMapNode represents
// the virtual-network.
//
// The vxlan_network_identifier is 0 when automatic mode is in use. In that
// case, the network_id is used as vxlan id.
//
static int GetVirtualNetworkVxlanId(DBGraph *graph, IFMapNode *node) {
    const autogen::VirtualNetwork *vn =
        static_cast<autogen::VirtualNetwork *>(node->GetObject());
    if (vn && vn->IsPropertySet(autogen::VirtualNetwork::PROPERTIES)) {
        if (vn->properties().vxlan_network_identifier) {
            return vn->properties().vxlan_network_identifier;
        } else {
            return vn->properties().network_id;
        }
    }
    return 0;
}

//
// Get router external property for a virtual-network.  The input IFMapNode
// represents the virtual-network.
//
static bool GetVirtualNetworkRouterExternal(DBGraph *graph, IFMapNode *node) {
    const autogen::VirtualNetwork *vn =
        static_cast<autogen::VirtualNetwork *>(node->GetObject());
    if (vn && vn->IsPropertySet(autogen::VirtualNetwork::ROUTER_EXTERNAL))
        return vn->router_external();
    return false;
}

static void SetStaticRouteConfig(BgpInstanceConfig *rti,
                                 const autogen::RoutingInstance *config) {
    BgpInstanceConfig::StaticRouteList list;
    BOOST_FOREACH(const autogen::StaticRouteType &route,
                  config->static_route_entries()) {
        StaticRouteConfig item;
        Ip4Address address;
        Ip4PrefixParse(route.prefix, &address, &item.prefix_length);
        item.address = address;
        boost::system::error_code err;
        item.nexthop = IpAddress::from_string(route.next_hop, err);
        item.route_target = route.route_target;
        list.push_back(item);
    }

    rti->swap_static_routes(&list);
}

static void SetServiceChainConfig(BgpInstanceConfig *rti,
                                  const autogen::RoutingInstance *config) {
    const autogen::ServiceChainInfo &chain =
            config->service_chain_information();
    BgpInstanceConfig::ServiceChainList list;
    if (config->IsPropertySet(
        autogen::RoutingInstance::SERVICE_CHAIN_INFORMATION)) {
        ServiceChainConfig item = {
            chain.routing_instance,
            chain.prefix,
            chain.service_chain_address,
            chain.service_instance,
            chain.source_routing_instance
        };
        list.push_back(item);
    }
    rti->swap_service_chain_list(&list);
}

//
// Update BgpIfmapInstanceConfig based on a new autogen::RoutingInstance object.
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
void BgpIfmapInstanceConfig::Update(BgpIfmapConfigManager *manager,
                                    const autogen::RoutingInstance *config) {
    BgpInstanceConfig::RouteTargetList import_list, export_list;
    BgpInstanceConfig::RouteTargetList conn_export_list;
    data_.Clear();

    bool vn_router_external = false;
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
                if (!itarget)
                    continue;
                const autogen::InstanceTargetType &itt = itarget->data();
                if (itt.import_export == "import") {
                    import_list.insert(target);
                } else if (itt.import_export == "export") {
                    export_list.insert(target);
                } else {
                    import_list.insert(target);
                    export_list.insert(target);
                }
            }
        } else if (strcmp(adj->table()->Typename(), "routing-instance") == 0) {
            vector<string> target_list;
            GetRoutingInstanceExportTargets(graph, adj, &target_list);
            conn_export_list.insert(target_list.begin(), target_list.end());
        } else if (strcmp(adj->table()->Typename(), "virtual-network") == 0) {
            data_.set_virtual_network(adj->name());
            data_.set_virtual_network_index(GetVirtualNetworkIndex(graph, adj));
            data_.set_virtual_network_allow_transit(
                GetVirtualNetworkAllowTransit(graph, adj));
            data_.set_vxlan_id(GetVirtualNetworkVxlanId(graph, adj));
            vn_router_external = GetVirtualNetworkRouterExternal(graph, adj);
        }
    }

    // Insert export targets of connected routing instances into import list.
    // Don't do this for non-default routing instances of router-external VNs
    // as a temporary workaround for launchpad bug 1554175.
    if (!ControlNode::GetOptimizeSnat() || !vn_router_external ||
        (config && config->is_default())) {
        import_list.insert(conn_export_list.begin(), conn_export_list.end());
    }

    data_.set_import_list(import_list);
    data_.set_export_list(export_list);

    if (config) {
        SetStaticRouteConfig(&data_, config);
        SetServiceChainConfig(&data_, config);
    }
}

//
// Reset IFMap related state in the BgpIfmapInstanceConfig.
//
void BgpIfmapInstanceConfig::ResetConfig() {
    node_proxy_.Clear();
}

//
// Return true if the BgpIfmapInstanceConfig is ready to be deleted.  The caller is
// responsible for actually deleting it.
//
bool BgpIfmapInstanceConfig::DeleteIfEmpty(BgpConfigManager *manager) {
    if (name_ == BgpConfigManager::kMasterInstance) {
        return false;
    }
    if (node() != NULL || protocol_.get() != NULL) {
        return false;
    }
    if (!neighbors_.empty() || !peerings_.empty()) {
        return false;
    }

    manager->Notify(&data_, BgpConfigManager::CFG_DELETE);
    return true;
}

//
// Add a BgpNeighborConfig to this BgpIfmapInstanceConfig.
//
// The BgpNeighborConfig is added to the NeighborMap and the BgpConfigManager
// is notified.
//
void BgpIfmapInstanceConfig::AddNeighbor(BgpConfigManager *manager,
                                         BgpNeighborConfig *neighbor) {
    BGP_CONFIG_LOG_NEIGHBOR(
        Create, manager->server(), neighbor, SandeshLevel::SYS_DEBUG,
        BGP_LOG_FLAG_ALL,
        BgpIdentifierToString(neighbor->local_identifier()),
        neighbor->local_as(),
        neighbor->peer_address().to_string(), neighbor->peer_as(),
        neighbor->address_families(), neighbor->AuthKeyTypeToString(),
        neighbor->AuthKeysToString());
    neighbors_.insert(make_pair(neighbor->name(), neighbor));
    manager->Notify(neighbor, BgpConfigManager::CFG_ADD);
}

//
// Change a BgpNeighborConfig that's already in this BgpIfmapInstanceConfig.
//
void BgpIfmapInstanceConfig::ChangeNeighbor(BgpConfigManager *manager,
                                            BgpNeighborConfig *neighbor) {
    NeighborMap::iterator loc = neighbors_.find(neighbor->name());
    assert(loc != neighbors_.end());
    loc->second = neighbor;

    BGP_CONFIG_LOG_NEIGHBOR(
        Update, manager->server(), neighbor,
        SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
        BgpIdentifierToString(neighbor->local_identifier()),
        neighbor->local_as(),
        neighbor->peer_address().to_string(), neighbor->peer_as(),
        neighbor->address_families(), neighbor->AuthKeyTypeToString(),
        neighbor->AuthKeysToString());
    manager->Notify(neighbor, BgpConfigManager::CFG_CHANGE);
}

//
// Delete a BgpNeighborConfig from this BgpIfmapInstanceConfig.
//
// The BgpConfigManager is notified and BgpNeighborConfig is removed from the
// NeighborMap. Note that the caller is responsible for actually deleting the
// BgpNeighborConfig object.
//
void BgpIfmapInstanceConfig::DeleteNeighbor(BgpConfigManager *manager,
                                            BgpNeighborConfig *neighbor) {
    BGP_CONFIG_LOG_NEIGHBOR(Delete, manager->server(), neighbor,
        SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
    manager->Notify(neighbor, BgpConfigManager::CFG_DELETE);
    neighbors_.erase(neighbor->name());
}

//
// Find the BgpNeighborConfig by name in this BgpIfmapInstanceConfig.
//
const BgpNeighborConfig *BgpIfmapInstanceConfig::FindNeighbor(
    const string &name) const {
    NeighborMap::const_iterator loc;

    if (name.find(name_) == string::npos) {
        string fqn(name_);
        fqn += ":";
        fqn += name;
        loc = neighbors_.find(fqn);
    } else {
        loc = neighbors_.find(name);
    }

    return loc != neighbors_.end() ? loc->second : NULL;
}

//
// Add a BgpPeeringConfig to this BgpIfmapInstanceConfig.
//
void BgpIfmapInstanceConfig::AddPeering(BgpIfmapPeeringConfig *peering) {
    peerings_.insert(make_pair(peering->name(), peering));
}

//
// Delete a BgpPeeringConfig from this BgpIfmapInstanceConfig.
//
void BgpIfmapInstanceConfig::DeletePeering(BgpIfmapPeeringConfig *peering) {
    peerings_.erase(peering->name());
}

//
// Constructor for BgpIfmapConfigData.
//
BgpIfmapConfigData::BgpIfmapConfigData() {
}

//
// Destructor for BgpConfigData.
//
BgpIfmapConfigData::~BgpIfmapConfigData() {
    STLDeleteElements(&instances_);
    STLDeleteElements(&peerings_);
}

//
// Locate the BgpIfmapInstanceConfig by name, create it if not found.  The newly
// created BgpIfmapInstanceConfig gets added to the IfmapInstanceMap.
//
// Note that we do not have the IFMapNode representing the routing-instance
// at this point.
//
BgpIfmapInstanceConfig *BgpIfmapConfigData::LocateInstance(const string &name) {
    BgpIfmapInstanceConfig *rti = FindInstance(name);
    if (rti != NULL) {
        return rti;
    }
    rti = new BgpIfmapInstanceConfig(name);
    pair<IfmapInstanceMap::iterator, bool> result =
            instances_.insert(make_pair(name, rti));
    assert(result.second);
    pair<BgpInstanceMap::iterator, bool> result2 =
            instance_config_map_.insert(
                make_pair(name, rti->instance_config()));
    assert(result2.second);
    return rti;
}

//
// Remove the given BgpIfmapInstanceConfig from the IfmapInstanceMap
// and delete it.
//
void BgpIfmapConfigData::DeleteInstance(BgpIfmapInstanceConfig *rti) {
    IfmapInstanceMap::iterator loc = instances_.find(rti->name());
    assert(loc != instances_.end());
    instances_.erase(loc);
    BgpInstanceMap::iterator loc2 = instance_config_map_.find(rti->name());
    assert(loc2 != instance_config_map_.end());
    instance_config_map_.erase(loc2);
    delete rti;
}

//
// Find the BgpIfmapInstanceConfig by name.
//
BgpIfmapInstanceConfig *BgpIfmapConfigData::FindInstance(const string &name) {
    IfmapInstanceMap::iterator loc = instances_.find(name);
    if (loc != instances_.end()) {
        return loc->second;
    }
    return NULL;
}

//
// Find the BgpIfmapInstanceConfig by name.
// Const version.
//
const BgpIfmapInstanceConfig *BgpIfmapConfigData::FindInstance(
    const string &name) const {
    IfmapInstanceMap::const_iterator loc = instances_.find(name);
    if (loc != instances_.end()) {
        return loc->second;
    }
    return NULL;
}

BgpConfigManager::NeighborMapRange
BgpIfmapInstanceConfig::NeighborMapItems() const {
    return make_pair(neighbors_.begin(), neighbors_.end());
}

//
// Create a new BgpIfmapPeeringConfig.
//
// The IFMapNodeProxy is a proxy for the IFMapNode which is the
// midnode that represents the bgp-peering. The newly created
// BgpIfmapPeeringConfig gets added to the IfmapPeeringMap.
//
BgpIfmapPeeringConfig *BgpIfmapConfigData::CreatePeering(
    BgpIfmapInstanceConfig *rti, IFMapNodeProxy *proxy) {
    BgpIfmapPeeringConfig *peering = new BgpIfmapPeeringConfig(rti);
    peering->SetNodeProxy(proxy);
    pair<IfmapPeeringMap::iterator, bool> result =
            peerings_.insert(make_pair(peering->node()->name(), peering));
    assert(result.second);
    peering->instance()->AddPeering(peering);
    return peering;
}

//
// Delete a BgpPeeringConfig.
//
// The BgpPeeringConfig is removed from the IfmapPeeringMap and then deleted.
// Note that the reference to the IFMapNode for the bgp-peering gets released
// via the destructor when the IFMapNodeProxy is destroyed.
//
void BgpIfmapConfigData::DeletePeering(BgpIfmapPeeringConfig *peering) {
    peering->instance()->DeletePeering(peering);
    peerings_.erase(peering->node()->name());
    delete peering;
}

//
// Find the BgpPeeringConfig by name.
//
BgpIfmapPeeringConfig *BgpIfmapConfigData::FindPeering(const string &name) {
    IfmapPeeringMap::iterator loc = peerings_.find(name);
    if (loc != peerings_.end()) {
        return loc->second;
    }
    return NULL;
}

//
// Find the BgpPeeringConfig by name.
// Const version.
//
const BgpIfmapPeeringConfig *BgpIfmapConfigData::FindPeering(
    const string &name) const {
    IfmapPeeringMap::const_iterator loc = peerings_.find(name);
    if (loc != peerings_.end()) {
        return loc->second;
    }
    return NULL;
}

BgpConfigManager::InstanceMapRange
BgpIfmapConfigData::InstanceMapItems(const string &start_name) const {
    return make_pair(instance_config_map_.lower_bound(start_name),
        instance_config_map_.end());
}

//
// Constructor for BgpIfmapConfigManager.
//
BgpIfmapConfigManager::BgpIfmapConfigManager(BgpServer *server)
        : BgpConfigManager(server),
          db_(NULL), db_graph_(NULL),
          trigger_(boost::bind(&BgpIfmapConfigManager::ConfigHandler, this),
                   TaskScheduler::GetInstance()->GetTaskId("bgp::Config"),
                   kConfigTaskInstanceId),
          listener_(new BgpConfigListener(this)),
          config_(new BgpIfmapConfigData()) {
    IdentifierMapInit();

    if (config_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        config_task_id_ = scheduler->GetTaskId("bgp::Config");
    }
}

//
// Destructor for BgpIfmapConfigManager.
//
BgpIfmapConfigManager::~BgpIfmapConfigManager() {
}

//
// Initialize the BgpConfigManager.
//
void BgpIfmapConfigManager::Initialize(DB *db, DBGraph *db_graph,
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
void BgpIfmapConfigManager::OnChange() {
    CHECK_CONCURRENCY("db::DBTable");
    trigger_.Set();
}

BgpConfigManager::InstanceMapRange
BgpIfmapConfigManager::InstanceMapItems(const string &start_name) const {
    return config_->InstanceMapItems(start_name);
}

BgpConfigManager::NeighborMapRange
BgpIfmapConfigManager::NeighborMapItems(
    const std::string &instance_name) const {
    static BgpConfigManager::NeighborMap nilMap;
    BgpIfmapInstanceConfig *rti = config_->FindInstance(instance_name);
    if (rti == NULL) {
        return make_pair(nilMap.begin(), nilMap.end());
    }
    return rti->NeighborMapItems();
}

int BgpIfmapConfigManager::NeighborCount(
    const std::string &instance_name) const {
    BgpIfmapInstanceConfig *rti = config_->FindInstance(instance_name);
    if (rti == NULL) {
        return 0;
    }
    return rti->neighbors().size();
}

const BgpInstanceConfig *BgpIfmapConfigManager::FindInstance(
    const std::string &name) const {
    BgpIfmapInstanceConfig *rti = config_->FindInstance(name);
    if (rti == NULL) {
        return NULL;
    }
    return rti->instance_config();
}

const BgpProtocolConfig *BgpIfmapConfigManager::GetProtocolConfig(
    const std::string &instance_name) const {
    BgpIfmapInstanceConfig *rti = config_->FindInstance(instance_name);
    if (rti == NULL) {
        return NULL;
    }
    const BgpIfmapProtocolConfig *proto = rti->protocol_config();
    if (proto == NULL) {
        return NULL;
    }
    return proto->protocol_config();
}

const BgpNeighborConfig *BgpIfmapConfigManager::FindNeighbor(
    const std::string &instance_name, const std::string &name) const {
    BgpIfmapInstanceConfig *rti = config_->FindInstance(instance_name);
    if (rti == NULL) {
        return NULL;
    }
    return rti->FindNeighbor(name);
}

//
// Initialize autogen::BgpRouterParams with default values.
//
void BgpIfmapConfigManager::DefaultBgpRouterParams(
    autogen::BgpRouterParams &param) {
    param.Clear();
    param.autonomous_system = BgpConfigManager::kDefaultAutonomousSystem;
    param.port = BgpConfigManager::kDefaultPort;
}

//
// Create BgpInsatnceConfig for master routing-instance.  This
// includes the BgpIfmapProtocolConfig for the local bgp-router in the
// master routing-instance.
//
void BgpIfmapConfigManager::DefaultConfig() {
    BgpIfmapInstanceConfig *rti = config_->LocateInstance(kMasterInstance);
    auto_ptr<autogen::BgpRouter> router(new autogen::BgpRouter());
    autogen::BgpRouterParams param;
    DefaultBgpRouterParams(param);
    router->SetProperty("bgp-router-parameters", &param);
    BgpIfmapProtocolConfig *protocol = rti->LocateProtocol();
    protocol->Update(this, router.release());
    Notify(rti->instance_config(), BgpConfigManager::CFG_ADD);

    vector<string> import_list;
    vector<string> export_list;
    BGP_CONFIG_LOG_INSTANCE(
        Create, server(), rti, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
        import_list, export_list,
        rti->virtual_network(), rti->virtual_network_index());

    BGP_CONFIG_LOG_PROTOCOL(
        Create, server(), protocol,
        SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
        protocol->router_params().autonomous_system,
        protocol->router_params().identifier,
        protocol->router_params().address,
        protocol->router_params().hold_time,
        vector<string>());
}

//
// Initialize IdentifierMap with handlers for interesting identifier types.
//
// The IdentifierMap is used when processing BgpConfigDeltas generated by
// the BgpConfigListener.
//
void BgpIfmapConfigManager::IdentifierMapInit() {
    id_map_.insert(make_pair("routing-instance",
            boost::bind(&BgpIfmapConfigManager::ProcessRoutingInstance,
                        this, _1)));
    id_map_.insert(make_pair("bgp-router",
            boost::bind(&BgpIfmapConfigManager::ProcessBgpRouter, this, _1)));
    id_map_.insert(make_pair("bgp-peering",
            boost::bind(&BgpIfmapConfigManager::ProcessBgpPeering, this, _1)));
}

//
// Handler for routing-instance objects.
//
// Note that the BgpIfmapInstanceConfig object for the master instance is created
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
// References to the IFMapNode and the autogen::RoutingInstance are
// released when the IFMapNode is marked deleted.  However the
// BgpIfmapInstanceConfig does not get deleted till the NeighborMap is
// empty and the BgpIfmapProtocolConfig is gone.
//
void BgpIfmapConfigManager::ProcessRoutingInstance(
    const BgpConfigDelta &delta) {
    CHECK_CONCURRENCY("bgp::Config");

    BgpConfigManager::EventType event = BgpConfigManager::CFG_CHANGE;
    string instance_name = delta.id_name;
    BgpIfmapInstanceConfig *rti = config_->FindInstance(instance_name);
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
                BGP_CONFIG_LOG_INSTANCE(
                    Delete, server(), rti,
                    SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
                config_->DeleteInstance(rti);
            }
            return;
        }
    }

    autogen::RoutingInstance *rti_config =
        static_cast<autogen::RoutingInstance *>(delta.obj.get());
    rti->Update(this, rti_config);
    Notify(rti->instance_config(), event);

    vector<string> import_rt(rti->import_list().begin(),
                             rti->import_list().end());
    vector<string> export_rt(rti->export_list().begin(),
                             rti->export_list().end());
    if (event == BgpConfigManager::CFG_ADD) {
        BGP_CONFIG_LOG_INSTANCE(Create, server(), rti,
            SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            import_rt, export_rt,
            rti->virtual_network(), rti->virtual_network_index());
    } else {
        BGP_CONFIG_LOG_INSTANCE(Update, server(), rti,
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
void BgpIfmapConfigManager::ProcessBgpProtocol(const BgpConfigDelta &delta) {
    CHECK_CONCURRENCY("bgp::Config");

    BgpConfigManager::EventType event = BgpConfigManager::CFG_CHANGE;
    string instance_name(IdentifierParent(delta.id_name));
    BgpIfmapInstanceConfig *rti = config_->FindInstance(instance_name);
    BgpIfmapProtocolConfig *protocol = NULL;
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
            Notify(rti->instance_config(), BgpConfigManager::CFG_ADD);

            vector<string> import_rt(rti->import_list().begin(),
                                     rti->import_list().end());
            vector<string> export_rt(rti->export_list().begin(),
                                     rti->export_list().end());
            BGP_CONFIG_LOG_INSTANCE(Create, server(), rti,
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
        } else if (delta.obj.get() == NULL) {
            BGP_CONFIG_LOG_PROTOCOL(Delete, server(), protocol,
                SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
            protocol->Delete(this);
            rti->ResetProtocol();
            if (rti->DeleteIfEmpty(this)) {
                BGP_CONFIG_LOG_INSTANCE(Delete, server(), rti,
                    SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
                config_->DeleteInstance(rti);
            }
            return;
        }
    }

    autogen::BgpRouter *rt_config =
        static_cast<autogen::BgpRouter *>(delta.obj.get());
    protocol->Update(this, rt_config);
    Notify(protocol->protocol_config(), event);

    if (!rt_config) {
        return;
    }

    vector<string> families(
        protocol->router_params().address_families.begin(),
        protocol->router_params().address_families.end());
    if (event == BgpConfigManager::CFG_ADD) {
        BGP_CONFIG_LOG_PROTOCOL(Create, server(), protocol,
            SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            protocol->router_params().autonomous_system,
            protocol->router_params().identifier,
            protocol->router_params().address,
            protocol->router_params().hold_time,
            families);
    } else {
        BGP_CONFIG_LOG_PROTOCOL(Update, server(), protocol,
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
void BgpIfmapConfigManager::ProcessBgpRouter(const BgpConfigDelta &delta) {
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
// Note that we are guaranteed that the IFMapNodes for the 2
// bgp-routers for the bgp-peering already exist when we see the
// bgp-peering. IFMap creates the nodes for the bgp-routers before
// creating the midnode. This in turn guarantees that the
// BgpIfmapInstanceConfig for the routing-instance also exists since we
// create the BgpIfmapInstanceConfig before creating the
// BgpIfmapProtocolConfig for a local bgp-router.
//
void BgpIfmapConfigManager::ProcessBgpPeering(const BgpConfigDelta &delta) {
    CHECK_CONCURRENCY("bgp::Config");

    BgpConfigManager::EventType event = BgpConfigManager::CFG_CHANGE;
    BgpIfmapPeeringConfig *peering = config_->FindPeering(delta.id_name);
    if (peering == NULL) {
        IFMapNodeProxy *proxy = delta.node.get();
        if (proxy == NULL) {
            return;
        }
        IFMapNode *node = proxy->node();
        if (node == NULL || delta.obj.get() == NULL) {
            return;
        }

        pair<IFMapNode *, IFMapNode *> routers;
        if (!BgpIfmapPeeringConfig::GetRouterPair(db_graph_, localname_, node,
                                                  &routers)) {
            return;
        }

        event = BgpConfigManager::CFG_ADD;
        string instance_name(IdentifierParent(routers.first->name()));
        BgpIfmapInstanceConfig *rti = config_->FindInstance(instance_name);
        if (rti == NULL) {
            return;
        }
        peering = config_->CreatePeering(rti, proxy);
    } else {
        const IFMapNode *node = peering->node();
        assert(node != NULL);
        if (delta.obj.get() == NULL) {
            BGP_CONFIG_LOG_PEERING(Delete, server(), peering,
                SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
            BgpIfmapInstanceConfig *rti = peering->instance();
            peering->Delete(this);
            config_->DeletePeering(peering);
            if (rti->DeleteIfEmpty(this)) {
                BGP_CONFIG_LOG_INSTANCE(Delete, server(), rti,
                    SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
                config_->DeleteInstance(rti);
            }
            return;
        }
    }

    if (event == BgpConfigManager::CFG_ADD) {
        BGP_CONFIG_LOG_PEERING(Create, server(), peering,
            SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
    } else {
        BGP_CONFIG_LOG_PEERING(Update, server(), peering,
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
void BgpIfmapConfigManager::ProcessChanges(const ChangeList &change_list) {
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
bool BgpIfmapConfigManager::ConfigHandler() {
    CHECK_CONCURRENCY("bgp::Config");

    BgpConfigListener::ChangeList change_list;
    listener_->GetChangeList(&change_list);
    ProcessChanges(change_list);
    return true;
}

//
// Terminate the BgpConfigManager.
//
void BgpIfmapConfigManager::Terminate() {
    listener_->Terminate();
    config_.reset();
}
