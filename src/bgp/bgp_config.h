/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef SRC_BGP_BGP_CONFIG_H__
#define SRC_BGP_BGP_CONFIG_H__

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/util.h"
#include "base/address.h"
#include "bgp/bgp_common.h"
#include "bgp/routing-instance/iservice_chain_mgr.h"
#include "io/tcp_session.h"
#include "schema/vnc_cfg_types.h"

class BgpServer;

struct AuthenticationKey {
    AuthenticationKey() : id(-1), start_time(0) {
    }

    bool operator<(const AuthenticationKey &rhs) const;
    bool operator==(const AuthenticationKey &rhs) const;
    void Reset() {
        value = "";
        start_time = 0;
    }

    int id;
    std::string value;
    time_t start_time;
};

typedef std::vector<AuthenticationKey> AuthenticationKeyChain;

class AuthenticationData {
public:
    enum KeyType {
        NIL = 0,
        MD5,
    };
    typedef AuthenticationKeyChain::iterator iterator;
    typedef AuthenticationKeyChain::const_iterator const_iterator;

    iterator begin() { return key_chain_.begin(); }
    iterator end() { return key_chain_.end(); }
    const_iterator begin() const { return key_chain_.begin(); }
    const_iterator end() const { return key_chain_.end(); }

    AuthenticationData();
    AuthenticationData(KeyType type, const AuthenticationKeyChain &chain);

    bool operator==(const AuthenticationData &rhs) const;
    bool operator<(const AuthenticationData &rhs) const;
    AuthenticationData &operator=(const AuthenticationData &rhs);

    const AuthenticationKey *Find(int key_id) const;
    bool IsMd5() const { return key_type_ == MD5; }
    bool Empty() const;
    void Clear();
    std::string KeyTypeToString() const;
    static std::string KeyTypeToString(KeyType key_type);
    void AddKeyToKeyChain(const AuthenticationKey &key);

    KeyType key_type() const { return key_type_; }
    void set_key_type(KeyType in_type) {
        key_type_ = in_type;
    }
    const AuthenticationKeyChain &key_chain() const { return key_chain_; }
    void set_key_chain(const AuthenticationKeyChain &in_chain) {
        key_chain_ = in_chain;
    }
    std::vector<std::string> KeysToString() const;
    std::vector<std::string> KeysToStringDetail() const;

private:
    KeyType key_type_;
    AuthenticationKeyChain key_chain_;
};

//
// Per address family configuration for a BGP neighbor.
//
struct BgpFamilyAttributesConfig {
    explicit BgpFamilyAttributesConfig(const std::string &family)
        : family(family), loop_count(0), prefix_limit(0), idle_timeout(0) {
    }
    bool operator==(const BgpFamilyAttributesConfig &rhs) const;

    std::string family;
    uint8_t loop_count;
    uint32_t prefix_limit;
    uint32_t idle_timeout;
    std::vector<std::string> default_tunnel_encap_list;
};

//
// Comparator for BgpFamilyAttributesConfig.
//
struct BgpFamilyAttributesConfigCompare {
    int operator()(const BgpFamilyAttributesConfig lhs,
                   const BgpFamilyAttributesConfig rhs) const {
        KEY_COMPARE(lhs.family, rhs.family);
        KEY_COMPARE(lhs.loop_count, rhs.loop_count);
        KEY_COMPARE(lhs.prefix_limit, rhs.prefix_limit);
        KEY_COMPARE(lhs.idle_timeout, rhs.idle_timeout);
        KEY_COMPARE(lhs.default_tunnel_encap_list,
                    rhs.default_tunnel_encap_list);
        return 0;
    }
};

