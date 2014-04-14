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
#include "oper/nexthop.h"
#include "oper/vxlan.h"
#include "oper/mirror_table.h"

#include "ksync/interface_ksync.h"
#include "ksync/nexthop_ksync.h"
#include "ksync/vxlan_ksync.h"

#include "ksync_init.h"

VxLanIdKSyncEntry::VxLanIdKSyncEntry(VxLanKSyncObject *obj, 
                                     const VxLanIdKSyncEntry *entry, 
                                     uint32_t index) :
    KSyncNetlinkDBEntry(index), ksync_obj_(obj), label_(entry->label_), 
    nh_(NULL) { 
}

VxLanIdKSyncEntry::VxLanIdKSyncEntry(VxLanKSyncObject *obj,
                                     const VxLanId *vxlan_id) : 
    KSyncNetlinkDBEntry(kInvalidIndex), ksync_obj_(obj), 
    label_(vxlan_id->vxlan_id()), nh_(NULL) {
}

VxLanIdKSyncEntry::~VxLanIdKSyncEntry() {
}

KSyncDBObject *VxLanIdKSyncEntry::GetObject() {
    return ksync_obj_;
}

bool VxLanIdKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const VxLanIdKSyncEntry &entry = 
        static_cast<const VxLanIdKSyncEntry &>(rhs);

    return label_ < entry.label_;
}

std::string VxLanIdKSyncEntry::ToString() const {
    std::stringstream s;
    NHKSyncEntry *nexthop = nh();

    if (nexthop) {
        s << "VXLAN Label: " << label_ << " Index : "
            << GetIndex() << nexthop->ToString();
    } else {
        s << "VXLAN Label: " << label_ << " Index : "
            << GetIndex() << " NextHop : <null>";
    }
    return s.str();
}

bool VxLanIdKSyncEntry::Sync(DBEntry *e) {
    bool ret = false;
    const VxLanId *vxlan_id = static_cast<VxLanId *>(e);

    NHKSyncObject *nh_object = ksync_obj_->ksync()->nh_ksync_obj();
    if (vxlan_id->nexthop() == NULL) {
        LOG(DEBUG, "nexthop in network-id label is null");
        assert(0);
    }
    NHKSyncEntry nexthop(nh_object, vxlan_id->nexthop());
    NHKSyncEntry *old_nh = nh();

    nh_ = nh_object->GetReference(&nexthop);
    if (old_nh != nh()) {
        ret = true;
    }

    return ret;
};

int VxLanIdKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_vxlan_req encoder;
    int encode_len, error;
    NHKSyncEntry *nexthop = nh();

    encoder.set_h_op(op);
    encoder.set_vxlanr_rid(0);
    encoder.set_vxlanr_vnid(label_);
    encoder.set_vxlanr_nhid(nexthop->GetIndex());
    encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    return encode_len;
}

void VxLanIdKSyncEntry::FillObjectLog(sandesh_op::type op, 
                                      KSyncVxLanInfo &info) const {
    info.set_label(label_);
    info.set_nh(nh()->GetIndex());

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
    NHKSyncEntry *nexthop = nh();
    if (!nexthop->IsResolved()) {
        return nexthop;
    }
    return NULL;
}

VxLanKSyncObject::VxLanKSyncObject(KSync *ksync) 
    : KSyncDBObject(kVxLanIndexCount), ksync_(ksync) {
}

VxLanKSyncObject::~VxLanKSyncObject() {
}

void VxLanKSyncObject::RegisterDBClients() {
    RegisterDb(ksync_->agent()->GetVxLanTable());
}

KSyncEntry *VxLanKSyncObject::Alloc(const KSyncEntry *entry, uint32_t index) {
    const VxLanIdKSyncEntry *vxlan = 
        static_cast<const VxLanIdKSyncEntry *>(entry);
    VxLanIdKSyncEntry *ksync = new VxLanIdKSyncEntry(this, vxlan, index);
    return static_cast<KSyncEntry *>(ksync);
}

KSyncEntry *VxLanKSyncObject::DBToKSyncEntry(const DBEntry *e) {
    const VxLanId *vxlan = static_cast<const VxLanId *>(e);
    VxLanIdKSyncEntry *key = new VxLanIdKSyncEntry(this, vxlan);
    return static_cast<KSyncEntry *>(key);
}

void vr_vxlan_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->VxLanMsgHandler(this);
}

