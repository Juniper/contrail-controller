/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include "bgp/bgp_config.h"

const char *BgpConfigManager::kMasterInstance =
        "default-domain:default-project:ip-fabric:__default__";
const int BgpConfigManager::kDefaultPort = 179;
const uint32_t BgpConfigManager::kDefaultAutonomousSystem = 64512;

static int CompareTo(const AuthenticationKey &lhs,
                     const AuthenticationKey &rhs) {
    KEY_COMPARE(lhs.id, rhs.id);
    KEY_COMPARE(lhs.type, rhs.type);
    KEY_COMPARE(lhs.value, rhs.value);
    KEY_COMPARE(lhs.start_time, rhs.start_time);
    return 0;
}

bool AuthenticationKey::operator<(const AuthenticationKey &rhs) const {
    int cmp = CompareTo(*this, rhs);
    return cmp < 0;
}

BgpNeighborConfig::BgpNeighborConfig()
        : peer_as_(0),
          identifier_(0),
          port_(BgpConfigManager::kDefaultPort),
          hold_time_(0),
          local_as_(0),
          local_identifier_(0) {
}

int BgpNeighborConfig::CompareTo(const BgpNeighborConfig &rhs) const {
    KEY_COMPARE(name_, rhs.name_);
    KEY_COMPARE(uuid_, rhs.uuid_);
    KEY_COMPARE(instance_name_, rhs.instance_name_);
    KEY_COMPARE(peer_as_, rhs.peer_as_);
    KEY_COMPARE(identifier_, rhs.identifier_);
    KEY_COMPARE(address_, rhs.address_);
    KEY_COMPARE(port_, rhs.port_);
    KEY_COMPARE(hold_time_, rhs.hold_time_);
    KEY_COMPARE(local_as_, rhs.local_as_);
    KEY_COMPARE(local_identifier_, rhs.local_identifier_);
    KEY_COMPARE(keychain_, rhs.keychain_);
    KEY_COMPARE(address_families_, rhs.address_families_);
    return 0;
}

BgpProtocolConfig::BgpProtocolConfig(const std::string &instance_name)
        : instance_name_(instance_name),
          autonomous_system_(0), 
          local_autonomous_system_(0),
          identifier_(0), 
          hold_time_(-1) {
}

BgpInstanceConfig::BgpInstanceConfig(const std::string &name)
        : name_(name),
          virtual_network_index_(0), 
          virtual_network_allow_transit_(false),
          vxlan_id_(0) {
}

BgpInstanceConfig::~BgpInstanceConfig() {
}

void BgpInstanceConfig::Clear() {
    import_list_.clear();
    export_list_.clear();
    virtual_network_.clear();
    virtual_network_index_ = 0;
    virtual_network_allow_transit_ = false;
    vxlan_id_ = 0;
    static_routes_.clear();
    service_chain_list_.clear();
}

BgpConfigManager::BgpConfigManager(BgpServer *server)
        : server_(server) {
}

BgpConfigManager::~BgpConfigManager() {
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
