/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_intf_hpp
#define vnsw_agent_intf_hpp

#include <net/ethernet.h>
#include <cmn/agent_cmn.h>
#include <oper/vrf.h>
#include <oper/vm.h>
#include <oper/vn.h>
#include <oper/sg.h>
#include <oper/agent_types.h>

using namespace boost::uuids;
using namespace std;

typedef vector<uuid> SgUuidList;
typedef vector<SgEntryRef> SgList;

/////////////////////////////////////////////////////////////////////////////
// Base class for Interfaces. Implementation of specific interfaces must 
// derive from this class
/////////////////////////////////////////////////////////////////////////////
class Interface : AgentRefCount<Interface>, public AgentDBEntry {
public:
    enum Type {
        INVALID,
        ETH,
        VMPORT,
        VHOST,
        HOST
    };

    enum Trace {
        ADD,
        DELETE,
        ACTIVATED,
        DEACTIVATED,
        FLOATING_IP_CHANGE,
        SERVICE_CHANGE,
    };

    enum MirrorDirection {
        MIRROR_RX_TX,
        MIRROR_RX,
        MIRROR_TX,
        UNKNOWN,
    };

    static const uint32_t kInvalidIndex = 0xFFFFFFFF;

    Interface(Type type, const uuid &uuid, const string &name, VrfEntry *vrf);
    virtual ~Interface();

    // Implement comparator since key is common for all interfaces
    bool IsLess(const DBEntry &rhs) const {
        const Interface &intf = static_cast<const Interface &>(rhs);
        if (type_ != intf.type_) {
            return type_ < intf.type_;
        }

        return CmpInterface(rhs);
    };
    virtual bool CmpInterface(const DBEntry &rhs) const = 0;
    virtual void SetKey(const DBRequestKey *key);
    virtual KeyPtr GetDBRequestKey() const = 0;
    virtual uint32_t GetVrfId() const;
    virtual void SendTrace(Trace event);
    virtual void Add() { };

    AgentDBTable *DBToTable() const;
    Type GetType() const {return type_;};
    const uuid &GetUuid() const {return uuid_;};
    const string &GetName() const {return name_;};
    VrfEntry *GetVrf() const {return vrf_.get();};
    bool GetActiveState() const {return active_;};
    const uint32_t GetInterfaceId() const {return id_;};
    bool IsDhcpServiceEnabled() const {return dhcp_service_enabled_;};
    bool IsDnsServiceEnabled() const {return dns_service_enabled_;};
    bool IsTunnelEnabled() const { return (type_ == ETH);};
    bool IsLabelValid(uint32_t label) const { return (label_ == label);};
    uint32_t GetLabel() const {return label_;};
    bool IsL2LabelValid(uint32_t label) const { return (label_ == label);};
    uint32_t GetL2Label() const {return label_;};
    uint32_t GetOsIfindex() const {return os_index_;};
    const ether_addr &GetMacAddr() const {return mac_;};

    uint32_t GetRefCount() const {
        return AgentRefCount<Interface>::GetRefCount();
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;

    static void SetTestMode(bool mode) {test_mode_ = mode;};
    static bool GetTestMode() {return test_mode_;};

protected:
    void SetItfSandeshData(ItfSandeshData &data) const;
    void SetActiveState(bool active) {active_ = active;};

    Type type_;
    uuid uuid_;
    string name_;
    VrfEntryRef vrf_;
    uint32_t label_;
    uint32_t l2_label_;
    bool active_;
    size_t id_;
    bool dhcp_service_enabled_;
    bool dns_service_enabled_;
    struct ether_addr mac_;
    size_t os_index_;
    static bool test_mode_;

private:
    friend class InterfaceTable;
    DISALLOW_COPY_AND_ASSIGN(Interface);
};

struct InterfaceData;

struct InterfaceKey : public AgentKey {
    InterfaceKey(const InterfaceKey &rhs) {
        type_ = rhs.type_;
        uuid_ = rhs.uuid_;
        name_ = rhs.name_;
        is_mcast_nh_ = rhs.is_mcast_nh_;
    }

    InterfaceKey(Interface::Type type, const uuid &uuid, const string &name)
                  : AgentKey(), type_(type), uuid_(uuid), 
                 name_(name), is_mcast_nh_(false) { };

