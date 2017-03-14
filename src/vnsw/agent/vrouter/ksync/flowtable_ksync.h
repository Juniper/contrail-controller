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
#include <vrouter/ksync/agent_ksync_types.h>
#include <vrouter/ksync/ksync_flow_memory.h>
#include <pkt/flow_proto.h>
#include <pkt/flow_table.h>
#include <vr_types.h>
#include <vr_flow.h>

class FlowTableKSyncObject;
class KSyncFlowIndexManager;

struct FlowKSyncResponseInfo {
    int ksync_error_;
    uint32_t flow_handle_;
    uint8_t gen_id_;
    uint64_t evict_flow_bytes_;
    uint64_t evict_flow_packets_;
    int32_t evict_flow_oflow_;

    void Reset() {
        ksync_error_ = 0;
        flow_handle_ = FlowEntry::kInvalidFlowHandle;
        gen_id_ = 0;
        evict_flow_bytes_ = 0;
        evict_flow_packets_ = 0;
        evict_flow_oflow_ = 0;
    }
    FlowKSyncResponseInfo() {
        Reset();
    }
};

class FlowTableKSyncEntry : public KSyncNetlinkEntry {
public:
    FlowTableKSyncEntry(FlowTableKSyncObject *obj);
    FlowTableKSyncEntry(FlowTableKSyncObject *obj, FlowEntry *flow,
                        uint32_t hash_id);
    virtual ~FlowTableKSyncEntry();

    void Reset();
    void Reset(FlowEntry *flow, uint32_t hash_id);

    FlowEntryPtr flow_entry() const {return flow_entry_;}
    uint32_t hash_id() const {return hash_id_;}
    void set_hash_id(uint32_t hash_id) {
        hash_id_ = hash_id;
    }
    int Encode(sandesh_op::type op, char *buf, int buf_len);
    KSyncObject *GetObject() const;

    std::string ToString() const;
    bool IsLess(const KSyncEntry &rhs) const;
    int AddMsg(char *buf, int buf_len);
    int ChangeMsg(char *buf, int buf_len);
    int DeleteMsg(char *buf, int buf_len);
    void SetPcapData(FlowEntryPtr fe, std::vector<int8_t> &data);
    // For flows allocate buffers in ksync-sock context
    virtual bool pre_alloc_rx_buffer() const { return true; }
    // KSync flow responses must be processed in multiple ksync response queues
    // to support scaling. Distribute the flows based on flow-table index
    virtual uint32_t GetTableIndex() const;
    virtual bool Sync();
    virtual KSyncEntry *UnresolvedReference();
    bool AllowDeleteStateComp() {return false;}
    virtual void ErrorHandler(int, uint32_t, KSyncEvent) const;
    virtual std::string VrouterError(uint32_t error) const;
    uint8_t gen_id() { return gen_id_; }
    void set_gen_id(uint8_t gen_id) { gen_id_ = gen_id; }
    uint8_t evict_gen_id() { return evict_gen_id_; }
    void set_evict_gen_id(uint8_t gen_id) { evict_gen_id_ = gen_id; }
    uint8_t vrouter_gen_id() { return vrouter_gen_id_; }
    uint32_t vrouter_hash_id() const { return vrouter_hash_id_; }
    FlowEvent::Event last_event() const { return last_event_; }
    void ReleaseToken();
    void ResetKSyncResponseInfo() {
        ksync_response_info_.Reset();
    }
    void SetKSyncResponseInfo(int ksync_error, uint32_t flow_handle,
                              uint8_t gen_id, uint64_t evict_flow_bytes,
                              uint64_t evict_flow_packets,
                              int32_t evict_flow_oflow) {
        ksync_response_info_.ksync_error_ = ksync_error;
        ksync_response_info_.flow_handle_ = flow_handle;
        ksync_response_info_.gen_id_ = gen_id;
        ksync_response_info_.evict_flow_bytes_ = evict_flow_bytes;
        ksync_response_info_.evict_flow_packets_ = evict_flow_packets;
        ksync_response_info_.evict_flow_oflow_ = evict_flow_oflow;
    }
    const FlowKSyncResponseInfo *ksync_response_info() const {
        return &ksync_response_info_;
    }

    uint32_t old_first_mirror_index() {
        return old_first_mirror_index_;
    }

private:
    friend class KSyncFlowEntryFreeList;
    friend class KSyncFlowIndexManager;

    FlowEntryPtr flow_entry_;
    uint8_t gen_id_;  // contains the last propagated genid from flow module
    uint8_t evict_gen_id_;  // contains current active gen-id in vrouter
    uint8_t vrouter_gen_id_;  // Used to identify the last genid sent to vrouter

    // used to identify last flow index sent to vrouter
    // helps in knowing whether the vrouter response is index
    // allocation or not
    uint32_t vrouter_hash_id_;

