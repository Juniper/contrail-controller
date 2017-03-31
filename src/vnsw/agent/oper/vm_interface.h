/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_interface_hpp
#define vnsw_agent_vm_interface_hpp

#include <oper/oper_dhcp_options.h>
#include <oper/audit_list.h>
#include <oper/ecmp_load_balance.h>

/////////////////////////////////////////////////////////////////////////////
// Implementation of VM Port interfaces
/////////////////////////////////////////////////////////////////////////////
typedef std::vector<boost::uuids::uuid> SgUuidList;
typedef std::vector<SgEntryRef> SgList;
struct VmInterfaceData;
struct VmInterfaceConfigData;
struct VmInterfaceNovaData;
struct VmInterfaceIpAddressData;
struct VmInterfaceOsOperStateData;
struct VmInterfaceMirrorData;
class OperDhcpOptions;
class PathPreference;
class MetaDataIp;
class HealthCheckInstance;

class LocalVmPortPeer;

/////////////////////////////////////////////////////////////////////////////
// Definition for VmInterface
// Agent supports multiple type of VMInterfaces
// - VMI for a virtual-machine spawned on KVM compute node. It will have a TAP
//   interface associated with it. Agent also expects a INSTANCE_MSG from
//    nova-compute/equivalent to add this port
//    DeviceType = VM_ON_TAP, VmiType = INSTANCE
// - VMI for a virtual-machine spawned on VMware ESXi. All virtual-machines
//   are connected on a physical-port and VLAN is used to distinguish them
//   DeviceType = VM_PHYSICAL_VLAN, VmiType = INTANCE, agent->hypervisor_mode = ESXi
// - VMI for a virtual-machine spawned on VMWare VCenter . All virtual-machines
//   are connected on a physical-port and they are classified by the smac
//   DeviceType = VM_PHYSICAL_MAC, VmiType = INTANCE and
//   agent hypervisor mode = VCenter
// - VMI for service-chain virtual-machines
// - VMI for service-chain virtual-machine spawned on KVM compute node.
//   It will have a TAP interface associated with it. Agent also expects a
//   INSTANCE_MSG from interface associated with it.
//   DeviceType = VM_ON_TAP, VmiType = SERVICE_CHAIN
// - VMI for service-instances spawned by agent itself.
//   It will have a TAP interface associated with it. Agent also expects a
//   INSTANCE_MSG from interface associated with it.
//   DeviceType = VM, VmiType = SERVICE_INTANCE
//
/////////////////////////////////////////////////////////////////////////////
class VmInterface : public Interface {
public:
    static const uint32_t kInvalidVlanId = 0xFFFF;
    static const uint32_t kInvalidPmdId = 0xFFFF;
    static const uint32_t kInvalidIsid = 0xFFFFFF;

    enum Configurer {
        INSTANCE_MSG,
        CONFIG
    };

    // Type of VMI Port
    enum DeviceType {
        DEVICE_TYPE_INVALID,
        VM_ON_TAP,          // VMI on TAP/physial port interface
                            // VMI is created based on the INSTANCE_MSG
        VM_VLAN_ON_VMI,     // VMI on TAP port with VLAN as classifier
                            // VMI is created based on config message
        VM_PHYSICAL_VLAN,   // VMI classified with VLAN on a physical-port
                            // (used in VMWare ESXi)
                            // VMI is created based on the INSTANCE_MSG
        VM_PHYSICAL_MAC,    // VMI classified with MAC on a physical-port
                            // (used in VMWare VCenter)
                            // VMI is created based on the INSTANCE_MSG
        TOR,                // Baremetal connected to ToR
        LOCAL_DEVICE,       // VMI on a local port. Used in GATEWAY
        REMOTE_VM_VLAN_ON_VMI, // VMI on a local phy-port with VLAN as classifier
        VM_SRIOV            // VMI on an SRIOV VM
    };

    // Type of VM on the VMI
    enum VmiType {
        VMI_TYPE_INVALID,
        INSTANCE,
        SERVICE_CHAIN,
        SERVICE_INSTANCE,
        BAREMETAL,
        GATEWAY,
        REMOTE_VM,
        SRIOV
    };

    // Interface uses different type of labels. Enumeration of different
    // types is given below
    enum LabelType {
        LABEL_TYPE_INVALID = 0,
        LABEL_TYPE_L2,
        LABEL_TYPE_L3,
        LABEL_TYPE_AAP,
        LABEL_TYPE_SERVICE_VLAN,
        LABEL_TYPE_MAX
    };

    enum ProxyArpMode {
        PROXY_ARP_NONE,
        PROXY_ARP_UNRESTRICTED,
        PROXY_ARP_INVALID
    };

    struct ListEntry {
        ListEntry() : installed_(false), del_pending_(false) { }
        ListEntry(bool installed, bool del_pending) :
            installed_(installed), del_pending_(del_pending) { }
        virtual ~ListEntry()  {}

        bool installed() const { return installed_; }
        bool del_pending() const { return del_pending_; }
        void set_installed(bool val) const { installed_ = val; }
        void set_del_pending(bool val) const { del_pending_ = val; }

        mutable bool installed_;
        mutable bool del_pending_;
    };

    // A unified structure for storing FloatingIp information for both
    // operational and config elements
    struct FloatingIp : public ListEntry {
        enum Direction {
            DIRECTION_BOTH,
            DIRECTION_INGRESS,
            DIRECTION_EGRESS,
            DIRECTION_INVALID
        };

        struct PortMapKey {
            uint8_t protocol_;
            uint16_t port_;
            PortMapKey(): protocol_(0), port_(0) { }
            PortMapKey(uint8_t protocol, uint16_t port) :
                protocol_(protocol), port_(port) { }
            ~PortMapKey() { }

            bool operator()(const PortMapKey &lhs, const PortMapKey &rhs) {
                if (lhs.protocol_ != rhs.protocol_)
                    return lhs.protocol_ < rhs.protocol_;
                return lhs.port_ < rhs.port_;
            }
        };

        typedef std::map<PortMapKey, uint16_t, PortMapKey> PortMap;
        typedef PortMap::iterator PortMapIterator;
        FloatingIp();
        FloatingIp(const FloatingIp &rhs);
        FloatingIp(const IpAddress &addr, const std::string &vrf,
                   const boost::uuids::uuid &vn_uuid, const IpAddress &ip,
                   Direction direction, bool port_mappng_enabled,
                   const PortMap &src_port_map, const PortMap &dst_port_map);
        virtual ~FloatingIp();

        bool operator() (const FloatingIp &lhs, const FloatingIp &rhs) const;
        bool IsLess(const FloatingIp *rhs) const;
        void L3Activate(VmInterface *interface, bool force_update) const;
        void L3DeActivate(VmInterface *interface) const;
        void L2Activate(VmInterface *interface, bool force_update,
                        uint32_t old_ethernet_tag) const;
        void L2DeActivate(VmInterface *interface,
                          uint32_t ethernet_tag) const;
        void DeActivate(VmInterface *interface, bool l2,
                        uint32_t old_ethernet_tag) const;
        void Activate(VmInterface *interface, bool force_update,
                      bool l2, uint32_t old_ethernet_tag) const;
        const IpAddress GetFixedIp(const VmInterface *) const;
        bool port_map_enabled() const;
        uint32_t PortMappingSize() const;
        int32_t GetSrcPortMap(uint8_t protocol, uint16_t src_port) const;
        int32_t GetDstPortMap(uint8_t protocol, uint16_t dst_port) const;
        Direction direction() const { return direction_; }
        // direction_ is based on packet direction. Allow DNAT if direction is
        // "both or ingress"
        bool AllowDNat() const {
            return (direction_ == DIRECTION_BOTH ||
                    direction_ == DIRECTION_INGRESS);
        }
        // direction_ is based on packet direction. Allow SNAT if direction is
        // "both or egress"
        bool AllowSNat() const {
            return (direction_ == DIRECTION_BOTH ||
                    direction_ == DIRECTION_EGRESS);
        }