    InterfaceKey(Interface::Type type, const uuid &uuid, const string &name,
                 bool is_mcast) : AgentKey(), type_(type), uuid_(uuid), 
                 name_(name), is_mcast_nh_(is_mcast) { };

    InterfaceKey(AgentKey::DBSubOperation sub_op, Interface::Type type, 
                 const uuid &uuid, const string &name) : 
        AgentKey(sub_op), type_(type), uuid_(uuid), name_(name) { };

    void Init(Interface::Type type, const uuid &intf_uuid,
              const string &name) {
        type_ = type;
        uuid_ = intf_uuid;
        name_ = name;
        is_mcast_nh_ = false;
    }

    void Init(Interface::Type type, const uuid &intf_uuid,
              const string &name, bool is_mcast_nh) {
        type_ = type;
        uuid_ = intf_uuid;
        name_ = name;
        is_mcast_nh_ = is_mcast_nh;
    }

    bool Compare(const InterfaceKey &rhs) const {
        if (type_ != rhs.type_)
            return false;

        if (uuid_ != rhs.uuid_)
            return false;

        if (name_ != rhs.name_)
            return false;

        return is_mcast_nh_ == rhs.is_mcast_nh_;
    }

    virtual Interface *AllocEntry() const = 0;
    virtual Interface *AllocEntry(const InterfaceData *data) const = 0;
    virtual InterfaceKey *Clone() const = 0;

    Interface::Type type_;
    uuid uuid_;
    string name_;
    bool is_mcast_nh_;
};

struct InterfaceData : public AgentData {
    InterfaceData() : AgentData() { };
    // Following fields are constant-data. They are set only during create
    // Change does not modify this
    string vrf_name_;

    void VmPortInit() {
        vrf_name_ = "";
    }

    void EthInit(const string &vrf_name) {
        vrf_name_ = vrf_name;
    }

    void HostInit() {
        vrf_name_ = "";
    }

    void VirtualHostInit(const string &vrf_name) {
        vrf_name_ = vrf_name;
    }
};

/////////////////////////////////////////////////////////////////////////////
// Implementation of VM Port interfaces
/////////////////////////////////////////////////////////////////////////////
struct CfgFloatingIp {
    CfgFloatingIp(const Ip4Address &addr, const string &vrf,
                  const uuid vn_uuid) 
        : addr_(addr), vrf_(vrf), vn_uuid_(vn_uuid) { };
    ~CfgFloatingIp() { };
    bool operator< (const CfgFloatingIp &rhs) const{
        if (addr_ != rhs.addr_) {
            return addr_ < rhs.addr_;
        }

        return vrf_ < rhs.vrf_;
    }

    Ip4Address addr_;
    string vrf_;
    uuid vn_uuid_;
};
typedef std::set<CfgFloatingIp> CfgFloatingIpList;

struct CfgServiceVlan {
    CfgServiceVlan() : tag_(0), addr_(0), vrf_(""), smac_(), dmac_() { };
    CfgServiceVlan(const CfgServiceVlan &e) : 
        tag_(e.tag_), addr_(e.addr_), vrf_(e.vrf_), smac_(e.smac_), 
        dmac_(e.dmac_){ };
    CfgServiceVlan(uint16_t tag, const Ip4Address &addr, const string &vrf, 
                   const struct ether_addr &smac, const struct ether_addr &dmac):
        tag_(tag), addr_(addr), vrf_(vrf), smac_(smac), dmac_(dmac) { };
    ~CfgServiceVlan() { };

