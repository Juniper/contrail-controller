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

KSyncVxlanBridgeEntry::KSyncVxlanBridgeEntry(KSyncVxlanBridgeObject *obj,
                                            const KSyncVxlanBridgeEntry *entry):
    KSyncDBEntry(), vxlan_id_(entry->vxlan_id_), ksync_obj_(obj) {
}

KSyncVxlanBridgeEntry::KSyncVxlanBridgeEntry(KSyncVxlanBridgeObject *obj,
                                             const VxLanId *vxlan) :
    KSyncDBEntry(), vxlan_id_(vxlan->vxlan_id()), ksync_obj_(obj) {
}

KSyncVxlanBridgeEntry::~KSyncVxlanBridgeEntry() {
}

KSyncDBObject *KSyncVxlanBridgeEntry::GetObject() {
    return ksync_obj_;
}

bool KSyncVxlanBridgeEntry::IsLess(const KSyncEntry &rhs) const {
    const KSyncVxlanBridgeEntry &entry = static_cast
        <const KSyncVxlanBridgeEntry &>(rhs);
    return vxlan_id_ < entry.vxlan_id_;
}

std::string KSyncVxlanBridgeEntry::ToString() const {
    std::stringstream s;
    s << "Vxlan-ID : " << vxlan_id_;
    return s.str();
}

bool KSyncVxlanBridgeEntry::Sync(DBEntry *e) {
    return false;
}

KSyncEntry *KSyncVxlanBridgeEntry::UnresolvedReference() {
    return NULL;
}

KSyncVxlanBridgeObject::KSyncVxlanBridgeObject(KSyncVxlan *ksync) :
    KSyncDBObject(), ksync_(ksync) {
}

KSyncVxlanBridgeObject::~KSyncVxlanBridgeObject() {
}

void KSyncVxlanBridgeObject::RegisterDBClients() {
    RegisterDb(ksync_->agent()->vxlan_table());
}

void KSyncVxlanBridgeObject::Init() {
    VxLanId vxlan(0);
    KSyncEntry *key = DBToKSyncEntry(&vxlan);
    ksync_->set_defer_entry(GetReference(key));
    return;
}

void KSyncVxlanBridgeObject::Shutdown() {
}