//
// The configuration associated with a BGP neighbor.
//
// BgpNeighborConfig represents a single session between the local bgp-router
// and a remote bgp-router.  There may be multiple BgpNeighborConfigs for one
// BgpIfmapPeeringConfig, though there's typically just one.
//
// Each BgpNeighborConfig causes creation of a BgpPeer. BgpPeer has a pointer
// to the BgpNeighborConfig. This pointer is invalidated and cleared when the
// BgpPeer is undergoing deletion.
//
// BgpNeighborConfigs get created/updated/deleted when the
// BgpIfmapPeeringConfig is created/updated/deleted.
//
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
    typedef std::vector<BgpFamilyAttributesConfig> FamilyAttributesList;

    enum Type {
        UNSPECIFIED,
        IBGP,
        EBGP,
    };

    struct OriginOverrideConfig {
        OriginOverrideConfig();
        bool operator<(const OriginOverrideConfig &rhs) const;

        bool origin_override;
        std::string origin;
    };

    BgpNeighborConfig();

    void CopyValues(const BgpNeighborConfig &rhs);

    const std::string &name() const { return name_; }
    void set_name(const std::string &name) { name_ = name; }

    const std::string &uuid() const { return uuid_; }
    void set_uuid(const std::string &uuid) { uuid_ = uuid; }

    const std::string &instance_name() const { return instance_name_; }
    void set_instance_name(const std::string &instance_name) {
        instance_name_ = instance_name;
    }

    const std::string &group_name() const { return group_name_; }
    void set_group_name(const std::string &group_name) {
        group_name_ = group_name;
    }

    Type peer_type() const { return type_; }
    void set_peer_type(Type type) { type_ = type; }

    bool admin_down() const { return admin_down_; }
    void set_admin_down(bool admin_down) { admin_down_ = admin_down; }

    bool passive() const { return passive_; }
    void set_passive(bool passive) { passive_ = passive; }

    bool as_override() const { return as_override_; }
    void set_as_override(bool as_override) { as_override_ = as_override; }

    std::string private_as_action() const { return private_as_action_; }
    void set_private_as_action(const std::string &private_as_action) {
        private_as_action_ = private_as_action;
    }

    uint32_t cluster_id() const { return cluster_id_; }
    void set_cluster_id(uint32_t cluster_id) { cluster_id_ = cluster_id; }

    uint32_t peer_as() const { return peer_as_; }
    void set_peer_as(uint32_t peer_as) { peer_as_ = peer_as; }

    const IpAddress &peer_address() const { return address_; }
    void set_peer_address(const IpAddress &address) { address_ = address; }
    std::string peer_address_string() const { return address_.to_string(); }

    uint32_t peer_identifier() const { return identifier_; }
    void set_peer_identifier(uint32_t identifier) {
        identifier_ = identifier;
    }
    std::string peer_identifier_string() const {
        return Ip4Address(ntohl(identifier_)).to_string();
    }

    const IpAddress &gateway_address(Address::Family family) const;
    void set_gateway_address(Address::Family family, const IpAddress &address);

    uint16_t port() const { return port_; }
    void set_port(uint16_t port) { port_ = port; }

    uint16_t source_port() const { return source_port_; }
    void set_source_port(uint16_t source_port) { source_port_ = source_port; }

    std::string router_type() const { return router_type_; }
    void set_router_type(const std::string &router_type) {
        router_type_ = router_type;
    }

    int hold_time() const { return hold_time_; }
    void set_hold_time(int hold_time) { hold_time_ = hold_time; }

    uint8_t loop_count() const { return loop_count_; }
    void set_loop_count(uint8_t loop_count) { loop_count_ = loop_count; }

    uint32_t local_as() const { return local_as_; }
    void set_local_as(uint32_t local_as) { local_as_ = local_as; }

    uint32_t local_identifier() const { return local_identifier_; }
    void set_local_identifier(uint32_t identifier) {
        local_identifier_ = identifier;
    }
    std::string local_identifier_string() const {
        return Ip4Address(ntohl(local_identifier_)).to_string();
    }

    const AuthenticationData &auth_data() const {
        return auth_data_;
    }
    void set_keydata(const AuthenticationData &in_auth_data) {
        auth_data_ = in_auth_data;
    }

    AddressFamilyList GetAddressFamilies() const;

    const FamilyAttributesList &family_attributes_list() const {
        return family_attributes_list_;
    }

    void set_family_attributes_list(
        const FamilyAttributesList &family_attributes_list) {
        family_attributes_list_ = family_attributes_list;
    }

    uint64_t last_change_at() const { return last_change_at_; }
    void set_last_change_at(uint64_t tstamp) const { last_change_at_ = tstamp; }

    std::string AuthKeyTypeToString() const;
    std::vector<std::string> AuthKeysToString() const;

    int CompareTo(const BgpNeighborConfig &rhs) const;
    bool operator!=(const BgpNeighborConfig &rhs) const {
        return CompareTo(rhs) != 0;
    }

    const OriginOverrideConfig &origin_override() const {
        return origin_override_;
    }
    void SetOriginOverride(bool origin_override, std::string origin);

