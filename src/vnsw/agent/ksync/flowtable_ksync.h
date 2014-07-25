/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_FLOWTABLE_KSYNC_H__
#define __AGENT_FLOWTABLE_KSYNC_H__

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/agent_ksync_types.h>
#include <pkt/flow_table.h>
#include <vr_types.h>
#include <vr_flow.h>

class NHKSyncEntry;
class FlowTableKSyncObject;

class FlowTableKSyncEntry : public KSyncNetlinkEntry {
public:
    FlowTableKSyncEntry(FlowTableKSyncObject *obj, FlowEntryPtr fe, 
                        uint32_t hash_id);
    virtual ~FlowTableKSyncEntry();

    FlowEntryPtr flow_entry() const {return flow_entry_;}
    uint32_t hash_id() const {return hash_id_;}
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    KSyncObject *GetObject();

    std::string ToString() const;
    bool IsLess(const KSyncEntry &rhs) const;
    int AddMsg(char *buf, int buf_len);
    int ChangeMsg(char *buf, int buf_len);
    int DeleteMsg(char *buf, int buf_len);
    void SetPcapData(FlowEntryPtr fe, std::vector<int8_t> &data);
    virtual bool Sync();
    virtual KSyncEntry *UnresolvedReference();
    bool AllowDeleteStateComp() {return false;}
    virtual void ErrorHandler(int, uint32_t) const;
private:
    FlowEntryPtr flow_entry_;
    uint32_t hash_id_;
    uint32_t old_hash_id_;
    uint32_t old_reverse_flow_id_;
    uint32_t old_action_;
    uint32_t old_component_nh_idx_;
    uint32_t old_first_mirror_index_;
    uint32_t old_second_mirror_index_;
    uint32_t trap_flow_;
    uint16_t old_drop_reason_;
    bool ecmp_;
    KSyncEntryPtr nh_;
    FlowTableKSyncObject *ksync_obj_;
    DISALLOW_COPY_AND_ASSIGN(FlowTableKSyncEntry);
};

class FlowTableKSyncObject : public KSyncObject {
public:
    static const int kTestFlowTableSize = 131072 * sizeof(vr_flow_entry);
    static const uint32_t AuditYieldTimer = 500;         // in msec
    static const uint32_t AuditTimeout = 2000;           // in msec
    static const int AuditYield = 1024;

    FlowTableKSyncObject(KSync *ksync);
    FlowTableKSyncObject(KSync *ksync, int max_index);
    virtual ~FlowTableKSyncObject();
    vr_flow_req &flow_req() { return flow_req_; }
    KSync *ksync() const { return ksync_; }
    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    bool DoEventTrace(void) { return false; }
    FlowTableKSyncEntry *Find(FlowEntry *key);
    const vr_flow_entry *GetKernelFlowEntry(uint32_t idx, 
                                            bool ignore_active_status);
    bool GetFlowKey(uint32_t index, FlowKey *key);

    uint32_t flow_table_entries_count() { return flow_table_entries_count_; }
    bool AuditProcess();
    void MapFlowMem();
    void MapFlowMemTest();
    void UnmapFlowMemTest();
    void InitFlowMem() {
        MapFlowMem();
    }
    void InitTest() {
        MapFlowMemTest();
    }
    void Init();

    void Shutdown() {
        UnmapFlowMemTest();
    }
private:
    friend class KSyncSandeshContext;
    KSync *ksync_;
    int major_devid_;
    int flow_table_size_;
    vr_flow_req flow_req_;
    vr_flow_entry *flow_table_;
    uint32_t flow_table_entries_count_;
    int audit_yield_;
    uint32_t audit_timeout_;
    uint32_t audit_flow_idx_;
    uint64_t audit_timestamp_;
    std::list<std::pair<uint32_t, uint64_t> > audit_flow_list_;
    Timer *audit_timer_;
    DISALLOW_COPY_AND_ASSIGN(FlowTableKSyncObject);
};

#endif /* __AGENT_FLOWTABLE_KSYNC_H__ */
