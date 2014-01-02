/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_mpls_ksync_h
#define vnsw_agent_mpls_ksync_h

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include "oper/mpls.h"
#include "ksync/mpls_ksync.h"
#include "ksync/nexthop_ksync.h"

class MplsKSyncObject;

class MplsKSyncEntry : public KSyncNetlinkDBEntry {
public:
    MplsKSyncEntry(const MplsKSyncEntry *entry, uint32_t index,
                   MplsKSyncObject* obj);
    MplsKSyncEntry(const MplsLabel *label, MplsKSyncObject* obj);
    virtual ~MplsKSyncEntry();

    NHKSyncEntry *nh() const {
        return static_cast<NHKSyncEntry *>(nh_.get());
    }
    KSyncDBObject *GetObject();

    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);
    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);
    void FillObjectLog(sandesh_op::type op, KSyncMplsInfo &info) const;
private:
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    uint32_t label_;
    KSyncEntryPtr nh_;
    MplsKSyncObject *ksync_obj_;
    DISALLOW_COPY_AND_ASSIGN(MplsKSyncEntry);
};

class MplsKSyncObject : public KSyncDBObject {
public:
    static const int kMplsIndexCount = 10000;
    MplsKSyncObject(Agent *agent);
    virtual ~MplsKSyncObject();
    Agent *agent() const { return agent_; }
    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    void RegisterDBClients();
private:
    Agent *agent_;
    DISALLOW_COPY_AND_ASSIGN(MplsKSyncObject);
};

#endif // vnsw_agent_mpls_ksync_h
