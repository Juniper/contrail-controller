/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_interface_hpp
#define vnsw_agent_vm_interface_hpp

/////////////////////////////////////////////////////////////////////////////
// Implementation of VM Port interfaces
/////////////////////////////////////////////////////////////////////////////
typedef vector<boost::uuids::uuid> SgUuidList;
typedef vector<SgEntryRef> SgList;
struct VmInterfaceData;
struct VmInterfaceConfigData;
struct VmInterfaceIpAddressData;

/////////////////////////////////////////////////////////////////////////////
// Definition for VmInterface
/////////////////////////////////////////////////////////////////////////////
class VmInterface : public Interface {
public:
    static const uint32_t kInvalidVlanId = 0xFFFF;
    struct FloatingIp {
        FloatingIp() : 
            floating_ip_(0), vrf_(NULL), vn_(NULL), installed_(false) {
        }

        FloatingIp(const Ip4Address &addr, VrfEntry *vrf, VnEntry *vn,
                   bool installed)
            : floating_ip_(addr), vrf_(vrf), vn_(vn), installed_(installed) {
        }

        virtual ~FloatingIp() { }

        bool operator() (const FloatingIp &lhs, const FloatingIp &rhs) {
            if (lhs.floating_ip_ != rhs.floating_ip_) {
                return lhs.floating_ip_ < rhs.floating_ip_;
            }

            if (lhs.vrf_.get() == NULL)
                return false;

            if (rhs.vrf_.get() == NULL)
                return true;

            return (lhs.vrf_.get()->GetName() < rhs.vrf_.get()->GetName());
        }

        Ip4Address floating_ip_;
        VrfEntryRef vrf_;
        VnEntryRef vn_;
        bool installed_;
    };
    typedef std::set<FloatingIp, FloatingIp> FloatingIpList;

    struct ServiceVlan {
        ServiceVlan() : 
            vrf_(NULL), addr_(0), plen_(0),
            tag_(0), installed_(false), smac_(), dmac_() {
        }
        ServiceVlan(const ServiceVlan &e) : 
            vrf_(e.vrf_), addr_(e.addr_), plen_(e.plen_),
            tag_(e.tag_), installed_(false), smac_(e.smac_), dmac_(e.dmac_) {
        }

        ServiceVlan(VrfEntry *vrf, const Ip4Address &addr, uint8_t plen,
                    uint16_t tag, const struct ether_addr &smac,
                    const struct ether_addr &dmac) :
            vrf_(vrf), addr_(addr), plen_(plen), tag_(tag), label_(0),
            installed_(false), smac_(smac), dmac_(dmac) {
        }
        ~ServiceVlan() { }

        VrfEntryRef vrf_;
        Ip4Address addr_;
        uint8_t plen_;
        uint16_t tag_;
        uint32_t label_;
        bool installed_;
        struct ether_addr smac_;
        struct ether_addr dmac_;
    };
    typedef std::map<int, ServiceVlan> ServiceVlanList;

    struct StaticRoute {
        StaticRoute(): vrf_(""), addr_(0), plen_(0) { }
        StaticRoute(const std::string &vrf, const Ip4Address &addr,
                    uint32_t plen) : 
            vrf_(vrf), addr_(addr), plen_(plen) { }
        ~StaticRoute() { }

         bool operator() (const StaticRoute &lhs, const StaticRoute &rhs) {
             if (lhs.vrf_ != rhs.vrf_) {
                 return lhs.vrf_ < rhs.vrf_;
             }

             return lhs.addr_ < rhs.addr_;
         }

         std::string vrf_;
         Ip4Address  addr_;
         uint32_t    plen_;
    };
    typedef std::set<StaticRoute, StaticRoute> StaticRouteList;

    enum Trace {
        ADD,
        DELETE,
        ACTIVATED,
        DEACTIVATED,
        FLOATING_IP_CHANGE,
        SERVICE_CHANGE,
    };

    VmInterface(const boost::uuids::uuid &uuid) :
        Interface(Interface::VM_INTERFACE, uuid, "", NULL),
        vm_(NULL), vn_(NULL), ip_addr_(0),
        mdata_addr_(0), subnet_bcast_addr_(0), vm_mac_(""),
        policy_enabled_(false), mirror_entry_(NULL),
        mirror_direction_(MIRROR_RX_TX), floating_iplist_(),
        service_vlan_list_(), static_route_list_(), cfg_name_(""),
        fabric_port_(true), need_linklocal_ip_(false), dhcp_snoop_ip_(false),
        vm_name_(), vxlan_id_(0), layer2_forwarding_(true),
        ipv4_forwarding_(true) { 
        active_ = false;
    }

