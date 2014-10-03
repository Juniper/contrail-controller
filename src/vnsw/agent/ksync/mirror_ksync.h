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
#include <ksync/ksync_netlink.h>
#include <ksync/nexthop_ksync.h>
#include "oper/mirror_table.h"

class MirrorKSyncObject;

class MirrorKSyncEntry : public KSyncNetlinkDBEntry {
public:
    MirrorKSyncEntry(MirrorKSyncObject *obj, const MirrorEntry *);
    MirrorKSyncEntry(MirrorKSyncObject *obj, const MirrorKSyncEntry *entry, 
                     uint32_t index);
    MirrorKSyncEntry(MirrorKSyncObject *obj, const uint32_t vrf_id, 
                     uint32_t dip, uint16_t dport);
    MirrorKSyncEntry(MirrorKSyncObject *obj, std::string &analyzer_name);
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
    MirrorKSyncObject *ksync_obj_;
    uint32_t vrf_id_;
    Ip4Address sip_;
    uint16_t   sport_;
    Ip4Address dip_;
    uint16_t   dport_;
    KSyncEntryPtr nh_;
    std::string analyzer_name_;
    DISALLOW_COPY_AND_ASSIGN(MirrorKSyncEntry);
};

class MirrorKSyncObject : public KSyncDBObject {
public:
    static const int kMirrorIndexCount = 1000;
    MirrorKSyncObject(KSync *ksync);
    virtual ~MirrorKSyncObject();

    KSync *ksync() const { return ksync_; }

    void RegisterDBClients();
    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    uint32_t GetIdx(std::string analyzer_name);
private:
    KSync *ksync_;
    DISALLOW_COPY_AND_ASSIGN(MirrorKSyncObject);
};

#endif // vnsw_agent_mirror_ksync_h