    uint32_t hash_id_;
    uint32_t old_reverse_flow_id_;
    uint32_t old_action_;
    uint32_t old_component_nh_idx_;
    uint32_t old_first_mirror_index_;
    uint32_t old_second_mirror_index_;
    uint32_t trap_flow_;
    uint16_t old_drop_reason_;
    bool ecmp_;
    bool enable_rpf_;
    uint32_t src_nh_id_;
    FlowEvent::Event last_event_;
    boost::shared_ptr<Token> token_;
    FlowKSyncResponseInfo ksync_response_info_;
    FlowTableKSyncObject *ksync_obj_;
    boost::intrusive::list_member_hook<> free_list_node_;
    uint32_t qos_config_idx;
    DISALLOW_COPY_AND_ASSIGN(FlowTableKSyncEntry);
};

/////////////////////////////////////////////////////////////////////////////
// Class to manage free-list of flow ksync entries
// Flow allocation can happen from multiple threads. In scaled scenarios
// allocation of flow-entries in multi-thread environment adds overheads.
// The KSyncFlowEntryFreeList helps to maintain a per task free-list. Alloc/Free
// can happen without lock.
//
// Alloc and Free happens in a chunk. Alloc/Free are done based on thresholds
// in task context of the corresponding flow-table
/////////////////////////////////////////////////////////////////////////////
class KSyncFlowEntryFreeList {
public:
    static const uint32_t kInitCount = (25 * 1000);
    static const uint32_t kTestInitCount = (5 * 1000);
    static const uint32_t kGrowSize = (1 * 1000);
    static const uint32_t kMinThreshold = (4 * 1000);

    typedef boost::intrusive::member_hook<FlowTableKSyncEntry,
            boost::intrusive::list_member_hook<>,
            &FlowTableKSyncEntry::free_list_node_> Node;
    typedef boost::intrusive::list<FlowTableKSyncEntry, Node> FreeList;

    KSyncFlowEntryFreeList(FlowTableKSyncObject *object);
    KSyncFlowEntryFreeList(FlowTableKSyncObject *object, FlowEntry *flow,
                           uint32_t hash_id);
    virtual ~KSyncFlowEntryFreeList();

    void Reset();
    void Reset(FlowEntryPtr fe, uint32_t hash_id);

    FlowTableKSyncEntry *Allocate(const KSyncEntry *key);
    void Free(FlowTableKSyncEntry *flow);
    void Grow();
    uint32_t max_count() const { return max_count_; }
    uint32_t free_count() const { return free_list_.size(); }
    uint32_t alloc_count() const { return (max_count_ - free_list_.size()); }
    uint32_t total_alloc() const { return total_alloc_; }
    uint32_t total_free() const { return total_free_; }

private:
    FlowTableKSyncObject *object_;
    uint32_t max_count_;
    bool grow_pending_;
    uint64_t total_alloc_;
    uint64_t total_free_;
    FreeList free_list_;
    DISALLOW_COPY_AND_ASSIGN(KSyncFlowEntryFreeList);
};

class FlowTableKSyncObject : public KSyncObject {
public:
     // flow dependency timer on mirror entry in msec
    static const uint32_t kFlowDepSyncTimeout = 1000;
    static const uint32_t KFlowUnresolvedListYield = 32;
    FlowTableKSyncObject(KSync *ksync);
    FlowTableKSyncObject(KSync *ksync, int max_index);
    virtual ~FlowTableKSyncObject();

    void Init();
    void Shutdown() { }

    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    void Free(KSyncEntry *key);
    bool DoEventTrace(void) { return false; }
    FlowTableKSyncEntry *Find(FlowEntry *key);

    vr_flow_req &flow_req() { return flow_req_; }
    KSync *ksync() const { return ksync_; }
    void set_flow_table(FlowTable *table) { flow_table_ = table; }
    FlowTable *flow_table() const { return flow_table_; }
    void UpdateFlowHandle(FlowTableKSyncEntry *entry, uint32_t flow_handle);
    void UpdateKey(KSyncEntry *entry, uint32_t flow_handle);
    uint32_t GetKey(KSyncEntry *entry);

    void GrowFreeList();
    KSyncFlowEntryFreeList *free_list() { return &free_list_; }

    void NetlinkAck(KSyncEntry *entry, KSyncEntry::KSyncEvent event);
    void GenerateKSyncEvent(FlowTableKSyncEntry *entry,
                            KSyncEntry::KSyncEvent event);
    void StartTimer();
    bool TimerExpiry();
    void UpdateUnresolvedFlowEntry(FlowEntryPtr flowptr);
private:
    friend class KSyncSandeshContext;
    friend class FlowTable;
    KSync *ksync_;
    FlowTable *flow_table_;
    vr_flow_req flow_req_;
    KSyncFlowEntryFreeList free_list_;
    std::list<FlowEntryPtr> unresolved_flow_list_;
    Timer * timer_;
    DISALLOW_COPY_AND_ASSIGN(FlowTableKSyncObject);
};

#endif /* __AGENT_FLOWTABLE_KSYNC_H__ */