    VmInterface(const boost::uuids::uuid &uuid, const std::string &name,
                const Ip4Address &addr, const std::string &mac,
                const std::string &vm_name) : 
        Interface(Interface::VM_INTERFACE, uuid, name, NULL),
        vm_(NULL), vn_(NULL), ip_addr_(addr),
        mdata_addr_(0), subnet_bcast_addr_(0), vm_mac_(mac),
        policy_enabled_(false), mirror_entry_(NULL),
        mirror_direction_(MIRROR_RX_TX), floating_iplist_(),
        service_vlan_list_(), static_route_list_(), cfg_name_(""),
        fabric_port_(true), need_linklocal_ip_(false), dhcp_snoop_ip_(false),
        vm_name_(vm_name), vxlan_id_(0), layer2_forwarding_(true),
        ipv4_forwarding_(true) {
        active_ = false;
    }

    virtual ~VmInterface() { }

    virtual bool CmpInterface(const DBEntry &rhs) const {
        const VmInterface &intf=static_cast<const VmInterface &>(rhs);
        return uuid_ < intf.uuid_;
    }
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
    bool dhcp_snoop_ip() const { return dhcp_snoop_ip_; }
    int vxlan_id() const { return vxlan_id_; }
    bool layer2_forwarding() const { return layer2_forwarding_; }
    bool ipv4_forwarding() const { return ipv4_forwarding_; }
    const std::string &vm_name() const { return vm_name_; }
    const SgList &sg_list() const { return sg_entry_l_; }
    const SgUuidList &sg_uuid_list() const { return sg_uuid_l_; }
    const std::string &cfg_name() const { return cfg_name_; }

    Interface::MirrorDirection mirror_direction() const {
        return mirror_direction_;
    }
    const FloatingIpList &floating_ip_list() const {
        return floating_iplist_;
    }
    const ServiceVlanList &service_vlan_list() const {
        return service_vlan_list_;
    }

    const StaticRouteList &static_route_list() const {
        return static_route_list_;
    }

    void set_sg_list(SecurityGroupList &sg_id_list) const;
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
    bool set_ip_addr(const Ip4Address &addr);

    const std::string GetAnalyzer() const; 
    bool IsDhcpSnoopIp(std::string &name, uint32_t &addr) const;
    bool SgExists(const boost::uuids::uuid &id, const SgList &sg_l);
    bool IsMirrorEnabled() const { return mirror_entry_.get() != NULL; }
    bool HasFloatingIp() const { return floating_iplist_.size() != 0; }
    size_t GetFloatingIpCount() const { return floating_iplist_.size(); }
    bool HasServiceVlan() const { return service_vlan_list_.size() != 0; }

    uint32_t GetServiceVlanLabel(const VrfEntry *vrf) const;
    uint32_t GetServiceVlanTag(const VrfEntry *vrf) const;
    const VrfEntry* GetServiceVlanVrf(uint16_t vlan_tag) const;
    void Activate();
    void DeActivate(const std::string &vrf_name, const Ip4Address &ip);
    bool OnResyncServiceVlan(VmInterfaceConfigData *data);
    void UpdateAllRoutes();

    // Add a vm-interface
    static void Add(InterfaceTable *table,
                    const boost::uuids::uuid &intf_uuid,
                    const std::string &os_name, const Ip4Address &addr,
                    const std::string &mac, const std::string &vn_name);
    // Del a vm-interface
    static void Delete(InterfaceTable *table,
                       const boost::uuids::uuid &intf_uuid);

    void AllocL2MPLSLabels();
    void AddL2Route();
    void DeleteL2Route(const std::string &vrf_name, 
                       const struct ether_addr &mac);

    // Calback from configuration
    static void InstanceIpSync(InterfaceTable *table, IFMapNode *node);
    static void FloatingIpVnSync(InterfaceTable *table, IFMapNode *node);
    static void FloatingIpPoolSync(InterfaceTable *table, IFMapNode *node);
    static void FloatingIpSync(InterfaceTable *table, IFMapNode *node);
    static void FloatingIpVrfSync(InterfaceTable *table, IFMapNode *node);
    static void VnSync(InterfaceTable *table, IFMapNode *node);

private:
    bool IsActive(VnEntry *vn, VrfEntry *vrf, VmEntry *vm,
                  bool ipv4_addr_compare);
    void AllocMPLSLabels();
    void ActivateServices();
    void DeActivateServices();
    void AddRoute(const std::string &vrf_name, const Ip4Address &ip,
                  uint32_t plen, bool policy);
    void DeleteRoute(const std::string &vrf_name, const Ip4Address &ip,
                     uint32_t plen);
    void ServiceVlanAdd(ServiceVlan &entry);
    void ServiceVlanDel(ServiceVlan &entry);
    void ServiceVlanRouteAdd(ServiceVlan &entry);
    void ServiceVlanRouteDel(ServiceVlan &entry);

