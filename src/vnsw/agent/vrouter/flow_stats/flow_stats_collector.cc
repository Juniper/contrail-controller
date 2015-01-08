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
#include <algorithm>
#include <pkt/flow_proto.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/flow_stats/flow_stats_interval_types.h>

FlowStatsCollector::FlowStatsCollector(boost::asio::io_service &io, int intvl,
                                       uint32_t flow_cache_timeout,
                                       AgentUveBase *uve) :
        StatsCollector(TaskScheduler::GetInstance()->GetTaskId
                       ("Agent::StatsCollector"),
                       StatsCollector::FlowStatsCollector,
                       io, intvl, "Flow stats collector"),
        agent_uve_(uve), delete_short_flow_(true) {
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

bool FlowStatsCollector::ShouldBeAged(FlowStats *stats,
                                      const vr_flow_entry *k_flow,
                                      uint64_t curr_time) {
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
    VmUveEntry::FipInfo fip_info;

    /* Ignore Non-Floating-IP flow */
    if (!flow->stats().fip ||
        flow->stats().fip_vm_port_id == Interface::kInvalidIndex) {
        return;
    }

    VmUveTable *vm_table = static_cast<VmUveTable *>
        (agent_uve_->vm_uve_table());
    VmUveEntry *entry = vm_table->InterfaceIdToVmUveEntry
        (flow->stats().fip_vm_port_id);
    if (entry == NULL) {
        return;
    }

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

    entry->UpdateFloatingIpStats(fip_info);
}

VmUveEntry::FloatingIp *FlowStatsCollector::ReverseFlowFip
    (const FlowEntry *flow) {
    uint32_t fip = flow->reverse_flow_fip();
    const string &vn = flow->data().source_vn;
    uint32_t intf_id = flow->reverse_flow_vmport_id();
    Interface *intf = InterfaceTable::GetInstance()->FindInterface(intf_id);

    VmUveTable *vm_table = static_cast<VmUveTable *>
        (agent_uve_->vm_uve_table());
    VmUveEntry *entry = vm_table->InterfaceIdToVmUveEntry(intf_id);
    if (entry != NULL) {
        return entry->FipEntry(fip, vn, intf);
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
        if (ShouldBeAged(stats, k_flow, curr_time)) {
            // If reverse_flow is present, wait till both are aged
            if (reverse_flow) {
                const vr_flow_entry *k_flow_rev;
                k_flow_rev = ksync_obj->GetKernelFlowEntry
                    (reverse_flow->flow_handle(), false);
                if (ShouldBeAged(&(reverse_flow->stats_), k_flow_rev,
                                 curr_time)) {
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
                flow_obj->FlowExport(entry, diff_bytes, diff_pkts);
            } else if (!stats->exported && !entry->deleted()) {
                /* export flow (reverse) for which traffic is not seen yet. */
                flow_obj->FlowExport(entry, 0, 0);
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
