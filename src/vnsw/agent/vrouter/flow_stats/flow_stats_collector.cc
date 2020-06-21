/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <db/db.h>
#include <base/util.h>
#include <base/string_util.h>

#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <boost/functional/factory.hpp>
#include <cmn/agent_factory.h>
#include <oper/interface_common.h>
#include <oper/mirror_table.h>
#include <oper/global_vrouter.h>

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>
#include <uve/agent_uve.h>
#include <vrouter/flow_stats/flow_stats_collector.h>
#include <uve/vn_uve_table.h>
#include <uve/vm_uve_table.h>
#include <uve/interface_uve_stats_table.h>
#include <uve/vrouter_uve_entry.h>
#include <algorithm>
#include <pkt/flow_proto.h>
#include <pkt/flow_mgmt.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/flow_stats/flow_stats_types.h>

bool flow_ageing_debug_ = false;
FlowStatsCollector::FlowStatsCollector(boost::asio::io_service &io, int intvl,
                                       uint32_t flow_cache_timeout,
                                       AgentUveBase *uve,
                                       uint32_t instance_id,
                                       FlowAgingTableKey *key,
                                       FlowStatsManager *aging_module,
                                       FlowStatsCollectorObject *obj) :
        StatsCollector(TaskScheduler::GetInstance()->GetTaskId
                       (kTaskFlowStatsCollector), instance_id,
                       io, kFlowStatsTimerInterval, "Flow stats collector"),
        agent_uve_(uve),
        task_id_(uve->agent()->task_scheduler()->GetTaskId
                 (kTaskFlowStatsCollector)),
        rand_gen_(boost::uuids::random_generator()),
        flow_iteration_key_(NULL),
        entries_to_visit_(0),
        flow_tcp_syn_age_time_(FlowTcpSynAgeTime),
        retry_delete_(true),
        request_queue_(agent_uve_->agent()->task_scheduler()->
                       GetTaskId(kTaskFlowStatsCollector),
                       instance_id,
                       boost::bind(&FlowStatsCollector::RequestHandler,
                                   this, _1)),
        flow_aging_key_(*key), instance_id_(instance_id),
        flow_stats_manager_(aging_module), parent_(obj), ageing_task_(NULL),
        current_time_(GetCurrentTime()), ageing_task_starts_(0) {
        if (flow_cache_timeout) {
            // Convert to usec
            flow_age_time_intvl_ = 1000000L * (uint64_t)flow_cache_timeout;
        } else {
            flow_age_time_intvl_ = FlowAgeTime;
        }
        deleted_ = false;
        request_queue_.set_name("Flow stats collector");
        request_queue_.set_measure_busy_time
            (agent_uve_->agent()->MeasureQueueDelay());
        request_queue_.SetEntryCallback
            (boost::bind(&FlowStatsCollector::RequestHandlerEntry, this));
        request_queue_.SetExitCallback
            (boost::bind(&FlowStatsCollector::RequestHandlerExit, this, _1));
        // Aging timer fires every kFlowStatsTimerInterval msec. Compute
        // number of timer fires needed to scan complete table
        timers_per_scan_ = TimersPerScan();
        InitDone();
}

FlowStatsCollector::~FlowStatsCollector() {
    flow_stats_manager_->FreeIndex(instance_id_);
}

boost::uuids::uuid FlowStatsCollector::rand_gen() {
    return rand_gen_();
}

uint64_t FlowStatsCollector::GetCurrentTime() {
    return UTCTimestampUsec();
}

void FlowStatsCollector::Shutdown() {
    assert(ageing_task_ == NULL);
    StatsCollector::Shutdown();
    request_queue_.Shutdown();
}

// We want to scan the flow table every 25% of configured ageing time.
// Compute number of timer fires needed to scan the flow-table once.
uint32_t FlowStatsCollector::TimersPerScan() {
    uint64_t scan_time_millisec;
    /* Use Age Time itself as scan-time for flows */

    // Convert aging-time configured in micro-sec to millisecond
    scan_time_millisec = flow_age_time_intvl_ / 1000;

    // Compute time in which we must scan the complete table to honor the
    // kFlowScanTime
    scan_time_millisec = (scan_time_millisec * kFlowScanTime) / 100;

    // Enforce min value on scan-time
    if (scan_time_millisec < kFlowStatsTimerInterval) {
        scan_time_millisec = kFlowStatsTimerInterval;
    }

    // Number of timer fires needed to scan table once
    return scan_time_millisec / kFlowStatsTimerInterval;
}

// Update entries_to_visit_ based on total flows
// Timer fires every kFlowScanTime. Its possible that we may not have visited
// all entries by the time next timer fires. So, keep accumulating the number
// of entries to visit into entries_to_visit_
//
// A lower-bound and an upper-bound are enforced on entries_to_visit_
void FlowStatsCollector::UpdateEntriesToVisit() {
    // Compute number of flows to visit per scan-time
    uint32_t count = flow_export_info_list_.size();
    uint32_t entries = count / timers_per_scan_;

    // Update number of entries to visit in flow.
    // The scan for previous timer may still be in progress. So, accmulate
    // number of entries to visit
    entries_to_visit_ += entries;

    // Cap number of entries to visit to 25% of table
    if (entries_to_visit_ > ((count * kFlowScanTime)/100))
        entries_to_visit_ = (count * kFlowScanTime)/100;

    // Apply lower-limit
    if (entries_to_visit_ < kMinFlowsPerTimer)
        entries_to_visit_ = kMinFlowsPerTimer;

    return;
}

bool FlowStatsCollector::ShouldBeAged(FlowExportInfo *info,
                                      const vr_flow_entry *k_flow,
                                      const vr_flow_stats &k_stats,
                                      uint64_t curr_time) {
    FlowEntry *flow = info->flow();
    //If both forward and reverse flow are marked
    //as TCP closed then immediately remote the flow
    if (k_flow != NULL) {
        uint64_t k_flow_bytes, bytes;
        k_flow_bytes = GetFlowStats(k_stats.flow_bytes_oflow,
                                    k_stats.flow_bytes);
        bytes = 0x0000ffffffffffffULL & info->bytes();
        /* Don't account for agent overflow bits while comparing change in
         * stats */
        if (bytes < k_flow_bytes) {
            return false;
        }
    }

    uint64_t diff_time = curr_time - info->last_modified_time();
    if (diff_time < flow_age_time_intvl()) {
        return false;
    }

    if (flow->is_flags_set(FlowEntry::BgpRouterService)) {
        return false;
    }

    return true;
}

