/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/logging.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>
#include <vrouter/ksync/ksync_init.h>
#include <init/agent_param.h>
#include "vrouter/ksync/qos_queue_ksync.h"
#include "vrouter/ksync/forwarding_class_ksync.h"

ForwardingClassKSyncEntry::ForwardingClassKSyncEntry(
        ForwardingClassKSyncObject *obj, const ForwardingClassKSyncEntry *fc):
    KSyncNetlinkDBEntry(), ksync_obj_(obj), id_(fc->id()),
    qos_queue_ksync_(NULL) {
}

ForwardingClassKSyncEntry::ForwardingClassKSyncEntry(
        ForwardingClassKSyncObject *obj, const ForwardingClass *fc):
    KSyncNetlinkDBEntry(), ksync_obj_(obj), id_(fc->id()),
    qos_queue_ksync_(NULL) {
}

ForwardingClassKSyncEntry::ForwardingClassKSyncEntry(
        ForwardingClassKSyncObject *obj, uint32_t id):
    KSyncNetlinkDBEntry(), ksync_obj_(obj), id_(id),
    qos_queue_ksync_(NULL) {
}

ForwardingClassKSyncEntry::~ForwardingClassKSyncEntry() {
}

KSyncDBObject *ForwardingClassKSyncEntry::GetObject() const {
    return ksync_obj_;
}

bool ForwardingClassKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const ForwardingClassKSyncEntry &entry = static_cast<const ForwardingClassKSyncEntry &>(rhs);
    return id_ < entry.id_;
}

std::string ForwardingClassKSyncEntry::ToString() const {
    std::stringstream s;
    s << "Forwarding class id " << id_;
    return s.str();
}

bool ForwardingClassKSyncEntry::Sync(DBEntry *e) {
    ForwardingClass *fc = static_cast<ForwardingClass *>(e);

    bool ret = false;
    if (dscp_ != fc->dscp()) {
        dscp_ = fc->dscp();
        ret = true;
    }

    if (vlan_priority_ != fc->vlan_priority()) {
        vlan_priority_ = fc->vlan_priority();
        ret = true;
    }

    if (mpls_exp_ != fc->mpls_exp()) {
        mpls_exp_ = fc->mpls_exp();
        ret = true;
    }

    QosQueueKSyncObject *qos_queue_object =
        (static_cast<QosQueueKSyncObject *>(ksync_obj_))->
        ksync()->qos_queue_ksync_obj();

    KSyncEntry *qos_queue_ksync_ptr = NULL;
    if (fc->qos_queue_ref()) {
        QosQueueKSyncEntry qos_queue_ksync(qos_queue_object,
                                           fc->qos_queue_ref());
        qos_queue_ksync_ptr = qos_queue_object->GetReference(&qos_queue_ksync);
    }

    if (qos_queue_ksync_ptr != qos_queue_ksync_) {
        qos_queue_ksync_ = qos_queue_ksync_ptr;
        ret = true;
    }

    uint16_t nic_queue = Agent::kInvalidQueueId;
    if (fc->qos_queue_ref()) {
        nic_queue = fc->qos_queue_ref()->nic_queue_id();
    } else {
        nic_queue = (static_cast<QosQueueKSyncObject *>(ksync_obj_))->ksync()->
                         agent()->params()->default_nic_queue();
    }

    if (nic_queue_id_ != nic_queue) {
        nic_queue_id_ = nic_queue;
        ret = true;
    }

    return ret;
}

int ForwardingClassKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_fc_map_req encoder;
    encoder.set_h_op(op);
    encoder.set_fmr_rid(0);

    std::vector<int16_t> id_list;
    id_list.push_back((int16_t)id_);
    encoder.set_fmr_id(id_list);

    std::vector<int8_t> dscp_list;
    dscp_list.push_back(dscp_);
    encoder.set_fmr_dscp(dscp_list);

    std::vector<int8_t> vlan_priority_list;
    vlan_priority_list.push_back(vlan_priority_);
    encoder.set_fmr_dotonep(vlan_priority_list);

    std::vector<int8_t> mpls_exp_list;
    mpls_exp_list.push_back(mpls_exp_);
    encoder.set_fmr_mpls_qos(mpls_exp_list);

    std::vector<int8_t> qos_queue_list;
    qos_queue_list.push_back(nic_queue_id_);
    encoder.set_fmr_queue_id(qos_queue_list);

    int error = 0;
    int encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    assert(error == 0);
    assert(encode_len <= buf_len);
    return encode_len;
}

int ForwardingClassKSyncEntry::AddMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int ForwardingClassKSyncEntry::ChangeMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int ForwardingClassKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::DEL, buf, buf_len);
}

KSyncEntry *ForwardingClassKSyncEntry::UnresolvedReference() {
    if (qos_queue_ksync() && qos_queue_ksync()->IsResolved() == false) {
        return qos_queue_ksync();
    }

    return NULL;
}

ForwardingClassKSyncObject::ForwardingClassKSyncObject(KSync *ksync):
    KSyncDBObject("KSync Forwarding class object"), ksync_(ksync) {
}

