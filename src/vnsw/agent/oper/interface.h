/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_interface_hpp
#define vnsw_agent_interface_hpp

/////////////////////////////////////////////////////////////////////////////
// Agent supports multiple type of interface. Class Interface is defines
// common attributes of all interfaces. All interfaces derive from the base 
// Interface class
/////////////////////////////////////////////////////////////////////////////

#include <cmn/agent_cmn.h>
#include <cmn/index_vector.h>
#include <oper_db.h>

struct InterfaceData;
class VmInterface;
class IFMapDependencyManager;

class Interface : AgentRefCount<Interface>, public AgentOperDBEntry {
public:
    // Type of interfaces supported
    enum Type {
        INVALID,
        // Represents the physical ethernet port. Can be LAG interface also
        PHYSICAL,
        // Remote Physical interface
        REMOTE_PHYSICAL,
        // Logical interface
        LOGICAL,
        // Interface in the virtual machine
        VM_INTERFACE,
        // The inet interfaces created in host-os
        // Example interfaces:
        // vhost0 in case of KVM
        // xap0 in case of XEN
        // vgw in case of Simple Gateway
        INET,
        // pkt0 interface used to exchange packets between vrouter and agent
        PACKET
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

    enum Transport {
        TRANSPORT_INVALID,
        TRANSPORT_VIRTUAL,
        TRANSPORT_ETHERNET,
        TRANSPORT_SOCKET,
        TRANSPORT_PMD
    };

    static const uint32_t kInvalidIndex = 0xFFFFFFFF;

    Interface(Type type, const boost::uuids::uuid &uuid,
              const std::string &name, VrfEntry *vrf);
    virtual ~Interface();

    // DBEntry virtual function. Must be implemented by derived interfaces
    virtual KeyPtr GetDBRequestKey() const = 0;
    //DBEntry virtual function. Implmeneted in base class since its common 
    //for all interfaces
    virtual void SetKey(const DBRequestKey *key);

    // virtual functions for specific interface types
    virtual bool CmpInterface(const DBEntry &rhs) const = 0;
    virtual bool Delete(const DBRequest *req) { return true; }
    virtual void Add() { }
    virtual void SendTrace(const AgentDBTable *table, Trace event) const;
    virtual void GetOsParams(Agent *agent);
    void SetPciIndex(Agent *agent);

    // DBEntry comparator virtual function
    bool IsLess(const DBEntry &rhs) const {
        const Interface &intf = static_cast<const Interface &>(rhs);
        if (type_ != intf.type_) {
            return type_ < intf.type_;
        }

        return CmpInterface(rhs);
    }

    uint32_t vrf_id() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<Interface>::GetRefCount();
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;

    // Tunnelled packets are expected on PHYSICAL interfaces only
    bool IsTunnelEnabled() const { return (type_ == PHYSICAL);}

    // Accessor methods
    Type type() const {return type_;}
    const boost::uuids::uuid &GetUuid() const {return uuid_;}
    const std::string &name() const {return name_;}
    VrfEntry *vrf() const {return vrf_.get();}
    bool ipv4_active() const {return ipv4_active_;}
    bool ipv6_active() const {return ipv6_active_;}
    bool is_hc_active() const { return is_hc_active_; }
    bool metadata_ip_active() const {return metadata_ip_active_;}
    bool metadata_l2_active() const {return metadata_l2_active_;}
    bool ip_active(Address::Family family) const;
    bool l2_active() const {return l2_active_;}
    const uint32_t id() const {return id_;}
    bool dhcp_enabled() const {return dhcp_enabled_;}
    bool dns_enabled() const {return dns_enabled_;}
    uint32_t label() const {return label_;}
    uint32_t l2_label() const {return l2_label_;}
    bool IsL2LabelValid(uint32_t label) const { return (label_ == label);}
    uint32_t os_index() const {return os_index_;}
    const MacAddress &mac() const {return mac_;}
    bool os_oper_state() const { return os_oper_state_; }
    bool admin_state() const { return admin_state_; }
    // Used only for test code
    void set_test_oper_state(bool val) { test_oper_state_ = val; }
    void set_flow_key_nh(const NextHop *nh) { flow_key_nh_ = nh;}
    const NextHop* flow_key_nh() const {return flow_key_nh_.get();}
    Interface::Transport transport() const { return transport_;}
    bool IsUveActive() const;
    const AgentQosConfig* qos_config() const {
        return qos_config_.get();
    }

protected:
    void SetItfSandeshData(ItfSandeshData &data) const;