        IpAddress floating_ip_;
        mutable VnEntryRef vn_;
        mutable VrfEntryRef vrf_;
        std::string vrf_name_;
        boost::uuids::uuid vn_uuid_;
        mutable bool l2_installed_;
        mutable IpAddress fixed_ip_;
        mutable bool force_l3_update_;
        mutable bool force_l2_update_;
        mutable Direction direction_;
        mutable bool port_map_enabled_;
        mutable PortMap src_port_map_;
        mutable PortMap dst_port_map_;
    };
    typedef std::set<FloatingIp, FloatingIp> FloatingIpSet;
    typedef std::map<Ip4Address, MetaDataIp*> MetaDataIpMap;
    typedef std::set<HealthCheckInstance *> HealthCheckInstanceSet;

    struct FloatingIpList {
        FloatingIpList() : v4_count_(0), v6_count_(0), list_() { }
        ~FloatingIpList() { }

        void Insert(const FloatingIp *rhs);
        void Update(const FloatingIp *lhs, const FloatingIp *rhs);
        void Remove(FloatingIpSet::iterator &it);

        uint16_t v4_count_;
        uint16_t v6_count_;
        FloatingIpSet list_;
    };

    // A unified structure for storing AliasIp information for both
    // operational and config elements
    struct AliasIp : public ListEntry {
        AliasIp();
        AliasIp(const AliasIp &rhs);
        AliasIp(const IpAddress &addr, const std::string &vrf,
                   const boost::uuids::uuid &vn_uuid);
        virtual ~AliasIp();

        bool operator() (const AliasIp &lhs, const AliasIp &rhs) const;
        bool IsLess(const AliasIp *rhs) const;
        void Activate(VmInterface *interface, bool force_update) const;
        void DeActivate(VmInterface *interface) const;

        IpAddress alias_ip_;
        mutable VnEntryRef vn_;
        mutable VrfEntryRef vrf_;
        std::string vrf_name_;
        boost::uuids::uuid vn_uuid_;
        mutable bool force_update_;
    };
    typedef std::set<AliasIp, AliasIp> AliasIpSet;

    struct AliasIpList {
        AliasIpList() : v4_count_(0), v6_count_(0), list_() { }
        ~AliasIpList() { }

        void Insert(const AliasIp *rhs);
        void Update(const AliasIp *lhs, const AliasIp *rhs);
        void Remove(AliasIpSet::iterator &it);

        uint16_t v4_count_;
        uint16_t v6_count_;
        AliasIpSet list_;
    };

    struct ServiceVlan : ListEntry {
        ServiceVlan();
        ServiceVlan(const ServiceVlan &rhs);
        ServiceVlan(uint16_t tag, const std::string &vrf_name,
                    const Ip4Address &addr, const Ip6Address &addr6,
                    const MacAddress &smac, const MacAddress &dmac);
        virtual ~ServiceVlan();

        bool operator() (const ServiceVlan &lhs, const ServiceVlan &rhs) const;
        bool IsLess(const ServiceVlan *rhs) const;
        void Activate(VmInterface *interface, bool force_change,
                      bool old_ipv4_active, bool old_ipv6_active) const;
        void DeActivate(VmInterface *interface) const;
        void V4RouteDelete(const Peer *peer) const;
        void V6RouteDelete(const Peer *peer) const;

        uint16_t tag_;
        mutable std::string vrf_name_;
        Ip4Address addr_;
        Ip6Address addr6_;
        MacAddress smac_;
        MacAddress dmac_;
        mutable VrfEntryRef vrf_;
        mutable uint32_t label_;
        mutable bool v4_rt_installed_;
        mutable bool v6_rt_installed_;
    };
    typedef std::set<ServiceVlan, ServiceVlan> ServiceVlanSet;

    struct ServiceVlanList {
        ServiceVlanList() : list_() { }
        ~ServiceVlanList() { }
        void Insert(const ServiceVlan *rhs);
        void Update(const ServiceVlan *lhs, const ServiceVlan *rhs);
        void Remove(ServiceVlanSet::iterator &it);

        ServiceVlanSet list_;
    };

    struct StaticRoute : ListEntry {
        StaticRoute();
        StaticRoute(const StaticRoute &rhs);
        StaticRoute(const std::string &vrf, const IpAddress &addr,
                    uint32_t plen, const IpAddress &gw,
                    const CommunityList &communities);
        virtual ~StaticRoute();

        bool operator() (const StaticRoute &lhs, const StaticRoute &rhs) const;
        bool IsLess(const StaticRoute *rhs) const;
        void Activate(VmInterface *interface, bool force_update) const;
        void DeActivate(VmInterface *interface) const;

        mutable std::string vrf_;
        IpAddress  addr_;
        uint32_t    plen_;
        IpAddress  gw_;
        CommunityList communities_;
    };
    typedef std::set<StaticRoute, StaticRoute> StaticRouteSet;

    struct StaticRouteList {
        StaticRouteList() : list_() { }
        ~StaticRouteList() { }
        void Insert(const StaticRoute *rhs);
        void Update(const StaticRoute *lhs, const StaticRoute *rhs);
        void Remove(StaticRouteSet::iterator &it);

        StaticRouteSet list_;
    };

    struct AllowedAddressPair : ListEntry {
        AllowedAddressPair();
        AllowedAddressPair(const AllowedAddressPair &rhs);
        AllowedAddressPair(const std::string &vrf, const IpAddress &addr,
                           uint32_t plen, bool ecmp, const MacAddress &mac);
        virtual ~AllowedAddressPair();

        bool operator() (const AllowedAddressPair &lhs,
                         const AllowedAddressPair &rhs) const;
        bool IsLess(const AllowedAddressPair *rhs) const;
        void Activate(VmInterface *interface, bool force_update,
                      bool policy_change) const;
        void DeActivate(VmInterface *interface) const;
        void L2Activate(VmInterface *interface, bool force_update,
                        bool policy_change, bool old_layer2_forwarding,
                        bool old_layer3_forwarding) const;
        void L2DeActivate(VmInterface *interface) const;
        void CreateLabelAndNH(Agent *agent, VmInterface *interface,
                              bool policy_change) const;

        mutable std::string vrf_;
        IpAddress   addr_;
        uint32_t    plen_;
        mutable bool ecmp_;
        MacAddress  mac_;
        mutable bool        l2_entry_installed_;
        mutable bool        ecmp_config_changed_;
        mutable uint32_t    ethernet_tag_;
        mutable VrfEntryRef vrf_ref_;
        mutable IpAddress  service_ip_;
        mutable uint32_t label_;
        mutable NextHopRef policy_enabled_nh_;
        mutable NextHopRef policy_disabled_nh_;
    };
    typedef std::set<AllowedAddressPair, AllowedAddressPair>
        AllowedAddressPairSet;

