/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_BASE_H_
#define SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_BASE_H_

#include "cmn/agent.h"
#include "mac_learning_key.h"
#include "pkt/flow_token.h"

class MacAgingTable;
class MacAgingPartition;
class MacLearningEntryRequest;

class MacLearningEntry;
typedef boost::shared_ptr<MacLearningEntry> MacLearningEntryPtr;
typedef boost::shared_ptr<MacLearningEntryRequest> MacLearningEntryRequestPtr;
class MacLearningEntry {
public:
    MacLearningEntry(uint32_t vrf_id):
         deleted_(false) {
        vrf_ = Agent::GetInstance()->vrf_table()->
                  FindVrfFromIdIncludingDeletedVrf(vrf_id);
}

    virtual ~MacLearningEntry() {}
    virtual bool Add() = 0;
    virtual void Delete() = 0;
    virtual void Resync() = 0;


    VrfEntry* vrf() const {
        return vrf_.get();
    }

    virtual uint32_t vrf_id() = 0;

    virtual void AddWithToken() {
    }

    virtual void AddToken(TokenPtr ptr) {
    }

    virtual void ReleaseToken() {
    }

    virtual void CopyToken(MacLearningEntry *entry) {
    }

    virtual bool HasTokens() {
        return false;
    }

    bool deleted() const {
        return deleted_;
    }
    virtual void EnqueueToTable(MacLearningEntryRequestPtr req)= 0;

protected:
    VrfEntryRef vrf_;
    bool deleted_;
private:
    DISALLOW_COPY_AND_ASSIGN(MacLearningEntry);
};
#endif
