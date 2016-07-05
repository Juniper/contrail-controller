/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <bitset>
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

FlowStatsCollector::FlowStatsCollector(boost::asio::io_service &io, int intvl,
                                       uint32_t flow_cache_timeout,
                                       AgentUveBase *uve,
                                       uint32_t instance_id,
                                       FlowAgingTableKey *key,
                                       FlowStatsManager *aging_module) :
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
        request_queue_(agent_uve_->agent()->task_scheduler()->
                       GetTaskId(kTaskFlowStatsCollector),
                       instance_id,
                       boost::bind(&FlowStatsCollector::RequestHandler, 
                                   this, _1)),
        msg_list_(kMaxFlowMsgsPerSend, FlowLogData()), msg_index_(0),
        flow_aging_key_(*key), instance_id_(instance_id),
        flow_stats_manager_(aging_module), ageing_task_(NULL) {
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
        // Aging timer fires every kFlowStatsTimerInterval msec. Compute
        // number of timer fires needed to scan complete table
        timers_per_scan_ = TimersPerScan();
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
    uint32_t entries = flow_tree_.size() / timers_per_scan_;

    // Update number of entries to visit in flow.
    // The scan for previous timer may still be in progress. So, accmulate
    // number of entries to visit
    entries_to_visit_ += entries;

    // Cap number of entries to visit to 25% of table
    if (entries_to_visit_ > ((flow_tree_.size() * kFlowScanTime)/100))
        entries_to_visit_ = (flow_tree_.size() * kFlowScanTime)/100;

    // Apply lower-limit
    if (entries_to_visit_ < kMinFlowsPerTimer)
        entries_to_visit_ = kMinFlowsPerTimer;

    return;
}