uint64_t FlowStatsCollector::GetFlowStats(const uint16_t &oflow_data,
                                          const uint32_t &data) {
    uint64_t flow_stats = (uint64_t) oflow_data << (sizeof(uint32_t) * 8);
    flow_stats |= data;
    return flow_stats;
}

uint64_t FlowStatsCollector::GetUpdatedFlowBytes(const FlowExportInfo *stats,
                                                 uint64_t k_flow_bytes) {
    uint64_t oflow_bytes = 0xffff000000000000ULL & stats->bytes();
    uint64_t old_bytes = 0x0000ffffffffffffULL & stats->bytes();
    if (old_bytes > k_flow_bytes) {
        oflow_bytes += 0x0001000000000000ULL;
    }
    return (oflow_bytes |= k_flow_bytes);
}

uint64_t FlowStatsCollector::GetUpdatedFlowPackets(const FlowExportInfo *stats,
                                                   uint64_t k_flow_pkts) {
    uint64_t oflow_pkts = 0xffffff0000000000ULL & stats->packets();
    uint64_t old_pkts = 0x000000ffffffffffULL & stats->packets();
    if (old_pkts > k_flow_pkts) {
        oflow_pkts += 0x0000010000000000ULL;
    }
    return (oflow_pkts |= k_flow_pkts);
}

void FlowStatsCollector::UpdateFloatingIpStats(const FlowExportInfo *flow,
                                               uint64_t bytes, uint64_t pkts) {
    InterfaceUveTable::FipInfo fip_info;
    FlowEntry *fe = flow->flow();
    if (!fe) {
        return;
    }

    /* Ignore Non-Floating-IP flow */
    if (!fe->fip() || fe->fip_vmi().uuid_ == boost::uuids::nil_uuid()) {
        return;
    }

    InterfaceUveStatsTable *table = static_cast<InterfaceUveStatsTable *>
        (agent_uve_->interface_uve_table());

    fip_info.bytes_ = bytes;
    fip_info.packets_ = pkts;
    fip_info.fip_ = fe->fip();
    fip_info.fip_vmi_ = fe->fip_vmi();
    fip_info.is_local_flow_ = fe->is_flags_set(FlowEntry::LocalFlow);
    fip_info.is_ingress_flow_ = fe->is_flags_set(FlowEntry::IngressDir);
    fip_info.is_reverse_flow_ = fe->is_flags_set(FlowEntry::ReverseFlow);
    fip_info.vn_ = fe->data().source_vn_match;

    fip_info.rev_fip_ = NULL;
    if (fe->fip() != ReverseFlowFip(flow)) {
        /* This is the case where Source and Destination VMs (part of
         * same compute node) ping to each other to their respective
         * Floating IPs. In this case for each flow we need to increment
         * stats for both the VMs */
        fip_info.rev_fip_ = ReverseFlowFipEntry(flow);
    }

    table->UpdateFloatingIpStats(fip_info);
}

InterfaceUveTable::FloatingIp *FlowStatsCollector::ReverseFlowFipEntry
    (const FlowExportInfo *flow) {
    uint32_t fip = ReverseFlowFip(flow);
    VmInterfaceKey vmi = ReverseFlowFipVmi(flow);
    Interface *intf = dynamic_cast<Interface *>
        (agent_uve_->agent()->interface_table()->FindActiveEntry(&vmi));

    if (intf) {
        InterfaceUveStatsTable *table = static_cast<InterfaceUveStatsTable *>
            (agent_uve_->interface_uve_table());
        const string &vn = flow->flow()->data().source_vn_match;
        return table->FipEntry(fip, vn, intf);
    }
    return NULL;
}

uint32_t FlowStatsCollector::ReverseFlowFip(const FlowExportInfo *info) {
    FlowEntry *rflow = info->reverse_flow();
    if (rflow) {
        return rflow->fip();
    }
    return 0;
}

VmInterfaceKey FlowStatsCollector::ReverseFlowFipVmi
    (const FlowExportInfo *info)
{
    FlowEntry *rflow = info->reverse_flow();
    if (rflow) {
        return rflow->fip_vmi();
    }
    return VmInterfaceKey(
        AgentKey::ADD_DEL_CHANGE, boost::uuids::nil_uuid(), "");
}

