/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_vxlan_bridge_h
#define vnsw_agent_ksync_vxlan_bridge_h

/**************************************************************************
 * KSync object to manage VxLan bridge.
 * Agent oper DBTable VxLan drives VxLan Bridges
 **************************************************************************/
class KSyncVxlanBridgeEntry : public KSyncDBEntry {
public:
    KSyncVxlanBridgeEntry(KSyncVxlanBridgeObject *obj,
                          const KSyncVxlanBridgeEntry *entry);
    KSyncVxlanBridgeEntry(KSyncVxlanBridgeObject *obj, const VxLanId *vxlan);
    ~KSyncVxlanBridgeEntry();

    KSyncDBObject *GetObject();

    virtual std::string ToString() const;
    virtual bool Sync(DBEntry *e);
    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual KSyncEntry *UnresolvedReference();

    virtual bool Add() = 0;
    virtual bool Change() = 0;
    virtual bool Delete() = 0;

    uint32_t vxlan_id() const { return vxlan_id_; }
private:
    uint32_t vxlan_id_;
    KSyncVxlanBridgeObject *ksync_obj_;
    DISALLOW_COPY_AND_ASSIGN(KSyncVxlanBridgeEntry);
};

class KSyncVxlanBridgeObject : public KSyncDBObject {
public:
    KSyncVxlanBridgeObject(KSyncVxlan *ksync);
    virtual ~KSyncVxlanBridgeObject();

    void Init();
    void Shutdown();
    void RegisterDBClients();

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index) = 0;
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) = 0;

    KSyncVxlan *ksync() const { return ksync_; }
private:
    KSyncVxlan *ksync_;
    DISALLOW_COPY_AND_ASSIGN(KSyncVxlanBridgeObject);
};

#endif // vnsw_agent_ksync_vxlan_bridge_h