    uint16_t tag_;
    Ip4Address addr_;
    string vrf_;
    struct ether_addr smac_;
    struct ether_addr dmac_;
};
typedef std::map<int, CfgServiceVlan> CfgServiceVlanList;

struct VmPortInterfaceData : public InterfaceData {
    VmPortInterfaceData() : 
        InterfaceData(), addr_(0), vm_mac_(""), cfg_name_(""), vm_uuid_(), vm_name_(), 
        vn_uuid_(), vrf_name_(""), floating_iplist_(), fabric_port_(true),
        need_linklocal_ip_(false), mirror_enable_(false), ip_addr_update_only_(false),
        analyzer_name_(""), mirror_direction_(Interface::UNKNOWN), sg_uuid_l_() { };
    VmPortInterfaceData(const Ip4Address addr, const string &mac, const string vm_name) :
            InterfaceData(), addr_(addr), vm_mac_(mac), cfg_name_(""),
            vm_uuid_(), vm_name_(vm_name), vn_uuid_(), vrf_name_(""), floating_iplist_(),
            fabric_port_(true), need_linklocal_ip_(false), mirror_enable_(false), 
            ip_addr_update_only_(false), analyzer_name_(""), 
            mirror_direction_(Interface::UNKNOWN), sg_uuid_l_() { };
    VmPortInterfaceData(bool mirror_enable, const string &analyzer_name) : 
            mirror_enable_(mirror_enable), ip_addr_update_only_(false),
            analyzer_name_(analyzer_name), mirror_direction_(Interface::UNKNOWN) { };
    virtual ~VmPortInterfaceData() { };
    void SetFabricPort(bool val) {fabric_port_ = val;};
    void SetNeedLinkLocalIp(bool val) {need_linklocal_ip_ = val;};
    void SetSgUuidList(SgUuidList &sg_uuid_l) {sg_uuid_l_ = sg_uuid_l;};

    /* Nova data items */
    Ip4Address addr_;
    string vm_mac_;

    /* Config data items */
    string cfg_name_;
    uuid vm_uuid_;
    string vm_name_;
    uuid vn_uuid_;
    string vrf_name_;
    CfgFloatingIpList floating_iplist_;
    CfgServiceVlanList service_vlan_list_;
    bool fabric_port_;
    bool need_linklocal_ip_;
    bool mirror_enable_;
    bool ip_addr_update_only_;        // set to true if only IP address is changing in the data
    string analyzer_name_;
    Interface::MirrorDirection mirror_direction_;
    SgUuidList sg_uuid_l_;
};

class VmPortInterface : public Interface {
public:
    struct FloatingIp {
        FloatingIp() : floating_ip_(0), vrf_(NULL), vn_(NULL), installed_(false) { };
        FloatingIp(const Ip4Address &addr, VrfEntry *vrf, VnEntry *vn,
                   bool installed)
            : floating_ip_(addr), vrf_(vrf), vn_(vn), installed_(installed) { };
        ~FloatingIp() { };

        bool operator() (const FloatingIp &lhs, const FloatingIp &rhs) {
            if (lhs.floating_ip_ != rhs.floating_ip_) {
                return lhs.floating_ip_ < rhs.floating_ip_;
            }

            if (lhs.vrf_.get() == NULL)
                return false;

            if (rhs.vrf_.get() == NULL)
                return true;

            return (lhs.vrf_.get()->GetName() < rhs.vrf_.get()->GetName());
        };

        Ip4Address floating_ip_;
        VrfEntryRef vrf_;
        VnEntryRef vn_;
        bool installed_;
    };
    typedef std::set<FloatingIp, FloatingIp> FloatingIpList;

    struct ServiceVlan {
        ServiceVlan() : vrf_(NULL), addr_(0), tag_(0), installed_(false) { };
        ServiceVlan(const ServiceVlan &e) : 
            vrf_(e.vrf_), addr_(e.addr_), tag_(e.tag_), installed_(false),
            smac_(e.smac_), dmac_(e.dmac_) { };
        ServiceVlan(VrfEntry *vrf, const Ip4Address &addr, uint16_t tag, 
                    const struct ether_addr &smac, const struct ether_addr &dmac) :
            vrf_(vrf), addr_(addr), tag_(tag), label_(0), installed_(false),
            smac_(smac), dmac_(dmac) { };
        ~ServiceVlan() { };

        VrfEntryRef vrf_;
        Ip4Address addr_;
        uint16_t tag_;
        uint32_t label_;
        bool installed_;
        struct ether_addr smac_;
        struct ether_addr dmac_;
    };
    typedef std::map<int, ServiceVlan> ServiceVlanList;

    enum Trace {
        ADD,
        DELETE,
        ACTIVATED,
        DEACTIVATED,
        FLOATING_IP_CHANGE,
        SERVICE_CHANGE,
    };