    struct AllowedAddressPairList {
        AllowedAddressPairList() : list_() { }
        ~AllowedAddressPairList() { }
        void Insert(const AllowedAddressPair *rhs);
        void Update(const AllowedAddressPair *lhs,
                    const AllowedAddressPair *rhs);
        void Remove(AllowedAddressPairSet::iterator &it);

        AllowedAddressPairSet list_;
    };

    struct SecurityGroupEntry : ListEntry {
        SecurityGroupEntry();
        SecurityGroupEntry(const SecurityGroupEntry &rhs);
        SecurityGroupEntry(const boost::uuids::uuid &uuid);
        virtual ~SecurityGroupEntry();

        bool operator == (const SecurityGroupEntry &rhs) const;
        bool operator() (const SecurityGroupEntry &lhs,
                         const SecurityGroupEntry &rhs) const;
        bool IsLess(const SecurityGroupEntry *rhs) const;
        void Activate(VmInterface *interface) const;
        void DeActivate(VmInterface *interface) const;

        mutable SgEntryRef sg_;
        boost::uuids::uuid uuid_;
    };
    typedef std::set<SecurityGroupEntry, SecurityGroupEntry>
        SecurityGroupEntrySet;
    typedef std::vector<boost::uuids::uuid> SecurityGroupUuidList;

    struct SecurityGroupEntryList {
        SecurityGroupEntryList() : list_() { }
        ~SecurityGroupEntryList() { }

        void Insert(const SecurityGroupEntry *rhs);
        void Update(const SecurityGroupEntry *lhs,
                    const SecurityGroupEntry *rhs);
        void Remove(SecurityGroupEntrySet::iterator &it);

        SecurityGroupEntrySet list_;
    };

    struct VrfAssignRule : ListEntry {
        VrfAssignRule();
        VrfAssignRule(const VrfAssignRule &rhs);
        VrfAssignRule(uint32_t id, 
                      const autogen::MatchConditionType &match_condition_,
                      const std::string &vrf_name, bool ignore_acl);
        ~VrfAssignRule();
        bool operator == (const VrfAssignRule &rhs) const;
        bool operator() (const VrfAssignRule &lhs,
                         const VrfAssignRule &rhs) const;
        bool IsLess(const VrfAssignRule *rhs) const;

        const uint32_t id_;
        mutable std::string vrf_name_;
        mutable bool ignore_acl_;
        mutable autogen::MatchConditionType match_condition_;
    };
    typedef std::set<VrfAssignRule, VrfAssignRule> VrfAssignRuleSet;

    struct VrfAssignRuleList {
        VrfAssignRuleList() : list_() { }
        ~VrfAssignRuleList() { };
        void Insert(const VrfAssignRule *rhs);
        void Update(const VrfAssignRule *lhs, const VrfAssignRule *rhs);
        void Remove(VrfAssignRuleSet::iterator &it);

        VrfAssignRuleSet list_;
    };

    struct InstanceIp : ListEntry {
        InstanceIp();
        InstanceIp(const InstanceIp &rhs);
        InstanceIp(const IpAddress &ip, uint8_t plen, bool ecmp,
                   bool is_primary, bool is_service_health_check_ip,
                   bool is_local, const IpAddress &tracking_ip);
        ~InstanceIp();
        bool operator == (const InstanceIp &rhs) const;
        bool operator() (const InstanceIp &lhs,
                         const InstanceIp &rhs) const;
        InstanceIp operator = (const InstanceIp &rhs) const {
            InstanceIp ret(rhs);
            return ret;
        }
        bool IsLess(const InstanceIp *rhs) const;
        void L3Activate(VmInterface *interface, bool force_update) const;
        void L3DeActivate(VmInterface *interface, VrfEntry *old_vrf) const;
        void L2Activate(VmInterface *interface, bool force_update,
                        uint32_t old_ethernet_tag) const;
        void L2DeActivate(VmInterface *interface, VrfEntry *old_vrf,
                          uint32_t old_ethernet_tag) const;
        void DeActivate(VmInterface *interface, bool l2,
                        VrfEntry *old_vrf, uint32_t old_ethernet_tag) const;
        void Activate(VmInterface *interface, bool force_update, bool l2,
                      int old_ethernet_tag) const;
        void SetPrefixForAllocUnitIpam(VmInterface *interface) const;
        bool installed() const {
            if (l2_installed_ || installed_) {
                return true;
            }
            return false;
        }

        bool is_force_policy() const {
            return is_service_health_check_ip_;
        }

        bool IsL3Only() const {
            return is_service_health_check_ip_;
        }

        const IpAddress ip_;
        mutable uint8_t plen_;
        mutable bool ecmp_;
        mutable bool l2_installed_;
        mutable bool old_ecmp_;
        mutable bool is_primary_;
        mutable bool is_service_health_check_ip_;
        mutable bool is_local_;
        mutable IpAddress old_tracking_ip_;
        mutable IpAddress tracking_ip_;
    };
    typedef std::set<InstanceIp, InstanceIp> InstanceIpSet;

    struct InstanceIpList {
        InstanceIpList() : list_() { }
        ~InstanceIpList() { };
        void Insert(const InstanceIp *rhs);
        void Update(const InstanceIp *lhs, const InstanceIp *rhs);
        void Remove(InstanceIpSet::iterator &it);

        InstanceIpSet list_;
    };

    struct FatFlowEntry : ListEntry {
        FatFlowEntry(): protocol(0), port(0) {}
        FatFlowEntry(const FatFlowEntry &rhs):
            protocol(rhs.protocol), port(rhs.port) {}
        FatFlowEntry(const uint8_t proto, const uint16_t p):
            protocol(proto), port(p) {}
        virtual ~FatFlowEntry(){}
        bool operator == (const FatFlowEntry &rhs) const {
            return (rhs.protocol == protocol && rhs.port == port);
        }

        bool operator() (const FatFlowEntry  &lhs,
                         const FatFlowEntry &rhs) const {
            return lhs.IsLess(&rhs);
        }

        bool IsLess(const FatFlowEntry *rhs) const {
            if (protocol != rhs->protocol) {
                return protocol < rhs->protocol;
            }
            return port < rhs->port;
        }
        uint8_t protocol;
        uint16_t port;
    };
    typedef std::set<FatFlowEntry, FatFlowEntry> FatFlowEntrySet;

    struct FatFlowList {
        FatFlowList(): list_() {}
        ~FatFlowList() {}
        void Insert(const FatFlowEntry *rhs);
        void Update(const FatFlowEntry *lhs, const FatFlowEntry *rhs);
        void Remove(FatFlowEntrySet::iterator &it);

        FatFlowEntrySet list_;
    };

    struct BridgeDomain : ListEntry {
        BridgeDomain(): uuid_(nil_uuid()), vlan_tag_(0),
            bridge_domain_(NULL) {}
        BridgeDomain(const BridgeDomain &rhs):
            uuid_(rhs.uuid_), vlan_tag_(rhs.vlan_tag_),
            bridge_domain_(rhs.bridge_domain_) {}
        BridgeDomain(const boost::uuids::uuid &uuid, uint32_t vlan_tag):
            uuid_(uuid), vlan_tag_(vlan_tag), bridge_domain_(NULL) {}
        virtual ~BridgeDomain(){}
        bool operator == (const BridgeDomain &rhs) const {
            return (uuid_ == rhs.uuid_);
        }

