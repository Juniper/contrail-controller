/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_interface_hpp
#define vnsw_agent_vm_interface_hpp

#include <oper/oper_dhcp_options.h>

/////////////////////////////////////////////////////////////////////////////
// Implementation of VM Port interfaces
/////////////////////////////////////////////////////////////////////////////
typedef std::vector<boost::uuids::uuid> SgUuidList;
typedef std::vector<SgEntryRef> SgList;
struct VmInterfaceData;
struct VmInterfaceConfigData;
struct VmInterfaceIpAddressData;
struct VmInterfaceOsOperStateData;
struct VmInterfaceMirrorData;
class OperDhcpOptions;

class LocalVmPortPeer;
/////////////////////////////////////////////////////////////////////////////
// Definition for VmInterface
/////////////////////////////////////////////////////////////////////////////
class VmInterface : public Interface {
public:
    static const uint32_t kInvalidVlanId = 0xFFFF;

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
        FloatingIp();
        FloatingIp(const FloatingIp &rhs);
        FloatingIp(const Ip4Address &addr, const std::string &vrf,
                   const boost::uuids::uuid &vn_uuid);
        virtual ~FloatingIp();

        bool operator() (const FloatingIp &lhs, const FloatingIp &rhs) const;
        bool IsLess(const FloatingIp *rhs) const;
        void Activate(VmInterface *interface, bool force_update) const;
        void DeActivate(VmInterface *interface) const;

