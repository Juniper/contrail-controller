/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

extern "C" {
#include <ovsdb_wrapper.h>
};

#include <ovsdb_entry.h>
#include <ovsdb_object.h>

using OVSDB::OvsdbObject;
using OVSDB::OvsdbDBObject;
using OVSDB::OvsdbDBEntry;

OvsdbObject::OvsdbObject(OvsdbClientIdl *idl) : KSyncObject(),
    client_idl_(idl) {
}

OvsdbObject::~OvsdbObject() {
}

bool OvsdbObject::IsActiveEntry(KSyncEntry *entry) {
    return (entry->GetState() != KSyncEntry::TEMP && !entry->IsDeleted());
}

KSyncEntry *OvsdbObject::FindActiveEntry(KSyncEntry *key) {
    KSyncEntry *entry = Find(key);
    if (entry != NULL && IsActiveEntry(entry)) {
        return entry;
    }
    return NULL;
}

void OvsdbObject::DeleteTable(void) {
    client_idl_->ksync_obj_manager()->Delete(this);
}

void OvsdbObject::EmptyTable(void) {
    if (delete_scheduled()) {
        client_idl_ = NULL;
    }
}

OvsdbDBObject::OvsdbDBObject(OvsdbClientIdl *idl,
                             bool init_stale_entry_cleanup) : KSyncDBObject(),
    client_idl_(idl), walkid_(DBTableWalker::kInvalidWalkerId) {
    if (init_stale_entry_cleanup) {
        InitStaleEntryCleanup(*(idl->agent()->event_manager())->io_service(),
                              StaleEntryCleanupTimer, StaleEntryYeildTimer,
                              StaleEntryDeletePerIteration);
    }
}

OvsdbDBObject::OvsdbDBObject(OvsdbClientIdl *idl, DBTable *tbl,
                             bool init_stale_entry_cleanup) :
    KSyncDBObject(tbl), client_idl_(idl),
    walkid_(DBTableWalker::kInvalidWalkerId) {
    DBTableWalker *walker = client_idl_->agent()->db()->GetWalker();
    // Start a walker to get the entries which were already present,
    // when we register to the DB Table
    walkid_ = walker->WalkTable(tbl, NULL,
            boost::bind(&OvsdbDBObject::DBWalkNotify, this, _1, _2),
            boost::bind(&OvsdbDBObject::DBWalkDone, this, _1));
    if (init_stale_entry_cleanup) {
        InitStaleEntryCleanup(*(idl->agent()->event_manager())->io_service(),
                              StaleEntryCleanupTimer, StaleEntryYeildTimer,
                              StaleEntryDeletePerIteration);
    }
}

OvsdbDBObject::~OvsdbDBObject() {
    if (walkid_ != DBTableWalker::kInvalidWalkerId) {
        DBTableWalker *walker = client_idl_->agent()->db()->GetWalker();
        walker->WalkCancel(walkid_);
    }
}

void OvsdbDBObject::OvsdbRegisterDBTable(DBTable *tbl) {
    assert(client_idl_.get() != NULL);
    RegisterDb(tbl);
    DBTableWalker *walker = client_idl_->agent()->db()->GetWalker();
    // Start a walker to get the entries which were already present,
    // when we register to the DB Table
    walkid_ = walker->WalkTable(tbl, NULL,
            boost::bind(&OvsdbDBObject::DBWalkNotify, this, _1, _2),
            boost::bind(&OvsdbDBObject::DBWalkDone, this, _1));
}

void OvsdbDBObject::NotifyAddOvsdb(OvsdbDBEntry *key, struct ovsdb_idl_row *row) {
    OvsdbDBEntry *entry = static_cast<OvsdbDBEntry *>(Find(key));
    // Check if entry is present and active. in case if we find an entry
    // which is inactive, we will need to alloc ovs entry and trigger
    // delete to update ovsdb-server
    if (entry && entry->IsActive()) {
        // trigger notify add for the entry, to update ovs_idl state
        entry->NotifyAdd(row);
    } else {
        OvsdbDBEntry *del_entry = AllocOvsEntry(row);
        del_entry->ovs_entry_ = row;
        // entry created by AllocOvsEntry should always be stale
        assert(del_entry->stale());
    }
}

void OvsdbDBObject::NotifyDeleteOvsdb(OvsdbDBEntry *key) {
    OvsdbDBEntry *entry = static_cast<OvsdbDBEntry *>(Find(key));
    if (entry) {
        // trigger notify delete for the entry, to reset ovs_idl state
        entry->NotifyDelete();
        if (!entry->IsDelAckWaiting()) {
            // we were not waiting for delete to happen, state for
            // OVSDB server and client mismatch happend.
            // Trigger Add/Change Req on entry to resync
            NotifyEvent(entry, KSyncEntry::ADD_CHANGE_REQ);
        }
    }
}

bool OvsdbDBObject::DBWalkNotify(DBTablePartBase *part, DBEntryBase *entry) {
    Notify(part, entry);
    return true;
}

void OvsdbDBObject::DBWalkDone(DBTableBase *partition) {
    walkid_ = DBTableWalker::kInvalidWalkerId;
}

void OvsdbDBObject::DeleteTable(void) {
    client_idl_->ksync_obj_manager()->Delete(this);
}

void OvsdbDBObject::EmptyTable(void) {
    if (delete_scheduled()) {
        client_idl_ = NULL;
    }
}

