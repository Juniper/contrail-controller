/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include "bgp/bgp_config.h"

#include "base/string_util.h"
#include "base/time_util.h"

const char *BgpConfigManager::kMasterInstance =
        "default-domain:default-project:ip-fabric:__default__";
const int BgpConfigManager::kDefaultPort = 179;
const uint32_t BgpConfigManager::kDefaultAutonomousSystem = 64512;

static int CompareTo(const AuthenticationKey &lhs,
                     const AuthenticationKey &rhs) {
    KEY_COMPARE(lhs.id, rhs.id);
    KEY_COMPARE(lhs.value, rhs.value);
    KEY_COMPARE(lhs.start_time, rhs.start_time);
    return 0;
}

bool AuthenticationKey::operator<(const AuthenticationKey &rhs) const {
    int cmp = CompareTo(*this, rhs);
    return cmp < 0;
}

bool AuthenticationKey::operator==(const AuthenticationKey &rhs) const {
    int cmp = CompareTo(*this, rhs);
    return cmp == 0;
}

AuthenticationData::AuthenticationData() : key_type_(NIL) {
}

AuthenticationData::AuthenticationData(KeyType type,
        const AuthenticationKeyChain &chain) :
    key_type_(type), key_chain_(chain) {
}

bool AuthenticationData::operator<(const AuthenticationData &rhs) const {
    BOOL_KEY_COMPARE(key_type_, rhs.key_type());
    BOOL_KEY_COMPARE(key_chain_, rhs.key_chain());
    return false;
}

