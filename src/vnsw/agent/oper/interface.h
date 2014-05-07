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

struct InterfaceData;

class Interface : AgentRefCount<Interface>, public AgentDBEntry {
public:
    // Type of interfaces supported
    enum Type {
        INVALID,
        // Represents the physical ethernet port. Can be LAG interface also
        PHYSICAL,
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
    virtual void Delete() { }
    virtual void Add() { }
    virtual void SendTrace(Trace event) const;
    virtual void GetOsParams(Agent *agent);

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
    bool l2_active() const {return l2_active_;}
    const uint32_t id() const {return id_;}
    bool dhcp_enabled() const {return dhcp_enabled_;}
    bool dns_enabled() const {return dns_enabled_;}
    uint32_t label() const {return label_;}
    bool IsL2LabelValid(uint32_t label) const { return (label_ == label);}
    uint32_t os_index() const {return os_index_;}
    const ether_addr &mac() const {return mac_;}
    bool os_oper_state() const { return os_oper_state_; }
    // Used only for test code
    void set_test_oper_state(bool val) { test_oper_state_ = val; }

protected:
    void SetItfSandeshData(ItfSandeshData &data) const;

    Type type_;
    boost::uuids::uuid uuid_;
    std::string name_;
    VrfEntryRef vrf_;
    uint32_t label_;
    uint32_t l2_label_;
    bool ipv4_active_;
    bool l2_active_;
    size_t id_;
    bool dhcp_enabled_;
    bool dns_enabled_;
    struct ether_addr mac_;
    size_t os_index_;
    bool os_oper_state_;
    // Used only for test code
    bool test_oper_state_;

private:
    friend class InterfaceTable;
    InterfaceTable *table_;
    DISALLOW_COPY_AND_ASSIGN(Interface);
};

// Common key for all interfaces. 
struct InterfaceKey : public AgentKey {
    InterfaceKey(const InterfaceKey &rhs) {
        type_ = rhs.type_;
        uuid_ = rhs.uuid_;
        name_ = rhs.name_;
    }

    InterfaceKey(AgentKey::DBSubOperation sub_op, Interface::Type type,
                 const boost::uuids::uuid &uuid,
                 const std::string &name, bool is_mcast) :
        AgentKey(sub_op), type_(type), uuid_(uuid), name_(name) {
    }

    void Init(Interface::Type type, const boost::uuids::uuid &intf_uuid,
              const std::string &name) {
        type_ = type;
        uuid_ = intf_uuid;
        name_ = name;
    }

    bool Compare(const InterfaceKey &rhs) const {
        if (type_ != rhs.type_)
            return false;

        if (uuid_ != rhs.uuid_)
            return false;

        return (name_ == rhs.name_);

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
struct InterfaceData : public AgentData {
    InterfaceData() : AgentData() { }

    void VmPortInit() { vrf_name_ = ""; }
    void EthInit(const std::string &vrf_name) { vrf_name_ = vrf_name; }
    void PktInit() { vrf_name_ = ""; }
    void InetInit(const std::string &vrf_name) { vrf_name_ = vrf_name; }

    // This is constant-data. Set only during create and not modified later
    std::string vrf_name_;
};

/////////////////////////////////////////////////////////////////////////////
// Interface Table
// Index for interface is maintained using an IndexVector.
/////////////////////////////////////////////////////////////////////////////
class InterfaceTable : public AgentDBTable {
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

    InterfaceTable(DB *db, const std::string &name) :
        AgentDBTable(db, name), operdb_(NULL), agent_(NULL),
        walkid_(DBTableWalker::kInvalidWalkerId), index_table_() { 
    }
    virtual ~InterfaceTable() { }

    void Init(OperDB *oper);
    static DBTableBase *CreateTable(DB *db, const std::string &name);

    // DBTable virtual functions
    std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    size_t Hash(const DBEntry *entry) const { return 0; }
    size_t Hash(const DBRequestKey *key) const { return 0; }

    DBEntry *Add(const DBRequest *req);
    bool OnChange(DBEntry *entry, const DBRequest *req);
    void Delete(DBEntry *entry, const DBRequest *req);
    bool Resync(DBEntry *entry, DBRequest *req);

    // Config handlers
    bool IFNodeToReq(IFMapNode *node, DBRequest &req);
    // Handle change in config VRF for the interface
    void VmInterfaceVrfSync(IFMapNode *node);
    // Handle change in VxLan Identifier mode from global-config
    void UpdateVxLanNetworkIdentifierMode();

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
    void VmPortToMetaDataIp(uint16_t ifindex, uint32_t vrfid, Ip4Address *addr);

    // Dhcp Snoop Map entries
    const Ip4Address GetDhcpSnoopEntry(const std::string &ifname);
    void DeleteDhcpSnoopEntry(const std::string &ifname);
    void AddDhcpSnoopEntry(const std::string &ifname, const Ip4Address &addr);
    void AuditDhcpSnoopTable();
    void DhcpSnoopSetConfigSeen(const std::string &ifname);

    // TODO : to remove this
    static InterfaceTable *GetInstance() { return interface_table_; }
    Agent *agent() const { return agent_; }
    OperDB *operdb() const { return operdb_; }

private:
    bool L2VmInterfaceWalk(DBTablePartBase *partition, DBEntryBase *entry);
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
    DISALLOW_COPY_AND_ASSIGN(InterfaceTable);
};

#endif // vnsw_agent_interface_hpp
