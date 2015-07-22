/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_HA_STALE_DEV_VN_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_HA_STALE_DEV_VN_H_

#include <base/lifetime.h>
#include <ovsdb_entry.h>
#include <ovsdb_object.h>
#include <ovsdb_client_connection_state.h>

namespace OVSDB {
class HaStaleL2RouteTable;
class HaStaleVnTable;
class ConnectionStateEntry;

class HaStaleDevVnTable : public OvsdbDBObject {
public:
    HaStaleDevVnTable(Agent *agent, OvsPeerManager *manager,
                              ConnectionStateEntry *state,
                              std::string &dev_name);
    virtual ~HaStaleDevVnTable();

    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    KSyncEntry *DBToKSyncEntry(const DBEntry*);
    DBFilterResp OvsdbDBEntryFilter(const DBEntry *entry,
                                    const OvsdbDBEntry *ovsdb_entry);

    Agent *GetAgentPtr();
    void DeleteTableDone();
    virtual void EmptyTable();

    OvsPeer *route_peer();
    const std::string &dev_name();
    ConnectionStateEntry *state();

private:
    friend class HaStaleDevVnEntry;
    void OvsdbNotify(OvsdbClientIdl::Op op, struct ovsdb_idl_row *row) {}
    OvsdbDBEntry *AllocOvsEntry(struct ovsdb_idl_row *row) {return NULL;}

    Agent *agent_;
    OvsPeerManager *manager_;
    std::auto_ptr<OvsPeer> route_peer_;
    std::string dev_name_;
    ConnectionStateEntryPtr state_;
    HaStaleVnTable *vn_table_;
    DISALLOW_COPY_AND_ASSIGN(HaStaleDevVnTable);
};

class HaStaleDevVnEntry : public OvsdbDBEntry {
public:
    HaStaleDevVnEntry(OvsdbDBObject *table, const std::string &logical_switch);
    ~HaStaleDevVnEntry();

    void AddMsg(struct ovsdb_idl_txn *);
    void ChangeMsg(struct ovsdb_idl_txn *);
    void DeleteMsg(struct ovsdb_idl_txn *);

    bool Sync(DBEntry*);
    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Vn Route Replicator Entry";}
    KSyncEntry* UnresolvedReference();

    HaStaleL2RouteTable *route_table() {return route_table_;}
    const std::string &logical_switch_name() { return logical_switch_name_; }

    Agent *GetAgentPtr();
    OvsPeer *route_peer();
    const std::string &dev_name();
    ConnectionStateEntry *state();
    IpAddress dev_ip();
    const std::string &vn_name();
    uint32_t vxlan_id();

protected:
    virtual bool IsNoTxnEntry() { return true; }

private:
    friend class HaStaleDevVnTable;
    std::string logical_switch_name_;
    HaStaleL2RouteTable *route_table_;
    AgentRouteTable *oper_route_table_;
    IpAddress dev_ip_;
    std::string vn_name_;
    uint32_t vxlan_id_;
    DISALLOW_COPY_AND_ASSIGN(HaStaleDevVnEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_HA_STALE_DEV_VN_H_

