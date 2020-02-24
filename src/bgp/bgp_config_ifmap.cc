/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_config_ifmap.h"

#include <boost/foreach.hpp>

#include <algorithm>

#include "base/string_util.h"
#include "base/task_annotations.h"
#include "bgp/bgp_common.h"
#include "bgp/bgp_config_listener.h"
#include "bgp/bgp_log.h"
#include "bgp/routing-instance/iservice_chain_mgr.h"
#include "ifmap/ifmap_node.h"
#include "ifmap/ifmap_table.h"

#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"

using std::auto_ptr;
using std::find;
using std::make_pair;
using std::pair;
using std::set;
using std::sort;
using std::string;
using std::vector;
using boost::iequals;

const int BgpIfmapConfigManager::kConfigTaskInstanceId = 0;

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

static string BgpIdentifierToString(uint32_t identifier) {
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

BgpIfmapRoutingPolicyLinkConfig::BgpIfmapRoutingPolicyLinkConfig(
    BgpIfmapInstanceConfig *rti, BgpIfmapRoutingPolicyConfig *rtp) :
     instance_(rti), policy_(rtp) {
}

BgpIfmapRoutingPolicyLinkConfig::~BgpIfmapRoutingPolicyLinkConfig() {
}

void BgpIfmapRoutingPolicyLinkConfig::SetNodeProxy(IFMapNodeProxy *proxy) {
    if (proxy != NULL) {
        node_proxy_.Swap(proxy);
        name_ = node_proxy_.node()->name();
    }
}

bool BgpIfmapRoutingPolicyLinkConfig::GetInstancePolicyPair(
    DBGraph *graph, IFMapNode *node, pair<IFMapNode *, IFMapNode *> *pair) {
    IFMapNode *routing_instance = NULL;
    IFMapNode *routing_policy = NULL;

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (strcmp(adj->table()->Typename(), "routing-instance") == 0)
            routing_instance = adj;
        if (strcmp(adj->table()->Typename(), "routing-policy") == 0)
            routing_policy = adj;
    }
    if (routing_policy == NULL || routing_instance == NULL) {
        return false;
    }

    pair->first = routing_instance;
    pair->second = routing_policy;
    return true;
}

void BgpIfmapRoutingPolicyLinkConfig::Update(BgpIfmapConfigManager *manager,
    const autogen::RoutingPolicyRoutingInstance *ri_rp) {
    ri_rp_link_.reset(ri_rp);
}

void BgpIfmapRoutingPolicyLinkConfig::Delete(BgpIfmapConfigManager *manager) {
    ri_rp_link_.reset();
}

static AuthenticationData::KeyType KeyChainType(const string &value) {
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
    for (vector<autogen::AuthenticationKeyItem>::const_iterator iter =
            values.key_items.begin(); iter != values.key_items.end(); ++iter) {
        key.id = iter->key_id;
        key.value = iter->key;
        key.start_time = 0;
        keydata.AddKeyToKeyChain(key);
    }
    neighbor->set_keydata(keydata);
}

//
// Check if the family is allowed to be configured for BgpNeighborConfig.
// Only families inet and inet6 are allowed on non-master instances.
//
static bool AddressFamilyIsValid(BgpNeighborConfig *neighbor,
    const string &family) {
    if (neighbor->instance_name() == BgpConfigManager::kMasterInstance)
        return true;
    return (family == "inet" || family == "inet6");
}

//
// Build list of BgpFamilyAttributesConfig elements from the list of address
// families. This is provided for backward compatibility with configurations
// that represent each family with a simple string.
//
static void BuildFamilyAttributesList(BgpNeighborConfig *neighbor,
    const BgpNeighborConfig::AddressFamilyList &family_list,
    const vector<string> &remote_family_list) {
    BgpNeighborConfig::FamilyAttributesList family_attributes_list;
    BOOST_FOREACH(const string &family, family_list) {
        // Skip families that are not valid/supported for the neighbor.
        if (!AddressFamilyIsValid(neighbor, family))
            continue;

        // Skip families that are not configured on remote bgp-router.
        if (!remote_family_list.empty()) {
            vector<string>::const_iterator it = find(
                remote_family_list.begin(), remote_family_list.end(), family);
            if (it == remote_family_list.end())
                continue;
        }

        BgpFamilyAttributesConfig family_attributes(family);
        family_attributes_list.push_back(family_attributes);
    }

    neighbor->set_family_attributes_list(family_attributes_list);
}

//
// Build list of BgpFamilyAttributesConfig elements from BgpFamilyAttributes
// list in BgpSessionAttributes.
//
// Implement backward compatibility by also adding BgpFamilyAttributesConfig
// elements for families that are not in BgpFamilyAttributes list but are in
// the address_families list.
//
static void BuildFamilyAttributesList(BgpNeighborConfig *neighbor,
    const autogen::BgpSessionAttributes *attributes) {
    set<string> family_set;
    BgpNeighborConfig::FamilyAttributesList family_attributes_list;
    BOOST_FOREACH(const autogen::BgpFamilyAttributes &family_config,
        attributes->family_attributes) {
        if (!AddressFamilyIsValid(neighbor, family_config.address_family))
            continue;
        BgpFamilyAttributesConfig family_attributes(
            family_config.address_family);
        family_attributes.loop_count = family_config.loop_count;
        family_attributes.prefix_limit = family_config.prefix_limit.maximum;
        family_attributes.idle_timeout =
            family_config.prefix_limit.idle_timeout;
        family_attributes.default_tunnel_encap_list =
            family_config.default_tunnel_encap;
        family_attributes_list.push_back(family_attributes);
        family_set.insert(family_config.address_family);
    }

    BOOST_FOREACH(const string &family, attributes->address_families.family) {
        if (family_set.find(family) != family_set.end())
            continue;
        BgpFamilyAttributesConfig family_attributes(family);
        family_attributes_list.push_back(family_attributes);
    }

    neighbor->set_family_attributes_list(family_attributes_list);
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
        } else if (neighbor->router_type() != "bgpaas-client" &&
            attr->bgp_router == localname) {
            local = attr;
        } else if (neighbor->router_type() == "bgpaas-client" &&
            attr->bgp_router == "bgpaas-server") {
            local = attr;
        }
    }

    // TODO(nsheth): local should override rather than replace common.
    const autogen::BgpSessionAttributes *attributes = NULL;
    if (common != NULL) {
        attributes = common;
    } else if (local != NULL) {
        attributes = local;
    }
    if (attributes != NULL) {
        neighbor->set_passive(attributes->passive);
        neighbor->set_as_override(attributes->as_override);
        neighbor->set_private_as_action(attributes->private_as_action);
        neighbor->set_loop_count(attributes->loop_count);
        if (attributes->admin_down) {
            neighbor->set_admin_down(true);
        }
        if (attributes->local_autonomous_system) {
            neighbor->set_local_as(attributes->local_autonomous_system);
        }
        if (attributes->hold_time) {
            neighbor->set_hold_time(attributes->hold_time);
        }
        BuildFamilyAttributesList(neighbor, attributes);
        BuildKeyChain(neighbor, attributes->auth_data);
        const autogen::RouteOriginOverride &origin_override =
                attributes->route_origin_override;
        neighbor->SetOriginOverride(origin_override.origin_override,
                                    origin_override.origin);
    }
}

