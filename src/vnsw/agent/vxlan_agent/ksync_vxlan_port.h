/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_vxlan_port_h
#define vnsw_agent_ksync_vxlan_port_h

/**************************************************************************
 * KSync object to manage VxLan Ports
 *
 * Agent oper DBTable Interface drives VxLan Ports
 * A vxlan port is created for every vm-interface in the table. Other
 * interface types are ignored
 **************************************************************************/
class KSyncVxlanPortObject : public KSyncDBObject {
public:
    KSyncVxlanPortObject(KSyncVxlan *ksync);
    virtual ~KSyncVxlanPortObject();

    void Init();
    void RegisterDBClients();
    void Shutdown();

    // Method to allocate the inherited KSyncVxlanPortEntry
    // Must populate the KSyncEntry from DBEntry passed.
    // DBEntry will be of type Interface
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) = 0;

    // Method to allocate the inherited KSyncVxlanPortEntry.
    // The fields in "key" must be copied into the newly allocated object
    virtual KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index) = 0;

    KSyncVxlan *ksync() const { return ksync_; }
private:
    KSyncVxlan *ksync_;
    DISALLOW_COPY_AND_ASSIGN(KSyncVxlanPortObject);
};

/**************************************************************************
 * KSync representation of every Vxlan Port created.
 * A KSyncVxlanBridgeEntry is created for every interface of type VM_INTERFACE
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

    // Virtual method invoked when a Vxlan Port is to be created
    virtual bool Add() = 0;
    // Virtual method invoked when a Vxlan Port is to be modified
    virtual bool Change() = 0;
    // Virtual method invoked when a Vxlan Port is to be deleted
    virtual bool Delete() = 0;

    Interface::Type type() const { return type_; }
    const std::string &port_name() const { return port_name_; }
    const KSyncVxlanBridgeEntry *bridge() const { return bridge_; }
private:
    Interface::Type type_;
    std::string port_name_;
    // Bridge for the port
    KSyncVxlanBridgeEntry *bridge_;
    KSyncVxlanPortObject *ksync_obj_;
    DISALLOW_COPY_AND_ASSIGN(KSyncVxlanPortEntry);
};
#endif // vnsw_agent_ksync_vxlan_port_h