ForwardingClassKSyncObject::~ForwardingClassKSyncObject() {

}

void ForwardingClassKSyncObject::RegisterDBClients() {
    RegisterDb(ksync_->agent()->forwarding_class_table());
}

KSyncEntry*
ForwardingClassKSyncObject::Alloc(const KSyncEntry *e, uint32_t index) {
    const ForwardingClassKSyncEntry *entry =
        static_cast<const ForwardingClassKSyncEntry *>(e);
    ForwardingClassKSyncEntry *new_entry =
        new ForwardingClassKSyncEntry(this, entry);
    return static_cast<KSyncEntry *>(new_entry);
}

KSyncEntry *ForwardingClassKSyncObject::DBToKSyncEntry(const DBEntry *e) {
    const ForwardingClass *fc = static_cast<const ForwardingClass *>(e);
    ForwardingClassKSyncEntry *entry = new ForwardingClassKSyncEntry(this, fc);
    return static_cast<KSyncEntry *>(entry);
}

void vr_fc_map_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->ForwardingClassMsgHandler(this);
}

KSyncDBObject::DBFilterResp
ForwardingClassKSyncObject::DBEntryFilter(const DBEntry *entry,
                                          const KSyncDBEntry *ksync) {
    const ForwardingClass *fc = static_cast<const ForwardingClass *>(entry);
    if (ksync) {
        const ForwardingClassKSyncEntry *ksync_entry =
            static_cast<const ForwardingClassKSyncEntry *>(ksync);
        if (fc->id() != ksync_entry->id()) {
            return DBFilterDelAdd;
        }
    }
    return DBFilterAccept;
}

//////////////////////////////////////////////////////////////////////////////
// ksync entry restore routines
//////////////////////////////////////////////////////////////////////////////

KSyncEntry *ForwardingClassKSyncObject::CreateStale(const KSyncEntry *key) {
    return CreateStaleInternal(key);;
}
void ForwardingClassKSyncEntry::StaleTimerExpired() {
    Delete();
    SetState(KSyncEntry::TEMP);
}


ForwardingClassKSyncRestoreData::ForwardingClassKSyncRestoreData(
                                    KSyncDBObject *obj,
                                    KForwardingClassPtr forwardingClassInfo):
    KSyncRestoreData(obj), fwdClassInfo_(forwardingClassInfo) {
}

ForwardingClassKSyncRestoreData::~ForwardingClassKSyncRestoreData() {
}

void ForwardingClassKSyncObject::RestoreVrouterEntriesReq(void) {
    InitStaleEntryCleanup(*(ksync()->agent()->event_manager())->io_service(),
                            KSyncRestoreManager::StaleEntryCleanupTimer, 
                            KSyncRestoreManager::StaleEntryYeildTimer,
                            KSyncRestoreManager::StaleEntryDeletePerIteration);
    KForwardingClassReq *req = new KForwardingClassReq();
    req->set_index(-1);
    req->set_context(LLGR_KSYNC_RESTORE_CONTEXT);
    Sandesh::set_response_callback(
        boost::bind(&ForwardingClassKSyncObject::ReadVrouterEntriesResp,
                    this, _1));
    req->HandleRequest();
    req->Release();
}
void ForwardingClassKSyncObject::ReadVrouterEntriesResp(Sandesh *sandesh) {
    if (sandesh->context() !=  LLGR_KSYNC_RESTORE_CONTEXT) {
        // ignore callback, if context is not relevant
        return;
    }
    KForwardingClassResp *response = 
                        dynamic_cast<KForwardingClassResp *>(sandesh);
    if (response != NULL) {
        for(std::vector<KForwardingClass>::const_iterator it
                = response->get_forwarding_class_list().begin();
                it != response->get_forwarding_class_list().end();it++) {
            ForwardingClassKSyncRestoreData::KForwardingClassPtr 
                        forwardingClassInfo(new KForwardingClass(*it));
            KSyncRestoreData::Ptr restore_data(
                        new ForwardingClassKSyncRestoreData(
                                            this, forwardingClassInfo));
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
void ForwardingClassKSyncObject::ProcessVrouterEntries(
                        KSyncRestoreData::Ptr restore_data) {
    if(dynamic_cast<KSyncRestoreEndData *>(restore_data.get())) {
        ksync()->ksync_restore_manager()->UpdateKSyncRestoreStatus(
                    KSyncRestoreManager::KSYNC_TYPE_FORWARDING_CLASS);
        return;
    }
    ForwardingClassKSyncRestoreData *dataPtr = 
        dynamic_cast<ForwardingClassKSyncRestoreData *>(restore_data.get());
    ForwardingClassKSyncEntry ksync_entry_key(this,
                        dataPtr->GetForwardingClassEntry()->get_id());
    ForwardingClassKSyncEntry *ksync_entry_ptr =
        static_cast<ForwardingClassKSyncEntry *>(Find(&ksync_entry_key));
    if (ksync_entry_ptr == NULL) {
        ksync_entry_ptr = 
        static_cast<ForwardingClassKSyncEntry *>(CreateStale(&ksync_entry_key));
    }
    
}
