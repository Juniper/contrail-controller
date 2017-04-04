/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include "bgp/bgp_config.h"

#include <boost/foreach.hpp>

#include <sstream>

#include "base/string_util.h"
#include "base/time_util.h"

using std::copy;
using std::endl;
using std::ostringstream;
using std::ostream_iterator;
using std::sort;
using std::string;
using std::swap;
using std::vector;

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

string AuthenticationData::KeyTypeToString() const {
    switch (key_type_) {
        case MD5:
            return "MD5";
        default:
            return "NIL";
    }
}

string AuthenticationData::KeyTypeToString(KeyType key_type) {
    switch (key_type) {
        case MD5:
            return "MD5";
        default:
            return "NIL";
    }
}

vector<string> AuthenticationData::KeysToString() const {
    AuthenticationKeyChain::const_iterator iter;
    vector<string> auth_keys;
    for (iter = key_chain_.begin(); iter != key_chain_.end(); ++iter) {
        AuthenticationKey key = *iter;
        auth_keys.push_back(integerToString(key.id));
    }
    return auth_keys;
}

// NOTE: This prints the actual key too. Use with care.
vector<string> AuthenticationData::KeysToStringDetail() const {
    AuthenticationKeyChain::const_iterator iter;
    vector<string> auth_keys;
    for (iter = key_chain_.begin(); iter != key_chain_.end(); ++iter) {
        AuthenticationKey key = *iter;
        auth_keys.push_back(integerToString(key.id) + ":" + key.value);
    }
    return auth_keys;
}

BgpNeighborConfig::BgpNeighborConfig()
        : type_(UNSPECIFIED),
          admin_down_(false),
          passive_(false),
          as_override_(false),
          peer_as_(0),
          identifier_(0),
          port_(BgpConfigManager::kDefaultPort),
          source_port_(0),
          hold_time_(0),
          loop_count_(0),
          local_as_(0),
          local_identifier_(0),
          last_change_at_(UTCTimestampUsec()) {
}

void BgpNeighborConfig::CopyValues(const BgpNeighborConfig &rhs) {
    instance_name_ = rhs.instance_name_;
    group_name_ = rhs.group_name_;
    type_ = rhs.type_;
    admin_down_ = rhs.admin_down_;
    passive_ = rhs.passive_;
    as_override_ = rhs.as_override_;
    private_as_action_  = rhs.private_as_action_;
    peer_as_ = rhs.peer_as_;
    identifier_ = rhs.identifier_;
    address_ = rhs.address_;
    inet_gateway_address_ = rhs.inet_gateway_address_;
    inet6_gateway_address_ = rhs.inet6_gateway_address_;
    port_ = rhs.port_;
    source_port_ = rhs.source_port_;
    hold_time_ = rhs.hold_time_;
    loop_count_ = rhs.loop_count_;
    local_as_ = rhs.local_as_;
    local_identifier_ = rhs.local_identifier_;
    auth_data_ = rhs.auth_data_;
    family_attributes_list_ = rhs.family_attributes_list_;
}

int BgpNeighborConfig::CompareTo(const BgpNeighborConfig &rhs) const {
    KEY_COMPARE(name_, rhs.name_);
    KEY_COMPARE(uuid_, rhs.uuid_);
    KEY_COMPARE(instance_name_, rhs.instance_name_);
    KEY_COMPARE(type_, rhs.type_);
    KEY_COMPARE(admin_down_, rhs.admin_down_);
    KEY_COMPARE(passive_, rhs.passive_);
    KEY_COMPARE(as_override_, rhs.as_override_);
    KEY_COMPARE(private_as_action_, rhs.private_as_action_);
    KEY_COMPARE(peer_as_, rhs.peer_as_);
    KEY_COMPARE(identifier_, rhs.identifier_);
    KEY_COMPARE(address_, rhs.address_);
    KEY_COMPARE(inet_gateway_address_, rhs.inet_gateway_address_);
    KEY_COMPARE(inet6_gateway_address_, rhs.inet6_gateway_address_);
    KEY_COMPARE(port_, rhs.port_);
    KEY_COMPARE(source_port_, rhs.source_port_);
    KEY_COMPARE(hold_time_, rhs.hold_time_);
    KEY_COMPARE(loop_count_, rhs.loop_count_);
    KEY_COMPARE(local_as_, rhs.local_as_);
    KEY_COMPARE(local_identifier_, rhs.local_identifier_);
    KEY_COMPARE(auth_data_, rhs.auth_data_);
    return STLSortedCompare(
        family_attributes_list_.begin(), family_attributes_list_.end(),
        rhs.family_attributes_list_.begin(), rhs.family_attributes_list_.end(),
        BgpFamilyAttributesConfigCompare());
}

