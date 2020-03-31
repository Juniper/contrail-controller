/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#include "bgp/bgp_config_yaml.h"

#include <boost/assign/list_of.hpp>
#include <boost/bind.hpp>
#include <yaml-cpp/yaml.h>
#include "base/logging.h"
#include "base/map_util.h"

using namespace std;
using boost::assign::map_list_of;

// Container of per-instance data.
class YamlInstanceData {
public:
    typedef BgpConfigManager::NeighborMap NeighborMap;

    explicit YamlInstanceData(const std::string &name) : protocol_(name) {
    }
    ~YamlInstanceData() {
        STLDeleteElements(&neighbors_);
    }

    // Transfers the memory ownership of the neighbor config to this object.
    bool AddNeighbor(BgpNeighborConfig *neighbor) {
        pair<NeighborMap::iterator, bool> result =
                neighbors_.insert(make_pair(neighbor->name(), neighbor));
        bool success = result.second;
        if (!success) {
            delete neighbor;
        }
        return success;
    }

    const NeighborMap &neighbors() const {
        return neighbors_;
    }

    NeighborMap *neighbors() {
        return &neighbors_;
    }

    BgpProtocolConfig *GetProtocol() { return &protocol_; }

private:
    BgpProtocolConfig protocol_;
    NeighborMap neighbors_;
};

// Configuration data
class BgpYamlConfigManager::Configuration {
public:
    typedef map<string, YamlInstanceData *> InstanceDataMap;

    Configuration() {
        DefaultConfig();
    }

    virtual ~Configuration() {
        STLDeleteElements(&routing_policies_);
        STLDeleteElements(&instances_);
        STLDeleteElements(&config_map_);
    }

    bool AddNeighbor(const string &instance, BgpNeighborConfig *neighbor) {
        YamlInstanceData *config = LocateInstanceData(instance);
        return config->AddNeighbor(neighbor);
    }

    BgpProtocolConfig *GetProtocolConfig() {
        return GetProtocolConfig(BgpConfigManager::kMasterInstance);
    }

    BgpProtocolConfig *GetProtocolConfig(const std::string &instance_name) {
        YamlInstanceData *config = LocateInstanceData(instance_name);
        return config->GetProtocol();
    }

    const NeighborMap &GetNeighborMap() {
        const YamlInstanceData *config = LocateInstanceData(
            BgpConfigManager::kMasterInstance);
        return config->neighbors();
    }

    NeighborMap *NeighborMapMutable() {
        YamlInstanceData *config = LocateInstanceData(
            BgpConfigManager::kMasterInstance);
        return config->neighbors();
    }

    const InstanceMap &GetInstanceMap() const {
        return instances_;
    }
    InstanceMap *InstanceMapMutable() {
        return &instances_;
    }

    const RoutingPolicyMap &GetRoutingPolicyMap() const {
        return routing_policies_;
    }
    RoutingPolicyMap *RoutingPolicyMapMutable() {
        return &routing_policies_;
    }

private:
    BgpInstanceConfig *LocateInstance(const std::string &name) {
        InstanceMap::iterator loc = instances_.find(name);
        if (loc != instances_.end()) {
            return loc->second;
        }
        pair<InstanceMap::iterator, bool> result =
                instances_.insert(
                    make_pair(name, new BgpInstanceConfig(name)));
        return result.first->second;
    }

    BgpRoutingPolicyConfig *LocateRoutingPolicy(const std::string &name) {
        RoutingPolicyMap::iterator loc = routing_policies_.find(name);
        if (loc != routing_policies_.end()) {
            return loc->second;
        }
        pair<RoutingPolicyMap::iterator, bool> result =
                routing_policies_.insert(
                    make_pair(name, new BgpRoutingPolicyConfig(name)));
        return result.first->second;
    }

    YamlInstanceData *LocateInstanceData(const std::string &name) {
        InstanceDataMap::iterator loc = config_map_.find(name);
        if (loc != config_map_.end()) {
            return loc->second;
        }
        pair<InstanceDataMap::iterator, bool> result =
                config_map_.insert(
                    make_pair(name, new YamlInstanceData(name)));
        return result.first->second;
    }

    void DefaultConfig() {
        BgpProtocolConfig *proto = GetProtocolConfig();
        proto->set_autonomous_system(
            BgpConfigManager::kDefaultAutonomousSystem);
        proto->set_port(BgpConfigManager::kDefaultPort);
        LocateInstance(BgpConfigManager::kMasterInstance);
    }

