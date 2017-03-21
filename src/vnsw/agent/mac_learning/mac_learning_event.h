/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_EVENT_H_
#define SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_EVENT_H_

#include "cmn/agent.h"
#include "mac_learning_key.h"
#include "mac_learning_base.h"

class MacLearningEntryRequest {
public:
     enum Event {
         INVALID,
         VROUTER_MSG,
         ADD_MAC,
         RESYNC_MAC,
         DELETE_MAC,
         FREE_DB_ENTRY,
         DELETE_VRF
     };

     MacLearningEntryRequest(Event event, PktInfoPtr pkt):
         event_(event), pkt_info_(pkt) {}
     MacLearningEntryRequest(Event event, MacLearningEntryPtr ptr):
         event_(event), mac_learning_entry_(ptr) {
     }

     MacLearningEntryRequest(Event event, uint32_t vrf_id):
         event_(event), vrf_id_(vrf_id) {
     }

     MacLearningEntryRequest(Event event, const DBEntry *entry,
                             uint32_t gen_id):
         event_(event), db_entry_(entry), gen_id_(gen_id) {}

     MacLearningEntryPtr mac_learning_entry() {
         return mac_learning_entry_;
     }

     Event event() {
         return event_;
     }

     const DBEntry *db_entry() {
         return db_entry_;
     }

     PktInfoPtr pkt_info() {
         return pkt_info_;
     }

     uint32_t vrf_id() {
         return vrf_id_;
     }

     uint32_t gen_id() {
         return gen_id_;
     }
private:
    Event event_;
    MacLearningEntryPtr mac_learning_entry_;
    PktInfoPtr pkt_info_;
    uint32_t vrf_id_;
    const DBEntry *db_entry_;
    uint32_t gen_id_;
};
typedef boost::shared_ptr<MacLearningEntryRequest> MacLearningEntryRequestPtr;

class MacLearningRequestQueue {
public:
    typedef WorkQueue<MacLearningEntryRequestPtr> Queue;
    MacLearningRequestQueue(MacLearningPartition *partition,
                            TokenPool *pool);
    ~MacLearningRequestQueue() {}
    void Shutdown() {}
    virtual bool HandleEvent(MacLearningEntryRequestPtr ptr);
    virtual bool TokenCheck();

    void Enqueue(MacLearningEntryRequestPtr ptr) {
        queue_.Enqueue(ptr);
    }

    void MayBeStartRunner() {
        queue_.MayBeStartRunner();
    }

    void SetQueueDisable(bool disable) {
        queue_.set_disable(disable);
    }

private:
    MacLearningPartition *partition_;
    TokenPool *pool_;
    Queue queue_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningRequestQueue);
};
#endif
