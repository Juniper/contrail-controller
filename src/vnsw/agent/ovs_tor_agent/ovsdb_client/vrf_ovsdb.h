/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_VRF_OVSDB_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_VRF_OVSDB_H_

#include <base/lifetime.h>
#include <ovsdb_entry.h>
#include <ovsdb_object.h>
#include <ovsdb_client_idl.h>

class BridgeRouteEntry;

namespace OVSDB {
class UnicastMacRemoteTable;

class VrfOvsdbObject : public OvsdbDBObject {
public:
    VrfOvsdbObject(OvsdbClientIdl *idl, DBTable *table);
    virtual ~VrfOvsdbObject();

    void OvsdbNotify(OvsdbClientIdl::Op, struct ovsdb_idl_row *);

    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    KSyncEntry *DBToKSyncEntry(const DBEntry*);
    OvsdbDBEntry *AllocOvsEntry(struct ovsdb_idl_row *row);
    KSyncDBObject::DBFilterResp DBEntryFilter(const DBEntry *entry);

private:
    DISALLOW_COPY_AND_ASSIGN(VrfOvsdbObject);
};

class VrfOvsdbEntry : public OvsdbDBEntry {
public:
    enum Trace {
        ADD_REQ,
        DEL_REQ,
        ADD_ACK,
        DEL_ACK,
    };
    VrfOvsdbEntry(OvsdbDBObject *table, const std::string &logical_switch);
    ~VrfOvsdbEntry();

    void AddMsg(struct ovsdb_idl_txn *);
    void ChangeMsg(struct ovsdb_idl_txn *);
    void DeleteMsg(struct ovsdb_idl_txn *);

    bool Sync(DBEntry*);
    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Vrf Ovsdb Entry";}
    KSyncEntry* UnresolvedReference();

    UnicastMacRemoteTable *route_table() {return route_table_;}

protected:
    virtual bool IsNoTxnEntry() { return true; }

private:
    friend class VrfOvsdbObject;
    void SendTrace(Trace event) const;
    std::string logical_switch_name_;
    UnicastMacRemoteTable *route_table_;
    AgentRouteTable *oper_route_table_;
    DISALLOW_COPY_AND_ASSIGN(VrfOvsdbEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_VRF_OVSDB_H_

