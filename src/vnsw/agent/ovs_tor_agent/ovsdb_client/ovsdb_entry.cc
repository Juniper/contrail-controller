/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
extern "C" {
#include <ovsdb_wrapper.h>
}
#include <ovsdb_object.h>
#include <ovsdb_entry.h>
#include <ovsdb_types.h>

using OVSDB::OvsdbEntry;
using OVSDB::OvsdbDBEntry;
using OVSDB::OvsdbObject;
using OVSDB::OvsdbDBObject;

OvsdbEntry::OvsdbEntry(OvsdbObject *table) : KSyncEntry(), table_(table),
    ovs_entry_(NULL) {
}

OvsdbEntry::OvsdbEntry(OvsdbObject *table, uint32_t index) : KSyncEntry(index),
    table_(table), ovs_entry_(NULL) {
}

OvsdbEntry::~OvsdbEntry() {
}

bool OvsdbEntry::Add() {
    return true;
}

bool OvsdbEntry::Change() {
    return true;
}

bool OvsdbEntry::Delete() {
    return true;
}

KSyncObject *OvsdbEntry::GetObject() {
    return table_;
}

void OvsdbEntry::Ack(bool success) {
    // TODO we don't handle failures for these entries
    if (!success) {
        OVSDB_TRACE(Error, "Transaction failed for " + ToString());
    }

    OvsdbObject *object = static_cast<OvsdbObject*>(GetObject());
    object->SafeNotifyEvent(this, ack_event_);
}

OvsdbDBEntry::OvsdbDBEntry(OvsdbDBObject *table) : KSyncDBEntry(), table_(table),
    ovs_entry_(NULL) {
}

OvsdbDBEntry::OvsdbDBEntry(OvsdbDBObject *table, struct ovsdb_idl_row *ovs_entry) : KSyncDBEntry(),
    table_(table), ovs_entry_(ovs_entry) {
}

OvsdbDBEntry::~OvsdbDBEntry() {
}

bool OvsdbDBEntry::Add() {
    OvsdbDBObject *object = static_cast<OvsdbDBObject*>(GetObject());
    // trigger pre add/change only if idl is not marked deleted.
    // we should not update KSync references as, these references eventually
    // need to be released as part of delete trigger due to cleanup.
    if (object->client_idl_ != NULL && !object->client_idl_->deleted()) {
        PreAddChange();
    }

    if (IsNoTxnEntry()) {
        // trigger AddMsg with NULL pointer and return true to complete
        // KSync state
        AddMsg(NULL);
        return true;
    }

    struct ovsdb_idl_txn *txn;
    if (UseBulkTxn()) {
        txn = object->client_idl_->CreateBulkTxn(this, KSyncEntry::ADD_ACK);
    } else {
        txn = object->client_idl_->CreateTxn(this, KSyncEntry::ADD_ACK);
    }

    if (txn == NULL) {
        // failed to create transaction because of idl marked for
        // deletion return from here.
        TxnDoneNoMessage();
        return true;
    }
    AddMsg(txn);
    return object->client_idl_->EncodeSendTxn(txn, this);
}

bool OvsdbDBEntry::Change() {
    OvsdbDBObject *object = static_cast<OvsdbDBObject*>(GetObject());
    // trigger pre add/change only if idl is not marked deleted.
    // we should not update KSync references as, these references eventually
    // need to be released as part of delete trigger due to cleanup.
    if (object->client_idl_ != NULL && !object->client_idl_->deleted()) {
        PreAddChange();
    }

    if (IsNoTxnEntry()) {
        // trigger ChangeMsg with NULL pointer and return true to complete
        // KSync state
        ChangeMsg(NULL);
        return true;
    }

    struct ovsdb_idl_txn *txn;
    if (UseBulkTxn()) {
        txn = object->client_idl_->CreateBulkTxn(this, KSyncEntry::CHANGE_ACK);
    } else {
        txn = object->client_idl_->CreateTxn(this, KSyncEntry::CHANGE_ACK);
    }

    if (txn == NULL) {
        // failed to create transaction because of idl marked for
        // deletion return from here.
        TxnDoneNoMessage();
        return true;
    }
    ChangeMsg(txn);
    return object->client_idl_->EncodeSendTxn(txn, this);
}

