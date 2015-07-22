/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
#include <ovsdb_wrapper.h>
};

#include <oper/vn.h>
#include <oper/vrf.h>
#include <oper/vxlan.h>
#include <ovsdb_types.h>

#include <ha_stale_vn.h>

using namespace OVSDB;

HaStaleVnEntry::HaStaleVnEntry(HaStaleVnTable *table,
        const boost::uuids::uuid &uuid) : OvsdbDBEntry(table), uuid_(uuid) {
}

void HaStaleVnEntry::AddMsg(struct ovsdb_idl_txn *txn) {
}

void HaStaleVnEntry::ChangeMsg(struct ovsdb_idl_txn *txn) {
}

void HaStaleVnEntry::DeleteMsg(struct ovsdb_idl_txn *txn) {
}

bool HaStaleVnEntry::Sync(DBEntry *db_entry) {
    VnEntry *vn = static_cast<VnEntry *>(db_entry);
    if (bridge_table_ != vn->GetVrf()->GetBridgeRouteTable()) {
        bridge_table_ = vn->GetVrf()->GetBridgeRouteTable();
        return true;
    }
    return false;
}

bool HaStaleVnEntry::IsLess(const KSyncEntry &entry) const {
    const HaStaleVnEntry &vn_entry = static_cast<const HaStaleVnEntry&>(entry);
    return uuid_ < vn_entry.uuid_;
}

KSyncEntry *HaStaleVnEntry::UnresolvedReference() {
    return NULL;
}

AgentRouteTable *HaStaleVnEntry::bridge_table() {
    return bridge_table_;
}

HaStaleVnTable::HaStaleVnTable(Agent *agent) :
    OvsdbDBObject(NULL, false), agent_(agent) {
    OvsdbRegisterDBTable((DBTable *)agent->vn_table());
}

HaStaleVnTable::~HaStaleVnTable() {
}

void HaStaleVnTable::OvsdbNotify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
}

KSyncEntry *HaStaleVnTable::Alloc(const KSyncEntry *key, uint32_t index) {
    const HaStaleVnEntry *k_entry =
        static_cast<const HaStaleVnEntry *>(key);
    HaStaleVnEntry *entry = new HaStaleVnEntry(this, k_entry->uuid_);
    return entry;
}

KSyncEntry *HaStaleVnTable::DBToKSyncEntry(const DBEntry* db_entry) {
    const VnEntry *entry = static_cast<const VnEntry *>(db_entry);
    HaStaleVnEntry *key = new HaStaleVnEntry(this, entry->GetUuid());
    return static_cast<KSyncEntry *>(key);
}

OvsdbDBEntry *HaStaleVnTable::AllocOvsEntry(struct ovsdb_idl_row *row) {
    return NULL;
}

KSyncDBObject::DBFilterResp HaStaleVnTable::OvsdbDBEntryFilter(
        const DBEntry *entry, const OvsdbDBEntry *ovsdb_entry) {
    const VnEntry *vn = static_cast<const VnEntry *>(entry);
    // only accept Virtual Networks with non-NULL vrf
    // and non zero vxlan id
    if (vn->GetVrf() == NULL || vn->GetVxLanId() == 0) {
        return DBFilterDelete;
    }
    return DBFilterAccept;
}

void HaStaleVnTable::EmptyTable(void) {
    OvsdbDBObject::EmptyTable();
    // unregister the object if emptytable is called with
    // object being scheduled for delete
    if (delete_scheduled()) {
        KSyncObjectManager::Unregister(this);
    }
}

Agent *HaStaleVnTable::GetAgentPtr() {
    return agent_;
}

