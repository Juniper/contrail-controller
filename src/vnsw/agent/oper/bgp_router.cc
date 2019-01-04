/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent.h>
#include <vnc_cfg_types.h>
#include <bgp_schema_types.h>
#include <oper_db.h>
#include <bgp_router.h>
#include <config_manager.h>

////////////////////////////////////////////////////////////////////////////////
// ControlNodeZone routines
////////////////////////////////////////////////////////////////////////////////
ControlNodeZone::ControlNodeZone(const std::string &name,
                                 const std::string &display_name,
                                 const boost::uuids::uuid &uuid) :
    name_(name), display_name_(display_name), uuid_(uuid) {
}

ControlNodeZone::~ControlNodeZone() {
}

////////////////////////////////////////////////////////////////////////////////
// BgpRouter routines
////////////////////////////////////////////////////////////////////////////////
BgpRouter::BgpRouter(const std::string &name,
                     const std::string &ipv4_address,
                     const uint32_t &port) :
    name_(name), port_(port) {
    boost::system::error_code ec;
    ipv4_address_ = Ip4Address::from_string(ipv4_address, ec);
    if (ec.value() != 0) {
        ipv4_address_ = Ip4Address();
    }
}

void BgpRouter::set_ip_address_port(const std::string &ip_address,
                                    const uint32_t &port) {
    boost::system::error_code ec;
    ipv4_address_ = Ip4Address::from_string(ip_address, ec);
    if (ec.value() != 0) {
        ipv4_address_ = Ip4Address();
    }
    port_ = port;
}

void BgpRouter::set_control_node_zone_name(
        const std::string &control_node_zone_name) {
    control_node_zone_name_ = control_node_zone_name;
}

BgpRouter::~BgpRouter() {
}

////////////////////////////////////////////////////////////////////////////////
// BgpRouterConfig routines
////////////////////////////////////////////////////////////////////////////////
BgpRouterConfig::BgpRouterConfig(Agent *agent) :
    OperIFMapTable(agent) {
}

BgpRouterPtr BgpRouterConfig::GetBgpRouterFromIpAddress(
    const std::string &ip_address) {
    boost::system::error_code ec;
    Ip4Address ipv4_address = Ip4Address::from_string(ip_address, ec);
    if (ec.value() != 0) {
        return BgpRouterPtr();
    }
    BgpRouterPtr entry;
    tbb::mutex::scoped_lock lock(mutex_);
    BgpRouterTree::const_iterator it = bgp_router_tree_.begin();
    for ( ;it != bgp_router_tree_.end(); it++) {
        entry = it->second;
        if (entry->ipv4_address() == ipv4_address)
            return entry;
    }
    return BgpRouterPtr();
}

BgpRouterPtr BgpRouterConfig::GetBgpRouterFromControlNodeZone(
    const std::string &cnz_name) {
    tbb::mutex::scoped_lock lock(mutex_);
    ControlNodeZoneTree::const_iterator cnz_it =
        control_node_zone_tree_.find(cnz_name);
    if (cnz_it != control_node_zone_tree_.end()) {
        ControlNodeZonePtr entry = cnz_it->second;
        int count = 0;
        int index = rand() % entry->bgp_router_tree_.size();
        BgpRouterTree::const_iterator it = entry->bgp_router_tree_.begin();
        for ( ;it != entry->bgp_router_tree_.end(); it++) {
            if (count == index)
                return it->second;
            count++;
        }
    }
    return BgpRouterPtr();
}

uint32_t BgpRouterConfig::GetBgpRouterCount(const std::string &cnz_name) {
    tbb::mutex::scoped_lock lock(mutex_);
    ControlNodeZoneTree::const_iterator cnz_it =
        control_node_zone_tree_.find(cnz_name);
    if (cnz_it != control_node_zone_tree_.end()) {
        ControlNodeZonePtr entry = cnz_it->second;
        return entry->bgp_router_tree_.size();
    }
    return 0;
}

void BgpRouterConfig::DeleteControlNodeZoneConfig(IFMapNode *bgp_router_node,
                                                  BgpRouterPtr bgp_router) {
    std::string cnz_name = bgp_router->control_node_zone_name();
    if (cnz_name.empty())
        return;
    ControlNodeZoneTree::const_iterator it =
        control_node_zone_tree_.find(cnz_name);
    if (it != control_node_zone_tree_.end()) {
        ControlNodeZonePtr entry = it->second;
        entry->bgp_router_tree_.erase(bgp_router->name());
        if (entry->bgp_router_tree_.size() == 0) {
            control_node_zone_tree_.erase(cnz_name);
            entry.reset();
        }
    }
}

