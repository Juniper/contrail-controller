/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/logging.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>
#include <vrouter/ksync/ksync_init.h>
#include "vrouter/ksync/qos_queue_ksync.h"

QosQueueKSyncEntry::QosQueueKSyncEntry(QosQueueKSyncObject *obj,
                                      const QosQueueKSyncEntry *qos_queue):
    KSyncNetlinkDBEntry(), ksync_obj_(obj), id_(qos_queue->id()) {
}

QosQueueKSyncEntry::QosQueueKSyncEntry(QosQueueKSyncObject *obj,
                                       const QosQueue *qos_queue):
    KSyncNetlinkDBEntry(), ksync_obj_(obj), id_(qos_queue->id()) {
}

QosQueueKSyncEntry::QosQueueKSyncEntry(QosQueueKSyncObject *obj,
                                       uint32_t id):
    KSyncNetlinkDBEntry(), ksync_obj_(obj), id_(id) {
}

QosQueueKSyncEntry::~QosQueueKSyncEntry() {
}

KSyncDBObject *QosQueueKSyncEntry::GetObject() {
    return ksync_obj_;
}

bool QosQueueKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const QosQueueKSyncEntry &entry = static_cast<const QosQueueKSyncEntry &>(rhs);
    return id_ < entry.id_;
}

std::string QosQueueKSyncEntry::ToString() const {

    std::stringstream s;
    s << "Qos Queue id " << id_;
    return s.str();
}

bool QosQueueKSyncEntry::Sync(DBEntry *e) {
    return false;
}

int QosQueueKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    return 0;
}

int QosQueueKSyncEntry::AddMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int QosQueueKSyncEntry::ChangeMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int QosQueueKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::DELETE, buf, buf_len);
}

KSyncEntry *QosQueueKSyncEntry::UnresolvedReference() {
    return NULL;
}

QosQueueKSyncObject::QosQueueKSyncObject(KSync *ksync):
    KSyncDBObject("KSync Qos Queue Object"), ksync_(ksync) {
}

QosQueueKSyncObject::~QosQueueKSyncObject() {

}

void QosQueueKSyncObject::RegisterDBClients() {
    RegisterDb(ksync_->agent()->qos_queue_table());
}

KSyncEntry*
QosQueueKSyncObject::Alloc(const KSyncEntry *e, uint32_t index) {
    const QosQueueKSyncEntry *entry = static_cast<const QosQueueKSyncEntry *>(e);
    QosQueueKSyncEntry *new_entry = new QosQueueKSyncEntry(this, entry);
    return static_cast<KSyncEntry *>(new_entry);
}

KSyncEntry *QosQueueKSyncObject::DBToKSyncEntry(const DBEntry *e) {
    const QosQueue *qos_queue = static_cast<const QosQueue *>(e);
    QosQueueKSyncEntry *entry = new QosQueueKSyncEntry(this, qos_queue);
    return static_cast<KSyncEntry *>(entry);
}
