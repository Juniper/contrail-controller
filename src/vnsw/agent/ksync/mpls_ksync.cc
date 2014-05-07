/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/logging.h>
#include <oper/nexthop.h>
#include <oper/mirror_table.h>
#include <ksync/ksync_index.h>
#include <ksync/interface_ksync.h>
#include <ksync/nexthop_ksync.h>
#include <ksync/mpls_ksync.h>
#include <ksync/ksync_init.h>
#include <ksync/ksync_sock.h>

MplsKSyncEntry::MplsKSyncEntry(MplsKSyncObject* obj, const MplsKSyncEntry *me,
                               uint32_t index) :
    KSyncNetlinkDBEntry(index), ksync_obj_(obj), label_(me->label_), 
    nh_(NULL)  { 
}

MplsKSyncEntry::MplsKSyncEntry(MplsKSyncObject* obj, const MplsLabel *mpls) :
    KSyncNetlinkDBEntry(kInvalidIndex), ksync_obj_(obj), 
    label_(mpls->label()), nh_(NULL) {
}

MplsKSyncEntry::~MplsKSyncEntry() {
}

KSyncDBObject *MplsKSyncEntry::GetObject() {
    return ksync_obj_;
}

bool MplsKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const MplsKSyncEntry &entry = static_cast<const MplsKSyncEntry &>(rhs);
    return label_ < entry.label_;
}

std::string MplsKSyncEntry::ToString() const {
    std::stringstream s;
    NHKSyncEntry *next_hop = nh();

    s << "Mpls : " << label_ << " Index : " << GetIndex();
    if (next_hop) {
        s << next_hop->ToString();
    } else {
        s << "NextHop :  NULL";
    }
    return s.str();
}

bool MplsKSyncEntry::Sync(DBEntry *e) {
    bool ret = false;
    const MplsLabel *mpls = static_cast<MplsLabel *>(e);

    NHKSyncObject *nh_object = ksync_obj_->ksync()->nh_ksync_obj();
    if (mpls->nexthop() == NULL) {
        LOG(DEBUG, "nexthop in mpls label is null");
        assert(0);
    }
    NHKSyncEntry next_hop(nh_object, mpls->nexthop());
    NHKSyncEntry *old_nh = nh(); 

    nh_ = nh_object->GetReference(&next_hop);
    if (old_nh != nh()) {
        ret = true;
    }

    return ret;
};

int MplsKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_mpls_req encoder;
    int encode_len, error;
    NHKSyncEntry *next_hop = nh();

    encoder.set_h_op(op);
    encoder.set_mr_label(label_);
    encoder.set_mr_rid(0);
    encoder.set_mr_nhid(next_hop->GetIndex());
    encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    return encode_len;
}

void MplsKSyncEntry::FillObjectLog(sandesh_op::type op, 
                                   KSyncMplsInfo &info) const {
    info.set_label(label_);
    info.set_nh(nh()->GetIndex());

    if (op == sandesh_op::ADD) {
        info.set_operation("ADD/CHANGE");
    } else {
        info.set_operation("DELETE");
    }
}

int MplsKSyncEntry::AddMsg(char *buf, int buf_len) {
    KSyncMplsInfo info;
    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(Mpls, info);

    return Encode(sandesh_op::ADD, buf, buf_len);
}

int MplsKSyncEntry::ChangeMsg(char *buf, int buf_len){
    KSyncMplsInfo info;
    FillObjectLog(sandesh_op::ADD, info);
    KSYNC_TRACE(Mpls, info);
 
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int MplsKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    KSyncMplsInfo info;
    FillObjectLog(sandesh_op::DELETE, info);
    KSYNC_TRACE(Mpls, info);
 
    return Encode(sandesh_op::DELETE, buf, buf_len);
}

KSyncEntry *MplsKSyncEntry::UnresolvedReference() {
    NHKSyncEntry *next_hop = nh(); 
    if (!next_hop->IsResolved()) {
        return next_hop;
    }
    return NULL;
}

MplsKSyncObject::MplsKSyncObject(KSync *ksync) : 
    KSyncDBObject(kMplsIndexCount), ksync_(ksync) {
}

MplsKSyncObject::~MplsKSyncObject() {
}

void MplsKSyncObject::RegisterDBClients() {
    RegisterDb(ksync_->agent()->GetMplsTable());
}

KSyncEntry *MplsKSyncObject::Alloc(const KSyncEntry *entry, uint32_t index) {
    const MplsKSyncEntry *mpls = static_cast<const MplsKSyncEntry *>(entry);
    MplsKSyncEntry *ksync = new MplsKSyncEntry(this, mpls, index);
    return static_cast<KSyncEntry *>(ksync);
};

KSyncEntry *MplsKSyncObject::DBToKSyncEntry(const DBEntry *e) {
    const MplsLabel *mpls = static_cast<const MplsLabel *>(e);
    MplsKSyncEntry *key = new MplsKSyncEntry(this, mpls);
    return static_cast<KSyncEntry *>(key);
}

void vr_mpls_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->MplsMsgHandler(this);
}