private:
    std::string name_;
    std::string uuid_;
    std::string instance_name_;
    std::string group_name_;
    Type type_;
    std::string router_type_;
    bool admin_down_;
    bool passive_;
    bool as_override_;
    std::string private_as_action_;
    uint32_t cluster_id_;
    uint32_t peer_as_;
    uint32_t identifier_;
    IpAddress address_;
    IpAddress inet_gateway_address_;
    IpAddress inet6_gateway_address_;
    uint16_t port_;
    uint16_t source_port_;
    TcpSession::Endpoint remote_endpoint_;
    int hold_time_;
    uint8_t loop_count_;
    uint32_t local_as_;
    uint32_t local_identifier_;
    mutable uint64_t last_change_at_;
    AuthenticationData auth_data_;
    FamilyAttributesList family_attributes_list_;
    OriginOverrideConfig origin_override_;

    DISALLOW_COPY_AND_ASSIGN(BgpNeighborConfig);
};

//
// AggregateRouteConfig represents the route-aggregation config on a
// routing instance. This config is derived from routing-instance to
// route-aggregate link in schema. "route-aggregate" is a config object
// containing information about prefix to aggregate and nexthop.
// Routing instance may refer to multiple route-aggregate config object hence
// a list of AggregateRouteConfig is maintained in BgpInstanceConfig
// This list is not an ordered list.
//
struct AggregateRouteConfig {
    IpAddress aggregate;
    int prefix_length;
    IpAddress nexthop;
};

struct ServiceChainConfig {
    SCAddress::Family family;
    std::string routing_instance;
    std::vector<std::string> prefix;
    std::string service_chain_address;
    std::string service_instance;
    std::string source_routing_instance;
    std::string service_chain_id;
    bool sc_head;
};

struct StaticRouteConfig {
    bool operator<(const StaticRouteConfig &rhs) const;
    IpAddress address;
    int prefix_length;
    IpAddress nexthop;
    std::vector<std::string> route_targets;
    std::vector<std::string> communities;
};

typedef std::vector<as_t> AsnList;
typedef std::vector<std::string> CommunityList;
typedef std::vector<std::string> ProtocolList;

struct PrefixMatchConfig {
    PrefixMatchConfig(std::string to_match, std::string match_type)
        : prefix_to_match(to_match), prefix_match_type(match_type) {
    }
    std::string prefix_to_match;
    std::string prefix_match_type;
};

typedef std::vector<PrefixMatchConfig> PrefixMatchConfigList;

struct RoutingPolicyMatchConfig {
    ProtocolList protocols_match;
    PrefixMatchConfigList prefixes_to_match;
    CommunityList community_match;
    bool community_match_all;
    CommunityList ext_community_match;
    bool ext_community_match_all;
    std::string ToString() const;
};

struct ActionUpdate {
    AsnList aspath_expand;
    CommunityList community_set;
    CommunityList community_add;
    CommunityList community_remove;
    CommunityList ext_community_set;
    CommunityList ext_community_add;
    CommunityList ext_community_remove;
    uint32_t local_pref;
    uint32_t med;
};

