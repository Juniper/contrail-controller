/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>

#include <db/db.h>
#include <base/util.h>

#include <oper/interface_common.h>
#include <oper/mirror_table.h>
#include <oper/vrouter.h>

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
        agent_uve_(uve), flow_iteration_key_(NULL), delete_short_flow_(true),
        request_queue_(agent_uve_->agent()->task_scheduler()->
                       GetTaskId("Agent::StatsCollector"),
                       StatsCollector::FlowStatsCollector,
                       boost::bind(&FlowStatsCollector::RequestHandler, this,
                                   _1)),
        response_queue_(agent_uve_->agent()->task_scheduler()->
                        GetTaskId(FlowTable::TaskName()), 1,
                        boost::bind(&FlowStatsCollector::ResponseHandler, this,
                                    _1)),
        flow_export_count_(0), prev_flow_export_rate_compute_time_(0),
        flow_export_rate_(0), threshold_(kDefaultFlowSamplingThreshold),
        flow_export_msg_drops_(0), prev_cfg_flow_export_rate_(0)  {
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

bool FlowStatsCollector::ShouldBeAged(FlowExportInfo *info,
                                      const vr_flow_entry *k_flow,
                                      uint64_t curr_time) {
    if (k_flow != NULL) {
        uint64_t k_flow_bytes, bytes;

        k_flow_bytes = GetFlowStats(k_flow->fe_stats.flow_bytes_oflow,
                                    k_flow->fe_stats.flow_bytes);
        bytes = 0x0000ffffffffffffULL & info->bytes;
        /* Don't account for agent overflow bits while comparing change in
         * stats */
        if (bytes < k_flow_bytes) {
            return false;
        }
    }

    uint64_t diff_time = curr_time - info->last_modified_time;
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
    uint64_t oflow_bytes = 0xffff000000000000ULL & stats->bytes;
    uint64_t old_bytes = 0x0000ffffffffffffULL & stats->bytes;
    if (old_bytes > k_flow_bytes) {
        oflow_bytes += 0x0001000000000000ULL;
    }
    return (oflow_bytes |= k_flow_bytes);
}

uint64_t FlowStatsCollector::GetUpdatedFlowPackets(const FlowExportInfo *stats,
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
    if (!flow->fip() || flow->fip_vm_port_id() == Interface::kInvalidIndex) {
        return;
    }

    InterfaceUveStatsTable *table = static_cast<InterfaceUveStatsTable *>
        (agent_uve_->interface_uve_table());

    fip_info.bytes_ = bytes;
    fip_info.packets_ = pkts;
    fip_info.fip_ = flow->fip();
    fip_info.fip_vm_port_id_ = flow->fip_vm_port_id();
    fip_info.is_local_flow_ = flow->is_flags_set(FlowEntry::LocalFlow);
    fip_info.is_ingress_flow_ = flow->is_flags_set(FlowEntry::IngressDir);
    fip_info.is_reverse_flow_ = flow->is_flags_set(FlowEntry::ReverseFlow);
    fip_info.vn_ = flow->data().source_vn;

    fip_info.rev_fip_ = NULL;
    if (flow->fip() != flow->reverse_flow_fip()) {
        /* This is the case where Source and Destination VMs (part of
         * same compute node) ping to each other to their respective
         * Floating IPs. In this case for each flow we need to increment
         * stats for both the VMs */
        fip_info.rev_fip_ = ReverseFlowFipEntry(flow);
    }

    table->UpdateFloatingIpStats(fip_info);
}

InterfaceUveTable::FloatingIp *FlowStatsCollector::ReverseFlowFipEntry
    (const FlowEntry *flow) {
    uint32_t fip = flow->reverse_flow_fip();
    const string &vn = flow->data().source_vn;
    uint32_t intf_id = flow->reverse_flow_vm_port_id();
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

    diff_bytes = 0;
    diff_packets = 0;
    const vr_flow_entry *k_flow = ksync_obj->GetKernelFlowEntry
        (flow->flow_handle(), false);
    if (k_flow) {
        uint64_t k_bytes, k_packets, bytes, packets;
        k_bytes = GetFlowStats(k_flow->fe_stats.flow_bytes_oflow,
                               k_flow->fe_stats.flow_bytes);
        k_packets = GetFlowStats(k_flow->fe_stats.flow_packets_oflow,
                                 k_flow->fe_stats.flow_packets);
        FlowExportInfo *stats = FindFlowExportInfo(FlowEntryPtr(flow));
        if (stats) {
            bytes = GetUpdatedFlowBytes(stats, k_bytes);
            packets = GetUpdatedFlowPackets(stats, k_packets);
            diff_bytes = bytes - stats->bytes;
            diff_packets = packets - stats->packets;
            stats->bytes = bytes;
            stats->packets = packets;
        }
    }
}

bool FlowStatsCollector::Run() {
    FlowEntryTree::iterator it;
    FlowEntry *entry = NULL, *reverse_flow;
    FlowExportInfo *info = NULL;
    uint32_t count = 0;
    bool key_updation_reqd = true, deleted;
    uint64_t diff_bytes, diff_pkts;
    Agent *agent = agent_uve_->agent();
    FlowTable *flow_obj = agent->pkt()->flow_table();

    run_counter_++;
    if (!flow_tree_.size()) {
        return true;
    }
    uint64_t curr_time = UTCTimestampUsec();
    it = flow_tree_.upper_bound(FlowEntryPtr(flow_iteration_key_));
    if (it == flow_tree_.end()) {
        it = flow_tree_.begin();
    }
    FlowTableKSyncObject *ksync_obj = agent->ksync()->flowtable_ksync_obj();

    while (it != flow_tree_.end()) {
        entry = it->first.get();
        info = &it->second;
        it++;
        assert(entry);
        deleted = false;

        if (entry->deleted()) {
            continue;
        }

        flow_iteration_key_ = entry;
        const vr_flow_entry *k_flow = ksync_obj->GetKernelFlowEntry
            (entry->flow_handle(), false);
        reverse_flow = entry->reverse_flow_entry();
        // Can the flow be aged?
        if (ShouldBeAged(info, k_flow, curr_time)) {
            FlowExportInfo *rev_info = NULL;
            if (reverse_flow) {
                rev_info = FindFlowExportInfo(FlowEntryPtr(reverse_flow));
            }
            // If reverse_flow is present, wait till both are aged
            if (rev_info) {
                const vr_flow_entry *k_flow_rev;

                k_flow_rev = ksync_obj->GetKernelFlowEntry
                    (reverse_flow->flow_handle(), false);
                if (ShouldBeAged(rev_info, k_flow_rev, curr_time)) {
                    deleted = true;
                }
            } else {
                deleted = true;
            }
        }

        if (deleted == true) {
            if (it != flow_tree_.end()) {
                if (it->first.get() == reverse_flow) {
                    it++;
                }
            }
            agent->pkt()->flow_table()->Delete(entry->key(),
                reverse_flow != NULL? true : false);
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
            bytes = 0x0000ffffffffffffULL & info->bytes;
            /* Always copy udp source port even though vrouter does not change
             * it. Vrouter many change this behavior and recompute source port
             * whenever flow action changes. To keep agent independent of this,
             * always copy UDP source port */
            info->underlay_source_port = k_flow->fe_udp_src_port;
            /* Don't account for agent overflow bits while comparing change in
             * stats */
            if (bytes != k_bytes) {
                uint64_t packets, k_packets;

                k_packets = GetFlowStats(k_flow->fe_stats.flow_packets_oflow,
                                         k_flow->fe_stats.flow_packets);
                bytes = GetUpdatedFlowBytes(info, k_bytes);
                packets = GetUpdatedFlowPackets(info, k_packets);
                diff_bytes = bytes - info->bytes;
                diff_pkts = packets - info->packets;
                //Update Inter-VN stats
                UpdateInterVnStats(entry, diff_bytes, diff_pkts);
                //Update Floating-IP stats
                UpdateFloatingIpStats(entry, diff_bytes, diff_pkts);
                info->bytes = bytes;
                info->packets = packets;
                info->last_modified_time = curr_time;
                ExportFlowHandler(entry, diff_bytes, diff_pkts);
            } else if (!entry->deleted() && !info->exported) {
                /* export flow (reverse) for which traffic is not seen yet. */
                ExportFlowHandler(entry, 0, 0);
            }
        }

        if ((!deleted) && (delete_short_flow_ == true) &&
            entry->is_flags_set(FlowEntry::ShortFlow)) {
            if (it != flow_tree_.end()) {
                if (it->first.get() == reverse_flow) {
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
        if (it != flow_tree_.end()) {
            key_updation_reqd = false;
        }
    }

    /* Reset the iteration key if we are done with all the elements */
    if (key_updation_reqd) {
        flow_iteration_key_ = NULL;
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
void FlowStatsCollector::AddEvent(FlowEntry *flow) {
    FlowEntryPtr flow_ptr(flow);
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::ADD_FLOW, flow_ptr,
                                UTCTimestampUsec()));
    request_queue_.Enqueue(req);
}

void FlowStatsCollector::DeleteEvent(FlowEntry *flow) {
    FlowEntryPtr flow_ptr(flow);
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::DELETE_FLOW, flow_ptr));
    request_queue_.Enqueue(req);
}

RevFlowParams FlowStatsCollector::BuildRevFlowParams(FlowEntry *fe) const {
    RevFlowParams params;
    if (SourceIpOverride(fe, &params.rev_flow_dst_ip)) {
        params.set_flags(DstIpSet);
    }
    FlowEntry *rev_flow = fe->reverse_flow_entry();
    if (rev_flow) {
        params.rev_flow_uuid = rev_flow->flow_uuid();
        params.set_flags(UuidSet);
    }
    return params;
}

FlowExportParams FlowStatsCollector::BuildFlowExportParms(FlowEntry *fe) {
    FlowExportParams params;

    /* Build Reverse flow dependent parameters */
    params.rev_flow_params = BuildRevFlowParams(fe);

    UpdateFlowStats(fe, params.diff_bytes, params.diff_packets);

    params.teardown_time = UTCTimestampUsec();
    return params;
}

void FlowStatsCollector::ExportOnDelete(FlowEntry *flow) {
    FlowEntryPtr flow_ptr(flow);
    /* Reverse flow dependent params have to be built/fetched before the
     * request is enqueued because when the request is dequeued and processed
     * it is possible that this relationship is broken */
    const FlowExportParams params = BuildFlowExportParms(flow);
    boost::shared_ptr<FlowMgmtRequest>
        req(new FlowMgmtRequest(FlowMgmtRequest::EXPORT_FLOW, flow_ptr,
                                params));
    request_queue_.Enqueue(req);
}

bool FlowStatsCollector::SetUnderlayPort(FlowEntry *flow, FlowExportInfo *info,
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
            underlay_src_port = info->underlay_source_port;
            if (underlay_src_port) {
                exported = true;
            }
        } else {
            exported = true;
        }
        s_flow.set_underlay_source_port(underlay_src_port);
    }
    info->underlay_sport_exported = exported;
    return exported;
}