    BgpConfigManager::InstanceMap instances_;
    BgpConfigManager::RoutingPolicyMap routing_policies_;
    InstanceDataMap config_map_;
};

typedef map<string, BgpNeighborConfig::Type> PeerTypeMap;
static PeerTypeMap peertype_map = {{"ibgp", BgpNeighborConfig::IBGP},
                                   {"ebgp", BgpNeighborConfig::EBGP}};

static BgpNeighborConfig::Type PeerTypeGetValue(const string &value) {
    PeerTypeMap::const_iterator loc = peertype_map.find(value);
    if (loc != peertype_map.end()) {
        return loc->second;
    }
    return BgpNeighborConfig::UNSPECIFIED;
}

BgpYamlConfigManager::BgpYamlConfigManager(BgpServer *server)
        : BgpConfigManager(server),
          data_(new Configuration()) {
}

BgpYamlConfigManager::~BgpYamlConfigManager() {
}

void BgpYamlConfigManager::Terminate() {
}

const string &BgpYamlConfigManager::localname() const {
    static string localname;
    return localname;
}

BgpConfigManager::InstanceMapRange
BgpYamlConfigManager::InstanceMapItems(const string &start_name) const {
    const BgpConfigManager::InstanceMap &map = data_->GetInstanceMap();
    return make_pair(map.lower_bound(start_name), map.end());
}

BgpConfigManager::RoutingPolicyMapRange
BgpYamlConfigManager::RoutingPolicyMapItems(
                         const std::string &policy_name) const {
    const BgpConfigManager::RoutingPolicyMap map = data_->GetRoutingPolicyMap();
    return make_pair(map.begin(), map.end());
}

BgpConfigManager::NeighborMapRange BgpYamlConfigManager::NeighborMapItems(
    const std::string &instance_name) const {
    const BgpConfigManager::NeighborMap map = data_->GetNeighborMap();
    return make_pair(map.begin(), map.end());
}

int BgpYamlConfigManager::NeighborCount(
    const std::string &instance_name) const {
    const BgpConfigManager::NeighborMap map = data_->GetNeighborMap();
    return map.size();
}

const BgpInstanceConfig *BgpYamlConfigManager::FindInstance(
    const std::string &name) const {
    return NULL;
}

const BgpProtocolConfig *BgpYamlConfigManager::GetProtocolConfig(
    const std::string &instance_name) const {
    return data_->GetProtocolConfig(instance_name);
}

const BgpNeighborConfig *BgpYamlConfigManager::FindNeighbor(
    const std::string &instance_name, const std::string &name) const {
    return NULL;
}

static bool ParseTimers(const YAML::Node &timers, uint32_t *hold_time,
                        string *error_msg) {
    if (timers["hold-time"]) {
        uint32_t value;
        try {
            value = timers["hold-time"].as<uint32_t>();
        } catch (...) {
            *error_msg = "Invalid hold-time value: not an integer";
            return false;
        }
        if (value < 0 || value > BgpYamlConfigManager::kMaxHoldTime) {
            ostringstream msg;
            msg << "Invalid hold-time value (out of range): " << value;
            *error_msg = msg.str();
            return false;
        }
        *hold_time = value;
    } else {
        *hold_time = 0;
    }
    return true;
}

/*
 * Computes the parsed identifier value (in network order).
 */
static bool ParseIdentifier(const YAML::Node &node, uint32_t *valuep,
                            string *error_msg) {
    string value;
    try {
        value = node.as<string>();
    } catch (...) {
        *error_msg = "Invalid identifier value: not a string";
        return false;
    }
    boost::system::error_code ec;
    Ip4Address address = Ip4Address::from_string(value, ec);
    if (ec) {
        char *endp;
        long int id = strtol(value.c_str(), &endp, 10);
        size_t strlen = endp - value.c_str();
        if (id == 0 || strlen != value.length()) {
            *error_msg = "Invalid identifier value: "
                    "not an IP address or integer";
            return false;
        }
        *valuep = htonl(id);
    } else {
        *valuep = htonl(address.to_ulong());
    }
    return true;
}

