/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_AGENT_QOS_CONFIG_KSYNC_H__
#define __VNSW_AGENT_QOS_CONFIG_KSYNC_H__

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include "oper/qos_queue.h"
#include "oper/forwarding_class.h"
#include "oper/qos_config.h"
class KSync;
class QosConfigKSyncObject;

class QosConfigKSyncEntry : public KSyncNetlinkDBEntry {
public:
    static const uint32_t kDefaultQosMsgSize = 4096;
    typedef std::pair<uint32_t, KSyncEntryPtr> KSyncQosFcPair;
    typedef std::map<uint32_t, KSyncEntryPtr> KSyncQosFcMap;

    QosConfigKSyncEntry(QosConfigKSyncObject *obj,
                        const QosConfigKSyncEntry *entry);
    QosConfigKSyncEntry(QosConfigKSyncObject *obj,
                        const AgentQosConfig *qc);
    virtual ~QosConfigKSyncEntry();

    KSyncDBObject *GetObject();
    virtual bool IsLess(const KSyncEntry &rhs) const;
    virtual std::string ToString() const;
    virtual KSyncEntry *UnresolvedReference();
    virtual bool Sync(DBEntry *e);
    virtual int AddMsg(char *buf, int buf_len);
    virtual int ChangeMsg(char *buf, int buf_len);
    virtual int DeleteMsg(char *buf, int buf_len);
    boost::uuids::uuid uuid() const { return uuid_;}
    uint32_t id() const { return id_;}
    int MsgLen() { return kDefaultQosMsgSize; }
private:
    bool CopyQosMap(KSyncQosFcMap &ksync_map,
                    const AgentQosConfig::QosIdForwardingClassMap *map);

    KSyncDBObject *ksync_obj_;
    boost::uuids::uuid uuid_;
    uint32_t id_;
    KSyncQosFcMap dscp_map_;
    KSyncQosFcMap vlan_priority_map_;
    KSyncQosFcMap mpls_exp_map_;
    uint32_t default_forwarding_class_;
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    DISALLOW_COPY_AND_ASSIGN(QosConfigKSyncEntry);
};

class QosConfigKSyncObject : public KSyncDBObject {
public:
    QosConfigKSyncObject(KSync *ksync);
    virtual ~QosConfigKSyncObject();
    KSync *ksync() const { return ksync_; }
    virtual KSyncEntry *Alloc(const KSyncEntry *entry, uint32_t index);
    virtual KSyncEntry *DBToKSyncEntry(const DBEntry *e);
    void RegisterDBClients();
private:
    KSync *ksync_;
    DISALLOW_COPY_AND_ASSIGN(QosConfigKSyncObject);
};
#endif
