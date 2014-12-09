/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
#include <ovsdb_wrapper.h>
};
#include <vm_interface_ksync.h>

#include <oper/vn.h>
#include <oper/interface.h>
#include <oper/vm_interface.h>
#include <ovsdb_types.h>

using OVSDB::OvsdbDBEntry;
using OVSDB::VMInterfaceKSyncEntry;
using OVSDB::VMInterfaceKSyncObject;

VMInterfaceKSyncEntry::VMInterfaceKSyncEntry(VMInterfaceKSyncObject *table,
        const VmInterface *entry) : OvsdbDBEntry(table_),
    uuid_(entry->GetUuid()) {
}

VMInterfaceKSyncEntry::VMInterfaceKSyncEntry(VMInterfaceKSyncObject *table,
        const VMInterfaceKSyncEntry *key) : OvsdbDBEntry(table),
    uuid_(key->uuid_) {
}

VMInterfaceKSyncEntry::VMInterfaceKSyncEntry(VMInterfaceKSyncObject *table,
        boost::uuids::uuid uuid) : OvsdbDBEntry(table), uuid_(uuid) {
}

void VMInterfaceKSyncEntry::AddMsg(struct ovsdb_idl_txn *txn) {
}

void VMInterfaceKSyncEntry::ChangeMsg(struct ovsdb_idl_txn *txn) {
}

void VMInterfaceKSyncEntry::DeleteMsg(struct ovsdb_idl_txn *txn) {
}

bool VMInterfaceKSyncEntry::Sync(DBEntry *db_entry) {
    VmInterface *entry = static_cast<VmInterface *>(db_entry);

    std::string vn_name;
    if (entry->vn()) {
        vn_name = UuidToString(entry->vn()->GetUuid());
    }
    if (vn_name_ != vn_name) {
        vn_name_ = vn_name;
        return true;
    }
    return false;
}

bool VMInterfaceKSyncEntry::IsLess(const KSyncEntry &entry) const {
    const VMInterfaceKSyncEntry &intf_entry =
        static_cast<const VMInterfaceKSyncEntry&>(entry);
    return uuid_ < intf_entry.uuid_;
}

KSyncEntry *VMInterfaceKSyncEntry::UnresolvedReference() {
    if (vn_name_.empty()) {
        return table_->client_idl()->ksync_obj_manager()->default_defer_entry();
    }
    return NULL;
}

const std::string &VMInterfaceKSyncEntry::vn_name() const {
    return vn_name_;
}

VMInterfaceKSyncObject::VMInterfaceKSyncObject(OvsdbClientIdl *idl, DBTable *table) :
    OvsdbDBObject(idl, table) {
}

VMInterfaceKSyncObject::~VMInterfaceKSyncObject() {
}

void VMInterfaceKSyncObject::OvsdbNotify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
}

KSyncEntry *VMInterfaceKSyncObject::Alloc(const KSyncEntry *key, uint32_t index) {
    const VMInterfaceKSyncEntry *k_entry =
        static_cast<const VMInterfaceKSyncEntry *>(key);
    VMInterfaceKSyncEntry *entry = new VMInterfaceKSyncEntry(this, k_entry);
    return entry;
}

KSyncEntry *VMInterfaceKSyncObject::DBToKSyncEntry(const DBEntry* db_entry) {
    const VmInterface *entry = static_cast<const VmInterface *>(db_entry);
    VMInterfaceKSyncEntry *key = new VMInterfaceKSyncEntry(this, entry);
    return static_cast<KSyncEntry *>(key);
}

OvsdbDBEntry *VMInterfaceKSyncObject::AllocOvsEntry(struct ovsdb_idl_row *row) {
    return NULL;
}

KSyncDBObject::DBFilterResp VMInterfaceKSyncObject::DBEntryFilter(
        const DBEntry *entry) {
    const Interface *intf = static_cast<const Interface *>(entry);
    // only accept vm interfaces
    if (intf->type() != Interface::VM_INTERFACE) {
        return DBFilterIgnore;
    }
    return DBFilterAccept;
}

