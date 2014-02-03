/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/interface_stats_io_context.h>
#include <uve/agent_stats_collector.h>
#include <ksync/ksync_types.h>

InterfaceStatsIoContext::InterfaceStatsIoContext(int msg_len, char *msg, 
                                                 uint32_t seqno, 
                                                 AgentStatsSandeshContext *ctx,
                                                 IoContext::IoContextWorkQId id)
    : IoContext(msg, msg_len, seqno, ctx, id) {
}

InterfaceStatsIoContext::~InterfaceStatsIoContext() {
}

void InterfaceStatsIoContext::Handler() {
    AgentStatsSandeshContext *ctx = static_cast<AgentStatsSandeshContext *>
                                                                        (ctx_);
    AgentStatsCollector *collector = ctx->collector();
    collector->run_counter_++;
    collector->SendStats();
    /* Reset the marker for query during next timer interval, if there is
     * no additional records for the current query */
    if (!ctx->MoreData()) {
        ctx->set_marker_id(-1);
    }
}

void InterfaceStatsIoContext::ErrorHandler(int err) {
    KSYNC_ERROR(VRouterError, "VRouter Interface query failed. Error <", err,
                ":", strerror(err), ">. Object <", "N/A", ">. State <", "N/A",
                ">. Message number :", GetSeqno());
    LOG(ERROR, "Error reading Interface Stats. Error <" << err << ": "
        << strerror(err) << ": Sequence No : " << GetSeqno());
}

