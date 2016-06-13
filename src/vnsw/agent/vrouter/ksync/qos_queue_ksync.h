/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_AGENT_QOS_QUEUE_KSYNC_H__
#define __VNSW_AGENT_QOS_QUEUE_KSYNC_H__

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include "oper/qos_queue.h"

class KSync;
class QosQueueKSyncObject;

class QosQueueKSyncEntry : public KSyncNetlinkDBEntry {
public:
    QosQueueKSyncEntry(QosQueueKSyncObject *obj, const QosQueueKSyncEntry *entry);
    QosQueueKSyncEntry(QosQueueKSyncObject *obj, const QosQueue *qos_queue);
    QosQueueKSyncEntry(QosQueueKSyncObject *obj, uint32_t qos_queue);
    virtual ~QosQueueKSyncEntry();

    KSyncDBObject *GetObject();
    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);
    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);
    uint32_t id() const { return id_;}
private:
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    KSyncDBObject *ksync_obj_;
    uint32_t id_;
    DISALLOW_COPY_AND_ASSIGN(QosQueueKSyncEntry);
};

class QosQueueKSyncObject : public KSyncDBObject {
public:
    QosQueueKSyncObject(KSync *ksync);
    virtual ~QosQueueKSyncObject();
    KSync *ksync() const { return ksync_; }
    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t id);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    void RegisterDBClients();
private:
    KSync *ksync_;
    DISALLOW_COPY_AND_ASSIGN(QosQueueKSyncObject);
};
#endif