        bool operator() (const BridgeDomain &lhs,
                         const BridgeDomain &rhs) const {
            return lhs.IsLess(&rhs);
        }

        bool IsLess(const BridgeDomain *rhs) const {
            return uuid_ < rhs->uuid_;
        }

        boost::uuids::uuid uuid_;
        uint32_t vlan_tag_;
        mutable BridgeDomainConstRef bridge_domain_;
    };
    typedef std::set<BridgeDomain, BridgeDomain> BridgeDomainEntrySet;

    struct BridgeDomainList {
        BridgeDomainList(): list_() {}
        ~BridgeDomainList() {}
        void Insert(const BridgeDomain *rhs);
        void Update(const BridgeDomain *lhs, const BridgeDomain *rhs);
        void Remove(BridgeDomainEntrySet::iterator &it);

        BridgeDomainEntrySet list_;
    };

    enum Trace {
        ADD,
        DELETE,
        ACTIVATED_IPV4,
        ACTIVATED_IPV6,
        ACTIVATED_L2,
        DEACTIVATED_IPV4,
        DEACTIVATED_IPV6,
        DEACTIVATED_L2,
        FLOATING_IP_CHANGE,
        SERVICE_CHANGE,
    };

    enum Preference {
        INVALID = 0,
        LOW     = 100,
        HIGH    = 200
    };

    VmInterface(const boost::uuids::uuid &uuid);
    VmInterface(const boost::uuids::uuid &uuid, const std::string &name,
                const Ip4Address &addr, const MacAddress &mac,
                const std::string &vm_name,
                const boost::uuids::uuid &vm_project_uuid, uint16_t tx_vlan_id,
                uint16_t rx_vlan_id, Interface *parent,
                const Ip6Address &addr6, DeviceType dev_type, VmiType vmi_type);
    virtual ~VmInterface();

    virtual bool CmpInterface(const DBEntry &rhs) const;
    virtual void GetOsParams(Agent *agent);
    void SendTrace(const AgentDBTable *table, Trace event) const;

    // DBEntry vectors
    KeyPtr GetDBRequestKey() const;
    std::string ToString() const;
    bool Resync(const InterfaceTable *table, const VmInterfaceData *data);
    bool OnChange(VmInterfaceData *data);
    void PostAdd();

    // Accessor functions
    const VmEntry *vm() const { return vm_.get(); }
    const VnEntry *vn() const { return vn_.get(); }
    const MirrorEntry *mirror_entry() const { return mirror_entry_.get(); }
    const Ip4Address &primary_ip_addr() const { return primary_ip_addr_; }
    bool policy_enabled() const { return policy_enabled_; }
    const Ip4Address &subnet_bcast_addr() const { return subnet_bcast_addr_; }
    const Ip6Address &primary_ip6_addr() const { return primary_ip6_addr_; }
    const MacAddress &vm_mac() const { return vm_mac_; }
    bool fabric_port() const { return fabric_port_; }
    bool need_linklocal_ip() const { return  need_linklocal_ip_; }
    bool drop_new_flows() const { return drop_new_flows_; }
    bool dhcp_enable_config() const { return dhcp_enable_; }
    void set_dhcp_enable_config(bool dhcp_enable) {
        dhcp_enable_= dhcp_enable;
    }
    bool admin_state() const { return admin_state_; }
    bool do_dhcp_relay() const { return do_dhcp_relay_; }
    ProxyArpMode proxy_arp_mode() const { return proxy_arp_mode_; }
    bool IsUnrestrictedProxyArp() const {
        return proxy_arp_mode_ == PROXY_ARP_UNRESTRICTED;
    }
    int vxlan_id() const { return vxlan_id_; }
    bool bridging() const { return bridging_; }
    bool layer3_forwarding() const { return layer3_forwarding_; }
    const std::string &vm_name() const { return vm_name_; }
    const boost::uuids::uuid &vm_project_uuid() const { return vm_project_uuid_; }
    const std::string &cfg_name() const { return cfg_name_; }
    Preference local_preference() const { return local_preference_; }
    uint16_t tx_vlan_id() const { return tx_vlan_id_; }
    uint16_t rx_vlan_id() const { return rx_vlan_id_; }
    const Interface *parent() const { return parent_.get(); }
    bool ecmp() const { return ecmp_;}
    bool ecmp6() const { return ecmp6_;}
    Ip4Address service_ip() { return service_ip_;}
    bool service_ip_ecmp() const { return service_ip_ecmp_;}
    Ip6Address service_ip6() { return service_ip6_;}
    bool service_ip_ecmp6() const { return service_ip_ecmp6_;}
    const OperDhcpOptions &oper_dhcp_options() const { return oper_dhcp_options_; }
    uint8_t configurer() const {return configurer_;}
    bool IsConfigurerSet(VmInterface::Configurer type);
    void SetConfigurer(VmInterface::Configurer type);
    void ResetConfigurer(VmInterface::Configurer type);
    bool CanBeDeleted() const {return (configurer_ == 0);}
    const Ip4Address& subnet() const { return subnet_;}
    const uint8_t subnet_plen() const { return subnet_plen_;}
    const MacAddress& GetVifMac(const Agent*) const;
    const boost::uuids::uuid &logical_interface() const {
        return logical_interface_;
    }
    bool flood_unknown_unicast() const {
        return flood_unknown_unicast_;
    }

    Interface::MirrorDirection mirror_direction() const {
        return mirror_direction_;
    }

    uint32_t FloatingIpCount() const { return floating_ip_list_.list_.size(); }
    const FloatingIpList &floating_ip_list() const {
        return floating_ip_list_;
    }

    const AliasIpList &alias_ip_list() const {
        return alias_ip_list_;
    }

    const ServiceVlanList &service_vlan_list() const {
        return service_vlan_list_;
    }

    const StaticRouteList &static_route_list() const {
        return static_route_list_;
    }

    const SecurityGroupEntryList &sg_list() const {
        return sg_list_;
    }

    const VrfAssignRuleList &vrf_assign_rule_list() const {
        return vrf_assign_rule_list_;
    }

    const AllowedAddressPairList &allowed_address_pair_list() const {
        return allowed_address_pair_list_;
    }

    const InstanceIpList &instance_ipv4_list() const {
        return instance_ipv4_list_;
    }

    const InstanceIpList &instance_ipv6_list() const {
        return instance_ipv6_list_;
    }

    const FatFlowList &fat_flow_list() const {
        return fat_flow_list_;
    }

    const BridgeDomainList &bridge_domain_list() const {
        return bridge_domain_list_;
    }

    bool IsFatFlow(uint8_t protocol, uint16_t port) const;
    void set_vxlan_id(int vxlan_id) { vxlan_id_ = vxlan_id; }
    void set_subnet_bcast_addr(const Ip4Address &addr) {
        subnet_bcast_addr_ = addr;
    }
    void set_mirror_entry (MirrorEntry *mirror_entry) {
        mirror_entry_ = mirror_entry;
    }
    void set_mirror_direction(Interface::MirrorDirection mirror_direction) {
        mirror_direction_ = mirror_direction;
    }
    void set_primary_ip_addr(const Ip4Address &addr) { primary_ip_addr_ = addr; }