bool FlowStatsCollector::ShouldBeAged(FlowExportInfo *info,
                                      const vr_flow_entry *k_flow,
                                      uint64_t curr_time) {
    FlowEntry *flow = info->flow();
    //If both forward and reverse flow are marked
    //as TCP closed then immediately remote the flow
    if (k_flow != NULL) {
        uint64_t k_flow_bytes, bytes;
        k_flow_bytes = GetFlowStats(k_flow->fe_stats.flow_bytes_oflow,
                                    k_flow->fe_stats.flow_bytes);
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
    if (!fe->fip() || fe->fip_vmi().uuid_ == nil_uuid()) {
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
    return VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, nil_uuid(), "");
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

void FlowStatsCollector::UpdateStatsAndExportFlow(FlowExportInfo *info,
                                                  uint64_t teardown_time,
                                                  const RevFlowDepParams *p) {
    KSyncFlowMemory *ksync_obj = agent_uve_->agent()->ksync()->
                                         ksync_flow_memory();
    if (!info) {
        return;
    }
    FlowEntry *fe = info->flow();
    const vr_flow_entry *k_flow = ksync_obj->GetValidKFlowEntry(fe->key(),
                                                            fe->flow_handle(),
                                                            fe->gen_id());
    if (k_flow) {
        UpdateAndExportInternal(info, k_flow->fe_stats.flow_bytes,
                                k_flow->fe_stats.flow_bytes_oflow,
                                k_flow->fe_stats.flow_packets,
                                k_flow->fe_stats.flow_packets_oflow,
                                teardown_time, true, p);
        return;
    }
    /* If reading of stats fails, send a message with just teardown time */
    info->set_teardown_time(teardown_time);
    ExportFlow(info, 0, 0, p);
}

void FlowStatsCollector::FlowDeleteEnqueue(FlowExportInfo *info, uint64_t t) {
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

void FlowStatsCollector::FlowEvictEnqueue(FlowExportInfo *info, uint64_t t) {
    FlowEntry *fe = info->flow();
    agent_uve_->agent()->pkt()->get_flow_proto()->EvictFlowRequest
        (fe, fe->flow_handle(), fe->gen_id(), (fe->gen_id() + 1));
    info->set_evict_enqueue_time(t);
}

void FlowStatsCollector::UpdateFlowStatsInternal(FlowExportInfo *info,
                                                 uint32_t bytes,
                                                 uint16_t oflow_bytes,
                                                 uint32_t pkts,
                                                 uint16_t oflow_pkts,
                                                 uint64_t time,
                                                 bool teardown_time,
                                                 uint64_t *diff_bytes,
                                                 uint64_t *diff_pkts) {
    uint64_t k_bytes, k_packets, total_bytes, total_packets;
    k_bytes = GetFlowStats(oflow_bytes, bytes);
    k_packets = GetFlowStats(oflow_pkts, pkts);

    total_bytes = GetUpdatedFlowBytes(info, k_bytes);
    total_packets = GetUpdatedFlowPackets(info, k_packets);
    *diff_bytes = total_bytes - info->bytes();
    *diff_pkts = total_packets - info->packets();
    info->set_bytes(total_bytes);
    info->set_packets(total_packets);

    //Update Inter-VN stats
    UpdateInterVnStats(info, *diff_bytes, *diff_pkts);
    //Update Floating-IP stats
    UpdateFloatingIpStats(info, *diff_bytes, *diff_pkts);
    if (teardown_time) {
        info->set_teardown_time(time);
    } else {
        info->set_last_modified_time(time);
    }
}

void FlowStatsCollector::UpdateAndExportInternalLocked(FlowExportInfo *info,
                                                       uint32_t bytes,
                                                       uint16_t oflow_bytes,
                                                       uint32_t pkts,
                                                       uint16_t oflow_pkts,
                                                       uint64_t time,
                                                       bool teardown_time,
                                                   const RevFlowDepParams *p) {
    FlowEntry *flow = info->flow();
    FlowEntry *rflow = info->reverse_flow();
    FLOW_LOCK(flow, rflow, FlowEvent::FLOW_MESSAGE);
    UpdateAndExportInternal(info, bytes, oflow_bytes, pkts, oflow_pkts, time,
                            teardown_time, p);
}

void FlowStatsCollector::UpdateAndExportInternal(FlowExportInfo *info,
                                                 uint32_t bytes,
                                                 uint16_t oflow_bytes,
                                                 uint32_t pkts,
                                                 uint16_t oflow_pkts,
                                                 uint64_t time,
                                                 bool teardown_time,
                                                 const RevFlowDepParams *p) {
    uint64_t diff_bytes, diff_pkts;
    UpdateFlowStatsInternal(info, bytes, oflow_bytes, pkts, oflow_pkts, time,
                            teardown_time, &diff_bytes, &diff_pkts);
    ExportFlow(info, diff_bytes, diff_pkts, p);
}

// Scan for max_count entries in flow-table
uint32_t FlowStatsCollector::RunAgeing(uint32_t max_count) {
    FlowEntryTree::iterator it = flow_tree_.lower_bound(flow_iteration_key_);
    if (it == flow_tree_.end()) {
        it = flow_tree_.begin();
    }
    KSyncFlowMemory *ksync_obj = agent_uve_->agent()->ksync()->
                                         ksync_flow_memory();
    uint32_t count = 0;
    uint64_t curr_time = GetCurrentTime();
    while (it != flow_tree_.end() && count < max_count) {
        FlowExportInfo *info = NULL;
        info = &it->second;
        FlowEntry *fe = info->flow();
        FlowEntry *rfe = info->reverse_flow();
        it++;

        // if we come across deleted entry, retry flow deletion after some time
        // duplicate delete will be suppressed in flow_table
        uint64_t delete_time = info->delete_enqueue_time();
        if (delete_time) {
            if ((curr_time - delete_time) > kFlowDeleteRetryTime) {
                FlowDeleteEnqueue(info, curr_time);
                count++;
            }
            continue;
        }

        count++;
        const vr_flow_entry *k_flow = ksync_obj->GetValidKFlowEntry
            (fe->key(), fe->flow_handle(), fe->gen_id());

        if ((fe->key().protocol == IPPROTO_TCP) &&
            ksync_obj->IsEvictionMarked(k_flow)) {
            uint64_t evict_time = info->evict_enqueue_time();
            if (evict_time) {
                if ((curr_time - evict_time) > kFlowDeleteRetryTime) {
                    FlowEvictEnqueue(info, curr_time);
                }
                continue;
            }
            FlowEvictEnqueue(info, curr_time);
            continue;
        }

        FlowExportInfo *rev_info = NULL;
        // Delete short flows
        if ((flow_stats_manager_->delete_short_flow() == true) &&
            fe->is_flags_set(FlowEntry::ShortFlow)) {
            rev_info = FindFlowExportInfo(rfe);
            FlowDeleteEnqueue(info, curr_time);
            if (rev_info) {
                count++;
            }
            continue;
        }

        bool deleted = false;
        // Can the flow be aged?
        if (ShouldBeAged(info, k_flow, curr_time)) {
            rev_info = FindFlowExportInfo(rfe);
            // ShouldBeAged looks at one flow only. So, check for both forward and
            // reverse flows
            if (rev_info) {
                const vr_flow_entry *k_flow_rev;
                k_flow_rev = ksync_obj->GetValidKFlowEntry
                    (rfe->key(), rfe->flow_handle(), rfe->gen_id());
                if (ShouldBeAged(rev_info, k_flow_rev, curr_time)) {
                    deleted = true;
                }
            } else {
                deleted = true;
            }
        }

        if (deleted == true) {
            FlowDeleteEnqueue(info, curr_time);
            // We delete both forward and reverse flows. So, account for
            // reverse flow also
            if (rev_info) {
                count++;
            }
        }

        // Update stats for flows not being deleted
        // Stats for deleted flow are updated when we get DELETE message
        if (deleted == false && k_flow) {
            uint64_t k_bytes, bytes;
            /* Copy full stats in one shot and use local copy instead of reading
             * individual stats from shared memory directly to minimize the
             * inconsistency */
            struct vr_flow_stats fe_stats = k_flow->fe_stats;

            k_bytes = GetFlowStats(fe_stats.flow_bytes_oflow,
                                   fe_stats.flow_bytes);
            bytes = 0x0000ffffffffffffULL & info->bytes();
            /* Always copy udp source port even though vrouter does not change
             * it. Vrouter many change this behavior and recompute source port
             * whenever flow action changes. To keep agent independent of this,
             * always copy UDP source port */
            info->set_underlay_source_port(k_flow->fe_udp_src_port);
            info->set_tcp_flags(k_flow->fe_tcp_flags);
            /* Don't account for agent overflow bits while comparing change in
             * stats */
            if (bytes != k_bytes) {
                UpdateAndExportInternalLocked(info,
                                        fe_stats.flow_bytes,
                                        fe_stats.flow_bytes_oflow,
                                        fe_stats.flow_packets,
                                        fe_stats.flow_packets_oflow,
                                        curr_time, false, NULL);
            } else if (info->changed()) {
                /* export flow (reverse) for which traffic is not seen yet. */
                ExportFlowLocked(info, 0, 0, NULL);
            }
        }
    }

    //Send any pending flow export messages
    DispatchPendingFlowMsg();

    // Update iterator for next pass
    if (it == flow_tree_.end()) {
        flow_iteration_key_ = NULL;
    } else {
        flow_iteration_key_ = it->first;
    }

    return count;
}

// Timer fired for ageing. Update the number of entries to visit and start the
// task if its already not ruuning
bool FlowStatsCollector::Run() {
    run_counter_++;
    if (flow_tree_.size() == 0) {
        return true;
     }

    // Update number of entries to visit in flow.
    UpdateEntriesToVisit();

    // Start task to scan the entries
    if (ageing_task_ == NULL) {
        ageing_task_ = new AgeingTask(this);
        agent_uve_->agent()->task_scheduler()->Enqueue(ageing_task_);
    }
    return true;
}
  
// Called on runnig of a task
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
                                          uint32_t oflow_bytes) {
    FlowExportInfo info(flow);
    boost::shared_ptr<FlowExportReq>
        req(new FlowExportReq(FlowExportReq::UPDATE_FLOW_STATS, info, bytes,
                              packets, oflow_bytes));
    request_queue_.Enqueue(req);
}

void FlowStatsCollector::SetUnderlayInfo(FlowExportInfo *info,
                                         FlowLogData &s_flow) {
    string rid = agent_uve_->agent()->router_id().to_string();
    FlowEntry *flow = info->flow();
    uint16_t underlay_src_port = 0;
    if (flow->is_flags_set(FlowEntry::LocalFlow)) {
        s_flow.set_vrouter_ip(rid);
        s_flow.set_other_vrouter_ip(rid);
        /* Set source_port as 0 for local flows. Source port is calculated by
         * vrouter irrespective of whether flow is local or not. So for local
         * flows we need to ignore port given by vrouter
         */
        s_flow.set_underlay_source_port(0);
    } else {
        s_flow.set_vrouter_ip(rid);
        s_flow.set_other_vrouter_ip(flow->peer_vrouter());
        if (flow->tunnel_type().GetType() != TunnelType::MPLS_GRE) {
            underlay_src_port = info->underlay_source_port();
        }
        s_flow.set_underlay_source_port(underlay_src_port);
    }
    s_flow.set_underlay_proto(flow->tunnel_type().GetType());
}

/* For ingress flows, change the SIP as Nat-IP instead of Native IP */
void FlowStatsCollector::SourceIpOverride(FlowExportInfo *info,
                                          FlowLogData &s_flow,
                                          const RevFlowDepParams *params) {
    if (params && params->sip_.is_v4()) {
        s_flow.set_sourceip(params->sip_);
        return;
    }
    FlowEntry *flow = info->flow();
    FlowEntry *rflow = info->reverse_flow();
    if (flow->is_flags_set(FlowEntry::NatFlow) && s_flow.get_direction_ing() &&
        rflow) {
        const FlowKey *nat_key = &rflow->key();
        if (flow->key().src_addr != nat_key->dst_addr) {
            s_flow.set_sourceip(nat_key->dst_addr);
        }
    }
}

void FlowStatsCollector::SetImplicitFlowDetails(FlowExportInfo *info,
                                                FlowLogData &s_flow,
                                                const RevFlowDepParams *params) {

    FlowEntry *rflow = info->reverse_flow();

    if (rflow) {
        s_flow.set_flowuuid(to_string(rflow->egress_uuid()));
        s_flow.set_vm(rflow->data().vm_cfg_name);
        s_flow.set_sg_rule_uuid(rflow->sg_rule_uuid());
        if (rflow->intf_entry()) {
            s_flow.set_vmi_uuid(UuidToString(rflow->intf_entry()->GetUuid()));
        }
        s_flow.set_reverse_uuid(to_string(rflow->uuid()));
    } else if (params) {
        s_flow.set_flowuuid(to_string(params->rev_egress_uuid_));
        s_flow.set_vm(params->vm_cfg_name_);
        s_flow.set_sg_rule_uuid(params->sg_uuid_);
        s_flow.set_reverse_uuid(to_string(params->rev_uuid_));
        s_flow.set_vmi_uuid(params->vmi_uuid_);
    }
}

void FlowStatsCollector::GetFlowSandeshActionParams
    (const FlowAction &action_info, std::string &action_str) {
    std::bitset<32> bs(action_info.action);
    for (unsigned int i = 0; i <= bs.size(); i++) {
        if (bs[i]) {
            if (!action_str.empty()) {
                action_str += "|";
            }
            action_str += TrafficAction::ActionToString(
                static_cast<TrafficAction::Action>(i));
        }
    }
}

void FlowStatsCollector::EnqueueFlowMsg() {
    msg_index_++;
    if (msg_index_ == kMaxFlowMsgsPerSend) {
        DispatchFlowMsg(msg_list_);
        msg_index_ = 0;
    }
}

void FlowStatsCollector::DispatchPendingFlowMsg() {
    if (msg_index_ == 0) {
        return;
    }

    vector<FlowLogData>::const_iterator first = msg_list_.begin();
    vector<FlowLogData>::const_iterator last = msg_list_.begin() + msg_index_;
    vector<FlowLogData> new_list(first, last);
    DispatchFlowMsg(new_list);
    msg_index_ = 0;
}

void FlowStatsCollector::DispatchFlowMsg(const std::vector<FlowLogData> &lst) {
    FLOW_LOG_DATA_OBJECT_LOG("", SandeshLevel::SYS_INFO, lst);
}

uint8_t FlowStatsCollector::GetFlowMsgIdx() {
    FlowLogData &obj = msg_list_[msg_index_];
    obj = FlowLogData();
    return msg_index_;
}

void FlowStatsCollector::ExportFlowLocked(FlowExportInfo *info,
                                          uint64_t diff_bytes,
                                          uint64_t diff_pkts,
                                          const RevFlowDepParams *params) {
    FlowEntry *flow = info->flow();
    FlowEntry *rflow = info->reverse_flow();
    FLOW_LOCK(flow, rflow, FlowEvent::FLOW_MESSAGE);
    ExportFlow(info, diff_bytes, diff_pkts, params);
}

/* Flow Export Algorithm
 * (1) Flow samples greater than or equal to sampling threshold will always be
 * exported, with the byte/packet counts reported as-is.
 * (2) Flow samples smaller than the sampling threshold will be exported
 * probabilistically, with the byte/packets counts adjusted upwards according to
 * the probability.
 * (3) Probability =  diff_bytes/sampling_threshold
 * (4) We generate a random number less than sampling threshold.
 * (5) If the diff_bytes is greater than random number then the flow is dropped
 * (6) Otherwise the flow is exported after normalizing the diff bytes and
 * packets. The normalization is done by dividing diff_bytes and diff_pkts with
 * probability. This normalization is used as heuristictic to account for stats
 * of dropped flows */
void FlowStatsCollector::ExportFlow(FlowExportInfo *info,
                                    uint64_t diff_bytes,
                                    uint64_t diff_pkts,
                                    const RevFlowDepParams *params) {
    assert((agent_uve_->agent()->tsn_enabled() == false));
    FlowEntry *flow = info->flow();
    FlowEntry *rflow = info->reverse_flow();

    int32_t cfg_rate = agent_uve_->agent()->oper_db()->global_vrouter()->
                        flow_export_rate();
    /* We should always try to export flows with Action as LOG regardless of
     * configured flow-export-rate */
    if (!info->IsActionLog() && !cfg_rate) {
        flow_stats_manager_->flow_export_disable_drops_++;
        return;
    }

    /* Compute diff stats by adding the previous diff stats of sample that
     * was dropped */
    diff_bytes += info->prev_diff_bytes();
    diff_pkts += info->prev_diff_packets();

    /* Subject a flow to sampling algorithm only when all of below is met:-
     * a. if Log is not configured as action for flow
     * b. actual flow-export-rate is >= 80% of configured flow-export-rate
     * c. diff_bytes is lesser than the threshold
     * d. Flow-sampling is not disabled
     * e. Flow-sample does not have teardown time or the sample for the flow is
     *    not exported earlier.
     */
    bool subject_flows_to_algorithm = false;
    if (!info->IsActionLog() && (diff_bytes < threshold()) &&
        (cfg_rate != GlobalVrouter::kDisableSampling) &&
        (!info->teardown_time() || !info->exported_atleast_once()) &&
        flow_stats_manager_->flow_export_rate() >= ((double)cfg_rate) * 0.8) {
        subject_flows_to_algorithm = true;
    }

    if (subject_flows_to_algorithm) {
        double probability = diff_bytes/threshold();
        uint32_t num = rand() % threshold();
        if (num > diff_bytes) {
            /* Do not export the flow, if the random number generated is more
             * than the diff_bytes */
            flow_stats_manager_->flow_export_sampling_drops_++;
            info->set_prev_diff_bytes(diff_bytes);
            info->set_prev_diff_packets(diff_pkts);
            /* The second part of the if condition below is not required but
             * added for better readability. It is not required because
             * exported_atleast_once() will always be false if teardown time is
             * set. If both teardown_time and exported_atleast_once are true we
             * will never be here */
            if (info->teardown_time() && !info->exported_atleast_once()) {
                flow_stats_manager_->flow_export_drops_++;
            }
            return;
        }
        /* Normalize the diff_bytes and diff_packets reported using the
         * probability value */
        if (probability == 0) {
            diff_bytes = diff_pkts = 0;
        } else {
            diff_bytes = diff_bytes/probability;
            diff_pkts = diff_pkts/probability;
        }
    }
    /* Reset diff stats since flow will be exported now */
    info->set_prev_diff_bytes(0);
    info->set_prev_diff_packets(0);

    /* Mark the flow as exported */
    info->set_exported_atleast_once(true);

    FlowLogData &s_flow = msg_list_[GetFlowMsgIdx()];

    s_flow.set_flowuuid(to_string(flow->uuid()));
    s_flow.set_bytes(info->bytes());
    s_flow.set_packets(info->packets());
    s_flow.set_diff_bytes(diff_bytes);
    s_flow.set_diff_packets(diff_pkts);
    s_flow.set_tcp_flags(info->tcp_flags());

    s_flow.set_sourceip(flow->key().src_addr);
    s_flow.set_destip(flow->key().dst_addr);
    s_flow.set_protocol(flow->key().protocol);
    s_flow.set_sport(flow->key().src_port);
    s_flow.set_dport(flow->key().dst_port);
    s_flow.set_sourcevn(flow->data().source_vn_match);
    s_flow.set_destvn(flow->data().dest_vn_match);
    s_flow.set_vm(flow->data().vm_cfg_name);
    if (flow->is_flags_set(FlowEntry::ReverseFlow)) {
        s_flow.set_forward_flow(false);
    } else {
        s_flow.set_forward_flow(true);
    }

    string drop_reason = FlowEntry::DropReasonStr(flow->data().drop_reason);
    s_flow.set_drop_reason(drop_reason);

    s_flow.set_sg_rule_uuid(flow->sg_rule_uuid());
    s_flow.set_nw_ace_uuid(flow->nw_ace_uuid());
    if (flow->intf_entry()) {
        s_flow.set_vmi_uuid(UuidToString(flow->intf_entry()->GetUuid()));
    }

    if (rflow) {
        s_flow.set_reverse_uuid(to_string(rflow->uuid()));
    } else if (params) {
        s_flow.set_reverse_uuid(to_string(params->rev_uuid_));
    }

    // Set flow action
    std::string action_str;
    GetFlowSandeshActionParams(flow->data().match_p.action_info, action_str);
    s_flow.set_action(action_str);
    s_flow.set_setup_time(info->setup_time());
    if (info->teardown_time()) {
        s_flow.set_teardown_time(info->teardown_time());
    }
    SetUnderlayInfo(info, s_flow);
    info->set_changed(false);

    if (flow->is_flags_set(FlowEntry::LocalFlow)) {
        /* For local flows we need to send two flow log messages.
         * 1. With direction as ingress
         * 2. With direction as egress
         * For local flows we have already sent flow log above with
         * direction as ingress. We are sending flow log below with
         * direction as egress.
         */
        s_flow.set_direction_ing(1);
        s_flow.set_reverse_uuid(to_string(flow->egress_uuid()));
        SourceIpOverride(info, s_flow, params);
        EnqueueFlowMsg();

        FlowLogData &s_flow2 = msg_list_[GetFlowMsgIdx()];
        s_flow2 = s_flow;
        s_flow2.set_direction_ing(0);
        //Update the interface and VM name in this flow
        //For the reverse flow this would be egress flow
        //For example
        //    VM1 A --> B is fwd flow
        //    VM2 B --> A is rev flow
        //
        //Egress flow for fwd flow would be exported
        //while exporting rev flow, this done so that
        //key, stats and other stuff can be copied over
        //from current flow
        SetImplicitFlowDetails(info, s_flow2, params);
        //Export local flow of egress direction with a different UUID even when
        //the flow is same. Required for analytics module to query flows
        //irrespective of direction.
        EnqueueFlowMsg();
        flow_stats_manager_->flow_export_count_ += 2;
    } else {
        if (flow->is_flags_set(FlowEntry::IngressDir)) {
            s_flow.set_direction_ing(1);
            SourceIpOverride(info, s_flow, params);
        } else {
            s_flow.set_direction_ing(0);
        }
        EnqueueFlowMsg();
        flow_stats_manager_->flow_export_count_++;
    }
}

bool FlowStatsManager::UpdateFlowThreshold() {
    uint64_t curr_time = FlowStatsCollector::GetCurrentTime();
    bool export_rate_calculated = false;

    /* If flows are not being exported, no need to update threshold */
    if (!flow_export_count_) {
        return true;
    }

    // Calculate Flow Export rate
    if (prev_flow_export_rate_compute_time_) {
        uint64_t diff_secs = 0;
        uint64_t diff_micro_secs = curr_time -
            prev_flow_export_rate_compute_time_;
        if (diff_micro_secs) {
            diff_secs = diff_micro_secs/1000000;
        }
        if (diff_secs) {
            uint32_t flow_export_count = flow_export_count_reset();
            flow_export_rate_ = flow_export_count/diff_secs;
            prev_flow_export_rate_compute_time_ = curr_time;
            export_rate_calculated = true;
        }
    } else {
        prev_flow_export_rate_compute_time_ = curr_time;
        flow_export_count_ = 0;
        return true;
    }

    uint32_t cfg_rate = agent_->oper_db()->global_vrouter()->
                            flow_export_rate();
    /* No need to update threshold when flow_export_rate is NOT calculated
     * and configured flow export rate has not changed */
    if (!export_rate_calculated &&
        (cfg_rate == prev_cfg_flow_export_rate_)) {
        return true;
    }
    // Update sampling threshold based on flow_export_rate_
    if (flow_export_rate_ < ((double)cfg_rate) * 0.8) {
        UpdateThreshold(kDefaultFlowSamplingThreshold);
    } else if (flow_export_rate_ > (cfg_rate * 3)) {
        UpdateThreshold((threshold_ * 4));
    } else if (flow_export_rate_ > (cfg_rate * 2)) {
        UpdateThreshold((threshold_ * 3));
    } else if (flow_export_rate_ > ((double)cfg_rate) * 1.25) {
        UpdateThreshold((threshold_ * 2));
    }
    prev_cfg_flow_export_rate_ = cfg_rate;
    return true;
}

uint32_t FlowStatsCollector::threshold() const {
    return flow_stats_manager_->threshold();
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
        /* We don't export flows in TSN mode */
        if (agent_uve_->agent()->tsn_enabled() == false) {
            /* Fetch the update stats and export the flow with teardown_time */
            FlowExportInfo *info = FindFlowExportInfo(flow);
            if (!info) {
                break;
            }
            /* While updating stats for evicted flows, we set the teardown_time
             * and export the flow. So delete handling for evicted flows need
             * not update stats and export flow */
            RevFlowDepParams params = req->params();
            if (!info->teardown_time()) {
                UpdateStatsAndExportFlow(info, req->time(), &params);
            }
            /* ExportFlow will enqueue FlowLog message for send. If we have not
             * hit max messages to be sent, it will not dispatch. Invoke
             * DispatchPendingFlowMsg to send any enqueued messages in the queue
             * even if we don't have max messages to be sent */
            DispatchPendingFlowMsg();
        }
        /* Remove the entry from our tree */
        DeleteFlow(flow);
        break;
    }

    case FlowExportReq::UPDATE_FLOW_STATS: {
        EvictedFlowStatsUpdate(flow, req->bytes(), req->packets(),
                               req->oflow_bytes());
        /* ExportFlow will enqueue FlowLog message for send. If we have not hit
         * max messages to be sent, it will not dispatch. Invoke
         * DispatchPendingFlowMsg to send any enqueued messages in the queue
         * even if we don't have max messages to be sent */
        DispatchPendingFlowMsg();
        break;
    }

    default:
         assert(0);
    }

    if (deleted_ && flow_tree_.size() == 0 &&
        request_queue_.IsQueueEmpty() == true) {
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


void FlowStatsCollector::NewFlow(const FlowExportInfo &info) {
    FlowEntry *flow = info.flow();
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
    FlowEntryTree::iterator it = flow_tree_.find(info.flow());
    if (it != flow_tree_.end()) {
        it->second.set_changed(true);
        it->second.set_delete_enqueue_time(0);
        it->second.set_evict_enqueue_time(0);
        return;
    }

    /* Invoke NewFlow only if the entry is not present in our tree */
    NewFlow(info);
    flow_tree_.insert(make_pair(info.flow(), info));
}

void FlowStatsCollector::DeleteFlow(const FlowEntryPtr &flow) {
    FlowEntryTree::iterator it = flow_tree_.find(flow.get());
    if (it == flow_tree_.end())
        return;

    flow_tree_.erase(it);
}

void FlowStatsCollector::EvictedFlowStatsUpdate(const FlowEntryPtr &flow,
                                                uint32_t bytes,
                                                uint32_t packets,
                                                uint32_t oflow_bytes) {
    FlowEntry *fe = flow.get();
    FlowExportInfo *info = FindFlowExportInfo(fe);
    if (info) {
        /* We are updating stats of evicted flow. Set teardown_time here.
         * When delete event is being handled we don't export flow if
         * teardown time is set */
        UpdateAndExportInternal(info, bytes, oflow_bytes & 0xFFFF,
                                packets, oflow_bytes & 0xFFFF0000,
                                GetCurrentTime(), true, NULL);
    }
}

/////////////////////////////////////////////////////////////////////////////
// Introspect routines
/////////////////////////////////////////////////////////////////////////////
void FlowStatsCollectionParamsReq::HandleRequest() const {
    FlowStatsManager *mgr = Agent::GetInstance()->flow_stats_manager();
    FlowStatsCollectionParamsResp *resp = new FlowStatsCollectionParamsResp();
    resp->set_flow_export_rate(mgr->flow_export_rate());
    resp->set_sampling_threshold(mgr->threshold());

    resp->set_context(context());
    resp->Response();
    return;
}

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
    info.set_egress_uuid(to_string(value.egress_uuid()));
    info.set_source_vn(flow->data().source_vn_match);
    info.set_dest_vn(flow->data().dest_vn_match);
    info.set_sg_rule_uuid(flow->sg_rule_uuid());
    info.set_nw_ace_uuid(flow->nw_ace_uuid());
    info.set_setup_time(value.setup_time());
    info.set_teardown_time(value.teardown_time());
    info.set_last_modified_time(value.last_modified_time());
    info.set_bytes(value.bytes());
    info.set_packets(value.packets());
    info.set_flags(flow->flags());
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
    info.set_underlay_source_port(value.underlay_source_port());
    info.set_delete_enqueued(value.delete_enqueue_time() ? true : false);
}

void FlowStatsRecordsReq::HandleRequest() const {
    FlowStatsCollector::FlowEntryTree::iterator it;
    vector<FlowStatsRecord> list;
    FlowStatsCollector *col = Agent::GetInstance()->
        flow_stats_manager()->default_flow_stats_collector();
    FlowStatsRecordsResp *resp = new FlowStatsRecordsResp();
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
