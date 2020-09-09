/*
 * Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_VNSW_AGENT_MAC_IP_LEARNING_MAC_LEARNING_PROTO_HANDLER_H_
#define SRC_VNSW_AGENT_MAC_IP_LEARNING_MAC_LEARNING_PROTO_HANDLER_H_

#include <net/if.h>
#include "cmn/agent.h"
#include "cmn/agent_cmn.h"
#include "base/queue_task.h"
#include "pkt/proto.h"
#include "pkt/proto_handler.h"
#include "mac_learning/mac_ip_learning.h"

class MacIpLearningProtoHandler : public ProtoHandler {
public:
    MacIpLearningProtoHandler(Agent *agent,
                            boost::shared_ptr<PktInfo> info,
                            boost::asio::io_service &io);
    virtual ~MacIpLearningProtoHandler() {}
    bool Run();
private:
    void PktHandler();
    void Log(std::string str);
    const Interface *intf_;
    const VrfEntry *vrf_;
    MacIpLearningTable  *table_;
    MacLearningEntryPtr entry_;
    DISALLOW_COPY_AND_ASSIGN(MacIpLearningProtoHandler);
};
#endif