void FlowStatsCollector::UpdateVmiTagBasedStats(FlowExportInfo *info,
                                                uint64_t bytes, uint64_t pkts) {
    FlowEntry *flow = info->flow();

    const Interface *itf = flow->intf_entry();
    if (!itf) {
        return;
    }
    if (itf->type() != Interface::VM_INTERFACE) {
        return;
    }
    const VmInterface *vmi = static_cast<const VmInterface *>(itf);
    const string &src_vn = flow->data().source_vn_match;
    const string &dst_vn = flow->data().dest_vn_match;

    /* Ignore flows for which source VN or destination VN are not known */
    if (!src_vn.length() || !dst_vn.length()) {
        return;
    }

    InterfaceUveStatsTable *itf_table = static_cast<InterfaceUveStatsTable *>
        (agent_uve_->interface_uve_table());
    EndpointStatsInfo ep;
    ep.vmi = vmi;
    ep.local_tagset = flow->local_tagset();
    ep.remote_tagset = flow->remote_tagset();
    ep.remote_prefix = flow->RemotePrefix();
    ep.policy = flow->fw_policy_name_uuid();
    ep.diff_bytes = bytes;
    ep.diff_pkts = pkts;
    FlowTable::GetFlowSandeshActionParams(flow->data().match_p.action_info,
                                          ep.action);
    if (flow->is_flags_set(FlowEntry::LocalFlow)) {
        /* When VM A talks to VM B which is in different compute nodes, the
         * following flows are created
         * (1) A-B, Ingress, Forward, pol1
         * (2) B-A, Egress, Reverse, pol1
         * (3) A-B, Egress, Forward, pol2
         * (4) B-A, Inress, Reverse, pol2
         * When both A and B are in single compute, we have only the following
         * flows (Flows marked as LocalFlow)
         * (1) A-B, Ingress, Forward, pol1
         * (2) B-A, Inress, Reverse, pol2
         * To simulate session stats similar to case where VMs are in different
         * computes, for local flows, we do the following.
         * (a) when "A-B, Ingress, Forward, pol1" flow is seen, we also
         *     update stats for "A-B, Egress, Forward, pol2". This is because
         *     diff stats for "A-B, Ingress, Forward, pol1" and
         *     "A-B, Egress, Forward, pol2" are same. Policy for implicit flow
         *     is picked from reverse flow
         * (b) when "B-A, Ingress, Reverse, pol2" flow is seen, we also
         *     update stats for "B-A, Egress, Reverse, pol1". This is because diff
         *     stats for "B-A, Ingress, Reverse, pol2" and
         *     "B-A, Egress, Reverse, pol1" is same. Policy for implicit flow is
         *     picked from reverse flow
         */
        ep.local_vn = src_vn;
        ep.remote_vn = dst_vn;
        ep.in_stats = true;
        bool egress_flow_is_client;
        if (flow->is_flags_set(FlowEntry::ReverseFlow)) {
            ep.client = false;
            egress_flow_is_client = true;
        } else {
            ep.client = true;
            egress_flow_is_client = false;
        }
        itf_table->UpdateVmiTagBasedStats(ep);

        /* Local flows will not have egress flows in the system. So we need to
         * explicitly build stats for egress flow using the data available from
         * ingress flow. Egress flow stats has to be updated on destination
         * VMI. We skip updation if we are unable to pick destination VMI from
         * reverse flow. */

        FlowEntry* rflow = info->reverse_flow();
        if (rflow) {
            const Interface *ritf = rflow->intf_entry();
            if (ritf && (ritf->type() == Interface::VM_INTERFACE)) {
                ep.local_tagset = flow->remote_tagset();
                ep.remote_tagset = flow->local_tagset();
                ep.local_vn = dst_vn;
                ep.remote_vn = src_vn;
                ep.policy = rflow->fw_policy_name_uuid();
                ep.client = egress_flow_is_client;
                ep.vmi = static_cast<const VmInterface *>(ritf);
                ep.in_stats = false;
                itf_table->UpdateVmiTagBasedStats(ep);
            }
        }
    } else {
        if (flow->is_flags_set(FlowEntry::IngressDir)) {
            ep.local_vn = src_vn;
            ep.remote_vn = dst_vn;
            ep.in_stats = true;
            if (flow->is_flags_set(FlowEntry::ReverseFlow)) {
                ep.client = false;
            } else {
                ep.client = true;
            }
        } else {
            ep.local_vn = dst_vn;
            ep.remote_vn = src_vn;
            ep.in_stats = false;
            if (flow->is_flags_set(FlowEntry::ReverseFlow)) {
                ep.client = true;
            } else {
                ep.client = false;
            }
        }
        itf_table->UpdateVmiTagBasedStats(ep);
    }
}

void FlowStatsCollector::UpdateInterVnStats(FlowExportInfo *info,
                                            uint64_t bytes, uint64_t pkts) {
    FlowEntry *flow = info->flow();
    string src_vn = flow->data().source_vn_match;
    string dst_vn = flow->data().dest_vn_match;
    VnUveTable *vn_table = static_cast<VnUveTable *>
        (agent_uve_->vn_uve_table());

    if (!src_vn.length())
        src_vn = FlowHandler::UnknownVn();
    if (!dst_vn.length())
        dst_vn = FlowHandler::UnknownVn();

    /* When packet is going from src_vn to dst_vn it should be interpreted
     * as ingress to vrouter and hence in-stats for src_vn w.r.t. dst_vn
     * should be incremented. Similarly when the packet is egressing vrouter
     * it should be considered as out-stats for dst_vn w.r.t. src_vn.
     * Here the direction "in" and "out" should be interpreted w.r.t vrouter
     */
    if (flow->is_flags_set(FlowEntry::LocalFlow)) {
        vn_table->UpdateInterVnStats(src_vn, dst_vn, bytes, pkts, false);
        vn_table->UpdateInterVnStats(dst_vn, src_vn, bytes, pkts, true);
    } else {
        if (flow->is_flags_set(FlowEntry::IngressDir)) {
            vn_table->UpdateInterVnStats(src_vn, dst_vn, bytes, pkts, false);
        } else {
            vn_table->UpdateInterVnStats(dst_vn, src_vn, bytes, pkts, true);
        }
    }
}

void FlowStatsCollector::UpdateFlowStats(FlowExportInfo *info,
                                         uint64_t teardown_time) {
    if (!info) {
        return;
    }
    FlowEntry *fe = info->flow();
    KSyncFlowMemory *ksync_obj = agent_uve_->agent()->ksync()->
                                         ksync_flow_memory();
    /* Fetch vrouter Flow entry using gen_id and flow_handle from FlowExportInfo
     * to account for the case where FlowEntry's flow_handle/gen_id has changed
     * during Delete processing by FlowStatsCollector */
    vr_flow_stats k_stats;
    const vr_flow_entry *k_flow = ksync_obj->GetKFlowStats(fe->key(),
                                                           info->flow_handle(),
                                                           info->gen_id(),
                                                           &k_stats);
    if (k_flow) {
        UpdateFlowStatsInternal(info, k_stats.flow_bytes,
                                k_stats.flow_bytes_oflow,
                                k_stats.flow_packets,
                                k_stats.flow_packets_oflow,
                                teardown_time, true);
        return;
    }
}

void FlowStatsCollector::FlowDeleteEnqueue(FlowExportInfo *info, uint64_t t) {
    flows_aged_++;
    FlowEntry *fe = info->flow();
    agent_uve_->agent()->pkt()->get_flow_proto()->DeleteFlowRequest(fe);
    info->set_delete_enqueue_time(t);
    FlowEntry *rflow = info->reverse_flow();
    if (rflow) {
        FlowExportInfo *rev_info = FindFlowExportInfo(rflow);
        if (rev_info) {
            rev_info->set_delete_enqueue_time(t);
        }
    }
}

