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
#include "vrouter/ksync/qos_queue_ksync.h"
#include "vrouter/ksync/forwarding_class_ksync.h"

QosConfigKSyncEntry::QosConfigKSyncEntry(
        QosConfigKSyncObject *obj, const QosConfigKSyncEntry *qc):
    KSyncNetlinkDBEntry(), ksync_obj_(obj), uuid_(qc->uuid()), id_(qc->id()) {
}

QosConfigKSyncEntry::QosConfigKSyncEntry(QosConfigKSyncObject *obj,
                                         const AgentQosConfig *qc):
    KSyncNetlinkDBEntry(), ksync_obj_(obj), uuid_(qc->uuid()), id_(qc->id()) {
}

QosConfigKSyncEntry::QosConfigKSyncEntry(QosConfigKSyncObject *obj,
                                         const uint32_t id):
    KSyncNetlinkDBEntry(), ksync_obj_(obj), id_(id) {
}

QosConfigKSyncEntry::~QosConfigKSyncEntry() {
}

KSyncDBObject *QosConfigKSyncEntry::GetObject() const {
    return ksync_obj_;
}

bool QosConfigKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const QosConfigKSyncEntry &entry = static_cast<const QosConfigKSyncEntry &>(rhs);
    return id_ < entry.id_;
}

std::string QosConfigKSyncEntry::ToString() const {
    std::stringstream s;
    s << "Qos Config class id " << uuid_;
    return s.str();
}

bool QosConfigKSyncEntry::CopyQosMap(KSyncQosFcMap &ksync_map,
                                     const AgentQosConfig::
                                     QosIdForwardingClassMap *map) {

    bool ret = false;
    ForwardingClassKSyncObject *fc_object =
        static_cast<ForwardingClassKSyncObject *>(ksync_obj_)
        ->ksync()->forwarding_class_ksync_obj();

    KSyncQosFcMap new_ksync_map;
    AgentQosConfig::QosIdForwardingClassMap::const_iterator it = map->begin();
    for (; it != map->end(); it++) {
        KSyncEntryPtr ptr(NULL);
        ForwardingClassKSyncEntry fc_key(fc_object, it->second);
        ptr = fc_object->GetReference(&fc_key);
        new_ksync_map.insert(KSyncQosFcPair(it->first, ptr));
    }

    if (ksync_map != new_ksync_map) {
        ksync_map = new_ksync_map;
        ret = true;
    }

    return ret;
}

bool QosConfigKSyncEntry::Sync(DBEntry *e) {
    AgentQosConfig *qc = static_cast<AgentQosConfig *>(e);
    bool ret = false;
    
    if (uuid_.is_nil()) {
        uuid_ = qc->uuid();
    }

    if (CopyQosMap(dscp_map_, &(qc->dscp_map()))) {
        ret = true;
    }

    if (CopyQosMap(vlan_priority_map_, &(qc->vlan_priority_map()))) {
        ret = true;
    }

    if (CopyQosMap(mpls_exp_map_, &(qc->mpls_exp_map()))) {
        ret = true;
    }

    if (default_forwarding_class_ != qc->default_forwarding_class()) {
        default_forwarding_class_ = qc->default_forwarding_class();
        ret = true;
    }

    return ret;
}

int QosConfigKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
    vr_qos_map_req encoder;

    encoder.set_h_op(op);
    encoder.set_qmr_id(id_);
    encoder.set_qmr_rid(0);

    std::vector<int8_t> key;
    std::vector<int8_t> data;
    KSyncQosFcMap::const_iterator it = dscp_map_.begin();
    for (uint32_t index = 0;
         index <= AgentQosConfigTable::kDscpEntries; index++) {
        it = dscp_map_.find(index);
        if (it != dscp_map_.end()) {
            ForwardingClassKSyncEntry *fc =
                static_cast<ForwardingClassKSyncEntry *>(it->second.get());
            key.push_back(index);
            data.push_back(fc->id());
        } else {
            key.push_back(index);
            data.push_back(default_forwarding_class_);
        }
    }

    encoder.set_qmr_dscp(key);
    encoder.set_qmr_dscp_fc_id(data);

    std::vector<int8_t> vlan_key;
    std::vector<int8_t> vlan_data;
    it = vlan_priority_map_.begin();
    for (uint32_t index = 0;
            index <= AgentQosConfigTable::k801pEntries; index++) {
        it = vlan_priority_map_.find(index);
        if (it != vlan_priority_map_.end()) {
            ForwardingClassKSyncEntry *fc =
                static_cast<ForwardingClassKSyncEntry *>(it->second.get());
            vlan_key.push_back(index);
            vlan_data.push_back(fc->id());
        } else {
            vlan_key.push_back(index);
            vlan_data.push_back(default_forwarding_class_);
        }
    }
    encoder.set_qmr_dotonep(vlan_key);
    encoder.set_qmr_dotonep_fc_id(vlan_data);

    std::vector<int8_t> mpls_key;
    std::vector<int8_t> mpls_data;
    it = mpls_exp_map_.begin();
    for (uint32_t index = 0;
         index <= AgentQosConfigTable::kExpEntries; index++) {
        it = mpls_exp_map_.find(index);
        if (it != mpls_exp_map_.end()) {
            ForwardingClassKSyncEntry *fc =
                static_cast<ForwardingClassKSyncEntry *>(it->second.get());
            mpls_key.push_back(index);
            mpls_data.push_back(fc->id());
        } else {
            mpls_key.push_back(index);
            mpls_data.push_back(default_forwarding_class_);
        }
    }
    encoder.set_qmr_mpls_qos(mpls_key);
    encoder.set_qmr_mpls_qos_fc_id(mpls_data);

    int error = 0;
    int encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    assert(error == 0);
    assert(encode_len <= buf_len);
    return encode_len;
}

int QosConfigKSyncEntry::AddMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int QosConfigKSyncEntry::ChangeMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int QosConfigKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::DEL, buf, buf_len);
}

KSyncEntry *QosConfigKSyncEntry::UnresolvedReference() {

    //QOS config has forwarding-class ID
    //And qos-config may have a forwarding-class ID which
    //is not present, in that case unresolved dependency
    //would never resolve and all dependent object like
    //VMI also would not be programmed. vrouter also
    //doesnt cross check for forwarding-class
    //Hence not checking for any forwarding class reference
    return NULL;

    KSyncQosFcMap::const_iterator it = dscp_map_.begin();
    for (; it != dscp_map_.end(); it++) {
        if (it->second.get()->IsResolved() == false) {
            return it->second.get();
        }
    }

    it = vlan_priority_map_.begin();
    for (; it != vlan_priority_map_.end(); it++) {
        if (it->second.get()->IsResolved() == false) {
            return it->second.get();
        }
    }

    it = mpls_exp_map_.begin();
    for (; it != mpls_exp_map_.end(); it++) {
        if (it->second.get()->IsResolved() == false) {
            return it->second.get();
        }
    }

    if (default_forwarding_class_) {
        ForwardingClassKSyncObject *fc_object =
            static_cast<ForwardingClassKSyncObject *>(ksync_obj_)
            ->ksync()->forwarding_class_ksync_obj();
        ForwardingClassKSyncEntry fc_key(fc_object,
                                         default_forwarding_class_);
        KSyncEntryPtr ptr = fc_object->GetReference(&fc_key);
        if (ptr && ptr->IsResolved() == false) {
            return ptr.get();
        }
    }

    return NULL;
}

QosConfigKSyncObject::QosConfigKSyncObject(KSync *ksync):
    KSyncDBObject("KSync Qos Config class object"), ksync_(ksync) {
}

QosConfigKSyncObject::~QosConfigKSyncObject() {

}

void QosConfigKSyncObject::RegisterDBClients() {
    RegisterDb(ksync_->agent()->qos_config_table());
}

