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

QosConfigKSyncEntry::~QosConfigKSyncEntry() {
}

KSyncDBObject *QosConfigKSyncEntry::GetObject() {
    return ksync_obj_;
}

bool QosConfigKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const QosConfigKSyncEntry &entry = static_cast<const QosConfigKSyncEntry &>(rhs);
    return uuid_ < entry.uuid_;
}

std::string QosConfigKSyncEntry::ToString() const {
    std::stringstream s;
    s << "Forwarding class id " << uuid_;
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


    if (trusted_ != qc->trusted()) {
        trusted_ = qc->trusted();
        ret = true;
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

    return ret;
}

int QosConfigKSyncEntry::Encode(sandesh_op::type op, char *buf, int buf_len) {
#if 0
    vr_qos_map_req encoder;

    encoder.set_h_op(op);
    encoder.set_qmr_id(id_);
    encoder.set_qmr_rid(0);

    std::vector<int8_t> key;
    std::vector<int8_t> data;
    KSyncQosFcMap::const_iterator it = dscp_map_.begin();
    for (;it != dscp_map_.end(); it++) {
        ForwardingClassKSyncEntry *fc =
            static_cast<ForwardingClassKSyncEntry *>(it->second.get());
        key.push_back(it->first);
        data.push_back(fc->id());
    }

    encoder.set_qmr_dscp(key);
    encoder.set_qmr_dscp_fc_id(data);

    std::vector<int8_t> vlan_key;
    std::vector<int8_t> vlan_data;
    it = vlan_priority_map_.begin();
    for (;it != vlan_priority_map_.end(); it++) {
        ForwardingClassKSyncEntry *fc =
            static_cast<ForwardingClassKSyncEntry *>(it->second.get());
        vlan_key.push_back(it->first);
        vlan_data.push_back(fc->id());
    }
    encoder.set_qmr_dotonep(vlan_key);
    encoder.set_qmr_dotonep_fc_id(vlan_data);

    std::vector<int8_t> mpls_key;
    std::vector<int8_t> mpls_data;
    it = mpls_exp_map_.begin();
    for (;it != mpls_exp_map_.end(); it++) {
        ForwardingClassKSyncEntry *fc =
            static_cast<ForwardingClassKSyncEntry *>(it->second.get());
        mpls_key.push_back(it->first);
        mpls_data.push_back(fc->id());
    }
    encoder.set_qmr_mpls_qos(mpls_key);
    encoder.set_qmr_mpls_qos_fc_id(mpls_data);

    int error = 0;
    int encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    assert(error == 0);
    assert(encode_len <= buf_len);
    return encode_len;
#endif
}

int QosConfigKSyncEntry::AddMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int QosConfigKSyncEntry::ChangeMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::ADD, buf, buf_len);
}

int QosConfigKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    return Encode(sandesh_op::DELETE, buf, buf_len);
}

KSyncEntry *QosConfigKSyncEntry::UnresolvedReference() {

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

    return NULL;
}

QosConfigKSyncObject::QosConfigKSyncObject(KSync *ksync):
    KSyncDBObject("KSync Forwarding class object"), ksync_(ksync) {
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
