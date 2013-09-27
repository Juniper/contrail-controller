/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_vgw_h
#define vnsw_agent_vgw_h

class VGwTable {
public:
    VGwTable();
    ~VGwTable() {};

    static void Init();
    static void Shutdown();
    static void CreateStaticObjects();
    void InterfaceNotify(DBTablePartBase *partition, DBEntryBase *entry);

    static VGwTable *GetObject() {return singleton_;};
private:
    DBTableBase::ListenerId lid_;
    uint32_t label_;
    static VGwTable *singleton_;
    DISALLOW_COPY_AND_ASSIGN(VGwTable);
};

#endif //vnsw_agent_vgw_h
