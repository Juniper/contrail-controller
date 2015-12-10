/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vrouter/stats_collector/drop_stats_io_context.h>
#include <vrouter/stats_collector/agent_stats_collector.h>
#include <ksync/ksync_types.h>

DropStatsIoContext::DropStatsIoContext(int msg_len, char *msg, uint32_t seqno,
                                       AgentStatsSandeshContext *ctx,
                                       IoContext::IoContextWorkQId id)
    : IoContext(msg, msg_len, seqno, ctx, id) {
}

DropStatsIoContext::~DropStatsIoContext() {
}

void DropStatsIoContext::Handler() {
}

void DropStatsIoContext::ErrorHandler(int err) {
    KSYNC_ERROR(VRouterError, "VRouter drop stats query failed. Error <", err,
                ":", KSyncEntry::VrouterErrorToString(err),
                ">. Object <", "N/A", ">. State <", "N/A",
                ">. Message number :", GetSeqno());
    LOG(ERROR, "Error reading Drop Stats. Error <" << err << ": "
        << KSyncEntry::VrouterErrorToString(err)
        << ": Sequence No : " << GetSeqno());
}

