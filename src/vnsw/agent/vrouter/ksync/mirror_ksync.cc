/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <ksync/ksync_index.h>
#include "vrouter/ksync/mirror_ksync.h"
#include "vrouter/ksync/nexthop_ksync.h"
#include "vrouter/ksync/ksync_init.h"
#include <ksync/ksync_sock.h>
#include "vr_mirror.h"

MirrorKSyncEntry::MirrorKSyncEntry(MirrorKSyncObject *obj, 
                                   const MirrorKSyncEntry *entry,
                                   uint32_t index) : 
    KSyncNetlinkDBEntry(index), ksync_obj_(obj), vrf_id_(entry->vrf_id_), 
    sip_(entry->sip_), sport_(entry->sport_), dip_(entry->dip_), 
    dport_(entry->dport_), analyzer_name_(entry->analyzer_name_),
    mirror_flag_(entry->mirror_flag_), vni_(entry->vni_),
    nic_assisted_mirroring_(entry->nic_assisted_mirroring_),
    nic_assisted_mirroring_vlan_(entry->nic_assisted_mirroring_vlan_),
    mirror_index_(entry->mirror_index_) {
}

MirrorKSyncEntry::MirrorKSyncEntry(MirrorKSyncObject *obj, 
                                   const uint32_t vrf_id, IpAddress dip,
                                   uint16_t dport) :
    KSyncNetlinkDBEntry(kInvalidIndex), ksync_obj_(obj), vrf_id_(vrf_id), 
    dip_(dip), dport_(dport) {
}
MirrorKSyncEntry::MirrorKSyncEntry(MirrorKSyncObject *obj, 
                                   const uint32_t mirror_index) :
    KSyncNetlinkDBEntry(kInvalidIndex), mirror_index_(mirror_index) {
}

MirrorKSyncEntry::MirrorKSyncEntry(MirrorKSyncObject *obj,
                                   const MirrorEntry *mirror_entry) :
    KSyncNetlinkDBEntry(kInvalidIndex), ksync_obj_(obj), 
    vrf_id_(mirror_entry->vrf_id()), sip_(*mirror_entry->GetSip()), 
    sport_(mirror_entry->GetSPort()), dip_(*mirror_entry->GetDip()), 
    dport_(mirror_entry->GetDPort()), nh_(NULL),
    analyzer_name_(mirror_entry->GetAnalyzerName()),
    mirror_flag_(mirror_entry->GetMirrorFlag()), vni_(mirror_entry->GetVni()),
    nic_assisted_mirroring_(mirror_entry->nic_assisted_mirroring()),
    nic_assisted_mirroring_vlan_(mirror_entry->nic_assisted_mirroring_vlan()),
    mirror_index_(mirror_entry->mirror_index()) {
}

MirrorKSyncEntry::MirrorKSyncEntry(MirrorKSyncObject *obj,
                                   std::string &analyzer_name) :
    ksync_obj_(obj), analyzer_name_(analyzer_name),
    mirror_index_(MirrorTable::kInvalidIndex) {
}

MirrorKSyncEntry::~MirrorKSyncEntry() {
}

KSyncDBObject *MirrorKSyncEntry::GetObject() const {
    return ksync_obj_;
}

bool MirrorKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const MirrorKSyncEntry &entry = static_cast<const MirrorKSyncEntry &>(rhs);
    return (mirror_index_ < entry.mirror_index_);
}

std::string MirrorKSyncEntry::ToString() const {
    std::stringstream s;

    s << "Mirror Entry : " << dip_.to_string() << ":" << dport_;
    const VrfEntry* vrf =
        ksync_obj_->ksync()->agent()->vrf_table()->FindVrfFromId(vrf_id_);
    if (vrf) {
        s << " Vrf : " << vrf->GetName();
    }
    return s.str();
}
void MirrorKSyncEntry::UpdateRestoreEntry(const MirrorEntry *mirror) {
        vrf_id_ = mirror->vrf_id();
        sip_ = *mirror->GetSip();
        dip_ = *mirror->GetDip();
        sport_ = mirror->GetSPort();
        dport_ = mirror->GetDPort();
        analyzer_name_ = mirror->GetAnalyzerName();
        mirror_flag_ = mirror->GetMirrorFlag();
        vni_ = mirror->GetVni();
        nic_assisted_mirroring_ = mirror->nic_assisted_mirroring();
        nic_assisted_mirroring_vlan_ = mirror->nic_assisted_mirroring_vlan();
}


