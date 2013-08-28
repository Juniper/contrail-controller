/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <net/if.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/if_tun.h>

#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <db/db.h>
#include <base/util.h>
#include <cmn/agent_cmn.h>

#include <oper/interface.h>
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

#include <pkt/pkt_flow.h>

/* For ingress flows, change the SIP as Nat-IP instead of Native IP */
void FlowStatsCollector::SourceIpOverride(FlowEntry *flow, FlowDataIpv4 &s_flow) {
    FlowEntry *rev_flow = flow->data.reverse_flow.get();
    if (flow->nat && s_flow.get_direction_ing() && rev_flow) {
        FlowKey *nat_key = &rev_flow->key;
        if (flow->key.src.ipv4 != nat_key->dst.ipv4) {
            s_flow.set_sourceip(nat_key->dst.ipv4);
        }
    }
}

void FlowStatsCollector::FlowExport(FlowEntry *flow, uint64_t diff_bytes, uint64_t diff_pkts) {
    FlowDataIpv4   s_flow;

    s_flow.set_flowuuid(to_string(flow->flow_uuid));
    s_flow.set_bytes(flow->data.bytes);
    s_flow.set_packets(flow->data.packets);
    s_flow.set_diff_bytes(diff_bytes);
    s_flow.set_diff_packets(diff_pkts);

    s_flow.set_sourceip(flow->key.src.ipv4);
    s_flow.set_destip(flow->key.dst.ipv4);
    s_flow.set_protocol(flow->key.protocol);
    s_flow.set_sport(flow->key.src_port);
    s_flow.set_dport(flow->key.dst_port);
    s_flow.set_sourcevn(flow->data.source_vn);
    s_flow.set_destvn(flow->data.dest_vn);

    if (flow->intf_in != Interface::kInvalidIndex) {
        Interface *intf = InterfaceTable::FindInterface(flow->intf_in);
        if (intf && intf->GetType() == Interface::VMPORT) {
            VmPortInterface *vm_port = static_cast<VmPortInterface *>(intf);
            const VmEntry *vm = vm_port->GetVmEntry();
            if (vm) {
                s_flow.set_vm(vm->GetCfgName());
            }
        }
    }
    FlowEntry *rev_flow = flow->data.reverse_flow.get();
    if (rev_flow) {
        s_flow.set_reverse_uuid(to_string(rev_flow->flow_uuid));
    }

    s_flow.set_setup_time(flow->setup_time);
    if (flow->teardown_time)
        s_flow.set_teardown_time(flow->teardown_time);

    if (flow->local_flow) {
        /* For local flows we need to send two flow log messages.
         * 1. With direction as ingress
         * 2. With direction as egress
         * For local flows we have already sent flow log above with
         * direction as ingress. We are sending flow log below with
         * direction as egress.
         */
        s_flow.set_direction_ing(1);
        SourceIpOverride(flow, s_flow);
        FLOW_DATA_IPV4_OBJECT_SEND(s_flow);
        s_flow.set_direction_ing(0);
        //Export local flow of egress direction with a different UUID even when
        //the flow is same. Required for analytics module to query flows
        //irrespective of direction.
        s_flow.set_flowuuid(to_string(flow->egress_uuid));
        FLOW_DATA_IPV4_OBJECT_SEND(s_flow);
    } else {
        if (flow->data.ingress) {
            s_flow.set_direction_ing(1);
            SourceIpOverride(flow, s_flow);
        } else {
            s_flow.set_direction_ing(0);
        }
        FLOW_DATA_IPV4_OBJECT_SEND(s_flow);
    }

}

bool FlowStatsCollector::ShouldBeAged(FlowEntry *entry,
                                      const vr_flow_entry *k_flow,
                                      uint64_t curr_time) {
    if (k_flow != NULL) {
        if (entry->data.bytes < k_flow->fe_stats.flow_bytes &&
            entry->data.packets < k_flow->fe_stats.flow_packets) {
            return false;
        }
    }

    uint64_t diff_time = curr_time - entry->last_modified_time;
    if (diff_time < GetFlowAgeTime()) {
        return false;
    }
    return true;
}

bool FlowStatsCollector::Run() {
    FlowTable::FlowEntryMap::iterator it;
    FlowEntry *entry = NULL, *reverse_flow;
    uint32_t count = 0;
    bool key_updation_reqd = true, deleted;
    uint64_t diff_bytes, diff_pkts;
    FlowTable *flow_obj = FlowTable::GetFlowTableObject();
   
    run_counter_++;
    if (!flow_obj->Size()) {
        return true;
    }
    uint64_t curr_time = UTCTimestampUsec();
    it = flow_obj->flow_entry_map_.upper_bound(flow_iteration_key_);
    if (it == flow_obj->flow_entry_map_.end()) {
        it = flow_obj->flow_entry_map_.begin();
    }

    while (it != flow_obj->flow_entry_map_.end()) {
        entry = it->second;
        it++;
        assert(entry);
        deleted = false;

        const vr_flow_entry *k_flow = FlowTableKSyncObject::GetKernelFlowEntry
            (entry->flow_handle, false);
        // Can the flow be aged?
        if (ShouldBeAged(entry, k_flow, curr_time)) {
            reverse_flow = entry->data.reverse_flow.get();
            // If reverse_flow is present, wait till both are aged
            if (reverse_flow) {
                const vr_flow_entry *k_flow_rev;
                k_flow_rev = FlowTableKSyncObject::GetKernelFlowEntry
                    (reverse_flow->flow_handle, false);
                if (ShouldBeAged(reverse_flow, k_flow_rev, curr_time)) {
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
            FlowTable::GetFlowTableObject()->DeleteRevFlow
                (entry->key, reverse_flow != NULL? true : false);
            if (reverse_flow) {
                count++;
            }
        }

        if (deleted == false && k_flow) {
            if (entry->data.bytes != k_flow->fe_stats.flow_bytes) {
                diff_bytes = k_flow->fe_stats.flow_bytes - entry->data.bytes;
                diff_pkts = k_flow->fe_stats.flow_packets - entry->data.packets;
                //Update Inter-VN stats
                AgentUve::GetInterVnStatsCollector()->UpdateVnStats(entry, 
                                                                    diff_bytes, diff_pkts);
                entry->data.bytes = k_flow->fe_stats.flow_bytes;
                entry->data.packets = k_flow->fe_stats.flow_packets;
                entry->last_modified_time = curr_time;
                FlowExport(entry, diff_bytes, diff_pkts);
            }
        }

        if ((!deleted) && entry->ShortFlow()) {
            deleted = true;
            FlowTable::GetFlowTableObject()->DeleteRevFlow(entry->key, false);
        }

        count++;
        if (count == FlowCountPerPass) {
            if (it != flow_obj->flow_entry_map_.end()) {
                flow_iteration_key_ = entry->key;
                key_updation_reqd = false;
            }
            break;
        }
    }

    /* Reset the iteration key if we are done with all the elements */
    if (key_updation_reqd) {
        flow_iteration_key_.Reset();
    }
    return true;
}
