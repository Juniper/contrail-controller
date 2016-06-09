/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/stats_manager.h>
#include <uve/agent_uve_stats.h>
#include <uve/vn_uve_table.h>
#include <uve/interface_uve_stats_table.h>
#include <oper/vm_interface.h>

StatsManager::StatsManager(Agent* agent)
    : vrf_listener_id_(DBTableBase::kInvalidId),
      intf_listener_id_(DBTableBase::kInvalidId), agent_(agent),
      request_queue_(agent->task_scheduler()->GetTaskId("Agent::Uve"), 0,
                     boost::bind(&StatsManager::RequestHandler, this, _1)) {

    AddNamelessVrfStatsEntry();
}

StatsManager::~StatsManager() {
}

void StatsManager::AddInterfaceStatsEntry(const Interface *intf) {
    InterfaceStatsTree::iterator it;
    it = if_stats_tree_.find(intf);
    if (it == if_stats_tree_.end()) {
        InterfaceStats stats;
        stats.name = intf->name();
        if_stats_tree_.insert(InterfaceStatsPair(intf, stats));
    }
}

void StatsManager::DelInterfaceStatsEntry(const Interface *intf) {
    InterfaceStatsTree::iterator it;
    it = if_stats_tree_.find(intf);
    if (it != if_stats_tree_.end()) {
        if_stats_tree_.erase(it);
    }
}

void StatsManager::AddNamelessVrfStatsEntry() {
    VrfStats stats;
    stats.name = GetNamelessVrf();
    vrf_stats_tree_.insert(VrfStatsPair(GetNamelessVrfId(), stats));
}

void StatsManager::AddUpdateVrfStatsEntry(const VrfEntry *vrf) {
    StatsManager::VrfIdToVrfStatsTree::iterator it;
    it = vrf_stats_tree_.find(vrf->vrf_id());
    if (it == vrf_stats_tree_.end()) {
        VrfStats stats;
        stats.name = vrf->GetName();
        vrf_stats_tree_.insert(VrfStatsPair(vrf->vrf_id(), stats));
    } else {
        /* Vrf could be deleted in agent oper DB but not in Kernel. To handle
         * this case we maintain vrfstats object in StatsManager even
         * when vrf is absent in agent oper DB.  Since vrf could get deleted and
         * re-added we need to update the name in vrfstats object.
         */
        VrfStats *stats = &it->second;
        stats->name = vrf->GetName();
    }
}

void StatsManager::DelVrfStatsEntry(const VrfEntry *vrf) {
    StatsManager::VrfIdToVrfStatsTree::iterator it;
    it = vrf_stats_tree_.find(vrf->vrf_id());
    if (it != vrf_stats_tree_.end()) {
        VrfStats *stats = &it->second;
        stats->prev_discards = stats->k_discards;
        stats->prev_resolves = stats->k_resolves;
        stats->prev_receives = stats->k_receives;
        stats->prev_udp_mpls_tunnels = stats->k_udp_mpls_tunnels;
        stats->prev_udp_tunnels = stats->k_udp_tunnels;
        stats->prev_gre_mpls_tunnels = stats->k_gre_mpls_tunnels;
        stats->prev_fabric_composites = stats->k_fabric_composites;
        stats->prev_l2_mcast_composites = stats->k_l2_mcast_composites;
        stats->prev_ecmp_composites = stats->k_ecmp_composites;
        stats->prev_l2_encaps = stats->k_l2_encaps;
        stats->prev_encaps = stats->k_encaps;
        stats->prev_gros = stats->gros;
        stats->prev_diags = stats->diags;
        stats->prev_encap_composites = stats->encap_composites;
        stats->prev_evpn_composites = stats->evpn_composites;
        stats->prev_vrf_translates = stats->vrf_translates;
        stats->prev_vxlan_tunnels = stats->vxlan_tunnels;
        stats->prev_arp_virtual_proxy = stats->arp_virtual_proxy;
        stats->prev_arp_virtual_stitch = stats->arp_virtual_stitch;
        stats->prev_arp_virtual_flood = stats->arp_virtual_flood;
        stats->prev_arp_physical_stitch = stats->arp_physical_stitch;
        stats->prev_arp_tor_proxy = stats->arp_tor_proxy;
        stats->prev_arp_physical_flood = stats->arp_physical_flood;
        stats->prev_l2_receives = stats->l2_receives;
        stats->prev_uuc_floods = stats->uuc_floods;
    }
}

