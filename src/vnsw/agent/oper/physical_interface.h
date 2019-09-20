/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef src_vnsw_agent_oper_physical_interface_hpp
#define src_vnsw_agent_oper_physical_interface_hpp

struct PhysicalInterfaceData;
struct PhysicalInterfaceOsOperStateData;

/////////////////////////////////////////////////////////////////////////////
// Implementation of Physical Ports local to the device
// Does not have reference to physical-device since they are local devices
// Can be Ethernet Ports or LAG Ports
// Name of port is used as key
/////////////////////////////////////////////////////////////////////////////
class PhysicalInterface : public Interface {
public:
    enum SubType {
        FABRIC,     // Physical port connecting to fabric network
        VMWARE,     // For vmware, port connecting to contrail-vm-portgroup
        CONFIG,     // Interface created from config
        INVALID
    };

    enum EncapType {
        ETHERNET,       // Ethernet with ARP
        RAW_IP          // No L2 header. Packets sent as raw-ip
    };

    struct Bond_ChildIntf {
        string intf_name;
        string intf_drv_name;
        bool intf_status;
    };

    typedef std::map<const std::string, Bond_ChildIntf> BondChildIntfMap;
    typedef BondChildIntfMap::const_iterator BondChildIntfMapIterator;


    PhysicalInterface(const std::string &name,
                      const boost::uuids::uuid &logical_router_uuid);
    virtual ~PhysicalInterface();

    virtual bool CmpInterface(const DBEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KeyPtr GetDBRequestKey() const;

    virtual void PostAdd();
    virtual bool Delete(const DBRequest *req);
    virtual bool OnChange(const InterfaceTable *table,
                          const PhysicalInterfaceData *data);
    SubType subtype() const { return subtype_; }
    PhysicalDevice *physical_device() const;
    const std::string& display_name() const {
        return display_name_;
    }

    // Lets kernel know if physical interface is to be kept after agent exits or
    // dies. If its true keep the interface, else remove it.
    // Currently only vnware physical interface is persistent.
    // By default every physical interface is non-persistent.
    bool persistent() const {return persistent_;}
    EncapType encap_type() const { return encap_type_; }
    bool no_arp() const { return no_arp_; }
    Ip4Address ip_addr() const { return ip_;}

    // Helper functions
    static void CreateReq(InterfaceTable *table, const std::string &ifname,
                          const std::string &vrf_name, SubType subtype,
                          EncapType encap, bool no_arp,
                          const boost::uuids::uuid &device_uuid,
                          const Ip4Address &ip,
                          Interface::Transport transport);
    static void Create(InterfaceTable *table, const std::string &ifname,
                       const std::string &vrf_name, SubType subtype,
                       EncapType encap, bool no_arp,
                       const boost::uuids::uuid &device_uuid,
                       const Ip4Address &ip,
                       Interface::Transport transport_);
    static void DeleteReq(InterfaceTable *table, const std::string &ifname);
    static void Delete(InterfaceTable *table, const std::string &ifname);
    bool OnChange(PhysicalInterfaceData *data);
    virtual void ObtainOsSpecificParams(const std::string &name);
    friend struct PhysicalInterfaceKey;
    friend struct PhysicalInterfaceOsOperStateData;
    friend class TestVnswIf;

    const BondChildIntfMap& getBondChildIntfMap() const {
        return bond_childIntf_map_;
    }
    void setBondChildIntfMap(const BondChildIntfMap bondChildIntfMap) {
        bond_childIntf_map_ = bondChildIntfMap;
    }

private:
    std::string GetPhysicalInterfaceName() const;

    bool persistent_;
    SubType subtype_;
    EncapType encap_type_;
    bool no_arp_;
    PhysicalDeviceRef physical_device_;
    std::string display_name_;
    Ip4Address ip_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalInterface);
    BondChildIntfMap bond_childIntf_map_;
};

struct PhysicalInterfaceData : public InterfaceData {
    PhysicalInterfaceData(Agent *agent, IFMapNode *node,
                          const std::string &vrf_name,
                          PhysicalInterface::SubType subtype,
                          PhysicalInterface::EncapType encap,
                          bool no_arp,
                          const boost::uuids::uuid &device_uuid,
                          const std::string &display_name,
                          const Ip4Address &ip,
                          Interface::Transport transport);
    virtual ~PhysicalInterfaceData() { }
    PhysicalInterface::SubType subtype_;
    PhysicalInterface::EncapType encap_type_;
    bool no_arp_;
    boost::uuids::uuid device_uuid_;
    std::string display_name_;
    Ip4Address ip_;
};

struct PhysicalInterfaceKey : public InterfaceKey {
    PhysicalInterfaceKey(const std::string &name);
    virtual ~PhysicalInterfaceKey();

    Interface *AllocEntry(const InterfaceTable *table) const;
    Interface *AllocEntry(const InterfaceTable *table,
                          const InterfaceData *data) const;
    InterfaceKey *Clone() const;
};

struct PhysicalInterfaceOsOperStateData : public PhysicalInterfaceData {
    enum bond_intf_type {
       VR_FABRIC = 1,
       VR_BOND_SLAVES,
    };

    PhysicalInterfaceOsOperStateData(unsigned short int type_,
            std::string intf_name, std::string intf_drv_name, bool status):
            PhysicalInterfaceData(NULL, NULL, "", PhysicalInterface::INVALID,
                    PhysicalInterface::ETHERNET, false, boost::uuids::nil_uuid(),
                    "", Ip4Address(0), Interface::TRANSPORT_INVALID),
            type_(type_), intf_name(intf_name), intf_drv_name(intf_drv_name),
            oper_state_(status) { }
    virtual ~PhysicalInterfaceOsOperStateData() { }
    virtual bool OnResync(PhysicalInterface *phy_intf) const;

    unsigned short int type_;
    string intf_name;
    string intf_drv_name;
    bool oper_state_;
};

#endif // src_vnsw_agent_oper_physical_interface_hpp