    bool OnResyncFloatingIp(VmInterfaceConfigData *data, bool new_active);
    bool OnResyncSecurityGroupList(VmInterfaceConfigData *data,
                                   bool new_active);
    bool OnResyncStaticRoute(VmInterfaceConfigData *data, bool new_active);
    bool ResyncConfig(VmInterfaceConfigData *data);
    bool ResyncIpAddress(const VmInterfaceIpAddressData *data);

    VmEntryRef vm_;
    VnEntryRef vn_;
    Ip4Address ip_addr_;
    Ip4Address mdata_addr_;
    Ip4Address subnet_bcast_addr_;
    std::string vm_mac_;
    bool policy_enabled_;
    MirrorEntryRef mirror_entry_;
    Interface::MirrorDirection mirror_direction_;
    FloatingIpList floating_iplist_;
    ServiceVlanList service_vlan_list_;
    StaticRouteList static_route_list_;
    std::string cfg_name_;
    bool fabric_port_;
    bool need_linklocal_ip_;
    // true if IP is obtained from snooping DHCP from fabric, not from config
    bool dhcp_snoop_ip_; 
    // vector of SgEntry
    SgList sg_entry_l_;
    // vector of SgUuid
    SgUuidList sg_uuid_l_;
    // VM-Name. Used by DNS
    std::string vm_name_;
    int vxlan_id_;
    bool layer2_forwarding_;
    bool ipv4_forwarding_;
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
// Request for VM-Data are of 3 types
// - ADD_DEL_CHANGE 
//   VM Interface are not added from IFMap config messages. In case of
//   Openstack, VM-Interfaces are added by a message from Nova.
//   Data of type ADD_DEL_CHANGE is used when interface is created
// - CONFIG
//   Data dervied from the IFMap configuration
// - IP_ADDR
//   In one of the modes, the IP address for an interface is not got from IFMap
//   or Nova. Agent will relay DHCP Request in such cases to IP Fabric network.
//   The IP address learnt with DHCP in this case is confgured with this type
/////////////////////////////////////////////////////////////////////////////
struct VmInterfaceData : public InterfaceData {
    enum Type {
        ADD_DEL_CHANGE,
        CONFIG,
        IP_ADDR
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
                       const std::string &vm_name) :
        VmInterfaceData(ADD_DEL_CHANGE), ip_addr_(ip_addr), vm_mac_(vm_mac),
        vm_name_(vm_name) {
    }

    virtual ~VmInterfaceAddData() { }

    Ip4Address ip_addr_;
    std::string vm_mac_;
    std::string vm_name_;
};

// Structure used when type=IP_ADDR. Used to update IP-Address of VM-Interface
struct VmInterfaceIpAddressData : public VmInterfaceData {
    VmInterfaceIpAddressData(const Ip4Address &ip_addr) :
        VmInterfaceData(IP_ADDR), ip_addr_(ip_addr) { }
    virtual ~VmInterfaceIpAddressData() { }

    Ip4Address ip_addr_;
};

/////////////////////////////////////////////////////////////////////////////
// Definition for structures when request queued from IFMap config.
/////////////////////////////////////////////////////////////////////////////
// Config structure for floating-ip
struct FloatingIpConfig {
    FloatingIpConfig(const Ip4Address &addr, const std::string &vrf,
                  const boost::uuids::uuid &vn_uuid) 
        : addr_(addr), vrf_(vrf), vn_uuid_(vn_uuid) { 
    }
    ~FloatingIpConfig() { }

    bool operator< (const FloatingIpConfig &rhs) const{
        if (addr_ != rhs.addr_) {
            return addr_ < rhs.addr_;
        }

        return vrf_ < rhs.vrf_;
    }

