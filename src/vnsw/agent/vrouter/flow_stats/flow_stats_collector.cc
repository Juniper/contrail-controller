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
                       ("Agent::StatsCollector"), instance_id,
                       io, intvl, "Flow stats collector"),
        agent_uve_(uve), rand_gen_(boost::uuids::random_generator()),
        flow_iteration_key_(boost::uuids::nil_uuid()),
        flow_tcp_syn_age_time_(FlowTcpSynAgeTime),
        request_queue_(agent_uve_->agent()->task_scheduler()->
                       GetTaskId("Agent::StatsCollector"),
                       instance_id,
                       boost::bind(&FlowStatsCollector::RequestHandler, 
                                   this, _1)),
        msg_list_(kMaxFlowMsgsPerSend, FlowLogData()), msg_index_(0),
        flow_aging_key_(*key), instance_id_(instance_id),
        flow_stats_manager_(aging_module) {
        flow_default_interval_ = intvl;
        if (flow_cache_timeout) {
            // Convert to usec
            flow_age_time_intvl_ = 1000000L * (uint64_t)flow_cache_timeout;
        } else {
            flow_age_time_intvl_ = FlowAgeTime;
        }
        flow_count_per_pass_ = FlowCountPerPass;
        UpdateFlowMultiplier();
        deleted_ = false;
        request_queue_.set_name("Flow stats collector");
}

FlowStatsCollector::~FlowStatsCollector() {
    flow_stats_manager_->FreeIndex(instance_id_);
}

boost::uuids::uuid FlowStatsCollector::rand_gen() {
    return rand_gen_();
}

void FlowStatsCollector::Shutdown() {
    StatsCollector::Shutdown();
    request_queue_.Shutdown();
}

uint64_t FlowStatsCollector::GetScanTime() {
    uint64_t scan_time_millisec;
    /* Use Age Time itself as scan-time for non-tcp flows */
    if (flow_aging_key_.proto == IPPROTO_TCP) {
        /* Convert from seconds to milliseconds */
        scan_time_millisec = agent_uve_->agent()->params()->
            tcp_flow_scan_interval() * 1000;
    } else {
        /* Convert from micro-seconds to milliseconds */
        scan_time_millisec = flow_age_time_intvl_ / 1000;
    }

    if (scan_time_millisec == 0) {
        scan_time_millisec = 1;
    }
    return scan_time_millisec;
}

void FlowStatsCollector::UpdateFlowMultiplier() {
    uint64_t scan_time_millisec = GetScanTime();
    uint64_t default_age_time_millisec = FlowAgeTime / 1000;
    uint64_t max_flows = (MaxFlows * scan_time_millisec) /
                                            default_age_time_millisec;
    flow_multiplier_ = (max_flows * FlowStatsMinInterval)/scan_time_millisec;
}

bool FlowStatsCollector::TcpFlowShouldBeAged(FlowExportInfo *stats,
                                             const vr_flow_entry *k_flow,
                                             uint64_t curr_time) {
    if (stats->key().protocol != IPPROTO_TCP) {
        return false;
    }

    uint32_t closed_flags = VR_FLOW_TCP_HALF_CLOSE | VR_FLOW_TCP_RST;
    if (k_flow->fe_tcp_flags & closed_flags) {
        return true;
    }

    uint32_t syn_flag = VR_FLOW_TCP_SYN | VR_FLOW_TCP_SYN_R;
    if (k_flow->fe_tcp_flags & syn_flag) {
        uint32_t established =
            VR_FLOW_TCP_ESTABLISHED | VR_FLOW_TCP_ESTABLISHED_R;
        if (k_flow->fe_tcp_flags & established) {
            return false;
        }

        uint64_t diff_time = curr_time - stats->setup_time();
        if (diff_time >= flow_tcp_syn_age_time()) {
            return true;
        }
    }

    return false;
}

