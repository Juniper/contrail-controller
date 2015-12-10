/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vrouter/stats_collector/vrf_stats_io_context.h>
#include <vrouter/stats_collector/agent_stats_collector.h>
#include <uve/agent_uve.h>
#include <uve/vn_uve_table.h>
#include <ksync/ksync_types.h>

void VrfStatsIoContext::Handler() {
    AgentStatsSandeshContext *ctx =
        static_cast<AgentStatsSandeshContext *> (sandesh_context_);
    VnUveTable *vt = static_cast<VnUveTable *>
        (ctx->agent()->uve()->vn_uve_table());
    vt->SendVnStats(true);
    /* Reset the marker for query during next timer interval, if there is
     * no additional records for the current query */
    if (!ctx->MoreData()) {
        ctx->set_marker_id(-1);
    }
}

void VrfStatsIoContext::ErrorHandler(int err) {
    KSYNC_ERROR(VRouterError, "VRouter VRF stats query failed. Error <", err,
                ":", KSyncEntry::VrouterErrorToString(err),
                ">. Object <", "N/A", ">. State <", "N/A",
                ">. Message number :", GetSeqno());
    LOG(ERROR, "Error reading Vrf Stats. Error <" << err << ": "
        << KSyncEntry::VrouterErrorToString(err)
        << ": Sequence No : " << GetSeqno());
}