    const std::string GetAnalyzer() const; 

    void SetPathPreference(PathPreference *pref, bool ecmp,
                           const IpAddress &dependent_ip) const;
    void SetServiceVlanPathPreference(PathPreference *pref,
                                      const IpAddress &service_ip) const;
    void CopySgIdList(SecurityGroupList *sg_id_list) const;
    bool NeedMplsLabel() const;
    bool IsVxlanMode() const;
    bool SgExists(const boost::uuids::uuid &id, const SgList &sg_l);
    bool IsMirrorEnabled() const { return mirror_entry_.get() != NULL; }
    bool HasFloatingIp(Address::Family family) const;
    bool HasFloatingIp() const;
    bool IsFloatingIp(const IpAddress &ip) const;
    VrfEntry *GetAliasIpVrf(const IpAddress &ip) const;
    Ip4Address mdata_ip_addr() const;
    MetaDataIp *GetMetaDataIp(const Ip4Address &ip) const;
    const MacAddress& GetIpMac(const IpAddress &,
                              const uint8_t plen) const;
    void BuildIpStringList(Address::Family family,
                           std::vector<std::string> *vect) const;

    void InsertMetaDataIpInfo(MetaDataIp *mip);
    void DeleteMetaDataIpInfo(MetaDataIp *mip);
    void UpdateMetaDataIpInfo();

    void InsertHealthCheckInstance(HealthCheckInstance *hc_inst);
    void DeleteHealthCheckInstance(HealthCheckInstance *hc_inst);
    const HealthCheckInstanceSet &hc_instance_set() const;
    bool IsHealthCheckEnabled() const;
    const HealthCheckInstance *GetHealthCheckFromVmiFlow(const IpAddress &sip,
                                                         const IpAddress &dip,
                                                         uint8_t proto,
                                                         uint16_t sport) const;
    size_t GetFloatingIpCount() const { return floating_ip_list_.list_.size(); }
    size_t GetAliasIpCount() const { return alias_ip_list_.list_.size(); }
    bool HasServiceVlan() const { return service_vlan_list_.list_.size() != 0; }

    uint32_t GetServiceVlanLabel(const VrfEntry *vrf) const;
    uint32_t GetServiceVlanTag(const VrfEntry *vrf) const;
    const VrfEntry* GetServiceVlanVrf(uint16_t vlan_tag) const;
    bool Delete(const DBRequest *req);
    void Add();
    bool OnResyncServiceVlan(VmInterfaceConfigData *data);
    void UpdateAllRoutes();

    bool IsL2Active() const;
    bool IsIpv6Active() const;
    bool NeedDevice() const;
    VmInterface::DeviceType device_type() const {return device_type_;}
    VmInterface::VmiType vmi_type() const {return vmi_type_;}

    // Add a vm-interface
    static void NovaAdd(InterfaceTable *table,
                        const boost::uuids::uuid &intf_uuid,
                        const std::string &os_name, const Ip4Address &addr,
                        const std::string &mac, const std::string &vn_name,
                        const boost::uuids::uuid &vm_project_uuid,
                        uint16_t tx_vlan_id, uint16_t rx_vlan_id,
                        const std::string &parent, const Ip6Address &ipv6,
                        Interface::Transport transport);
    // Del a vm-interface
    static void Delete(InterfaceTable *table,
                       const boost::uuids::uuid &intf_uuid,
                       VmInterface::Configurer configurer);
    static void SetIfNameReq(InterfaceTable *table,
                             const boost::uuids::uuid &uuid,
                             const std::string &ifname);
    static void DeleteIfNameReq(InterfaceTable *table,
                                const boost::uuids::uuid &uuid);

    const AclDBEntry* vrf_assign_acl() const { return vrf_assign_acl_.get();}
    bool WaitForTraffic() const;
    bool GetInterfaceDhcpOptions(
            std::vector<autogen::DhcpOptionType> *options) const;
    bool GetSubnetDhcpOptions(
            std::vector<autogen::DhcpOptionType> *options, bool ipv6) const;
    bool GetIpamDhcpOptions(
            std::vector<autogen::DhcpOptionType> *options, bool ipv6) const;
    const Peer *peer() const;
    IpAddress GetServiceIp(const IpAddress &ip) const;
    void UpdateL2InterfaceRoute(bool old_bridging, bool force_update,
                                VrfEntry *vrf,
                                const Ip4Address &old_addr,
                                const Ip6Address &old_v6_addr,
                                int ethernet_tag,
                                bool old_layer3_forwarding,
                                bool policy_change,
                                const Ip4Address &new_addr,
                                const Ip6Address &new_v6_addr,
                                const MacAddress &mac,
                                const IpAddress &dependent_ip) const;
    uint32_t ethernet_tag() const {return ethernet_tag_;}
    IpAddress service_health_check_ip() const { return service_health_check_ip_; }
    void UpdateVxLan();
    bool IsActive() const;
    const VmiEcmpLoadBalance &ecmp_load_balance() const {return ecmp_load_balance_;}
    bool InstallBridgeRoutes() const;
    Agent *agent() const {
        return (static_cast<InterfaceTable *>(get_table()))->agent();
    }

    bool is_vn_qos_config() const {
        return is_vn_qos_config_;
    }

    const NextHop* l3_interface_nh_no_policy() const {
        return l3_interface_nh_no_policy_.get();
    }

    const NextHop* l2_interface_nh_no_policy() const {
        return l2_interface_nh_no_policy_.get();
    }

    const NextHop* l2_interface_nh_policy() const {
        return l2_interface_nh_policy_.get();
    }

    bool learning_enabled() const {
        return learning_enabled_;
    }

    void set_learning_enabled(bool val) {
        learning_enabled_ = val;
    }

    bool etree_leaf() const {
        return etree_leaf_;
    }

    void set_etree_leaf(bool val) {
        etree_leaf_ = val;
    }

    bool pbb_interface() const {
        return pbb_interface_;
    }

    bool layer2_control_word() const {
        return layer2_control_word_;
    }

    void set_layer2_control_word(bool layer2_control_word) {
        layer2_control_word_ = layer2_control_word;
    }

    uint32_t GetIsid() const;
    uint32_t GetPbbVrf() const;
    uint32_t GetPbbLabel() const;
private:
    friend struct VmInterfaceConfigData;
    friend struct VmInterfaceNovaData;
    friend struct VmInterfaceIpAddressData;
    friend struct VmInterfaceOsOperStateData;
    friend struct VmInterfaceMirrorData;
    friend struct VmInterfaceGlobalVrouterData;
    friend struct VmInterfaceHealthCheckData;
    friend struct VmInterfaceNewFlowDropData;
    bool IsMetaDataL2Active() const;
    bool IsMetaDataIPActive() const;
    bool IsIpv4Active() const;
    bool PolicyEnabled() const;
    void AddRoute(const std::string &vrf_name, const IpAddress &ip,
                  uint32_t plen, const std::string &vn_name, bool force_policy,
                  bool ecmp, bool is_local, bool proxy_arp,
                  const IpAddress &service_ip, const IpAddress &dependent_ip,
                  const CommunityList &communties, uint32_t label);
    void DeleteRoute(const std::string &vrf_name, const IpAddress &ip,
                     uint32_t plen);
    void ResolveRoute(const std::string &vrf_name, const Ip4Address &addr,
                      uint32_t plen, const std::string &dest_vn, bool policy);
    void ServiceVlanAdd(ServiceVlan &entry);
    void ServiceVlanDel(ServiceVlan &entry);
    void ServiceVlanRouteAdd(const ServiceVlan &entry, bool force_update);
    void ServiceVlanRouteDel(const ServiceVlan &entry);

