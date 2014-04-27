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
    PhysicalInterface(const std::string &name, VrfEntry *vrf) :
        Interface(Interface::PHYSICAL, nil_uuid(), name, vrf) {
    }
    virtual ~PhysicalInterface() { }

    bool CmpInterface(const DBEntry &rhs) const {
        const PhysicalInterface &intf = 
            static_cast<const PhysicalInterface &>(rhs);
        return name_ < intf.name_;
    }

    std::string ToString() const { return "ETH <" + name() + ">"; }
    KeyPtr GetDBRequestKey() const;

    // Helper functions
    static void CreateReq(InterfaceTable *table, const std::string &ifname,
                          const std::string &vrf_name);
    static void Create(InterfaceTable *table, const std::string &ifname,
                       const std::string &vrf_name);
    static void DeleteReq(InterfaceTable *table, const std::string &ifname);
    static void Delete(InterfaceTable *table, const std::string &ifname);
private:
    DISALLOW_COPY_AND_ASSIGN(PhysicalInterface);
};

struct PhysicalInterfaceKey : public InterfaceKey {
    PhysicalInterfaceKey(const std::string &name) :
        InterfaceKey(AgentKey::ADD_DEL_CHANGE, Interface::PHYSICAL, nil_uuid(),
                     name, false) {
    }
    virtual ~PhysicalInterfaceKey() {}

    Interface *AllocEntry(const InterfaceTable *table) const {
        return new PhysicalInterface(name_, NULL);
    }
    Interface *AllocEntry(const InterfaceTable *table,
                          const InterfaceData *data) const {
        VrfKey key(data->vrf_name_);
        VrfEntry *vrf = static_cast<VrfEntry *>
            (table->agent()->GetVrfTable()->FindActiveEntry(&key));
        if (vrf == NULL) {
            return NULL;
        }

        return new PhysicalInterface(name_, vrf);
    }

    InterfaceKey *Clone() const {
        return new PhysicalInterfaceKey(*this);
    }
};

struct PhysicalInterfaceData : public InterfaceData {
    PhysicalInterfaceData(const std::string &vrf_name) : InterfaceData() {
        EthInit(vrf_name);
    }
};

#endif // vnsw_agent_physical_interface_hpp
