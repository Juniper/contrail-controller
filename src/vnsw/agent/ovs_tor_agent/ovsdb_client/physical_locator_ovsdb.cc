/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
#include <ovsdb_wrapper.h>
};

#include <ovsdb_client_idl.h>
#include <physical_locator_ovsdb.h>
#include <ovsdb_types.h>

using OVSDB::PhysicalLocatorTable;
using OVSDB::PhysicalLocatorEntry;

PhysicalLocatorEntry::PhysicalLocatorEntry(PhysicalLocatorTable *table,
        const std::string &dip_str) : OvsdbEntry(table), dip_(dip_str),
    create_req_owner_(NULL) {
}

PhysicalLocatorEntry::~PhysicalLocatorEntry() {
    assert(create_req_owner_ == NULL);
}

bool PhysicalLocatorEntry::IsLess(const KSyncEntry &entry) const {
    const PhysicalLocatorEntry &pl_entry =
        static_cast<const PhysicalLocatorEntry&>(entry);
    return (dip_ < pl_entry.dip_);
}

KSyncEntry *PhysicalLocatorEntry::UnresolvedReference() {
    return NULL;
}

bool PhysicalLocatorEntry::AcquireCreateRequest(KSyncEntry *creator) {
    if (create_req_owner_ == NULL) {
        create_req_owner_ = creator;
        return true;
    }
    return false;
}

void PhysicalLocatorEntry::ReleaseCreateRequest(KSyncEntry *creator) {
    assert(create_req_owner_ == creator);
    if (!IsResolved() && create_req_owner_ != NULL) {
        // if entry is not resolved before clear Create Request
        table_->BackRefReEval(this);
    }
    create_req_owner_ = NULL;
}

PhysicalLocatorTable::PhysicalLocatorTable(OvsdbClientIdl *idl) :
    OvsdbObject(idl) {
    idl->Register(OvsdbClientIdl::OVSDB_PHYSICAL_LOCATOR,
                  boost::bind(&PhysicalLocatorTable::OvsdbNotify, this, _1, _2));
}

PhysicalLocatorTable::~PhysicalLocatorTable() {
}

void PhysicalLocatorTable::OvsdbNotify(OvsdbClientIdl::Op op,
        struct ovsdb_idl_row *row) {
    const char *dip_str = ovsdb_wrapper_physical_locator_dst_ip(row);
    PhysicalLocatorEntry key(this, dip_str);
    PhysicalLocatorEntry *entry =
        static_cast<PhysicalLocatorEntry *>(Find(&key));
    if (op == OvsdbClientIdl::OVSDB_DEL) {
        if (entry != NULL && entry->IsActive()) {
            OVSDB_TRACE(Trace, "Delete received for Physical Locator " +
                    entry->dip_);
            Delete(entry);
        }
    } else if (op == OvsdbClientIdl::OVSDB_ADD) {
        if (entry == NULL) {
            entry = static_cast<PhysicalLocatorEntry *>(Create(&key));
            entry->ovs_entry_ = row;
        } else if (!entry->IsActive()) {
            entry->ovs_entry_ = row;
            // Activate entry by triggering change
            Change(entry);
        }
        OVSDB_TRACE(Trace, "Add received for Physical Locator " +
                    entry->dip_);
    } else {
        assert(0);
    }
}

KSyncEntry *PhysicalLocatorTable::Alloc(const KSyncEntry *key, uint32_t index) {
    const PhysicalLocatorEntry *k_entry =
        static_cast<const PhysicalLocatorEntry *>(key);
    PhysicalLocatorEntry *entry = new PhysicalLocatorEntry(this, k_entry->dip_);
    return entry;
}

