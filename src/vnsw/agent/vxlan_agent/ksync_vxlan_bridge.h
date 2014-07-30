/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_vxlan_bridge_h
#define vnsw_agent_ksync_vxlan_bridge_h

/**************************************************************************
 * KSync object to manage VxLan bridge.
 * Registers as client to VxLan DBTable in oper-db.
 **************************************************************************/
class KSyncVxlanBridgeObject : public KSyncDBObject {
public:
    KSyncVxlanBridgeObject(KSyncVxlan *ksync);
    virtual ~KSyncVxlanBridgeObject();

    void Init();
    void Shutdown();
    void RegisterDBClients();

    // Method to allocate the inherited KSyncVxlanBridgeEntry
    // Must populate the KSyncEntry from DBEntry passed.
    // DBEntry will be of type VxLanId
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) = 0;

    // Method to allocate the inherited KSyncVxlanBridgeEntry.
    // The fields in "key" must be copied into the newly allocated object
    virtual KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index) = 0;

    KSyncVxlan *ksync() const { return ksync_; }
private:
    KSyncVxlan *ksync_;
    DISALLOW_COPY_AND_ASSIGN(KSyncVxlanBridgeObject);
};

/**************************************************************************
 * KSync representation of every Vxlan Bridge created.
 * A KSyncVxlanBridgeEntry is created for every Vxlan entry in oper-db
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

    // Virtual method invoked when a Vxlan Bridge is to be created
    virtual bool Add() = 0;
    // Virtual method invoked by KSync to handle change to Vxlan Bridge.
    // Vxlan-Id is the only member of vxlan bridge and it should never change.
    // Assert to catch any unexpected scenarios.
    virtual bool Change() {
        assert(0);
        return true;
    }
    // Virtual method invoked to delete a Vxlan Bridge
    virtual bool Delete() = 0;

    uint32_t vxlan_id() const { return vxlan_id_; }
private:
    uint32_t vxlan_id_;
    KSyncVxlanBridgeObject *ksync_obj_;
    DISALLOW_COPY_AND_ASSIGN(KSyncVxlanBridgeEntry);
};
#endif // vnsw_agent_ksync_vxlan_bridge_h
