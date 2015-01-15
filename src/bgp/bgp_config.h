/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#ifndef BGP_BGP_CONFIG_H__
#define BGP_BGP_CONFIG_H__

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/util.h"
#include "net/address.h"

class BgpServer;

struct AuthenticationKey {
    enum KeyType {
        NIL = 0,
        MD5,
    };

    bool operator<(const AuthenticationKey &) const;

    std::string id;
    KeyType type;
    std::string value;
    time_t start_time;
};

typedef std::vector<AuthenticationKey> AuthenticationKeyChain;

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

    BgpNeighborConfig();

    const std::string &name() const { return name_; }
    void set_name(const std::string &name) { name_ = name; }

    const std::string &uuid() const { return uuid_; }
    void set_uuid(const std::string &uuid) { uuid_ = uuid; }

    const std::string &instance_name() const { return instance_name_; }
    void set_instance_name(const std::string &instance_name) {
        instance_name_ = instance_name;
    }

    uint32_t peer_as() const { return peer_as_; }
    void set_peer_as(uint32_t peer_as) { peer_as_ = peer_as; }

    const IpAddress &peer_address() const { return address_; }
    void set_peer_address(const IpAddress &address) { address_ = address; }

    uint32_t peer_identifier() const { return identifier_; }
    void set_peer_identifier(uint32_t identifier) {
        identifier_ = identifier;
    }

    int port() const { return port_; }
    void set_port(int port) { port_ = port; }

    int hold_time() const { return hold_time_; }
    void set_hold_time(int hold_time) { hold_time_ = hold_time; }

    uint32_t local_as() const { return local_as_; }
    void set_local_as(uint32_t local_as) { local_as_ = local_as; }

    uint32_t local_identifier() const { return local_identifier_; }
    void set_local_identifier(uint32_t identifier) {
        local_identifier_ = identifier;
    }

    const AuthenticationKeyChain &keychain() const {
        return keychain_;
    }
    void set_keychain(const AuthenticationKeyChain &keychain) {
        keychain_ = keychain;
    }

    const AddressFamilyList &address_families() const {
        return address_families_;
    }

    void set_address_families(const AddressFamilyList &address_families) {
        address_families_ = address_families;
    }

    int CompareTo(const BgpNeighborConfig &rhs) const;
    bool operator!=(const BgpNeighborConfig &rhs) const {
        return CompareTo(rhs) != 0;
    }

private:
    std::string name_;
    std::string uuid_;
    std::string instance_name_;
    uint32_t peer_as_;
    uint32_t identifier_;
    IpAddress address_;
    int port_;
    int hold_time_;
    uint32_t local_as_;
    uint32_t local_identifier_;
    AuthenticationKeyChain keychain_;
    AddressFamilyList address_families_;

    DISALLOW_COPY_AND_ASSIGN(BgpNeighborConfig);
};

struct ServiceChainConfig {
    std::string routing_instance;
    std::vector<std::string> prefix;
    std::string service_chain_address;
    std::string service_instance;
    std::string source_routing_instance;
};

struct StaticRouteConfig {
    IpAddress address;
    int prefix_length;
    IpAddress nexthop;
    std::vector<std::string> route_target;
};

// Instance configuration.
class BgpInstanceConfig {
public:
    typedef std::set<std::string> RouteTargetList;
    typedef std::vector<StaticRouteConfig> StaticRouteList;
    typedef std::vector<ServiceChainConfig> ServiceChainList;

    explicit BgpInstanceConfig(const std::string &name);
    virtual ~BgpInstanceConfig();

    const std::string &name() const { return name_; }

    const RouteTargetList &import_list() const { return import_list_; }
    void set_import_list(const RouteTargetList &import_list) {
        import_list_ = import_list;
    }
    const RouteTargetList &export_list() const { return export_list_; }
    void set_export_list(const RouteTargetList &export_list) {
        export_list_ = export_list;
    }

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

    int vxlan_id() const { return vxlan_id_; }
    void set_vxlan_id(int vxlan_id) { vxlan_id_ = vxlan_id; }

    const StaticRouteList &static_routes() const { return static_routes_; }
    void swap_static_routes(StaticRouteList *list) {
        std::swap(static_routes_, *list);
    }

    const ServiceChainList &service_chain_list() const {
        return service_chain_list_;
    }
    void swap_service_chain_list(ServiceChainList *list) {
        std::swap(service_chain_list_, *list);
    }

    void Clear();

private:
    friend class BgpInstanceConfigTest;

    std::string name_;
    RouteTargetList import_list_;
    RouteTargetList export_list_;
    std::string virtual_network_;
    int virtual_network_index_;
    bool virtual_network_allow_transit_;
    int vxlan_id_;
    StaticRouteList static_routes_;
    ServiceChainList service_chain_list_;
    DISALLOW_COPY_AND_ASSIGN(BgpInstanceConfig);
};

// Local configuration.
class BgpProtocolConfig {
public:
    explicit BgpProtocolConfig(const std::string &instance_name);
    const std::string &instance_name() const {
        return instance_name_;
    }

    uint32_t identifier() const { return identifier_; }
    void set_identifier(uint32_t identifier) { identifier_ = identifier; }
    uint32_t autonomous_system() const { return autonomous_system_; }
    void set_autonomous_system(uint32_t autonomous_system) {
        autonomous_system_ = autonomous_system;
    }
    int hold_time() const { return hold_time_; }
    void set_hold_time(int hold_time) { hold_time_ = hold_time; }

private:
    std::string instance_name_;
    uint32_t autonomous_system_;
    uint32_t identifier_;
    int hold_time_;
    DISALLOW_COPY_AND_ASSIGN(BgpProtocolConfig);
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

    struct Observers {
        BgpProtocolObserver protocol;
        BgpInstanceObserver instance;
        BgpNeighborObserver neighbor;
    };

    typedef std::map<std::string, BgpInstanceConfig *> InstanceMap;
    typedef std::pair<InstanceMap::const_iterator,
            InstanceMap::const_iterator> InstanceMapRange;
    typedef std::map<std::string, BgpNeighborConfig *> NeighborMap;
    typedef std::pair<NeighborMap::const_iterator,
            NeighborMap::const_iterator> NeighborMapRange;

    static const char *kMasterInstance;
    static const int kDefaultPort;
    static const uint32_t kDefaultAutonomousSystem;

    BgpConfigManager(BgpServer *server);
    virtual ~BgpConfigManager();

    void RegisterObservers(const Observers &obs) { obs_ = obs; }

    virtual void Terminate() = 0;
    virtual const std::string &localname() const = 0;

    virtual InstanceMapRange InstanceMapItems() const = 0;
    virtual NeighborMapRange NeighborMapItems(
        const std::string &instance_name) const = 0;

    virtual int NeighborCount(const std::string &instance_name) const = 0;

    virtual const BgpInstanceConfig *FindInstance(
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
    Observers obs_;

    DISALLOW_COPY_AND_ASSIGN(BgpConfigManager);
};

#endif  // BGP_BGP_CONFIG_H__