bool FlowStatsCollector::ShouldBeAged(FlowExportInfo *info,
                                      const vr_flow_entry *k_flow,
                                      uint64_t curr_time) {
    //If both forward and reverse flow are marked
    //as TCP closed then immediately remote the flow
    if (k_flow != NULL) {
        if (info->key().protocol == IPPROTO_TCP) {
            if (k_flow->fe_tcp_flags & VR_FLOW_TCP_DEAD) {
                return true;
            }
        }
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

    /* Ignore Non-Floating-IP flow */
    if (!flow->fip() || flow->fip_vmi().uuid_ == nil_uuid()) {
        return;
    }

    InterfaceUveStatsTable *table = static_cast<InterfaceUveStatsTable *>
        (agent_uve_->interface_uve_table());

    fip_info.bytes_ = bytes;
    fip_info.packets_ = pkts;
    fip_info.fip_ = flow->fip();
    fip_info.fip_vmi_ = flow->fip_vmi();
    fip_info.is_local_flow_ = flow->is_flags_set(FlowEntry::LocalFlow);
    fip_info.is_ingress_flow_ = flow->is_flags_set(FlowEntry::IngressDir);
    fip_info.is_reverse_flow_ = flow->is_flags_set(FlowEntry::ReverseFlow);
    fip_info.vn_ = flow->source_vn();

    fip_info.rev_fip_ = NULL;
    if (flow->fip() != ReverseFlowFip(flow)) {
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
    const string &vn = flow->source_vn();
    VmInterfaceKey vmi = ReverseFlowFipVmi(flow);
    Interface *intf = dynamic_cast<Interface *>
        (agent_uve_->agent()->interface_table()->FindActiveEntry(&vmi));

    if (intf) {
        InterfaceUveStatsTable *table = static_cast<InterfaceUveStatsTable *>
            (agent_uve_->interface_uve_table());
        return table->FipEntry(fip, vn, intf);
    }
    return NULL;
}

uint32_t FlowStatsCollector::ReverseFlowFip(const FlowExportInfo *info) {
    FlowExportInfo *rev_info = FindFlowExportInfo(info->rev_flow_uuid());
    if (!rev_info) {
        return 0;
    }

    return rev_info->fip();
}

VmInterfaceKey FlowStatsCollector::ReverseFlowFipVmi(const FlowExportInfo *info)
{
    FlowExportInfo *rev_info = FindFlowExportInfo(info->rev_flow_uuid());
    if (!rev_info) {
        return VmInterfaceKey(AgentKey::ADD_DEL_CHANGE, nil_uuid(), "");
    }

    return rev_info->fip_vmi();
}

void FlowStatsCollector::UpdateInterVnStats(FlowExportInfo *info,
                                            uint64_t bytes, uint64_t pkts) {
    string src_vn = info->source_vn(), dst_vn = info->dest_vn();
    VnUveTable *vn_table = static_cast<VnUveTable *>
        (agent_uve_->vn_uve_table());

    if (!info->source_vn().length())
        src_vn = FlowHandler::UnknownVn();
    if (!info->dest_vn().length())
        dst_vn = FlowHandler::UnknownVn();

    /* When packet is going from src_vn to dst_vn it should be interpreted
     * as ingress to vrouter and hence in-stats for src_vn w.r.t. dst_vn
     * should be incremented. Similarly when the packet is egressing vrouter
     * it should be considered as out-stats for dst_vn w.r.t. src_vn.
     * Here the direction "in" and "out" should be interpreted w.r.t vrouter
     */
    if (info->is_flags_set(FlowEntry::LocalFlow)) {
        vn_table->UpdateInterVnStats(src_vn, dst_vn, bytes, pkts, false);
        vn_table->UpdateInterVnStats(dst_vn, src_vn, bytes, pkts, true);
    } else {
        if (info->is_flags_set(FlowEntry::IngressDir)) {
            vn_table->UpdateInterVnStats(src_vn, dst_vn, bytes, pkts, false);
        } else {
            vn_table->UpdateInterVnStats(dst_vn, src_vn, bytes, pkts, true);
        }
    }
}

void FlowStatsCollector::UpdateStatsAndExportFlow(FlowExportInfo *info,
                                                  uint64_t teardown_time) {
    KSyncFlowMemory *ksync_obj = agent_uve_->agent()->ksync()->
                                         ksync_flow_memory();
    if (!info) {
        return;
    }
    const vr_flow_entry *k_flow = ksync_obj->GetValidKFlowEntry(info->key(),
                                                                info->
                                                                flow_handle());
    if (k_flow) {
        UpdateAndExportInternal(info, k_flow->fe_stats.flow_bytes,
                                k_flow->fe_stats.flow_bytes_oflow,
                                k_flow->fe_stats.flow_packets,
                                k_flow->fe_stats.flow_packets_oflow,
                                teardown_time, true);
        return;
    }
    /* If reading of stats fails, send a message with just teardown time */
    info->set_teardown_time(teardown_time);
    ExportFlow(info, 0, 0);
}

void FlowStatsCollector::FlowDeleteEnqueue(FlowExportInfo *info) {
    agent_uve_->agent()->pkt()->get_flow_proto()->DeleteFlowRequest
        (info->key(), true, info->flow_partition());
    info->set_delete_enqueued(true);
    FlowExportInfo *rev_info = FindFlowExportInfo(info->rev_flow_uuid());
    if (rev_info) {
        rev_info->set_delete_enqueued(true);
    }
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

void FlowStatsCollector::UpdateAndExportInternal(FlowExportInfo *info,
                                                 uint32_t bytes,
                                                 uint16_t oflow_bytes,
                                                 uint32_t pkts,
                                                 uint16_t oflow_pkts,
                                                 uint64_t time,
                                                 bool teardown_time) {
    uint64_t diff_bytes, diff_pkts;
    UpdateFlowStatsInternal(info, bytes, oflow_bytes, pkts, oflow_pkts, time,
                            teardown_time, &diff_bytes, &diff_pkts);
    ExportFlow(info, diff_bytes, diff_pkts);
}

bool FlowStatsCollector::Run() {
    FlowEntryTree::iterator it;
    FlowExportInfo *rev_info = NULL;
    FlowExportInfo *info = NULL;
    uint32_t count = 0;
    bool key_updation_reqd = true, deleted;

    run_counter_++;
    if (!flow_tree_.size()) {
        return true;
    }
    uint64_t curr_time = UTCTimestampUsec();
    it = flow_tree_.upper_bound(flow_iteration_key_);
    if (it == flow_tree_.end()) {
        it = flow_tree_.begin();
    }
    KSyncFlowMemory *ksync_obj = agent_uve_->agent()->ksync()->
                                         ksync_flow_memory();

    while (it != flow_tree_.end()) {
        info = &it->second;
        it++;

        if (info->delete_enqueued()) {
            // if we come across deleted entry, trigger explicit delete
            // again and skip further processing.
            // duplicate delete will be suppressed in flow_table
            FlowDeleteEnqueue(info);
            continue;
        }
        deleted = false;

        flow_iteration_key_ = it->first;
        const vr_flow_entry *k_flow = ksync_obj->GetValidKFlowEntry
            (info->key(), info->flow_handle());
        // Can the flow be aged?
        if (ShouldBeAged(info, k_flow, curr_time)) {
            rev_info = FindFlowExportInfo(info->rev_flow_uuid());
            // If reverse_flow is present, wait till both are aged
            if (rev_info) {
                const vr_flow_entry *k_flow_rev;
                k_flow_rev = ksync_obj->GetValidKFlowEntry
                    (rev_info->key(), rev_info->flow_handle());
                if (ShouldBeAged(rev_info, k_flow_rev, curr_time)) {
                    deleted = true;
                }
            } else {
                deleted = true;
            }
        }

        if (deleted == true) {
            FlowDeleteEnqueue(info);
            if (rev_info) {
                count++;
                if (count == flow_count_per_pass_) {
                    break;
                }
            }
        }

        if (deleted == false && k_flow) {
            uint64_t k_bytes, bytes;
            k_bytes = GetFlowStats(k_flow->fe_stats.flow_bytes_oflow,
                                   k_flow->fe_stats.flow_bytes);
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
                UpdateAndExportInternal(info, k_flow->fe_stats.flow_bytes,
                                        k_flow->fe_stats.flow_bytes_oflow,
                                        k_flow->fe_stats.flow_packets,
                                        k_flow->fe_stats.flow_packets_oflow,
                                        curr_time, false);
            } else if (info->changed()) {
                /* export flow (reverse) for which traffic is not seen yet. */
                ExportFlow(info, 0, 0);
            }
        }

        if ((!deleted) && (flow_stats_manager_->delete_short_flow() == true) &&
            info->is_flags_set(FlowEntry::ShortFlow)) {
            FlowDeleteEnqueue(info);
            if (rev_info) {
                count++;
                if (count == flow_count_per_pass_) {
                    break;
                }
            }
        }

        count++;
        if (count == flow_count_per_pass_) {
            break;
        }
    }

    //Send any pending flow export messages
    DispatchPendingFlowMsg();

    if (count == flow_count_per_pass_) {
        if (it != flow_tree_.end()) {
            key_updation_reqd = false;
        }
    }

    /* Reset the iteration key if we are done with all the elements */
    if (key_updation_reqd) {
        flow_iteration_key_ = boost::uuids::nil_uuid();
    }

    /* Update the flow_timer_interval and flow_count_per_pass_ based on
     * total flows that we have
     */
    uint32_t total_flows = flow_tree_.size();
    uint32_t flow_timer_interval;

    uint32_t scan_time_millisec = GetScanTime();

    if (total_flows > 0) {
        flow_timer_interval = std::min((scan_time_millisec * flow_multiplier_)/
                                        total_flows, 1000U);
        if (flow_timer_interval < FlowStatsMinInterval) {
            flow_timer_interval = FlowStatsMinInterval;
        }
    } else {
        flow_timer_interval = flow_default_interval_;
    }

    if (scan_time_millisec > 0) {
        flow_count_per_pass_ = std::max((flow_timer_interval * total_flows)/
                                         scan_time_millisec, 100U);
    } else {
        flow_count_per_pass_ = 100U;
    }
    set_expiry_time(flow_timer_interval);
    return true;
}

/////////////////////////////////////////////////////////////////////////////
// Utility methods to enqueue events into work-queue
/////////////////////////////////////////////////////////////////////////////
void FlowStatsCollector::AddEvent(FlowEntryPtr &flow) {
    FlowEntry *fe = flow.get();
    FlowExportInfo info(fe, UTCTimestampUsec());
    boost::shared_ptr<FlowExportReq>
        req(new FlowExportReq(FlowExportReq::ADD_FLOW, fe->uuid(), info));
    request_queue_.Enqueue(req);
}

void FlowStatsCollector::DeleteEvent(const boost::uuids::uuid &u) {
    boost::shared_ptr<FlowExportReq>
        req(new FlowExportReq(FlowExportReq::DELETE_FLOW, u,
                              UTCTimestampUsec()));
    request_queue_.Enqueue(req);
}

void FlowStatsCollector::FlowIndexUpdateEvent(const boost::uuids::uuid &u,
                                              uint32_t idx) {
    boost::shared_ptr<FlowExportReq>
        req(new FlowExportReq(FlowExportReq::UPDATE_FLOW_INDEX, u, 0, idx));
    request_queue_.Enqueue(req);
}

void FlowStatsCollector::UpdateStatsEvent(const boost::uuids::uuid &u,
                                          uint32_t bytes,
                                          uint32_t packets,
                                          uint32_t oflow_bytes) {
    boost::shared_ptr<FlowExportReq>
        req(new FlowExportReq(FlowExportReq::UPDATE_FLOW_STATS, u, bytes,
                              packets, oflow_bytes));
    request_queue_.Enqueue(req);
}

void FlowStatsCollector::SetUnderlayInfo(FlowExportInfo *info,
                                         FlowLogData &s_flow) {
    string rid = agent_uve_->agent()->router_id().to_string();
    uint16_t underlay_src_port = 0;
    if (info->is_flags_set(FlowEntry::LocalFlow)) {
        s_flow.set_vrouter_ip(rid);
        s_flow.set_other_vrouter_ip(rid);
        /* Set source_port as 0 for local flows. Source port is calculated by
         * vrouter irrespective of whether flow is local or not. So for local
         * flows we need to ignore port given by vrouter
         */
        s_flow.set_underlay_source_port(0);
    } else {
        s_flow.set_vrouter_ip(rid);
        s_flow.set_other_vrouter_ip(info->peer_vrouter());
        if (info->tunnel_type().GetType() != TunnelType::MPLS_GRE) {
            underlay_src_port = info->underlay_source_port();
        }
        s_flow.set_underlay_source_port(underlay_src_port);
    }
    s_flow.set_underlay_proto(info->tunnel_type().GetType());
}

/* For ingress flows, change the SIP as Nat-IP instead of Native IP */
void FlowStatsCollector::SourceIpOverride(FlowExportInfo *info,
                                          FlowLogData &s_flow) {
    FlowExportInfo *rev_info = FindFlowExportInfo(info->rev_flow_uuid());
    if (info->is_flags_set(FlowEntry::NatFlow) && s_flow.get_direction_ing() &&
        rev_info) {
        const FlowKey *nat_key = &rev_info->key();
        if (info->key().src_addr != nat_key->dst_addr) {
            s_flow.set_sourceip(nat_key->dst_addr);
        }
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
    FLOW_LOG_DATA_OBJECT_LOG("", SandeshLevel::SYS_CRIT, lst);
}

uint8_t FlowStatsCollector::GetFlowMsgIdx() {
    FlowLogData &obj = msg_list_[msg_index_];
    obj = FlowLogData();
    return msg_index_;
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
                                    uint64_t diff_pkts) {
    uint32_t cfg_rate = agent_uve_->agent()->oper_db()->global_vrouter()->
                        flow_export_rate();
    /* We should always try to export flows with Action as LOG regardless of
     * configured flow-export-rate */
    if (!info->IsActionLog() && !cfg_rate) {
        flow_stats_manager_->flow_export_msg_drops_++;
        return;
    }

    /* Subject a flow to sampling algorithm only when all of below is met:-
     * a. if Log is not configured as action for flow
     * b. actual flow-export-rate is >= 80% of configured flow-export-rate
     * c. diff_bytes is lesser than the threshold
     */
    bool subject_flows_to_algorithm = false;
    if (!info->IsActionLog() && (diff_bytes < threshold()) &&
        flow_stats_manager_->flow_export_rate() >= ((double)cfg_rate) * 0.8) {
        subject_flows_to_algorithm = true;
    }

    if (subject_flows_to_algorithm) {
        double probability = diff_bytes/threshold();
        uint32_t num = rand() % threshold();
        if (num > diff_bytes) {
            /* Do not export the flow, if the random number generated is more
             * than the diff_bytes */
            flow_stats_manager_->flow_export_msg_drops_++;
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
    FlowLogData &s_flow = msg_list_[GetFlowMsgIdx()];

    s_flow.set_flowuuid(to_string(info->flow_uuid()));
    s_flow.set_bytes(info->bytes());
    s_flow.set_packets(info->packets());
    s_flow.set_diff_bytes(diff_bytes);
    s_flow.set_diff_packets(diff_pkts);
    s_flow.set_tcp_flags(info->tcp_flags());

    s_flow.set_sourceip(info->key().src_addr);
    s_flow.set_destip(info->key().dst_addr);
    s_flow.set_protocol(info->key().protocol);
    s_flow.set_sport(info->key().src_port);
    s_flow.set_dport(info->key().dst_port);
    s_flow.set_sourcevn(info->source_vn());
    s_flow.set_destvn(info->dest_vn());
    s_flow.set_vm(info->vm_cfg_name());
    s_flow.set_drop_reason(info->drop_reason());

    s_flow.set_sg_rule_uuid(info->sg_rule_uuid());
    s_flow.set_nw_ace_uuid(info->nw_ace_uuid());
    if(info->interface_uuid().is_nil() == false) {
        s_flow.set_vmi_uuid(UuidToString(info->interface_uuid()));
    }

    FlowExportInfo *rev_info = FindFlowExportInfo(info->rev_flow_uuid());
    if (rev_info) {
        s_flow.set_reverse_uuid(to_string(rev_info->flow_uuid()));
    }

    // Set flow action
    std::string action_str;
    GetFlowSandeshActionParams(info->action_info(), action_str);
    s_flow.set_action(action_str);
    s_flow.set_setup_time(info->setup_time());
    SetUnderlayInfo(info, s_flow);
    info->set_changed(false);

    if (info->is_flags_set(FlowEntry::LocalFlow)) {
        /* For local flows we need to send two flow log messages.
         * 1. With direction as ingress
         * 2. With direction as egress
         * For local flows we have already sent flow log above with
         * direction as ingress. We are sending flow log below with
         * direction as egress.
         */
        s_flow.set_direction_ing(1);
        SourceIpOverride(info, s_flow);
        EnqueueFlowMsg();
        FlowLogData &s_flow2 = msg_list_[GetFlowMsgIdx()];
        s_flow2 = s_flow;
        s_flow2.set_direction_ing(0);
        //Export local flow of egress direction with a different UUID even when
        //the flow is same. Required for analytics module to query flows
        //irrespective of direction.
        s_flow2.set_flowuuid(to_string(info->egress_uuid()));
        EnqueueFlowMsg();
        flow_stats_manager_->flow_export_count_ += 2;
    } else {
        if (info->is_flags_set(FlowEntry::IngressDir)) {
            s_flow.set_direction_ing(1);
            SourceIpOverride(info, s_flow);
        } else {
            s_flow.set_direction_ing(0);
        }
        EnqueueFlowMsg();
        flow_stats_manager_->flow_export_count_++;
    }
}

bool FlowStatsManager::UpdateFlowThreshold() {
    uint64_t curr_time = UTCTimestampUsec();
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
    switch (req->event()) {
    case FlowExportReq::ADD_FLOW: {
        AddFlow(req->uuid(), req->info());
        break;
    }

    case FlowExportReq::DELETE_FLOW: {
        /* Fetch the update stats and export the flow with teardown_time */
        FlowExportInfo *info = FindFlowExportInfo(req->uuid());
        if (!info) {
            /* Ignore duplicate deletes for NOW. When two flow-entries point
             * to same flow-key (because entries are in different partition),
             * we can get duplicate deletes */
            /* TODO: change the flow_tree_ key of FlowStatsCollector from
             * FlowKey to Flow UUID */
            break;
        }
        /* While updating stats for evicted flows, we set the teardown_time
         * and export the flow. So delete handling for evicted flows need not
         * update stats and export flow */
        if (!info->teardown_time()) {
            UpdateStatsAndExportFlow(info, req->time());
        }
        /* ExportFlow will enqueue FlowLog message for send. If we have not hit
         * max messages to be sent, it will not dispatch. Invoke
         * DispatchPendingFlowMsg to send any enqueued messages in the queue
         * even if we don't have max messages to be sent */
        DispatchPendingFlowMsg();
        /* Remove the entry from our tree */
        DeleteFlow(req->uuid());
        break;
    }

    case FlowExportReq::UPDATE_FLOW_INDEX: {
        UpdateFlowIndex(req->uuid(), req->index());
        break;
    }

    case FlowExportReq::UPDATE_FLOW_STATS: {
        EvictedFlowStatsUpdate(req->uuid(), req->bytes(), req->packets(),
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
FlowStatsCollector::FindFlowExportInfo(const boost::uuids::uuid &u) {
    FlowEntryTree::iterator it = flow_tree_.find(u);
    if (it == flow_tree_.end()) {
        return NULL;
    }

    return &it->second;
}

const FlowExportInfo *
FlowStatsCollector::FindFlowExportInfo(const boost::uuids::uuid &u) const {
    FlowEntryTree::const_iterator it = flow_tree_.find(u);
    if (it == flow_tree_.end()) {
        return NULL;
    }

    return &it->second;
}


void FlowStatsCollector::NewFlow(const FlowExportInfo &info) {
    const FlowKey &key = info.key();
    uint8_t proto = key.protocol;
    uint16_t sport = key.src_port;
    uint16_t dport = key.dst_port;

    // Update vrouter port bitmap
    VrouterUveEntry *vre = static_cast<VrouterUveEntry *>(
        agent_uve_->vrouter_uve_entry());
    vre->UpdateBitmap(proto, sport, dport);

    // Update source-vn port bitmap
    VnUveTable *vnte = static_cast<VnUveTable *>(agent_uve_->vn_uve_table());
    vnte->UpdateBitmap(info.source_vn(), proto, sport, dport);
    // Update dest-vn port bitmap
    vnte->UpdateBitmap(info.dest_vn(), proto, sport, dport);


    VmInterfaceKey intf_key(AgentKey::ADD_DEL_CHANGE, info.interface_uuid(),
                            "");
    Interface *intf = static_cast<Interface *>
        (agent_uve_->agent()->interface_table()->Find(&intf_key, true));

    const VmInterface *port = dynamic_cast<const VmInterface *>(intf);
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

void FlowStatsCollector::AddFlow(const boost::uuids::uuid &uuid,
                                 FlowExportInfo info) {
    FlowEntryTree::iterator it = flow_tree_.find(uuid);
    if (it != flow_tree_.end()) {
        /* If entry is already present, copy only those fields which are
         * inherited from FlowEntry to existing entry. Copy this only if there
         * are changes in fields inherited from FlowEntry */
        if (!it->second.IsEqual(info)) {
            it->second.Copy(info);
        }
        it->second.set_delete_enqueued(false);
        return;
    }

    /* Invoke NewFlow only if the entry is not present in our tree */
    NewFlow(info);
    /* Generate egress UUID only while adding into our tree */
    info.set_egress_uuid(rand_gen());
    flow_tree_.insert(make_pair(uuid, info));
}

void FlowStatsCollector::DeleteFlow(const boost::uuids::uuid &uuid) {
    FlowEntryTree::iterator it = flow_tree_.find(uuid);
    if (it == flow_tree_.end())
        return;

    flow_tree_.erase(it);
}

void FlowStatsCollector::UpdateFlowIndex(const boost::uuids::uuid &uuid,
                                         uint32_t idx) {
    FlowExportInfo *info = FindFlowExportInfo(uuid);
    if (info) {
        info->set_flow_handle(idx);
    }
}

void FlowStatsCollector::EvictedFlowStatsUpdate(const boost::uuids::uuid &uuid,
                                                uint32_t bytes,
                                                uint32_t packets,
                                                uint32_t oflow_bytes) {
    FlowExportInfo *info = FindFlowExportInfo(uuid);
    if (info) {
        /* We are updating stats of evicted flow. Set teardown_time here.
         * When delete event is being handled we don't export flow if
         * teardown time is set */
        UpdateAndExportInternal(info, bytes, oflow_bytes & 0xFFFF,
                                packets, oflow_bytes & 0xFFFF0000,
                                UTCTimestampUsec(), true);
    }
}

/////////////////////////////////////////////////////////////////////////////
// Introspect routines
/////////////////////////////////////////////////////////////////////////////
void FlowStatsCollectionParamsReq::HandleRequest() const {
    FlowStatsCollector *col = Agent::GetInstance()->
        flow_stats_manager()->default_flow_stats_collector();
    FlowStatsCollectionParamsResp *resp = new FlowStatsCollectionParamsResp();
    resp->set_flow_export_rate(col->flow_export_rate());
    resp->set_sampling_threshold(col->threshold());

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
    KeyToSandeshFlowKey(value.key(), skey);
    info.set_key(skey);
    info.set_uuid(to_string(value.flow_uuid()));
    info.set_egress_uuid(to_string(value.egress_uuid()));
    info.set_rev_flow_uuid(to_string(value.rev_flow_uuid()));
    info.set_source_vn(value.source_vn());
    info.set_dest_vn(value.dest_vn());
    info.set_sg_rule_uuid(value.sg_rule_uuid());
    info.set_nw_ace_uuid(value.nw_ace_uuid());
    info.set_setup_time(value.setup_time());
    info.set_teardown_time(value.teardown_time());
    info.set_last_modified_time(value.last_modified_time());
    info.set_bytes(value.bytes());
    info.set_packets(value.packets());
    info.set_flags(value.flags());
    info.set_flow_handle(value.flow_handle());
    std::vector<ActionStr> action_str_l;
    info.set_action(action_str_l);
    SetActionStr(value.action_info(), action_str_l);
    info.set_vm_cfg_name(value.vm_cfg_name());
    info.set_peer_vrouter(value.peer_vrouter());
    info.set_tunnel_type(value.tunnel_type().ToString());
    const VmInterfaceKey &vmi = value.fip_vmi();
    string vmi_str = to_string(vmi.uuid_) + vmi.name_;
    info.set_fip_vmi(vmi_str);
    Ip4Address ip(value.fip());
    info.set_fip(ip.to_string());
    info.set_underlay_source_port(value.underlay_source_port());
    info.set_delete_enqueued(value.delete_enqueued());
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
        KeyToSandeshFlowKey(value.key(), skey);

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

void FetchFlowStatsRecord::HandleRequest() const {
    FlowStatsCollector::FlowEntryTree::iterator it;
    FlowStatsCollector *col = 
        Agent::GetInstance()->flow_stats_manager()->
        default_flow_stats_collector();
    FlowStatsRecordResp *resp = new FlowStatsRecordResp();
    resp->set_context(context());

    boost::uuids::uuid u = StringToUuid(get_uuid());

    it = col->flow_tree_.find(u);
    if (it != col->flow_tree_.end()) {
        const FlowExportInfo &info = it->second;
        SandeshFlowExportInfo sinfo;
        FlowExportInfoToSandesh(info, sinfo);
        resp->set_info(sinfo);
    }

    resp->Response();
    return;
}
