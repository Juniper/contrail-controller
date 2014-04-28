/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_packet_interface_hpp
#define vnsw_agent_packet_interface_hpp

/////////////////////////////////////////////////////////////////////////////
// Implementation of Pkt Interface. Interface is used to exchange packets 
// between vrouter to agent
/////////////////////////////////////////////////////////////////////////////

class PacketInterface : public Interface {
public:
    PacketInterface(const std::string &name) : 
        Interface(Interface::PACKET, nil_uuid(), name, NULL) {
    }
    virtual ~PacketInterface() { }

    virtual bool CmpInterface(const DBEntry &rhs) const { return false; }

    std::string ToString() const { return "PKT <" + name() + ">"; }
    KeyPtr GetDBRequestKey() const;

    // Helper function to enqueue DBRequest to create a Pkt Interface
    static void CreateReq(InterfaceTable *table, const std::string &ifname);
    static void Create(InterfaceTable *table, const std::string &ifname);
    // Helper function to enqueue DBRequest to delete a Pkt Interface
    static void DeleteReq(InterfaceTable *table, const std::string &ifname);
    static void Delete(InterfaceTable *table, const std::string &ifname);
private:
    DISALLOW_COPY_AND_ASSIGN(PacketInterface);
};

struct PacketInterfaceKey : public InterfaceKey {
    PacketInterfaceKey(const boost::uuids::uuid &uuid,
                       const std::string &name) :
        InterfaceKey(AgentKey::ADD_DEL_CHANGE, Interface::PACKET, uuid, name,
                     false) {
    }

    virtual ~PacketInterfaceKey() {}

    Interface *AllocEntry(const InterfaceTable *table) const {
        return new PacketInterface(name_);
    }
    Interface *AllocEntry(const InterfaceTable *table,
                          const InterfaceData *data) const {
        return new PacketInterface(name_);
    }

    InterfaceKey *Clone() const {
        return new PacketInterfaceKey(*this);
    }
};

struct PacketInterfaceData : public InterfaceData {
    PacketInterfaceData() : InterfaceData() {
        PktInit();
    }
};

#endif // vnsw_agent_packet_interface_hpp