const IpAddress &BgpNeighborConfig::gateway_address(
    Address::Family family) const {
    assert(family == Address::INET || family == Address::INET6);
    if (family == Address::INET) {
        return inet_gateway_address_;
    } else {
        return inet6_gateway_address_;
    }
}

void BgpNeighborConfig::set_gateway_address(Address::Family family,
    const IpAddress &address) {
    assert(family == Address::INET || family == Address::INET6);
    if (family == Address::INET) {
        inet_gateway_address_ = address;
    } else {
        inet6_gateway_address_ = address;
    }
}

BgpNeighborConfig::AddressFamilyList
BgpNeighborConfig::GetAddressFamilies() const {
    BgpNeighborConfig::AddressFamilyList family_list;
    BOOST_FOREACH(const BgpFamilyAttributesConfig family_config,
        family_attributes_list_) {
        family_list.push_back(family_config.family);
    }
    sort(family_list.begin(), family_list.end());
    return family_list;
}

string BgpNeighborConfig::AuthKeyTypeToString() const {
    return auth_data_.KeyTypeToString();
}

// Return the key's id concatenated with its type.
vector<string> BgpNeighborConfig::AuthKeysToString() const {
    return auth_data_.KeysToString();
}


bool StaticRouteConfig::operator<(const StaticRouteConfig &rhs) const {
    BOOL_KEY_COMPARE(address, rhs.address);
    BOOL_KEY_COMPARE(prefix_length, rhs.prefix_length);
    return false;
}

BgpProtocolConfig::BgpProtocolConfig(const string &instance_name)
    : instance_name_(instance_name),
      admin_down_(false),
      autonomous_system_(0),
      local_autonomous_system_(0),
      identifier_(0),
      port_(0),
      hold_time_(0),
      last_change_at_(UTCTimestampUsec()) {
}

int BgpProtocolConfig::CompareTo(const BgpProtocolConfig &rhs) const {
    KEY_COMPARE(instance_name_, rhs.instance_name_);
    KEY_COMPARE(admin_down_, rhs.admin_down_);
    KEY_COMPARE(autonomous_system_, rhs.autonomous_system_);
    KEY_COMPARE(identifier_, rhs.identifier_);
    KEY_COMPARE(port_, rhs.port_);
    KEY_COMPARE(hold_time_, rhs.hold_time_);
    return 0;
}

BgpInstanceConfig::BgpInstanceConfig(const string &name)
    : name_(name),
      has_pnf_(false),
      virtual_network_index_(0),
      virtual_network_allow_transit_(false),
      virtual_network_pbb_evpn_enable_(false),
      vxlan_id_(0),
      last_change_at_(UTCTimestampUsec()) {
}

BgpInstanceConfig::~BgpInstanceConfig() {
}

void BgpInstanceConfig::Clear() {
    import_list_.clear();
    export_list_.clear();
    has_pnf_ = false;
    virtual_network_.clear();
    virtual_network_index_ = 0;
    virtual_network_allow_transit_ = false;
    virtual_network_pbb_evpn_enable_ = false;
    vxlan_id_ = 0;
    inet_static_routes_.clear();
    inet6_static_routes_.clear();
    service_chain_list_.clear();
}

const BgpInstanceConfig::StaticRouteList &BgpInstanceConfig::static_routes(
    Address::Family family) const {
    assert(family == Address::INET || family == Address::INET6);
    if (family == Address::INET) {
        return inet_static_routes_;
    } else {
        return inet6_static_routes_;
    }
}

void BgpInstanceConfig::swap_static_routes(Address::Family family,
    StaticRouteList *list) {
    assert(family == Address::INET || family == Address::INET6);
    if (family == Address::INET) {
        swap(inet_static_routes_, *list);
    } else {
        swap(inet6_static_routes_, *list);
    }
}

const ServiceChainConfig *BgpInstanceConfig::service_chain_info(
    Address::Family family) const {
    for (ServiceChainList::const_iterator it = service_chain_list_.begin();
         it != service_chain_list_.end(); ++it) {
        if (it->family == family)
            return it.operator->();
    }
    return NULL;
}

const BgpInstanceConfig::AggregateRouteList &
BgpInstanceConfig::aggregate_routes(Address::Family family) const {
    assert(family == Address::INET || family == Address::INET6);
    if (family == Address::INET) {
        return inet_aggregate_routes_;
    } else {
        return inet6_aggregate_routes_;
    }
}

