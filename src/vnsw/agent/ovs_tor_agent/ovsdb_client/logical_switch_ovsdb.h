/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_LOGICAL_SWITCH_OVSDB_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_LOGICAL_SWITCH_OVSDB_H_

#include <ovsdb_entry.h>
#include <ovsdb_object.h>
#include <ovsdb_client_idl.h>

class PhysicalDeviceVn;

namespace OVSDB {
class LogicalSwitchTable : public OvsdbDBObject {
public:
    LogicalSwitchTable(OvsdbClientIdl *idl, DBTable *table);
    virtual ~LogicalSwitchTable();

    void OvsdbNotify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);
    void OvsdbMcastLocalMacNotify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);
    void OvsdbMcastRemoteMacNotify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);

    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    KSyncEntry *DBToKSyncEntry(const DBEntry*);
    OvsdbDBEntry *AllocOvsEntry(struct ovsdb_idl_row *row);
private:
    DISALLOW_COPY_AND_ASSIGN(LogicalSwitchTable);
};

class LogicalSwitchEntry : public OvsdbDBEntry {
public:
    enum Trace {
        ADD_REQ,
        DEL_REQ,
        ADD_ACK,
        DEL_ACK,
    };
    LogicalSwitchEntry(OvsdbDBObject *table, const char *name) :
        OvsdbDBEntry(table), name_(name) {}
    LogicalSwitchEntry(OvsdbDBObject *table, const LogicalSwitchEntry *key);
    LogicalSwitchEntry(OvsdbDBObject *table,
            const PhysicalDeviceVn *entry);
    LogicalSwitchEntry(OvsdbDBObject *table,
            struct ovsdb_idl_row *entry);

    Ip4Address &physical_switch_tunnel_ip();
    void AddMsg(struct ovsdb_idl_txn *);
    void ChangeMsg(struct ovsdb_idl_txn *);
    void DeleteMsg(struct ovsdb_idl_txn *);

    void OvsdbChange();

    const std::string &name() const;
    const std::string &device_name() const;
    int64_t vxlan_id() const;

    bool Sync(DBEntry*);
    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Logical Switch";}
    KSyncEntry* UnresolvedReference();
private:
    void SendTrace(Trace event) const;

    friend class LogicalSwitchTable;
    std::string name_;
    std::string device_name_;
    KSyncEntryPtr physical_switch_;
    int64_t vxlan_id_;
    struct ovsdb_idl_row *mcast_local_row_;
    struct ovsdb_idl_row *mcast_remote_row_;
    struct ovsdb_idl_row *old_mcast_remote_row_;
    DISALLOW_COPY_AND_ASSIGN(LogicalSwitchEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_LOGICAL_SWITCH_OVSDB_H_