    bool OnResyncSecurityGroupList(VmInterfaceConfigData *data,
                                   bool new_ipv4_active);
    bool OnResyncStaticRoute(VmInterfaceConfigData *data, bool new_ipv4_active);
    bool ResyncIpAddress(const VmInterfaceIpAddressData *data);
    bool ResyncOsOperState(const VmInterfaceOsOperStateData *data);
    bool ResyncConfig(VmInterfaceConfigData *data);
    bool CopyIpAddress(Ip4Address &addr);
    bool CopyIp6Address(const Ip6Address &addr);
    void ApplyMacVmBindingConfig(const VrfEntry *old_vrf,
                                bool old_l2_active,
                                bool old_dhcp_enable);
    void ApplyConfigCommon(const VrfEntry *old_vrf,
                           bool old_l2_active,
                           bool old_dhcp_enable);
    bool CopyConfig(const InterfaceTable *table,
                    const VmInterfaceConfigData *data, bool *sg_changed,
                    bool *ecmp_changed, bool *local_pref_changed,
                    bool *ecmp_load_balance_changed,
                    bool *static_route_config_changed,
                    bool *etree_leaf_mode_changed);
    void ApplyConfig(bool old_ipv4_active,bool old_l2_active,  bool old_policy,
                     VrfEntry *old_vrf, const Ip4Address &old_addr,
                     int old_ethernet_tag, bool old_need_linklocal_ip,
                     bool old_ipv6_active, const Ip6Address &old_v6_addr,
                     const Ip4Address &old_subnet, const uint8_t old_subnet_plen,
                     bool old_dhcp_enable, bool old_layer3_forwarding,
                     bool force_update, const Ip4Address &old_dhcp_addr,
                     bool old_metadata_ip_active, bool old_bridging);
    void UpdateL3MetadataIp(VrfEntry *old_vrf, bool force_update,
                            bool policy_change, bool old_metadata_ip_active);
    void DeleteL3MetadataIp(VrfEntry *old_vrf, bool force_update,
                            bool policy_change, bool old_metadata_ip_active,
                            bool old_need_linklocal_ip);
    void UpdateL3(bool old_ipv4_active, VrfEntry *old_vrf,
                  const Ip4Address &old_addr, int old_ethernet_tag,
                  bool force_update, bool policy_change,
                  bool old_ipv6_active, const Ip6Address &old_v6_addr,
                  const Ip4Address &subnet, const uint8_t old_subnet_plen,
                  const Ip4Address &old_dhcp_addr);
    void DeleteL3(bool old_ipv4_active, VrfEntry *old_vrf,
                  const Ip4Address &old_addr, bool old_need_linklocal_ip,
                  bool old_ipv6_active, const Ip6Address &old_v6_addr,
                  const Ip4Address &old_subnet, const uint8_t old_subnet_plen,
                  int old_ethernet_tag, const Ip4Address &old_dhcp_addr,
                  bool force_update);
    void UpdateBridgeRoutes(bool old_bridging, VrfEntry *old_vrf,
                            int old_ethernet_tag, bool force_update,
                            bool policy_change, const Ip4Address &old_addr,
                            const Ip6Address &old_v6_addr,
                            bool old_layer3_forwarding);
    void DeleteBridgeRoutes(bool old_bridging, VrfEntry *old_vrf,
                            int old_ethernet_tag, const Ip4Address &old_addr,
                            const Ip6Address &old_v6_addr,
                            bool old_layer3_forwarding, bool force_update);
    void UpdateL2(bool old_l2_active, bool policy_change);
    void DeleteL2(bool old_l2_active);

    void DeleteL3MplsLabel();
    void DeleteL3TunnelId();
    void UpdateMacVmBinding();
    void UpdateL3NextHop();
    void DeleteL3NextHop();
    void UpdateL2NextHop(bool force_update);
    void UpdateFlowKeyNextHop();
    void DeleteL2NextHop();
    void DeleteMacVmBinding(const VrfEntry *old_vrf);
    bool L2Activated(bool old_l2_active);
    bool BridgingActivated(bool old_bridging);
    bool Ipv4Activated(bool old_ipv4_active);
    bool Ipv6Activated(bool old_ipv6_active);
    bool L2Deactivated(bool old_l2_active);
    bool BridgingDeactivated(bool old_bridging);
    bool Ipv4Deactivated(bool old_ipv4_active);
    bool Ipv6Deactivated(bool old_ipv6_active);
    void UpdateIpv4InterfaceRoute(bool old_ipv4_active, bool force_update,
                                  VrfEntry * old_vrf,
                                  const Ip4Address &old_addr);
    void DeleteIpv4InterfaceRoute(VrfEntry *old_vrf,
                                  const Ip4Address &old_addr);
    void UpdateResolveRoute(bool old_ipv4_active, bool force_update,
                            bool policy_change, VrfEntry * old_vrf,
                            const Ip4Address &old_addr, uint8_t old_plen);
    void DeleteResolveRoute(VrfEntry *old_vrf,
                            const Ip4Address &old_addr, const uint8_t old_plen);
    void DeleteInterfaceNH();
    void UpdateMetadataRoute(bool old_ipv4_active, VrfEntry *old_vrf);
    void DeleteMetadataRoute(bool old_ipv4_active, VrfEntry *old_vrf,
                             bool old_need_linklocal_ip);
    void CleanupFloatingIpList();
    void UpdateFloatingIp(bool force_update, bool policy_change, bool l2,
                          uint32_t old_ethernet_tag);
    void DeleteFloatingIp(bool l2, uint32_t old_ethernet_tag);

    void CleanupAliasIpList();
    void UpdateAliasIp(bool force_update, bool policy_change);
    void DeleteAliasIp();

    void UpdateServiceVlan(bool force_update, bool policy_change,
                           bool old_ipv4_active, bool old_ipv6_active);
    void DeleteServiceVlan();
    void UpdateStaticRoute(bool force_update);
    void DeleteStaticRoute();
    void UpdateAllowedAddressPair(bool force_update, bool policy_change,
                                  bool l2, bool old_l2_forwarding,
                                  bool old_l3_forwarding);
    void DeleteAllowedAddressPair(bool l2);
    void UpdateSecurityGroup();
    void DeleteSecurityGroup();
    void UpdateFatFlow();
    void DeleteFatFlow();
    void UpdateBridgeDomain();
    void DeleteBridgeDomain();
    void DeleteL2InterfaceRoute(bool old_bridging, VrfEntry *old_vrf,
                                const Ip4Address &old_v4_addr,
                                const Ip6Address &old_v6_addr,
                                int old_ethernet_tag,
                                const MacAddress &mac) const;

