/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_PROTO_H_
#define SRC_VNSW_AGENT_MAC_LEARNING_MAC_LEARNING_PROTO_H_

#include "cmn/agent.h"
#include "cmn/agent_cmn.h"
#include "base/queue_task.h"
#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "pkt/flow_token.h"
#include "mac_learning_event.h"

class MacLearningPartition;
class MacAgingTable;

class MacLearningProto : public Proto {
public:
    typedef boost::shared_ptr<MacLearningPartition> MacLearningPartitionPtr;
    typedef std::vector<MacLearningPartitionPtr> MacLearningPartitionList;

    typedef boost::shared_ptr<MacAgingTable> MacAgingTablePtr;

    MacLearningProto(Agent *agent,
                     boost::asio::io_service &io);
    virtual ~MacLearningProto() {}

    virtual bool Validate(PktInfo *msg) { return true; }
    virtual ProtoHandler*
        AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                          boost::asio::io_service &io);
    MacLearningPartition* Find(uint32_t index);

    void Delete(uint32_t index) {
        mac_learning_partition_list_[index].reset();
    }
    bool Enqueue(PktInfoPtr msg);
    void Init();
    uint32_t Hash(uint32_t vrf_id, const MacAddress &mac);
    uint32_t size() {
        return mac_learning_partition_list_.size();
    }

    TokenPool* add_tokens() {
        return &add_tokens_;
    }

    TokenPool* change_tokens() {
        return &change_tokens_;
    }

    TokenPool* delete_tokens() {
        return &delete_tokens_;
    }

    TokenPtr GetToken(MacLearningEntryRequest::Event event);
    virtual void TokenAvailable(TokenPool *pool);

private:
    tbb::mutex::scoped_lock mutex_;
    MacLearningPartitionList mac_learning_partition_list_;
    TokenPool add_tokens_;
    TokenPool change_tokens_;
    TokenPool delete_tokens_;
    DISALLOW_COPY_AND_ASSIGN(MacLearningProto);
};
#endif
