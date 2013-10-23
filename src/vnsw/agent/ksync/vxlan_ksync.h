/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vxlan_ksync_h
#define vnsw_agent_vxlan_ksync_h

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include "oper/nexthop.h"
#include "oper/vxlan.h"
#include "ksync/agent_ksync_types.h"

class VxLanIdKSyncEntry : public KSyncNetlinkDBEntry {
public:
    VxLanIdKSyncEntry(const VxLanIdKSyncEntry *entry, uint32_t index) : 
        KSyncNetlinkDBEntry(index), label_(entry->label_), nh_(NULL) { };

    VxLanIdKSyncEntry(const VxLanId *label);
    virtual ~VxLanIdKSyncEntry() {};

    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);
    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);
    KSyncDBObject *GetObject();

    uint32_t GetLabel() const {return label_;};
    NHKSyncEntry *GetNH() const {
        return static_cast<NHKSyncEntry *>(nh_.get());
    }
    void FillObjectLog(sandesh_op::type op, KSyncVxLanInfo &info);
private:
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    uint32_t label_;
    KSyncEntryPtr nh_;
    DISALLOW_COPY_AND_ASSIGN(VxLanIdKSyncEntry);
};

class VxLanKSyncObject : public KSyncDBObject {
public:
    static const int kVxLanIndexCount = 10000;
    VxLanKSyncObject(DBTableBase *table) : 
        KSyncDBObject(table, kVxLanIndexCount) {};

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index) {
        const VxLanIdKSyncEntry *vxlan = 
            static_cast<const VxLanIdKSyncEntry *>(entry);
        VxLanIdKSyncEntry *ksync = new VxLanIdKSyncEntry(vxlan, index);
        return static_cast<KSyncEntry *>(ksync);
    };

    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) {
        const VxLanId *vxlan = static_cast<const VxLanId *>(e);
        VxLanIdKSyncEntry *key = new VxLanIdKSyncEntry(vxlan);
        return static_cast<KSyncEntry *>(key);
    }

    static void Init(VxLanTable *table) {
        assert(singleton_ == NULL);
        singleton_ = new VxLanKSyncObject(table);
    };

    static void Shutdown() {
        delete singleton_;
        singleton_ = NULL;
    }

    static VxLanKSyncObject *GetKSyncObject() { return singleton_; };

private:
    static VxLanKSyncObject *singleton_;
    DISALLOW_COPY_AND_ASSIGN(VxLanKSyncObject);

};

#endif // vnsw_agent_vxlan_ksync_h
