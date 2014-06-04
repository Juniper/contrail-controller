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
#include <ksync/ksync_netlink.h>
#include "oper/nexthop.h"
#include "oper/vxlan.h"
#include "ksync/agent_ksync_types.h"

class VxLanKSyncObject;

class VxLanIdKSyncEntry : public KSyncNetlinkDBEntry {
public:
    VxLanIdKSyncEntry(VxLanKSyncObject *obj, const VxLanIdKSyncEntry *entry, 
                      uint32_t index);
    VxLanIdKSyncEntry(VxLanKSyncObject *obj, const VxLanId *label);
    virtual ~VxLanIdKSyncEntry();

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
    void FillObjectLog(sandesh_op::type op, KSyncVxLanInfo &info) const;
private:
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    VxLanKSyncObject *ksync_obj_;
    uint32_t label_;
    KSyncEntryPtr nh_;
    DISALLOW_COPY_AND_ASSIGN(VxLanIdKSyncEntry);
};

class VxLanKSyncObject : public KSyncDBObject {
public:
    static const int kVxLanIndexCount = 10000;
    VxLanKSyncObject(KSync *ksync);
    virtual ~VxLanKSyncObject();
    KSync *ksync() const { return ksync_; }
    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    void RegisterDBClients();
private:
    KSync *ksync_;
    DISALLOW_COPY_AND_ASSIGN(VxLanKSyncObject);
};

#endif // vnsw_agent_vxlan_ksync_h