    VmPortInterface(const uuid &uuid) :
        Interface(Interface::VMPORT, uuid, "", NULL), vm_(NULL), vn_(NULL),
        addr_(0), mdata_addr_(0), subnet_bcast_addr_(0), vm_mac_(""), 
        policy_enabled_(false), mirror_entry_(NULL), mirror_direction_(MIRROR_RX_TX),
        floating_iplist_(), service_vlan_list_(), 
        cfg_name_(""), fabric_port_(true), alloc_linklocal_ip_(false), 
        dhcp_snoop_ip_(false), vm_name_(), vxlan_id_(0) { 
        SetActiveState(false);
    };
    VmPortInterface(const uuid &uuid, const string &name, Ip4Address addr,
                    const string &mac, const string vm_name) :
        Interface(Interface::VMPORT, uuid, name, NULL), vm_(NULL), vn_(NULL),
        addr_(addr), mdata_addr_(0), subnet_bcast_addr_(0), vm_mac_(mac), 
        policy_enabled_(false), mirror_entry_(NULL), mirror_direction_(MIRROR_RX_TX),
        floating_iplist_(), service_vlan_list_(), 
        cfg_name_(""), fabric_port_(true), alloc_linklocal_ip_(false),
        dhcp_snoop_ip_(false), vm_name_(vm_name), vxlan_id_(0) { 
        SetActiveState(false);
    };
    virtual ~VmPortInterface() { };

    virtual bool CmpInterface(const DBEntry &rhs) const {
        const VmPortInterface &intf=static_cast<const VmPortInterface &>(rhs);
        return uuid_ < intf.uuid_;
    };

    KeyPtr GetDBRequestKey() const;
    bool OnResync(const DBRequest *req);
    bool OnIpAddrResync(const DBRequest *req);

    virtual string ToString() const;
    const VmEntry *GetVmEntry() const {return vm_.get();};
    const VnEntry *GetVnEntry() const {return vn_.get();};
    const MirrorEntry *GetMirrorEntry() const {return mirror_entry_.get();};
    Interface::MirrorDirection GetMirrorDirection() const {return mirror_direction_;};
    const string GetAnalyzer() const; 
    const Ip4Address &GetIpAddr() const {return addr_;};
    void SetIpAddr(const Ip4Address &addr) { addr_ = addr; };
    const Ip4Address &GetMdataIpAddr() const {return mdata_addr_;};
    const Ip4Address &GetSubnetBroadcastAddr() const {
        return subnet_bcast_addr_;
    };
    void SetSubnetBroadcastAddr(const Ip4Address &addr) {
        subnet_bcast_addr_ = addr;
    };
    const string &GetVmMacAddr() const {return vm_mac_;};
    bool IsPolicyEnabled() const {return policy_enabled_;};
    bool IsMirrorEnabled() const { return (mirror_entry_.get()) ? true:false;};
    void SetMirrorEntry (MirrorEntry *mirror_entry) {
        mirror_entry_ = mirror_entry;};
    void SetMirrorDirection(Interface::MirrorDirection mirror_direction) {
        mirror_direction_ = mirror_direction;
    }
    const string &GetCfgName() const;

    bool HasFloatingIp() const {return floating_iplist_.size()? true : false;};
    size_t GetFloatingIpCount() const {return floating_iplist_.size();}
    const FloatingIpList &GetFloatingIpList() const {
        return floating_iplist_;
    };

    bool HasServiceVlan() const {return service_vlan_list_.size()? true:false;};
    const ServiceVlanList &GetServiceVlanList() const {
        return service_vlan_list_;
    };

    uint32_t GetServiceVlanLabel(const VrfEntry *vrf) const {
        ServiceVlanList::const_iterator vlan_it = service_vlan_list_.begin();
        while (vlan_it != service_vlan_list_.end()) {
            if (vlan_it->second.vrf_.get() == vrf) {
                return vlan_it->second.label_;
            }
            vlan_it++;
        }
        //Default return native vrf
        return label_;
    }

    uint32_t GetServiceVlanTag(const VrfEntry *vrf) const {
        ServiceVlanList::const_iterator vlan_it = service_vlan_list_.begin();
        while (vlan_it != service_vlan_list_.end()) {
            if (vlan_it->second.vrf_.get() == vrf) {
                return vlan_it->second.tag_;
            }
            vlan_it++;
        }
        return 0;
    }

