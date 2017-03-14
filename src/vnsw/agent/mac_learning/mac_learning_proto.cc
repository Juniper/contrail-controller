/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <init/agent_param.h>
#include "mac_learning_proto.h"
#include "mac_learning_proto_handler.h"
#include "mac_learning.h"
#include "mac_aging.h"

MacLearningProto::MacLearningProto(Agent *agent, boost::asio::io_service &io):
    Proto(agent, kTaskMacLearning, PktHandler::MAC_LEARNING, io),
    add_tokens_("Add Tokens", this, agent->params()->mac_learning_add_tokens()),
    change_tokens_("Change tokens", this,
                   agent->params()->mac_learning_update_tokens()),
    delete_tokens_("Delete tokens", this,
                   agent->params()->mac_learning_delete_tokens()) {
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


TokenPtr MacLearningProto::GetToken(MacLearningEntryRequest::Event event) {
    switch(event) {
    case MacLearningEntryRequest::ADD_MAC:
        return add_tokens_.GetToken();

    case MacLearningEntryRequest::RESYNC_MAC:
        return change_tokens_.GetToken();

    case MacLearningEntryRequest::DELETE_MAC:
        return delete_tokens_.GetToken();

    default:
        assert(0);
    }

    return add_tokens_.GetToken();
}

MacLearningPartition*
MacLearningProto::Find(uint32_t idx) {
    if (idx >= mac_learning_partition_list_.size()) {
        assert(0);
        return NULL;
    }

    return mac_learning_partition_list_[idx].get();
}

void MacLearningProto::TokenAvailable(TokenPool *pool) {
    pool->IncrementRestarts();
    for (uint32_t i = 0;
            i < agent()->params()->mac_learning_thread_count(); i++) {
        mac_learning_partition_list_[i]->MayBeStartRunner(pool);
    }
}

void
MacLearningProto::Init() {
    for (uint32_t i = 0;
            i < agent()->params()->mac_learning_thread_count(); i++) {
        MacLearningPartitionPtr ptr(new MacLearningPartition(agent(), this, i));
        mac_learning_partition_list_.push_back(ptr);
    }
}
