/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <base/logging.h>
#include <db/db.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_sock.h>

#include "ksync/interface_ksync.h"
#include "ksync/nexthop_ksync.h"
#include "ksync/mirror_ksync.h"
#include "nl_util.h"
#include "ksync_init.h"

void vr_mirror_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->MirrorMsgHandler(this);
}

MirrorKSyncObject *MirrorKSyncObject::singleton_;

KSyncDBObject *MirrorKSyncEntry::GetObject() {
    return MirrorKSyncObject::GetKSyncObject();
}

MirrorKSyncEntry::MirrorKSyncEntry(const MirrorEntry *mirror_entry) :
    KSyncNetlinkDBEntry(kInvalidIndex), vrf_id_(mirror_entry->GetVrfId()),
    sip_(*mirror_entry->GetSip()), sport_(mirror_entry->GetSPort()),
    dip_(*mirror_entry->GetDip()), dport_(mirror_entry->GetDPort()), nh_(NULL),
    analyzer_name_(mirror_entry->GetAnalyzerName()) {
}

bool MirrorKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const MirrorKSyncEntry &entry = static_cast<const MirrorKSyncEntry &>(rhs);
    return (analyzer_name_ < entry.analyzer_name_);
}

std::string MirrorKSyncEntry::ToString() const {
    std::stringstream s;
    NHKSyncEntry *nh = GetNH();

    if (nh) {
        s << "Mirror Index : " << GetIndex() << " NH : " << nh->GetIndex()
            << " Vrf : " << vrf_id_;
    } else {
        s << "Mirror Index : " << GetIndex() << " NH : <null>" 
            << " Vrf : " << vrf_id_;
    }
    return s.str();
}

bool MirrorKSyncEntry::Sync(DBEntry *e) {
    bool ret = false;
    const MirrorEntry *mirror = static_cast<MirrorEntry *>(e);

    NHKSyncObject *nh_object = NHKSyncObject::GetKSyncObject();
    if (mirror->GetNH() == NULL) {
        LOG(DEBUG, "nexthop in Mirror entry is null");
        assert(0);
    }
    NHKSyncEntry nh(mirror->GetNH());
    NHKSyncEntry *old_nh = GetNH();

    nh_ = nh_object->GetReference(&nh);
    if (old_nh != GetNH()) {
        ret = true;
    }

    return ret;
};

char *MirrorKSyncEntry::Encode(sandesh_op::type op, int &len) {
    struct nl_client cl;
    vr_mirror_req encoder;
    int encode_len, error, ret;
    uint8_t *buf;
    uint32_t buf_len;
    NHKSyncEntry *nh = GetNH();

    nl_init_generic_client_req(&cl, KSyncSock::GetNetlinkFamilyId());

    if ((ret = nl_build_header(&cl, &buf, &buf_len)) < 0) {
        LOG(DEBUG, "Error creating Mirror message. Error : " << ret);
        return NULL;
    }

    encoder.set_h_op(op);
    encoder.set_mirr_index(GetIndex());
    encoder.set_mirr_rid(0);
    encoder.set_mirr_nhid(nh->GetIndex());
    encode_len = encoder.WriteBinary(buf, buf_len, &error);
    nl_update_header(&cl, encode_len);
    LOG(DEBUG, "Mirror index " << GetIndex() << " nhid " << nh->GetIndex());
    len = cl.cl_msg_len;
    return (char *)cl.cl_buf;
}

char *MirrorKSyncEntry::AddMsg(int &len) {
    LOG(DEBUG, "MirrorEntry: Add");
    return Encode(sandesh_op::ADD, len);
}

char *MirrorKSyncEntry::ChangeMsg(int &len){
    return AddMsg(len);
}

char *MirrorKSyncEntry::DeleteMsg(int &len) {
    LOG(DEBUG, "MirrorEntry: Delete");
    return Encode(sandesh_op::DELETE, len);
}

KSyncEntry *MirrorKSyncEntry::UnresolvedReference() {
    NHKSyncEntry *nh = GetNH();
    if (!nh->IsResolved()) {
        return nh;
    }
    return NULL;
}