bool OvsdbDBEntry::Delete() {
    if (IsNoTxnEntry()) {
        // trigger DeleteMsg with NULL pointer
        DeleteMsg(NULL);
        // trigger Post Delete, to allow post delete processing
        PostDelete();
        // return true to complete KSync State
        return true;
    }

    OvsdbDBObject *object = static_cast<OvsdbDBObject*>(GetObject());
    struct ovsdb_idl_txn *txn;
    if (UseBulkTxn()) {
        txn = object->client_idl_->CreateBulkTxn(this, KSyncEntry::DEL_ACK);
    } else {
        txn = object->client_idl_->CreateTxn(this, KSyncEntry::DEL_ACK);
    }
    if (txn == NULL) {
        // failed to create transaction because of idl marked for
        // deletion return from here.
        TxnDoneNoMessage();
        return true;
    }
    DeleteMsg(txn);
    bool ret = object->client_idl_->EncodeSendTxn(txn, this);
    // trigger post delete if we are not waiting for Ack
    // otherwise trigger post delete on Ack
    if (ret) {
        // current transaction send completed trigger post delete
        PostDelete();
    }
    return ret;
}

bool OvsdbDBEntry::IsDataResolved() {
    return (ovs_entry_ == NULL) ? false : true;
}

bool OvsdbDBEntry::IsDelAckWaiting() {
    KSyncState state = GetState();
    return (state >= DEL_DEFER_DEL_ACK && state <= RENEW_WAIT);
}

bool OvsdbDBEntry::IsAddChangeAckWaiting() {
    KSyncState state = GetState();
    return (state == SYNC_WAIT || state == NEED_SYNC ||
            state == DEL_DEFER_SYNC);
}

void OvsdbDBEntry::NotifyAdd(struct ovsdb_idl_row *row) {
    if (ovs_entry_ == NULL) {
        ovs_entry_ = row;
    } else {
        // ovs_entry should never change from Non-NULL value to
        // another Non-NULL value.
        assert(ovs_entry_ == row);
    }
    OvsdbChange();
}

void OvsdbDBEntry::NotifyDelete(struct ovsdb_idl_row *row) {
    ovs_entry_ = NULL;
}

KSyncObject *OvsdbDBEntry::GetObject() {
    return table_;
}

void OvsdbDBEntry::Ack(bool success) {
    OvsdbDBObject *object = static_cast<OvsdbDBObject*>(GetObject());

    // trigger post delete for Del Ack
    if (ack_event_ == KSyncEntry::DEL_ACK) {
        // current transaction send completed trigger post delete
        PostDelete();
    }

    if (success) {
        if (IsDelAckWaiting())
            object->SafeNotifyEvent(this, KSyncEntry::DEL_ACK);
        else if (IsAddChangeAckWaiting())
            object->SafeNotifyEvent(this, KSyncEntry::ADD_ACK);
        else
            delete this;
    } else {
        bool trigger_ack = false;
        // On Failure try again
        if (IsDelAckWaiting()) {
            OVSDB_TRACE(Error, "Delete Transaction failed for " + ToString());
            // if we are waiting to renew, dont retry delete.
            if (GetState() != RENEW_WAIT) {
                // trigger ack when if no message to send, on calling delete
                trigger_ack = Delete();
            } else {
                trigger_ack = true;
            }

            if (trigger_ack) {
                object->SafeNotifyEvent(this, KSyncEntry::DEL_ACK);
            }
        } else if (IsAddChangeAckWaiting()) {
            OVSDB_TRACE(Error, "Add Transaction failed for " + ToString());
            // if we are waiting to delete, dont retry add.
            if (GetState() != DEL_DEFER_SYNC) {
                // Enqueue a change before generating an ADD_ACK to keep
                // the entry in a not sync'd state.
                object->Change(this);
            }

            object->SafeNotifyEvent(this, KSyncEntry::ADD_ACK);
        } else {
            // should never happen
            assert(0);
        }
    }
}

void OvsdbDBEntry::TriggerDeleteAdd() {
    if (IsDeleted() || stale()) {
        // skip this operation for Deleted/stale Entry, After this operation
        // Deleted/Stale entry would become active/non-stale which is not
        // intended use of this API, caller must ensure the entry to be Active
        // if this operation is required
        return;
    }
    GetObject()->SafeNotifyEvent(this, KSyncEntry::DEL_ADD_REQ);
}