void BgpInstanceConfig::swap_aggregate_routes(Address::Family family,
                                              AggregateRouteList *list) {
    assert(family == Address::INET || family == Address::INET6);
    if (family == Address::INET) {
        swap(inet_aggregate_routes_, *list);
    } else {
        swap(inet6_aggregate_routes_, *list);
    }
}

BgpRoutingPolicyConfig::BgpRoutingPolicyConfig(const string &name)
        : name_(name),
          last_change_at_(UTCTimestampUsec()) {
}

BgpRoutingPolicyConfig::~BgpRoutingPolicyConfig() {
}

void BgpRoutingPolicyConfig::Clear() {
    terms_.clear();
}

string RoutingPolicyMatchConfig::ToString() const {
    ostringstream oss;
    oss << "from {" << endl;
    if (!protocols_match.empty()) {
        oss << "    protocol [ ";
        BOOST_FOREACH(const string &protocol, protocols_match) {
            oss << protocol << ",";
        }
        oss.seekp(-1, oss.cur);
        oss << " ]";
    }
    if (!community_match.empty()) {
        oss << "    community " << community_match << endl;
    }
    if (!prefixes_to_match.empty()) {
        BOOST_FOREACH(const PrefixMatchConfig &match, prefixes_to_match) {
            oss << "    prefix " << match.prefix_to_match << " "
                << match.prefix_match_type << endl;
        }
    }
    oss << "}" << endl;
    return oss.str();
}

static void PutCommunityList(ostringstream *oss, const CommunityList &list) {
    copy(list.begin(), list.end(), ostream_iterator<string>(*oss, ","));
    oss->seekp(-1, oss->cur);
}

string RoutingPolicyActionConfig::ToString() const {
    ostringstream oss;
    oss << "then {" << endl;
    if (!update.community_set.empty()) {
        oss << "    community set [ ";
        PutCommunityList(&oss, update.community_set);
        oss << " ]" << endl;
    }
    if (!update.community_add.empty()) {
        oss << "    community add [ ";
        PutCommunityList(&oss, update.community_add);
        oss << " ]" << endl;
    }
    if (!update.community_remove.empty()) {
        oss << "    community remove [ ";
        PutCommunityList(&oss, update.community_remove);
        oss << " ]" << endl;
    }
    if (update.local_pref) {
        oss << "    local-preference " << update.local_pref << endl;
    }
    if (update.med) {
        oss << "    med " << update.med << endl;
    }

    if (action == ACCEPT) {
        oss << "    accept" << endl;
    } else if (action == REJECT) {
        oss << "    reject" << endl;
    } else if (action == NEXT_TERM) {
        oss << "    next-term" << endl;
    }
    oss << "}" << endl;
    return oss.str();
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
    BOOST_FOREACH(Observers obs, obs_) {
        if (obs.instance) {
            (obs.instance)(config, event);
        }
    }
}

template<>
void BgpConfigManager::Notify<BgpRoutingPolicyConfig>(
        const BgpRoutingPolicyConfig *config, EventType event) {
    config->set_last_change_at(UTCTimestampUsec());
    BOOST_FOREACH(Observers obs, obs_) {
        if (obs.policy) {
            (obs.policy)(config, event);
        }
    }
}

template<>
void BgpConfigManager::Notify<BgpProtocolConfig>(
        const BgpProtocolConfig *config, EventType event) {
    config->set_last_change_at(UTCTimestampUsec());
    BOOST_FOREACH(Observers obs, obs_) {
        if (obs.protocol) {
            (obs.protocol)(config, event);
        }
    }
}

template<>
void BgpConfigManager::Notify<BgpNeighborConfig>(
        const BgpNeighborConfig *config, EventType event) {
    config->set_last_change_at(UTCTimestampUsec());
    BOOST_FOREACH(Observers obs, obs_) {
        if (obs.neighbor) {
            (obs.neighbor)(config, event);
        }
    }
}

template<>
void BgpConfigManager::Notify<BgpGlobalSystemConfig>(
        const BgpGlobalSystemConfig *config, EventType event) {
    config->set_last_change_at(UTCTimestampUsec());
    BOOST_FOREACH(Observers obs, obs_) {
        if (obs.system) {
            (obs.system)(config, event);
        }
    }
}

template<>
void BgpConfigManager::Notify<BgpGlobalQosConfig>(
        const BgpGlobalQosConfig *config, EventType event) {
    config->set_last_change_at(UTCTimestampUsec());
    BOOST_FOREACH(Observers obs, obs_) {
        if (obs.qos) {
            (obs.qos)(config, event);
        }
    }
}
