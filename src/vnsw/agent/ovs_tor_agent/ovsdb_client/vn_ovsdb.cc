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

#include <unicast_mac_local_ovsdb.h>
#include <multicast_mac_local_ovsdb.h>
#include <vn_ovsdb.h>

using OVSDB::OvsdbDBEntry;
using OVSDB::VnOvsdbEntry;
using OVSDB::VnOvsdbObject;

VnOvsdbEntry::VnOvsdbEntry(VnOvsdbObject *table,
        const boost::uuids::uuid &uuid) : OvsdbDBEntry(table), uuid_(uuid),
        vrf_(NULL, this), vxlan_id_(0), name_("") {
}

void VnOvsdbEntry::AddMsg(struct ovsdb_idl_txn *txn) {
    VnEntry *vn = static_cast<VnEntry *>(GetDBEntry());
    vrf_ = vn->GetVrf();
}

void VnOvsdbEntry::ChangeMsg(struct ovsdb_idl_txn *txn) {
    UnicastMacLocalOvsdb *uc_obj =
        table_->client_idl()->unicast_mac_local_ovsdb();
    // Entries in Unicast Mac Local Table are dependent on vn/vrf
    // and VxLAN ID on Change of this entry trigger Vrf re-eval for
    // entries in Unicast Mac Local Table
    uc_obj->VrfReEvalEnqueue(vrf_.get());

    // Update VRF with the current value.
    VnEntry *vn = static_cast<VnEntry *>(GetDBEntry());
    vrf_ = vn->GetVrf();
}

void VnOvsdbEntry::DeleteMsg(struct ovsdb_idl_txn *txn) {
    UnicastMacLocalOvsdb *uc_obj =
        table_->client_idl()->unicast_mac_local_ovsdb();
    // Entries in Unicast Mac Local Table are dependent on vn/vrf
    // on delete of this entry trigger Vrf re-eval for entries in
    // Unicast Mac Local Table
    uc_obj->VrfReEvalEnqueue(vrf_.get());
    //For multicast vrf delete needs to known for deleting
    //route.
    MulticastMacLocalOvsdb *mc_obj =
        table_->client_idl()->multicast_mac_local_ovsdb();
    mc_obj->VrfReEvalEnqueue(vrf_.get());
    vrf_ = NULL;
}

bool VnOvsdbEntry::Sync(DBEntry *db_entry) {
    VnEntry *vn = static_cast<VnEntry *>(db_entry);
    uint32_t vxlan_id = vn->GetVxLanId();
    bool ret = false;

    if (vrf_.get() != vn->GetVrf()) {
        // Update Vrf After taking action on previous vrf in Add/Change
        ret = true;
    }

    if (vxlan_id_ != vxlan_id) {
        vxlan_id_ = vxlan_id;
        ret = true;
    }

    if (name_ != vn->GetName()) {
        name_ = vn->GetName();
        ret = true;
    }

    return ret;
}

bool VnOvsdbEntry::IsLess(const KSyncEntry &entry) const {
    const VnOvsdbEntry &vn_entry = static_cast<const VnOvsdbEntry&>(entry);
    return uuid_ < vn_entry.uuid_;
}

KSyncEntry *VnOvsdbEntry::UnresolvedReference() {
    return NULL;
}

VrfEntry *VnOvsdbEntry::vrf() {
    return vrf_.get();
}

VnOvsdbObject::VnOvsdbObject(OvsdbClientIdl *idl, DBTable *table) :
    OvsdbDBObject(idl, table, false) {
}

VnOvsdbObject::~VnOvsdbObject() {
}

KSyncEntry *VnOvsdbObject::Alloc(const KSyncEntry *key, uint32_t index) {
    const VnOvsdbEntry *k_entry =
        static_cast<const VnOvsdbEntry *>(key);
    VnOvsdbEntry *entry = new VnOvsdbEntry(this, k_entry->uuid_);
    return entry;
}

KSyncEntry *VnOvsdbObject::DBToKSyncEntry(const DBEntry* db_entry) {
    const VnEntry *entry = static_cast<const VnEntry *>(db_entry);
    VnOvsdbEntry *key = new VnOvsdbEntry(this, entry->GetUuid());
    return static_cast<KSyncEntry *>(key);
}

KSyncDBObject::DBFilterResp VnOvsdbObject::OvsdbDBEntryFilter(
        const DBEntry *entry, const OvsdbDBEntry *ovsdb_entry) {
    const VnEntry *vn = static_cast<const VnEntry *>(entry);
    // only accept Virtual Networks with non-NULL vrf
    // and non zero vxlan id
    if (vn->GetVrf() == NULL || vn->GetVxLanId() == 0) {
        return DBFilterDelete;
    }
    return DBFilterAccept;
}