    const VrfEntry* GetServiceVlanVrf(uint16_t vlan_tag) const {
        ServiceVlanList::const_iterator vlan_it = service_vlan_list_.begin();
        while (vlan_it != service_vlan_list_.end()) {
            if (vlan_it->second.tag_ == vlan_tag) {
                return vlan_it->second.vrf_.get();
            }
            vlan_it++;
        }
        return NULL;
    }

    bool IsFabricPort() const {return fabric_port_;};
    bool NeedLinkLocalIp() const {return  alloc_linklocal_ip_;};
    bool IsDhcpSnoopIp() const {return dhcp_snoop_ip_;};
    bool IsDhcpSnoopIp(std::string &name, uint32_t &addr) const;
    bool SgExists(const uuid &id, const SgList &sg_l);
    void SetVxLanId(int vxlan_id) {vxlan_id_ = vxlan_id;};
    int GetVxLanId() const {return vxlan_id_;};

    void Activate();
    void DeActivate(const string &vrf_name, const Ip4Address &ip);
    bool OnResyncFloatingIp(VmPortInterfaceData *data, bool new_active);
    bool OnResyncSecurityGroupList(VmPortInterfaceData *data, bool new_active);
    bool OnResyncServiceVlan(VmPortInterfaceData *data);
    void UpdateAllRoutes();
    virtual void SendTrace(Trace ev);

    // Nova VM-Port message
    static void NovaMsg(const uuid &intf_uuid, const string &os_name,
                        const Ip4Address &addr, const string &mac,
                        const string &vn_name);
    // Config VM-Port delete message
    static void NovaDel(const uuid &intf_uuid);

    static void InstanceIpSync(IFMapNode *node);
    static void FloatingIpVnSync(IFMapNode *node);
    static void FloatingIpPoolSync(IFMapNode *node);
    static void FloatingIpSync(IFMapNode *node);
    static void FloatingIpVrfSync(IFMapNode *node);
    void AddRoute(std::string vrf_name, Ip4Address ip, bool policy);
    void AddL2Route(const std::string vrf_name, 
                    struct ether_addr mac, const Ip4Address &ip, 
                    bool policy);
    void DeleteRoute(std::string vrf_name, Ip4Address ip, bool policy);
    void DeleteL2Route(const std::string vrf_name, 
                       struct ether_addr mac);
    void ServiceVlanAdd(ServiceVlan &entry);
    void ServiceVlanDel(ServiceVlan &entry);
    void ServiceVlanRouteAdd(ServiceVlan &entry);
    void ServiceVlanRouteDel(ServiceVlan &entry);
    const SgList &GetSecurityGroupList() const {return sg_entry_l_;};
    const SgUuidList &GetSecurityGroupUuidList() const {return sg_uuid_l_;};
    void SgIdList(SecurityGroupList &sg_id_list) const;
    const string &GetVmName() const {return vm_name_;};
private:
    inline bool IsActive(VnEntry *vn, VrfEntry *vrf, VmEntry *vm);
    inline bool SetIpAddress(Ip4Address &addr);

    VmEntryRef vm_;
    VnEntryRef vn_;
    Ip4Address addr_;
    Ip4Address mdata_addr_;
    Ip4Address subnet_bcast_addr_;
    string vm_mac_;
    bool policy_enabled_;
    MirrorEntryRef mirror_entry_;
    Interface::MirrorDirection mirror_direction_;
    FloatingIpList floating_iplist_;
    ServiceVlanList service_vlan_list_;
    string cfg_name_;
    bool fabric_port_;
    bool alloc_linklocal_ip_;
    bool dhcp_snoop_ip_; // true if the IP is obtained from snooping DHCP from fabric, not from config
    // vector of SgEntry
    SgList sg_entry_l_;
    // vector of SgUuid
    SgUuidList sg_uuid_l_;
    string vm_name_;
    int vxlan_id_;
    void AllocMPLSLabels();
    void AllocL2Labels(int old_vxlan_id);
    DISALLOW_COPY_AND_ASSIGN(VmPortInterface);
};

struct VmPortInterfaceKey : public InterfaceKey {
    VmPortInterfaceKey(const uuid &uuid, const string &name) :
        InterfaceKey(Interface::VMPORT, uuid, name) {};
    VmPortInterfaceKey(AgentKey::DBSubOperation sub_op, const uuid &uuid, const string &name) :
        InterfaceKey(sub_op, Interface::VMPORT, uuid, name) {};