    Type type_;
    boost::uuids::uuid uuid_;
    std::string name_;
    VrfEntryRef vrf_;
    uint32_t label_;
    uint32_t l2_label_;
    bool ipv4_active_;
    bool ipv6_active_;
    // if interface is marked active by health check
    bool is_hc_active_;
    // interface has metadata ip active
    bool metadata_ip_active_;
    bool metadata_l2_active_;
    bool l2_active_;
    size_t id_;
    bool dhcp_enabled_;
    bool dns_enabled_;
    MacAddress mac_;
    size_t os_index_;
    bool os_oper_state_;
    bool admin_state_;
    // Used only for test code
    bool test_oper_state_;
    //Reference to nexthop, whose index gets used as key in
    //flow lookup for traffic ingressing from this interface
    //packet interface and bridge interface will not have this
    //reference set.
    NextHopConstRef flow_key_nh_;
    Transport transport_;
    AgentQosConfigConstRef qos_config_;

private:
    friend class InterfaceTable;
    InterfaceTable *table_;
    DISALLOW_COPY_AND_ASSIGN(Interface);
};

// Common key for all interfaces.
struct InterfaceKey : public AgentOperDBKey {
    InterfaceKey(const InterfaceKey &rhs) {
        type_ = rhs.type_;
        uuid_ = rhs.uuid_;
        name_ = rhs.name_;
    }

    InterfaceKey(AgentKey::DBSubOperation sub_op, Interface::Type type,
                 const boost::uuids::uuid &uuid,
                 const std::string &name, bool is_mcast) :
        AgentOperDBKey(sub_op), type_(type), uuid_(uuid), name_(name) {
    }

    void Init(Interface::Type type, const boost::uuids::uuid &intf_uuid,
              const std::string &name) {
        type_ = type;
        uuid_ = intf_uuid;
        name_ = name;
    }

    bool IsLess(const InterfaceKey &rhs) const {
        if (type_ != rhs.type_) {
            return type_ < rhs.type_;
        }

        if (uuid_ != rhs.uuid_) {
            return uuid_ < rhs.uuid_;
        }

        return name_ < rhs.name_;
    }

    bool IsEqual(const InterfaceKey &rhs) const {
        if ((IsLess(rhs) == false) && (rhs.IsLess(*this) == false)) {
            return true;
        }
        return false;
    }

    // Virtual methods for interface keys
    virtual Interface *AllocEntry(const InterfaceTable *table) const = 0;
    virtual Interface *AllocEntry(const InterfaceTable *table,
                                  const InterfaceData *data) const = 0;
    virtual InterfaceKey *Clone() const = 0;

    Interface::Type type_;
    boost::uuids::uuid uuid_;
    std::string name_;
};

// Common data for all interfaces. The data is further derived based on type
// of interfaces
struct InterfaceData : public AgentOperDBData {
    InterfaceData(Agent *agent, IFMapNode *node,
                  Interface::Transport transport) :
        AgentOperDBData(agent, node), transport_(transport) { }

    void VmPortInit() { vrf_name_ = ""; }
    void EthInit(const std::string &vrf_name) { vrf_name_ = vrf_name; }
    void PktInit() { vrf_name_ = ""; }
    void InetInit(const std::string &vrf_name) { vrf_name_ = vrf_name; }
    void RemotePhysicalPortInit(const std::string &vrf_name) {
        vrf_name_ = vrf_name;
    }

    // This is constant-data. Set only during create and not modified later
    std::string vrf_name_;
    Interface::Transport transport_;
};

struct InterfaceQosConfigData : public AgentOperDBData {
    InterfaceQosConfigData(const Agent *agent, IFMapNode *node,
                           boost::uuids::uuid qos_config_uuid):
        AgentOperDBData(agent, node), qos_config_uuid_(qos_config_uuid) {}

    boost::uuids::uuid qos_config_uuid_;
};

/////////////////////////////////////////////////////////////////////////////
// Definition of tree containing physical-device-vn entry created by the VMI
//
// Physical-device-vn entries are built from the VM-Interface to VN link
// The table contains physical-device-vn entry for every VM-Interface
/////////////////////////////////////////////////////////////////////////////
struct VmiToPhysicalDeviceVnData {
    VmiToPhysicalDeviceVnData(const boost::uuids::uuid &dev,
                              const boost::uuids::uuid &vn);
    ~VmiToPhysicalDeviceVnData();

