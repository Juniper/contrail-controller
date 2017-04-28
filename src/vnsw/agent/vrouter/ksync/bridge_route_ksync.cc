/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <base/string_util.h>
#include <cmn/agent.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/ksync/bridge_route_ksync.h>
#include <vrouter/ksync/route_ksync.h>

BridgeRouteKSyncObject::BridgeRouteKSyncObject(KSync *ksync) :
    KSyncObject("KSync BridgeRouteTable"), ksync_(ksync) {
}

BridgeRouteKSyncObject::~BridgeRouteKSyncObject() {
}

KSyncEntry *BridgeRouteKSyncObject::Alloc(const KSyncEntry *key, uint32_t idx) {
    const RouteKSyncEntry *route = static_cast<const RouteKSyncEntry *>(key);
    RouteKSyncEntry *ksync = new RouteKSyncEntry(this, route, idx);
    return static_cast<KSyncEntry *>(ksync);
}