    virtual ~VmPortInterfaceKey() {};
    Interface *AllocEntry() const { return new VmPortInterface(uuid_);};
    Interface *AllocEntry(const InterfaceData *data) const {
        const VmPortInterfaceData *vm_data = static_cast<const VmPortInterfaceData *>(data);
        return new VmPortInterface(uuid_, name_, vm_data->addr_,
                                   vm_data->vm_mac_, vm_data->vm_name_);
    };
    InterfaceKey *Clone() const {
        return new VmPortInterfaceKey(*this);
    };
};

/////////////////////////////////////////////////////////////////////////////
// Implementation of Ethernet Ports
/////////////////////////////////////////////////////////////////////////////
struct EthInterfaceData : public InterfaceData {
    EthInterfaceData() : InterfaceData() { };
};

class EthInterface : public Interface {
public:
    EthInterface(const string &name) :
        Interface(Interface::ETH, nil_uuid(), name, NULL) { };
    EthInterface(const uuid &uuid, const string &name, VrfEntry *vrf) :
        Interface(Interface::ETH, uuid, name, vrf) { };
    virtual ~EthInterface() { };

    virtual bool CmpInterface(const DBEntry &rhs) const {
        const EthInterface &intf = static_cast<const EthInterface &>(rhs);
        return name_ < intf.name_;
    };

    KeyPtr GetDBRequestKey() const;
    virtual string ToString() const {return "ETH";};
    static void CreateReq(const string &ifname, const string &vrf_name);
    // Enqueue DBRequest to delete a Host Interface
    static void DeleteReq(const string &ifname);
private:
    DISALLOW_COPY_AND_ASSIGN(EthInterface);
};

struct EthInterfaceKey : public InterfaceKey {
    EthInterfaceKey(const uuid &uuid, const string &name) :
        InterfaceKey(Interface::ETH, uuid, name) {};

    virtual ~EthInterfaceKey() {};
    Interface *AllocEntry() const { return new EthInterface(name_);};
    Interface *AllocEntry(const InterfaceData *data) const {
        VrfKey key(data->vrf_name_);
        VrfEntry *vrf =
           static_cast<VrfEntry *>(Agent::GetInstance()->GetVrfTable()->FindActiveEntry(&key));
        if (vrf == NULL) {
            LOG(DEBUG, "Interface : VRF not found");
            return NULL;
        }

        return new EthInterface(uuid_, name_, vrf);
    };
    InterfaceKey *Clone() const {
        return new EthInterfaceKey(*this);
    };
};

/////////////////////////////////////////////////////////////////////////////
// Implementation of Host Interface from VNSW Router to Agent
/////////////////////////////////////////////////////////////////////////////
struct HostInterfaceData : public InterfaceData {
    HostInterfaceData() : InterfaceData() { };
};

class HostInterface : public Interface {
public:
    HostInterface(const string &name) : 
        Interface(Interface::HOST, nil_uuid(), name, NULL) { };
    virtual ~HostInterface() { };

    virtual bool CmpInterface(const DBEntry &rhs) const {
        return false;
    };

    KeyPtr GetDBRequestKey() const;
    virtual string ToString() const {return "HOST";};
    virtual uint32_t GetVrfId() const {return VrfEntry::kInvalidIndex;};
   
    // Enqueue DBRequest to create a Host Interface
    static void CreateReq(const string &ifname);
    // Enqueue DBRequest to delete a Host Interface
    static void DeleteReq(const string &ifname);
private:
    DISALLOW_COPY_AND_ASSIGN(HostInterface);
};

struct HostInterfaceKey : public InterfaceKey {
    HostInterfaceKey(const uuid &uuid, const string &name) :
        InterfaceKey(Interface::HOST, uuid, name) {};

    virtual ~HostInterfaceKey() {};
    Interface *AllocEntry() const { return new HostInterface(name_);};
    Interface *AllocEntry(const InterfaceData *data) const {
        return new HostInterface(name_);
    };
    InterfaceKey *Clone() const {
        return new HostInterfaceKey(*this);
    };
};

/////////////////////////////////////////////////////////////////////////////
// Implementation of Virtual Host Interface from VNSW Router to Host OS
/////////////////////////////////////////////////////////////////////////////
class VirtualHostInterface : public Interface {
public:
    enum SubType {
        HOST,
        LINK_LOCAL,
        GATEWAY
    };

