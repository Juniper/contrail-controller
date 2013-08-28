/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_FLOWTABLE_KSYNC_H__
#define __AGENT_FLOWTABLE_KSYNC_H__

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <base/util.h>
#include <base/timer.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include "ksync/agent_ksync_types.h"
#include <pkt/flowtable.h>
#include <vr_types.h>
#include <vr_flow.h>

class FlowTableKSyncEntry : public KSyncNetlinkEntry {
public:
    FlowTableKSyncEntry(FlowEntryPtr fe, uint32_t hash_id) : 
        fe_(fe), hash_id_(hash_id), 
        old_reverse_flow_id_(FlowEntry::kInvalidFlowHandle), old_action_(0), 
        old_component_nh_idx_(0xFFFF), old_first_mirror_index_(0xFFFF), 
        old_second_mirror_index_(0xFFFF) {
    };
    FlowTableKSyncEntry() : hash_id_(0), 
        old_reverse_flow_id_(FlowEntry::kInvalidFlowHandle), old_action_(0), 
        old_component_nh_idx_(0xFFFF), old_first_mirror_index_(0xFFFF),
        old_second_mirror_index_(0xFFFF)  {
    };
    ~FlowTableKSyncEntry() {};

    std::string ToString() const {
        std::ostringstream str;
        str << fe_;
        return str.str();
    };

    bool IsLess(const KSyncEntry &rhs) const {
        const FlowTableKSyncEntry &entry = static_cast<const FlowTableKSyncEntry &>(rhs);
        return fe_ < entry.fe_;
    };

    KSyncObject *GetObject();
    KSyncEntry *UnresolvedReference() {return NULL;};
    char *AddMsg(int &len);
    char *ChangeMsg(int &len);
    char *DeleteMsg(int &len);
    void Response();
    FlowEntryPtr GetFe() const {return fe_;};
    char *Encode(sandesh_op::type op, int &len);
    uint32_t GetHashId() const {return hash_id_;};
    void FillFlowInfo(sandesh_op::type op, uint16_t action, uint16_t flag);
    std::string GetActionString(uint16_t action, uint16_t flag);
    void SetPcapData(FlowEntryPtr fe, std::vector<int8_t> &data);
    virtual bool Sync();
private:
    FlowEntryPtr fe_;
    uint32_t hash_id_;
    uint32_t old_hash_id_;
    uint32_t old_reverse_flow_id_;
    uint32_t old_action_;
    uint32_t old_component_nh_idx_;
    uint32_t old_first_mirror_index_;
    uint32_t old_second_mirror_index_;
    DISALLOW_COPY_AND_ASSIGN(FlowTableKSyncEntry);
};

class FlowTableKSyncObject : public KSyncObject {
public:
    static const int kTestFlowTableSize = 131072;
    static const uint32_t AuditTimeout = 2000;
    static const int AuditYeild = 1024;
    FlowTableKSyncObject() : KSyncObject(), audit_flow_idx_(0),
    audit_timer_(TimerManager::CreateTimer(*(Agent::GetEventManager())->io_service(),
                                           "Flow Audit Timer",
                                           TaskScheduler::GetInstance()->GetTaskId(
                                           "Agent::StatsCollector"))) { };
    FlowTableKSyncObject(int max_index) : KSyncObject(max_index), audit_flow_idx_(0),
    audit_timer_(TimerManager::CreateTimer(*(Agent::GetEventManager())->io_service(),
                                           "Flow Audit Timer",
                                           TaskScheduler::GetInstance()->GetTaskId(
                                           "Agent::StatsCollector"))) { };
    ~FlowTableKSyncObject() {
        TimerManager::DeleteTimer(audit_timer_);
    };

    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index) {
        const FlowTableKSyncEntry *entry  = static_cast<const FlowTableKSyncEntry *>(key);
        FlowTableKSyncEntry *ksync = new FlowTableKSyncEntry(entry->GetFe(),
                                                             entry->GetHashId());
        return static_cast<KSyncEntry *>(ksync);
    }

    FlowTableKSyncEntry *Find(FlowEntry *key) {
        FlowTableKSyncEntry entry(key, key->flow_handle);
        KSyncObject *obj = 
            static_cast<KSyncObject *>(FlowTableKSyncObject::GetKSyncObject());
        return static_cast<FlowTableKSyncEntry *>(obj->Find(&entry));
    }

    static bool AuditProcess(FlowTableKSyncObject *obj);
    static void MapFlowMem();
    static void MapFlowMemTest();
    static void UnmapFlowMemTest();
    static void Init() {
        assert(singleton_ == NULL);
        singleton_ = new FlowTableKSyncObject();
        MapFlowMem();
    };
    static void InitTest() {
        assert(singleton_ == NULL);
        singleton_ = new FlowTableKSyncObject();
        MapFlowMemTest();
    };
    static void Shutdown() {
        UnmapFlowMemTest();
        delete singleton_;
        singleton_ = NULL;
    };

    static void UpdateFlowStats(FlowEntry *fe, bool ignore_active_status) {
        const vr_flow_entry *k_flow = GetKernelFlowEntry(
                                                     fe->flow_handle, 
                                                     ignore_active_status);
        if (k_flow) {
            fe->data.bytes =  k_flow->fe_stats.flow_bytes;
            fe->data.packets =  k_flow->fe_stats.flow_packets;
        }

    }
    static const vr_flow_entry * GetKernelFlowEntry(uint32_t idx, 
                                                bool ignore_active_status) { 
        if (idx == FlowEntry::kInvalidFlowHandle) {
            return NULL;
        }

        if (ignore_active_status) {
            return &flow_table_[idx];
        }

        if (flow_table_[idx].fe_flags & VR_FLOW_FLAG_ACTIVE) {
            return &flow_table_[idx];
        }
        return NULL;
    }

    bool GetFlowKey(uint32_t index, FlowKey &key) {
        const vr_flow_entry *kflow = GetKernelFlowEntry(index, false);
        if (!kflow) {
            return false;
        }
        key.vrf = kflow->fe_key.key_vrf_id;
        key.src.ipv4 = ntohl(kflow->fe_key.key_src_ip);
        key.dst.ipv4 = ntohl(kflow->fe_key.key_dest_ip);
        key.src_port = ntohs(kflow->fe_key.key_src_port);
        key.dst_port = ntohs(kflow->fe_key.key_dst_port);
        key.protocol = kflow->fe_key.key_proto;
        return true;
    }

    static FlowTableKSyncObject *GetKSyncObject() { return singleton_; };
    static uint32_t GetFlowTableSize() { return flow_table_entries_; }
private:
    friend class KSyncSandeshContext;
    static vr_flow_req flow_info_;
    static vr_flow_entry *flow_table_;
    static uint32_t flow_table_entries_;
    static FlowTableKSyncObject *singleton_;
    static int audit_yeild_;
    uint32_t audit_flow_idx_;
    std::list<uint32_t>  audit_flow_list_;
    Timer *audit_timer_;
    DISALLOW_COPY_AND_ASSIGN(FlowTableKSyncObject);
};

#endif /* __AGENT_FLOWTABLE_KSYNC_H__ */