    void UpdateVrfAssignRule();
    void DeleteVrfAssignRule();
    void UpdateIpv4InstanceIp(bool force_update, bool policy_change,
                              bool l2, uint32_t old_ethernet_tag,
                              VrfEntry *old_vrf);
    void DeleteIpv4InstanceIp(bool l2, uint32_t old_ethernet_tag,
                              VrfEntry *old_vrf, bool force_update);
    void UpdateIpv6InstanceIp(bool force_update, bool policy_change,
                              bool l2, uint32_t old_ethernet_tag);
    void DeleteIpv6InstanceIp(bool l2, uint32_t old_ethernet_tag,
                              VrfEntry *old_vrf, bool force_update);

    void AddL2ReceiveRoute(bool old_bridging);
    void DeleteL2ReceiveRoute(const VrfEntry *old_vrf, bool old_briding);

    bool UpdateIsHealthCheckActive();
    void CopyEcmpLoadBalance(EcmpLoadBalance &ecmp_load_balance);
    void UpdateCommonNextHop(bool force_update);
    void DeleteCommonNextHop();

    VmEntryBackRef vm_;
    VnEntryRef vn_;
    Ip4Address primary_ip_addr_;
    std::auto_ptr<MetaDataIp> mdata_ip_;
    Ip4Address subnet_bcast_addr_;
    Ip6Address primary_ip6_addr_;
    MacAddress vm_mac_;
    bool policy_enabled_;
    MirrorEntryRef mirror_entry_;
    Interface::MirrorDirection mirror_direction_;
    std::string cfg_name_;
    bool fabric_port_;
    bool need_linklocal_ip_;
    bool drop_new_flows_;
    // DHCP flag - set according to the dhcp option in the ifmap subnet object.
    // It controls whether the vrouter sends the DHCP requests from VM interface
    // to agent or if it would flood the request in the VN.
    bool dhcp_enable_;
    // true if IP is to be obtained from DHCP Relay and not learnt from fabric
    bool do_dhcp_relay_; 
    // Proxy ARP mode for interface
    ProxyArpMode proxy_arp_mode_;
    // VM-Name. Used by DNS
    std::string vm_name_;
    // project uuid of the vm to which the interface belongs
    boost::uuids::uuid vm_project_uuid_;
    int vxlan_id_;
    bool bridging_;
    bool layer3_forwarding_;
    bool flood_unknown_unicast_;
    bool mac_set_;
    bool ecmp_;
    bool ecmp6_;
    Ip4Address service_ip_;
    bool service_ip_ecmp_;
    Ip6Address service_ip6_;
    bool service_ip_ecmp6_;
    // disable-policy configuration on VMI. When this is configured, policy
    // dependent features like flows, floating-IP and SG will not work on this
    // VMI. However metadata-services will work because metadata route will
    // still point to policy enabled NH.
    bool disable_policy_;
    // VLAN Tag and the parent interface when VLAN is enabled
    uint16_t tx_vlan_id_;
    uint16_t rx_vlan_id_;
    InterfaceBackRef parent_;
    Preference local_preference_;
    // DHCP options defined for the interface
    OperDhcpOptions oper_dhcp_options_;

    // Lists
    SecurityGroupEntryList sg_list_;
    FloatingIpList floating_ip_list_;
    AliasIpList alias_ip_list_;
    ServiceVlanList service_vlan_list_;
    StaticRouteList static_route_list_;
    AllowedAddressPairList allowed_address_pair_list_;
    InstanceIpList instance_ipv4_list_;
    InstanceIpList instance_ipv6_list_;
    FatFlowList fat_flow_list_;
    BridgeDomainList bridge_domain_list_;

    // Peer for interface routes
    std::auto_ptr<LocalVmPortPeer> peer_;
    VrfAssignRuleList vrf_assign_rule_list_;
    AclDBEntryRef vrf_assign_acl_;
    Ip4Address vm_ip_service_addr_;
    VmInterface::DeviceType device_type_;
    VmInterface::VmiType vmi_type_;
    uint8_t configurer_;
    Ip4Address subnet_;
    uint8_t subnet_plen_;
    int ethernet_tag_;
    // Logical interface uuid to which the interface belongs
    boost::uuids::uuid logical_interface_;
    Ip4Address nova_ip_addr_;
    Ip6Address nova_ip6_addr_;
    Ip4Address dhcp_addr_;
    MetaDataIpMap metadata_ip_map_;
    HealthCheckInstanceSet hc_instance_set_;
    VmiEcmpLoadBalance ecmp_load_balance_;
    IpAddress service_health_check_ip_;
    NextHopRef l3_interface_nh_policy_;
    NextHopRef l2_interface_nh_policy_;
    NextHopRef l3_interface_nh_no_policy_;
    NextHopRef l2_interface_nh_no_policy_;
    bool is_vn_qos_config_;
    bool learning_enabled_;
    bool etree_leaf_;
    bool pbb_interface_;
    bool layer2_control_word_;
    DISALLOW_COPY_AND_ASSIGN(VmInterface);
};

/////////////////////////////////////////////////////////////////////////////
// Key for VM Interfaces
/////////////////////////////////////////////////////////////////////////////
struct VmInterfaceKey : public InterfaceKey {
    VmInterfaceKey(AgentKey::DBSubOperation sub_op,
                   const boost::uuids::uuid &uuid, const std::string &name);
    virtual ~VmInterfaceKey() { }

    Interface *AllocEntry(const InterfaceTable *table) const;
    Interface *AllocEntry(const InterfaceTable *table, 
                          const InterfaceData *data) const;
    InterfaceKey *Clone() const;
};

/////////////////////////////////////////////////////////////////////////////
// The base class for different type of InterfaceData used for VmInterfaces.
//
// Request for VM-Interface data are of 3 types
// - ADD_DEL_CHANGE 
//   Message for ADD/DEL/CHANGE of an interface
// - MIRROR
//   Data for mirror enable/disable
// - IP_ADDR
//   In one of the modes, the IP address for an interface is not got from IFMap
//   or Nova. Agent will relay DHCP Request in such cases to IP Fabric network.
//   The IP address learnt with DHCP in this case is confgured with this type
// - OS_OPER_STATE
//   Update to oper state of the interface
/////////////////////////////////////////////////////////////////////////////
struct VmInterfaceData : public InterfaceData {
    enum Type {
        CONFIG,
        INSTANCE_MSG,
        MIRROR,
        IP_ADDR,
        OS_OPER_STATE,
        GLOBAL_VROUTER,
        HEALTH_CHECK,
        DROP_NEW_FLOWS
    };

    VmInterfaceData(Agent *agent, IFMapNode *node, Type type,
            Interface::Transport transport) :
        InterfaceData(agent, node, transport), type_(type) {
        VmPortInit();
    }
    virtual ~VmInterfaceData() { }

    virtual VmInterface *OnAdd(const InterfaceTable *table,
                               const VmInterfaceKey *key) const {
        return NULL;
    }
    virtual bool OnDelete(const InterfaceTable *table,
                          VmInterface *entry) const {
        return true;
    }
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const = 0;

    Type type_;
};

// Structure used when type=IP_ADDR. Used to update IP-Address of VM-Interface
// The IP Address is picked up from the DHCP Snoop table
struct VmInterfaceIpAddressData : public VmInterfaceData {
    VmInterfaceIpAddressData() : VmInterfaceData(NULL, NULL, IP_ADDR,
                                     Interface::TRANSPORT_INVALID) { }
    virtual ~VmInterfaceIpAddressData() { }
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;
};

