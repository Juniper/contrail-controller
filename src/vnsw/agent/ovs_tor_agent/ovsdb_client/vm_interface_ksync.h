/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_VM_INTERFACE_KSYNC_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_VM_INTERFACE_KSYNC_H_

#include <ovsdb_entry.h>
#include <ovsdb_object.h>

class VmInterface;

namespace OVSDB {
class VMInterfaceKSyncObject : public OvsdbDBObject {
public:
    VMInterfaceKSyncObject(OvsdbClientIdl *idl, DBTable *table);
    virtual ~VMInterfaceKSyncObject();

    void OvsdbNotify(OvsdbClientIdl::Op, struct ovsdb_idl_row*);
    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    KSyncEntry *DBToKSyncEntry(const DBEntry*);
    OvsdbDBEntry* AllocOvsEntry(struct ovsdb_idl_row*);
    DBFilterResp DBEntryFilter(const DBEntry *entry);

private:
    DISALLOW_COPY_AND_ASSIGN(VMInterfaceKSyncObject);
};

class VMInterfaceKSyncEntry : public OvsdbDBEntry {
public:
    VMInterfaceKSyncEntry(VMInterfaceKSyncObject *table,
            const VMInterfaceKSyncEntry *key);
    VMInterfaceKSyncEntry(VMInterfaceKSyncObject *table,
            const VmInterface *entry);
    VMInterfaceKSyncEntry(VMInterfaceKSyncObject *table,
            boost::uuids::uuid uuid);

    bool IsDataResolved() {return true;};
    void AddMsg(struct ovsdb_idl_txn *);
    void ChangeMsg(struct ovsdb_idl_txn *);
    void DeleteMsg(struct ovsdb_idl_txn *);
    bool Sync(DBEntry*);
    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "VM Interface Ksync";}
    KSyncEntry* UnresolvedReference();

    const std::string &vn_name() const;

private:
    friend class VMInterfaceKSyncObject;
    boost::uuids::uuid uuid_;
    std::string vn_name_;
    DISALLOW_COPY_AND_ASSIGN(VMInterfaceKSyncEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_VM_INTERFACE_KSYNC_H_

