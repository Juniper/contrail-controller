/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_AGENT_FORWARDING_CLASS_KSYNC_H__
#define __VNSW_AGENT_FORWARDING_CLASS_KSYNC_H__

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include "oper/qos_queue.h"
#include "oper/forwarding_class.h"

class KSync;
class ForwardingClassKSyncObject;

class ForwardingClassKSyncEntry : public KSyncNetlinkDBEntry {
public:
    ForwardingClassKSyncEntry(ForwardingClassKSyncObject *obj,
                              const ForwardingClassKSyncEntry *entry);
    ForwardingClassKSyncEntry(ForwardingClassKSyncObject *obj,
                              const ForwardingClass *fc);
    ForwardingClassKSyncEntry(ForwardingClassKSyncObject *obj,
                              uint32_t i);
    virtual ~ForwardingClassKSyncEntry();

    KSyncDBObject *GetObject();
    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);
    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);
    uint32_t id() const { return id_;}
    uint32_t dscp() const { return dscp_;}
    uint32_t vlan_priority() const { return vlan_priority_;}
    uint32_t mpls_exp() const { return mpls_exp_;}
    KSyncEntry* qos_queue_ksync() const {
        return qos_queue_ksync_.get();
    }
private:
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    KSyncDBObject *ksync_obj_;
    uint32_t id_;
    uint32_t dscp_;
    uint32_t vlan_priority_;
    uint32_t mpls_exp_;
    KSyncEntryPtr qos_queue_ksync_;
    DISALLOW_COPY_AND_ASSIGN(ForwardingClassKSyncEntry);
};

class ForwardingClassKSyncObject : public KSyncDBObject {
public:
    ForwardingClassKSyncObject(KSync *ksync);
    virtual ~ForwardingClassKSyncObject();
    KSync *ksync() const { return ksync_; }
    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    void RegisterDBClients();
    virtual KSyncDBObject::DBFilterResp DBEntryFilter(const DBEntry *entry,
                                                      const KSyncDBEntry*ksync);
private:
    KSync *ksync_;
    DISALLOW_COPY_AND_ASSIGN(ForwardingClassKSyncObject);
};
#endif