StatsManager::InterfaceStats *StatsManager::GetInterfaceStats
    (const Interface *intf) {
    StatsManager::InterfaceStatsTree::iterator it;

    it = if_stats_tree_.find(intf);
    if (it == if_stats_tree_.end()) {
        return NULL;
    }

    return &it->second;
}

StatsManager::VrfStats *StatsManager::GetVrfStats(int vrf_id) {
    StatsManager::VrfIdToVrfStatsTree::iterator it;
    it = vrf_stats_tree_.find(vrf_id);
    if (it == vrf_stats_tree_.end()) {
        return NULL;
    }

    return &it->second;
}

void StatsManager::InterfaceNotify(DBTablePartBase *part, DBEntryBase *e) {
    const Interface *intf = static_cast<const Interface *>(e);
    const VmInterface *vmi = NULL;
    bool set_state = false, reset_state = false;

    DBState *state = static_cast<DBState *>
                      (e->GetState(part->parent(), intf_listener_id_));
    switch (intf->type()) {
    case Interface::VM_INTERFACE:
        vmi = static_cast<const VmInterface *>(intf);
        if (e->IsDeleted() || (vmi->IsUveActive() == false)) {
            if (state) {
                reset_state = true;
            }
        } else {
            if (!state) {
                set_state = true;
            }
        }
        break;
    default:
        if (e->IsDeleted()) {
            if (state) {
                reset_state = true;
            }
        } else {
            if (!state) {
                set_state = true;
            }
        }
    }
    if (set_state) {
        state = new DBState();
        e->SetState(part->parent(), intf_listener_id_, state);
        AddInterfaceStatsEntry(intf);
    } else if (reset_state) {
        DelInterfaceStatsEntry(intf);
        delete state;
        e->ClearState(part->parent(), intf_listener_id_);
    }
    return;
}

void StatsManager::VrfNotify(DBTablePartBase *part, DBEntryBase *e) {
    const VrfEntry *vrf = static_cast<const VrfEntry *>(e);
    DBState *state = static_cast<DBState *>
                      (e->GetState(part->parent(), vrf_listener_id_));
    if (e->IsDeleted()) {
        if (state) {
            DelVrfStatsEntry(vrf);
            delete state;
            e->ClearState(part->parent(), vrf_listener_id_);
        }
    } else {
        if (!state) {
            state = new DBState();
            e->SetState(part->parent(), vrf_listener_id_, state);
        }
        AddUpdateVrfStatsEntry(vrf);
    }
}

void StatsManager::RegisterDBClients() {
    InterfaceTable *intf_table = agent_->interface_table();
    intf_listener_id_ = intf_table->Register
        (boost::bind(&StatsManager::InterfaceNotify, this, _1, _2));

    VrfTable *vrf_table = agent_->vrf_table();
    vrf_listener_id_ = vrf_table->Register
        (boost::bind(&StatsManager::VrfNotify, this, _1, _2));
}

void StatsManager::Shutdown(void) {
    agent_->vrf_table()->Unregister(vrf_listener_id_);
    agent_->interface_table()->Unregister(intf_listener_id_);
    request_queue_.Shutdown();
}

StatsManager::InterfaceStats::InterfaceStats()
    : name(""), speed(0), duplexity(0), in_pkts(0), in_bytes(0),
    out_pkts(0), out_bytes(0), prev_in_bytes(0),
    prev_out_bytes(0), prev_in_pkts(0), prev_out_pkts(0),
    prev_5min_in_bytes(0), prev_5min_out_bytes(0), stats_time(0) {
}

void StatsManager::InterfaceStats::UpdateStats
    (uint64_t in_b, uint64_t in_p, uint64_t out_b, uint64_t out_p) {
    in_bytes = in_b;
    in_pkts = in_p;
    out_bytes = out_b;
    out_pkts = out_p;
}

void StatsManager::InterfaceStats::UpdatePrevStats() {
    prev_in_bytes = in_bytes;
    prev_in_pkts = in_pkts;
    prev_out_bytes = out_bytes;
    prev_out_pkts = out_pkts;
}

void StatsManager::InterfaceStats::GetDiffStats
    (uint64_t *in_b, uint64_t *in_p, uint64_t *out_b, uint64_t *out_p) {
    *in_b = in_bytes - prev_in_bytes;
    *in_p = in_pkts - prev_in_pkts;
    *out_b = out_bytes - prev_out_bytes;
    *out_p = out_pkts - prev_out_pkts;
}