void BgpRouterConfig::UpdateControlNodeZoneConfig(IFMapNode *bgp_router_node,
                                                  BgpRouterPtr bgp_router) {
    std::string cnz_node_name;
    IFMapNode *cnz_node = NULL;
    IFMapAgentTable *bgp_router_table =
        static_cast<IFMapAgentTable *>(bgp_router_node->table());
    DBGraph *graph = bgp_router_table->GetGraph();
    DBGraphVertex::adjacency_iterator iter = bgp_router_node->begin(graph);
    while (iter != bgp_router_node->end(graph)) {
        IFMapNode *node = static_cast<IFMapNode *>(iter.operator->());
        if (strcmp(node->table()->Typename(),
                CONTROL_NODE_ZONE_CONFIG_NAME) == 0) {
            cnz_node = node;
            cnz_node_name = cnz_node->name();
            break;
        }
        iter++;
    }

    std::string cnz_name = bgp_router->control_node_zone_name();
    if (strcmp(cnz_name.c_str(), cnz_node_name.c_str()) == 0) {
        return;
    }

    DeleteControlNodeZoneConfig(bgp_router_node, bgp_router);
    bgp_router->set_control_node_zone_name(cnz_node_name);
    if (cnz_node == NULL)
        return;

    ControlNodeZonePtr entry;
    ControlNodeZoneTree::const_iterator it =
        control_node_zone_tree_.find(cnz_node->name());
    if (it != control_node_zone_tree_.end()) {
        entry = it->second;
        entry->bgp_router_tree_.insert(
            std::make_pair(bgp_router->name(), bgp_router));
    } else {
        autogen::ControlNodeZone *cfg =
            static_cast<autogen::ControlNodeZone *>(cnz_node->GetObject());
        autogen::IdPermsType id_perms = cfg->id_perms();
        boost::uuids::uuid uuid;
        CfgUuidSet(id_perms.uuid.uuid_mslong, id_perms.uuid.uuid_lslong, uuid);
        ControlNodeZonePtr cnz(
            new ControlNodeZone(cnz_node_name, cfg->display_name(), uuid));
        entry = cnz;
        entry->bgp_router_tree_.insert(
            std::make_pair(bgp_router->name(), bgp_router));
        control_node_zone_tree_.insert(std::make_pair(cnz_node_name, entry));
    }
}

void BgpRouterConfig::ConfigAddChange(IFMapNode *node) {
    if (node->IsDeleted())
        return;
    autogen::BgpRouter *bgp_router_cfg =
        static_cast<autogen::BgpRouter *>(node->GetObject());
    if (bgp_router_cfg == NULL)
        return;
    autogen::BgpRouterParams params = bgp_router_cfg->parameters();
    if (strcmp(params.router_type.c_str(), BGP_ROUTER_TYPE) != 0)
        return;

    std::string name = node->name();
    BgpRouterPtr entry;
    tbb::mutex::scoped_lock lock(mutex_);
    BgpRouterTree::const_iterator it = bgp_router_tree_.find(name);
    if (it != bgp_router_tree_.end()) {
        entry = it->second;
        entry->set_ip_address_port(params.address, params.port);
    } else {
        BgpRouterPtr bgp_router(
            new BgpRouter(name, params.address, params.port));
        entry = bgp_router;
        bgp_router_tree_.insert(std::make_pair(name, entry));
    }
    UpdateControlNodeZoneConfig(node, entry);

    return;
}

void BgpRouterConfig::ConfigDelete(IFMapNode *node) {
    autogen::BgpRouter *bgp_router_cfg =
        static_cast<autogen::BgpRouter *>(node->GetObject());
    if (bgp_router_cfg == NULL)
        return;
    autogen::BgpRouterParams params = bgp_router_cfg->parameters();
    if (strcmp(params.router_type.c_str(), BGP_ROUTER_TYPE) != 0)
        return;

    std::string name = node->name();
    tbb::mutex::scoped_lock lock(mutex_);
    BgpRouterTree::const_iterator it = bgp_router_tree_.find(name);
    if (it != bgp_router_tree_.end()) {
        BgpRouterPtr entry = it->second;
        DeleteControlNodeZoneConfig(node, entry);
        entry.reset();
        bgp_router_tree_.erase(it->first);
    }
}