static bool ParseBgpGroupNeighborCommon(
    BgpNeighborConfig *neighbor, const YAML::Node &node, string *error_msg) {
    if (node["peer-as"]) {
        int value;
        try {
            value = node["peer-as"].as<int>();
        } catch (...) {
            *error_msg = "Invalid peer-as value: not an integer";
            return false;
        }
        if (value < 0 || value > USHRT_MAX) {
            ostringstream msg;
            msg << "Invalid peer-as value (out of range): " << value;
            *error_msg = msg.str();
            return false;
        }
        neighbor->set_peer_as(value);
    }

    if (node["port"]) {
        int value;
        try {
            value = node["port"].as<int>();
        } catch (...) {
            *error_msg = "Invalid port value: not an integer";
            return false;
        }
        if (value < 0 || value > USHRT_MAX) {
            ostringstream msg;
            msg << "Invalid port value (out of range): " << value;
            *error_msg = msg.str();
            return false;
        }
        neighbor->set_port(value);
    }

    if (node["peer-type"]) {
        string value;
        try {
            value = node["peer-type"].as<string>();
        } catch (...) {
            *error_msg = "Invalid peer-type value: not a string";
            return false;
        }
        BgpNeighborConfig::Type type = PeerTypeGetValue(value);
        if (type == BgpNeighborConfig::UNSPECIFIED) {
            ostringstream msg;
            msg << "Invalid peer-type value: " << value;
            *error_msg = msg.str();
            return false;
        }
        neighbor->set_peer_type(type);
    }

    if (node["identifier"]) {
        uint32_t value;
        if (!ParseIdentifier(node["identifier"], &value, error_msg)) {
            return false;
        }
        neighbor->set_peer_identifier(value);
    }

    if (node["local-identifier"]) {
        uint32_t value;
        if (!ParseIdentifier(node["local-identifier"], &value, error_msg)) {
            return false;
        }
        neighbor->set_local_identifier(value);
    }

    if (node["local-address"]) {
        string value;
        try {
            value = node["local-address"].as<string>();
        } catch (...) {
            *error_msg = "Invalid local-address value: not a string";
            return false;
        }
        boost::system::error_code ec;
        IpAddress address = IpAddress::from_string(value, ec);
        if (ec) {
            ostringstream ss;
            ss << "Invalid local-address value: " << value;
            *error_msg = ss.str();
            return false;
        }
        LOG(DEBUG, "local-address " << address.to_string() << " not used");
        // TODO: store local address and select bind address for peer.
    }

    if (node["timers"]) {
        uint32_t hold_time;
        if (!ParseTimers(node["timers"], &hold_time, error_msg)) {
            return false;
        }
        if (hold_time) {
            neighbor->set_hold_time(hold_time);
        }
    }

    if (node["address-families"]) {
        YAML::Node families = node["address-families"];
        BgpNeighborConfig::FamilyAttributesList list;
        for (YAML::const_iterator iter = families.begin();
             iter != families.end(); ++iter) {
            string family;
            try {
                family = iter->as<string>();
            } catch (...) {
                *error_msg = "Invalid address family value: not a string";
                return false;
            }
            if (Address::FamilyFromString(family) == Address::UNSPEC) {
                *error_msg = "Invalid address family value: " + family;
                return false;
            }
            BgpFamilyAttributesConfig family_config(family);
            list.push_back(family_config);
        }
        neighbor->set_family_attributes_list(list);
    }
    return true;
}

static bool ParseBgpNeighbor(
    BgpNeighborConfig *neighbor, const YAML::Node &node, string *error_msg) {
    return ParseBgpGroupNeighborCommon(neighbor, node, error_msg);
}

static bool ParseBgpNeighbors(BgpYamlConfigManager::Configuration *data,
                              const BgpNeighborConfig *tmpl,
                              const YAML::Node &node, string *error_msg) {
    if (!node.IsMap()) {
        *error_msg = "Expected mapping under bgp:neighbors";
        return false;
    }
    for (YAML::const_iterator iter = node.begin();
         iter != node.end(); ++iter) {
        string address;
        try {
            address = iter->first.as<string>();
        } catch (...) {
            *error_msg = "Invalid neighbor key: not a string";
            return false;
        }
        boost::system::error_code ec;
        IpAddress ipaddress = IpAddress::from_string(address, ec);
        if (ec) {
            *error_msg = "Invalid address: "  + address;
            return false;
        }
        LOG(DEBUG, "neighbor: " << address);
        auto_ptr<BgpNeighborConfig> neighbor(new BgpNeighborConfig());
        neighbor->CopyValues(*tmpl);
        neighbor->set_name(address);
        neighbor->set_peer_address(ipaddress);
        if (!ParseBgpNeighbor(neighbor.get(), iter->second, error_msg)) {
            return false;
        }
        if (!data->AddNeighbor(BgpConfigManager::kMasterInstance,
                               neighbor.release())) {
            *error_msg = "Duplicate neighbor " + address;
            return false;
        }
    }

    return true;
}

