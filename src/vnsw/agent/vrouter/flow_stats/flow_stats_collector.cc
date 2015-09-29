/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <bitset>
#include <boost/uuid/uuid_io.hpp>

#include <db/db.h>
#include <base/util.h>

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
#include <algorithm>
#include <pkt/flow_proto.h>
#include <pkt/flow_mgmt.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/flow_stats/flow_stats_interval_types.h>

FlowStatsCollector::FlowStatsCollector(boost::asio::io_service &io, int intvl,
                                       uint32_t flow_cache_timeout,
                                       AgentUveBase *uve) :
        StatsCollector(TaskScheduler::GetInstance()->GetTaskId
                       ("Agent::StatsCollector"),
                       StatsCollector::FlowStatsCollector,
                       io, intvl, "Flow stats collector"),
        agent_uve_(uve), delete_short_flow_(true),
        flow_tcp_syn_age_time_(FlowTcpSynAgeTime),
        request_queue_(agent_uve_->agent()->task_scheduler()->
                       GetTaskId("Agent::StatsCollector"),
                       StatsCollector::FlowStatsCollector,
                       boost::bind(&FlowStatsCollector::RequestHandler, this,
                                   _1)),
        flow_export_count_(0), prev_flow_export_rate_compute_time_(0),
        flow_export_rate_(0), threshold_(kDefaultFlowSamplingThreshold),
        flow_export_msg_drops_(0), prev_cfg_flow_export_rate_(0)  {
        flow_iteration_key_.Reset();
        flow_default_interval_ = intvl;
        if (flow_cache_timeout) {
            // Convert to usec
            flow_age_time_intvl_ = 1000000 * flow_cache_timeout;
        } else {
            flow_age_time_intvl_ = FlowAgeTime;
        }
        flow_count_per_pass_ = FlowCountPerPass;
        UpdateFlowMultiplier();
}

FlowStatsCollector::~FlowStatsCollector() {
}

void FlowStatsCollector::Shutdown() {
    StatsCollector::Shutdown();
}

void FlowStatsCollector::UpdateFlowMultiplier() {
    uint32_t age_time_millisec = flow_age_time_intvl_ / 1000;
    if (age_time_millisec == 0) {
        age_time_millisec = 1;
    }
    uint64_t default_age_time_millisec = FlowAgeTime / 1000;
    uint64_t max_flows = (MaxFlows * age_time_millisec) /
                                            default_age_time_millisec;
    flow_multiplier_ = (max_flows * FlowStatsMinInterval)/age_time_millisec;
}

bool FlowStatsCollector::TcpFlowShouldBeAged(FlowExportInfo *stats,
                                             const vr_flow_entry *k_flow,
                                             uint64_t curr_time,
                                             const FlowKey &key) {
    if (key.protocol != IPPROTO_TCP) {
        return false;
    }
#if 0
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
#endif

    return false;
}