bool AuthenticationData::operator==(const AuthenticationData &rhs) const {
    if ((key_chain_.size() != rhs.key_chain().size()) ||
        (key_type_ != rhs.key_type())) {
        return false;
    }

    for (size_t ocnt = 0; ocnt < rhs.key_chain().size(); ++ocnt) {
        bool found = false;
        for (size_t icnt = 0; icnt < key_chain_.size(); ++icnt) {
            if (key_chain_[icnt] == rhs.key_chain()[ocnt]) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

AuthenticationData &AuthenticationData::operator=(
        const AuthenticationData &rhs) {
    set_key_type(rhs.key_type());
    set_key_chain(rhs.key_chain());
    return *this;
}

void AuthenticationData::AddKeyToKeyChain(const AuthenticationKey &key) {
    key_chain_.push_back(key);
}

const AuthenticationKey *AuthenticationData::Find(int key_id) const {
    const AuthenticationKey *item;
    for (size_t i = 0; i < key_chain_.size(); ++i) {
        item = &key_chain_[i];
        if (item->id == key_id) {
            return item;
        }
    }
    return NULL;
}

bool AuthenticationData::Empty() const {
    return key_chain_.empty();
}

void AuthenticationData::Clear() {
    key_chain_.clear();
}

std::string AuthenticationData::KeyTypeToString() const {
    switch (key_type_) {
        case MD5:
            return "MD5";
        default:
            return "NIL";
    }
}

std::string AuthenticationData::KeyTypeToString(KeyType key_type) {
    switch (key_type) {
        case MD5:
            return "MD5";
        default:
            return "NIL";
    }
}

std::vector<std::string> AuthenticationData::KeysToString() const {
    AuthenticationKeyChain::const_iterator iter;
    std::vector<std::string> auth_keys;
    for (iter = key_chain_.begin(); iter != key_chain_.end(); ++iter) {
        AuthenticationKey key = *iter;
        auth_keys.push_back(integerToString(key.id));
    }
    return auth_keys;
}

// NOTE: This prints the actual key too. Use with care.
std::vector<std::string> AuthenticationData::KeysToStringDetail() const {
    AuthenticationKeyChain::const_iterator iter;
    std::vector<std::string> auth_keys;
    for (iter = key_chain_.begin(); iter != key_chain_.end(); ++iter) {
        AuthenticationKey key = *iter;
        auth_keys.push_back(integerToString(key.id) + ":" + key.value);
    }
    return auth_keys;
}

BgpNeighborConfig::BgpNeighborConfig()
        : type_(UNSPECIFIED),
          peer_as_(0),
          identifier_(0),
          port_(BgpConfigManager::kDefaultPort),
          hold_time_(0),
          local_as_(0),
          local_identifier_(0),
          last_change_at_(UTCTimestampUsec()) {
}

void BgpNeighborConfig::CopyValues(const BgpNeighborConfig &rhs) {
    instance_name_ = rhs.instance_name_;
    group_name_ = rhs.group_name_;
    type_ = rhs.type_;
    peer_as_ = rhs.peer_as_;
    identifier_ = rhs.identifier_;
    address_ = rhs.address_;
    port_ = rhs.port_;
    hold_time_ = rhs.hold_time_;
    local_as_ = rhs.local_as_;
    local_identifier_ = rhs.local_identifier_;
    auth_data_ = rhs.auth_data_;
    address_families_ = rhs.address_families_;
}

int BgpNeighborConfig::CompareTo(const BgpNeighborConfig &rhs) const {
    KEY_COMPARE(name_, rhs.name_);
    KEY_COMPARE(uuid_, rhs.uuid_);
    KEY_COMPARE(instance_name_, rhs.instance_name_);
    KEY_COMPARE(type_, rhs.type_);
    KEY_COMPARE(peer_as_, rhs.peer_as_);
    KEY_COMPARE(identifier_, rhs.identifier_);
    KEY_COMPARE(address_, rhs.address_);
    KEY_COMPARE(port_, rhs.port_);
    KEY_COMPARE(hold_time_, rhs.hold_time_);
    KEY_COMPARE(local_as_, rhs.local_as_);
    KEY_COMPARE(local_identifier_, rhs.local_identifier_);
    KEY_COMPARE(auth_data_, rhs.auth_data_);
    KEY_COMPARE(address_families_, rhs.address_families_);
    return 0;
}

std::string BgpNeighborConfig::AuthKeyTypeToString() const {
    return auth_data_.KeyTypeToString();
}

// Return the key's id concatenated with its type.
std::vector<std::string> BgpNeighborConfig::AuthKeysToString() const {
    return auth_data_.KeysToString();
}

BgpProtocolConfig::BgpProtocolConfig(const std::string &instance_name)
        : instance_name_(instance_name),
          autonomous_system_(0), 
          local_autonomous_system_(0),
          identifier_(0), 
          port_(0),
          hold_time_(-1),
          last_change_at_(UTCTimestampUsec()) {
}

int BgpProtocolConfig::CompareTo(const BgpProtocolConfig &rhs) const {
    KEY_COMPARE(instance_name_, rhs.instance_name_);
    KEY_COMPARE(autonomous_system_, rhs.autonomous_system_);
    KEY_COMPARE(identifier_, rhs.identifier_);
    KEY_COMPARE(port_, rhs.port_);
    KEY_COMPARE(hold_time_, rhs.hold_time_);
    return 0;
}

BgpInstanceConfig::BgpInstanceConfig(const std::string &name)
        : name_(name),
          virtual_network_index_(0), 
          virtual_network_allow_transit_(false),
          vxlan_id_(0),
          last_change_at_(UTCTimestampUsec()) {
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
    config->set_last_change_at(UTCTimestampUsec());
    if (obs_.instance) {
        (obs_.instance)(config, event);
    }
}

template<>
void BgpConfigManager::Notify<BgpProtocolConfig>(
        const BgpProtocolConfig *config, EventType event) {
    config->set_last_change_at(UTCTimestampUsec());
    if (obs_.protocol) {
        (obs_.protocol)(config, event);
    }
}

template<>
void BgpConfigManager::Notify<BgpNeighborConfig>(
        const BgpNeighborConfig *config, EventType event) {
    config->set_last_change_at(UTCTimestampUsec());
    if (obs_.neighbor) {
        (obs_.neighbor)(config, event);
    }
}