static bool ParseBgpGroup(BgpYamlConfigManager::Configuration *data,
                          const BgpNeighborConfig *global,
                          const string &group_name,
                          const YAML::Node &node, string *error_msg) {
    BgpNeighborConfig tmpl;
    tmpl.CopyValues(*global);
    tmpl.set_group_name(group_name);

    if (!ParseBgpGroupNeighborCommon(&tmpl, node, error_msg)) {
        return false;
    }

    YAML::Node neighbors = node["neighbors"];
    if (neighbors && !ParseBgpNeighbors(data, &tmpl, neighbors, error_msg)) {
        return false;
    }
    return true;
}

static bool ParseBgpGroups(BgpYamlConfigManager::Configuration *data,
                           const BgpNeighborConfig *global,
                           const YAML::Node &node, string *error_msg) {
    if (!node.IsMap()) {
        *error_msg = "Expected mapping under bgp:peer-groups";
        return false;
    }

    for (YAML::const_iterator iter = node.begin();
         iter != node.end(); ++iter) {
        string group_name;
        try {
            group_name = iter->first.as<string>();
        } catch (...) {
            *error_msg = "Invalid group key: not a string";
            return false;
        }
        if (!ParseBgpGroup(data, global, group_name, iter->second,
                           error_msg)) {
            return false;
        }
    }
    return true;
}

static bool ParseBgp(BgpYamlConfigManager::Configuration *data,
                     const YAML::Node &node, string *error_msg) {
    BgpNeighborConfig global;
    BgpProtocolConfig *config = data->GetProtocolConfig();

    if (node["autonomous-system"]) {
        int value;
        try {
            value = node["autonomous-system"].as<int>();
        } catch (...) {
            *error_msg = "Invalid autonomous-system number: not an integer";
            return false;
        }
        config->set_autonomous_system(value);
    }

    if (node["port"]) {
        int value;
        try {
            value = node["port"].as<int>();
        } catch (...) {
            *error_msg = "Invalid port number: not an integer";
            return false;
        }
        if (value < 0 || value > USHRT_MAX) {
            ostringstream msg;
            msg << "Invalid port value (out of range): " << value;
            *error_msg = msg.str();
            return false;
        }
        config->set_port(value);
    }

    if (node["identifier"]) {
        uint32_t value;
        if (!ParseIdentifier(node["identifier"], &value, error_msg)) {
            return false;
        }
        config->set_identifier(value);
    }

    YAML::Node timers = node["timers"];
    if (timers) {
        int hold_time;
        if (!ParseTimers(node["timers"], &hold_time, error_msg)) {
            return false;
        }
        if (hold_time) {
            config->set_hold_time(hold_time);
        }
    }

    YAML::Node groups = node["peer-groups"];
    if (groups && !ParseBgpGroups(data, &global, groups, error_msg)) {
        return false;
    }

    YAML::Node neighbors = node["neighbors"];
    if (neighbors && !ParseBgpNeighbors(data, &global, neighbors, error_msg)) {
        return false;
    }

    return true;
}

bool BgpYamlConfigManager::Resolve(Configuration *candidate,
                                   string *error_msg) {
    BgpProtocolConfig *config = candidate->GetProtocolConfig();
    const NeighborMap &neighbors = candidate->GetNeighborMap();
    for (NeighborMap::const_iterator iter = neighbors.begin();
         iter != neighbors.end(); ++iter) {
        BgpNeighborConfig *neighbor = iter->second;
        if (neighbor->peer_as() == 0) {
            if (neighbor->peer_type() == BgpNeighborConfig::IBGP) {
                neighbor->set_peer_as(config->autonomous_system());
            } else {
                ostringstream msg;
                msg << "Neighbor " << neighbor->name()
                    << " autonomous-system must be set";
                *error_msg = msg.str();
                return false;
            }
        } else {
            if (neighbor->peer_type() == BgpNeighborConfig::IBGP) {
                if (neighbor->peer_as() != config->autonomous_system()) {
                    ostringstream msg;
                    msg << "Neighbor " << neighbor->name()
                        << ": autonomous-system mismatch ("
                        << neighbor->peer_as()
                        << ") for IBGP peer";
                    *error_msg = msg.str();
                    return false;
                }
            } else if (neighbor->peer_type() == BgpNeighborConfig::EBGP) {
                if (neighbor->peer_as() == config->autonomous_system()) {
                    ostringstream msg;
                    msg << "Neighbor " << neighbor->name()
                        << ": EBGP peer configured with local "
                        << "autonomous-system";
                    *error_msg = msg.str();
                    return false;
                }
            } else {
                if (neighbor->peer_as() == config->autonomous_system()) {
                    neighbor->set_peer_type(BgpNeighborConfig::IBGP);
                } else {
                    neighbor->set_peer_type(BgpNeighborConfig::EBGP);
                }
            }
        }
    }
    return true;
}