    VirtualHostInterface(const string &name, VrfEntry *vrf) :
        Interface(Interface::VHOST, nil_uuid(), name, vrf), 
        sub_type_(HOST) { };
    VirtualHostInterface(const string &name, VrfEntry *vrf, SubType sub_type) :
        Interface(Interface::VHOST, nil_uuid(), name, vrf), 
        sub_type_(sub_type) { };

    virtual ~VirtualHostInterface() { };

    virtual bool CmpInterface(const DBEntry &rhs) const {
        const VirtualHostInterface &intf = static_cast<const VirtualHostInterface &>(rhs);
        return name_ < intf.name_;
    };

    KeyPtr GetDBRequestKey() const;
    virtual string ToString() const {return "VHOST";};
    SubType GetSubType() const { return sub_type_;};
    static void CreateReq(const string &ifname, const string &vrf_name, 
                          SubType sub_type);
    // Enqueue DBRequest to delete a Host Interface
    static void DeleteReq(const string &ifname);
private:
    SubType sub_type_;
    DISALLOW_COPY_AND_ASSIGN(VirtualHostInterface);
};

struct VirtualHostInterfaceData : public InterfaceData {
    VirtualHostInterfaceData(VirtualHostInterface::SubType sub_type) :
        InterfaceData(), sub_type_(sub_type) { };
    VirtualHostInterface::SubType sub_type_;
};

struct VirtualHostInterfaceKey : public InterfaceKey {
    VirtualHostInterfaceKey(const uuid &uuid, const string &name) :
        InterfaceKey(Interface::VHOST, uuid, name) {};

    virtual ~VirtualHostInterfaceKey() {};
    Interface *AllocEntry() const {return new VirtualHostInterface(name_, NULL);};
    Interface *AllocEntry(const InterfaceData *data) const {
        const VirtualHostInterfaceData *vhost_data = static_cast<const VirtualHostInterfaceData *>(data);
        VrfKey key(data->vrf_name_);
        VrfEntry *vrf =
           static_cast<VrfEntry *>(Agent::GetInstance()->GetVrfTable()->FindActiveEntry(&key));
        assert(vrf);
        return new VirtualHostInterface(name_, vrf, vhost_data->sub_type_);
    };
    InterfaceKey *Clone() const {
        return new VirtualHostInterfaceKey(*this);
    };
};

/////////////////////////////////////////////////////////////////////////////
// Implementation of Interface Table
/////////////////////////////////////////////////////////////////////////////
class InterfaceTable : public AgentDBTable {
public:
    // Map from intf-uuid to intf-name
    typedef std::pair<uuid, string> UuidNamePair;

    InterfaceTable(DB *db, const std::string &name) : AgentDBTable(db, name) { }
    virtual ~InterfaceTable() { 
    };

    virtual std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    virtual size_t Hash(const DBEntry *entry) const {return 0;};
    virtual size_t Hash(const DBRequestKey *key) const {return 0;};

    virtual DBEntry *Add(const DBRequest *req);
    virtual bool OnChange(DBEntry *entry, const DBRequest *req);
    virtual void Delete(DBEntry *entry, const DBRequest *req);

    virtual bool Resync(DBEntry *entry, DBRequest *req);

    virtual bool IFNodeToReq(IFMapNode *node, DBRequest &req);
    static void VmInterfaceVrfSync(IFMapNode *node);

    void FreeInterfaceId(size_t index) {index_table_.Remove(index);};
    Interface *FindInterface(size_t index) {return index_table_.At(index);};

    VrfEntry *FindVrfRef(const string &name) const;
    VnEntry *FindVnRef(const uuid &uuid) const;
    VmEntry *FindVmRef(const uuid &uuid) const;
    MirrorEntry *FindMirrorRef(const string &name) const;

    static DBTableBase *CreateTable(DB *db, const std::string &name);
    static InterfaceTable *GetInstance() {return interface_table_;};

private:
    static InterfaceTable *interface_table_;
    IndexVector<Interface> index_table_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceTable);
};

#endif // vnsw_agent_intf_hpp
