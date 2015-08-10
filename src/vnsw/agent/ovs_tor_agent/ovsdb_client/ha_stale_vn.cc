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
#include <ha_stale_dev_vn.h>

using namespace OVSDB;

HaStaleVnEntry::HaStaleVnEntry(HaStaleVnTable *table,
        const boost::uuids::uuid &uuid) : OvsdbDBEntry(table), uuid_(uuid),
        vn_name_(""), bridge_table_(NULL) {
}

HaStaleVnEntry::~HaStaleVnEntry() {
}

void HaStaleVnEntry::AddMsg(struct ovsdb_idl_txn *txn) {
    HaStaleVnTable *table = static_cast<HaStaleVnTable *>(table_);
    if (table->dev_vn_table_ != NULL) {
        // trigger VnReEval for Any Change
        table->dev_vn_table_->VnReEvalEnqueue(uuid_);
    }
}

void HaStaleVnEntry::ChangeMsg(struct ovsdb_idl_txn *txn) {
    AddMsg(txn);
}

void HaStaleVnEntry::DeleteMsg(struct ovsdb_idl_txn *txn) {
    HaStaleVnTable *table = static_cast<HaStaleVnTable *>(table_);
    if (table->dev_vn_table_ != NULL) {
        table->dev_vn_table_->VnReEvalEnqueue(uuid_);
    }
}

bool HaStaleVnEntry::Sync(DBEntry *db_entry) {
    VnEntry *vn = static_cast<VnEntry *>(db_entry);
    bool change = false;
    if (vn_name_ != vn->GetName()) {
        vn_name_ = vn->GetName();
        change = true;
    }
    if (bridge_table_ != vn->GetVrf()->GetBridgeRouteTable()) {
        bridge_table_ = vn->GetVrf()->GetBridgeRouteTable();
        change = true;
    }
    return change;
}

bool HaStaleVnEntry::IsLess(const KSyncEntry &entry) const {
    const HaStaleVnEntry &vn_entry = static_cast<const HaStaleVnEntry&>(entry);
    return uuid_ < vn_entry.uuid_;
}

KSyncEntry *HaStaleVnEntry::UnresolvedReference() {
    return NULL;
}

const std::string &HaStaleVnEntry::vn_name() const {
    return vn_name_;
}

AgentRouteTable *HaStaleVnEntry::bridge_table() const {
    return bridge_table_;
}

HaStaleVnTable::HaStaleVnTable(Agent *agent, HaStaleDevVnTable *dev_vn_table) :
    OvsdbDBObject(NULL, false), agent_(agent), dev_vn_table_(dev_vn_table) {
    OvsdbRegisterDBTable((DBTable *)agent->vn_table());
}

HaStaleVnTable::~HaStaleVnTable() {
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

Agent *HaStaleVnTable::agent() const {
    return agent_;
}

void HaStaleVnTable::DeleteTableDone() {
    dev_vn_table_ = NULL;
}

