/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_vgw_h
#define vnsw_agent_vgw_h

class VGwTable {
public:
    VGwTable(Agent *agent);
    ~VGwTable() {};

    static void Init();
    static void Shutdown();
    static void CreateStaticObjects();
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *entry);
    void CreateVrf();
    void CreateInterfaces();
    void RegisterDBClients();

    static VGwTable *GetObject() {return singleton_;};
private:
    Agent *agent_;
    DBTableBase::ListenerId lid_;
    uint32_t label_;
    static VGwTable *singleton_;
    DISALLOW_COPY_AND_ASSIGN(VGwTable);
};

#endif //vnsw_agent_vgw_h
