/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_inet_interface_hpp
#define vnsw_agent_inet_interface_hpp

struct InetInterfaceData;

/////////////////////////////////////////////////////////////////////////////
// Implementation of inet interfaces created in host-os. 
//
// Example interfaces:
// vhost0 : A L3 interface between host-os and vrouter. Used in KVM mode
// xenbr : A L3 interface between host-os and vrouter. Used in XEN mode
// xapi0 : A L3 interface for link-local subnets. Used in XEN mode
// vgw : A un-numbered L3 interface for simple gateway
/////////////////////////////////////////////////////////////////////////////
class InetInterface : public Interface {
public:
    enum SubType {
        VHOST,
        LINK_LOCAL,
        SIMPLE_GATEWAY
    };

    InetInterface(const std::string &name);
    InetInterface(const std::string &name, SubType sub_type, VrfEntry *vrf,
                  const Ip4Address &ip_addr, int plen, const Ip4Address &gw,
                  Interface *xconnect, const std::string &vn_name);
    virtual ~InetInterface() { }

    // DBTable virtual functions
    KeyPtr GetDBRequestKey() const;
    std::string ToString() const { return "INET <" + name() + ">"; }

    // The interfaces are keyed by name. No UUID is allocated for them
    virtual bool CmpInterface(const DBEntry &rhs) const;
    SubType sub_type() const { return sub_type_; }
    const Ip4Address &ip_addr() const { return ip_addr_; }
    Interface *xconnect() const { return xconnect_.get(); }

    void PostAdd();
    bool OnChange(InetInterfaceData *data);
    void Delete();
    void Activate();
    void DeActivate();

    void ActivateSimpleGateway();
    void DeActivateSimpleGateway();
    void ActivateHostInterface();
    void DeActivateHostInterface();

    // Helper functions
    static void Create(InterfaceTable *table, const std::string &ifname,
                       SubType sub_type, const std::string &vrf_name,
                       const Ip4Address &addr, int plen,
                       const Ip4Address &gw, const std::string &xconnect,
                       const std::string &vn_name);
    static void CreateReq(InterfaceTable *table, const std::string &ifname,
                          SubType sub_type, const std::string &vrf_name,
                          const Ip4Address &addr, int plen,
                          const Ip4Address &gw, const std::string &xconnect,
                          const std::string &vn_name);
    static void Delete(InterfaceTable *table, const std::string &ifname);
    static void DeleteReq(InterfaceTable *table, const std::string &ifname);
private:
    SubType sub_type_;
    Ip4Address ip_addr_;
    int plen_;
    Ip4Address gw_;
    InterfaceRef xconnect_;   // Physical interface for VHOST
    std::string vn_name_;
    DISALLOW_COPY_AND_ASSIGN(InetInterface);
};

struct InetInterfaceKey : public InterfaceKey {
    InetInterfaceKey(const std::string &name);
    virtual ~InetInterfaceKey() { }

    Interface *AllocEntry(const InterfaceTable *table) const;
    Interface *AllocEntry(const InterfaceTable *table,
                          const InterfaceData *data) const;
    InterfaceKey *Clone() const;
};

struct InetInterfaceData : public InterfaceData {
    InetInterfaceData(InetInterface::SubType sub_type, 
                      const std::string &vrf_name, const Ip4Address &addr,
                      int plen, const Ip4Address &gw,
                      const std::string &xconnect, const std::string vn_name);
    virtual ~InetInterfaceData() { }

    InetInterface::SubType sub_type_;
    Ip4Address ip_addr_;
    int plen_;
    Ip4Address gw_;
    std::string xconnect_;
    std::string vn_name_;
};

#endif // vnsw_agent_inet_interface_hpp
