/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_DEVICE_VN_KSYNC_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_DEVICE_VN_KSYNC_H_

#include <ovsdb_entry.h>
#include <ovsdb_object.h>
#include <ovsdb_client_idl.h>
#include <logical_switch_ovsdb.h>

class PhysicalDeviceVn;

namespace OVSDB {
class PhysicalDeviceVnKSyncEntry;

class PhysicalDeviceVnKSyncTable : public OvsdbDBObject {
public:
    PhysicalDeviceVnKSyncTable(OvsdbClientIdl *idl);
    virtual ~PhysicalDeviceVnKSyncTable();

    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    KSyncEntry *DBToKSyncEntry(const DBEntry*);
    OvsdbDBEntry *AllocOvsEntry(struct ovsdb_idl_row *row);
    DBFilterResp OvsdbDBEntryFilter(const DBEntry *entry,
                                    const OvsdbDBEntry *ovsdb_entry);

private:
    DISALLOW_COPY_AND_ASSIGN(PhysicalDeviceVnKSyncTable);
};

class PhysicalDeviceVnKSyncEntry : public OvsdbDBEntry {
public:
    enum Trace {
        ADD_REQ,
        DEL_REQ,
    };
    PhysicalDeviceVnKSyncEntry(OvsdbDBObject *table,
                               const PhysicalDeviceVnKSyncEntry *key);
    PhysicalDeviceVnKSyncEntry(OvsdbDBObject *table,
                               const PhysicalDeviceVn *entry);

    virtual ~PhysicalDeviceVnKSyncEntry();

    void AddMsg(struct ovsdb_idl_txn *);
    void ChangeMsg(struct ovsdb_idl_txn *);
    void DeleteMsg(struct ovsdb_idl_txn *);

    const std::string &name() const;
    const std::string &device_name() const;
    int64_t vxlan_id() const;

    bool Sync(DBEntry*);
    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Physical Device VN KSync";}
    KSyncEntry* UnresolvedReference();

    LogicalSwitchEntry *logical_switch();

protected:
    virtual bool IsNoTxnEntry() { return true; }

private:
    void SendTrace(Trace event) const;

    friend class PhysicalDeviceVnKSyncTable;

    std::string name_;
    std::string device_name_;

    int64_t vxlan_id_;
    // Ksync reference to the logical switch created,
    // for pointer access sanity
    KSyncEntryPtr logical_switch_ref_;
    // Creator reference to the Create Request Object
    LogicalSwitchCreateRequestPtr ls_create_ref_;

    DISALLOW_COPY_AND_ASSIGN(PhysicalDeviceVnKSyncEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_PHYSICAL_DEVICE_VN_KSYNC_H_

