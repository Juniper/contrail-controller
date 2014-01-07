/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/if_tun.h>

#include <boost/uuid/uuid_io.hpp>

#include <db/db.h>
#include <base/util.h>
#include <cmn/agent_cmn.h>

#include <oper/interface_common.h>
#include <oper/mirror_table.h>

#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_sock.h>

#include "vr_genetlink.h"
#include "vr_interface.h"
#include "vr_types.h"
#include "nl_util.h"

#include <ksync/flowtable_ksync.h>

#include <uve/stats_collector.h>
#include <uve/uve_init.h>
#include <uve/uve_client.h>
#include <uve/flow_stats.h>
#include <uve/inter_vn_stats.h>
#include <algorithm>
#include <pkt/flow_proto.h>
#include <ksync/ksync_init.h>

/* For ingress flows, change the SIP as Nat-IP instead of Native IP */
void FlowStatsCollector::SourceIpOverride(FlowEntry *flow, FlowDataIpv4 &s_flow) {
    FlowEntry *rev_flow = flow->reverse_flow_entry();
    if (flow->nat_flow() && s_flow.get_direction_ing() && rev_flow) {
        const FlowKey *nat_key = &rev_flow->key();
        if (flow->key().src.ipv4 != nat_key->dst.ipv4) {
            s_flow.set_sourceip(nat_key->dst.ipv4);
        }
    }
}

void FlowStatsCollector::FlowExport(FlowEntry *flow, uint64_t diff_bytes, uint64_t diff_pkts) {
    FlowDataIpv4   s_flow;
    SandeshLevel::type level = SandeshLevel::SYS_DEBUG;
    FlowStats &stats = flow->stats();

    s_flow.set_flowuuid(to_string(flow->flow_uuid()));
    s_flow.set_bytes(stats.bytes);
    s_flow.set_packets(stats.packets);
    s_flow.set_diff_bytes(diff_bytes);
    s_flow.set_diff_packets(diff_pkts);

    s_flow.set_sourceip(flow->key().src.ipv4);
    s_flow.set_destip(flow->key().dst.ipv4);
    s_flow.set_protocol(flow->key().protocol);
    s_flow.set_sport(flow->key().src_port);
    s_flow.set_dport(flow->key().dst_port);
    s_flow.set_sourcevn(flow->data.source_vn);
    s_flow.set_destvn(flow->data.dest_vn);

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
    FlowEntry *rev_flow = flow->reverse_flow_entry();
    if (rev_flow) {
        s_flow.set_reverse_uuid(to_string(rev_flow->flow_uuid()));
    }

    // Flow setup and teardown messages are sent with higher priority
    if (!stats.exported) {
        s_flow.set_setup_time(stats.setup_time);
        stats.exported = true;
        level = SandeshLevel::SYS_ERR;
    }
    if (stats.teardown_time) {
        s_flow.set_teardown_time(stats.teardown_time);
        stats.exported = false;
        level = SandeshLevel::SYS_ERR;
    }

    if (flow->local_flow()) {
        /* For local flows we need to send two flow log messages.
         * 1. With direction as ingress
         * 2. With direction as egress
         * For local flows we have already sent flow log above with
         * direction as ingress. We are sending flow log below with
         * direction as egress.
         */
        s_flow.set_direction_ing(1);
        SourceIpOverride(flow, s_flow);
        FLOW_DATA_IPV4_OBJECT_LOG("", level, s_flow);
        s_flow.set_direction_ing(0);
        //Export local flow of egress direction with a different UUID even when
        //the flow is same. Required for analytics module to query flows
        //irrespective of direction.
        s_flow.set_flowuuid(to_string(flow->egress_uuid()));
        FLOW_DATA_IPV4_OBJECT_LOG("", level, s_flow);
    } else {
        if (flow->ingress()) {
            s_flow.set_direction_ing(1);
            SourceIpOverride(flow, s_flow);
        } else {
            s_flow.set_direction_ing(0);
        }
        FLOW_DATA_IPV4_OBJECT_LOG("", level, s_flow);
    }

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
    if (diff_time < GetFlowAgeTime()) {
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
        stats = &(entry->stats());
        it++;
        assert(entry);
        deleted = false;

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
                if (ShouldBeAged(&(reverse_flow->stats()), k_flow_rev, curr_time)) {
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
                AgentUve::GetInstance()->GetInterVnStatsCollector()->UpdateVnStats(entry, 
                                                                    diff_bytes, diff_pkts);
                stats->bytes = bytes;
                stats->packets = packets;
                stats->last_modified_time = curr_time;
                FlowExport(entry, diff_bytes, diff_pkts);
            }
        }

        if ((!deleted) && entry->short_flow()) {
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

    uint32_t age_time_millisec = GetFlowAgeTime() / 1000;

    if (total_flows > 0) {
        flow_timer_interval = std::min((age_time_millisec * flow_multiplier_)/total_flows, 1000U);
    } else {
        flow_timer_interval = flow_default_interval_;
    }

    if (age_time_millisec > 0) {
        flow_count_per_pass_ = std::max((flow_timer_interval * total_flows)/age_time_millisec, 100U);
    } else {
        flow_count_per_pass_ = 100U;
    }
    SetExpiryTime(flow_timer_interval);
    return true;
}
