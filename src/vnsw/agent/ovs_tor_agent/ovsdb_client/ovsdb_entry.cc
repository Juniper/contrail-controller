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

OvsdbEntry::OvsdbEntry(OvsdbObject *table) : KSyncEntry(), table_(table) {
}

OvsdbEntry::OvsdbEntry(OvsdbObject *table, uint32_t index) : KSyncEntry(index),
    table_(table) {
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
    PreAddChange();
    OvsdbDBObject *object = static_cast<OvsdbDBObject*>(GetObject());
    struct ovsdb_idl_txn *txn = object->client_idl_->CreateTxn(this);
    AddMsg(txn);
    struct jsonrpc_msg *msg = ovsdb_wrapper_idl_txn_encode(txn);
    if (msg == NULL) {
        object->client_idl()->DeleteTxn(txn);
        return true;
    }
    object->client_idl_->SendJsonRpc(msg);
    return false;
}

bool OvsdbDBEntry::Change() {
    PreAddChange();
    OvsdbDBObject *object = static_cast<OvsdbDBObject*>(GetObject());
    struct ovsdb_idl_txn *txn = object->client_idl_->CreateTxn(this);
    ChangeMsg(txn);
    struct jsonrpc_msg *msg = ovsdb_wrapper_idl_txn_encode(txn);
    if (msg == NULL) {
        object->client_idl()->DeleteTxn(txn);
        return true;
    }
    object->client_idl_->SendJsonRpc(msg);
    return false;
}

bool OvsdbDBEntry::Delete() {
    OvsdbDBObject *object = static_cast<OvsdbDBObject*>(GetObject());
    struct ovsdb_idl_txn *txn = object->client_idl_->CreateTxn(this);
    DeleteMsg(txn);
    struct jsonrpc_msg *msg = ovsdb_wrapper_idl_txn_encode(txn);
    PostDelete();
    if (msg == NULL) {
        object->client_idl()->DeleteTxn(txn);
        return true;
    }
    object->client_idl_->SendJsonRpc(msg);
    return false;
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
    ovs_entry_ = row;
    OvsdbChange();
}

void OvsdbDBEntry::NotifyDelete() {
    ovs_entry_ = NULL;
}

KSyncObject *OvsdbDBEntry::GetObject() {
    return table_;
}

void OvsdbDBEntry::Ack(bool success) {
    OvsdbDBObject *object = static_cast<OvsdbDBObject*>(GetObject());
    // TODO we are not retrying as of now,
    // success = true;
    if (success) {
        if (IsDelAckWaiting())
            object->NotifyEvent(this, KSyncEntry::DEL_ACK);
        else if (IsAddChangeAckWaiting())
            object->NotifyEvent(this, KSyncEntry::ADD_ACK);
        else
            delete this;
    } else {
        // On Failure try again
        if (IsDelAckWaiting()) {
            OVSDB_TRACE(Error, "Delete Transaction failed for " + ToString());
            //object->NotifyEvent(this, KSyncEntry::DEL_ACK);
            // if we are waiting to renew, dont retry delete.
            if (GetState() != RENEW_WAIT) {
                Delete();
            } else {
                object->NotifyEvent(this, KSyncEntry::DEL_ACK);
            }
        } else if (IsAddChangeAckWaiting()) {
            OVSDB_TRACE(Error, "Add Transaction failed for " + ToString());
            //object->NotifyEvent(this, KSyncEntry::ADD_ACK);
            // if we are waiting to delete, dont retry add.
            if (GetState() != DEL_DEFER_SYNC) {
                Add();
            } else {
                object->NotifyEvent(this, KSyncEntry::ADD_ACK);
            }
        } else {
            OVSDB_TRACE(Error, "Ovsdb Delete Transaction failed for " + ToString());
            object->OvsdbNotify(OvsdbClientIdl::OVSDB_ADD, ovs_entry_);
            delete this;
            //Delete();
        }
    }
}