bool MirrorKSyncEntry::Sync(DBEntry *e) {
    bool ret = false;
    const MirrorEntry *mirror = static_cast<MirrorEntry *>(e);

    if (analyzer_name_.empty()) {
        // this is the first request after ksync restore for this entry,
        // so populate all fields from dbentry
        UpdateRestoreEntry(mirror);
        return true;
    }

    // ignore nh refernce if it is nic assisted.
    // and return early.
    if (nic_assisted_mirroring_ != mirror->nic_assisted_mirroring()) {
        nic_assisted_mirroring_ = mirror->nic_assisted_mirroring();
        ret = true;
    }

    if (mirror->nic_assisted_mirroring()) {
        nh_ = NULL;
        if (nic_assisted_mirroring_vlan_ !=
            mirror->nic_assisted_mirroring_vlan()) {
            nic_assisted_mirroring_vlan_ =
                mirror->nic_assisted_mirroring_vlan();
            ret = true;
        }
        return ret;
    } else {
        nic_assisted_mirroring_vlan_ = mirror->nic_assisted_mirroring_vlan();
    }

    if (vni_ != mirror->GetVni()) {
        vni_ = mirror->GetVni();
        ret =true;
    }

    if (mirror_flag_ != mirror->GetMirrorFlag()) {
        mirror_flag_ = mirror->GetMirrorFlag();
        ret = true;
    }

    NHKSyncObject *nh_object = ksync_obj_->ksync()->nh_ksync_obj();
    if (mirror->GetNH() == NULL) {
        LOG(DEBUG, "nexthop in Mirror entry is null");
        assert(0);
    }
    NHKSyncEntry nh_entry(nh_object, mirror->GetNH());
    NHKSyncEntry *old_nh = nh();

    nh_ = nh_object->GetReference(&nh_entry);
    if (old_nh != nh()) {
        ret = true;
    }

    // this should never be the case, should be removed???
    if (mirror_index_ != mirror->mirror_index()) {
        mirror_index_ = mirror->mirror_index();
        ret = true;
    }

    return ret;
}

int MirrorKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_mirror_req encoder;
    int encode_len;
    encoder.set_mirr_index(mirror_index_);
    encoder.set_h_op(op);
    encoder.set_mirr_rid(0);
    if (!nic_assisted_mirroring_) {
        NHKSyncEntry *nh_entry = nh();
        encoder.set_mirr_nhid(nh_entry->nh_id());
        encoder.set_mirr_vni(vni_);
        if (mirror_flag_ == MirrorEntryData::DynamicNH_With_JuniperHdr) {
            encoder.set_mirr_flags(VR_MIRROR_FLAG_DYNAMIC);
        }
    } else {
        encoder.set_mirr_vlan(nic_assisted_mirroring_vlan_);
        encoder.set_mirr_flags(VR_MIRROR_FLAG_HW_ASSISTED);
    }
    int error = 0;
    encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    assert(error == 0);
    assert(encode_len <= buf_len);
    LOG(DEBUG, "Mirror index " << GetIndex());
    return encode_len;
}

int MirrorKSyncEntry::AddMsg(char *buf, int buf_len) {
    LOG(DEBUG, "MirrorEntry: Add");
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int MirrorKSyncEntry::ChangeMsg(char *buf, int buf_len){
    return AddMsg(buf, buf_len);
}

int MirrorKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    LOG(DEBUG, "MirrorEntry: Delete");
    return Encode(sandesh_op::DEL, buf, buf_len);
}

KSyncEntry *MirrorKSyncEntry::UnresolvedReference() {
    if (nic_assisted_mirroring_) {
        return NULL;
    }

    NHKSyncEntry *nh_entry = nh();
    if (!nh_entry->IsResolved()) {
        return nh_entry;
    }
    return NULL;
}

MirrorKSyncObject::MirrorKSyncObject(KSync *ksync) : 
    KSyncDBObject("KSync Mirror", kMirrorIndexCount), ksync_(ksync) {
}

MirrorKSyncObject::~MirrorKSyncObject() {
}

void MirrorKSyncObject::RegisterDBClients() {
    RegisterDb(ksync_->agent()->mirror_table());
}

KSyncEntry *MirrorKSyncObject::Alloc(const KSyncEntry *entry, uint32_t index) {
    const MirrorKSyncEntry *mirror_entry = 
        static_cast<const MirrorKSyncEntry *>(entry);
    MirrorKSyncEntry *ksync = new MirrorKSyncEntry(this, mirror_entry, index);
    return static_cast<KSyncEntry *>(ksync);
}

