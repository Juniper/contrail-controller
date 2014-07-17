/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <cmn/agent_cmn.h>

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>

#include <vnc_cfg_types.h>
#include <bgp_schema_types.h>
#include <agent_types.h>

#include <oper/peer.h>
#include <oper/vrf.h>
#include <oper/interface_common.h>
#include <oper/nexthop.h>
#include <oper/multicast.h>
#include <oper/vn.h>
#include <oper/mirror_table.h>
#include <oper/vxlan.h>
#include <oper/mpls.h>
#include <oper/route_common.h>
#include <oper/layer2_route.h>

#include "ksync_vxlan.h"
#include "ksync_vxlan_bridge.h"
#include "ksync_vxlan_port.h"
#include "ksync_vxlan_route.h"

KSyncEntry *KSyncVxlan::defer_entry_;

KSyncVxlan::KSyncVxlan(Agent *agent) :
    agent_(agent), bridge_obj_(NULL), port_obj_(NULL), vrf_obj_(NULL) {
}

KSyncVxlan::~KSyncVxlan() {
}

KSyncVxlanBridgeObject *KSyncVxlan::bridge_obj() const {
    return bridge_obj_.get();
}

void KSyncVxlan::set_bridge_obj(KSyncVxlanBridgeObject *obj) {
    bridge_obj_.reset(obj);
}

KSyncVxlanPortObject *KSyncVxlan::port_obj() const {
    return port_obj_.get();
}

void KSyncVxlan::set_port_obj(KSyncVxlanPortObject *obj) {
    port_obj_.reset(obj);
}

KSyncVxlanVrfObject *KSyncVxlan::vrf_obj() const {
    return vrf_obj_.get();
}

void KSyncVxlan::set_vrf_obj(KSyncVxlanVrfObject *obj) {
    vrf_obj_.reset(obj);
}

KSyncEntry *KSyncVxlan::defer_entry() {
    return defer_entry_;
}

void KSyncVxlan::set_defer_entry(KSyncEntry *entry) {
    defer_entry_ = entry;
}

void KSyncVxlan::RegisterDBClients(DB *db) {
    KSyncObjectManager::Init();
    bridge_obj_.get()->RegisterDBClients();
    port_obj_.get()->RegisterDBClients();
    vrf_obj_.get()->RegisterDBClients();
}

void KSyncVxlan::Init() {
    bridge_obj_.get()->Init();
    port_obj_.get()->Init();
    vrf_obj_.get()->Init();
}

void KSyncVxlan::Shutdown() {
    bridge_obj_.reset(NULL);
    port_obj_.reset(NULL);
    vrf_obj_.reset(NULL);
    KSyncObjectManager::Shutdown();
}
