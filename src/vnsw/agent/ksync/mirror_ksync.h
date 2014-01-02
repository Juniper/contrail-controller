/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_mirror_ksync_h
#define vnsw_agent_mirror_ksync_h

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/nexthop_ksync.h>
#include "oper/mirror_table.h"

class MirrorKSyncObject;

class MirrorKSyncEntry : public KSyncNetlinkDBEntry {
public:
    MirrorKSyncEntry(const MirrorEntry *, MirrorKSyncObject *obj);
    MirrorKSyncEntry(const MirrorKSyncEntry *entry, uint32_t index,
                     MirrorKSyncObject *obj);
    MirrorKSyncEntry(const uint32_t vrf_id, uint32_t dip, uint16_t dport,
                     MirrorKSyncObject *obj);
    MirrorKSyncEntry(std::string &analyzer_name, MirrorKSyncObject *obj);
    virtual ~MirrorKSyncEntry();

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
private:
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    uint32_t vrf_id_;
    Ip4Address sip_;
    uint16_t   sport_;
    Ip4Address dip_;
    uint16_t   dport_;
    MirrorKSyncObject *ksync_obj_;
    KSyncEntryPtr nh_;
    std::string analyzer_name_;
    DISALLOW_COPY_AND_ASSIGN(MirrorKSyncEntry);
};

class MirrorKSyncObject : public KSyncDBObject {
public:
    static const int kMirrorIndexCount = 1000;
    MirrorKSyncObject(Agent *agent);
    virtual ~MirrorKSyncObject();

    Agent *agent() const { return agent_; }

    void RegisterDBClients();
    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    uint32_t GetIdx(std::string analyzer_name);
private:
    Agent *agent_;
    DISALLOW_COPY_AND_ASSIGN(MirrorKSyncObject);
};

#endif // vnsw_agent_mirror_ksync_h
