/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrf_assign_ksync_h
#define vnsw_agent_vrf_assign_ksync_h

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include "oper/vrf_assign.h"

class VrfAssignKSyncObject;

class VrfAssignKSyncEntry : public KSyncNetlinkDBEntry {
public:
    VrfAssignKSyncEntry(VrfAssignKSyncObject* obj, 
                        const VrfAssignKSyncEntry *entry, uint32_t index);
    VrfAssignKSyncEntry(VrfAssignKSyncObject* obj, const VrfAssign *rule);
    virtual ~VrfAssignKSyncEntry();

    uint16_t vlan_tag() const {return vlan_tag_;};
    InterfaceKSyncEntry *interface() const;
    NHKSyncEntry *nh() const;
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
    VrfAssignKSyncObject *ksync_obj_;
    KSyncEntryPtr interface_;
    uint16_t vlan_tag_;
    uint16_t vrf_id_;
    KSyncEntryPtr nh_;
    DISALLOW_COPY_AND_ASSIGN(VrfAssignKSyncEntry);
};

class VrfAssignKSyncObject : public KSyncDBObject {
public:
    VrfAssignKSyncObject(KSync *ksync);
    virtual ~VrfAssignKSyncObject();

    KSync *ksync() const { return ksync_; }

    void RegisterDBClients();
    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
private:
    KSync *ksync_;
    DISALLOW_COPY_AND_ASSIGN(VrfAssignKSyncObject);
};

#endif // vnsw_agent_vrf_assign_ksync_h