// Structure used when type=OS_OPER_STATE Used to update interface os oper-state
// The current oper-state is got by querying the device
struct VmInterfaceOsOperStateData : public VmInterfaceData {
    VmInterfaceOsOperStateData() :
        VmInterfaceData(NULL, NULL, OS_OPER_STATE,
                        Interface::TRANSPORT_INVALID) { }
    virtual ~VmInterfaceOsOperStateData() { }
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;
};

// Structure used when type=MIRROR. Used to update IP-Address of VM-Interface
struct VmInterfaceMirrorData : public VmInterfaceData {
    VmInterfaceMirrorData(bool mirror_enable, const std::string &analyzer_name):
        VmInterfaceData(NULL, NULL, MIRROR, Interface::TRANSPORT_INVALID),
        mirror_enable_(mirror_enable),
        analyzer_name_(analyzer_name) {
    }
    virtual ~VmInterfaceMirrorData() { }
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;

    bool mirror_enable_;
    std::string analyzer_name_;
};

// Definition for structures when request queued from IFMap config.
struct VmInterfaceConfigData : public VmInterfaceData {
    VmInterfaceConfigData(Agent *agent, IFMapNode *node);
    virtual ~VmInterfaceConfigData() { }
    virtual VmInterface *OnAdd(const InterfaceTable *table,
                               const VmInterfaceKey *key) const;
    virtual bool OnDelete(const InterfaceTable *table,
                          VmInterface *entry) const;
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;

    Ip4Address addr_;
    Ip6Address ip6_addr_;
    std::string vm_mac_;
    std::string cfg_name_;
    boost::uuids::uuid vm_uuid_;
    std::string vm_name_;
    boost::uuids::uuid vn_uuid_;
    std::string vrf_name_;

    // Is this port on IP Fabric
    bool fabric_port_;
    // Does the port need link-local IP to be allocated
    bool need_linklocal_ip_;
    bool bridging_;
    bool layer3_forwarding_;
    bool mirror_enable_;
    //Is interface in active-active mode or active-backup mode
    bool ecmp_;
    bool ecmp6_;
    bool dhcp_enable_; // is DHCP enabled for the interface (from subnet config)
    VmInterface::ProxyArpMode proxy_arp_mode_;
    bool admin_state_;
    bool disable_policy_;
    std::string analyzer_name_;
    VmInterface::Preference local_preference_;
    OperDhcpOptions oper_dhcp_options_;
    Interface::MirrorDirection mirror_direction_;
    VmInterface::SecurityGroupEntryList sg_list_;
    VmInterface::FloatingIpList floating_ip_list_;
    VmInterface::AliasIpList alias_ip_list_;
    VmInterface::ServiceVlanList service_vlan_list_;
    VmInterface::StaticRouteList static_route_list_;
    VmInterface::VrfAssignRuleList vrf_assign_rule_list_;
    VmInterface::AllowedAddressPairList allowed_address_pair_list_;
    VmInterface::InstanceIpList instance_ipv4_list_;
    VmInterface::InstanceIpList instance_ipv6_list_;
    VmInterface::FatFlowList fat_flow_list_;
    VmInterface::BridgeDomainList bridge_domain_list_;
    VmInterface::DeviceType device_type_;
    VmInterface::VmiType vmi_type_;
    // Parent physical-interface. Used in VMWare/ ToR logical-interface
    std::string physical_interface_;
    // Parent VMI. Set only for VM_VLAN_ON_VMI
    boost::uuids::uuid parent_vmi_;
    Ip4Address subnet_;
    uint8_t subnet_plen_;
    uint16_t rx_vlan_id_;
    uint16_t tx_vlan_id_;
    boost::uuids::uuid logical_interface_;
    VmiEcmpLoadBalance ecmp_load_balance_;
    IpAddress service_health_check_ip_;
    Ip4Address service_ip_;
    bool service_ip_ecmp_;
    Ip6Address service_ip6_;
    bool service_ip_ecmp6_;
    boost::uuids::uuid qos_config_uuid_;
    bool learning_enabled_;
};

// Definition for structures when request queued from Nova
struct VmInterfaceNovaData : public VmInterfaceData {
    VmInterfaceNovaData();
    VmInterfaceNovaData(const Ip4Address &ipv4_addr,
                        const Ip6Address &ipv6_addr,
                        const std::string &mac_addr,
                        const std::string vm_name,
                        boost::uuids::uuid vm_uuid,
                        boost::uuids::uuid vm_project_uuid,
                        const std::string &parent,
                        uint16_t tx_vlan_id,
                        uint16_t rx_vlan_id,
                        VmInterface::DeviceType device_type,
                        VmInterface::VmiType vmi_type,
                        Interface::Transport transport);
    virtual ~VmInterfaceNovaData();
    virtual VmInterface *OnAdd(const InterfaceTable *table,
                               const VmInterfaceKey *key) const;
    virtual bool OnDelete(const InterfaceTable *table,
                          VmInterface *entry) const;
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;

    Ip4Address ipv4_addr_;
    Ip6Address ipv6_addr_;
    std::string mac_addr_;
    std::string vm_name_;
    boost::uuids::uuid vm_uuid_;
    boost::uuids::uuid vm_project_uuid_;
    std::string physical_interface_;
    uint16_t tx_vlan_id_;
    uint16_t rx_vlan_id_;
    VmInterface::DeviceType device_type_;
    VmInterface::VmiType vmi_type_;
};

struct VmInterfaceGlobalVrouterData : public VmInterfaceData {
    VmInterfaceGlobalVrouterData(bool bridging,
                                 bool layer3_forwarding,
                                 int vxlan_id) :
        VmInterfaceData(NULL, NULL, GLOBAL_VROUTER, Interface::TRANSPORT_INVALID),
        bridging_(bridging),
        layer3_forwarding_(layer3_forwarding),
        vxlan_id_(vxlan_id) {
    }
    virtual ~VmInterfaceGlobalVrouterData() { }
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;

    bool bridging_;
    bool layer3_forwarding_;
    int vxlan_id_;
};

struct VmInterfaceHealthCheckData : public VmInterfaceData {
    VmInterfaceHealthCheckData();
    virtual ~VmInterfaceHealthCheckData();
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;
};

struct VmInterfaceNewFlowDropData : public VmInterfaceData {
    VmInterfaceNewFlowDropData(bool drop_new_flows);
    virtual ~VmInterfaceNewFlowDropData();
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;

    bool drop_new_flows_;
};

// Data used when interface added with only ifname in data
struct VmInterfaceIfNameData : public VmInterfaceData {
    VmInterfaceIfNameData();
    VmInterfaceIfNameData(const std::string &ifname);
    virtual ~VmInterfaceIfNameData();

    virtual VmInterface *OnAdd(const InterfaceTable *table,
                               const VmInterfaceKey *key) const;
    virtual bool OnDelete(const InterfaceTable *table, VmInterface *vmi) const;
    virtual bool OnResync(const InterfaceTable *table, VmInterface *vmi,
                          bool *force_update) const;

    std::string ifname_;
};
#endif // vnsw_agent_vm_interface_hpp