void FlowStatsCollector::SetUnderlayInfo(FlowEntry *flow, FlowExportInfo *info,
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
        info->underlay_sport_exported = true;
    } else {
        s_flow.set_vrouter_ip(rid);
        s_flow.set_other_vrouter_ip(flow->peer_vrouter());
        if (flow->tunnel_type().GetType() != TunnelType::MPLS_GRE) {
            underlay_src_port = info->underlay_source_port;
            if (underlay_src_port) {
                info->underlay_sport_exported = true;
            }
        } else {
            info->underlay_sport_exported = true;
        }
        s_flow.set_underlay_source_port(underlay_src_port);
    }
    s_flow.set_underlay_proto(flow->tunnel_type().GetType());
}

/* For ingress flows, change the SIP as Nat-IP instead of Native IP */
bool FlowStatsCollector::SourceIpOverride(FlowEntry *flow, uint32_t *ip) const {
    FlowEntry *rev_flow = flow->reverse_flow_entry();
    /* For local flows we will always consider direction as ingress. Export of
     * local flows is done twice (once for ingress and other for egress dir).
     * During this export we pick the IP only when exporting as ingress */
    bool ingress = flow->is_flags_set(FlowEntry::LocalFlow) ||
        flow->is_flags_set(FlowEntry::IngressDir);

    if (flow->is_flags_set(FlowEntry::NatFlow) && ingress && rev_flow) {
        const FlowKey *nat_key = &rev_flow->key();
        if (flow->key().src_addr != nat_key->dst_addr) {
            // TODO: IPV6
            if (flow->key().family == Address::INET) {
                *ip = nat_key->dst_addr.to_v4().to_ulong();
                return true;
            }
        }
    }
    return false;
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

void FlowStatsCollector::DispatchFlowMsg(SandeshLevel::type level,
                                         FlowDataIpv4 &flow) {
    FLOW_DATA_IPV4_OBJECT_LOG("", level, flow);
}

void FlowStatsCollector::ExportFlowHandler(FlowEntry *flow, uint64_t diff_bytes,
                                           uint64_t diff_pkts) {
    FlowExportParams params(diff_bytes, diff_pkts);

    /* Build Reverse flow dependent parameters */
    params.rev_flow_params = BuildRevFlowParams(flow);

    ExportFlow(flow, params);
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
void FlowStatsCollector::ExportFlow(FlowEntry *flow,
                                    const FlowExportParams params) {
    /* Lock is required to ensure that flow is not being modified from
     * Agent::FlowTable task while it is being accessed for read in
     * Agent::StatsCollector task */
    tbb::mutex::scoped_lock mutex(flow->mutex());
    /* We should always try to export flows with Action as LOG regardless of
     * configured flow-export-rate */
    if (!flow->IsActionLog() &&
        !agent_uve_->agent()->oper_db()->vrouter()->flow_export_rate()) {
        flow_export_msg_drops_++;
        return;
    }

    FlowExportInfo *info = FindFlowExportInfo(FlowEntryPtr(flow));
    if (info == NULL) {
        return;
    }
    uint64_t diff_bytes = params.diff_bytes;
    uint64_t diff_pkts = params.diff_packets;
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
    SandeshLevel::type level = SandeshLevel::SYS_DEBUG;

    s_flow.set_flowuuid(to_string(flow->flow_uuid()));
    s_flow.set_bytes(info->bytes);
    s_flow.set_packets(info->packets);
    s_flow.set_diff_bytes(diff_bytes);
    s_flow.set_diff_packets(diff_pkts);

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

    if (flow->data().intf_in != Interface::kInvalidIndex) {
        Interface *intf = InterfaceTable::GetInstance()->FindInterface
            (flow->data().intf_in);
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

    if (params.rev_flow_params.is_flags_set(UuidSet)) {
        s_flow.set_reverse_uuid(to_string(params.rev_flow_params.
                                          rev_flow_uuid));
    }

    // Set flow action
    std::string action_str;
    GetFlowSandeshActionParams(flow->match_p().action_info, action_str);
    s_flow.set_action(action_str);
    // Flow setup(first) and teardown(last) messages are sent with higher
    // priority.
    if (!info->exported) {
        s_flow.set_setup_time(info->setup_time);
        info->exported = true;
        level = SandeshLevel::SYS_ERR;
        SetUnderlayInfo(flow, info, s_flow);
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
        if (!info->underlay_sport_exported) {
            if (SetUnderlayPort(flow, info, s_flow)) {
                level = SandeshLevel::SYS_ERR;
            }
        }
    }

    if (params.teardown_time) {
        s_flow.set_teardown_time(params.teardown_time);
        level = SandeshLevel::SYS_ERR;
        info->teardown_time = params.teardown_time;
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
        if (params.rev_flow_params.is_flags_set(DstIpSet)) {
            s_flow.set_sourceip(params.rev_flow_params.rev_flow_dst_ip);
        }
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
            if (params.rev_flow_params.is_flags_set(DstIpSet)) {
                s_flow.set_sourceip(params.rev_flow_params.rev_flow_dst_ip);
            }
        } else {
            s_flow.set_direction_ing(0);
        }
        DispatchFlowMsg(level, s_flow);
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

    uint32_t cfg_rate = agent_uve_->agent()->oper_db()->vrouter()->
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
    LOG(DEBUG, "Export rate " << flow_export_rate_ << " threshold " << threshold_);
}

void FlowStatsCollector::UpdateThreshold(uint32_t new_value) {
    if (new_value != 0) {
        threshold_ = new_value;
    }
}

bool FlowStatsCollector::RequestHandler
    (boost::shared_ptr<FlowMgmtRequest> req) {
    switch (req->event()) {
    case FlowMgmtRequest::ADD_FLOW: {
        AddFlow(req->flow(), req->time());
        break;
    }

    case FlowMgmtRequest::DELETE_FLOW: {
        DeleteFlow(req->flow());
        // On return from here reference to the flow is removed which can
        // result in deletion of flow from the tree. But, flow management runs
        // in parallel to flow processing. As a result, it can result in tree
        // being modified by two threads. Avoid the concurrency issue by
        // enqueuing a dummy request to flow-table queue. The reference will
        // be removed in flow processing context
        FlowMgmtResponse flow_resp(FlowMgmtResponse::FREE_FLOW_REF,
                                   req->flow().get(), NULL);
        ResponseEnqueue(flow_resp);
        break;
    }

    case FlowMgmtRequest::EXPORT_FLOW: {
        ExportFlow(req->flow().get(), req->params());
        break;
    }

    default:
         assert(0);

    }
    return true;
}

FlowExportInfo *
FlowStatsCollector::FindFlowExportInfo(const FlowEntryPtr &flow) {
    FlowEntryTree::iterator it = flow_tree_.find(flow);
    if (it == flow_tree_.end()) {
        return NULL;
    }

    return &it->second;
}

void FlowStatsCollector::AddFlow(FlowEntryPtr &flow, uint64_t time) {
    LogFlow(flow.get(), "ADD");
    FlowExportInfo *info = FindFlowExportInfo(flow);
    if (info != NULL) {
        /* Ignore if the entry is already present. One possible case why this
         * can happen is when there is SG change */
        return;
    }
    FlowExportInfo entry(time);

    flow_tree_.insert(make_pair(flow, entry));
}

void FlowStatsCollector::DeleteFlow(FlowEntryPtr &flow) {
    LogFlow(flow.get(), "DEL");
    FlowEntryTree::iterator it = flow_tree_.find(flow);
    if (it == flow_tree_.end())
        return;

    flow_tree_.erase(it);
}

void FlowStatsCollector::LogFlow(FlowEntry *flow, const std::string &op) {
    FlowInfo trace;
    tbb::mutex::scoped_lock mutex(flow->mutex());
    flow->FillFlowInfo(trace);
    FLOW_TRACE(Trace, op, trace);
}

/////////////////////////////////////////////////////////////////////////////
// FlowMamagentResponse message handler
/////////////////////////////////////////////////////////////////////////////
bool FlowStatsCollector::ResponseHandler(const FlowMgmtResponse &resp){
    switch (resp.event()) {
    case FlowMgmtResponse::FREE_FLOW_REF:
        break;

    default: {
        assert(0);
        break;
    }
    }
    return true;
}