void FlowStatsCollector::FlowEvictEnqueue(FlowExportInfo *info, uint64_t t,
                                          uint32_t flow_handle,
                                          uint16_t gen_id) {
    flows_evicted_++;
    FlowEntry *fe = info->flow();
    agent_uve_->agent()->pkt()->get_flow_proto()->EvictFlowRequest
        (fe, flow_handle, gen_id, (gen_id + 1));
    info->set_evict_enqueue_time(t);
}

void FlowStatsCollector::UpdateFlowStatsInternalLocked(FlowExportInfo *info,
                                                       uint32_t bytes,
                                                       uint16_t oflow_bytes,
                                                       uint32_t pkts,
                                                       uint16_t oflow_pkts,
                                                       uint64_t time,
                                                       bool teardown_time) {
    FlowEntry *flow = info->flow();
    FlowEntry *rflow = info->reverse_flow();
    FLOW_LOCK(flow, rflow, FlowEvent::FLOW_MESSAGE);
    UpdateFlowStatsInternal(info, bytes, oflow_bytes, pkts, oflow_pkts, time,
                            teardown_time);
}

void FlowStatsCollector::UpdateFlowStatsInternal(FlowExportInfo *info,
                                                 uint32_t bytes,
                                                 uint16_t oflow_bytes,
                                                 uint32_t pkts,
                                                 uint16_t oflow_pkts,
                                                 uint64_t time,
                                                 bool teardown_time) {
    uint64_t k_bytes, k_packets, total_bytes, total_packets;
    k_bytes = GetFlowStats(oflow_bytes, bytes);
    k_packets = GetFlowStats(oflow_pkts, pkts);

    total_bytes = GetUpdatedFlowBytes(info, k_bytes);
    total_packets = GetUpdatedFlowPackets(info, k_packets);
    uint64_t diff_bytes = total_bytes - info->bytes();
    uint64_t diff_pkts = total_packets - info->packets();
    info->set_bytes(total_bytes);
    info->set_packets(total_packets);
    if (teardown_time) {
        info->set_teardown_time(time);
    } else {
        info->set_last_modified_time(time);
    }

    /* In TSN mode, we don't export flows or statistics based on flows */
    if (agent_uve_->agent()->tsn_enabled()) {
        return;
    }
    //Update Inter-VN stats
    UpdateInterVnStats(info, diff_bytes, diff_pkts);
    //Update Endpoint stats
    UpdateVmiTagBasedStats(info, diff_bytes, diff_pkts);
    //Update Floating-IP stats
    UpdateFloatingIpStats(info, diff_bytes, diff_pkts);
}

// Check if flow needs to be evicted
bool FlowStatsCollector::EvictFlow(KSyncFlowMemory *ksync_obj,
                                   const vr_flow_entry *k_flow,
                                   uint16_t k_flow_flags,
                                   uint32_t flow_handle, uint16_t gen_id,
                                   FlowExportInfo *info, uint64_t curr_time) {
    FlowEntry *fe = info->flow();

    if ((fe->key().protocol != IPPROTO_TCP))
        return false;

    if (ksync_obj->IsEvictionMarked(k_flow, k_flow_flags) == false)
        return false;

    // Flow evict already enqueued? Re-Enqueue request after retry-time
    uint64_t evict_time = info->evict_enqueue_time();
    if (evict_time) {
        if ((curr_time - evict_time) > kFlowDeleteRetryTime) {
            FlowEvictEnqueue(info, curr_time, flow_handle, gen_id);
        }
    } else {
        FlowEvictEnqueue(info, curr_time, flow_handle, gen_id);
    }

    return true;
}

bool FlowStatsCollector::AgeFlow(KSyncFlowMemory *ksync_obj,
                                 const vr_flow_entry *k_flow,
                                 const vr_flow_stats &k_stats,
                                 const KFlowData& kinfo,
                                 FlowExportInfo *info, uint64_t curr_time) {
    FlowEntry *fe = info->flow();
    FlowEntry *rfe = info->reverse_flow();

    // if we come across deleted entry, retry flow deletion after some time
    // duplicate delete will be suppressed in flow_table
    uint64_t delete_time = info->delete_enqueue_time();
    if (delete_time) {
        if ((curr_time - delete_time) > kFlowDeleteRetryTime) {
            FlowDeleteEnqueue(info, curr_time);
        }
        return true;
    }

    // Delete short flows
    if ((flow_stats_manager_->delete_short_flow() == true) &&
        fe->is_flags_set(FlowEntry::ShortFlow)) {
        FlowDeleteEnqueue(info, curr_time);
        return true;
    }

    bool deleted = false;
    FlowExportInfo *rev_info = NULL;
    // Can the flow be aged?
    if (ShouldBeAged(info, k_flow, k_stats, curr_time)) {
        rev_info = FindFlowExportInfo(rfe);
        // ShouldBeAged looks at one flow only. So, check for both forward and
        // reverse flows
        if (rev_info) {
            const vr_flow_entry *k_flow_rev = NULL;
            vr_flow_stats k_rflow_stats;
            k_flow_rev = ksync_obj->GetKFlowStats(rfe->key(),
                                                  rev_info->flow_handle(),
                                                  rev_info->gen_id(),
                                                  &k_rflow_stats);
            if (ShouldBeAged(rev_info, k_flow_rev, k_rflow_stats, curr_time)) {
                deleted = true;
            }
        } else {
            deleted = true;
        }
    }

    if (deleted == true) {
        FlowDeleteEnqueue(info, curr_time);
    }

    // Update stats for flows not being deleted
    // Stats for deleted flow are updated when we get DELETE message
    if (deleted == false && k_flow) {
        uint64_t k_bytes, bytes;

        k_bytes = GetFlowStats(k_stats.flow_bytes_oflow,
                               k_stats.flow_bytes);
        bytes = 0x0000ffffffffffffULL & info->bytes();
        /* Don't account for agent overflow bits while comparing change in
         * stats */
        if (bytes != k_bytes) {
            UpdateFlowStatsInternalLocked(info,
                                          k_stats.flow_bytes,
                                          k_stats.flow_bytes_oflow,
                                          k_stats.flow_packets,
                                          k_stats.flow_packets_oflow,
                                          curr_time, false);
        }
    }
    return deleted;
}

