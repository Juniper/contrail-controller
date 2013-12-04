/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_virtual_host_interface_hpp
#define vnsw_agent_virtual_host_interface_hpp

/////////////////////////////////////////////////////////////////////////////
// Implementation of virtual interfaces (typically tap interfaces) created in
// host-os. 
//
// Example interfaces:
// vhost0 : A L3 interface between host-os and vrouter. Used in KVM mode
// xapi0 : A L3 interface between host-os and vrouter. Used in XEN mode
// vgw : A un-numbered L3 interface for simple gateway
/////////////////////////////////////////////////////////////////////////////
class VirtualHostInterface : public Interface {
public:
    enum SubType {
        HOST,
        LINK_LOCAL,
        GATEWAY
    };

    VirtualHostInterface(const std::string &name, VrfEntry *vrf) :
        Interface(Interface::VIRTUAL_HOST, nil_uuid(), name, vrf), 
        sub_type_(HOST) { 
    }

    VirtualHostInterface(const std::string &name, VrfEntry *vrf,
                         SubType sub_type) :
        Interface(Interface::VIRTUAL_HOST, nil_uuid(), name, vrf), 
        sub_type_(sub_type) {
    }

    virtual ~VirtualHostInterface() { }

    // DBTable virtual functions
    KeyPtr GetDBRequestKey() const;
    virtual std::string ToString() const { return "VHOST"; }

    // The interfaces are keyed by name. No UUID is allocated for them
    virtual bool CmpInterface(const DBEntry &rhs) const {
        const VirtualHostInterface &intf =
            static_cast<const VirtualHostInterface &>(rhs);
        return name_ < intf.name_;
    }

    SubType sub_type() const { return sub_type_; }

    // Helper functions
    static void CreateReq(InterfaceTable *table, const std::string &ifname,
                          const std::string &vrf_name, SubType sub_type);
    static void DeleteReq(InterfaceTable *table, const std::string &ifname);
private:
    SubType sub_type_;
    DISALLOW_COPY_AND_ASSIGN(VirtualHostInterface);
};

struct VirtualHostInterfaceKey : public InterfaceKey {
    VirtualHostInterfaceKey(const std::string &name) :
        InterfaceKey(Interface::VIRTUAL_HOST, nil_uuid(), name) {
    }

    virtual ~VirtualHostInterfaceKey() { }

    Interface *AllocEntry() const {
        return new VirtualHostInterface(name_, NULL);
    }

    Interface *AllocEntry(const InterfaceData *data) const;

    InterfaceKey *Clone() const {
        return new VirtualHostInterfaceKey(*this);
    }
};

struct VirtualHostInterfaceData : public InterfaceData {
    VirtualHostInterfaceData(const std::string &vrf_name,
                             VirtualHostInterface::SubType sub_type) :
        InterfaceData(), sub_type_(sub_type) {
        VirtualHostInit(vrf_name);

    }
    VirtualHostInterface::SubType sub_type_;
};

#endif // vnsw_agent_virtual_host_interface_hpp
