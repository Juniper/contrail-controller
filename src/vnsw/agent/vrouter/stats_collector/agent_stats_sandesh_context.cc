/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <net/if.h>

#include <uve/agent_uve_stats.h>
#include <vrouter/stats_collector/agent_stats_sandesh_context.h>
#include <vrouter/stats_collector/agent_stats_collector.h>
#include <cmn/agent_stats.h>
#include <oper/vrf.h>
#include <vrouter_types.h>

AgentStatsSandeshContext::AgentStatsSandeshContext(Agent *agent)
    : agent_(agent), marker_id_(-1) {
    AgentUveStats *uve = static_cast<AgentUveStats *>(agent_->uve());
    stats_ = uve->stats_manager();
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
        LOG(ERROR, "Error in reading Statistics from vrouter: " <<
            KSyncEntry::VrouterErrorToString(-code));
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

    StatsManager::InterfaceStats *stats =stats_->GetInterfaceStats(intf);

    if (!stats) {
        return;
    }
    if (intf->type() == Interface::VM_INTERFACE) {
        agent_->stats()->incr_in_pkts(req->get_vifr_ipackets() -
                                      stats->in_pkts);
        agent_->stats()->incr_in_bytes(req->get_vifr_ibytes() -
                                       stats->in_bytes);
        agent_->stats()->incr_out_pkts(req->get_vifr_opackets() -
                                       stats->out_pkts);
        agent_->stats()->incr_out_bytes(req->get_vifr_obytes() -
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
    const VrfEntry *vrf = agent_->vrf_table()->
                          FindVrfFromId(req->get_vsr_vrf());
    if (vrf == NULL) {
        if (req->get_vsr_vrf() != stats_->GetNamelessVrfId()) {
            vrf_present = false;
        } else {
            return;
        }
     }

    StatsManager::VrfStats *stats = stats_->GetVrfStats(req->get_vsr_vrf());
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
        stats->prev_fabric_composites = req->get_vsr_fabric_composites();
        stats->prev_encaps = req->get_vsr_encaps();
        stats->prev_l2_encaps = req->get_vsr_l2_encaps();

        stats->prev_gros = req->get_vsr_gros();
        stats->prev_diags = req->get_vsr_diags();
        stats->prev_encap_composites = req->get_vsr_encap_composites();
        stats->prev_evpn_composites = req->get_vsr_evpn_composites();
        stats->prev_vrf_translates = req->get_vsr_vrf_translates();
        stats->prev_vxlan_tunnels = req->get_vsr_vxlan_tunnels();
        stats->prev_arp_virtual_proxy = req->get_vsr_arp_virtual_proxy();
        stats->prev_arp_virtual_stitch = req->get_vsr_arp_virtual_stitch();
        stats->prev_arp_virtual_flood = req->get_vsr_arp_virtual_flood();
        stats->prev_arp_physical_stitch = req->get_vsr_arp_physical_stitch();
        stats->prev_arp_tor_proxy = req->get_vsr_arp_tor_proxy();
        stats->prev_arp_physical_flood = req->get_vsr_arp_physical_flood();
        stats->prev_l2_receives = req->get_vsr_l2_receives();
        stats->prev_uuc_floods = req->get_vsr_uuc_floods();
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
        stats->fabric_composites = req->get_vsr_fabric_composites() - 
                                   stats->prev_fabric_composites;

        stats->gros = req->get_vsr_gros() - stats->prev_gros;
        stats->diags = req->get_vsr_diags() - stats->prev_diags;
        stats->encap_composites = req->get_vsr_encap_composites() -
                                  stats->prev_encap_composites;
        stats->evpn_composites = req->get_vsr_evpn_composites() -
                                 stats->prev_evpn_composites;
        stats->vrf_translates = req->get_vsr_vrf_translates() -
                                stats->prev_vrf_translates;
        stats->vxlan_tunnels = req->get_vsr_vxlan_tunnels() -
                               stats->prev_vxlan_tunnels;
        stats->arp_virtual_proxy = req->get_vsr_arp_virtual_proxy() -
                                   stats->prev_arp_virtual_proxy;
        stats->arp_virtual_stitch = req->get_vsr_arp_virtual_stitch() -
                                    stats->prev_arp_virtual_stitch;
        stats->arp_virtual_flood = req->get_vsr_arp_virtual_flood() -
                                   stats->prev_arp_virtual_flood;
        stats->arp_physical_stitch = req->get_vsr_arp_physical_stitch() -
                                     stats->prev_arp_physical_stitch;
        stats->arp_tor_proxy = req->get_vsr_arp_tor_proxy() -
                               stats->prev_arp_tor_proxy;
        stats->arp_physical_flood = req->get_vsr_arp_physical_flood() -
                                    stats->prev_arp_physical_flood;
        stats->l2_receives = req->get_vsr_l2_receives() -
                             stats->prev_l2_receives;
        stats->uuc_floods = req->get_vsr_uuc_floods() -
                            stats->prev_uuc_floods;
        /* Update the last read values from Kernel in the following fields.
         * This will be used to update prev_* fields on receiving vrf delete
         * notification
         */
        if (req->get_vsr_vrf() != stats_->GetNamelessVrfId()) {
            stats->k_discards = req->get_vsr_discards();
            stats->k_resolves = req->get_vsr_resolves();
            stats->k_receives = req->get_vsr_receives();
            stats->k_udp_tunnels = req->get_vsr_udp_tunnels();
            stats->k_gre_mpls_tunnels = req->get_vsr_gre_mpls_tunnels();
            stats->k_udp_mpls_tunnels = req->get_vsr_udp_mpls_tunnels();
            stats->k_l2_mcast_composites = req->get_vsr_l2_mcast_composites();
            stats->k_ecmp_composites = req->get_vsr_ecmp_composites();
            stats->k_fabric_composites = req->get_vsr_fabric_composites();
            stats->k_encaps = req->get_vsr_encaps();
            stats->k_l2_encaps = req->get_vsr_l2_encaps();
            stats->k_gros = req->get_vsr_gros();
            stats->k_diags = req->get_vsr_diags();
            stats->k_encap_composites = req->get_vsr_encap_composites();
            stats->k_evpn_composites = req->get_vsr_evpn_composites();
            stats->k_vrf_translates = req->get_vsr_vrf_translates();
            stats->k_vxlan_tunnels = req->get_vsr_vxlan_tunnels();
            stats->k_arp_virtual_proxy = req->get_vsr_arp_virtual_proxy();
            stats->k_arp_virtual_stitch = req->get_vsr_arp_virtual_stitch();
            stats->k_arp_virtual_flood = req->get_vsr_arp_virtual_flood();
            stats->k_arp_physical_stitch = req->get_vsr_arp_physical_stitch();
            stats->k_arp_tor_proxy = req->get_vsr_arp_tor_proxy();
            stats->k_arp_physical_flood = req->get_vsr_arp_physical_flood();
            stats->k_l2_receives = req->get_vsr_l2_receives();
            stats->k_uuc_floods = req->get_vsr_uuc_floods();
        }
    }
}

void AgentStatsSandeshContext::DropStatsMsgHandler(vr_drop_stats_req *req) {
    stats_->set_drop_stats(*req);
}