KSyncEntry*
QosConfigKSyncObject::Alloc(const KSyncEntry *e, uint32_t index) {
    const QosConfigKSyncEntry *entry =
        static_cast<const QosConfigKSyncEntry *>(e);
    QosConfigKSyncEntry *new_entry =
        new QosConfigKSyncEntry(this, entry);
    return static_cast<KSyncEntry *>(new_entry);
}

KSyncEntry *QosConfigKSyncObject::DBToKSyncEntry(const DBEntry *e) {
    const AgentQosConfig *qc = static_cast<const AgentQosConfig *>(e);
    QosConfigKSyncEntry *entry = new QosConfigKSyncEntry(this, qc);
    return static_cast<KSyncEntry *>(entry);
}

void vr_qos_map_req::Process(SandeshContext *context) {
    AgentSandeshContext *ioc = static_cast<AgentSandeshContext *>(context);
    ioc->QosConfigMsgHandler(this);
}

//////////////////////////////////////////////////////////////////////////////
// ksync entry restore routines
//////////////////////////////////////////////////////////////////////////////

KSyncEntry *QosConfigKSyncObject::CreateStale(const KSyncEntry *key) {
    return CreateStaleInternal(key);;
}
void QosConfigKSyncEntry::StaleTimerExpired() {
    Delete();
    SetState(KSyncEntry::TEMP);
}


QosConfigKSyncRestoreData::QosConfigKSyncRestoreData(KSyncDBObject *obj,
                                            KQosConfigPtr qos_config_info):
    KSyncRestoreData(obj), qos_config_info_(qos_config_info) {
}

QosConfigKSyncRestoreData::~QosConfigKSyncRestoreData() {
}

void QosConfigKSyncObject::RestoreVrouterEntriesReq(void) {
    InitStaleEntryCleanup(*(ksync()->agent()->event_manager())->io_service(),
                            KSyncRestoreManager::StaleEntryCleanupTimer, 
                            KSyncRestoreManager::StaleEntryYeildTimer,
                            KSyncRestoreManager::StaleEntryDeletePerIteration);
    KQosConfigReq *req = new KQosConfigReq();
    req->set_index(-1);
    Sandesh::set_response_callback(
        boost::bind(&QosConfigKSyncObject::ReadVrouterEntriesResp, this, _1));
    req->HandleRequest();
    req->Release();
}
void QosConfigKSyncObject::ReadVrouterEntriesResp(Sandesh *sandesh) {
    KQosConfigResp *response = dynamic_cast<KQosConfigResp *>(sandesh);
    if (response != NULL) {
        for(std::vector<KQosConfig>::const_iterator it
                = response->get_qos_config_list().begin();
                it != response->get_qos_config_list().end();it++) {
            QosConfigKSyncRestoreData::KQosConfigPtr 
                            qos_config_info(new KQosConfig(*it));
            KSyncRestoreData::Ptr restore_data(
                        new QosConfigKSyncRestoreData(this, qos_config_info));
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
void QosConfigKSyncObject::ProcessVrouterEntries(
                        KSyncRestoreData::Ptr restore_data) {
    if(dynamic_cast<KSyncRestoreEndData *>(restore_data.get())) {
        ksync()->ksync_restore_manager()->UpdateKSyncRestoreStatus(
                    KSyncRestoreManager::KSYNC_TYPE_QOS_CONFIG);
        return;
    }
    QosConfigKSyncRestoreData *dataPtr = 
        dynamic_cast<QosConfigKSyncRestoreData *>(restore_data.get());
    QosConfigKSyncEntry ksync_entry_key(this,
                        dataPtr->GetQosConfigEntry()->get_id());
    QosConfigKSyncEntry *ksync_entry_ptr =
        static_cast<QosConfigKSyncEntry *>(Find(&ksync_entry_key));
    if (ksync_entry_ptr == NULL) {
        ksync_entry_ptr = 
        static_cast<QosConfigKSyncEntry *>(CreateStale(&ksync_entry_key));
    }
    
}