bool FlowStatsCollector::ShouldBeAged(FlowExportInfo *info,
                                      const vr_flow_entry *k_flow,
                                      uint64_t curr_time,
                                      const FlowKey &key) {

    //If both forward and reverse flow are marked
    //as TCP closed then immediately remote the flow
    if (k_flow != NULL) {
        if (key.protocol == IPPROTO_TCP) {
            if (TcpFlowShouldBeAged(info, k_flow, curr_time, key)) {
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
    FlowExportInfo *rev_info = FindFlowExportInfo(info->rev_flow_key());
    if (!rev_info) {
        return 0;
    }

    return rev_info->fip();
}

VmInterfaceKey FlowStatsCollector::ReverseFlowFipVmi(const FlowExportInfo *info)
{
    FlowExportInfo *rev_info = FindFlowExportInfo(info->rev_flow_key());
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

void FlowStatsCollector::UpdateFlowStats(FlowExportInfo *info,
                                         uint64_t &diff_bytes,
                                         uint64_t &diff_packets) {
    FlowTableKSyncObject *ksync_obj = agent_uve_->agent()->ksync()->
                                         flowtable_ksync_obj();
    diff_bytes = 0;
    diff_packets = 0;
    if (!info) {
        return;
    }
    const vr_flow_entry *k_flow = ksync_obj->GetKernelFlowEntry
        (info->flow_handle(), false);
    if (k_flow) {
        uint64_t k_bytes, k_packets, bytes, packets;
        k_bytes = GetFlowStats(k_flow->fe_stats.flow_bytes_oflow,
                               k_flow->fe_stats.flow_bytes);
        k_packets = GetFlowStats(k_flow->fe_stats.flow_packets_oflow,
                                 k_flow->fe_stats.flow_packets);
        bytes = GetUpdatedFlowBytes(info, k_bytes);
        packets = GetUpdatedFlowPackets(info, k_packets);
        diff_bytes = bytes - info->bytes();
        diff_packets = packets - info->packets();
        info->set_bytes(bytes);
        info->set_packets(packets);
    }
}

void FlowStatsCollector::FlowDeleteEnqueue(const FlowKey &key, bool rev) {
    agent_uve_->agent()->pkt()->flow_table()->
        FlowEvent(FlowTableRequest::DELETE_FLOW, NULL, key, rev);
}

bool FlowStatsCollector::Run() {
    FlowEntryTree::iterator it;
    FlowExportInfo *rev_info = NULL;
    FlowExportInfo *info = NULL;
    uint32_t count = 0;
    bool key_updation_reqd = true, deleted;
    uint64_t diff_bytes, diff_pkts;
    Agent *agent = agent_uve_->agent();
    FlowTable *flow_obj = agent->pkt()->flow_table();
    FlowKey key;

    run_counter_++;
    if (!flow_tree_.size()) {
        return true;
    }
    uint64_t curr_time = UTCTimestampUsec();
    it = flow_tree_.upper_bound(flow_iteration_key_);
    if (it == flow_tree_.end()) {
        it = flow_tree_.begin();
    }
    FlowTableKSyncObject *ksync_obj = agent->ksync()->flowtable_ksync_obj();

    while (it != flow_tree_.end()) {
        key = it->first;
        info = &it->second;
        it++;
        deleted = false;

        flow_iteration_key_ = it->first;
        const vr_flow_entry *k_flow = ksync_obj->GetKernelFlowEntry
            (info->flow_handle(), false);
        // Can the flow be aged?
        if (ShouldBeAged(info, k_flow, curr_time, key)) {
            rev_info = FindFlowExportInfo(info->rev_flow_key());
            // If reverse_flow is present, wait till both are aged
            if (rev_info) {
                const vr_flow_entry *k_flow_rev;
                k_flow_rev = ksync_obj->GetKernelFlowEntry
                    (rev_info->flow_handle(), false);
                if (ShouldBeAged(rev_info, k_flow_rev, curr_time,
                                 info->rev_flow_key())) {
                    deleted = true;
                }
            } else {
                deleted = true;
            }
        }

        if (deleted == true) {
            if (it != flow_tree_.end()) {
                FlowKey next_flow_key = it->first;
                FlowKey del_flow_key = info->rev_flow_key();
                if (next_flow_key.IsEqual(del_flow_key)) {
                    it++;
                }
            }
            FlowDeleteEnqueue(key, rev_info != NULL? true : false);
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
            /* Don't account for agent overflow bits while comparing change in
             * stats */
            if (bytes != k_bytes) {
                uint64_t packets, k_packets;

                k_packets = GetFlowStats(k_flow->fe_stats.flow_packets_oflow,
                                         k_flow->fe_stats.flow_packets);
                bytes = GetUpdatedFlowBytes(info, k_bytes);
                packets = GetUpdatedFlowPackets(info, k_packets);
                diff_bytes = bytes - info->bytes();
                diff_pkts = packets - info->packets();
                //Update Inter-VN stats
                UpdateInterVnStats(info, diff_bytes, diff_pkts);
                //Update Floating-IP stats
                UpdateFloatingIpStats(info, diff_bytes, diff_pkts);
                info->set_bytes(bytes);
                info->set_packets(packets);
                info->set_last_modified_time(curr_time);
                ExportFlow(key, info, diff_bytes, diff_pkts);
            } else if (!info->exported()) {
                /* export flow (reverse) for which traffic is not seen yet. */
                ExportFlow(key, info, 0, 0);
            }
        }

        if ((!deleted) && (delete_short_flow_ == true) &&
            info->is_flags_set(FlowEntry::ShortFlow)) {
            if (it != flow_tree_.end()) {
                FlowKey next_flow_key =  it->first;
                FlowKey del_flow_key = info->rev_flow_key();
                if (next_flow_key.IsEqual(del_flow_key)) {
                    it++;
                }
            }
            FlowDeleteEnqueue(key, true);
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
    DispatchFlowMsg();

    if (count == flow_count_per_pass_) {
        if (it != flow_tree_.end()) {
            key_updation_reqd = false;
        }
    }

    /* Reset the iteration key if we are done with all the elements */
    if (key_updation_reqd) {
        flow_iteration_key_.Reset();
    }

    UpdateFlowThreshold(curr_time);
    /* Update the flow_timer_interval and flow_count_per_pass_ based on
     * total flows that we have
     */
    uint32_t total_flows = flow_obj->Size();
    uint32_t flow_timer_interval;

    uint32_t age_time_millisec = flow_age_time_intvl() / 1000;

    if (total_flows > 0) {
        flow_timer_interval = std::min((age_time_millisec * flow_multiplier_)/
                                        total_flows, 1000U);
    } else {
        flow_timer_interval = flow_default_interval_;
    }

    if (age_time_millisec > 0) {
        flow_count_per_pass_ = std::max((flow_timer_interval * total_flows)/
                                         age_time_millisec, 100U);
    } else {
        flow_count_per_pass_ = 100U;
    }
    set_expiry_time(flow_timer_interval);
    return true;
}

void SetFlowStatsInterval_InSeconds::HandleRequest() const {
    SandeshResponse *resp;
    if (get_interval() > 0) {
        FlowStatsCollector *fec = Agent::GetInstance()->flow_stats_collector();
        fec->set_expiry_time(get_interval() * 1000);
        resp = new FlowStatsCfgResp();
    } else {
        resp = new FlowStatsCfgErrResp();
    }

    resp->set_context(context());
    resp->Response();
    return;
}

void GetFlowStatsInterval::HandleRequest() const {
    FlowStatsIntervalResp_InSeconds *resp =
        new FlowStatsIntervalResp_InSeconds();
    resp->set_flow_stats_interval((Agent::GetInstance()->flow_stats_collector()->
        expiry_time())/1000);

    resp->set_context(context());
    resp->Response();
    return;
}

/////////////////////////////////////////////////////////////////////////////
// Utility methods to enqueue events into work-queue
/////////////////////////////////////////////////////////////////////////////
void FlowStatsCollector::AddEvent(FlowEntryPtr &flow) {
    FlowEntry *fe = flow.get();
    FlowExportInfo info(fe, UTCTimestampUsec());
    boost::shared_ptr<FlowExportReq>
        req(new FlowExportReq(FlowExportReq::ADD_FLOW, fe->key(), info));
    request_queue_.Enqueue(req);
}

void FlowStatsCollector::DeleteEvent(const FlowKey &key) {
    boost::shared_ptr<FlowExportReq>
        req(new FlowExportReq(FlowExportReq::DELETE_FLOW, key,
                              UTCTimestampUsec()));
    request_queue_.Enqueue(req);
}

void FlowStatsCollector::FlowIndexUpdateEvent(const FlowKey &key,
                                              uint32_t idx) {
    boost::shared_ptr<FlowExportReq>
        req(new FlowExportReq(FlowExportReq::UPDATE_FLOW_INDEX, key, 0, idx));
    request_queue_.Enqueue(req);
}

bool FlowStatsCollector::SetUnderlayPort(FlowExportInfo *info,
                                         FlowDataIpv4 &s_flow) {
    uint16_t underlay_src_port = 0;
    bool exported = false;
    if (info->is_flags_set(FlowEntry::LocalFlow)) {
        /* Set source_port as 0 for local flows. Source port is calculated by
         * vrouter irrespective of whether flow is local or not. So for local
         * flows we need to ignore port given by vrouter
         */
        s_flow.set_underlay_source_port(0);
        exported = true;
    } else {
        if (info->tunnel_type().GetType() != TunnelType::MPLS_GRE) {
            underlay_src_port = info->underlay_source_port();
            if (underlay_src_port) {
                exported = true;
            }
        } else {
            exported = true;
        }
        s_flow.set_underlay_source_port(underlay_src_port);
    }
    info->set_underlay_sport_exported(exported);
    return exported;
}

void FlowStatsCollector::SetUnderlayInfo(FlowExportInfo *info,
                                         FlowDataIpv4 &s_flow) {
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
        info->set_underlay_sport_exported(true);
    } else {
        s_flow.set_vrouter_ip(rid);
        s_flow.set_other_vrouter_ip(info->peer_vrouter());
        if (info->tunnel_type().GetType() != TunnelType::MPLS_GRE) {
            underlay_src_port = info->underlay_source_port();
            if (underlay_src_port) {
                info->set_underlay_sport_exported(true);
            }
        } else {
            info->set_underlay_sport_exported(true);
        }
        s_flow.set_underlay_source_port(underlay_src_port);
    }
    s_flow.set_underlay_proto(info->tunnel_type().GetType());
}

/* For ingress flows, change the SIP as Nat-IP instead of Native IP */
void FlowStatsCollector::SourceIpOverride(const FlowKey &key,
                                          FlowExportInfo *info,
                                          FlowDataIpv4 &s_flow) {
    FlowExportInfo *rev_info = FindFlowExportInfo(info->rev_flow_key());
    if (info->is_flags_set(FlowEntry::NatFlow) && s_flow.get_direction_ing() &&
        rev_info) {
        const FlowKey *nat_key = &info->rev_flow_key();
        if (key.src_addr != nat_key->dst_addr) {
            // TODO: IPV6
            if (key.family == Address::INET) {
                s_flow.set_sourceip(nat_key->dst_addr.to_v4().to_ulong());
            } else {
                s_flow.set_sourceip(0);
            }
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

void FlowStatsCollector::EnqueueFlowMsg(FlowDataIpv4 &flow) {
    msg_list_.push_back(flow);
    if (msg_list_.size() == kMaxFlowMsgsPerSend) {
        DispatchFlowMsg();
    }
}

void FlowStatsCollector::DispatchFlowMsg() {
    if (msg_list_.size()) {
        FLOW_DATA_IPV4_OBJECT_LOG("", SandeshLevel::SYS_CRIT, msg_list_);
        msg_list_.clear();
    }
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
void FlowStatsCollector::ExportFlow(const FlowKey &key,
                                    FlowExportInfo *info,
                                    uint64_t diff_bytes,
                                    uint64_t diff_pkts) {
    /* We should always try to export flows with Action as LOG regardless of
     * configured flow-export-rate */
    if (!info->IsActionLog() &&
        !agent_uve_->agent()->oper_db()->global_vrouter()->flow_export_rate()) {
        flow_export_msg_drops_++;
        return;
    }

    if (!info->IsActionLog() && (diff_bytes < threshold_)) {
        double probability = diff_bytes/threshold_;
        uint32_t num = rand() % threshold_;
        if (num > diff_bytes) {
            /* Do not export the flow, if the random number generated is more
             * than the diff_bytes */
            flow_export_msg_drops_++;
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
    FlowDataIpv4   s_flow;

    s_flow.set_flowuuid(to_string(info->flow_uuid()));
    s_flow.set_bytes(info->bytes());
    s_flow.set_packets(info->packets());
    s_flow.set_diff_bytes(diff_bytes);
    s_flow.set_diff_packets(diff_pkts);

    // TODO: IPV6
    if (key.family == Address::INET) {
        s_flow.set_sourceip(key.src_addr.to_v4().to_ulong());
        s_flow.set_destip(key.dst_addr.to_v4().to_ulong());
    } else {
        s_flow.set_sourceip(0);
        s_flow.set_destip(0);
    }
    s_flow.set_protocol(key.protocol);
    s_flow.set_sport(key.src_port);
    s_flow.set_dport(key.dst_port);
    s_flow.set_sourcevn(info->source_vn());
    s_flow.set_destvn(info->dest_vn());
    s_flow.set_vm(info->vm_cfg_name());

    s_flow.set_sg_rule_uuid(info->sg_rule_uuid());
    s_flow.set_nw_ace_uuid(info->nw_ace_uuid());

    FlowExportInfo *rev_info = FindFlowExportInfo(info->rev_flow_key());
    if (rev_info) {
        s_flow.set_reverse_uuid(to_string(rev_info->flow_uuid()));
    }

    // Set flow action
    std::string action_str;
    GetFlowSandeshActionParams(info->action_info(), action_str);
    s_flow.set_action(action_str);
    if (!info->exported()) {
        s_flow.set_setup_time(info->setup_time());
        info->set_exported(true);
        SetUnderlayInfo(info, s_flow);
    } else {
        /* When the flow is being exported for first time, underlay port
         * info is set as part of SetUnderlayInfo. At this point it is possible
         * that port is not yet populated to flow-entry because of either
         * (i) flow-entry has not got chance to be evaluated by
         *     flow-stats-collector
         * (ii) there is no flow entry in vrouter yet
         * (iii) the flow entry in vrouter does not have underlay source port
         *       populated yet
         */
        if (!info->underlay_sport_exported()) {
            SetUnderlayPort(info, s_flow);
        }
    }

    if (info->teardown_time()) {
        s_flow.set_teardown_time(info->teardown_time());
    }

    if (info->is_flags_set(FlowEntry::LocalFlow)) {
        /* For local flows we need to send two flow log messages.
         * 1. With direction as ingress
         * 2. With direction as egress
         * For local flows we have already sent flow log above with
         * direction as ingress. We are sending flow log below with
         * direction as egress.
         */
        s_flow.set_direction_ing(1);
        SourceIpOverride(key, info, s_flow);
        EnqueueFlowMsg(s_flow);
        s_flow.set_direction_ing(0);
        //Export local flow of egress direction with a different UUID even when
        //the flow is same. Required for analytics module to query flows
        //irrespective of direction.
        s_flow.set_flowuuid(to_string(info->egress_uuid()));
        EnqueueFlowMsg(s_flow);
        flow_export_count_ += 2;
    } else {
        if (info->is_flags_set(FlowEntry::IngressDir)) {
            s_flow.set_direction_ing(1);
            SourceIpOverride(key, info, s_flow);
        } else {
            s_flow.set_direction_ing(0);
        }
        EnqueueFlowMsg(s_flow);
        flow_export_count_++;
    }
}

void FlowStatsCollector::UpdateFlowThreshold(uint64_t curr_time) {
    bool export_rate_calculated = false;

    /* If flows are not being exported, no need to update threshold */
    if (!flow_export_count_) {
        return;
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
            flow_export_rate_ = flow_export_count_/diff_secs;
            prev_flow_export_rate_compute_time_ = curr_time;
            flow_export_count_ = 0;
            export_rate_calculated = true;
        }
    } else {
        prev_flow_export_rate_compute_time_ = curr_time;
        flow_export_count_ = 0;
    }

    uint32_t cfg_rate = agent_uve_->agent()->oper_db()->global_vrouter()->
        flow_export_rate();
    /* No need to update threshold when flow_export_rate is NOT calculated
     * and configured flow export rate has not changed */
    if (!export_rate_calculated &&
        (cfg_rate == prev_cfg_flow_export_rate_)) {
        return;
    }
    // Update sampling threshold based on flow_export_rate_
    if (flow_export_rate_ < cfg_rate/4) {
        UpdateThreshold((threshold_ / 8));
    } else if (flow_export_rate_ < cfg_rate/2) {
        UpdateThreshold((threshold_ / 4));
    } else if (flow_export_rate_ < cfg_rate/1.25) {
        UpdateThreshold((threshold_ / 2));
    } else if (flow_export_rate_ > (cfg_rate * 3)) {
        UpdateThreshold((threshold_ * 8));
    } else if (flow_export_rate_ > (cfg_rate * 2)) {
        UpdateThreshold((threshold_ * 4));
    } else if (flow_export_rate_ > (cfg_rate * 1.25)) {
        UpdateThreshold((threshold_ * 3));
    }
    prev_cfg_flow_export_rate_ = cfg_rate;
}

void FlowStatsCollector::UpdateThreshold(uint32_t new_value) {
    if (new_value != 0) {
        threshold_ = new_value;
    }
}

bool FlowStatsCollector::RequestHandler(boost::shared_ptr<FlowExportReq> req) {
    switch (req->event()) {
    case FlowExportReq::ADD_FLOW: {
        AddFlow(req->key(), req->info());
        break;
    }

    case FlowExportReq::DELETE_FLOW: {
        /* Fetch the update stats and export the flow with teardown_time */
        uint64_t diff_bytes, diff_packets;
        FlowExportInfo *info = FindFlowExportInfo(req->key());
        if (!info) {
            /* Do not expect duplicate deletes */
            assert(0);
        }
        info->set_teardown_time(req->time());
        UpdateFlowStats(info, diff_bytes, diff_packets);
        ExportFlow(req->key(), info, diff_bytes, diff_packets);
        /* Remove the entry from our tree */
        DeleteFlow(req->key());
        break;
    }

    case FlowExportReq::UPDATE_FLOW_INDEX: {
        UpdateFlowIndex(req->key(), req->index());
        break;
    }

    default:
         assert(0);

    }
    return true;
}

FlowExportInfo *
FlowStatsCollector::FindFlowExportInfo(const FlowKey &flow) {
    FlowEntryTree::iterator it = flow_tree_.find(flow);
    if (it == flow_tree_.end()) {
        return NULL;
    }

    return &it->second;
}

void FlowStatsCollector::AddFlow(const FlowKey &key, FlowExportInfo info) {
    FlowEntryTree::iterator it = flow_tree_.find(key);
    if (it != flow_tree_.end()) {
        it->second = info;
        return;
    }

    flow_tree_.insert(make_pair(key, info));
}

void FlowStatsCollector::DeleteFlow(const FlowKey &key) {
    FlowEntryTree::iterator it = flow_tree_.find(key);
    if (it == flow_tree_.end())
        return;

    flow_tree_.erase(it);
}

void FlowStatsCollector::UpdateFlowIndex(const FlowKey &key, uint32_t idx) {
    FlowExportInfo *info = FindFlowExportInfo(key);
    if (info) {
        info->set_flow_handle(idx);
    }
}
