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

#include "oper/interface.h"
#include "oper/vrf.h"
#include "oper/vrf_assign.h"
#include "oper/mirror_table.h"

#include "ksync/interface_ksync.h"
#include "ksync/vrf_assign_ksync.h"

#include "nl_util.h"

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

char *VrfAssignKSyncEntry::Encode(sandesh_op::type op, int &len) {
    struct nl_client cl;
    vr_vrf_assign_req encoder;
    int encode_len, error, ret;
    uint8_t *buf;
    uint32_t buf_len;
    IntfKSyncEntry *intf = GetInterface();

    nl_init_generic_client_req(&cl, KSyncSock::GetNetlinkFamilyId());

    if ((ret = nl_build_header(&cl, &buf, &buf_len)) < 0) {
        LOG(DEBUG, "Error creating vassign message. Error : " << ret);
        return NULL;
    }

    encoder.set_h_op(op);
    encoder.set_var_vif_index(intf->GetIndex());
    encoder.set_var_vlan_id(vlan_tag_);
    encoder.set_var_vif_vrf(vrf_id_);
    encode_len = encoder.WriteBinary(buf, buf_len, &error);
    nl_update_header(&cl, encode_len);
    LOG(DEBUG, "VRF Assign for Interface <" << intf->GetName() << "> Tag <" 
        << GetVlanTag() << "> Vrf <" << GetVrfId() << ">");
    len = cl.cl_msg_len;
    return (char *)cl.cl_buf;
}

char *VrfAssignKSyncEntry::AddMsg(int &len) {
    LOG(DEBUG, "VrfAssign: Add");
    return Encode(sandesh_op::ADD, len);
}

char *VrfAssignKSyncEntry::ChangeMsg(int &len){
    return AddMsg(len);
}

char *VrfAssignKSyncEntry::DeleteMsg(int &len) {
    LOG(DEBUG, "VrfAssign: Delete");
    return Encode(sandesh_op::DELETE, len);
}

KSyncEntry *VrfAssignKSyncEntry::UnresolvedReference() {
    IntfKSyncEntry *intf = GetInterface();
    if (!intf->IsResolved()) {
        return intf;
    }
    return NULL;
}
