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

KSyncEntry *OvsdbObject::FindActiveEntry(KSyncEntry *key) {
    KSyncEntry *entry = Find(key);
    if (entry != NULL && entry->GetState() != KSyncEntry::TEMP &&
            !entry->IsDeleted()) {
        return entry;
    }
    return NULL;
}

OvsdbDBObject::OvsdbDBObject(OvsdbClientIdl *idl) : KSyncDBObject(),
    client_idl_(idl), walkid_(DBTableWalker::kInvalidWalkerId) {
}

OvsdbDBObject::OvsdbDBObject(OvsdbClientIdl *idl, DBTable *tbl) :
    KSyncDBObject(tbl), client_idl_(idl),
    walkid_(DBTableWalker::kInvalidWalkerId) {
    DBTableWalker *walker = client_idl_->agent()->db()->GetWalker();
    // Start a walker to get the entries which were already present,
    // when we register to the DB Table
    walkid_ = walker->WalkTable(tbl, NULL,
            boost::bind(&OvsdbDBObject::DBWalkNotify, this, _1, _2),
            boost::bind(&OvsdbDBObject::DBWalkDone, this, _1));
}

OvsdbDBObject::~OvsdbDBObject() {
    if (walkid_ != DBTableWalker::kInvalidWalkerId) {
        DBTableWalker *walker = client_idl_->agent()->db()->GetWalker();
        walker->WalkCancel(walkid_);
    }
}

void OvsdbDBObject::NotifyAddOvsdb(OvsdbDBEntry *key, struct ovsdb_idl_row *row) {
    OvsdbDBEntry *entry = static_cast<OvsdbDBEntry *>(Find(key));
    if (entry) {
        if (entry->IsAddChangeAckWaiting()) {
            entry->NotifyAdd(row);
        }
    } else {
        //TODO trigger delete of this entry
        OvsdbDBEntry *del_entry = AllocOvsEntry(row);
        del_entry->ovs_entry_ = row;
        Delete(del_entry);
        //del_entry->Delete();
        //delete del_entry;
    }
}

void OvsdbDBObject::NotifyDeleteOvsdb(OvsdbDBEntry *key) {
    OvsdbDBEntry *entry = static_cast<OvsdbDBEntry *>(Find(key));
    if (entry) {
        if (entry->IsDelAckWaiting()) {
            entry->NotifyDelete();
        } else {
            // Clear OVS State and trigger Add/Change Req on entry
            entry->clear_ovs_entry();
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

