/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_vgw_h
#define vnsw_agent_vgw_h

// Simple Virtual Gateway operational class
// Creates the interface, route and nexthop for virtual-gateway
class VirtualGateway {
public:
    VirtualGateway(Agent *agent);
    ~VirtualGateway() {};
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *entry);

    void Init();
    void Shutdown();
    void CreateVrf();
    void CreateInterfaces();
    void RegisterDBClients();
private:
    // Cached entries
    Agent *agent_;
    DBTableBase::ListenerId listener_id_;
    VirtualGatewayConfigTable *vgw_config_table_;

    DISALLOW_COPY_AND_ASSIGN(VirtualGateway);
};

#endif //vnsw_agent_vgw_h
