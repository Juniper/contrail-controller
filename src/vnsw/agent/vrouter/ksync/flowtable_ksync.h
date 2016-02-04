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
#include <pkt/flow_table.h>
#include <vr_types.h>
#include <vr_flow.h>

class NHKSyncEntry;
class FlowTableKSyncObject;

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
    virtual std::string VrouterError(uint32_t error) const;
private:
    friend class KSyncFlowEntryFreeList;
    bool IgnoreVrouterError() const;

    FlowEntryPtr flow_entry_;
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
    KSyncEntryPtr nh_;
    FlowTableKSyncObject *ksync_obj_;
    boost::intrusive::list_member_hook<> free_list_node_;
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
    FlowTableKSyncObject(KSync *ksync);
    FlowTableKSyncObject(KSync *ksync, int max_index);
    virtual ~FlowTableKSyncObject();

    void Init();
    void Shutdown() { }

    KSyncEntry *Alloc(const KSyncEntry *key, uint32_t index);
    void Free(KSyncEntry *key);
    //bool DoEventTrace(void) { return false; }
    bool DoEventTrace(void) { return true; }
    FlowTableKSyncEntry *Find(FlowEntry *key);
    void PreFree(KSyncEntry *entry);

    vr_flow_req &flow_req() { return flow_req_; }
    KSync *ksync() const { return ksync_; }
    void set_flow_table(FlowTable *table) { flow_table_ = table; }
    FlowTable *flow_table() const { return flow_table_; }
    void UpdateFlowHandle(FlowTableKSyncEntry *entry, uint32_t flow_handle);
    void UpdateKey(KSyncEntry *entry, uint32_t flow_handle);

    void GrowFreeList();
    KSyncFlowEntryFreeList *free_list() { return &free_list_; }

    void NetlinkAck(KSyncEntry *entry, KSyncEntry::KSyncEvent event);
    void GenerateKSyncEvent(FlowTableKSyncEntry *entry,
                            KSyncEntry::KSyncEvent event);
private:
    friend class KSyncSandeshContext;
    friend class FlowTable;
    KSync *ksync_;
    FlowTable *flow_table_;
    vr_flow_req flow_req_;
    KSyncFlowEntryFreeList free_list_;
    DISALLOW_COPY_AND_ASSIGN(FlowTableKSyncObject);
};

#endif /* __AGENT_FLOWTABLE_KSYNC_H__ */
