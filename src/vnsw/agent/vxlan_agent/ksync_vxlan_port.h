/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_vxlan_port_h
#define vnsw_agent_ksync_vxlan_port_h

/**************************************************************************
 * KSync object to manage VxLan Ports
 * Agent oper DBTable Interface drives VxLan Bridges
 * VxLan is interested only in vm-interfaces
 **************************************************************************/
class KSyncVxlanPortEntry : public KSyncDBEntry {
public:
    KSyncVxlanPortEntry(KSyncVxlanPortObject *obj,
                        const KSyncVxlanPortEntry *entry);
    KSyncVxlanPortEntry(KSyncVxlanPortObject *obj, const Interface *interface);
    virtual ~KSyncVxlanPortEntry();

    KSyncDBObject *GetObject();

    virtual std::string ToString() const;
    virtual bool Sync(DBEntry *e);
    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual KSyncEntry *UnresolvedReference();

    virtual bool Add() = 0;
    virtual bool Change() = 0;
    virtual bool Delete() = 0;

    Interface::Type type() const { return type_; }
    const std::string &port_name() const { return port_name_; }
    const KSyncVxlanBridgeEntry *bridge() const { return bridge_; }
private:
    Interface::Type type_;
    std::string port_name_;
    KSyncVxlanBridgeEntry *bridge_;
    KSyncVxlanPortObject *ksync_obj_;
    DISALLOW_COPY_AND_ASSIGN(KSyncVxlanPortEntry);
};

class KSyncVxlanPortObject : public KSyncDBObject {
public:
    KSyncVxlanPortObject(KSyncVxlan *ksync);
    virtual ~KSyncVxlanPortObject();

    void Init();
    void RegisterDBClients();
    void Shutdown();

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index) = 0;
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) = 0;

    KSyncVxlan *ksync() const { return ksync_; }
private:
    KSyncVxlan *ksync_;
    DISALLOW_COPY_AND_ASSIGN(KSyncVxlanPortObject);
};

#endif // vnsw_agent_ksync_vxlan_port_h
