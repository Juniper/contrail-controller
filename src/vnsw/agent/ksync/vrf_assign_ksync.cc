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
#include <ksync/ksync_sock.h>

#include "oper/interface_common.h"
#include "oper/vrf.h"
#include "oper/vrf_assign.h"
#include "oper/mirror_table.h"

#include "ksync/interface_ksync.h"
#include "ksync/vrf_assign_ksync.h"

#include "ksync_init.h"

void vr_vrf_assign_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->VrfAssignMsgHandler(this);
}

VrfAssignKSyncObject *VrfAssignKSyncObject::singleton_;

KSyncDBObject *VrfAssignKSyncEntry::GetObject() {
    return VrfAssignKSyncObject::GetKSyncObject();
}

VrfAssignKSyncEntry::VrfAssignKSyncEntry(const VrfAssign *vassign) :
    KSyncNetlinkDBEntry(kInvalidIndex) {

    IntfKSyncObject *intf_object = IntfKSyncObject::GetKSyncObject();
    IntfKSyncEntry intf(vassign->GetInterface());

    interface_ = static_cast<IntfKSyncEntry *>
        (intf_object->GetReference(&intf));
    assert(interface_);

    assert(vassign->GetType() == VrfAssign::VLAN);
    const VlanVrfAssign *vlan = static_cast<const VlanVrfAssign *>(vassign);
    vlan_tag_ = vlan->GetVlanTag();

    if (vassign->GetVrf()) {
        vrf_id_ = vassign->GetVrf()->GetVrfId();
    } else {
        vrf_id_ = VIF_VRF_INVALID;
    }
}

bool VrfAssignKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const VrfAssignKSyncEntry &entry = static_cast<const VrfAssignKSyncEntry &>(rhs);

    if (GetInterface() != entry.GetInterface()) {
        return GetInterface() < entry.GetInterface();
    }

    return GetVlanTag() < entry.GetVlanTag();
}

std::string VrfAssignKSyncEntry::ToString() const {
    std::stringstream s;
    IntfKSyncEntry *intf = GetInterface();

    s << "VRF Assign : ";
    if (intf) {
        s << "Interface : " << intf->GetName() << " Intf-Service-Vlan : " <<
            (intf->HasServiceVlan() == true ? "Enable" : "Disable");
    } else { 
        s << "Interface : <NULL> ";
    }

    s << " Tag : " << GetVlanTag() << " Vrf : " << GetVrfId();
    return s.str();
}

bool VrfAssignKSyncEntry::Sync(DBEntry *e) {
    bool ret = false;
    const VrfAssign *vassign = static_cast<VrfAssign *>(e);
    uint16_t vrf_id = 0;

    if (vassign->GetVrf()) {
        vrf_id = vassign->GetVrf()->GetVrfId();
    } else {
        vrf_id = VIF_VRF_INVALID;
    }

    if (vrf_id_ != vrf_id) {
        vrf_id_ = vrf_id;
        ret = true;
    }

    return ret;
};

int VrfAssignKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_vrf_assign_req encoder;
    int encode_len, error;
    IntfKSyncEntry *intf = GetInterface();

    encoder.set_h_op(op);
    encoder.set_var_vif_index(intf->GetIndex());
    encoder.set_var_vlan_id(vlan_tag_);
    encoder.set_var_vif_vrf(vrf_id_);
    encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    LOG(DEBUG, "VRF Assign for Interface <" << intf->GetName() << "> Tag <" 
        << GetVlanTag() << "> Vrf <" << GetVrfId() << ">");
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
    IntfKSyncEntry *intf = GetInterface();
    if (!intf->IsResolved()) {
        return intf;
    }
    return NULL;
}
