/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_bridge_route_ksync_h
#define vnsw_agent_bridge_route_ksync_h

#include <string>
#include <net/mac_address.h>
#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <vrouter/ksync/agent_ksync_types.h>
#include <vrouter/ksync/ksync_flow_memory.h>

class KSync;

class BridgeRouteKSyncObject : public KSyncObject {
public:
    BridgeRouteKSyncObject(KSync *ksync);
    virtual ~BridgeRouteKSyncObject();

    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    KSync *ksync() const { return ksync_; }

private:
    KSync *ksync_;
    DISALLOW_COPY_AND_ASSIGN(BridgeRouteKSyncObject);
};

#endif // vnsw_agent_bridge_route_ksync_h
