/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_BASE_H_
#define SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_BASE_H_

#include "cmn/agent.h"
#include "mac_learning_key.h"
#include "pkt/flow_token.h"

class MacLearningPartition;
class MacAgingTable;
class MacAgingPartition;
class MacLearningEntry {
public:
    typedef std::vector<TokenPtr> TokenList;
    MacLearningEntry(MacLearningPartition *table, uint32_t vrf_id,
                     const MacAddress &mac, uint32_t index);
    virtual ~MacLearningEntry() {}
    virtual bool Add() = 0;
    virtual void Delete();
    virtual void Resync();
    virtual void AddWithToken();

    MacLearningPartition* mac_learning_table() const {
        return mac_learning_table_;
    }

    uint32_t index() const {
        return index_;
    }

    const MacAddress& mac() const {
        return key_.mac_;
    }

    VrfEntry* vrf() const {
        return vrf_.get();
    }

    uint32_t vrf_id() const {
        return key_.vrf_id_;
    }

    const MacLearningKey& key() const {
        return key_;
    }

    void AddToken(TokenPtr ptr) {
        tbb::mutex::scoped_lock lock(mutex_);
        list_.push_back(ptr);
    }

    void ReleaseToken() {
        tbb::mutex::scoped_lock lock(mutex_);
        list_.clear();
    }

    void CopyToken(MacLearningEntry *entry) {
        tbb::mutex::scoped_lock lock(mutex_);
        tbb::mutex::scoped_lock lock2(entry->mutex_);
        list_ = entry->list_;
        entry->list_.clear();
    }

    bool HasTokens() {
        tbb::mutex::scoped_lock lock(mutex_);
        return list_.size();
    }

    bool deleted() const {
        return deleted_;
    }

protected:
    MacLearningPartition *mac_learning_table_;
    MacLearningKey key_;
    uint32_t index_;
    uint32_t ethernet_tag_;
    VrfEntryRef vrf_;
    TokenList list_;
    tbb::mutex mutex_;
    bool deleted_;
private:
    DISALLOW_COPY_AND_ASSIGN(MacLearningEntry);
};
typedef boost::shared_ptr<MacLearningEntry> MacLearningEntryPtr;
#endif
