/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __MAC_AGING_H__
#define __MAC_AGIGN_H__

#include "cmn/agent.h"

class MacAgingEntry {
public:
    MacAgingEntry(MacLearningEntryPtr ptr);
    ~MacAgingEntry() {}

    void set_mac_learning_entry(MacLearningEntryPtr ptr) {
        mac_learning_entry_ = ptr;
    }

    MacLearningEntryPtr mac_learning_entry() {
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
private:
    MacLearningEntryPtr mac_learning_entry_;
    uint64_t packets_;
    uint64_t last_modified_time_;
};
typedef boost::shared_ptr<MacAgingEntry> MacAgingEntryPtr;

//Per VRF mac aging table
class MacAgingTable {
public:
    static const uint32_t kDefaultAgingTimeout = 30 * 1000;
    static const uint32_t kMinEntriesPerScan = 100;
    typedef std::pair<MacAddress, MacAgingEntryPtr> MacAgingPair;
    typedef std::map<MacAddress, MacAgingEntryPtr> MacAgingEntryTable;

    MacAgingTable(Agent *agent, uint32_t id, const VrfEntry *);
    ~MacAgingTable();
    uint32_t CalculateEntriesPerIteration(uint32_t table_size);
    uint64_t timeout_in_usecs() {
        return timeout_msec_ * 1000;
    }

    void set_timeout(uint32_t msec) {
        timeout_msec_ = msec;
    }
    bool Run();
    void Add(MacLearningEntryPtr ptr);
    void Delete(MacLearningEntryPtr ptr);

private:
    bool ShouldBeAged(MacAgingEntryPtr ptr, uint64_t curr_time);
    void SendDeleteMsg(MacAgingEntryPtr ptr);
    void ReadStats(MacAgingEntryPtr ptr);
    void Log(std::string str, MacAgingEntryPtr ptr);
    Agent *agent_;
    uint32_t id_;
    MacAgingEntryTable aging_table_;
    MacAddress last_key_;
    uint32_t timeout_msec_;
    VrfEntryConstRef vrf_;
    DISALLOW_COPY_AND_ASSIGN(MacAgingTable);
};

class MacAgingPartition {
public:
    static const uint32_t kMinIterationTimeout = 1 * 100;
    typedef boost::shared_ptr<MacAgingTable> MacAgingTablePtr;
    typedef std::pair<uint32_t, MacAgingTablePtr> MacAgingTablePair;
    typedef std::map<uint32_t, MacAgingTablePtr> MacAgingTableMap;
    MacAgingPartition(Agent *agent, uint32_t id);
    ~MacAgingPartition();
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
    Agent *agent_;
    uint32_t id_;
    MacLearningTable::MacLearningProtoQueue request_queue_;
    Timer *timer_;
    tbb::mutex mutex_;
    MacAgingTableMap aging_table_map_;
    DISALLOW_COPY_AND_ASSIGN(MacAgingPartition);
};
#endif