void BgpYamlConfigManager::UpdateProtocol(
    Configuration *current, Configuration *next) {
    BgpProtocolConfig *curr_proto = current->GetProtocolConfig();
    BgpProtocolConfig *next_proto = next->GetProtocolConfig();
    if (curr_proto->CompareTo(*next_proto) == 0) {
        Notify(next_proto, CFG_CHANGE);
    }
}

void BgpYamlConfigManager::AddInstance(InstanceMap::iterator iter) {
    Notify(iter->second, CFG_ADD);
}
void BgpYamlConfigManager::DeleteInstance(InstanceMap::iterator iter) {
    Notify(iter->second, CFG_DELETE);
}

void BgpYamlConfigManager::UpdateInstance(InstanceMap::iterator iter1,
                                          InstanceMap::iterator iter2) {
    // The instance configuration is always considered to be equal since
    // the configuration parameters are not settable by the YAML parser (yet).
    swap(iter1->second, iter2->second);
}

void BgpYamlConfigManager::UpdateInstances(
    Configuration *current, Configuration *next) {
    map_difference(
        current->InstanceMapMutable()->begin(),
        current->InstanceMapMutable()->end(),
        next->InstanceMapMutable()->begin(),
        next->InstanceMapMutable()->end(),
        boost::bind(&BgpYamlConfigManager::AddInstance, this, _1),
        boost::bind(&BgpYamlConfigManager::DeleteInstance, this, _1),
        boost::bind(&BgpYamlConfigManager::UpdateInstance, this, _1, _2));
}

void BgpYamlConfigManager::AddNeighbor(NeighborMap::iterator iter) {
    Notify(iter->second, CFG_ADD);
}
void BgpYamlConfigManager::DeleteNeighbor(NeighborMap::iterator iter) {
    Notify(iter->second, CFG_DELETE);
}

void BgpYamlConfigManager::UpdateNeighbor(NeighborMap::iterator iter1,
                                          NeighborMap::iterator iter2) {
    if (*iter1->second != *iter2->second) {
        Notify(iter2->second, CFG_CHANGE);
    } else {
        swap(iter1->second, iter2->second);
    }
}

void BgpYamlConfigManager::UpdateNeighbors(
    Configuration *current, Configuration *next) {
    map_difference(
        current->NeighborMapMutable()->begin(),
        current->NeighborMapMutable()->end(),
        next->NeighborMapMutable()->begin(),
        next->NeighborMapMutable()->end(),
        boost::bind(&BgpYamlConfigManager::AddNeighbor, this, _1),
        boost::bind(&BgpYamlConfigManager::DeleteNeighbor, this, _1),
        boost::bind(&BgpYamlConfigManager::UpdateNeighbor, this, _1, _2));
}

void BgpYamlConfigManager::Update(Configuration *current, Configuration *next) {
    UpdateProtocol(current, next);
    UpdateInstances(current, next);
    UpdateNeighbors(current, next);
}

bool BgpYamlConfigManager::Parse(istream *stream, string *error_msg) {
    YAML::Node config;
    try {
        config = YAML::Load(*stream);
    } catch (YAML::Exception &ex) {
        *error_msg = ex.msg;
        return false;
    }

    YAML::Node bgp = config["bgp"];
    auto_ptr<Configuration> candidate(new Configuration());
    if (bgp && !ParseBgp(candidate.get(), bgp, error_msg)) {
        return false;
    }

    if (!Resolve(candidate.get(), error_msg)) {
        return false;
    }
    Update(data_.get(), candidate.get());
    data_ = candidate;

    return true;
}
