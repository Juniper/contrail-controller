/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <base/logging.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>
#include "oper/interface_common.h"
#include "oper/vrf.h"
#include "oper/vrf_assign.h"
#include "oper/mirror_table.h"
#include "ksync/interface_ksync.h"
#include "ksync/nexthop_ksync.h"
#include "ksync/vrf_assign_ksync.h"
#include "ksync_init.h"

VrfAssignKSyncEntry::VrfAssignKSyncEntry(VrfAssignKSyncObject* obj,
                                         const VrfAssignKSyncEntry *entry, 
                                         uint32_t index) :
    KSyncNetlinkDBEntry(index), ksync_obj_(obj), interface_(entry->interface_),
    vlan_tag_(entry->vlan_tag_), vrf_id_(entry->vrf_id_),
    nh_(entry->nh_) {
}

VrfAssignKSyncEntry::VrfAssignKSyncEntry(VrfAssignKSyncObject* obj,
                                         const VrfAssign *vassign) :
    KSyncNetlinkDBEntry(kInvalidIndex), ksync_obj_(obj) {

    InterfaceKSyncObject *intf_object = 
        ksync_obj_->ksync()->interface_ksync_obj();
    InterfaceKSyncEntry intf(intf_object, vassign->GetInterface());

    interface_ = static_cast<InterfaceKSyncEntry *>
        (intf_object->GetReference(&intf));
    assert(interface_);

    assert(vassign->GetType() == VrfAssign::VLAN);
    const VlanVrfAssign *vlan = static_cast<const VlanVrfAssign *>(vassign);
    vlan_tag_ = vlan->GetVlanTag();

    if (vassign->GetVrf()) {
        vrf_id_ = vassign->GetVrf()->vrf_id();
    } else {
        vrf_id_ = VIF_VRF_INVALID;
    }
}

VrfAssignKSyncEntry::~VrfAssignKSyncEntry() {
}

KSyncDBObject *VrfAssignKSyncEntry::GetObject() {
    return ksync_obj_; 
}

bool VrfAssignKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const VrfAssignKSyncEntry &entry = 
        static_cast<const VrfAssignKSyncEntry &>(rhs);

    if (interface() != entry.interface()) {
        return interface() < entry.interface();
    }

    return vlan_tag() < entry.vlan_tag();
}

std::string VrfAssignKSyncEntry::ToString() const {
    std::stringstream s;
    InterfaceKSyncEntry *intf = interface();

    s << "VRF Assign : ";
    if (intf) {
        s << "Interface : " << intf->interface_name() << 
             " Intf-Service-Vlan : " << 
             (intf->has_service_vlan() == true ? "Enable" : "Disable");
    } else { 
        s << "Interface : <NULL> ";
    }

    s << " Tag : " << vlan_tag();
    const VrfEntry* vrf =
        ksync_obj_->ksync()->agent()->vrf_table()->FindVrfFromId(vrf_id_);
    if (vrf) {
        s << " Vrf : " << vrf->GetName();
    }

    return s.str();
}

bool VrfAssignKSyncEntry::Sync(DBEntry *e) {
    bool ret = false;
    const VrfAssign *vassign = static_cast<VrfAssign *>(e);
    uint16_t vrf_id = 0;

    if (vassign->GetVrf()) {
        vrf_id = vassign->GetVrf()->vrf_id();
    } else {
        vrf_id = VIF_VRF_INVALID;
    }

    if (vrf_id_ != vrf_id) {
        vrf_id_ = vrf_id;
        ret = true;
    }

    const VlanVrfAssign *vlan = static_cast<const VlanVrfAssign *>(vassign);
    vlan_tag_ = vlan->GetVlanTag();
    NHKSyncObject *nh_object =
        ksync_obj_->ksync()->nh_ksync_obj();
    NHKSyncEntry nh(nh_object, vlan->nh());
    if (nh_ != static_cast<NHKSyncEntry*>(nh_object->GetReference(&nh))) {
        nh_ = static_cast<NHKSyncEntry*>(nh_object->GetReference(&nh));
        ret = true;
    }

    return ret;
}

InterfaceKSyncEntry *VrfAssignKSyncEntry::interface() const {
    return static_cast<InterfaceKSyncEntry *>(interface_.get());
}

NHKSyncEntry *VrfAssignKSyncEntry::nh() const {
    return static_cast<NHKSyncEntry *>(nh_.get());
}

int VrfAssignKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_vrf_assign_req encoder;
    int encode_len, error;
    InterfaceKSyncEntry *intf = interface();

    encoder.set_h_op(op);
    encoder.set_var_vif_index(intf->interface_id());
    encoder.set_var_vlan_id(vlan_tag_);
    encoder.set_var_vif_vrf(vrf_id_);
    encoder.set_var_nh_id(nh()->nh_id());
    encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    LOG(DEBUG, "VRF Assign for Interface <" << intf->interface_name() << 
        "> Tag <" << vlan_tag() << "> Vrf <" << vrf_id_ << ">");
    return encode_len;
}

int VrfAssignKSyncEntry::AddMsg(char *buf, int buf_len) {
    LOG(DEBUG, "VrfAssign: Add");
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int VrfAssignKSyncEntry::ChangeMsg(char *buf, int buf_len){
    return AddMsg(buf, buf_len);
}

int VrfAssignKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    LOG(DEBUG, "VrfAssign: Delete");
    return Encode(sandesh_op::DELETE, buf, buf_len);
}

KSyncEntry *VrfAssignKSyncEntry::UnresolvedReference() {
    InterfaceKSyncEntry *intf = interface();
    if (!intf->IsResolved()) {
        return intf;
    }
    NHKSyncEntry *ksync_nh = nh();
    if (!ksync_nh->IsResolved()) {
        return ksync_nh;
    }
    return NULL;
}

VrfAssignKSyncObject::VrfAssignKSyncObject(KSync *ksync) 
    : KSyncDBObject(), ksync_(ksync) {
}

VrfAssignKSyncObject::~VrfAssignKSyncObject() {
}

void VrfAssignKSyncObject::RegisterDBClients() {
    RegisterDb(ksync_->agent()->vrf_assign_table());
}

KSyncEntry *VrfAssignKSyncObject::Alloc(const KSyncEntry *ke, uint32_t index) {
    const VrfAssignKSyncEntry *rule = 
        static_cast<const VrfAssignKSyncEntry *>(ke);
    VrfAssignKSyncEntry *ksync = new VrfAssignKSyncEntry(this, rule, index);
    return static_cast<KSyncEntry *>(ksync);
}

KSyncEntry *VrfAssignKSyncObject::DBToKSyncEntry(const DBEntry *e) {
    const VrfAssign *rule = static_cast<const VrfAssign *>(e);
    VrfAssignKSyncEntry *key = new VrfAssignKSyncEntry(this, rule);
    return static_cast<KSyncEntry *>(key);
}

void vr_vrf_assign_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->VrfAssignMsgHandler(this);
}
