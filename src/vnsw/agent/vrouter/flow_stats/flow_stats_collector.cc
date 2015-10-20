/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <db/db.h>
#include <base/util.h>

#include <oper/interface_common.h>
#include <oper/mirror_table.h>

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
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/flow_stats/flow_stats_interval_types.h>
#include <oper/global_vrouter.h>
#include <init/agent_param.h>

FlowStatsCollector::FlowStatsCollector(boost::asio::io_service &io, int intvl,
                                       uint32_t flow_cache_timeout,
                                       AgentUveBase *uve) :
        StatsCollector(TaskScheduler::GetInstance()->GetTaskId
                       ("Agent::StatsCollector"),
                       StatsCollector::FlowStatsCollector,
                       io, intvl, "Flow stats collector"),
        agent_uve_(uve), delete_short_flow_(true),
        flow_export_count_(0), prev_flow_export_rate_compute_time_(0),
        flow_export_rate_(0), threshold_(kDefaultFlowSamplingThreshold),
        flow_export_msg_drops_(0), prev_cfg_flow_export_rate_(0),
        flow_tcp_syn_age_time_(FlowTcpSynAgeTime) {
        flow_iteration_key_.Reset();
        flow_default_interval_ = intvl;
        if (flow_cache_timeout) {
            // Convert to usec
            flow_age_time_intvl_ = 1000000L * (uint64_t)flow_cache_timeout;
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
    uint64_t age_time_millisec = flow_age_time_intvl_ / 1000;
    if (age_time_millisec == 0) {
        age_time_millisec = 1;
    }
    uint64_t default_age_time_millisec = FlowAgeTime / 1000;
    uint64_t max_flows = (MaxFlows * age_time_millisec) /
                                            default_age_time_millisec;
    flow_multiplier_ = (max_flows * FlowStatsMinInterval)/age_time_millisec;
}

bool FlowStatsCollector::TcpFlowShouldBeAged(FlowStats *stats,
                                             const vr_flow_entry *k_flow,
                                             uint64_t curr_time,
                                             const FlowEntry *flow) {
    if (k_flow == NULL) {
        return false;
    }

    if (flow->key().protocol != IPPROTO_TCP) {
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

        uint64_t diff_time = curr_time - stats->setup_time;
        if (diff_time >= flow_tcp_syn_age_time()) {
            return true;
        }
    }

    return false;
}

bool FlowStatsCollector::ShouldBeAged(FlowStats *stats,
                                      const vr_flow_entry *k_flow,
                                      uint64_t curr_time,
                                      const FlowEntry *flow) {

    if (k_flow != NULL) {
        uint64_t k_flow_bytes, bytes;

        k_flow_bytes = GetFlowStats(k_flow->fe_stats.flow_bytes_oflow,
                                    k_flow->fe_stats.flow_bytes);
        bytes = 0x0000ffffffffffffULL & stats->bytes;
        /* Don't account for agent overflow bits while comparing change in
         * stats */
        if (bytes < k_flow_bytes) {
            return false;
        }
    }

    uint64_t diff_time = curr_time - stats->last_modified_time;
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

uint64_t FlowStatsCollector::GetUpdatedFlowBytes(const FlowStats *stats,
                                                 uint64_t k_flow_bytes) {
    uint64_t oflow_bytes = 0xffff000000000000ULL & stats->bytes;
    uint64_t old_bytes = 0x0000ffffffffffffULL & stats->bytes;
    if (old_bytes > k_flow_bytes) {
        oflow_bytes += 0x0001000000000000ULL;
    }
    return (oflow_bytes |= k_flow_bytes);
}

uint64_t FlowStatsCollector::GetUpdatedFlowPackets(const FlowStats *stats,
                                                   uint64_t k_flow_pkts) {
    uint64_t oflow_pkts = 0xffffff0000000000ULL & stats->packets;
    uint64_t old_pkts = 0x000000ffffffffffULL & stats->packets;
    if (old_pkts > k_flow_pkts) {
        oflow_pkts += 0x0000010000000000ULL;
    }
    return (oflow_pkts |= k_flow_pkts);
}

void FlowStatsCollector::UpdateFloatingIpStats(const FlowEntry *flow,
                                       uint64_t bytes, uint64_t pkts) {
    InterfaceUveTable::FipInfo fip_info;

    /* Ignore Non-Floating-IP flow */
    if (!flow->stats().fip ||
        flow->stats().fip_vm_port_id == Interface::kInvalidIndex) {
        return;
    }

    InterfaceUveStatsTable *table = static_cast<InterfaceUveStatsTable *>
        (agent_uve_->interface_uve_table());

    fip_info.bytes_ = bytes;
    fip_info.packets_ = pkts;
    fip_info.fip_ = flow->stats().fip;
    fip_info.fip_vm_port_id_ = flow->stats().fip_vm_port_id;
    fip_info.is_local_flow_ = flow->is_flags_set(FlowEntry::LocalFlow);
    fip_info.is_ingress_flow_ = flow->is_flags_set(FlowEntry::IngressDir);
    fip_info.is_reverse_flow_ = flow->is_flags_set(FlowEntry::ReverseFlow);
    fip_info.vn_ = flow->data().source_vn;

    fip_info.rev_fip_ = NULL;
    if (flow->stats().fip != flow->reverse_flow_fip()) {
        /* This is the case where Source and Destination VMs (part of
         * same compute node) ping to each other to their respective
         * Floating IPs. In this case for each flow we need to increment
         * stats for both the VMs */
        fip_info.rev_fip_ = ReverseFlowFip(flow);
    }

    table->UpdateFloatingIpStats(fip_info);
}

InterfaceUveTable::FloatingIp *FlowStatsCollector::ReverseFlowFip
    (const FlowEntry *flow) {
    uint32_t fip = flow->reverse_flow_fip();
    const string &vn = flow->data().source_vn;
    uint32_t intf_id = flow->reverse_flow_vmport_id();
    Interface *intf = InterfaceTable::GetInstance()->FindInterface(intf_id);

    if (intf) {
        InterfaceUveStatsTable *table = static_cast<InterfaceUveStatsTable *>
            (agent_uve_->interface_uve_table());
        return table->FipEntry(fip, vn, intf);
    }
    return NULL;
}

void FlowStatsCollector::UpdateInterVnStats(const FlowEntry *fe, uint64_t bytes,
                                            uint64_t pkts) {

    string src_vn = fe->data().source_vn, dst_vn = fe->data().dest_vn;
    VnUveTable *vn_table = static_cast<VnUveTable *>
        (agent_uve_->vn_uve_table());

    if (!fe->data().source_vn.length())
        src_vn = FlowHandler::UnknownVn();
    if (!fe->data().dest_vn.length())
        dst_vn = FlowHandler::UnknownVn();

    /* When packet is going from src_vn to dst_vn it should be interpreted
     * as ingress to vrouter and hence in-stats for src_vn w.r.t. dst_vn
     * should be incremented. Similarly when the packet is egressing vrouter
     * it should be considered as out-stats for dst_vn w.r.t. src_vn.
     * Here the direction "in" and "out" should be interpreted w.r.t vrouter
     */
    if (fe->is_flags_set(FlowEntry::LocalFlow)) {
        vn_table->UpdateInterVnStats(src_vn, dst_vn, bytes, pkts, false);
        vn_table->UpdateInterVnStats(dst_vn, src_vn, bytes, pkts, true);
    } else {
        if (fe->is_flags_set(FlowEntry::IngressDir)) {
            vn_table->UpdateInterVnStats(src_vn, dst_vn, bytes, pkts, false);
        } else {
            vn_table->UpdateInterVnStats(dst_vn, src_vn, bytes, pkts, true);
        }
    }
}

void FlowStatsCollector::UpdateFlowStats(FlowEntry *flow, uint64_t &diff_bytes,
                                         uint64_t &diff_packets) {
    FlowTableKSyncObject *ksync_obj = Agent::GetInstance()->ksync()->
                                         flowtable_ksync_obj();

    const vr_flow_entry *k_flow = ksync_obj->GetKernelFlowEntry
        (flow->flow_handle(), false);
    if (k_flow) {
        uint64_t k_bytes, k_packets, bytes, packets;
        k_bytes = GetFlowStats(k_flow->fe_stats.flow_bytes_oflow,
                               k_flow->fe_stats.flow_bytes);
        k_packets = GetFlowStats(k_flow->fe_stats.flow_packets_oflow,
                                 k_flow->fe_stats.flow_packets);
        FlowStats *stats = &(flow->stats_);
        bytes = GetUpdatedFlowBytes(stats, k_bytes);
        packets = GetUpdatedFlowPackets(stats, k_packets);
        diff_bytes = bytes - stats->bytes;
        diff_packets = packets - stats->packets;
        stats->bytes = bytes;
        stats->packets = packets;
    } else {
        diff_bytes = 0;
        diff_packets = 0;
    }
}

bool FlowStatsCollector::SetUnderlayPort(FlowEntry *flow,
                                         FlowDataIpv4 &s_flow) {
    uint16_t underlay_src_port = 0;
    bool exported = false;
    if (flow->is_flags_set(FlowEntry::LocalFlow)) {
        /* Set source_port as 0 for local flows. Source port is calculated by
         * vrouter irrespective of whether flow is local or not. So for local
         * flows we need to ignore port given by vrouter
         */
        s_flow.set_underlay_source_port(0);
        exported = true;
    } else {
        if (flow->tunnel_type().GetType() != TunnelType::MPLS_GRE) {
            underlay_src_port = flow->underlay_source_port();
            if (underlay_src_port) {
                exported = true;
            }
        } else {
            exported = true;
        }
        s_flow.set_underlay_source_port(underlay_src_port);
    }
    flow->set_underlay_sport_exported(exported);
    return exported;
}

void FlowStatsCollector::SetUnderlayInfo(FlowEntry *flow,
                                         FlowDataIpv4 &s_flow) {
    string rid = agent_uve_->agent()->router_id().to_string();
    uint16_t underlay_src_port = 0;
    if (flow->is_flags_set(FlowEntry::LocalFlow)) {
        s_flow.set_vrouter_ip(rid);
        s_flow.set_other_vrouter_ip(rid);
        /* Set source_port as 0 for local flows. Source port is calculated by
         * vrouter irrespective of whether flow is local or not. So for local
         * flows we need to ignore port given by vrouter
         */
        s_flow.set_underlay_source_port(0);
        flow->set_underlay_sport_exported(true);
    } else {
        s_flow.set_vrouter_ip(rid);
        s_flow.set_other_vrouter_ip(flow->peer_vrouter());
        if (flow->tunnel_type().GetType() != TunnelType::MPLS_GRE) {
            underlay_src_port = flow->underlay_source_port();
            if (underlay_src_port) {
                flow->set_underlay_sport_exported(true);
            }
        } else {
            flow->set_underlay_sport_exported(true);
        }
        s_flow.set_underlay_source_port(underlay_src_port);
    }
    s_flow.set_underlay_proto(flow->tunnel_type().GetType());
}

/* For ingress flows, change the SIP as Nat-IP instead of Native IP */
void FlowStatsCollector::SourceIpOverride(FlowEntry *flow,
                                          FlowDataIpv4 &s_flow) {
    FlowEntry *rev_flow = flow->reverse_flow_entry();
    if (flow->is_flags_set(FlowEntry::NatFlow) && s_flow.get_direction_ing() &&
        rev_flow) {
        const FlowKey *nat_key = &rev_flow->key();
        if (flow->key().src_addr != nat_key->dst_addr) {
            // TODO: IPV6
            if (flow->key().family == Address::INET) {
                s_flow.set_sourceip(nat_key->dst_addr.to_v4().to_ulong());
            } else {
                s_flow.set_sourceip(0);
            }
        }
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
void FlowStatsCollector::FlowExport(FlowEntry *flow, uint64_t diff_bytes,
                                    uint64_t diff_pkts) {
    /* We should always try to export flows with Action as LOG regardless of
     * configured value for disable_flow_collection  */
    if (!flow->IsActionLog() &&
        agent_uve_->agent()->params()->disable_flow_collection()) {
        /* The knob disable_flow_collection is retained for backward
         * compatability purpose only. The recommended way is to use the knob
         * available in global-vrouter-config. */
        return;
    }

    /* We should always try to export flows with Action as LOG regardless of
     * configured flow-export-rate */
    if (!flow->IsActionLog() &&
        !agent_uve_->agent()->oper_db()->global_vrouter()->flow_export_rate()) {
        flow_export_msg_drops_++;
        return;
    }

    if (!flow->IsActionLog() && (diff_bytes < threshold_)) {
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
    SandeshLevel::type level = SandeshLevel::SYS_CRIT;
    FlowStats &stats = flow->stats_;

    s_flow.set_flowuuid(to_string(flow->flow_uuid()));
    s_flow.set_bytes(stats.bytes);
    s_flow.set_packets(stats.packets);
    s_flow.set_diff_bytes(diff_bytes);
    s_flow.set_diff_packets(diff_pkts);
    s_flow.set_tcp_flags(stats.tcp_flags);

    // TODO: IPV6
    if (flow->key().family == Address::INET) {
        s_flow.set_sourceip(flow->key().src_addr.to_v4().to_ulong());
        s_flow.set_destip(flow->key().dst_addr.to_v4().to_ulong());
    } else {
        s_flow.set_sourceip(0);
        s_flow.set_destip(0);
    }
    s_flow.set_protocol(flow->key().protocol);
    s_flow.set_sport(flow->key().src_port);
    s_flow.set_dport(flow->key().dst_port);
    s_flow.set_sourcevn(flow->data().source_vn);
    s_flow.set_destvn(flow->data().dest_vn);

    if (stats.intf_in != Interface::kInvalidIndex) {
        Interface *intf = InterfaceTable::GetInstance()->FindInterface(stats.intf_in);
        if (intf && intf->type() == Interface::VM_INTERFACE) {
            VmInterface *vm_port = static_cast<VmInterface *>(intf);
            const VmEntry *vm = vm_port->vm();
            if (vm) {
                s_flow.set_vm(vm->GetCfgName());
            }
        }
    }
    s_flow.set_sg_rule_uuid(flow->sg_rule_uuid());
    s_flow.set_nw_ace_uuid(flow->nw_ace_uuid());
    s_flow.set_drop_reason
        (FlowEntry::FlowDropReasonStr.at(flow->data().drop_reason));

    FlowEntry *rev_flow = flow->reverse_flow_entry();
    if (rev_flow) {
        s_flow.set_reverse_uuid(to_string(rev_flow->flow_uuid()));
    }

    // Set flow action
    std::string action_str;
    GetFlowSandeshActionParams(flow->match_p().action_info, action_str);
    s_flow.set_action(action_str);

    // Flow setup(first) and teardown(last) messages are sent with higher
    // priority.
    if (!stats.exported) {
        s_flow.set_setup_time(stats.setup_time);
        stats.exported = true;
        SetUnderlayInfo(flow, s_flow);
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
        if (!flow->underlay_sport_exported()) {
            SetUnderlayPort(flow, s_flow);
        }
    }

    if (stats.teardown_time) {
        s_flow.set_teardown_time(stats.teardown_time);
        //Teardown time will be set in flow only when flow is deleted.
        //We need to reset the exported flag when flow is getting deleted to
        //handle flow entry reuse case (Flow add request coming for flows
        //marked as deleted)
        stats.exported = false;
        flow->set_underlay_sport_exported(false);
    }

    if (flow->is_flags_set(FlowEntry::LocalFlow)) {
        /* For local flows we need to send two flow log messages.
         * 1. With direction as ingress
         * 2. With direction as egress
         * For local flows we have already sent flow log above with
         * direction as ingress. We are sending flow log below with
         * direction as egress.
         */
        s_flow.set_direction_ing(1);
        SourceIpOverride(flow, s_flow);
        DispatchFlowMsg(level, s_flow);
        s_flow.set_direction_ing(0);
        //Export local flow of egress direction with a different UUID even when
        //the flow is same. Required for analytics module to query flows
        //irrespective of direction.
        s_flow.set_flowuuid(to_string(flow->egress_uuid()));
        DispatchFlowMsg(level, s_flow);
        flow_export_count_ += 2;
    } else {
        if (flow->is_flags_set(FlowEntry::IngressDir)) {
            s_flow.set_direction_ing(1);
            SourceIpOverride(flow, s_flow);
        } else {
            s_flow.set_direction_ing(0);
        }
        DispatchFlowMsg(level, s_flow);
        flow_export_count_++;
    }

}

void FlowStatsCollector::DispatchFlowMsg(SandeshLevel::type level,
                                         FlowDataIpv4 &flow) {
    FLOW_DATA_IPV4_OBJECT_LOG("", level, flow);
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
        UpdateThreshold((threshold_ * 4));
    } else if (flow_export_rate_ > (cfg_rate * 2)) {
        UpdateThreshold((threshold_ * 3));
    } else if (flow_export_rate_ > (cfg_rate * 1.25)) {
        UpdateThreshold((threshold_ * 2));
    }
    prev_cfg_flow_export_rate_ = cfg_rate;
}

void FlowStatsCollector::UpdateThreshold(uint32_t new_value) {
    if (new_value != 0) {
        threshold_ = new_value;
    }
}

bool FlowStatsCollector::Run() {
    FlowTable::FlowEntryMap::iterator it;
    FlowEntry *entry = NULL, *reverse_flow;
    FlowStats *stats = NULL;
    uint32_t count = 0;
    bool key_updation_reqd = true, deleted;
    uint64_t diff_bytes, diff_pkts;
    FlowTable *flow_obj = Agent::GetInstance()->pkt()->flow_table();

    run_counter_++;
    if (!flow_obj->Size()) {
        return true;
    }
    uint64_t curr_time = UTCTimestampUsec();
    it = flow_obj->flow_entry_map_.upper_bound(flow_iteration_key_);
    if (it == flow_obj->flow_entry_map_.end()) {
        it = flow_obj->flow_entry_map_.begin();
    }
    FlowTableKSyncObject *ksync_obj =
        Agent::GetInstance()->ksync()->flowtable_ksync_obj();

    while (it != flow_obj->flow_entry_map_.end()) {
        entry = it->second;
        stats = &(entry->stats_);
        it++;
        assert(entry);
        deleted = false;

        if (entry->deleted()) {
            continue;
        }

        flow_iteration_key_ = entry->key();
        const vr_flow_entry *k_flow = ksync_obj->GetKernelFlowEntry
            (entry->flow_handle(), false);
        reverse_flow = entry->reverse_flow_entry();
        // Can the flow be aged?
        if (ShouldBeAged(stats, k_flow, curr_time, entry)) {
            // If reverse_flow is present, wait till both are aged
            if (reverse_flow) {
                const vr_flow_entry *k_flow_rev;
                k_flow_rev = ksync_obj->GetKernelFlowEntry
                    (reverse_flow->flow_handle(), false);
                if (ShouldBeAged(&(reverse_flow->stats_), k_flow_rev,
                                 curr_time, entry)) {
                    deleted = true;
                }
            } else {
                deleted = true;
            }
        }

        if (deleted == true) {
            if (it != flow_obj->flow_entry_map_.end()) {
                if (it->second == reverse_flow) {
                    it++;
                }
            }
            Agent::GetInstance()->pkt()->flow_table()->Delete
                (entry->key(), reverse_flow != NULL? true : false);
            entry = NULL;
            if (reverse_flow) {
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
            bytes = 0x0000ffffffffffffULL & stats->bytes;
            /* Always copy udp source port even though vrouter does not change
             * it. Vrouter many change this behavior and recompute source port
             * whenever flow action changes. To keep agent independent of this,
             * always copy UDP source port */
            entry->set_underlay_source_port(k_flow->fe_udp_src_port);
            entry->set_tcp_flags(k_flow->fe_tcp_flags);
            /* Don't account for agent overflow bits while comparing change in
             * stats */
            if (bytes != k_bytes) {
                uint64_t packets, k_packets;

                k_packets = GetFlowStats(k_flow->fe_stats.flow_packets_oflow,
                                         k_flow->fe_stats.flow_packets);
                bytes = GetUpdatedFlowBytes(stats, k_bytes);
                packets = GetUpdatedFlowPackets(stats, k_packets);
                diff_bytes = bytes - stats->bytes;
                diff_pkts = packets - stats->packets;
                //Update Inter-VN stats
                UpdateInterVnStats(entry, diff_bytes, diff_pkts);
                //Update Floating-IP stats
                UpdateFloatingIpStats(entry, diff_bytes, diff_pkts);
                stats->bytes = bytes;
                stats->packets = packets;
                stats->last_modified_time = curr_time;
                FlowExport(entry, diff_bytes, diff_pkts);
            } else if (!stats->exported && !entry->deleted()) {
                /* export flow (reverse) for which traffic is not seen yet. */
                FlowExport(entry, 0, 0);
            }
        }

        if ((!deleted) && (delete_short_flow_ == true) &&
            entry->is_flags_set(FlowEntry::ShortFlow)) {
            if (it != flow_obj->flow_entry_map_.end()) {
                if (it->second == reverse_flow) {
                    it++;
                }
            }
            Agent::GetInstance()->pkt()->flow_table()->Delete
                (entry->key(), true);
            entry = NULL;
            if (reverse_flow) {
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

    if (count == flow_count_per_pass_) {
        if (it != flow_obj->flow_entry_map_.end()) {
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
        if (flow_timer_interval < FlowStatsMinInterval) {
            flow_timer_interval = FlowStatsMinInterval;
        }
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
