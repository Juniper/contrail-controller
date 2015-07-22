/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_VN_ROUTE_REPLICATOR_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_VN_ROUTE_REPLICATOR_H_

#include <ovsdb_entry.h>
#include <ovsdb_object.h>

class VnEntry;

namespace OVSDB {
class HaStaleDevVnTable;
class HaStaleVnEntry;

class HaStaleVnTable : public OvsdbDBObject {
public:
    HaStaleVnTable(Agent *agent, HaStaleDevVnTable *dev_vn_table);
    virtual ~HaStaleVnTable();

    void OvsdbNotify(OvsdbClientIdl::Op, struct ovsdb_idl_row*);
    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    KSyncEntry *DBToKSyncEntry(const DBEntry*);
    OvsdbDBEntry* AllocOvsEntry(struct ovsdb_idl_row*);
    DBFilterResp OvsdbDBEntryFilter(const DBEntry *entry,
                                    const OvsdbDBEntry *ovsdb_entry);
    virtual void EmptyTable(void);
    Agent *GetAgentPtr();
    void DeleteTableDone();

private:
    friend class HaStaleVnEntry;

    Agent *agent_;
    HaStaleDevVnTable *dev_vn_table_;
    DISALLOW_COPY_AND_ASSIGN(HaStaleVnTable);
};

class HaStaleVnEntry : public OvsdbDBEntry {
public:
    HaStaleVnEntry(HaStaleVnTable *table, const boost::uuids::uuid &uuid);

    // ovs_entry ref is not valid for VN, override IsDataResolved
    // to return always true
    bool IsDataResolved() {return true;}
    void AddMsg(struct ovsdb_idl_txn *);
    void ChangeMsg(struct ovsdb_idl_txn *);
    void DeleteMsg(struct ovsdb_idl_txn *);
    bool Sync(DBEntry*);
    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Virtual Network replicator entry";}
    KSyncEntry* UnresolvedReference();

    AgentRouteTable *bridge_table();

protected:
    virtual bool IsNoTxnEntry() { return true; }

private:
    friend class HaStaleVnTable;
    boost::uuids::uuid uuid_;
    AgentRouteTable *bridge_table_;
    DISALLOW_COPY_AND_ASSIGN(HaStaleVnEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_VN_ROUTE_REPLICATOR_H_

