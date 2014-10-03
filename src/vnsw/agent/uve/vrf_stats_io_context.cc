/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/vrf_stats_io_context.h>
#include <uve/agent_stats_collector.h>
#include <uve/agent_uve.h>
#include <ksync/ksync_types.h>

void VrfStatsIoContext::Handler() {
    AgentStatsSandeshContext *ctx = static_cast<AgentStatsSandeshContext *>
                                                                       (ctx_);
    AgentStatsCollector *collector = ctx->collector();
    collector->agent()->uve()->vn_uve_table()->SendVnStats(true);
    /* Reset the marker for query during next timer interval, if there is
     * no additional records for the current query */
    if (!ctx->MoreData()) {
        ctx->set_marker_id(-1);
    }
}

void VrfStatsIoContext::ErrorHandler(int err) {
    KSYNC_ERROR(VRouterError, "VRouter VRF stats query failed. Error <", err,
                ":", strerror(err), ">. Object <", "N/A", ">. State <", "N/A",
                ">. Message number :", GetSeqno());
    LOG(ERROR, "Error reading Vrf Stats. Error <" << err << ": "
        << strerror(err) << ": Sequence No : " << GetSeqno());
}

