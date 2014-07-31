/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <net/if.h>
#if defined(__linux__)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/if_tun.h>
#endif

#include <uve/agent_stats_sandesh_context.h>
#include <uve/agent_stats_collector.h>
#include <pkt/agent_stats.h>
#include <oper/vrf.h>

AgentStatsSandeshContext::AgentStatsSandeshContext(AgentStatsCollector *col) 
    : collector_(col), marker_id_(-1) {
}

AgentStatsSandeshContext::~AgentStatsSandeshContext() {
}

bool AgentStatsSandeshContext::MoreData() const { 
    return (response_code_ & VR_MESSAGE_DUMP_INCOMPLETE); 
}

int AgentStatsSandeshContext::VrResponseMsgHandler(vr_response *r) {
    int code = r->get_resp_code();
   
    set_response_code(code);
    if (code > 0) {
       /* Positive value indicates the number of records returned in the 
        * response from Kernel. Kernel response includes vr_response along
        * with actual response.
        */
        return 0;
    }
    
    if (code < 0) {
        LOG(ERROR, "Error: " << strerror(-code));
        return -code;
    }

    return 0;
}

void AgentStatsSandeshContext::IfMsgHandler(vr_interface_req *req) {
    set_marker_id(req->get_vifr_idx());
    const Interface *intf = InterfaceTable::GetInstance()->FindInterface
                                                    (req->get_vifr_idx());
    if (intf == NULL) {
        return;
     }
 
    AgentStatsCollector::InterfaceStats *stats = 
                                        collector_->GetInterfaceStats(intf);
    if (!stats) {
        return;
    }
    if (intf->type() == Interface::VM_INTERFACE) {
        collector_->agent()->stats()->incr_in_pkts(req->get_vifr_ipackets() - 
                                                 stats->in_pkts);
        collector_->agent()->stats()->incr_in_bytes(req->get_vifr_ibytes() - 
                                                  stats->in_bytes);
        collector_->agent()->stats()->incr_out_pkts(req->get_vifr_opackets() - 
                                                  stats->out_pkts);
        collector_->agent()->stats()->incr_out_bytes(req->get_vifr_obytes() - 
                                                   stats->out_bytes);
    }

    stats->UpdateStats(req->get_vifr_ibytes(), req->get_vifr_ipackets(),
                       req->get_vifr_obytes(), req->get_vifr_opackets());
    stats->speed = req->get_vifr_speed();
    stats->duplexity = req->get_vifr_duplex();
}

void AgentStatsSandeshContext::VrfStatsMsgHandler(vr_vrf_stats_req *req) {
    set_marker_id(req->get_vsr_vrf());
    bool vrf_present = true;
    const VrfEntry *vrf = collector_->agent()->vrf_table()->
                          FindVrfFromId(req->get_vsr_vrf());
    if (vrf == NULL) {
        if (req->get_vsr_vrf() != collector_->GetNamelessVrfId()) { 
            vrf_present = false;
        } else {
            return;
        }
     }
 
    AgentStatsCollector::VrfStats *stats = collector_->GetVrfStats
                                                        (req->get_vsr_vrf());
    if (!stats) {
        LOG(DEBUG, "Vrf not present in stats tree <" << req->get_vsr_vrf() 
            << ">");
        return;
    }
    if (!vrf_present) {
        stats->prev_discards = req->get_vsr_discards();
        stats->prev_resolves = req->get_vsr_resolves();
        stats->prev_receives = req->get_vsr_receives();
        stats->prev_udp_tunnels = req->get_vsr_udp_tunnels();
        stats->prev_udp_mpls_tunnels = req->get_vsr_udp_mpls_tunnels();
        stats->prev_gre_mpls_tunnels = req->get_vsr_gre_mpls_tunnels();
        stats->prev_ecmp_composites = req->get_vsr_ecmp_composites();
        stats->prev_l2_mcast_composites = req->get_vsr_l2_mcast_composites();
        stats->prev_l3_mcast_composites = req->get_vsr_l3_mcast_composites();
        stats->prev_fabric_composites = req->get_vsr_fabric_composites();
        stats->prev_multi_proto_composites = 
                                        req->get_vsr_multi_proto_composites();
        stats->prev_encaps = req->get_vsr_encaps();
        stats->prev_l2_encaps = req->get_vsr_l2_encaps();
    } else {
        stats->discards = req->get_vsr_discards() - stats->prev_discards;
        stats->resolves = req->get_vsr_resolves() - stats->prev_resolves;
        stats->receives = req->get_vsr_receives() - stats->prev_receives;
        stats->udp_tunnels = req->get_vsr_udp_tunnels() - 
                             stats->prev_udp_tunnels;
        stats->gre_mpls_tunnels = req->get_vsr_gre_mpls_tunnels() - 
                                  stats->prev_gre_mpls_tunnels;
        stats->udp_mpls_tunnels = req->get_vsr_udp_mpls_tunnels() - 
                                  stats->prev_udp_mpls_tunnels;
        stats->encaps = req->get_vsr_encaps() - stats->prev_encaps;
        stats->l2_encaps = req->get_vsr_l2_encaps() - stats->prev_l2_encaps;
        stats->ecmp_composites = req->get_vsr_ecmp_composites() - 
                                 stats->prev_ecmp_composites;
        stats->l2_mcast_composites = req->get_vsr_l2_mcast_composites() - 
                                     stats->prev_l2_mcast_composites;
        stats->l3_mcast_composites = req->get_vsr_l3_mcast_composites() - 
                                     stats->prev_l3_mcast_composites;
        stats->fabric_composites = req->get_vsr_fabric_composites() - 
                                   stats->prev_fabric_composites;
        stats->multi_proto_composites = req->get_vsr_multi_proto_composites() -
                                        stats->prev_multi_proto_composites;
        
        /* Update the last read values from Kernel in the following fields.
         * This will be used to update prev_* fields on receiving vrf delete
         * notification
         */
        if (req->get_vsr_vrf() != collector_->GetNamelessVrfId()) { 
            stats->k_discards = req->get_vsr_discards();
            stats->k_resolves = req->get_vsr_resolves();
            stats->k_receives = req->get_vsr_receives();
            stats->k_udp_tunnels = req->get_vsr_udp_tunnels();
            stats->k_gre_mpls_tunnels = req->get_vsr_gre_mpls_tunnels();
            stats->k_udp_mpls_tunnels = req->get_vsr_udp_mpls_tunnels();
            stats->k_l2_mcast_composites = req->get_vsr_l2_mcast_composites();
            stats->k_l3_mcast_composites = req->get_vsr_l3_mcast_composites();
            stats->k_ecmp_composites = req->get_vsr_ecmp_composites();
            stats->k_fabric_composites = req->get_vsr_fabric_composites();
            stats->k_multi_proto_composites = 
                                    req->get_vsr_multi_proto_composites();
            stats->k_encaps = req->get_vsr_encaps();
            stats->k_l2_encaps = req->get_vsr_l2_encaps();
        }

    }
}

void AgentStatsSandeshContext::DropStatsMsgHandler(vr_drop_stats_req *req) {
    collector_->set_drop_stats(*req);
}
