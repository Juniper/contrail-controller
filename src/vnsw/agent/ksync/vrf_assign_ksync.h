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
#include "oper/vrf_assign.h"

class VrfAssignKSyncEntry : public KSyncNetlinkDBEntry {
public:
    VrfAssignKSyncEntry(const VrfAssignKSyncEntry *entry, uint32_t index) :
        KSyncNetlinkDBEntry(index), interface_(entry->interface_),
        vlan_tag_(entry->vlan_tag_), vrf_id_(entry->vrf_id_) { };

    VrfAssignKSyncEntry(const VrfAssign *rule);
    virtual ~VrfAssignKSyncEntry() {};

    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);
    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);
    KSyncDBObject *GetObject();

    uint16_t GetVlanTag() const {return vlan_tag_;};
    uint16_t GetVrfId() const {return vrf_id_;};

    IntfKSyncEntry *GetInterface() const {
        return static_cast<IntfKSyncEntry *>(interface_.get());
    }
private:
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    KSyncEntryPtr interface_;
    uint16_t vlan_tag_;
    uint16_t vrf_id_;
    DISALLOW_COPY_AND_ASSIGN(VrfAssignKSyncEntry);
};

class VrfAssignKSyncObject : public KSyncDBObject {
public:
    VrfAssignKSyncObject(DBTableBase *table) : KSyncDBObject(table) {};

    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index) {
        const VrfAssignKSyncEntry *rule = 
            static_cast<const VrfAssignKSyncEntry *>(entry);
        VrfAssignKSyncEntry *ksync = new VrfAssignKSyncEntry(rule, index);
        return static_cast<KSyncEntry *>(ksync);
    };

    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e) {
        const VrfAssign *rule = static_cast<const VrfAssign *>(e);
        VrfAssignKSyncEntry *key = new VrfAssignKSyncEntry(rule);
        return static_cast<KSyncEntry *>(key);
    }

    static void Init(VrfAssignTable *table) {
        assert(singleton_ == NULL);
        singleton_ = new VrfAssignKSyncObject(table);
    };

    static void Shutdown() {
        delete singleton_;
        singleton_ = NULL;
    }

    static VrfAssignKSyncObject *GetKSyncObject() { return singleton_; };

private:
    static VrfAssignKSyncObject *singleton_;
    DISALLOW_COPY_AND_ASSIGN(VrfAssignKSyncObject);

};

#endif // vnsw_agent_vrf_assign_ksync_h
