/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_mpls_ksync_h
#define vnsw_agent_mpls_ksync_h

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include "oper/nexthop.h"
#include "oper/mpls.h"
#include "ksync/agent_ksync_types.h"

class MplsKSyncEntry : public KSyncNetlinkDBEntry {
public:
    MplsKSyncEntry(const MplsKSyncEntry *entry, uint32_t index) : 
        KSyncNetlinkDBEntry(index), label_(entry->label_), nh_(NULL) { };

    MplsKSyncEntry(const MplsLabel *label);
    virtual ~MplsKSyncEntry() {};

    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);
    virtual char *AddMsg(int &len);
    virtual char *ChangeMsg(int &len);
    virtual char *DeleteMsg(int &len);
    KSyncDBObject *GetObject();

    uint32_t GetLabel() const {return label_;};
    NHKSyncEntry *GetNH() const {
        return static_cast<NHKSyncEntry *>(nh_.get());
    }
    void FillObjectLog(sandesh_op::type op, KSyncMplsInfo &info);
private:
    char *Encode(sandesh_op::type op, int &len);
    uint32_t label_;
    KSyncEntryPtr nh_;
    DISALLOW_COPY_AND_ASSIGN(MplsKSyncEntry);
};

class MplsKSyncObject : public KSyncDBObject {
public:
    static const int kMplsIndexCount = 10000;
    MplsKSyncObject(DBTableBase *table) : 
        KSyncDBObject(table, kMplsIndexCount) {};

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index) {
        const MplsKSyncEntry *mpls = static_cast<const MplsKSyncEntry *>(entry);
        MplsKSyncEntry *ksync = new MplsKSyncEntry(mpls, index);
        return static_cast<KSyncEntry *>(ksync);
    };

    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) {
        const MplsLabel *mpls = static_cast<const MplsLabel *>(e);
        MplsKSyncEntry *key = new MplsKSyncEntry(mpls);
        return static_cast<KSyncEntry *>(key);
    }

    static void Init(MplsTable *table) {
        assert(singleton_ == NULL);
        singleton_ = new MplsKSyncObject(table);
    };

    static void Shutdown() {
        delete singleton_;
        singleton_ = NULL;
    }

    static MplsKSyncObject *GetKSyncObject() { return singleton_; };

private:
    static MplsKSyncObject *singleton_;
    DISALLOW_COPY_AND_ASSIGN(MplsKSyncObject);

};

#endif // vnsw_agent_mpls_ksync_h