        Ip4Address floating_ip_;
        mutable VnEntryRef vn_;
        mutable VrfEntryRef vrf_;
        std::string vrf_name_;
        boost::uuids::uuid vn_uuid_;
    };
    typedef std::set<FloatingIp, FloatingIp> FloatingIpSet;

    struct FloatingIpList {
        FloatingIpList() : list_() { }
        ~FloatingIpList() { }

        void Insert(const FloatingIp *rhs);
        void Update(const FloatingIp *lhs, const FloatingIp *rhs);
        void Remove(FloatingIpSet::iterator &it);

        FloatingIpSet list_;
    };

    struct ServiceVlan : ListEntry {
        ServiceVlan();
        ServiceVlan(const ServiceVlan &rhs);
        ServiceVlan(uint16_t tag, const std::string &vrf_name,
                    const Ip4Address &addr, uint8_t plen,
                    const MacAddress &smac,
                    const MacAddress &dmac);
        virtual ~ServiceVlan();

        bool operator() (const ServiceVlan &lhs, const ServiceVlan &rhs) const;
        bool IsLess(const ServiceVlan *rhs) const;
        void Activate(VmInterface *interface, bool force_change) const;
        void DeActivate(VmInterface *interface) const;

        uint16_t tag_;
        std::string vrf_name_;
        Ip4Address addr_;
        uint8_t plen_;
        MacAddress smac_;
        MacAddress dmac_;
        mutable VrfEntryRef vrf_;
        mutable uint32_t label_;
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
        StaticRoute(const std::string &vrf, const Ip4Address &addr,
                    uint32_t plen);
        virtual ~StaticRoute();

        bool operator() (const StaticRoute &lhs, const StaticRoute &rhs) const;
        bool IsLess(const StaticRoute *rhs) const;
        void Activate(VmInterface *interface, bool force_update,
                      bool policy_change) const;
        void DeActivate(VmInterface *interface) const;

        mutable std::string vrf_;
        Ip4Address  addr_;
        uint32_t    plen_;
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
        AllowedAddressPair(const std::string &vrf, const Ip4Address &addr,
                           uint32_t plen, bool ecmp);
        virtual ~AllowedAddressPair();

        bool operator() (const AllowedAddressPair &lhs,
                         const AllowedAddressPair &rhs) const;
        bool IsLess(const AllowedAddressPair *rhs) const;
        void Activate(VmInterface *interface, bool force_update,
                      bool policy_change) const;
        void DeActivate(VmInterface *interface) const;

        mutable std::string vrf_;
        Ip4Address  addr_;
        uint32_t    plen_;
        bool        ecmp_;
        mutable Ip4Address  gw_ip_;
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
        const std::string vrf_name_;
        const VrfEntryRef vrf_;
        bool ignore_acl_;
        autogen::MatchConditionType match_condition_;
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

    enum Trace {
        ADD,
        DELETE,
        ACTIVATED_IPV4,
        ACTIVATED_L2,
        DEACTIVATED_IPV4,
        DEACTIVATED_L2,
        FLOATING_IP_CHANGE,
        SERVICE_CHANGE,
    };

    VmInterface(const boost::uuids::uuid &uuid);
    VmInterface(const boost::uuids::uuid &uuid, const std::string &name,
                const Ip4Address &addr, const std::string &mac,
                const std::string &vm_name,
                const boost::uuids::uuid &vm_project_uuid, uint16_t vlan_id,
                Interface *parent);
    virtual ~VmInterface();

    virtual bool CmpInterface(const DBEntry &rhs) const;
    virtual void GetOsParams(Agent *agent);
    void SendTrace(Trace ev);

    // DBEntry vectors
    KeyPtr GetDBRequestKey() const;
    std::string ToString() const;
    bool Resync(VmInterfaceData *data);
    bool OnChange(VmInterfaceData *data);

    // Accessor functions
    const VmEntry *vm() const { return vm_.get(); }
    const VnEntry *vn() const { return vn_.get(); }
    const MirrorEntry *mirror_entry() const { return mirror_entry_.get(); }
    const Ip4Address &ip_addr() const { return ip_addr_; }
    bool policy_enabled() const { return policy_enabled_; }
    const Ip4Address &subnet_bcast_addr() const { return subnet_bcast_addr_; }
    const Ip4Address &mdata_ip_addr() const { return mdata_addr_; }
    const std::string &vm_mac() const { return vm_mac_; }
    bool fabric_port() const { return fabric_port_; }
    bool need_linklocal_ip() const { return  need_linklocal_ip_; }
    bool dhcp_enable_config() const { return dhcp_enable_; }
    void set_dhcp_enable_config(bool dhcp_enable) {
        dhcp_enable_= dhcp_enable;
    }
    bool do_dhcp_relay() const { return do_dhcp_relay_; }
    int vxlan_id() const { return vxlan_id_; }
    bool layer2_forwarding() const { return layer2_forwarding_; }
    bool ipv4_forwarding() const { return ipv4_forwarding_; }
    const std::string &vm_name() const { return vm_name_; }
    const boost::uuids::uuid &vm_project_uuid() const { return vm_project_uuid_; }
    const std::string &cfg_name() const { return cfg_name_; }
    uint16_t vlan_id() const { return vlan_id_; }
    const Interface *parent() const { return parent_.get(); }
    bool ecmp() const { return ecmp_;}
    const OperDhcpOptions &oper_dhcp_options() const { return oper_dhcp_options_; }

    Interface::MirrorDirection mirror_direction() const {
        return mirror_direction_;
    }
    const FloatingIpList &floating_ip_list() const {
        return floating_ip_list_;
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
    void set_ip_addr(const Ip4Address &addr) { ip_addr_ = addr; }

    const std::string GetAnalyzer() const; 

    void CopySgIdList(SecurityGroupList *sg_id_list) const;
    bool NeedMplsLabel() const;
    bool IsVxlanMode() const;
    bool SgExists(const boost::uuids::uuid &id, const SgList &sg_l);
    bool IsMirrorEnabled() const { return mirror_entry_.get() != NULL; }
    bool HasFloatingIp() const { return floating_ip_list_.list_.size() != 0; }
    size_t GetFloatingIpCount() const { return floating_ip_list_.list_.size(); }
    bool HasServiceVlan() const { return service_vlan_list_.list_.size() != 0; }

    uint32_t GetServiceVlanLabel(const VrfEntry *vrf) const;
    uint32_t GetServiceVlanTag(const VrfEntry *vrf) const;
    const VrfEntry* GetServiceVlanVrf(uint16_t vlan_tag) const;
    void Delete();
    void Add();
    bool OnResyncServiceVlan(VmInterfaceConfigData *data);
    void UpdateAllRoutes();

    // Add a vm-interface
    static void Add(InterfaceTable *table,
                    const boost::uuids::uuid &intf_uuid,
                    const std::string &os_name, const Ip4Address &addr,
                    const std::string &mac, const std::string &vn_name,
                    const boost::uuids::uuid &vm_project_uuid,
                    uint16_t vlan_id_, const std::string &parent);
    // Del a vm-interface
    static void Delete(InterfaceTable *table,
                       const boost::uuids::uuid &intf_uuid);

    // Calback from configuration
    static void InstanceIpSync(InterfaceTable *table, IFMapNode *node);
    static void FloatingIpVnSync(InterfaceTable *table, IFMapNode *node);
    static void FloatingIpPoolSync(InterfaceTable *table, IFMapNode *node);
    static void FloatingIpSync(InterfaceTable *table, IFMapNode *node);
    static void FloatingIpVrfSync(InterfaceTable *table, IFMapNode *node);
    static void VnSync(InterfaceTable *table, IFMapNode *node);

    void AllocL2MplsLabel(bool force_update, bool policy_change);
    void DeleteL2MplsLabel();
    void AddL2Route();
    void UpdateL2();
    const AclDBEntry* vrf_assign_acl() const { return vrf_assign_acl_.get();}
    bool WaitForTraffic() const;
    bool GetInterfaceDhcpOptions(
            std::vector<autogen::DhcpOptionType> *options) const;
    bool GetSubnetDhcpOptions(
            std::vector<autogen::DhcpOptionType> *options) const;
    bool GetIpamDhcpOptions(
            std::vector<autogen::DhcpOptionType> *options) const;
    const Peer *peer() const;
private:
    bool IsActive() const;
    bool IsL3Active() const;
    bool IsL2Active() const;
    bool PolicyEnabled() const;
    void UpdateL3Services(bool dhcp, bool dns);
    void AddRoute(const std::string &vrf_name, const Ip4Address &ip,
                  uint32_t plen, const std::string &vn_name, bool policy,
                  bool ecmp, const Ip4Address &gw_ip);
    void DeleteRoute(const std::string &vrf_name, const Ip4Address &ip,
                     uint32_t plen);
    void ServiceVlanAdd(ServiceVlan &entry);
    void ServiceVlanDel(ServiceVlan &entry);
    void ServiceVlanRouteAdd(const ServiceVlan &entry);
    void ServiceVlanRouteDel(const ServiceVlan &entry);

    bool OnResyncFloatingIp(VmInterfaceConfigData *data, bool new_ipv4_active);
    bool OnResyncSecurityGroupList(VmInterfaceConfigData *data,
                                   bool new_ipv4_active);
    bool OnResyncStaticRoute(VmInterfaceConfigData *data, bool new_ipv4_active);
    bool ResyncMirror(VmInterfaceMirrorData *data);
    bool ResyncIpAddress(const VmInterfaceIpAddressData *data);
    bool ResyncOsOperState(const VmInterfaceOsOperStateData *data);
    bool ResyncConfig(VmInterfaceConfigData *data);
    bool CopyIpAddress(Ip4Address &addr);
    bool CopyConfig(VmInterfaceConfigData *data, bool *sg_changed,
                    bool *ecmp_changed);
    void ApplyConfig(bool old_ipv4_active,bool old_l2_active,  bool old_policy, 
                     VrfEntry *old_vrf, const Ip4Address &old_addr, 
                     int old_vxlan_id, bool old_need_linklocal_ip,
                     bool sg_changed, bool ecmp_changed);

    void UpdateL3(bool old_ipv4_active, VrfEntry *old_vrf,
                  const Ip4Address &old_addr, int old_vxlan_id,
                  bool force_update, bool policy_change);
    void DeleteL3(bool old_ipv4_active, VrfEntry *old_vrf,
                  const Ip4Address &old_addr, bool old_need_linklocal_ip);
    void UpdateL2(bool old_l2_active, VrfEntry *old_vrf, int old_vxlan_id,
                  bool force_update, bool policy_change);
    void DeleteL2(bool old_l2_active, VrfEntry *old_vrf);
    void UpdateVxLan();

    void AllocL3MplsLabel(bool force_update, bool policy_change);
    void DeleteL3MplsLabel();
    void UpdateL3TunnelId(bool force_update, bool policy_change);
    void DeleteL3TunnelId();
    void UpdateMulticastNextHop(bool old_ipv4_active, bool old_l2_active);
    void DeleteMulticastNextHop();
    void UpdateL2NextHop(bool old_l2_active);
    void DeleteL2NextHop(bool old_l2_active);
    void UpdateL3NextHop(bool old_ipv4_active);
    void DeleteL3NextHop(bool old_ipv4_active);
    bool L2Activated(bool old_l2_active);
    bool L3Activated(bool old_ipv4_active);
    bool L2Deactivated(bool old_l2_active);
    bool L3Deactivated(bool old_ipv4_active);
    void UpdateL3InterfaceRoute(bool old_ipv4_active, bool force_update,
                             bool policy_change, VrfEntry * old_vrf,
                             const Ip4Address &old_addr);
    void DeleteL3InterfaceRoute(bool old_ipv4_active, VrfEntry *old_vrf,
                                const Ip4Address &old_addr);
    void DeleteInterfaceNH();
    void UpdateMetadataRoute(bool old_ipv4_active, VrfEntry *old_vrf);
    void DeleteMetadataRoute(bool old_ipv4_active, VrfEntry *old_vrf,
                             bool old_need_linklocal_ip);
    void UpdateFloatingIp(bool force_update, bool policy_change);
    void DeleteFloatingIp();
    void UpdateServiceVlan(bool force_update, bool policy_change);
    void DeleteServiceVlan();
    void UpdateStaticRoute(bool force_update, bool policy_change);
    void DeleteStaticRoute();
    void UpdateAllowedAddressPair(bool force_update, bool policy_change);
    void DeleteAllowedAddressPair();
    void UpdateSecurityGroup();
    void DeleteSecurityGroup();
    void UpdateL2TunnelId(bool force_update, bool policy_change);
    void DeleteL2TunnelId();
    void UpdateL2InterfaceRoute(bool old_l2_active, bool force_update);
    void DeleteL2InterfaceRoute(bool old_l2_active, VrfEntry *old_vrf);

    void DeleteL2Route(const std::string &vrf_name,
                       const MacAddress &mac);
    void UpdateVrfAssignRule();
    void DeleteVrfAssignRule();

    VmEntryRef vm_;
    VnEntryRef vn_;
    Ip4Address ip_addr_;
    Ip4Address mdata_addr_;
    Ip4Address subnet_bcast_addr_;
    std::string vm_mac_;
    bool policy_enabled_;
    MirrorEntryRef mirror_entry_;
    Interface::MirrorDirection mirror_direction_;
    std::string cfg_name_;
    bool fabric_port_;
    bool need_linklocal_ip_;
    // DHCP flag - set according to the dhcp option in the ifmap subnet object.
    // It controls whether the vrouter sends the DHCP requests from VM interface
    // to agent or if it would flood the request in the VN.
    bool dhcp_enable_;
    // true if IP is to be obtained from DHCP Relay and not learnt from fabric
    bool do_dhcp_relay_;
    // VM-Name. Used by DNS
    std::string vm_name_;
    // project uuid of the vm to which the interface belongs
    boost::uuids::uuid vm_project_uuid_;
    int vxlan_id_;
    bool layer2_forwarding_;
    bool ipv4_forwarding_;
    bool mac_set_;
    bool ecmp_;
    // VLAN Tag and the parent interface when VLAN is enabled
    uint16_t vlan_id_;
    InterfaceRef parent_;
    // DHCP options defined for the interface
    OperDhcpOptions oper_dhcp_options_;

    // Lists
    SecurityGroupEntryList sg_list_;
    FloatingIpList floating_ip_list_;
    ServiceVlanList service_vlan_list_;
    StaticRouteList static_route_list_;
    AllowedAddressPairList allowed_address_pair_list_;

    // Peer for interface routes
    std::auto_ptr<LocalVmPortPeer> peer_;
    VrfAssignRuleList vrf_assign_rule_list_;
    AclDBEntryRef vrf_assign_acl_;
    Ip4Address vm_ip_gw_addr_;
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
// Request for VM-Interface data are of 3 types
// - ADD_DEL_CHANGE 
//   VM Interface are not added from IFMap config messages. In case of
//   Openstack, VM-Interfaces are added by a message from Nova.
//   Data of type ADD_DEL_CHANGE is used when interface is created
// - CONFIG
//   Data dervied from the IFMap configuration
// - MIRROR
//   Data for mirror enable/disable
// - IP_ADDR
//   In one of the modes, the IP address for an interface is not got from IFMap
//   or Nova. Agent will relay DHCP Request in such cases to IP Fabric network.
//   The IP address learnt with DHCP in this case is confgured with this type
//
/////////////////////////////////////////////////////////////////////////////
struct VmInterfaceData : public InterfaceData {
    enum Type {
        ADD_DEL_CHANGE,
        CONFIG,
        MIRROR,
        IP_ADDR,
        OS_OPER_STATE
    };

    VmInterfaceData(Type type) : InterfaceData(), type_(type) {
        VmPortInit();
    }
    virtual ~VmInterfaceData() { }

    Type type_;
};

// Structure used when type=ADD_DEL_CHANGE. Used for creating of Vm-Interface
struct VmInterfaceAddData : public VmInterfaceData {
    VmInterfaceAddData(const Ip4Address &ip_addr,
                       const std::string &vm_mac,
                       const std::string &vm_name,
                       const boost::uuids::uuid &vm_project_uuid,
                       const uint16_t vlan_id, const std::string &parent) :
        VmInterfaceData(ADD_DEL_CHANGE), ip_addr_(ip_addr), vm_mac_(vm_mac),
        vm_name_(vm_name), vm_project_uuid_(vm_project_uuid), vlan_id_(vlan_id),
        parent_(parent) {
    }

    virtual ~VmInterfaceAddData() { }

    Ip4Address ip_addr_;
    std::string vm_mac_;
    std::string vm_name_;
    boost::uuids::uuid vm_project_uuid_;
    uint16_t vlan_id_;
    std::string parent_;
};

// Structure used when type=IP_ADDR. Used to update IP-Address of VM-Interface
struct VmInterfaceIpAddressData : public VmInterfaceData {
    VmInterfaceIpAddressData() : VmInterfaceData(IP_ADDR) { }
    virtual ~VmInterfaceIpAddressData() { }
};

// Structure used when type=OS_OPER_STATE Used to update interface os oper-state
struct VmInterfaceOsOperStateData : public VmInterfaceData {
    VmInterfaceOsOperStateData() : VmInterfaceData(OS_OPER_STATE) { }
    virtual ~VmInterfaceOsOperStateData() { }
};

// Structure used when type=MIRROR. Used to update IP-Address of VM-Interface
struct VmInterfaceMirrorData : public VmInterfaceData {
    VmInterfaceMirrorData(bool mirror_enable, const std::string &analyzer_name):
        VmInterfaceData(MIRROR), mirror_enable_(mirror_enable),
        analyzer_name_(analyzer_name) {
    }

    bool mirror_enable_;
    std::string analyzer_name_;
};

// Definition for structures when request queued from IFMap config.
struct VmInterfaceConfigData : public VmInterfaceData {
    VmInterfaceConfigData() :
        VmInterfaceData(CONFIG), addr_(0), vm_mac_(""), cfg_name_(""),
        vm_uuid_(), vm_name_(), vn_uuid_(), vrf_name_(""), fabric_port_(true),
        need_linklocal_ip_(false), layer2_forwarding_(true),
        ipv4_forwarding_(true), mirror_enable_(false), ecmp_(false),
        dhcp_enable_(true), analyzer_name_(""), oper_dhcp_options_(),
        mirror_direction_(Interface::UNKNOWN), sg_list_(),
        floating_ip_list_(), service_vlan_list_(), static_route_list_(),
        allowed_address_pair_list_() {
    }

    VmInterfaceConfigData(const Ip4Address &addr, const std::string &mac,
                          const std::string &vm_name) :
        VmInterfaceData(CONFIG), addr_(addr), vm_mac_(mac), cfg_name_(""),
        vm_uuid_(), vm_name_(vm_name), vn_uuid_(), vrf_name_(""),
        fabric_port_(true), need_linklocal_ip_(false), 
        layer2_forwarding_(true), ipv4_forwarding_(true),
        mirror_enable_(false), ecmp_(false), dhcp_enable_(true),
        analyzer_name_(""), oper_dhcp_options_(), 
        mirror_direction_(Interface::UNKNOWN), sg_list_(),
        floating_ip_list_(), service_vlan_list_(), static_route_list_(),
        allowed_address_pair_list_(){
    }

    virtual ~VmInterfaceConfigData() { }

    Ip4Address addr_;
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
    bool layer2_forwarding_;
    bool ipv4_forwarding_;
    bool mirror_enable_;
    //Is interface in active-active mode or active-backup mode
    bool ecmp_;
    bool dhcp_enable_; // is DHCP enabled for the interface (from subnet config)
    bool admin_state_;
    std::string analyzer_name_;
    OperDhcpOptions oper_dhcp_options_;
    Interface::MirrorDirection mirror_direction_;
    VmInterface::SecurityGroupEntryList sg_list_;
    VmInterface::FloatingIpList floating_ip_list_;
    VmInterface::ServiceVlanList service_vlan_list_;
    VmInterface::StaticRouteList static_route_list_;
    VmInterface::VrfAssignRuleList vrf_assign_rule_list_;
    VmInterface::AllowedAddressPairList allowed_address_pair_list_;
};

#endif // vnsw_agent_vm_interface_hpp