// Check if a flow is to be aged or evicted. Returns number of flows visited
uint32_t FlowStatsCollector::ProcessFlow(FlowExportInfoList::iterator &it,
                                         KSyncFlowMemory *ksync_obj,
                                         FlowExportInfo *info,
                                         uint64_t curr_time) {
    uint32_t count = 1;
    FlowEntry *fe = info->flow();
    /* Use flow-handle and gen-id from FlowExportInfo instead of FlowEntry.
     * The stats that FlowExportInfo holds corresponds to a given
     * (FlowKey, gen-id and FlowHandle). Since gen-id/flow-handle for a flow
     * can change dynamically, we need to pick gen-id and flow-handle from
     * FlowExportInfo. Otherwise stats will go wrong. Whenever gen-id/
     * flow-handle changes, the stats will be reset as part of AddFlow API
     */
    uint32_t flow_handle = info->flow_handle();
    uint16_t gen_id = info->gen_id();

    /* If Flow handle is still not populated in FlowStatsCollector, pick the
     * value from FlowEntry
     */
    if (flow_handle == FlowEntry::kInvalidFlowHandle) {
        {
            FlowEntry *rflow = NULL;
            FLOW_LOCK(fe, rflow, FlowEvent::FLOW_MESSAGE);
            // since flow processing and stats collector can run in parallel
            // flow handle and gen id not being the key for flow entry can
            // change while processing, so flow handle and gen id should be
            // fetched by holding an lock.
            flow_handle = fe->flow_handle();
            gen_id = fe->gen_id();
            info->CopyFlowInfo(fe);
        }
    }
    const vr_flow_entry *k_flow = NULL;
    vr_flow_stats k_stats;
    KFlowData kinfo;

    /* Teardown time is set when Evicted flow stats update message is received.
     * For flows whose teardown time is set, we need not read stats from
     * vrouter
     */
    if (!info->teardown_time()) {
        k_flow = ksync_obj->GetKFlowStatsAndInfo(fe->key(), flow_handle,
                                                 gen_id, &k_stats, &kinfo);

        // Flow evicted?
        if (EvictFlow(ksync_obj, k_flow, kinfo.flags, flow_handle, gen_id,
                      info, curr_time) == true) {
            // If retry_delete_ enabled, dont change flow_export_info_list_
            if (retry_delete_ == true)
                return count;

            // We dont want to retry delete-events, remove flow from ageing list
            assert(info->is_linked());
            FlowExportInfoList::iterator flow_it =
                flow_export_info_list_.iterator_to(*info);
            flow_export_info_list_.erase(flow_it);

            return count;
        }
    }


    // Flow aged?
    if (AgeFlow(ksync_obj, k_flow, k_stats, kinfo, info, curr_time) == false)
        return count;

    // If retry_delete_ enabled, dont change flow_export_info_list_
    if (retry_delete_ == false)
        return count;

    // Flow aged, remove both forward and reverse flow
    assert(info->is_linked());
    FlowExportInfoList::iterator flow_it =
        flow_export_info_list_.iterator_to(*info);
    flow_export_info_list_.erase(flow_it);

    FlowEntry *rfe = info->reverse_flow();
    FlowExportInfo *rev_info = FindFlowExportInfo(rfe);
    if (rev_info) {
        if (rev_info->is_linked()) {
            FlowExportInfoList::iterator rev_flow_it =
                flow_export_info_list_.iterator_to(*rev_info);
            if (rev_flow_it == it) {
                it++;
            }
            flow_export_info_list_.erase(rev_flow_it);
        }
        count++;
    }
    return count;
}

uint32_t FlowStatsCollector::RunAgeing(uint32_t max_count) {
    FlowExportInfoList::iterator it;
    if (flow_iteration_key_ == NULL) {
        it = flow_export_info_list_.begin();
    } else {
        FlowEntryTree::iterator tree_it = flow_tree_.find(flow_iteration_key_);
        // Flow to iterate next is not found. Force stop this iteration.
        // We will continue from begining on next timer
        if (tree_it == flow_tree_.end()) {
            flow_iteration_key_ = NULL;
            return entries_to_visit_;
        }
        it = flow_export_info_list_.iterator_to(tree_it->second);
    }

    KSyncFlowMemory *ksync_obj = agent_uve_->agent()->ksync()->
        ksync_flow_memory();
    uint64_t curr_time = GetCurrentTime();
    uint32_t count = 0;
    while (count < max_count) {
        if (it == flow_export_info_list_.end()) {
            break;
        }

        FlowExportInfo *info = &(*it);
        it++;
        flows_visited_++;
        count += ProcessFlow(it, ksync_obj, info, curr_time);
    }

    // Update iterator for next pass
    if (it == flow_export_info_list_.end()) {
        flow_iteration_key_ = NULL;
    } else {
        flow_iteration_key_ = it->flow();
    }

    return count;
}

// Timer fired for ageing. Update the number of entries to visit and start the
// task if its already not ruuning
bool FlowStatsCollector::Run() {
    if (flow_tree_.size() == 0) {
        return true;
     }

    // Update number of entries to visit in flow.
    UpdateEntriesToVisit();

    // Start task to scan the entries
    if (ageing_task_ == NULL) {
        ageing_task_starts_++;

        if (flow_ageing_debug_) {
            LOG(DEBUG,
                UTCUsecToString(ClockMonotonicUsec())
                << " AgeingTasks Num " << ageing_task_starts_
                << " Request count " << request_queue_.Length()
                << " Tree size " << flow_tree_.size()
                << " List size " << flow_export_info_list_.size()
                << " flows visited " << flows_visited_
                << " flows aged " << flows_aged_
                << " flows evicted " << flows_evicted_);
        }
        flows_visited_ = 0;
        flows_aged_ = 0;
        flows_evicted_ = 0;
        ageing_task_ = new AgeingTask(this);
        agent_uve_->agent()->task_scheduler()->Enqueue(ageing_task_);
    }
    return true;
}

