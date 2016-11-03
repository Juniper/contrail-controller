/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <vrouter/stats_collector/interface_stats_io_context.h>
#include <vrouter/stats_collector/agent_stats_collector.h>
#include <ksync/ksync_types.h>
#include <uve/interface_uve_stats_table.h>
#include <uve/vm_uve_table.h>

InterfaceStatsIoContext::InterfaceStatsIoContext(int msg_len, char *msg,
                                                 uint32_t seqno,
                                                 AgentStatsSandeshContext *ctx,
                                                 IoContext::Type type)
    : IoContext(msg, msg_len, seqno, ctx, type) {
}

InterfaceStatsIoContext::~InterfaceStatsIoContext() {
}

void InterfaceStatsIoContext::UpdateMarker() {
    AgentStatsSandeshContext *ctx =
        static_cast<AgentStatsSandeshContext *> (sandesh_context_);

    int id = ctx->marker_id();
    if (id != AgentStatsSandeshContext::kInvalidIndex) {
        StatsManager::InterfaceStats *stats = ctx->IdToStats(id);
        if (!stats) {
            /* Stats Entry for interface does not exist. Interface could have
             * been deleted */
            if (!ctx->MoreData()) {
                /* No more queries to vrouter */
                ctx->set_marker_id(AgentStatsSandeshContext::kInvalidIndex);
            }
            /* The marker will remain unchanged if there are more queries to be
             * done */
            return;
        }
        if (stats->drop_stats_received) {
            if (!ctx->MoreData()) {
                /* No more queries to vrouter */
                ctx->set_marker_id(AgentStatsSandeshContext::kInvalidIndex);
            }
            /* The marker will remain unchanged if there are more queries to be
             * done */
        } else {
            /* Drop stats not received for last interface. Start fetching the
             * stats from last interface */
            ctx->set_marker_id((id-1));
        }
    }
}

void InterfaceStatsIoContext::Handler() {
    AgentStatsSandeshContext *ctx =
        static_cast<AgentStatsSandeshContext *> (sandesh_context_);
    AgentStatsCollector *collector = ctx->agent()->stats_collector();
    /* (1) Reset the marker for query during next timer interval, if there is
     *     no additional records for the current query
     * (2) If there are additional interfaces to be queried, send DUMP request
     *     for those as well
     * (3) Send UVE for stats info only when we have queried and obtained
     *     results for all interfaces. */
    UpdateMarker();
    if (ctx->marker_id() == AgentStatsSandeshContext::kInvalidIndex) {
        InterfaceUveStatsTable *it = static_cast<InterfaceUveStatsTable *>
            (ctx->agent()->uve()->interface_uve_table());
        it->SendInterfaceStats();
        VmUveTable *vmt = static_cast<VmUveTable *>
            (ctx->agent()->uve()->vm_uve_table());
        vmt->SendVmStats();
    } else {
        collector->SendInterfaceBulkGet();
    }
}

void InterfaceStatsIoContext::ErrorHandler(int err) {
    KSYNC_ERROR(VRouterError, "VRouter Interface query failed. Error <", err,
                ":", KSyncEntry::VrouterErrorToString(err),
                ">. Object <", "N/A", ">. State <", "N/A",
                ">. Message number :", GetSeqno());
    LOG(ERROR, "Error reading Interface Stats. Error <" << err << ": "
        << KSyncEntry::VrouterErrorToString(err)
        << ": Sequence No : " << GetSeqno());
}

