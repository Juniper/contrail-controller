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
#include "oper/nexthop.h"
#include "oper/vxlan.h"
#include "oper/mirror_table.h"

#include "ksync/interface_ksync.h"
#include "ksync/nexthop_ksync.h"
#include "ksync/vxlan_ksync.h"

#include "ksync_init.h"

void vr_vxlan_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->VxLanMsgHandler(this);
}

VxLanKSyncObject *VxLanKSyncObject::singleton_;

KSyncDBObject *VxLanIdKSyncEntry::GetObject() {
    return VxLanKSyncObject::GetKSyncObject();
}

VxLanIdKSyncEntry::VxLanIdKSyncEntry(const VxLanId *vxlan_id) :
    KSyncNetlinkDBEntry(kInvalidIndex), label_(vxlan_id->GetLabel()), 
    nh_(NULL) {
}

bool VxLanIdKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const VxLanIdKSyncEntry &entry = 
        static_cast<const VxLanIdKSyncEntry &>(rhs);

    return label_ < entry.label_;
}

std::string VxLanIdKSyncEntry::ToString() const {
    std::stringstream s;
    NHKSyncEntry *nh = GetNH();

    if (nh) {
        s << "VXLAN Label: " << label_ << " Index : " 
            << GetIndex() << " NH : " 
        << nh->GetIndex();
    } else {
        s << "VXLAN Label: " << label_ << " Index : " 
            << GetIndex() << " NH : <null>";
    }
    return s.str();
}

bool VxLanIdKSyncEntry::Sync(DBEntry *e) {
    bool ret = false;
    const VxLanId *vxlan_id = static_cast<VxLanId *>(e);

    NHKSyncObject *nh_object = NHKSyncObject::GetKSyncObject();
    if (vxlan_id->GetNextHop() == NULL) {
        LOG(DEBUG, "nexthop in network-id label is null");
        assert(0);
    }
    NHKSyncEntry nh(vxlan_id->GetNextHop());
    NHKSyncEntry *old_nh = GetNH();

    nh_ = nh_object->GetReference(&nh);
    if (old_nh != GetNH()) {
        ret = true;
    }

    return ret;
};

int VxLanIdKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_vxlan_req encoder;
    int encode_len, error;
    NHKSyncEntry *nh = GetNH();

    encoder.set_h_op(op);
    encoder.set_vxlanr_rid(0);
    encoder.set_vxlanr_vnid(label_);
    encoder.set_vxlanr_nhid(nh->GetIndex());
    encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    return encode_len;
}

void VxLanIdKSyncEntry::FillObjectLog(sandesh_op::type op, 
                                        KSyncVxLanInfo &info) {
    info.set_label(label_);
    info.set_nh(GetNH()->GetIndex());

    if (op == sandesh_op::ADD) {
        info.set_operation("ADD/CHANGE");
    } else {
        info.set_operation("DELETE");
    }
}

int VxLanIdKSyncEntry::AddMsg(char *buf, int buf_len) {
    KSyncVxLanInfo info;
    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(VxLan, info);

    return Encode(sandesh_op::ADD, buf, buf_len);
}

int VxLanIdKSyncEntry::ChangeMsg(char *buf, int buf_len) {
    KSyncVxLanInfo info;
    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(VxLan, info);
 
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int VxLanIdKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    KSyncVxLanInfo info;
    FillObjectLog(sandesh_op::DELETE, info);
    KSYNC_TRACE(VxLan, info);
 
    return Encode(sandesh_op::DELETE, buf, buf_len);
}

KSyncEntry *VxLanIdKSyncEntry::UnresolvedReference() {
    NHKSyncEntry *nh = GetNH();
    if (!nh->IsResolved()) {
        return nh;
    }
    return NULL;
}