bool FlowStatsCollector::RunAgeingTask() {
    // Run ageing per task
    uint32_t count = RunAgeing(kFlowsPerTask);
    // Update number of entries visited
    if (count < entries_to_visit_)
        entries_to_visit_ -= count;
    else
        entries_to_visit_ = 0;
    // Done with task if we reach end of tree or count is exceeded
    if (flow_iteration_key_ == NULL || entries_to_visit_ == 0) {
        entries_to_visit_ = 0;
        ageing_task_ = NULL;
        return true;
    }

    // More entries to visit. Continue the task
    return false;
}

/////////////////////////////////////////////////////////////////////////////
// Utility methods to enqueue events into work-queue
/////////////////////////////////////////////////////////////////////////////
void FlowStatsCollector::AddEvent(const FlowEntryPtr &flow) {
    FlowExportInfo info(flow, GetCurrentTime());
    boost::shared_ptr<FlowExportReq>
        req(new FlowExportReq(FlowExportReq::ADD_FLOW, info));
    request_queue_.Enqueue(req);
}

void FlowStatsCollector::DeleteEvent(const FlowEntryPtr &flow,
                                     const RevFlowDepParams &params) {
    FlowExportInfo info(flow);
    boost::shared_ptr<FlowExportReq>
        req(new FlowExportReq(FlowExportReq::DELETE_FLOW, info,
                              GetCurrentTime(), params));
    request_queue_.Enqueue(req);
}

void FlowStatsCollector::UpdateStatsEvent(const FlowEntryPtr &flow,
                                          uint32_t bytes,
                                          uint32_t packets,
                                          uint32_t oflow_bytes,
                                          const boost::uuids::uuid &u) {
    FlowExportInfo info(flow);
    boost::shared_ptr<FlowExportReq>
        req(new FlowExportReq(FlowExportReq::UPDATE_FLOW_STATS, info, bytes,
                              packets, oflow_bytes, u));
    request_queue_.Enqueue(req);
}

bool FlowStatsCollector::RequestHandlerEntry() {
    current_time_ = GetCurrentTime();
    return true;
}

void FlowStatsCollector::RequestHandlerExit(bool done) {
}

bool FlowStatsCollector::RequestHandler(boost::shared_ptr<FlowExportReq> req) {
    const FlowExportInfo &info = req->info();
    FlowEntry *flow = info.flow();
    FlowEntry *rflow = info.reverse_flow();
    FLOW_LOCK(flow, rflow, FlowEvent::FLOW_MESSAGE);

    switch (req->event()) {
    case FlowExportReq::ADD_FLOW: {
        AddFlow(req->info());
        break;
    }

    case FlowExportReq::DELETE_FLOW: {
        FlowEntryTree::iterator it;
        // Get the FlowExportInfo for flow
        if (FindFlowExportInfo(flow, it) == false)
            break;

        /* We don't export flows in TSN mode */
        if (agent_uve_->agent()->tsn_enabled() == false) {
            FlowExportInfo *info = &it->second;
            /* While updating stats for evicted flows, we set the teardown_time
             * and export the flow. So delete handling for evicted flows need
             * not update stats and export flow */
            if (!info->teardown_time()) {
                UpdateFlowStats(info, req->time());
            }
        }
        /* Remove the entry from our tree */
        DeleteFlow(it);
        break;
    }

    case FlowExportReq::UPDATE_FLOW_STATS: {
        /* We don't export flows in TSN mode */
        if (agent_uve_->agent()->tsn_enabled() == false) {
            EvictedFlowStatsUpdate(flow, req->bytes(), req->packets(),
                                   req->oflow_bytes(), req->uuid());
        }
        break;
    }

    default:
         assert(0);
    }

    if (deleted_ && parent_->CanDelete()) {
        flow_stats_manager_->Free(flow_aging_key_);
    }

    return true;
}

FlowExportInfo *
FlowStatsCollector::FindFlowExportInfo(const FlowEntry *fe) {
    FlowEntryTree::iterator it = flow_tree_.find(fe);
    if (it == flow_tree_.end()) {
        return NULL;
    }

    return &it->second;
}

const FlowExportInfo *
FlowStatsCollector::FindFlowExportInfo(const FlowEntry *fe) const {
    FlowEntryTree::const_iterator it = flow_tree_.find(fe);
    if (it == flow_tree_.end()) {
        return NULL;
    }

    return &it->second;
}

bool FlowStatsCollector::FindFlowExportInfo(const FlowEntry *fe,
                                            FlowEntryTree::iterator &it) {
    it = flow_tree_.find(fe);
    if (it == flow_tree_.end()) {
        return false;
    }
    return true;
}

void FlowStatsCollector::NewFlow(FlowEntry *flow) {
    /* In TSN mode, we don't export flows or statistics based on flows */
    if (agent_uve_->agent()->tsn_enabled()) {
        return;
    }
    const FlowKey &key = flow->key();
    uint8_t proto = key.protocol;
    uint16_t sport = key.src_port;
    uint16_t dport = key.dst_port;

    // Update vrouter port bitmap
    VrouterUveEntry *vre = static_cast<VrouterUveEntry *>(
        agent_uve_->vrouter_uve_entry());
    vre->UpdateBitmap(proto, sport, dport);

    // Update source-vn port bitmap
    VnUveTable *vnte = static_cast<VnUveTable *>(agent_uve_->vn_uve_table());
    vnte->UpdateBitmap(flow->data().source_vn_match, proto, sport, dport);
    // Update dest-vn port bitmap
    vnte->UpdateBitmap(flow->data().dest_vn_match, proto, sport, dport);

    const VmInterface *port = dynamic_cast<const VmInterface *>
        (flow->intf_entry());
    if (port == NULL) {
        return;
    }
    const VmEntry *vm = port->vm();
    if (vm == NULL) {
        return;
    }

    // update vm and interface (all interfaces of vm) bitmap
    VmUveTable *vmt = static_cast<VmUveTable *>(agent_uve_->vm_uve_table());
    vmt->UpdateBitmap(vm, proto, sport, dport);
}

