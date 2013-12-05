/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_interface_hpp
#define vnsw_agent_pkt_interface_hpp

/////////////////////////////////////////////////////////////////////////////
// Implementation of Pkt Interface. Interface is used to exchange packets 
// between vrouter to agent
/////////////////////////////////////////////////////////////////////////////

class PktInterface : public Interface {
public:
    PktInterface(const std::string &name) : 
        Interface(Interface::PACKET, nil_uuid(), name, NULL) {
    }
    virtual ~PktInterface() { }

    virtual bool CmpInterface(const DBEntry &rhs) const { return false; }

    std::string ToString() const { return "PKT"; }
    KeyPtr GetDBRequestKey() const;

    // Helper function to enqueue DBRequest to create a Pkt Interface
    static void CreateReq(InterfaceTable *table, const std::string &ifname);
    // Helper function to enqueue DBRequest to delete a Pkt Interface
    static void DeleteReq(InterfaceTable *table, const std::string &ifname);
private:
    DISALLOW_COPY_AND_ASSIGN(PktInterface);
};

struct PktInterfaceKey : public InterfaceKey {
    PktInterfaceKey(const boost::uuids::uuid &uuid, const std::string &name) :
        InterfaceKey(Interface::PACKET, uuid, name) {
    }

    virtual ~PktInterfaceKey() {}

    Interface *AllocEntry() const {
        return new PktInterface(name_);
    }
    Interface *AllocEntry(const InterfaceData *data) const {
        return new PktInterface(name_);
    }

    InterfaceKey *Clone() const {
        return new PktInterfaceKey(*this);
    }
};

struct PktInterfaceData : public InterfaceData {
    PktInterfaceData() : InterfaceData() {
        PktInit();
    }
};

#endif // vnsw_agent_pkt_interface_hpp
