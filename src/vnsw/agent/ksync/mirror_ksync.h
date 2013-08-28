/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_mirror_ksync_h
#define vnsw_agent_mirror_ksync_h

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include "oper/nexthop.h"
#include "oper/mirror_table.h"

class MirrorKSyncEntry : public KSyncNetlinkDBEntry {
public:
    MirrorKSyncEntry(const MirrorKSyncEntry *entry, uint32_t index) : 
        KSyncNetlinkDBEntry(index), vrf_id_(entry->vrf_id_), 
        sip_(entry->sip_), sport_(entry->sport_), dip_(entry->dip_), 
        dport_(entry->dport_), analyzer_name_(entry->analyzer_name_) {
    }
    MirrorKSyncEntry(const MirrorEntry *);
    MirrorKSyncEntry(const uint32_t vrf_id, uint32_t dip, uint16_t dport) :
        KSyncNetlinkDBEntry(kInvalidIndex), vrf_id_(vrf_id), dip_(dip), 
        dport_(dport) {
    }
    MirrorKSyncEntry(std::string &analyzer_name) :
        analyzer_name_(analyzer_name) { }

    virtual ~MirrorKSyncEntry() {};

    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);
    virtual char *AddMsg(int &len);
    virtual char *ChangeMsg(int &len);
    virtual char *DeleteMsg(int &len);
    uint32_t GetIdx(Ip4Address &dip, uint16_t dport);
    KSyncDBObject *GetObject();

    NHKSyncEntry *GetNH() const {
        return static_cast<NHKSyncEntry *>(nh_.get());
    }
private:
    char *Encode(sandesh_op::type op, int &len);
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
    MirrorKSyncObject(DBTableBase *table) : 
        KSyncDBObject(table, kMirrorIndexCount) {};

    virtual ~MirrorKSyncObject() { };

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index) {
        const MirrorKSyncEntry *mirror_entry = 
            static_cast<const MirrorKSyncEntry *>(entry);
        MirrorKSyncEntry *ksync = new MirrorKSyncEntry(mirror_entry, index);
        return static_cast<KSyncEntry *>(ksync);
    };

    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) {
        const MirrorEntry *mirror_entry = static_cast<const MirrorEntry *>(e);
        MirrorKSyncEntry *ksync = new MirrorKSyncEntry(mirror_entry);
        return static_cast<KSyncEntry *>(ksync);
    }

    static void Init(MirrorTable *table) {
        assert(singleton_ == NULL);
        singleton_ = new MirrorKSyncObject(table);
    };

    static void Shutdown() {
        delete singleton_;
        singleton_ = NULL;
    }

    static uint32_t GetIdx(std::string analyzer_name) {
        MirrorKSyncEntry key(analyzer_name);
        return GetKSyncObject()->Find(&key)->GetIndex();
    }

    static MirrorKSyncObject *GetKSyncObject() { return singleton_; };

private:
    static MirrorKSyncObject *singleton_;
    DISALLOW_COPY_AND_ASSIGN(MirrorKSyncObject);
};
#endif // vnsw_agent_mirror_ksync_h
