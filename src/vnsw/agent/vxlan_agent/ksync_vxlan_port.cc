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

KSyncVxlanPortEntry::KSyncVxlanPortEntry(KSyncVxlanPortObject *obj,
                                         const KSyncVxlanPortEntry *entry) :
    KSyncDBEntry(), type_(entry->type_), port_name_(entry->port_name_),
    bridge_(entry->bridge_), ksync_obj_(obj) {
}

KSyncVxlanPortEntry::KSyncVxlanPortEntry(KSyncVxlanPortObject *obj,
                                         const Interface *interface) :
    KSyncDBEntry(), type_(interface->type()), port_name_(interface->name()),
    bridge_(NULL), ksync_obj_(obj) {
}

KSyncVxlanPortEntry::~KSyncVxlanPortEntry() {
}

KSyncDBObject *KSyncVxlanPortEntry::GetObject() {
    return ksync_obj_;
}

bool KSyncVxlanPortEntry::IsLess(const KSyncEntry &rhs) const {
    const KSyncVxlanPortEntry &entry = static_cast
        <const KSyncVxlanPortEntry &>(rhs);
    return port_name_ < entry.port_name_;
}

std::string KSyncVxlanPortEntry::ToString() const {
    std::stringstream s;
    s << "Interface : " << port_name_;
    return s.str();
}

bool KSyncVxlanPortEntry::Sync(DBEntry *e) {
    bool ret = false;
    if (type_ != Interface::VM_INTERFACE) {
        return ret;
    }

    VmInterface *vm_interface = static_cast<VmInterface *>(e);
    KSyncVxlanBridgeEntry *bridge = NULL;
    const VnEntry *vn = static_cast<const VnEntry *>(vm_interface->vn());
    const VxLanId *vxlan = NULL;
    if (vn)
        vxlan = vn->vxlan_id();
    if (vxlan != NULL) {
        KSyncVxlanBridgeObject *bridge_obj = ksync_obj_->ksync()->bridge_obj();
        KSyncEntry *key = bridge_obj->DBToKSyncEntry(vxlan);
        bridge = static_cast<KSyncVxlanBridgeEntry *>
            (bridge_obj->GetReference(key));
        delete key;
        assert(bridge);
    } else {
        bridge_ = NULL;
    }

    if (bridge_ != bridge) {
        bridge_ = bridge;
        ret = true;
    }

    return ret;
}

KSyncEntry *KSyncVxlanPortEntry::UnresolvedReference() {
    if (type_ != Interface::VM_INTERFACE) {
        return NULL;
    }

    if (bridge_ == NULL) {
        return KSyncVxlan::defer_entry();
    }

    if ((bridge_->IsResolved() == false)) {
        return bridge_;
    }

    return NULL;
}

KSyncVxlanPortObject::KSyncVxlanPortObject(KSyncVxlan *ksync) :
    KSyncDBObject(), ksync_(ksync) {
}

void KSyncVxlanPortObject::RegisterDBClients() {
    RegisterDb(ksync_->agent()->interface_table());
}

KSyncVxlanPortObject::~KSyncVxlanPortObject() {
}

void KSyncVxlanPortObject::Init() {
}

void KSyncVxlanPortObject::Shutdown() {
}