    // Floating-ip address configured
    Ip4Address addr_;
    // VRF and VN from where foating-ip is borrowed
    std::string vrf_;
    boost::uuids::uuid vn_uuid_;
};
typedef std::set<FloatingIpConfig> FloatingIpConfigList;

// Config structure for service-vlan used in service-chain
struct ServiceVlanConfig {
    ServiceVlanConfig() : tag_(0), addr_(0), vrf_(""), smac_(), dmac_() { }
    ServiceVlanConfig(const ServiceVlanConfig &e) :
        tag_(e.tag_), addr_(e.addr_), vrf_(e.vrf_), smac_(e.smac_), 
        dmac_(e.dmac_){ 
    }
    ServiceVlanConfig(uint16_t tag, const Ip4Address &addr,
                   const std::string &vrf, const struct ether_addr &smac,
                   const struct ether_addr &dmac) :
        tag_(tag), addr_(addr), vrf_(vrf), smac_(smac), dmac_(dmac) { 
    }
    ~ServiceVlanConfig() { }

    uint16_t tag_;
    Ip4Address addr_;
    std::string vrf_;
    struct ether_addr smac_;
    struct ether_addr dmac_;
};
typedef std::map<int, ServiceVlanConfig> ServiceVlanConfigList;

// Config structure for interface static routes
struct StaticRouteConfig {
    StaticRouteConfig() : vrf_(""), addr_(0), plen_(0) { }
    StaticRouteConfig(const std::string &vrf, const Ip4Address &addr,
                   uint32_t plen): 
        vrf_(vrf), addr_(addr), plen_(plen) { 
    }
    ~StaticRouteConfig() { }

    bool operator< (const StaticRouteConfig &rhs) const{
        if (addr_ != rhs.addr_) {
            return addr_ < rhs.addr_;
        }

        if (plen_ != rhs.plen_) {
            return plen_ < rhs.plen_;
        }

        return vrf_ < rhs.vrf_;
    }

    std::string vrf_;
    Ip4Address addr_;   
    uint32_t plen_;
};
typedef std::set<StaticRouteConfig> StaticRouteConfigList;

struct VmInterfaceConfigData : public VmInterfaceData {
    VmInterfaceConfigData() : 
        VmInterfaceData(CONFIG), addr_(0), vm_mac_(""), cfg_name_(""),
        vm_uuid_(), vm_name_(), vn_uuid_(), vrf_name_(""), floating_iplist_(),
        fabric_port_(true), need_linklocal_ip_(false), mirror_enable_(false),
        layer2_forwarding_(true), ipv4_forwarding_(true), analyzer_name_(""),
        mirror_direction_(Interface::UNKNOWN), sg_uuid_l_() { 
    }

    VmInterfaceConfigData(const Ip4Address &addr, const std::string &mac,
                    const std::string &vm_name) :
        VmInterfaceData(CONFIG), addr_(addr), vm_mac_(mac), cfg_name_(""),
        vm_uuid_(), vm_name_(vm_name), vn_uuid_(), vrf_name_(""),
        floating_iplist_(), fabric_port_(true), need_linklocal_ip_(false),
        mirror_enable_(false), layer2_forwarding_(true),
        ipv4_forwarding_(true), analyzer_name_(""),
        mirror_direction_(Interface::UNKNOWN), sg_uuid_l_() { 
    }

    VmInterfaceConfigData(bool mirror_enable, const std::string &analyzer_name):
        VmInterfaceData(CONFIG), addr_(0), vm_mac_(""), cfg_name_(""),
        vm_uuid_(), vm_name_(), vn_uuid_(), vrf_name_(""), floating_iplist_(),
        fabric_port_(true), need_linklocal_ip_(false),
        mirror_enable_(mirror_enable), layer2_forwarding_(true),
        ipv4_forwarding_(true), analyzer_name_(analyzer_name),
        mirror_direction_(Interface::UNKNOWN), sg_uuid_l_() { 
    }

    virtual ~VmInterfaceConfigData() { }

    Ip4Address addr_;
    std::string vm_mac_;
    std::string cfg_name_;
    boost::uuids::uuid vm_uuid_;
    std::string vm_name_;
    boost::uuids::uuid vn_uuid_;
    std::string vrf_name_;
    FloatingIpConfigList floating_iplist_;
    ServiceVlanConfigList service_vlan_list_;
    StaticRouteConfigList static_route_list_;
    // Is this port on IP Fabric
    bool fabric_port_;
    // Does the port need link-local IP to be allocated
    bool need_linklocal_ip_;
    bool mirror_enable_;
    bool layer2_forwarding_;
    bool ipv4_forwarding_;
    std::string analyzer_name_;
    Interface::MirrorDirection mirror_direction_;
    SgUuidList sg_uuid_l_;
};

#endif // vnsw_agent_vm_interface_hpp