StatsManager::VrfStats::VrfStats()
    : name(""), discards(0), resolves(0), receives(0), udp_tunnels(0),
    udp_mpls_tunnels(0), gre_mpls_tunnels(0), ecmp_composites(0),
    l2_mcast_composites(0), fabric_composites(0), encaps(0), l2_encaps(0),
    gros(0), diags(0), encap_composites(0), evpn_composites(0),
    vrf_translates(0), vxlan_tunnels(0), arp_virtual_proxy(0),
    arp_virtual_stitch(0), arp_virtual_flood(0), arp_physical_stitch(0),
    arp_tor_proxy(0), arp_physical_flood(0), l2_receives(0), uuc_floods(0),
    prev_discards(0), prev_resolves(0), prev_receives(0), prev_udp_tunnels(0),
    prev_udp_mpls_tunnels(0), prev_gre_mpls_tunnels(0), prev_ecmp_composites(0),
    prev_l2_mcast_composites(0), prev_fabric_composites(0), prev_encaps(0),
    prev_l2_encaps(0), prev_gros(0), prev_diags(0), prev_encap_composites(0),
    prev_evpn_composites(0), prev_vrf_translates(0), prev_vxlan_tunnels(0),
    prev_arp_virtual_proxy(0), prev_arp_virtual_stitch(0),
    prev_arp_virtual_flood(0), prev_arp_physical_stitch(0),
    prev_arp_tor_proxy(0), prev_arp_physical_flood(0), prev_l2_receives(0),
    prev_uuc_floods(0), k_discards(0), k_resolves(0), k_receives(0),
    k_udp_tunnels(0), k_udp_mpls_tunnels(0), k_gre_mpls_tunnels(0),
    k_ecmp_composites(0), k_l2_mcast_composites(0), k_fabric_composites(0),
    k_encaps(0), k_l2_encaps(0), k_gros(0), k_diags(0), k_encap_composites(0),
    k_evpn_composites(0), k_vrf_translates(0), k_vxlan_tunnels(0),
    k_arp_virtual_proxy(0), k_arp_virtual_stitch(0), k_arp_virtual_flood(0),
    k_arp_physical_stitch(0), k_arp_tor_proxy(0), k_arp_physical_flood(0),
    k_l2_receives(0), k_uuc_floods(0) {
}

void StatsManager::AddFlow(const FlowAceStatsRequest *req) {
    FlowAceTree::iterator it = flow_ace_tree_.find(req->uuid());
    AgentUveStats *uve = static_cast<AgentUveStats *>(agent_->uve());
    InterfaceUveStatsTable *itable = static_cast<InterfaceUveStatsTable *>
        (uve->interface_uve_table());
    VnUveTable *vtable = static_cast<VnUveTable *>(uve->vn_uve_table());
    if (it == flow_ace_tree_.end()) {
        InterfaceStats stats;
        FlowRuleMatchInfo info(req->interface(), req->sg_rule_uuid(), req->vn(),
                               req->nw_ace_uuid());
        flow_ace_tree_.insert(FlowAcePair(req->uuid(), info));
        itable->IncrInterfaceAceStats(req->interface(), req->sg_rule_uuid());
        vtable->IncrVnAceStats(req->vn(), req->nw_ace_uuid());
    } else {
        FlowRuleMatchInfo &info = it->second;
        if ((req->interface() != info.interface) ||
            (req->sg_rule_uuid() != info.sg_rule_uuid)) {
            itable->IncrInterfaceAceStats(req->interface(),
                                          req->sg_rule_uuid());
            info.interface = req->interface();
            info.sg_rule_uuid = req->sg_rule_uuid();
        }
        if ((req->vn() != info.vn) ||
            (req->nw_ace_uuid() != info.nw_ace_uuid)) {
            vtable->IncrVnAceStats(req->vn(), req->nw_ace_uuid());
            info.vn = req->vn();
            info.nw_ace_uuid = req->nw_ace_uuid();
        }
    }
}

void StatsManager::DeleteFlow(const FlowAceStatsRequest *req) {
    FlowAceTree::iterator it = flow_ace_tree_.find(req->uuid());
    if (it == flow_ace_tree_.end()) {
        return;
    }
    flow_ace_tree_.erase(it);
}

void StatsManager::EnqueueEvent(const boost::shared_ptr<FlowAceStatsRequest>
                                &req) {
    request_queue_.Enqueue(req);
}

bool StatsManager::RequestHandler(boost::shared_ptr<FlowAceStatsRequest> req) {
    switch (req->event()) {
    case FlowAceStatsRequest::ADD_FLOW:
        AddFlow(req.get());
        break;
    case FlowAceStatsRequest::DELETE_FLOW:
        DeleteFlow(req.get());
        break;
    default:
        assert(0);
    }
    return true;
}