void FlowStatsCollector::AddFlow(FlowExportInfo info) {
    /* Before inserting update the gen_id and flow_handle in FlowExportInfo.
     * Locks for accessing fields of flow are taken in calling function.
     */
    FlowEntry* fe = info.flow();
    info.CopyFlowInfo(fe);
    std::pair<FlowEntryTree::iterator, bool> ret =
        flow_tree_.insert(make_pair(fe, info));
    if (ret.second == false) {
        FlowExportInfo &prev = ret.first->second;
        if (prev.uuid() != fe->uuid()) {
            /* Received ADD request for already added entry with a different
             * UUID. Because of state-compression of messages to
             * FlowStatsCollector in FlowMgmt, we have not received DELETE for
             * previous UUID. Send FlowExport to indicate delete for the flow.
             * This export need not be sent if teardown time is already set.
             * Teardown time would be set if EvictedFlowStats update request
             * comes before this duplicate add.
             */
            if (!prev.teardown_time()) {
                UpdateFlowStats(&prev, info.last_modified_time());
            }
            /* After sending Delete to collector (if required), reset the stats
             */
            prev.ResetStats();
        }
        prev.CopyFlowInfo(fe);
        prev.set_delete_enqueue_time(0);
        prev.set_evict_enqueue_time(0);
        prev.set_teardown_time(0);
    } else {
        NewFlow(info.flow());
    }
    if (ret.first->second.is_linked() == false) {
        flow_export_info_list_.push_back(ret.first->second);
    }
}

// The flow being deleted may be the first flow to visit in next ageing
// iteration. Update the flow to visit next in such case
void FlowStatsCollector::UpdateFlowIterationKey
(const FlowEntry *del_flow, FlowEntryTree::iterator &tree_it) {
    // Flow not found in tree is not a valid scenario. Lets be safe and
    // restart walk here
    if (tree_it == flow_tree_.end()) {
        flow_iteration_key_ = NULL;
    }

    if (flow_iteration_key_ == NULL) {
        return;
    }

    // The flow to visit next for ageing is being deleted. Update next flow to
    // visit
    FlowExportInfoList::iterator it =
        flow_export_info_list_.iterator_to(tree_it->second);
    ++it;

    // If this is end of list, start from begining again
    if (it == flow_export_info_list_.end())
        it = flow_export_info_list_.begin();

    if (it == flow_export_info_list_.end()) {
        flow_iteration_key_ = NULL;
    } else {
        flow_iteration_key_ = it->flow();
    }
}

void FlowStatsCollector::DeleteFlow(FlowEntryTree::iterator &it) {
    // Update flow_iteration_key_ if flow being deleted is flow to visit in
    // next ageing cycle
    // Nothing to do if flow being deleted is not the next-iteration key
    if (it->first == flow_iteration_key_) {
        UpdateFlowIterationKey(it->first, it);
    }

    if (it == flow_tree_.end())
        return;

    if (it->second.is_linked()) {
        FlowExportInfoList::iterator it1 =
            flow_export_info_list_.iterator_to(it->second);
        flow_export_info_list_.erase(it1);
    }

    flow_tree_.erase(it);
}

void FlowStatsCollector::EvictedFlowStatsUpdate(const FlowEntryPtr &flow,
                                                uint32_t bytes,
                                                uint32_t packets,
                                                uint32_t oflow_bytes,
                                                const boost::uuids::uuid &u) {
    FlowExportInfo *info = FindFlowExportInfo(flow.get());
    if (info) {
        /* Ignore stats update request for Evicted flow, if we don't have
         * FlowEntry corresponding to the Evicted Flow. The match is done using
         * UUID
         */
        if (info->uuid() != u) {
            return;
        }
        /* We are updating stats of evicted flow. Set teardown_time here.
         * When delete event is being handled we don't export flow if
         * teardown time is set */
        UpdateFlowStatsInternal(info, bytes, oflow_bytes & 0xFFFF,
                                packets, oflow_bytes & 0xFFFF0000,
                                GetCurrentTime(), true);
    }
}

/////////////////////////////////////////////////////////////////////////////
// Introspect routines
/////////////////////////////////////////////////////////////////////////////
static void KeyToSandeshFlowKey(const FlowKey &key,
                                SandeshFlowKey &skey) {
    skey.set_nh(key.nh);
    skey.set_sip(key.src_addr.to_string());
    skey.set_dip(key.dst_addr.to_string());
    skey.set_src_port(key.src_port);
    skey.set_dst_port(key.dst_port);
    skey.set_protocol(key.protocol);
}

static void FlowExportInfoToSandesh(const FlowExportInfo &value,
                                    SandeshFlowExportInfo &info) {
    SandeshFlowKey skey;
    FlowEntry *flow = value.flow();
    FlowEntry *rflow = value.reverse_flow();
    KeyToSandeshFlowKey(flow->key(), skey);
    info.set_key(skey);
    info.set_uuid(to_string(flow->uuid()));
    if (rflow) {
        info.set_rev_flow_uuid(to_string(rflow->uuid()));
    }
    if (!flow->data().origin_vn_src.empty()) {
        info.set_source_vn(flow->data().origin_vn_src);
    } else {
        info.set_source_vn(flow->data().source_vn_match);
    }
    if (!flow->data().origin_vn_dst.empty()) {
        info.set_dest_vn(flow->data().origin_vn_dst);
    } else {
        info.set_dest_vn(flow->data().dest_vn_match);
    }
    info.set_sg_rule_uuid(flow->sg_rule_uuid());
    info.set_nw_ace_uuid(flow->nw_ace_uuid());
    info.set_teardown_time(value.teardown_time());
    info.set_last_modified_time(value.last_modified_time());
    info.set_bytes(value.bytes());
    info.set_packets(value.packets());
    info.set_flow_handle(flow->flow_handle());
    std::vector<ActionStr> action_str_l;
    SetActionStr(flow->data().match_p.action_info, action_str_l);
    info.set_action(action_str_l);
    info.set_vm_cfg_name(flow->data().vm_cfg_name);
    info.set_peer_vrouter(flow->peer_vrouter());
    info.set_tunnel_type(flow->tunnel_type().ToString());
    const VmInterfaceKey &vmi = flow->fip_vmi();
    string vmi_str = to_string(vmi.uuid_) + vmi.name_;
    info.set_fip_vmi(vmi_str);
    Ip4Address ip(flow->fip());
    info.set_fip(ip.to_string());
    info.set_delete_enqueued(value.delete_enqueue_time() ? true : false);
}