KSyncEntry *MirrorKSyncObject::DBToKSyncEntry(const DBEntry *e) {
    const MirrorEntry *mirror_entry = static_cast<const MirrorEntry *>(e);
    MirrorKSyncEntry *ksync = new MirrorKSyncEntry(this, mirror_entry);
    return static_cast<KSyncEntry *>(ksync);
}

uint32_t MirrorKSyncObject::GetIdx(std::string analyzer_name) {
    MirrorKSyncEntry key(this, analyzer_name);
    KSyncEntry *entry = Find(&key);
    if (entry) {
        return static_cast<MirrorKSyncEntry *>(entry)->mirror_index();
    }
    return MirrorTable::kInvalidIndex;
}

void vr_mirror_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->MirrorMsgHandler(this);
}

KSyncEntry *MirrorKSyncObject::CreateStale(const KSyncEntry *key) {
    return CreateStaleInternal(key);;
}
void MirrorKSyncEntry::StaleTimerExpired() {
    Delete();
    SetState(KSyncEntry::TEMP);
}

//////////////////////////////////////////////////////////////////////////////
// ksync entry restore routines
//////////////////////////////////////////////////////////////////////////////

MirrorKSyncRestoreData::MirrorKSyncRestoreData(KSyncDBObject *obj,
                                            KMirrorInfoPtr mirrorInfo):
    KSyncRestoreData(obj), mirrorInfo_(mirrorInfo) {
}
MirrorKSyncRestoreData::~MirrorKSyncRestoreData() {
}
void MirrorKSyncObject::RestoreVrouterEntriesReq(void) {
    InitStaleEntryCleanup(*(ksync()->agent()->event_manager())->io_service(),
                            KSyncRestoreManager::StaleEntryCleanupTimer, 
                            KSyncRestoreManager::StaleEntryYeildTimer,
                            KSyncRestoreManager::StaleEntryDeletePerIteration);
    KMirrorReq *req = new KMirrorReq();
    req->set_mirror_id(-1);
    req->set_context(LLGR_KSYNC_RESTORE_CONTEXT);
    Sandesh::set_response_callback(
        boost::bind(&MirrorKSyncObject::ReadVrouterEntriesResp, this, _1));
    req->HandleRequest();
    req->Release();
}
void MirrorKSyncObject::ReadVrouterEntriesResp(Sandesh *sandesh) {
    if (sandesh->context() !=  LLGR_KSYNC_RESTORE_CONTEXT) {
        // ignore callback, if context is not relevant
        return;
    }
    KMirrorResp *response = dynamic_cast<KMirrorResp *>(sandesh);
    if (response != NULL) {
        for(std::vector<KMirrorInfo>::const_iterator it
                = response->get_mirror_list().begin();
                it != response->get_mirror_list().end();it++) {
            MirrorKSyncRestoreData::KMirrorInfoPtr 
                                    mirrorInfo(new KMirrorInfo(*it));
            KSyncRestoreData::Ptr restore_data(
                        new MirrorKSyncRestoreData(this, mirrorInfo));
            ksync()->ksync_restore_manager()->EnqueueRestoreData(restore_data);
        }
        //TODO: check errorresp
        if (!response->get_more()) {
            KSyncRestoreData::Ptr end_data(
                    new KSyncRestoreEndData (this));
            ksync()->ksync_restore_manager()->EnqueueRestoreData(end_data);
        }
    }


}
void MirrorKSyncObject::ProcessVrouterEntries(
                        KSyncRestoreData::Ptr restore_data) {
    if(dynamic_cast<KSyncRestoreEndData *>(restore_data.get())) {
        ksync()->ksync_restore_manager()->UpdateKSyncRestoreStatus(
                    KSyncRestoreManager::KSYNC_TYPE_MIRROR);
        return;
    }
    MirrorKSyncRestoreData *dataPtr = 
        dynamic_cast<MirrorKSyncRestoreData *>(restore_data.get());
    MirrorKSyncEntry ksync_entry_key(this,
                        dataPtr->GetMirrorEntry()->get_mirr_index());
    MirrorKSyncEntry *ksync_entry_ptr =
        static_cast<MirrorKSyncEntry *>(Find(&ksync_entry_key));
    if (ksync_entry_ptr == NULL) {
        ksync_entry_ptr = 
        static_cast<MirrorKSyncEntry *>(CreateStale(&ksync_entry_key));
    }
    
}