    VmiToPhysicalDeviceVnData &operator= (VmiToPhysicalDeviceVnData const &rhs);

    boost::uuids::uuid dev_;
    boost::uuids::uuid vn_;
};

typedef std::map<boost::uuids::uuid, VmiToPhysicalDeviceVnData>
VmiToPhysicalDeviceVnTree;
/////////////////////////////////////////////////////////////////////////////
// Interface Table
// Index for interface is maintained using an IndexVector.
/////////////////////////////////////////////////////////////////////////////
class InterfaceTable : public AgentOperDBTable {
public:
    struct DhcpSnoopEntry {
        DhcpSnoopEntry() : addr_(0), config_entry_(false) { }
        DhcpSnoopEntry(const Ip4Address &addr, bool config_entry) :
            addr_(addr), config_entry_(config_entry) { }
        ~DhcpSnoopEntry() { }
        Ip4Address addr_;
        bool config_entry_;
    };

    typedef std::map<const std::string, DhcpSnoopEntry> DhcpSnoopMap;
    typedef std::map<const std::string, DhcpSnoopEntry>::iterator DhcpSnoopIterator;

    // Map of VM-Interface UUID to VmiType. VmiType is computed based on the
    // config when interface is added. But when VMI is deleted, the VmiType
    // cannot be computed. So, store the computed value in this map
    // Storing VmiType as int to avoid forward declaration
    typedef std::map<boost::uuids::uuid, int> VmiToVmiTypeMap;

    // DNS module is optional. Callback function to keep DNS entry for
    // floating ip in-sync. This callback is defined to avoid linking error
    // when DNS is not enabled
    typedef boost::function<void(VmInterface *, const VnEntry *,
                                 const Ip4Address &, bool)> UpdateFloatingIpFn;

    InterfaceTable(DB *db, const std::string &name) :
        AgentOperDBTable(db, name), operdb_(NULL), agent_(NULL),
        walkid_(DBTableWalker::kInvalidWalkerId), index_table_(),
        vmi_count_(0), li_count_(0), active_vmi_count_(0),
        vmi_ifnode_to_req_(0), li_ifnode_to_req_(0), pi_ifnode_to_req_(0) {
    }
    virtual ~InterfaceTable() { }

    void Init(OperDB *oper);
    static DBTableBase *CreateTable(DB *db, const std::string &name);
    void RegisterDBClients(IFMapDependencyManager *dep);

    // DBTable virtual functions
    std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    size_t Hash(const DBEntry *entry) const { return 0; }
    size_t Hash(const DBRequestKey *key) const { return 0; }
    virtual AgentSandeshPtr GetAgentSandesh(const AgentSandeshArguments *args,
                                            const std::string &context);

    DBEntry *OperDBAdd(const DBRequest *req);
    bool OperDBOnChange(DBEntry *entry, const DBRequest *req);
    bool OperDBDelete(DBEntry *entry, const DBRequest *req);
    bool OperDBResync(DBEntry *entry, const DBRequest *req);