void FlowStatsRecordsReq::HandleRequest() const {
    FlowStatsCollector::FlowEntryTree::iterator it;
    vector<FlowStatsRecord> list;
    FlowStatsRecordsResp *resp = new FlowStatsRecordsResp();
    for (int i = 0; i < FlowStatsCollectorObject::kMaxCollectors; i++) {
        FlowStatsCollector *col = Agent::GetInstance()->
            flow_stats_manager()->default_flow_stats_collector_obj()->
            GetCollector(i);
        it = col->flow_tree_.begin();
        while (it != col->flow_tree_.end()) {
            const FlowExportInfo &value = it->second;
            ++it;

            SandeshFlowKey skey;
            KeyToSandeshFlowKey(value.flow()->key(), skey);

            SandeshFlowExportInfo info;
            FlowExportInfoToSandesh(value, info);

            FlowStatsRecord rec;
            rec.set_info(info);
            list.push_back(rec);
        }
    }
    resp->set_records_list(list);

    resp->set_context(context());
    resp->Response();
    return;
}

/////////////////////////////////////////////////////////////////////////////
// Flow Stats Ageing task
/////////////////////////////////////////////////////////////////////////////
FlowStatsCollector::AgeingTask::AgeingTask(FlowStatsCollector *fsc) :
    Task(fsc->task_id(), fsc->instance_id()), fsc_(fsc) {
}

FlowStatsCollector::AgeingTask::~AgeingTask() {
}

std::string FlowStatsCollector::AgeingTask::Description() const {
    return "Flow Stats Collector Ageing Task";
}

bool FlowStatsCollector::AgeingTask::Run() {
    return fsc_->RunAgeingTask();
}

/////////////////////////////////////////////////////////////////////////////
// FlowStatsCollectorObject methods
/////////////////////////////////////////////////////////////////////////////
FlowStatsCollectorObject::FlowStatsCollectorObject(Agent *agent,
                                                   FlowStatsCollectorReq *req,
                                                   FlowStatsManager *mgr) {
    FlowAgingTableKey *key = &(req->key);
    for (int i = 0; i < kMaxCollectors; i++) {
        uint32_t instance_id = mgr->AllocateIndex();
        collectors[i].reset(
            AgentObjectFactory::Create<FlowStatsCollector>(
                *(agent->event_manager()->io_service()),
                req->flow_stats_interval, req->flow_cache_timeout,
                agent->uve(), instance_id, key, mgr, this));
    }
}

FlowStatsCollector* FlowStatsCollectorObject::GetCollector(uint8_t idx) const {
    if (idx >= 0 && idx < kMaxCollectors) {
        return collectors[idx].get();
    }
    return NULL;
}

void FlowStatsCollectorObject::SetExpiryTime(int time) {
    for (int i = 0; i < kMaxCollectors; i++) {
        collectors[i]->set_expiry_time(time);
    }
}

int FlowStatsCollectorObject::GetExpiryTime() const {
    /* Same expiry time would be configured for all the collectors. Pick value
     * from any one of them */
    return collectors[0]->expiry_time();
}

void FlowStatsCollectorObject::MarkDelete() {
    for (int i = 0; i < kMaxCollectors; i++) {
        collectors[i]->set_deleted(true);
    }
}

void FlowStatsCollectorObject::ClearDelete() {
    for (int i = 0; i < kMaxCollectors; i++) {
        collectors[i]->set_deleted(false);
    }
}

bool FlowStatsCollectorObject::IsDeleted() const {
    for (int i = 0; i < kMaxCollectors; i++) {
        if (!collectors[i]->deleted()) {
            return false;
        }
    }
    return true;
}

void FlowStatsCollectorObject::SetFlowAgeTime(uint64_t value) {
    for (int i = 0; i < kMaxCollectors; i++) {
        collectors[i]->set_flow_age_time_intvl(value);
    }
}

uint64_t FlowStatsCollectorObject::GetFlowAgeTime() const {
    /* Same age time would be configured for all the collectors. Pick value
     * from any one of them */
    return collectors[0]->flow_age_time_intvl();
}

bool FlowStatsCollectorObject::CanDelete() const {
    for (int i = 0; i < kMaxCollectors; i++) {
        if (collectors[i]->flow_tree_.size() != 0 ||
            collectors[i]->request_queue_.IsQueueEmpty() == false) {
            return false;
        }
    }
    return true;
}

void FlowStatsCollectorObject::Shutdown() {
    for (int i = 0; i < kMaxCollectors; i++) {
        collectors[i]->Shutdown();
        collectors[i].reset();
    }
}

FlowStatsCollector* FlowStatsCollectorObject::FlowToCollector
    (const FlowEntry *flow) {
    uint8_t idx = 0;
    FlowTable *table = flow->flow_table();
    if (table) {
        idx = table->table_index() % kMaxCollectors;
    }
    return collectors[idx].get();
}

void FlowStatsCollectorObject::UpdateAgeTimeInSeconds(uint32_t age_time) {
    for (int i = 0; i < kMaxCollectors; i++) {
        collectors[i]->UpdateFlowAgeTimeInSecs(age_time);
    }
}

uint32_t FlowStatsCollectorObject::GetAgeTimeInSeconds() const {
    /* Same age time would be configured for all the collectors. Pick value
     * from any one of them */
    return collectors[0]->flow_age_time_intvl_in_secs();
}

size_t FlowStatsCollectorObject::Size() const {
    size_t size = 0;
    for (int i = 0; i < kMaxCollectors; i++) {
        size += collectors[i]->Size();
    }
    return size;
}