struct RoutingPolicyActionConfig {
    enum ActionType {
        ACCEPT,
        REJECT,
        NEXT_TERM
    };
    ActionUpdate update;
    ActionType action;
    std::string ToString() const;
};

struct RoutingPolicyTermConfig {
    RoutingPolicyMatchConfig match;
    RoutingPolicyActionConfig action;
};

// Route Policy configuration.
class BgpRoutingPolicyConfig {
public:
    typedef std::vector<RoutingPolicyTermConfig> RoutingPolicyTermList;
    explicit BgpRoutingPolicyConfig(const std::string &name);
    virtual ~BgpRoutingPolicyConfig();

    const std::string &name() const { return name_; }
    void set_last_change_at(uint64_t tstamp) const { last_change_at_ = tstamp; }
    void add_term(const RoutingPolicyTermConfig &term) {
        terms_.push_back(term);
    }
    const RoutingPolicyTermList &terms() const { return terms_;}
    void Clear();

private:
    std::string name_;
    mutable uint64_t last_change_at_;
    RoutingPolicyTermList terms_;
    DISALLOW_COPY_AND_ASSIGN(BgpRoutingPolicyConfig);
};

// Instance configuration.
class BgpInstanceConfig {
public:
    typedef std::set<std::string> NeighborList;
    typedef std::set<std::string> RouteTargetList;
    typedef std::set<StaticRouteConfig> StaticRouteList;
    typedef std::vector<ServiceChainConfig> ServiceChainList;
    typedef std::vector<AggregateRouteConfig> AggregateRouteList;

    explicit BgpInstanceConfig(const std::string &name);
    virtual ~BgpInstanceConfig();

    const std::string &name() const { return name_; }

    const NeighborList &neighbor_list() const { return neighbor_list_; }
    void add_neighbor(const std::string &neighbor) {
        neighbor_list_.insert(neighbor);
    }
    void delete_neighbor(const std::string &neighbor) {
        neighbor_list_.erase(neighbor);
    }

    const RouteTargetList &import_list() const { return import_list_; }
    void set_import_list(const RouteTargetList &import_list) {
        import_list_ = import_list;
    }
    const RouteTargetList &export_list() const { return export_list_; }
    void set_export_list(const RouteTargetList &export_list) {
        export_list_ = export_list;
    }

    bool has_pnf() const { return has_pnf_; }
    void set_has_pnf(bool has_pnf) { has_pnf_ = has_pnf; }

    const std::string &virtual_network() const { return virtual_network_; }
    void set_virtual_network(const std::string &virtual_network) {
        virtual_network_ = virtual_network;
    }

    int virtual_network_index() const { return virtual_network_index_; }
    void set_virtual_network_index(int virtual_network_index) {
        virtual_network_index_ = virtual_network_index;
    }

    bool virtual_network_allow_transit() const {
        return virtual_network_allow_transit_;
    }
    void set_virtual_network_allow_transit(bool allow_transit) {
        virtual_network_allow_transit_ = allow_transit;
    }

    bool virtual_network_pbb_evpn_enable() const {
        return virtual_network_pbb_evpn_enable_;
    }
    void set_virtual_network_pbb_evpn_enable(bool pbb_evpn) {
        virtual_network_pbb_evpn_enable_ = pbb_evpn;
    }


    int vxlan_id() const { return vxlan_id_; }
    void set_vxlan_id(int vxlan_id) { vxlan_id_ = vxlan_id; }

    uint64_t last_change_at() const { return last_change_at_; }
    void set_last_change_at(uint64_t tstamp) const { last_change_at_ = tstamp; }

    const StaticRouteList &static_routes(Address::Family family) const;
    void swap_static_routes(Address::Family family, StaticRouteList *list);

