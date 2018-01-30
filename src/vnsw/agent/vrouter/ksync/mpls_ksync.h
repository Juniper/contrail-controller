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
#include <ksync/ksync_netlink.h>
#include "oper/mpls.h"
#include "vrouter/ksync/nexthop_ksync.h"

class MplsKSyncObject;

class MplsKSyncEntry : public KSyncNetlinkDBEntry {
public:
    MplsKSyncEntry(MplsKSyncObject* obj, const MplsKSyncEntry *entry,
                   uint32_t index);
    MplsKSyncEntry(MplsKSyncObject* obj, const MplsLabel *label);
    MplsKSyncEntry(MplsKSyncObject* obj, uint32_t mapls_label);
    virtual ~MplsKSyncEntry();

    NHKSyncEntry *nh() const {
        return static_cast<NHKSyncEntry *>(nh_.get());
    }
    KSyncDBObject *GetObject() const;

    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);
    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);
    void FillObjectLog(sandesh_op::type op, KSyncMplsInfo &info) const;
    virtual void StaleTimerExpired();

private:
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    MplsKSyncObject *ksync_obj_;
    uint32_t label_;
    KSyncEntryPtr nh_;
    DISALLOW_COPY_AND_ASSIGN(MplsKSyncEntry);
};

class MplsKSyncObject : public KSyncDBObject {
public:
    static const int kMplsIndexCount = 128 * 1024; // support 128K mpls labels
    MplsKSyncObject(KSync *ksync);
    virtual ~MplsKSyncObject();
    KSync *ksync() const { return ksync_; }
    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    void RegisterDBClients();
    virtual void RestoreVrouterEntriesReq(void);
    virtual void ReadVrouterEntriesResp(Sandesh *sandesh);
    virtual void ProcessVrouterEntries(KSyncRestoreData::Ptr restore_data);
    virtual KSyncEntry *CreateStale(const KSyncEntry *key);
private:
    KSync *ksync_;
    DISALLOW_COPY_AND_ASSIGN(MplsKSyncObject);
};

// ksync restore 
class MplsKSyncRestoreData  : public KSyncRestoreData {
public:
    typedef boost::shared_ptr<KMplsInfo> KMplsInfoPtr;
    MplsKSyncRestoreData(KSyncDBObject *obj, KMplsInfoPtr mplsInfo);
    ~MplsKSyncRestoreData();
    virtual const std::string ToString() { return "";}
    KMplsInfo *GetMplsEntry() { return mplsInfo_.get();}
private:
    KMplsInfoPtr mplsInfo_;
};
#endif // vnsw_agent_mpls_ksync_h
