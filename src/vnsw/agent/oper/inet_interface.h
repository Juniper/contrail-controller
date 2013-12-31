/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_inet_interface_hpp
#define vnsw_agent_inet_interface_hpp

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
        GATEWAY
    };

    InetInterface(const std::string &name, VrfEntry *vrf) :
        Interface(Interface::INET, nil_uuid(), name, vrf), 
        sub_type_(VHOST) { 
    }

    InetInterface(const std::string &name, VrfEntry *vrf,
                         SubType sub_type) :
        Interface(Interface::INET, nil_uuid(), name, vrf), 
        sub_type_(sub_type) {
    }

    virtual ~InetInterface() { }

    // DBTable virtual functions
    KeyPtr GetDBRequestKey() const;
    std::string ToString() const { return "INET <" + name() + ">"; }

    // The interfaces are keyed by name. No UUID is allocated for them
    virtual bool CmpInterface(const DBEntry &rhs) const {
        const InetInterface &intf =
            static_cast<const InetInterface &>(rhs);
        return name_ < intf.name_;
    }

    SubType sub_type() const { return sub_type_; }

    // Helper functions
    static void CreateReq(InterfaceTable *table, const std::string &ifname,
                          const std::string &vrf_name, SubType sub_type);
    static void DeleteReq(InterfaceTable *table, const std::string &ifname);
private:
    SubType sub_type_;
    DISALLOW_COPY_AND_ASSIGN(InetInterface);
};

struct InetInterfaceKey : public InterfaceKey {
    InetInterfaceKey(const std::string &name) :
        InterfaceKey(AgentKey::ADD_DEL_CHANGE, Interface::INET,
                     nil_uuid(), name, false) {
    }

    virtual ~InetInterfaceKey() { }

    Interface *AllocEntry(const InterfaceTable *table) const {
        return new InetInterface(name_, NULL);
    }

    Interface *AllocEntry(const InterfaceTable *table,
                          const InterfaceData *data) const;

    InterfaceKey *Clone() const {
        return new InetInterfaceKey(*this);
    }
};

struct InetInterfaceData : public InterfaceData {
    InetInterfaceData(const std::string &vrf_name,
                             InetInterface::SubType sub_type) :
        InterfaceData(), sub_type_(sub_type) {
        VirtualHostInit(vrf_name);

    }
    InetInterface::SubType sub_type_;
};

#endif // vnsw_agent_inet_interface_hpp