    const ServiceChainList &service_chain_list() const {
        return service_chain_list_;
    }
    void swap_service_chain_list(ServiceChainList *list) {
        std::swap(service_chain_list_, *list);
    }
    const ServiceChainConfig *service_chain_info(SCAddress::Family family)
        const;

    const RoutingPolicyConfigList &routing_policy_list() const {
        return routing_policies_;
    }
    void swap_routing_policy_list(RoutingPolicyConfigList *list) {
        std::swap(routing_policies_, *list);
    }

    const AggregateRouteList &aggregate_routes(Address::Family family) const;
    void swap_aggregate_routes(Address::Family family,
                               AggregateRouteList *list);
    void Clear();
    void set_index(int index) { index_ = index; }
    int index() const { return index_; }

private:
    friend class BgpInstanceConfigTest;

    std::string name_;
    NeighborList neighbor_list_;
    RouteTargetList import_list_;
    RouteTargetList export_list_;
    bool has_pnf_;
    std::string virtual_network_;
    int virtual_network_index_;
    bool virtual_network_allow_transit_;
    bool virtual_network_pbb_evpn_enable_;
    int index_;
    int vxlan_id_;
    mutable uint64_t last_change_at_;
    StaticRouteList inet_static_routes_;
    StaticRouteList inet6_static_routes_;
    AggregateRouteList inet_aggregate_routes_;
    AggregateRouteList inet6_aggregate_routes_;
    ServiceChainList service_chain_list_;
    RoutingPolicyConfigList routing_policies_;

    DISALLOW_COPY_AND_ASSIGN(BgpInstanceConfig);
};

// Local configuration.
class BgpProtocolConfig {
public:
    explicit BgpProtocolConfig(const std::string &instance_name);
    const std::string &instance_name() const {
        return instance_name_;
    }

    int CompareTo(const BgpProtocolConfig &rhs) const;

    bool admin_down() const { return admin_down_; }
    void set_admin_down(bool admin_down) { admin_down_ = admin_down; }

    uint32_t cluster_id() const { return cluster_id_; }
    void set_cluster_id(uint32_t cluster_id) { cluster_id_ = cluster_id; }

    uint32_t identifier() const { return identifier_; }
    void set_identifier(uint32_t identifier) { identifier_ = identifier; }

    const std::string& subcluster_name() const { return subcluster_name_; }
    void set_subcluster_name(const std::string& name) {
        subcluster_name_ = name;
    }
    void reset_subcluster_name() { subcluster_name_ = ""; }

    uint32_t subcluster_id() const { return subcluster_id_; }
    void set_subcluster_id(uint32_t id) {
        subcluster_id_ = id;
    }
    void reset_subcluster_id() { subcluster_id_ = 0; }

    uint32_t autonomous_system() const { return autonomous_system_; }
    void set_autonomous_system(uint32_t autonomous_system) {
        autonomous_system_ = autonomous_system;
    }

    uint32_t local_autonomous_system() const {
        return local_autonomous_system_;
    }
    void set_local_autonomous_system(uint32_t local_autonomous_system) {
        local_autonomous_system_ = local_autonomous_system;
    }

    int port() const { return port_; }
    void set_port(int port) { port_ = port; }

    uint32_t hold_time() const { return hold_time_; }
    void set_hold_time(uint32_t hold_time) { hold_time_ = hold_time; }

    uint64_t last_change_at() const { return last_change_at_; }
    void set_last_change_at(uint64_t tstamp) const { last_change_at_ = tstamp; }

private:
    std::string instance_name_;
    bool admin_down_;
    uint32_t cluster_id_;
    uint32_t autonomous_system_;
    uint32_t local_autonomous_system_;
    uint32_t identifier_;
    std::string subcluster_name_;
    uint32_t subcluster_id_;
    int port_;
    uint32_t hold_time_;
    mutable uint64_t last_change_at_;

    DISALLOW_COPY_AND_ASSIGN(BgpProtocolConfig);
};

