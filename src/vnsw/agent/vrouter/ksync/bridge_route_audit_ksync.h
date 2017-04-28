/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_bridge_route_audit_ksync_h
#define vnsw_agent_bridge_route_audit_ksync_h

#include <string>
#include <net/mac_address.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <vrouter/ksync/agent_ksync_types.h>
#include <vrouter/ksync/ksync_flow_memory.h>

class BridgeRouteAuditKSyncObject;
class KSync;

class BridgeRouteAuditKSyncEntry : public KSyncNetlinkEntry {
public:
    BridgeRouteAuditKSyncEntry(BridgeRouteAuditKSyncObject *obj,
                               uint32_t vrf_id, const MacAddress &mac);
    BridgeRouteAuditKSyncEntry(BridgeRouteAuditKSyncObject* obj,
                               const BridgeRouteAuditKSyncEntry *entry);
    virtual ~BridgeRouteAuditKSyncEntry();

    int EncodeDelete(char *buf, int buf_len);
    KSyncObject *GetObject() const;
    virtual bool Sync();
    virtual KSyncEntry *UnresolvedReference();
    void FillObjectLog(sandesh_op::type type, KSyncRouteInfo &info) const;

    std::string ToString() const;
    bool IsLess(const KSyncEntry &rhs) const;
    int AddMsg(char *buf, int buf_len);
    int ChangeMsg(char *buf, int buf_len);
    int DeleteMsg(char *buf, int buf_len);

private:
    BridgeRouteAuditKSyncObject *ksync_obj_;
    uint32_t vrf_id_;
    MacAddress mac_;
    DISALLOW_COPY_AND_ASSIGN(BridgeRouteAuditKSyncEntry);
};

class BridgeRouteAuditKSyncObject : public KSyncObject {
public:
    BridgeRouteAuditKSyncObject(KSync *ksync);
    virtual ~BridgeRouteAuditKSyncObject();

    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    KSync *ksync() const { return ksync_; }

private:
    KSync *ksync_;
    DISALLOW_COPY_AND_ASSIGN(BridgeRouteAuditKSyncObject);
};

#endif // vnsw_agent_bridge_route_audit_ksync_h
