/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include "mac_learning_proto.h"
#include "mac_learning_proto_handler.h"
#include "mac_learning.h"
#include "mac_aging.h"

MacLearningProto::MacLearningProto(Agent *agent, boost::asio::io_service &io):
    Proto(agent, kTaskMacLearning, PktHandler::MAC_LEARNING, io) {
    Init();
}

ProtoHandler*
MacLearningProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                    boost::asio::io_service &io) {
    return new MacLearningProtoHandler(agent(), info, io);
}

uint32_t MacLearningProto::Hash(uint32_t vrf_id, const MacAddress &mac) {
    size_t val = 0;
    uint8_t mac_array[ETH_ALEN];

    boost::hash_combine(val, vrf_id);
    mac.ToArray(mac_array, sizeof(mac_array));

    for (uint32_t i = 0; i < ETH_ALEN; i++) {
        boost::hash_combine(val, mac_array[i]);
    }

    return val % mac_learning_partition_list_.size();
}

bool
MacLearningProto::Enqueue(PktInfoPtr msg) {
    //XXXX disable trace ??
    //FreeBuffer(msg.get());
    MacLearningEntryRequestPtr ptr(new MacLearningEntryRequest(
                                    MacLearningEntryRequest::VROUTER_MSG, msg));

    uint32_t table_index = Hash(msg->agent_hdr.vrf, msg->smac);
    MacLearningPartition* partition = Find(table_index);
    assert(partition);
    partition->Enqueue(ptr);
    return true;
}

MacLearningPartition*
MacLearningProto::Find(uint32_t idx) {
    if (idx >= mac_learning_partition_list_.size()) {
        assert(0);
        return NULL;
    }

    return mac_learning_partition_list_[idx].get();
}

void
MacLearningProto::Init() {
    for (uint32_t i = 0; i < agent()->flow_thread_count(); i++) {
        MacLearningPartitionPtr ptr(new MacLearningPartition(agent(), i));
        mac_learning_partition_list_.push_back(ptr);
    }
}