// Global system configuration.
class BgpGlobalSystemConfig {
public:
    static const int kEndOfRibTime = 300; // seconds
    BgpGlobalSystemConfig() :
            last_change_at_(0), gr_time_(0), llgr_time_(0),
            end_of_rib_timeout_(kEndOfRibTime), gr_enable_(false),
            gr_bgp_helper_(false), gr_xmpp_helper_(false),
            enable_4byte_as_(false),
            bgpaas_port_start_(0),
            bgpaas_port_end_(0),
            always_compare_med_(false),
            rd_cluster_seed_(0) {
    }
    ~BgpGlobalSystemConfig() { }

    uint16_t gr_time() const { return gr_time_; }
    void set_gr_time(uint16_t gr_time) { gr_time_ = gr_time; }
    uint32_t llgr_time() const { return llgr_time_; }
    void set_llgr_time(uint64_t llgr_time) { llgr_time_ = llgr_time; }
    uint32_t last_change_at() const { return last_change_at_; }
    void set_last_change_at(uint32_t tstamp) const { last_change_at_ = tstamp; }
    uint16_t end_of_rib_timeout() const { return end_of_rib_timeout_; }
    void set_end_of_rib_timeout(uint16_t time) { end_of_rib_timeout_ = time; }
    bool gr_bgp_helper() const { return gr_bgp_helper_; }
    void set_gr_bgp_helper(bool helper) { gr_bgp_helper_ = helper; }
    bool gr_xmpp_helper() const { return gr_xmpp_helper_; }
    void set_gr_xmpp_helper(bool helper) { gr_xmpp_helper_ = helper; }
    bool gr_enable() const { return gr_enable_; }
    void set_gr_enable(bool enable) { gr_enable_ = enable; }
    bool enable_4byte_as() const { return enable_4byte_as_; }
    void set_enable_4byte_as(bool as_4byte) { enable_4byte_as_ = as_4byte; }
    bool always_compare_med() const { return always_compare_med_; }
    void set_always_compare_med(bool always_compare_med) {
        always_compare_med_ = always_compare_med;
    }
    uint16_t rd_cluster_seed() const {
        return rd_cluster_seed_;
    }
    void set_rd_cluster_seed(uint16_t seed) {
        rd_cluster_seed_ = seed;
    }
    uint16_t bgpaas_port_start() const { return bgpaas_port_start_; }
    void set_bgpaas_port_start(uint16_t bgpaas_port_start) {
        bgpaas_port_start_ = bgpaas_port_start;
    }
    uint16_t bgpaas_port_end() const { return bgpaas_port_end_; }
    void set_bgpaas_port_end(uint16_t bgpaas_port_end) {
        bgpaas_port_end_ = bgpaas_port_end;
    }

private:
    mutable uint64_t last_change_at_;
    uint16_t gr_time_;
    uint32_t llgr_time_;
    uint16_t end_of_rib_timeout_;
    bool gr_enable_;
    bool gr_bgp_helper_;
    bool gr_xmpp_helper_;
    bool enable_4byte_as_;
    uint16_t bgpaas_port_start_;
    uint16_t bgpaas_port_end_;
    bool always_compare_med_;
    uint16_t rd_cluster_seed_;

    DISALLOW_COPY_AND_ASSIGN(BgpGlobalSystemConfig);
};

// Global Qos configuration.
class BgpGlobalQosConfig {
public:
    BgpGlobalQosConfig() :
        last_change_at_(0), control_dscp_(0), analytics_dscp_(0) {
    }
    ~BgpGlobalQosConfig() { }
    uint64_t last_change_at() const { return last_change_at_; }
    void set_last_change_at(uint64_t tstamp) const { last_change_at_ = tstamp; }
    uint8_t control_dscp() const { return control_dscp_; }
    void set_control_dscp(uint8_t value) { control_dscp_ = value; }
    uint8_t analytics_dscp() const { return analytics_dscp_; }
    void set_analytics_dscp(uint8_t value) { analytics_dscp_ = value; }

private:
    mutable uint64_t last_change_at_;
    uint8_t control_dscp_;
    uint8_t analytics_dscp_;