static BgpNeighborConfig *MakeBgpNeighborConfig(
    const BgpIfmapInstanceConfig *instance,
    const BgpIfmapInstanceConfig *master_instance,
    const string &local_name,
    const string &remote_name,
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
    neighbor->set_router_type(params.router_type);
    if (params.admin_down) {
        neighbor->set_admin_down(true);
    }
    if (params.local_autonomous_system) {
        neighbor->set_peer_as(params.local_autonomous_system);
    } else {
        neighbor->set_peer_as(params.autonomous_system);
    }
    boost::system::error_code err;
    neighbor->set_peer_address(
        IpAddress::from_string(params.address, err));
    if (err) {
        BGP_LOG_WARNING_STR(BgpConfig, BGP_LOG_FLAG_ALL,
                            "Invalid peer address " << params.address <<
                            " for neighbor " << neighbor->name());
    }

    Ip4Address identifier = Ip4Address::from_string(params.identifier, err);
    if (err) {
        BGP_LOG_WARNING_STR(BgpConfig, BGP_LOG_FLAG_ALL,
                            "Invalid peer identifier " << params.identifier <<
                            " for neighbor " << neighbor->name());
    }
    neighbor->set_peer_identifier(IpAddressToBgpIdentifier(identifier));

    if (params.router_type == "bgpaas-client") {
        IpAddress inet_gw_address =
            Ip4Address::from_string(params.gateway_address, err);
        if (!params.gateway_address.empty() && err) {
            BGP_LOG_WARNING_STR(BgpConfig, BGP_LOG_FLAG_ALL,
                                "Invalid gateway address " <<
                                params.gateway_address <<
                                " for neighbor " << neighbor->name());
        } else {
            neighbor->set_gateway_address(Address::INET, inet_gw_address);
        }

        IpAddress inet6_gw_address =
            Ip6Address::from_string(params.ipv6_gateway_address, err);
        if (!params.ipv6_gateway_address.empty() && err) {
            BGP_LOG_WARNING_STR(BgpConfig, BGP_LOG_FLAG_ALL,
                "Invalid ipv6 gateway address " <<
                params.ipv6_gateway_address <<
                " for neighbor " << neighbor->name());
        } else {
            neighbor->set_gateway_address(Address::INET6, inet6_gw_address);
        }
    }

    neighbor->set_port(params.port);
    neighbor->set_source_port(params.source_port);
    neighbor->set_hold_time(params.hold_time);

    if (session != NULL) {
        NeighborSetSessionAttributes(neighbor, local_name, session);
    }

    // Get the local identifier and local as from the master protocol config.
    const BgpIfmapProtocolConfig *master_protocol =
        master_instance->protocol_config();
    if (master_protocol && master_protocol->bgp_router()) {
        const autogen::BgpRouterParams &master_params =
            master_protocol->router_params();
        if (master_params.admin_down) {
            neighbor->set_admin_down(true);
        }
        Ip4Address localid =
            Ip4Address::from_string(master_params.identifier, err);
        if (err == 0) {
            neighbor->set_local_identifier(IpAddressToBgpIdentifier(localid));
        }
        if (!neighbor->local_as()) {
            if (master_params.local_autonomous_system) {
                neighbor->set_local_as(master_params.local_autonomous_system);
            } else {
                neighbor->set_local_as(master_params.autonomous_system);
            }
        }
        if (instance != master_instance) {
            neighbor->set_passive(true);
        }
    }

    // Get other parameters from the instance protocol config.
    // Note that there's no instance protocol config for non-master instances.
    const BgpIfmapProtocolConfig *protocol = instance->protocol_config();
    if (protocol && protocol->bgp_router()) {
        const autogen::BgpRouterParams &protocol_params =
            protocol->router_params();
        if (neighbor->family_attributes_list().empty()) {
            BuildFamilyAttributesList(neighbor,
                protocol_params.address_families.family,
                params.address_families.family);
        }
        if (neighbor->auth_data().Empty()) {
            const autogen::BgpRouterParams &local_params =
                local_router->parameters();
            BuildKeyChain(neighbor, local_params.auth_data);
        }
    }

    if (neighbor->family_attributes_list().empty()) {
        BuildFamilyAttributesList(neighbor, default_addr_family_list,
            params.address_families.family);
    }

    return neighbor;
}

//
// Build map of BgpNeighborConfigs based on the data in autogen::BgpPeering.
//
void BgpIfmapPeeringConfig::BuildNeighbors(BgpIfmapConfigManager *manager,
    const autogen::BgpRouter *local_rt_config,
    const string &peername, const autogen::BgpRouter *remote_rt_config,
    const autogen::BgpPeering *peering, NeighborMap *map) {
    const BgpIfmapInstanceConfig *master_instance =
        manager->config()->FindInstance(BgpConfigManager::kMasterInstance);

    // If there are one or more autogen::BgpSessions for the peering, use
    // those to create the BgpNeighborConfigs.
    const autogen::BgpPeeringAttributes &attr = peering->data();
    for (autogen::BgpPeeringAttributes::const_iterator iter = attr.begin();
         iter != attr.end(); ++iter) {
        BgpNeighborConfig *neighbor = MakeBgpNeighborConfig(
            instance_, master_instance, manager->localname(), peername,
            local_rt_config, remote_rt_config, iter.operator->());
        map->insert(make_pair(neighbor->name(), neighbor));
    }

    // When no sessions are present, create a single BgpNeighborConfig with
    // no per-session configuration.
    if (map->empty()) {
        BgpNeighborConfig *neighbor = MakeBgpNeighborConfig(
            instance_, master_instance, manager->localname(), peername,
            local_rt_config, remote_rt_config, NULL);
        map->insert(make_pair(neighbor->name(), neighbor));
    }
}

//
// Update BgpIfmapPeeringConfig based on updated autogen::BgpPeering.
//
// This mainly involves building future BgpNeighborConfigs and doing a diff of
// the current and future BgpNeighborConfigs.  Note that BgpIfmapInstanceConfig
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
    string local_router_type;
    string remote_router_type;
    string local_instance;
    string remote_instance;

    for (DBGraphVertex::adjacency_iterator iter = node->begin(db_graph);
         iter != node->end(db_graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (strcmp(adj->table()->Typename(), "bgp-router") != 0)
            continue;
        autogen::BgpRouter *router =
            static_cast<autogen::BgpRouter *>(adj->GetObject());
        if (!router)
            continue;
        const autogen::BgpRouterParams &params = router->parameters();
        string instance_name(IdentifierParent(adj->name()));
        string name = adj->name().substr(instance_name.size() + 1);
        if (name == localname && params.router_type != "bgpaas-client") {
            local = adj;
            local_router_type = params.router_type;
            local_instance = instance_name;
        } else if (instance_name != BgpConfigManager::kMasterInstance &&
            params.router_type == "bgpaas-server") {
            local = adj;
            local_router_type = params.router_type;
            local_instance = instance_name;
        } else {
            remote = adj;
            remote_router_type = params.router_type;
            remote_instance = instance_name;
        }
    }

    if ((local == NULL || remote == NULL) || (local_router_type ==
         "bgpaas-server" && remote_router_type != "bgpaas-client") ||
        (local_router_type != "bgpaas-server" && remote_router_type ==
         "bgpaas-client") || (local_instance != remote_instance)) {
        BGP_LOG_STR(BgpConfig, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_SYSLOG,
                 "localname: " << localname <<
                 ((local == NULL) ? " Local node not present" :
                                  " Local node present") <<
                 ((remote == NULL) ? " Remote node not present" :
                                  " Remote node present") <<
                 " local_router_type: " << local_router_type <<
                 " remote_router_type: " << remote_router_type <<
                 " local instance: " << local_instance <<
                 " remote instance: " << remote_instance);
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
    data_.set_admin_down(params.admin_down);
    data_.set_cluster_id(params.cluster_id);
    data_.set_autonomous_system(params.autonomous_system);
    data_.set_local_autonomous_system(params.local_autonomous_system);
    data_.set_port(params.port ?: BgpConfigManager::kDefaultPort);
    boost::system::error_code err;
    IpAddress identifier = IpAddress::from_string(params.identifier, err);
    if (err == 0) {
        data_.set_identifier(IpAddressToBgpIdentifier(identifier));
    }
    data_.set_hold_time(params.hold_time);

    // reset subcluster name and id,
    // will be set again if mapping is found in schema
    data_.reset_subcluster_name();
    data_.reset_subcluster_id();
    // get subcluster name this router is associated with
    IFMapNode *node = node_proxy_.node();
    if (!node)
        return;
    DBGraph *graph = manager->graph();
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (strcmp(adj->table()->Typename(), "sub-cluster") == 0) {
            data_.set_subcluster_name(adj->name());
            const autogen::SubCluster *sc =
                static_cast<autogen::SubCluster *>(adj->GetObject());
            if (sc) {
                data_.set_subcluster_id(sc->id());
            }
            break;
        }
    }
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
      index_(-1),
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
// and look for instance-target adjacencies. If the instance-target is an
// export and import target, add it to the vector.
//
// Note that we purposely skip adding export only targets to the vector.
// Reasoning is that export only targets are manually configured by users
// and hence should not be imported based on policy.
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
            if (itt.import_export.empty())
                target_list->push_back(target);
        }
    }
}

