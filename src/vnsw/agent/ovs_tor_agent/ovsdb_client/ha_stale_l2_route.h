/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_HA_STALE_L2_ROUTE_H_
#define SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_HA_STALE_L2_ROUTE_H_

#include <base/lifetime.h>
#include <ovsdb_entry.h>
#include <ovsdb_object.h>
#include <ovsdb_client_idl.h>

class BridgeRouteEntry;

namespace OVSDB {
class HaStaleDevVnEntry;
class HaStaleL2RouteEntry;
class ConnectionStateEntry;

class HaStaleL2RouteTable : public OvsdbDBObject {
public:
    HaStaleL2RouteTable(HaStaleDevVnEntry *dev_vn,
                        AgentRouteTable *table);
    virtual ~HaStaleL2RouteTable();

    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    KSyncEntry *DBToKSyncEntry(const DBEntry*);

    DBFilterResp OvsdbDBEntryFilter(const DBEntry *entry,
                                    const OvsdbDBEntry *ovsdb_entry);

    Agent *agent() const;
    void ManagedDelete();
    virtual void EmptyTable();
    Ip4Address dev_ip() const;
    uint32_t vxlan_id() const;
    const std::string &vn_name() const;
    const std::string &vrf_name() const;

    void UpdateParams(HaStaleDevVnEntry *dev_vn);

private:
    friend class HaStaleL2RouteEntry;

    LifetimeRef<HaStaleL2RouteTable> table_delete_ref_;
    // take reference to a dummy dev_vn entry to hold peer till
    // route table is deleted. route object cannot hold reference
    // to dev_vn entry creating it, as it will cause cyclic dependency
    KSyncEntry::KSyncEntryPtr dev_vn_ref_;
    ConnectionStateEntry *state_;
    Ip4Address dev_ip_;
    uint32_t vxlan_id_;
    // take reference to the vrf while exporting route, to assure sanity
    // of vrf pointer even if Add route request fails, due to any reason
    VrfEntryRef vrf_;
    std::string vn_name_;
    DISALLOW_COPY_AND_ASSIGN(HaStaleL2RouteTable);
};

class HaStaleL2RouteEntry : public OvsdbDBEntry {
public:
    HaStaleL2RouteEntry(HaStaleL2RouteTable *table,
                          const std::string &mac);
    ~HaStaleL2RouteEntry();

    void AddMsg(struct ovsdb_idl_txn *);
    void ChangeMsg(struct ovsdb_idl_txn *);
    void DeleteMsg(struct ovsdb_idl_txn *);

    bool Sync(DBEntry*);
    bool IsLess(const KSyncEntry&) const;
    std::string ToString() const {return "Ha Stale L2 Route Entry";}
    KSyncEntry* UnresolvedReference();

    const std::string &mac() const;
    uint32_t vxlan_id() const;
    bool IsStale() const;

protected:
    virtual bool IsNoTxnEntry() { return true; }

private:
    friend class HaStaleL2RouteTable;
    friend class VrfRouteReflectorTable;

    void StaleClearCb();

    std::string mac_;
    uint32_t path_preference_;
    uint32_t vxlan_id_;
    uint64_t time_stamp_;
    DISALLOW_COPY_AND_ASSIGN(HaStaleL2RouteEntry);
};
};

#endif //SRC_VNSW_AGENT_OVS_TOR_AGENT_OVSDB_CLIENT_HA_STALE_L2_ROUTE_H_