    DISALLOW_COPY_AND_ASSIGN(BgpGlobalQosConfig);
};

/*
 * BgpConfigManager defines the interface between the BGP server and the
 * configuration sub-system. Multiple configuration sub-systems are
 * supported.
 */
class BgpConfigManager {
public:
    enum EventType {
        CFG_NONE,
        CFG_ADD,
        CFG_CHANGE,
        CFG_DELETE
    };

    typedef boost::function<void(const BgpProtocolConfig *, EventType)>
        BgpProtocolObserver;
    typedef boost::function<void(const BgpInstanceConfig *, EventType)>
        BgpInstanceObserver;
    typedef boost::function<void(const BgpNeighborConfig *, EventType)>
        BgpNeighborObserver;
    typedef boost::function<void(const BgpRoutingPolicyConfig *, EventType)>
        BgpRoutingPolicyObserver;
    typedef boost::function<void(const BgpGlobalSystemConfig *, EventType)>
        BgpGlobalSystemConfigObserver;
    typedef boost::function<void(const BgpGlobalQosConfig *, EventType)>
        BgpGlobalQosConfigObserver;

    struct Observers {
        BgpProtocolObserver protocol;
        BgpInstanceObserver instance;
        BgpNeighborObserver neighbor;
        BgpRoutingPolicyObserver policy;
        BgpGlobalSystemConfigObserver system;
        BgpGlobalQosConfigObserver qos;
    };

    typedef std::map<std::string, BgpRoutingPolicyConfig *> RoutingPolicyMap;
    typedef std::pair<RoutingPolicyMap::const_iterator,
            RoutingPolicyMap::const_iterator> RoutingPolicyMapRange;
    typedef std::map<std::string, BgpInstanceConfig *> InstanceMap;
    typedef std::pair<InstanceMap::const_iterator,
            InstanceMap::const_iterator> InstanceMapRange;
    typedef std::map<std::string, BgpNeighborConfig *> NeighborMap;
    typedef std::pair<NeighborMap::const_iterator,
            NeighborMap::const_iterator> NeighborMapRange;

    static const char *kMasterNetwork;
    static const char *kMasterInstance;
    static const char *kFabricInstance;
    static const int kDefaultPort;
    static const uint32_t kDefaultAutonomousSystem;

    explicit BgpConfigManager(BgpServer *server);
    virtual ~BgpConfigManager();

    void RegisterObservers(const Observers &obs) { obs_.push_back(obs); }

    virtual void Terminate() = 0;
    virtual const std::string &localname() const = 0;

    virtual RoutingPolicyMapRange RoutingPolicyMapItems(
        const std::string &start_policy = std::string()) const = 0;
    virtual InstanceMapRange InstanceMapItems(
        const std::string &start_instance = std::string()) const = 0;
    virtual NeighborMapRange NeighborMapItems(
        const std::string &instance_name) const = 0;

    virtual int NeighborCount(const std::string &instance_name) const = 0;

    virtual void ResetRoutingInstanceIndexBit(int index) = 0;
    virtual const BgpInstanceConfig *FindInstance(
        const std::string &name) const = 0;
    virtual const BgpRoutingPolicyConfig *FindRoutingPolicy(
        const std::string &name) const = 0;
    virtual const BgpProtocolConfig *GetProtocolConfig(
        const std::string &instance_name) const = 0;
    virtual const BgpNeighborConfig *FindNeighbor(
        const std::string &instance_name, const std::string &name) const = 0;

    // Invoke registered observer
    template <typename BgpConfigObject>
    void Notify(const BgpConfigObject *, EventType);

    const BgpServer *server() { return server_; }

private:
    BgpServer *server_;
    std::vector<Observers> obs_;

    DISALLOW_COPY_AND_ASSIGN(BgpConfigManager);
};

#endif  // SRC_BGP_BGP_CONFIG_H__