//
// Fill in all the export route targets of the routing-instance at the other
// end of a connection.  The src_node is the routing-instance from which we
// reached the connection node. The name of the source routing-instance is
// src_instance.
//
// If the connection is unidirectional and the destination-instance for the
// connection is not the routing-instance from which we started, we should
// not get any targets from this connection.  This is what a unidirectional
// connection means.
//
static void GetConnectionExportTargets(DBGraph *graph, IFMapNode *src_node,
    const string &src_instance, IFMapNode *node,
    vector<string> *target_list) {
    const autogen::Connection *conn =
        dynamic_cast<autogen::Connection *>(node->GetObject());
    if (!conn)
        return;
    const autogen::ConnectionType &conn_type = conn->data();
    if (!conn_type.destination_instance.empty() &&
         conn_type.destination_instance != src_instance)
        return;
    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (adj == src_node)
            continue;
        if (strcmp(adj->table()->Typename(), "routing-instance") != 0)
            continue;
        GetRoutingInstanceExportTargets(graph, adj, target_list);
    }
}

//
// Fill in all the routing-policies for a routing-instance.  The input
// IFMapNode represents the routing-policy-routing-instance.  We traverse to
// graph edges and look for routing-policy adjacency
//
static bool GetRoutingInstanceRoutingPolicy(DBGraph *graph, IFMapNode *node,
    RoutingPolicyAttachInfo *ri_rp_link) {
    string sequence;
    const autogen::RoutingPolicyRoutingInstance *policy =
        static_cast<autogen::RoutingPolicyRoutingInstance *>(node->GetObject());
    const autogen::RoutingPolicyType &attach_info = policy->data();

    ri_rp_link->sequence_ = attach_info.sequence;

    for (DBGraphVertex::adjacency_iterator iter = node->begin(graph);
         iter != node->end(graph); ++iter) {
        IFMapNode *adj = static_cast<IFMapNode *>(iter.operator->());
        if (strcmp(adj->table()->Typename(), "routing-policy") == 0) {
            ri_rp_link->routing_policy_ = adj->name();
            return true;
        }
    }
    return false;
}