void BgpRouterConfig::ConfigManagerEnqueue(IFMapNode *node) {
    agent()->config_manager()->AddBgpRouterConfigNode(node);
}

BgpRouterConfig::~BgpRouterConfig() {
}

void BgpRouterSandeshReq::HandleRequest() const {
    BgpRouterSandeshResp *resp = new BgpRouterSandeshResp();
    resp->set_context(context());
    std::string filter = get_name();
    std::vector<BgpRouterSandeshList> bgp_router_sandesh_list;
    Agent *agent = Agent::GetInstance();
    BgpRouterConfig *bgp_router_cfg = agent->oper_db()->bgp_router_config();
    BgpRouterTree bgp_router_tree = bgp_router_cfg->bgp_router_tree();
    BgpRouterTree::const_iterator it = bgp_router_tree.begin();
    for ( ;it != bgp_router_tree.end(); it++) {
        BgpRouterPtr bgp_router_entry = it->second;
        BgpRouterSandeshList bgp_router_sandesh_entry;
        std::string bgp_router_name = bgp_router_entry->name();
        if (filter.size()) {
            if (bgp_router_name.find(filter) == std::string::npos) {
                continue;
            }
        }
        bgp_router_sandesh_entry.set_name(bgp_router_entry->name());
        std::stringstream ss;
        ss << bgp_router_entry->ipv4_address()
            << ":"<< bgp_router_entry->port();
        bgp_router_sandesh_entry.set_ipv4_address_port(ss.str());
        bgp_router_sandesh_entry.set_control_node_zone(
            bgp_router_entry->control_node_zone_name());
        bgp_router_sandesh_list.push_back(bgp_router_sandesh_entry);
    }
    resp->set_bgp_router_list(bgp_router_sandesh_list);
    resp->Response();
}

void ControlNodeZoneSandeshReq::HandleRequest() const {
    ControlNodeZoneSandeshResp *resp = new ControlNodeZoneSandeshResp();
    resp->set_context(context());
    std::string filter = get_name();
    std::vector<ControlNodeZoneSandeshList> cnz_sandesh_list;
    Agent *agent = Agent::GetInstance();
    BgpRouterConfig *bgp_router_cfg = agent->oper_db()->bgp_router_config();
    ControlNodeZoneTree cnz_tree = bgp_router_cfg->control_node_zone_tree();
    ControlNodeZoneTree::const_iterator it = cnz_tree.begin();
    for ( ;it != cnz_tree.end(); it++) {
        ControlNodeZonePtr cnz_entry = it->second;
        ControlNodeZoneSandeshList cnz_sandesh_entry;
        std::string cnz_name = cnz_entry->name();
        if (filter.size()) {
            if (cnz_name.find(filter) == std::string::npos) {
                continue;
            }
        }
        cnz_sandesh_entry.set_name(cnz_entry->name());
        BgpRouterTree bgp_router_tree = cnz_entry->bgp_router_tree();
        BgpRouterTree::const_iterator it = bgp_router_tree.begin();
        std::vector<BgpRouterSandeshList> bgp_router_list;
        for ( ;it != bgp_router_tree.end(); it++) {
            BgpRouterPtr bgp_router_entry = it->second;
            BgpRouterSandeshList bgp_router_sandesh_entry;
            bgp_router_sandesh_entry.set_name(bgp_router_entry->name());
            std::stringstream ss;
            ss << bgp_router_entry->ipv4_address()
                << ":" << bgp_router_entry->port();
            bgp_router_sandesh_entry.set_ipv4_address_port(ss.str());
            bgp_router_sandesh_entry.set_control_node_zone(
                bgp_router_entry->control_node_zone_name());
            bgp_router_list.push_back(bgp_router_sandesh_entry);
        }
        cnz_sandesh_entry.set_bgp_router_list(bgp_router_list);
        cnz_sandesh_list.push_back(cnz_sandesh_entry);
    }
    resp->set_control_node_zone_list(cnz_sandesh_list);
    resp->Response();
}
