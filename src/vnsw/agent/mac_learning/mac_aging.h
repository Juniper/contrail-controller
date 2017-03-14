/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_MAC_LEARNING_MAC_AGING_H_
#define SRC_VNSW_AGENT_MAC_LEARNING_MAC_AGING_H_

#include "cmn/agent.h"
class MacEntryResp;
class SandeshMacEntry;

class MacAgingEntry {
public:
    MacAgingEntry(MacLearningEntryPtr ptr);
    virtual ~MacAgingEntry() {}

    void set_mac_learning_entry(MacLearningEntryPtr ptr) {
        mac_learning_entry_ = ptr;
    }

    MacLearningEntryPtr mac_learning_entry() const {
        return mac_learning_entry_;
    }

    void set_last_modified_time(uint64_t curr_time) {
        last_modified_time_ = curr_time;
    }

    uint64_t last_modified_time() const {
        return last_modified_time_;
    }

    uint64_t packets() const {
        return packets_;
    }

    void set_packets(uint64_t packets) {
        packets_ = packets;
    }

    void set_deleted(bool deleted) {
        deleted_ = deleted;
    }

    bool deleted() const {
        return deleted_;
    }

    void FillSandesh(SandeshMacEntry *sme) const;
private:
    MacLearningEntryPtr mac_learning_entry_;
    uint64_t packets_;
    uint64_t last_modified_time_;
    bool deleted_;
    uint64_t addition_time_;
};
typedef boost::shared_ptr<MacAgingEntry> MacAgingEntryPtr;

//Per VRF mac aging table
class MacAgingTable {
public:
    static const uint32_t kDefaultAgingTimeout = 30 * 1000;
    static const uint32_t kMinEntriesPerScan = 100;
    typedef std::pair<MacLearningEntry*, MacAgingEntryPtr> MacAgingPair;
    typedef std::map<MacLearningEntry*, MacAgingEntryPtr> MacAgingEntryTable;

    MacAgingTable(Agent *agent, const VrfEntry *);
    virtual ~MacAgingTable();
    uint32_t CalculateEntriesPerIteration(uint32_t table_size);
    uint64_t timeout_in_usecs() const {
        return timeout_msec_ * 1000;
    }

    void set_timeout(uint32_t msec) {
        timeout_msec_ = msec;
    }
    bool Run();
    void Add(MacLearningEntryPtr ptr);
    void Delete(MacLearningEntryPtr ptr);

    const MacAgingEntry *Find(MacLearningEntry *me) const {
        MacAgingEntryTable::const_iterator it =
            aging_table_.find(me);
        if (it != aging_table_.end()) {
            return it->second.get();
        }
        return NULL;
    }

private:
    bool ShouldBeAged(MacAgingEntry *ptr, uint64_t curr_time);
    void SendDeleteMsg(MacAgingEntry *ptr);
    void ReadStats(MacAgingEntry *ptr);
    void Trace(const std::string &str, MacAgingEntry *ptr);
    friend class MacAgingSandeshResp;
    Agent *agent_;
    MacAgingEntryTable aging_table_;
    MacLearningEntry* last_key_;
    uint32_t timeout_msec_;
    VrfEntryConstRef vrf_;
    DISALLOW_COPY_AND_ASSIGN(MacAgingTable);
};

//MacAgingPartition maintains Per VRF mac entries
//for aging purpose. Timer for each partition gets
//fired every 100ms and goes thru all the VRF entries.
//No. of entries to be visited would be based on aging timeout
//and no. of entries in tree.
class MacAgingPartition {
public:
    static const uint32_t kMinIterationTimeout = 1 * 100;
    typedef WorkQueue<MacLearningEntryRequestPtr> MacAgingQueue;
    typedef boost::shared_ptr<MacAgingTable> MacAgingTablePtr;
    typedef std::pair<uint32_t, MacAgingTablePtr> MacAgingTablePair;
    typedef std::map<uint32_t, MacAgingTablePtr> MacAgingTableMap;
    MacAgingPartition(Agent *agent, uint32_t id);
    virtual ~MacAgingPartition();
    void Enqueue(MacLearningEntryRequestPtr req);
    bool Run();
    bool RequestHandler(MacLearningEntryRequestPtr ptr);
    void Add(MacLearningEntryPtr ptr);
    void Delete(MacLearningEntryPtr ptr);

    MacAgingTable *Find(uint32_t id) {
        return aging_table_map_[id].get();
    }

private:
    void DeleteVrf(uint32_t id);
    friend class MacAgingSandeshResp;
    Agent *agent_;
    uint32_t partition_id_;
    MacAgingQueue request_queue_;
    Timer *timer_;
    tbb::mutex mutex_;
    MacAgingTableMap aging_table_map_;
    DISALLOW_COPY_AND_ASSIGN(MacAgingPartition);
};
#endif