//
// Fill in all the route-aggregation for a routing-instance.
//
static bool GetRouteAggregateConfig(DBGraph *graph, IFMapNode *node,
    BgpInstanceConfig::AggregateRouteList *inet_list,
    BgpInstanceConfig::AggregateRouteList *inet6_list) {
    const autogen::RouteAggregate *ra =
        static_cast<autogen::RouteAggregate *>(node->GetObject());
    if (ra == NULL) return false;

    boost::system::error_code ec;
    IpAddress nexthop =
        IpAddress::from_string(ra->aggregate_route_nexthop(), ec);
    if (ec != 0) return false;

    BOOST_FOREACH(const string &route, ra->aggregate_route_entries()) {
        AggregateRouteConfig aggregate;
        aggregate.nexthop = nexthop;

        Ip4Address address;
        ec = Ip4SubnetParse(route, &address, &aggregate.prefix_length);
        if (ec == 0) {
            if (!nexthop.is_v4()) continue;
            aggregate.aggregate = address;
            inet_list->push_back(aggregate);
        } else {
            if (!nexthop.is_v6()) continue;
            Ip6Address address;
            ec = Inet6SubnetParse(route, &address, &aggregate.prefix_length);
            if (ec != 0) continue;
            aggregate.aggregate = address;
            inet6_list->push_back(aggregate);
        }
    }

    return true;
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
// Check if a virtual-network has pbb-evpn enabled.
// The input IFMapNode represents the virtual-network.
//
static bool GetVirtualNetworkPbbEvpnEnable(DBGraph *graph, IFMapNode *node) {
    const autogen::VirtualNetwork *vn =
        static_cast<autogen::VirtualNetwork *>(node->GetObject());
    if (vn && vn->IsPropertySet(autogen::VirtualNetwork::PBB_EVPN_ENABLE))
        return vn->pbb_evpn_enable();
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

static void SetStaticRouteConfig(BgpInstanceConfig *rti,
    const autogen::RoutingInstance *config) {
    BgpInstanceConfig::StaticRouteList inet_list;
    BgpInstanceConfig::StaticRouteList inet6_list;
    BOOST_FOREACH(const autogen::StaticRouteType &route,
                  config->static_route_entries()) {
        boost::system::error_code ec;
        StaticRouteConfig item;
        item.nexthop = IpAddress::from_string(route.next_hop, ec);
        if (ec != 0)
            continue;

        item.route_targets = route.route_target;
        item.communities = route.community;
        if (item.nexthop.is_v4()) {
            Ip4Address address;
            ec = Ip4SubnetParse(route.prefix, &address, &item.prefix_length);
            if (ec != 0)
                continue;
            item.address = address;
            std::pair<BgpInstanceConfig::StaticRouteList::iterator, bool> ret =
                inet_list.insert(item);
            if (!ret.second) {
                BGP_LOG_WARNING_STR(BgpConfig, BGP_LOG_FLAG_ALL,
                    "Duplicate static route prefix " << route.prefix <<
                    " with nexthop " << route.next_hop <<
                    " for routing instance " << rti->name());
            }
        } else {
            Ip6Address address;
            ec = Inet6SubnetParse(route.prefix, &address, &item.prefix_length);
            if (ec != 0)
                continue;
            item.address = address;
            std::pair<BgpInstanceConfig::StaticRouteList::iterator, bool> ret =
                inet6_list.insert(item);
            if (!ret.second) {
                BGP_LOG_WARNING_STR(BgpConfig, BGP_LOG_FLAG_ALL,
                    "Duplicate static route prefix " << route.prefix <<
                    " with nexthop " << route.next_hop <<
                    " for routing instance " << rti->name());
            }
        }
    }
    rti->swap_static_routes(Address::INET, &inet_list);
    rti->swap_static_routes(Address::INET6, &inet6_list);
}

static void SetServiceChainConfig(BgpInstanceConfig *rti,
    const autogen::RoutingInstance *config) {
    BgpInstanceConfig::ServiceChainList list;
    const autogen::ServiceChainInfo &inet_chain =
        config->service_chain_information();
    if (config->IsPropertySet(
        autogen::RoutingInstance::SERVICE_CHAIN_INFORMATION)) {
        ServiceChainConfig item = {
            SCAddress::INET,
            inet_chain.routing_instance,
            inet_chain.prefix,
            inet_chain.service_chain_address,
            inet_chain.service_instance,
            inet_chain.source_routing_instance,
            inet_chain.service_chain_id,
            inet_chain.sc_head,
        };
        list.push_back(item);
    }

    const autogen::ServiceChainInfo &inet6_chain =
        config->ipv6_service_chain_information();
    if (config->IsPropertySet(
        autogen::RoutingInstance::IPV6_SERVICE_CHAIN_INFORMATION)) {
        ServiceChainConfig item = {
            SCAddress::INET6,
            inet6_chain.routing_instance,
            inet6_chain.prefix,
            inet6_chain.service_chain_address,
            inet6_chain.service_instance,
            inet6_chain.source_routing_instance,
            inet6_chain.service_chain_id,
            inet6_chain.sc_head,
        };
        list.push_back(item);
    }

    const autogen::ServiceChainInfo &evpn_chain =
        config->evpn_service_chain_information();
    if (config->IsPropertySet(
        autogen::RoutingInstance::EVPN_SERVICE_CHAIN_INFORMATION)) {
        ServiceChainConfig item = {
            SCAddress::EVPN,
            evpn_chain.routing_instance,
            evpn_chain.prefix,
            evpn_chain.service_chain_address,
            evpn_chain.service_instance,
            evpn_chain.source_routing_instance,
            evpn_chain.service_chain_id,
            evpn_chain.sc_head,
        };
        list.push_back(item);
    }

    const autogen::ServiceChainInfo &evpnv6_chain =
        config->evpn_ipv6_service_chain_information();
    if (config->IsPropertySet(
        autogen::RoutingInstance::EVPN_IPV6_SERVICE_CHAIN_INFORMATION)) {
        ServiceChainConfig item = {
            SCAddress::EVPN6,
            evpnv6_chain.routing_instance,
            evpnv6_chain.prefix,
            evpnv6_chain.service_chain_address,
            evpnv6_chain.service_instance,
            evpnv6_chain.source_routing_instance,
            evpnv6_chain.service_chain_id,
            evpnv6_chain.sc_head,
        };
        list.push_back(item);
    }

    rti->swap_service_chain_list(&list);
}

void BgpIfmapConfigManager::ProcessRoutingPolicyLink(
    const BgpConfigDelta &delta) {
    CHECK_CONCURRENCY("bgp::Config");

    BgpIfmapRoutingPolicyLinkConfig *ri_rp_link
        = config_->FindRoutingPolicyLink(delta.id_name);
    if (ri_rp_link == NULL) {
        IFMapNodeProxy *proxy = delta.node.get();
        if (proxy == NULL) {
            return;
        }
        IFMapNode *node = proxy->node();
        if (node == NULL || delta.obj.get() == NULL) {
            return;
        }

        pair<IFMapNode *, IFMapNode *> ri_rp_pair;
        if (!BgpIfmapRoutingPolicyLinkConfig::GetInstancePolicyPair(db_graph_,
            node, &ri_rp_pair)) {
            return;
        }

        string instance_name = ri_rp_pair.first->name();
        string policy_name = ri_rp_pair.second->name();

        BgpIfmapInstanceConfig *rti  =
            config_->FindInstance(instance_name);
        BgpIfmapRoutingPolicyConfig *rtp  =
            config_->FindRoutingPolicy(policy_name);
        if (!rti || !rtp) {
            return;
        }

        ri_rp_link = config_->CreateRoutingPolicyLink(rti, rtp, proxy);
    } else {
        const IFMapNode *node = ri_rp_link->node();
        assert(node != NULL);
        if (delta.obj.get() == NULL) {
            BgpIfmapRoutingPolicyConfig *rtp = ri_rp_link->policy();
            BgpIfmapInstanceConfig *rti = ri_rp_link->instance();
            config_->DeleteRoutingPolicyLink(ri_rp_link);
            if (rtp->DeleteIfEmpty(this)) {
                config_->DeleteRoutingPolicy(rtp);
            }
            if (rti->DeleteIfEmpty(this)) {
                BGP_CONFIG_LOG_INSTANCE(Delete, server(), rti,
                    SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL);
                config_->DeleteInstance(rti);
            }
            return;
        }
    }

    autogen::RoutingPolicyRoutingInstance *ri_rp_link_cfg =
        static_cast<autogen::RoutingPolicyRoutingInstance *>(delta.obj.get());

    ri_rp_link->Update(this, ri_rp_link_cfg);
}

static bool CompareRoutingPolicyOrder(const RoutingPolicyAttachInfo &lhs,
                                      const RoutingPolicyAttachInfo &rhs) {
    return (lhs.sequence_ < rhs.sequence_);
}

void BgpIfmapInstanceConfig::ProcessIdentifierUpdate(uint32_t new_id,
                                                     uint32_t old_id) {
    BgpInstanceConfig::RouteTargetList import_list = data_.import_list();
    if (old_id > 0) {
        string old_vit = "target:" + GetVitFromId(ntohl(old_id));
        import_list.erase(old_vit);
    }
    if (new_id > 0) {
        string new_vit = "target:" + GetVitFromId(ntohl(new_id));
        import_list.insert(new_vit);
    }
    data_.set_import_list(import_list);
}

void BgpIfmapInstanceConfig::ProcessASUpdate(uint32_t new_as, uint32_t old_as) {
    BgpInstanceConfig::RouteTargetList import_list = data_.import_list();
    if (old_as > 0) {
        string old_es_rtarget = "target:" + GetESRouteTarget(old_as);
        import_list.erase(old_es_rtarget);
    }
    if (new_as > 0) {
        string new_es_rtarget = "target:" + GetESRouteTarget(new_as);
        import_list.insert(new_es_rtarget);
    }
    data_.set_import_list(import_list);
}

string BgpIfmapInstanceConfig::GetVitFromId(uint32_t identifier) const {
    if (identifier == 0)
        return "";
    return Ip4Address(identifier).to_string() + ":" + integerToString(index());
}

string BgpIfmapInstanceConfig::GetESRouteTarget(uint32_t as) const {
    if (as == 0)
        return "";
    if (as > 0xffFF)
        return integerToString(as) + ":" +
            integerToString(EVPN_ES_IMPORT_ROUTE_TARGET_AS4);
    return integerToString(as) + ":" +
        integerToString(EVPN_ES_IMPORT_ROUTE_TARGET_AS2);
}

// Populate vrf import route-target (used for MVPN) and ES route target in
// import list.
void BgpIfmapInstanceConfig::InsertVitAndESRTargetInImportList(
                         BgpIfmapConfigManager *mgr,
                         BgpInstanceConfig::RouteTargetList& import_list) {
    const BgpIfmapInstanceConfig *master_instance =
        mgr->config()->FindInstance(BgpConfigManager::kMasterInstance);
    uint32_t bgp_identifier = 0;
    uint32_t as = 0;
    if (master_instance) {
        const BgpIfmapProtocolConfig *master_protocol =
            master_instance->protocol_config();
        if (master_protocol) {
            if (master_protocol->protocol_config()) {
                bgp_identifier =
                    master_protocol->protocol_config()->identifier();
                as = master_protocol->protocol_config()->autonomous_system();
            }
        }
    }
    if (bgp_identifier > 0)
        import_list.insert("target:" + GetVitFromId(ntohl(bgp_identifier)));
    if (as > 0)
        import_list.insert("target:" + GetESRouteTarget(as));
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
    BgpInstanceConfig::AggregateRouteList inet6_aggregate_list;
    BgpInstanceConfig::AggregateRouteList inet_aggregate_list;
    RoutingPolicyConfigList policy_list;
    data_.Clear();

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
        } else if (strcmp(adj->table()->Typename(),
                          "routing-policy-routing-instance") == 0) {
            RoutingPolicyAttachInfo policy_info;
            if (GetRoutingInstanceRoutingPolicy(graph, adj, &policy_info)) {
                policy_list.push_back(policy_info);
            }
        } else if (strcmp(adj->table()->Typename(), "route-aggregate") == 0) {
            GetRouteAggregateConfig(graph, adj, &inet_aggregate_list,
                                    &inet6_aggregate_list);
        } else if (strcmp(adj->table()->Typename(), "connection") == 0) {
            vector<string> target_list;
            GetConnectionExportTargets(graph, node, name_, adj, &target_list);
            BOOST_FOREACH(string target, target_list) {
                import_list.insert(target);
            }
        } else if (strcmp(adj->table()->Typename(), "virtual-network") == 0) {
            data_.set_virtual_network(adj->name());
            data_.set_virtual_network_index(GetVirtualNetworkIndex(graph, adj));
            data_.set_virtual_network_allow_transit(
                GetVirtualNetworkAllowTransit(graph, adj));
            data_.set_vxlan_id(GetVirtualNetworkVxlanId(graph, adj));
            data_.set_virtual_network_pbb_evpn_enable(
                GetVirtualNetworkPbbEvpnEnable(graph, adj));
        }
    }

    InsertVitAndESRTargetInImportList(manager, import_list);
    data_.set_import_list(import_list);
    data_.set_export_list(export_list);

    sort(policy_list.begin(), policy_list.end(), CompareRoutingPolicyOrder);
    data_.swap_routing_policy_list(&policy_list);
    data_.swap_aggregate_routes(Address::INET, &inet_aggregate_list);
    data_.swap_aggregate_routes(Address::INET6, &inet6_aggregate_list);

    if (config) {
        data_.set_has_pnf(config->has_pnf());
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
// Return true if the BgpIfmapInstanceConfig is ready to be deleted.  The
// caller is responsible for actually deleting it.
//
bool BgpIfmapInstanceConfig::DeleteIfEmpty(BgpConfigManager *manager) {
    if (name_ == BgpConfigManager::kMasterInstance) {
        return false;
    }
    if (node() != NULL || protocol_.get() != NULL) {
        return false;
    }
    if (!neighbors_.empty() || !peerings_.empty() ||
        !routing_policies_.empty()) {
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
        neighbor->admin_down(),
        neighbor->passive(),
        BgpIdentifierToString(neighbor->local_identifier()),
        neighbor->local_as(),
        neighbor->peer_address().to_string(), neighbor->peer_as(),
        neighbor->GetAddressFamilies(), neighbor->AuthKeyTypeToString(),
        neighbor->AuthKeysToString());
    neighbors_.insert(make_pair(neighbor->name(), neighbor));
    data_.add_neighbor(neighbor->name());
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
        neighbor->admin_down(),
        neighbor->passive(),
        BgpIdentifierToString(neighbor->local_identifier()),
        neighbor->local_as(),
        neighbor->peer_address().to_string(), neighbor->peer_as(),
        neighbor->GetAddressFamilies(), neighbor->AuthKeyTypeToString(),
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
    data_.delete_neighbor(neighbor->name());
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
// Add a BgpIfmapRoutingPolicyConfig to this BgpIfmapInstanceConfig.
//
void BgpIfmapInstanceConfig::AddRoutingPolicy(
    BgpIfmapRoutingPolicyConfig *rtp) {
    routing_policies_.insert(make_pair(rtp->name(), rtp));
}

//
// Delete a BgpIfmapRoutingPolicyConfig from this BgpIfmapInstanceConfig.
//
void BgpIfmapInstanceConfig::DeleteRoutingPolicy(
    BgpIfmapRoutingPolicyConfig *rtp) {
    routing_policies_.erase(rtp->name());
}

void BgpIfmapConfigData::ProcessIdentifierAndASUpdate(
                            BgpIfmapConfigManager* manager,
                            uint32_t new_id, uint32_t old_id,
                            uint32_t new_as, uint32_t old_as) {
    assert(new_id != old_id || new_as != old_as);
    for (unsigned int i = 0; i < instances_.size(); i++) {
        BgpIfmapInstanceConfig * ifmap_config = instances_.At(i);
        if (!ifmap_config)
            continue;
        if (new_id != old_id)
            ifmap_config->ProcessIdentifierUpdate(new_id, old_id);
        if (new_as != old_as)
            ifmap_config->ProcessASUpdate(new_as, old_as);
        manager->UpdateInstanceConfig(ifmap_config,
                                      BgpConfigManager::CFG_CHANGE);
    }
}

//
// Constructor for BgpIfmapConfigData.
//
BgpIfmapConfigData::BgpIfmapConfigData() {
    // Reserve bit 0 for master instance
    instances_.ReserveBit(0);
}

//
// Destructor for BgpConfigData.
//
BgpIfmapConfigData::~BgpIfmapConfigData() {
    instances_.clear();
    STLDeleteElements(&peerings_);
    STLDeleteElements(&routing_policies_);
    STLDeleteElements(&ri_rp_links_);
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
    int index = -1;
    if (name == BgpConfigManager::kMasterInstance) {
        index = 0;
    }
    index = instances_.Insert(name, rti, index);
    rti->set_index(index);
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
    BgpInstanceMap::iterator loc2 = instance_config_map_.find(rti->name());
    assert(loc2 != instance_config_map_.end());
    instance_config_map_.erase(loc2);
    instances_.Remove(rti->name(), rti->index(), false);
}

//
// Find the BgpIfmapInstanceConfig by name.
//
BgpIfmapInstanceConfig *BgpIfmapConfigData::FindInstance(const string &name) {
    return instances_.Find(name);
}

//
// Find the BgpIfmapInstanceConfig by name.
// Const version.
//
const BgpIfmapInstanceConfig *BgpIfmapConfigData::FindInstance(
    const string &name) const {
    return instances_.Find(name);
}

BgpConfigManager::NeighborMapRange
BgpIfmapInstanceConfig::NeighborMapItems() const {
    return make_pair(neighbors_.begin(), neighbors_.end());
}

//
// Create a new BgpIfmapRoutingPolicyLinkConfig.
//
// The IFMapNodeProxy is a proxy for the IFMapNode which is the midnode that
// represents the routing-policy-routing-instance. The newly created
// BgpIfmapRoutingPolicyLinkConfig gets added to the IfmapRoutingPolicyLinkMap.
//
BgpIfmapRoutingPolicyLinkConfig *BgpIfmapConfigData::CreateRoutingPolicyLink(
    BgpIfmapInstanceConfig *rti, BgpIfmapRoutingPolicyConfig *rtp,
    IFMapNodeProxy *proxy) {
    BgpIfmapRoutingPolicyLinkConfig *ri_rp_link =
        new BgpIfmapRoutingPolicyLinkConfig(rti, rtp);
    ri_rp_link->SetNodeProxy(proxy);
    pair<IfmapRoutingPolicyLinkMap::iterator, bool> result =
        ri_rp_links_.insert(make_pair(ri_rp_link->node()->name(), ri_rp_link));
    assert(result.second);
    ri_rp_link->instance()->AddRoutingPolicy(rtp);
    ri_rp_link->policy()->AddInstance(rti);
    return ri_rp_link;
}

//
// Delete a BgpIfmapRoutingPolicyLinkConfig.
//
// The BgpIfmapRoutingPolicyLinkConfig is erased from IfmapRoutingPolicyLinkMap
// and then deleted.
// Note that the reference to the IFMapNode for routing-policy-routing-instance
// gets released via the destructor when the IFMapNodeProxy is destroyed.
//
void BgpIfmapConfigData::DeleteRoutingPolicyLink(
    BgpIfmapRoutingPolicyLinkConfig *ri_rp_link) {
    ri_rp_links_.erase(ri_rp_link->node()->name());
    ri_rp_link->instance()->DeleteRoutingPolicy(ri_rp_link->policy());
    ri_rp_link->policy()->RemoveInstance(ri_rp_link->instance());
    delete ri_rp_link;
}

//
// Find the BgpIfmapRoutingPolicyLinkConfig by name.
//
BgpIfmapRoutingPolicyLinkConfig *BgpIfmapConfigData::FindRoutingPolicyLink(
    const string &name) {
    IfmapRoutingPolicyLinkMap::iterator loc = ri_rp_links_.find(name);
    if (loc != ri_rp_links_.end()) {
        return loc->second;
    }
    return NULL;
}

//
// Find the BgpIfmapRoutingPolicyLinkConfig by name.
// Const version.
//
const BgpIfmapRoutingPolicyLinkConfig *
BgpIfmapConfigData::FindRoutingPolicyLink(const string &name) const {
    IfmapRoutingPolicyLinkMap::const_iterator loc = ri_rp_links_.find(name);
    if (loc != ri_rp_links_.end()) {
        return loc->second;
    }
    return NULL;
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
    BGP_LOG_STR(BgpConfig, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_SYSLOG,
         "Creating BgpIfmapPeering " << peering->node()->name());
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
    BGP_LOG_STR(BgpConfig, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_SYSLOG,
         "Deleting BgpIfmapPeering " << peering->node()->name());
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

BgpConfigManager::RoutingPolicyMapRange
BgpIfmapConfigData::RoutingPolicyMapItems(const string &start_name) const {
    return make_pair(routing_policy_config_map_.lower_bound(start_name),
        routing_policy_config_map_.end());
}

//
// Locate the BgpIfmapRoutingPolicyConfig by name, create it if not found.
// The newly created BgpIfmapRoutingPolicyConfig gets added to the
// IfmapRoutingPolicyMap.
//
BgpIfmapRoutingPolicyConfig *BgpIfmapConfigData::LocateRoutingPolicy(
    const string &name) {
    BgpIfmapRoutingPolicyConfig *rtp = FindRoutingPolicy(name);
    if (rtp != NULL) {
        return rtp;
    }
    rtp = new BgpIfmapRoutingPolicyConfig(name);
    pair<IfmapRoutingPolicyMap::iterator, bool> result =
            routing_policies_.insert(make_pair(name, rtp));
    assert(result.second);
    pair<BgpRoutingPolicyMap::iterator, bool> result2 =
            routing_policy_config_map_.insert(
                make_pair(name, rtp->routing_policy_config()));
    assert(result2.second);
    return rtp;
}

//
// Remove the given BgpIfmapRoutingPolicyConfig from the IfmapRoutingPolicyMap
// and delete it.
//
void BgpIfmapConfigData::DeleteRoutingPolicy(BgpIfmapRoutingPolicyConfig *rtp) {
    IfmapRoutingPolicyMap::iterator loc = routing_policies_.find(rtp->name());
    assert(loc != routing_policies_.end());
    routing_policies_.erase(loc);
    BgpRoutingPolicyMap::iterator loc2 =
        routing_policy_config_map_.find(rtp->name());
    assert(loc2 != routing_policy_config_map_.end());
    routing_policy_config_map_.erase(loc2);
    delete rtp;
}

BgpIfmapRoutingPolicyConfig *BgpIfmapConfigData::FindRoutingPolicy(
    const string &name) {
    IfmapRoutingPolicyMap::iterator loc = routing_policies_.find(name);
    if (loc != routing_policies_.end()) {
        return loc->second;
    }
    return NULL;
}

const BgpIfmapRoutingPolicyConfig *BgpIfmapConfigData::FindRoutingPolicy(
    const string &name) const {
    IfmapRoutingPolicyMap::const_iterator loc = routing_policies_.find(name);
    if (loc != routing_policies_.end()) {
        return loc->second;
    }
    return NULL;
}

//
// Constructor for BgpIfmapConfigManager.
//
BgpIfmapConfigManager::BgpIfmapConfigManager(BgpServer *server)
    : BgpConfigManager(server),
      db_(NULL),
      db_graph_(NULL),
      trigger_(boost::bind(&BgpIfmapConfigManager::ConfigHandler, this),
          TaskScheduler::GetInstance()->GetTaskId("bgp::Config"),
          kConfigTaskInstanceId),
      listener_(new BgpConfigListener(this)),
      config_(new BgpIfmapConfigData()) {
    IdentifierMapInit();
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
    CHECK_CONCURRENCY("db::IFMapTable");
    trigger_.Set();
}

BgpConfigManager::InstanceMapRange
BgpIfmapConfigManager::InstanceMapItems(const string &start_name) const {
    return config_->InstanceMapItems(start_name);
}

BgpConfigManager::RoutingPolicyMapRange
BgpIfmapConfigManager::RoutingPolicyMapItems(const string &start_name) const {
    return config_->RoutingPolicyMapItems(start_name);
}

BgpConfigManager::NeighborMapRange
BgpIfmapConfigManager::NeighborMapItems(
    const string &instance_name) const {
    static BgpConfigManager::NeighborMap nilMap;
    BgpIfmapInstanceConfig *rti = config_->FindInstance(instance_name);
    if (rti == NULL) {
        return make_pair(nilMap.begin(), nilMap.end());
    }
    return rti->NeighborMapItems();
}

void BgpIfmapConfigManager::ResetRoutingInstanceIndexBit(int index) {
    config()->instances().ResetBit(index);
}

int BgpIfmapConfigManager::NeighborCount(
    const string &instance_name) const {
    BgpIfmapInstanceConfig *rti = config_->FindInstance(instance_name);
    if (rti == NULL) {
        return 0;
    }
    return rti->neighbors().size();
}

//
// Constructor for BgpIfmapRoutingPolicyConfig.
//
BgpIfmapRoutingPolicyConfig::BgpIfmapRoutingPolicyConfig(
    const string &name)
    : name_(name),
      data_(name) {
}

//
// Destructor for BgpIfmapRoutingPolicyConfig.
//
BgpIfmapRoutingPolicyConfig::~BgpIfmapRoutingPolicyConfig() {
}

//
// Set the IFMapNodeProxy for the BgpIfmapRoutingPolicyConfig.
//
void BgpIfmapRoutingPolicyConfig::SetNodeProxy(IFMapNodeProxy *proxy) {
    if (proxy != NULL) {
        node_proxy_.Swap(proxy);
    }
}

void BgpIfmapRoutingPolicyConfig::Delete(BgpConfigManager *manager) {
    manager->Notify(&data_, BgpConfigManager::CFG_DELETE);
    routing_policy_.reset();
}

//
// Return true if the BgpIfmapRoutingPolicyConfig is ready to be deleted.
// The caller is responsible for actually deleting it.
//
bool BgpIfmapRoutingPolicyConfig::DeleteIfEmpty(BgpConfigManager *manager) {
    if (node() != NULL) {
        return false;
    }
    if (!instances_.empty()) {
        return false;
    }

    Delete(manager);
    return true;
}

//
// Add a BgpIfmapInstanceConfig to BgpIfmapRoutingPolicyConfig.
//
void BgpIfmapRoutingPolicyConfig::AddInstance(BgpIfmapInstanceConfig *rti) {
    instances_.insert(make_pair(rti->name(), rti));
}

//
// Remove a BgpIfmapInstanceConfig to BgpIfmapRoutingPolicyConfig.
//
void BgpIfmapRoutingPolicyConfig::RemoveInstance(BgpIfmapInstanceConfig *rti) {
    instances_.erase(rti->name());
}


static void BuildPolicyTermConfig(autogen::PolicyTermType cfg_term,
    RoutingPolicyTermConfig *term) {
    term->match.protocols_match = cfg_term.term_match_condition.protocol;
    BOOST_FOREACH(const autogen::PrefixMatchType &prefix_match,
                  cfg_term.term_match_condition.prefix) {
        string prefix_type(prefix_match.prefix_type);
        PrefixMatchConfig match(prefix_match.prefix,
            prefix_type.empty() ? "exact" : prefix_type);
        term->match.prefixes_to_match.push_back(match);
    }
    term->match.community_match_all =
        cfg_term.term_match_condition.community_match_all;
    if (!cfg_term.term_match_condition.community_list.empty()) {
        term->match.community_match =
            cfg_term.term_match_condition.community_list;
    }
    if (!cfg_term.term_match_condition.community.empty()) {
        term->match.community_match.push_back(
            cfg_term.term_match_condition.community);
    }
    term->match.ext_community_match_all =
        cfg_term.term_match_condition.extcommunity_match_all;
    if (!cfg_term.term_match_condition.extcommunity_list.empty()) {
        term->match.ext_community_match =
            cfg_term.term_match_condition.extcommunity_list;
    }

    BOOST_FOREACH(uint32_t asn,
        cfg_term.term_action_list.update.as_path.expand.asn_list) {
        term->action.update.aspath_expand.push_back(asn);
    }
    BOOST_FOREACH(const string community,
                  cfg_term.term_action_list.update.community.add.community) {
        term->action.update.community_add.push_back(community);
    }
    BOOST_FOREACH(const string community,
                  cfg_term.term_action_list.update.community.remove.community) {
        term->action.update.community_remove.push_back(community);
    }
    BOOST_FOREACH(const string community,
                  cfg_term.term_action_list.update.community.set.community) {
        term->action.update.community_set.push_back(community);
    }
    BOOST_FOREACH(const string community,
                  cfg_term.term_action_list.update.extcommunity.add.community) {
        term->action.update.ext_community_add.push_back(community);
    }
    BOOST_FOREACH(const string community,
                  cfg_term.term_action_list.update.extcommunity.remove.community) {
        term->action.update.ext_community_remove.push_back(community);
    }
    BOOST_FOREACH(const string community,
                  cfg_term.term_action_list.update.extcommunity.set.community) {
        term->action.update.ext_community_set.push_back(community);
    }
    term->action.update.local_pref =
        cfg_term.term_action_list.update.local_pref;
    term->action.update.med = cfg_term.term_action_list.update.med;

    term->action.action = RoutingPolicyActionConfig::NEXT_TERM;
    if (strcmp(cfg_term.term_action_list.action.c_str(), "reject") == 0) {
        term->action.action = RoutingPolicyActionConfig::REJECT;
    } else if (
        strcmp(cfg_term.term_action_list.action.c_str(), "accept") == 0) {
        term->action.action = RoutingPolicyActionConfig::ACCEPT;
    }
}

static void BuildPolicyTermsConfig(BgpRoutingPolicyConfig *policy_cfg,
    const autogen::RoutingPolicy *policy) {
    vector<autogen::PolicyTermType> terms = policy->entries();
    BOOST_FOREACH(autogen::PolicyTermType cfg_term, terms) {
        RoutingPolicyTermConfig policy_term_cfg;
        BuildPolicyTermConfig(cfg_term, &policy_term_cfg);
        policy_cfg->add_term(policy_term_cfg);
    }
}

void BgpIfmapRoutingPolicyConfig::Update(BgpIfmapConfigManager *manager,
    const autogen::RoutingPolicy *policy) {
    routing_policy_.reset(policy);
    data_.Clear();
    if (policy) {
        BuildPolicyTermsConfig(&data_, policy);
    }
}

//
// Reset IFMap related state in the BgpIfmapRoutingPolicyConfig.
//
void BgpIfmapRoutingPolicyConfig::ResetConfig() {
    node_proxy_.Clear();
}

const BgpRoutingPolicyConfig *BgpIfmapConfigManager::FindRoutingPolicy(
    const string &name) const {
    BgpIfmapRoutingPolicyConfig *rtp = config_->FindRoutingPolicy(name);
    if (rtp == NULL) {
        return NULL;
    }
    return rtp->routing_policy_config();
}

const BgpInstanceConfig *BgpIfmapConfigManager::FindInstance(
    const string &name) const {
    BgpIfmapInstanceConfig *rti = config_->FindInstance(name);
    if (rti == NULL) {
        return NULL;
    }
    return rti->instance_config();
}

const BgpProtocolConfig *BgpIfmapConfigManager::GetProtocolConfig(
    const string &instance_name) const {
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
    const string &instance_name, const string &name) const {
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
    autogen::BgpRouterParams *param) {
    param->Clear();
    param->autonomous_system = BgpConfigManager::kDefaultAutonomousSystem;
    param->port = BgpConfigManager::kDefaultPort;
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
    DefaultBgpRouterParams(&param);
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
        protocol->router_params().admin_down,
        protocol->router_params().autonomous_system,
        protocol->router_params().identifier,
        protocol->router_params().address,
        protocol->router_params().hold_time,
        vector<string>());
}

bool BgpIfmapGlobalSystemConfig::Update(BgpIfmapConfigManager *manager,
        const autogen::GlobalSystemConfig *system) {
    bool changed = false;

    if (data_.gr_enable() != system->graceful_restart_parameters().enable) {
        data_.set_gr_enable(system->graceful_restart_parameters().enable);
        changed |= true;
    }

    if (data_.gr_time() != system->graceful_restart_parameters().restart_time) {
        data_.set_gr_time(system->graceful_restart_parameters().restart_time);
        changed |= true;
    }

    if (data_.llgr_time() != static_cast<uint32_t>(
            system->graceful_restart_parameters().long_lived_restart_time)) {
        data_.set_llgr_time(
            system->graceful_restart_parameters().long_lived_restart_time);
        changed |= true;
    }

    if (data_.end_of_rib_timeout() !=
            system->graceful_restart_parameters().end_of_rib_timeout) {
        data_.set_end_of_rib_timeout(
            system->graceful_restart_parameters().end_of_rib_timeout);
        changed |= true;
    }

    if (data_.gr_bgp_helper() !=
            system->graceful_restart_parameters().bgp_helper_enable) {
        data_.set_gr_bgp_helper(
            system->graceful_restart_parameters().bgp_helper_enable);
        changed |= true;
    }

    if (data_.gr_xmpp_helper() !=
            system->graceful_restart_parameters().xmpp_helper_enable) {
        data_.set_gr_xmpp_helper(
            system->graceful_restart_parameters().xmpp_helper_enable);
        changed |= true;
    }

    if (data_.enable_4byte_as() != system->enable_4byte_as()) {
        data_.set_enable_4byte_as(system->enable_4byte_as());
        changed |= true;
    }

    if (data_.always_compare_med() != system->bgp_always_compare_med()) {
        data_.set_always_compare_med(system->bgp_always_compare_med());
        changed |= true;
    }

    if (data_.rd_cluster_seed() != system->rd_cluster_seed()) {
        data_.set_rd_cluster_seed(system->rd_cluster_seed());
        changed |= true;
    }

    if (data_.bgpaas_port_start() != system->bgpaas_parameters().port_start) {
        data_.set_bgpaas_port_start(system->bgpaas_parameters().port_start);
        changed |= true;
    }

    if (data_.bgpaas_port_end() != system->bgpaas_parameters().port_end) {
        data_.set_bgpaas_port_end(system->bgpaas_parameters().port_end);
        changed |= true;
    }

    return changed;
}

bool BgpIfmapGlobalQosConfig::Update(BgpIfmapConfigManager *manager,
        const autogen::GlobalQosConfig *qos) {
    bool changed = false;
    const autogen::ControlTrafficDscpType &dscp = qos->control_traffic_dscp();

    if (data_.control_dscp() != dscp.control) {
        data_.set_control_dscp(dscp.control);
        Sandesh::SetDscpValue(dscp.control);
        changed = true;
    }
    if (data_.analytics_dscp() != dscp.analytics) {
        data_.set_analytics_dscp(dscp.analytics);
        changed = true;
    }
    return changed;
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
    id_map_.insert(make_pair("routing-policy",
        boost::bind(&BgpIfmapConfigManager::ProcessRoutingPolicy, this, _1)));
    id_map_.insert(make_pair("routing-policy-routing-instance",
        boost::bind(&BgpIfmapConfigManager::ProcessRoutingPolicyLink, this,
            _1)));
    id_map_.insert(make_pair("bgp-router",
        boost::bind(&BgpIfmapConfigManager::ProcessBgpRouter, this, _1)));
    id_map_.insert(make_pair("bgp-peering",
        boost::bind(&BgpIfmapConfigManager::ProcessBgpPeering, this, _1)));
    id_map_.insert(make_pair("global-system-config",
        boost::bind(&BgpIfmapConfigManager::ProcessGlobalSystemConfig, this,
                    _1)));
    id_map_.insert(make_pair("global-qos-config",
        boost::bind(&BgpIfmapConfigManager::ProcessGlobalQosConfig, this,
                    _1)));
}

void BgpIfmapConfigManager::UpdateInstanceConfig(BgpIfmapInstanceConfig *rti,
        BgpConfigManager::EventType event) {
    if (!rti) {
        return;
    }

    // in case of id change, update import list and call subsequent code
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
// Handler for routing-instance objects.
//
// Note that BgpIfmapInstanceConfig object for the master instance is created
// before we have received any configuration for it i.e. there's no IFMapNode
// or autogen::RoutingInstance for it. However, we will eventually receive a
// delta for the master.  The IFMapNodeProxy and autogen::RoutingInstance are
// set at that time.
//
// For other routing-instances BgpIfmapConfigInstance can get created before we
// see the IFMapNode for the routing-instance if we see the IFMapNode for the
// local bgp-router in the routing-instance.  In this case, the IFMapNodeProxy
// and autogen::RoutingInstance are set when we later see the IFMapNode for the
// routing-instance.
//
// In all other cases, BgpIfmapConfigInstance is created when we see IFMapNode
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
    if (rti->index() != -1)
        rti->instance_config()->set_index(rti->index());
    rti->Update(this, rti_config);
    UpdateInstanceConfig(rti, event);
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
            UpdateInstanceConfig(rti, event);
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
    uint32_t old_id = protocol->protocol_config()->identifier();
    uint32_t old_as = protocol->protocol_config()->autonomous_system();
    protocol->Update(this, rt_config);
    uint32_t new_id = protocol->protocol_config()->identifier();
    uint32_t new_as = protocol->protocol_config()->autonomous_system();
    if (new_id != old_id || new_as != old_as) {
        config_->ProcessIdentifierAndASUpdate(this, new_id, old_id, new_as,
                                              old_as);
    }
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
            protocol->router_params().admin_down,
            protocol->router_params().autonomous_system,
            protocol->router_params().identifier,
            protocol->router_params().address,
            protocol->router_params().hold_time,
            families);
    } else {
        BGP_CONFIG_LOG_PROTOCOL(Update, server(), protocol,
            SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            protocol->router_params().admin_down,
            protocol->router_params().autonomous_system,
            protocol->router_params().identifier,
            protocol->router_params().address,
            protocol->router_params().hold_time,
            families);
    }
}

//
// Handler for routing policy objects.
//
// BgpConfigListener::DependencyTracker ensures associated routing instances
// are present in the change list.
//
void BgpIfmapConfigManager::ProcessRoutingPolicy(const BgpConfigDelta &delta) {
    CHECK_CONCURRENCY("bgp::Config");

    BgpConfigManager::EventType event = BgpConfigManager::CFG_CHANGE;
    string policy_name = delta.id_name;
    BgpIfmapRoutingPolicyConfig *rtp = config_->FindRoutingPolicy(policy_name);
    if (rtp == NULL) {
        IFMapNodeProxy *proxy = delta.node.get();
        if (proxy == NULL) {
            return;
        }
        IFMapNode *node = proxy->node();
        if (node == NULL || node->IsDeleted()) {
            return;
        }
        event = BgpConfigManager::CFG_ADD;
        rtp = config_->LocateRoutingPolicy(policy_name);
        rtp->SetNodeProxy(proxy);
    } else {
        IFMapNode *node = rtp->node();
        if (node == NULL) {
            IFMapNodeProxy *proxy = delta.node.get();
            if (proxy == NULL) {
                return;
            }
            rtp->SetNodeProxy(proxy);
        } else if (node->IsDeleted()) {
            rtp->ResetConfig();
            if (rtp->DeleteIfEmpty(this)) {
                config_->DeleteRoutingPolicy(rtp);
            }
            return;
        }
    }

    autogen::RoutingPolicy *rtp_config =
        static_cast<autogen::RoutingPolicy *>(delta.obj.get());
    rtp->Update(this, rtp_config);
    Notify(rtp->routing_policy_config(), event);
}


//
// Handler for bgp-router objects.
//
void BgpIfmapConfigManager::ProcessBgpRouter(const BgpConfigDelta &delta) {
    CHECK_CONCURRENCY("bgp::Config");

    string instance_name(IdentifierParent(delta.id_name));
    if (instance_name.empty() ||
        instance_name != BgpConfigManager::kMasterInstance) {
        return;
    }

    // Ignore if this change is not for the local router.
    string name = delta.id_name.substr(instance_name.size() + 1);
    if (name != localname_) {
        return;
    }

    ProcessBgpProtocol(delta);

    // Update all peerings since we use local asn and identifier from master
    // instance for all neighbors, including those in non-master instances.
    BOOST_FOREACH(const BgpIfmapConfigData::IfmapPeeringMap::value_type &value,
        config_->peerings()) {
        BgpIfmapPeeringConfig *peering = value.second;
        peering->Update(this, peering->bgp_peering());
    }
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
            BGP_LOG_STR(BgpConfig, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_SYSLOG,
                 "ProcessBgpPeering failed. Cannot find proxy " <<
                 delta.id_name);
            return;
        }
        IFMapNode *node = proxy->node();
        if (node == NULL || delta.obj.get() == NULL) {
            BGP_LOG_STR(BgpConfig, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_SYSLOG,
                 "ProcessBgpPeering failed. Cannot find node/obj " <<
                  delta.id_name);
            return;
        }

        pair<IFMapNode *, IFMapNode *> routers;
        if (!BgpIfmapPeeringConfig::GetRouterPair(db_graph_, localname_, node,
                                                  &routers)) {
            return;
        }

        string instance_name(IdentifierParent(routers.first->name()));
        BgpIfmapInstanceConfig *rti = config_->FindInstance(instance_name);
        event = BgpConfigManager::CFG_ADD;
        // Create rti if not present.
        if (rti == NULL) {
            rti = config_->LocateInstance(instance_name);
            UpdateInstanceConfig(rti, event);
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

void BgpIfmapConfigManager::ProcessGlobalSystemConfig(
        const BgpConfigDelta &delta) {
    IFMapNodeProxy *proxy = delta.node.get();
    if (proxy == NULL)
        return;

    IFMapNode *node = proxy->node();
    autogen::GlobalSystemConfig *config, default_config;
    if (node == NULL || node->IsDeleted() || delta.obj.get() == NULL) {
        config = &default_config;
    } else {
        config = static_cast<autogen::GlobalSystemConfig *>(delta.obj.get());
    }

    if (config_->global_config()->Update(this, config))
        Notify(config_->global_config()->config(), BgpConfigManager::CFG_ADD);
}

void BgpIfmapConfigManager::ProcessGlobalQosConfig(
        const BgpConfigDelta &delta) {
    IFMapNodeProxy *proxy = delta.node.get();
    if (proxy == NULL)
        return;

    IFMapNode *node = proxy->node();
    autogen::GlobalQosConfig *config, default_config;
    if (node == NULL || node->IsDeleted() || delta.obj.get() == NULL) {
        config = &default_config;
    } else {
        config = static_cast<autogen::GlobalQosConfig *>(delta.obj.get());
    }

    if (config_->global_qos()->Update(this, config))
        Notify(config_->global_qos()->config(), BgpConfigManager::CFG_ADD);
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