    // Config handlers
    bool ProcessConfig(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    bool LogicalInterfaceProcessConfig(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    bool PhysicalInterfaceProcessConfig(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    bool VmiProcessConfig(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    bool VmiIFNodeToReq(IFMapNode *node, DBRequest &req,
         const boost::uuids::uuid &u);
    bool VmiIFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);
    bool LogicalInterfaceIFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);
    bool PhysicalInterfaceIFNodeToReq(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    bool LogicalInterfaceIFNodeToReq(IFMapNode *node, DBRequest &req,
            const boost::uuids::uuid &u);
    bool RemotePhysicalInterfaceIFNodeToReq(IFMapNode *node, DBRequest
            &req, const boost::uuids::uuid &u);
    bool IFNodeToReq(IFMapNode *node, DBRequest &req, const boost::uuids::uuid
            &u);
    bool IFNodeToUuid(IFMapNode *node, boost::uuids::uuid &u);

    // Handle change in VxLan Identifier mode from global-config
    void GlobalVrouterConfigChanged();

    // Helper functions
    VrfEntry *FindVrfRef(const std::string &name) const;
    VnEntry *FindVnRef(const boost::uuids::uuid &uuid) const;
    VmEntry *FindVmRef(const boost::uuids::uuid &uuid) const;
    MirrorEntry *FindMirrorRef(const std::string &name) const;

    // Interface index managing routines
    void FreeInterfaceId(size_t index) { index_table_.Remove(index); }
    Interface *FindInterface(size_t index);
    Interface *FindInterfaceFromMetadataIp(const Ip4Address &ip);

    // Metadata address management routines
    virtual bool FindVmUuidFromMetadataIp(const Ip4Address &ip,
                                          std::string *vm_ip,
                                          std::string *vm_uuid,
                                          std::string *vm_project_uuid);
    void VmPortToMetaDataIp(uint32_t ifindex, uint32_t vrfid, Ip4Address *addr);

    // Dhcp Snoop Map entries
    const Ip4Address GetDhcpSnoopEntry(const std::string &ifname);
    void DeleteDhcpSnoopEntry(const std::string &ifname);
    void AddDhcpSnoopEntry(const std::string &ifname, const Ip4Address &addr);
    void AuditDhcpSnoopTable();
    void DhcpSnoopSetConfigSeen(const std::string &ifname);

    void set_update_floatingip_cb(UpdateFloatingIpFn fn);
    const UpdateFloatingIpFn &update_floatingip_cb() const;
    void AddVmiToVmiType(const boost::uuids::uuid &u, int type);
    int GetVmiToVmiType(const boost::uuids::uuid &u);
    void DelVmiToVmiType(const boost::uuids::uuid &u);

    // Routines managing VMI to physical-device-vn entry
    void UpdatePhysicalDeviceVnEntry(const boost::uuids::uuid &vmi,
                                     boost::uuids::uuid &dev,
                                     boost::uuids::uuid &vn,
                                     IFMapNode *vn_node);
    void DelPhysicalDeviceVnEntry(const boost::uuids::uuid &vmi);

    // TODO : to remove this
    static InterfaceTable *GetInstance() { return interface_table_; }
    Agent *agent() const { return agent_; }
    OperDB *operdb() const { return operdb_; }

    uint32_t vmi_count() const { return vmi_count_; }
    void incr_vmi_count() { vmi_count_++; }
    void decr_vmi_count() { vmi_count_--; }

    uint32_t li_count() const { return li_count_; }
    void incr_li_count() { li_count_++; }
    void decr_li_count() { li_count_--; }

    uint32_t active_vmi_count() const { return active_vmi_count_; }
    void incr_active_vmi_count() { active_vmi_count_++; }
    void decr_active_vmi_count() { active_vmi_count_--; }

    uint32_t vmi_ifnode_to_req() const { return vmi_ifnode_to_req_; }
    uint32_t li_ifnode_to_req() const { return li_ifnode_to_req_; }
    uint32_t pi_ifnode_to_req() const { return pi_ifnode_to_req_; }
private:
    bool L2VmInterfaceWalk(DBTablePartBase *partition,
                           DBEntryBase *entry);
    void VmInterfaceWalkDone(DBTableBase *partition);

    static InterfaceTable *interface_table_;
    OperDB *operdb_;        // Cached entry
    Agent *agent_;          // Cached entry
    DBTableWalker::WalkId walkid_;
    IndexVector<Interface> index_table_;
    // On restart, DHCP Snoop entries are read from kernel and updated in the
    // ASIO context. Lock used to synchronize
    tbb::mutex dhcp_snoop_mutex_;
    DhcpSnoopMap dhcp_snoop_map_;
    UpdateFloatingIpFn update_floatingip_cb_;
    VmiToVmiTypeMap vmi_to_vmitype_map_;
    // Tree of physical-device-vn entries created by VMIs
    VmiToPhysicalDeviceVnTree vmi_to_physical_device_vn_tree_;
    // Count of type of interfaces
    uint32_t vmi_count_;
    uint32_t li_count_;
    // Count of active vm-interfaces
    uint32_t active_vmi_count_;
    uint32_t vmi_ifnode_to_req_;
    uint32_t li_ifnode_to_req_;
    uint32_t pi_ifnode_to_req_;

    DISALLOW_COPY_AND_ASSIGN(InterfaceTable);
};

#endif // vnsw_agent_interface_hpp
