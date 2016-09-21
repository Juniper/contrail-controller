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

OvsdbObject::OvsdbObject(OvsdbClientIdl *idl) : KSyncObject("OvsdbDBObject"),
    client_idl_(idl) {
}

OvsdbObject::~OvsdbObject() {
}

KSyncEntry *OvsdbObject::FindActiveEntry(KSyncEntry *key) {
    KSyncEntry *entry = Find(key);
    if (entry != NULL && entry->IsActive()) {
        return entry;
    }
    return NULL;
}

void OvsdbObject::DeleteTable(void) {
    client_idl_->ksync_obj_manager()->Delete(this);
    // trigger DeleteTableDone for derived class to take action on
    // delete table callback
    DeleteTableDone();
}

void OvsdbObject::EmptyTable(void) {
    if (delete_scheduled()) {
        client_idl_ = NULL;
    }
}

OvsdbDBObject::OvsdbDBObject(OvsdbClientIdl *idl,
                             bool init_stale_entry_cleanup) :
    KSyncDBObject("OvsdbDBObject"),
    client_idl_(idl), delete_triggered_(false) {
    if (init_stale_entry_cleanup) {
        InitStaleEntryCleanup(*(idl->agent()->event_manager())->io_service(),
                              StaleEntryCleanupTimer, StaleEntryYeildTimer,
                              StaleEntryDeletePerIteration);
    }
}

OvsdbDBObject::OvsdbDBObject(OvsdbClientIdl *idl, DBTable *tbl,
                             bool init_stale_entry_cleanup) :
    KSyncDBObject("OvsdbDBObject", tbl), client_idl_(idl),
    delete_triggered_(false) {
    // Start a walker to get the entries which were already present,
    // when we register to the DB Table
    walk_ref_ = tbl->AllocWalker(
            boost::bind(&OvsdbDBObject::DBWalkNotify, this, _1, _2),
            boost::bind(&OvsdbDBObject::DBWalkDone, this, _2));
    tbl->WalkAgain(walk_ref_);
    if (init_stale_entry_cleanup) {
        InitStaleEntryCleanup(*(idl->agent()->event_manager())->io_service(),
                              StaleEntryCleanupTimer, StaleEntryYeildTimer,
                              StaleEntryDeletePerIteration);
    }
}

OvsdbDBObject::~OvsdbDBObject() {
    assert(walk_ref_ == NULL);
}

void OvsdbDBObject::OvsdbRegisterDBTable(DBTable *tbl) {
    RegisterDb(tbl);
    // Start a walker to get the entries which were already present,
    // when we register to the DB Table
    if (walk_ref_.get() == NULL) {
        walk_ref_ = tbl->AllocWalker(
                      boost::bind(&OvsdbDBObject::DBWalkNotify, this, _1, _2),
                      boost::bind(&OvsdbDBObject::DBWalkDone, this, _2));
    }
    tbl->WalkAgain(walk_ref_);
}

void OvsdbDBObject::OvsdbStartResyncWalk() {
    (static_cast<DBTable*>(GetDBTable()))->WalkAgain(walk_ref_);
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
        // trigger notify add for the entry, to update ovs_idl state
        del_entry->NotifyAdd(row);
        // entry created by AllocOvsEntry should always be stale
        assert(del_entry->stale());
        // if delete of table is already triggered delete the created
        // stale entry immediately
        if (delete_triggered_) {
            Delete(del_entry);
        }
    }
}

void OvsdbDBObject::NotifyDeleteOvsdb(OvsdbDBEntry *key,
                                      struct ovsdb_idl_row *row) {
    OvsdbDBEntry *entry = static_cast<OvsdbDBEntry *>(Find(key));
    if (entry) {
        // trigger notify delete for the entry, to reset ovs_idl state
        entry->NotifyDelete(row);
        if (!entry->IsDelAckWaiting()) {
            // we were not waiting for delete to happen, state for
            // OVSDB server and client mismatch happend.
            // Trigger Add/Change Req on entry to resync
            SafeNotifyEvent(entry, KSyncEntry::ADD_CHANGE_REQ);
        }
    }
}

bool OvsdbDBObject::DBWalkNotify(DBTablePartBase *part, DBEntryBase *entry) {
    Notify(part, entry);
    return true;
}

void OvsdbDBObject::DBWalkDone(DBTableBase *partition) {
}

Agent *OvsdbDBObject::agent() const {
    return client_idl_->agent();
}

void OvsdbDBObject::DeleteTable(void) {
    // validate delete should not be triggered twice for an object
    assert(!delete_triggered_);
    delete_triggered_ = true;
    if (client_idl_ != NULL) {
        client_idl_->ksync_obj_manager()->Delete(this);
    } else {
        KSyncObjectManager::GetInstance()->Delete(this);
    }
    // trigger DeleteTableDone for derived class to take action on
    // delete table callback
    DeleteTableDone();
}

void OvsdbDBObject::EmptyTable(void) {
    if (delete_scheduled()) {
        if (walk_ref_.get() != NULL)
            (static_cast<DBTable*>(GetDBTable()))->ReleaseWalker(walk_ref_);
        walk_ref_ = NULL;
        client_idl_ = NULL;
    }
}

KSyncDBObject::DBFilterResp OvsdbDBObject::OvsdbDBEntryFilter(
        const DBEntry *entry, const OvsdbDBEntry *ovsdb_entry) {
    // Accept by default, unless overriden by dereived class
    return DBFilterAccept;
}

KSyncDBObject::DBFilterResp OvsdbDBObject::DBEntryFilter(
        const DBEntry *entry, const KSyncDBEntry *ksync) {
    // Ignore Add/Change notifications while client idl is deleted
    // there can be cases that the current table might be pending
    // for delete schedule in KSyncObjectManager Queue and in
    // the mean while we get a notification, where we should not
    // process it further.
    if (client_idl_ != NULL && client_idl_->deleted()) {
        return DBFilterIgnore;
    }

    return OvsdbDBEntryFilter(entry, static_cast<const OvsdbDBEntry *>(ksync));
}

