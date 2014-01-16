/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/drop_stats_io_context.h>
#include <uve/agent_stats_collector.h>

DropStatsIoContext::DropStatsIoContext(int msg_len, char *msg, uint32_t seqno,
                                       AgentStatsSandeshContext *ctx, 
                                       IoContext::IoContextWorkQId id) 
    : IoContext(msg, msg_len, seqno, ctx, id) {
}

DropStatsIoContext::~DropStatsIoContext() {
}

void DropStatsIoContext::Handler() {
    AgentStatsSandeshContext *ctx = static_cast<AgentStatsSandeshContext *>
                                                                      (ctx_);
    AgentStatsCollector *collector = ctx->collector();
    collector->drop_stats_responses_++;
}

void DropStatsIoContext::ErrorHandler(int err) {
    LOG(ERROR, "Error reading Drop Stats. Error <" << err << ": "
        << strerror(err) << ": Sequence No : " << GetSeqno());
}

