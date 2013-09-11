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
    int AddMsg(char *buf, int buf_len);
    int ChangeMsg(char *buf, int buf_len);
    int DeleteMsg(char *buf, int buf_len);
    void Response();
    FlowEntryPtr GetFe() const {return fe_;};
    int Encode(sandesh_op::type op, char *buf, int buf_len);
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
    static const int kTestFlowTableSize = 131072 * sizeof(vr_flow_entry);
    static const uint32_t AuditTimeout = 2000;
    static const int AuditYeild = 1024;

    FlowTableKSyncObject();
    FlowTableKSyncObject(int max_index);
    ~FlowTableKSyncObject();
    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    FlowTableKSyncEntry *Find(FlowEntry *key);
    void UpdateFlowStats(FlowEntry *fe, bool ignore_active_status);
    const vr_flow_entry *GetKernelFlowEntry(uint32_t idx, 
                                            bool ignore_active_status);
    bool GetFlowKey(uint32_t index, FlowKey &key);

    uint32_t GetFlowTableSize() { return flow_table_entries_; }
    static bool AuditProcess(FlowTableKSyncObject *obj);
    void MapFlowMem();
    void MapFlowMemTest();
    void UnmapFlowMemTest();

    static FlowTableKSyncObject *GetKSyncObject() { return singleton_; };
    static void Init() {
        assert(singleton_ == NULL);
        singleton_ = new FlowTableKSyncObject();
        singleton_->MapFlowMem();
    };
    static void InitTest() {
        assert(singleton_ == NULL);
        singleton_ = new FlowTableKSyncObject();
        singleton_->MapFlowMemTest();
    };
    static void Shutdown() {
        singleton_->UnmapFlowMemTest();
        delete singleton_->singleton_;
        singleton_->singleton_ = NULL;
    };

private:
    friend class KSyncSandeshContext;
    static FlowTableKSyncObject *singleton_;
    vr_flow_req flow_info_;
    vr_flow_entry *flow_table_;
    uint32_t flow_table_entries_;
    int audit_yeild_;
    uint32_t audit_flow_idx_;
    std::list<uint32_t> audit_flow_list_;
    Timer *audit_timer_;
    DISALLOW_COPY_AND_ASSIGN(FlowTableKSyncObject);
};

#endif /* __AGENT_FLOWTABLE_KSYNC_H__ */
