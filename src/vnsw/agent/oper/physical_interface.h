/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_physical_interface_hpp
#define vnsw_agent_physical_interface_hpp

/////////////////////////////////////////////////////////////////////////////
// Implementation of Physical Ports
// Can be Ethernet Ports or LAG Ports
// Name of port is used as key
/////////////////////////////////////////////////////////////////////////////
class PhysicalInterface : public Interface {
public:
    PhysicalInterface(const std::string &name, VrfEntry *vrf,
                      bool persistent);
    virtual ~PhysicalInterface();

    bool CmpInterface(const DBEntry &rhs) const;

    std::string ToString() const { return "ETH <" + name() + ">"; }
    KeyPtr GetDBRequestKey() const;
    // Lets kernel know if physical interface is to be kept after agent exits or
    // dies. If its true keep the interface, else remove it.
    // Currently only vnware physical interface is persistent.
    // By default every physical interface is non-persistent.
    bool persistent() const {return persistent_;}

    // Helper functions
    static void CreateReq(InterfaceTable *table, const std::string &ifname,
                          const std::string &vrf_name, bool persistent);
    static void Create(InterfaceTable *table, const std::string &ifname,
                       const std::string &vrf_name, bool persistent);
    static void DeleteReq(InterfaceTable *table, const std::string &ifname);
    static void Delete(InterfaceTable *table, const std::string &ifname);
private:
    bool persistent_;
    DISALLOW_COPY_AND_ASSIGN(PhysicalInterface);
};

struct PhysicalInterfaceData : public InterfaceData {
    PhysicalInterfaceData(const std::string &vrf_name, bool persistent);
    bool persistent_;
};

struct PhysicalInterfaceKey : public InterfaceKey {
    PhysicalInterfaceKey(const std::string &name);
    virtual ~PhysicalInterfaceKey();

    Interface *AllocEntry(const InterfaceTable *table) const;
    Interface *AllocEntry(const InterfaceTable *table,
                          const InterfaceData *data) const;
    InterfaceKey *Clone() const;
};

#endif // vnsw_agent_physical_interface_hpp
