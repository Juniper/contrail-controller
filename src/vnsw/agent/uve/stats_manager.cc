/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/stats_manager.h>

StatsManager::StatsManager(Agent* agent)
    : vrf_listener_id_(DBTableBase::kInvalidId),
    intf_listener_id_(DBTableBase::kInvalidId), agent_(agent) {
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
        stats->prev_l3_mcast_composites = stats->k_l3_mcast_composites;
        stats->prev_multi_proto_composites = stats->k_multi_proto_composites;
        stats->prev_ecmp_composites = stats->k_ecmp_composites;
        stats->prev_l2_encaps = stats->k_l2_encaps;
        stats->prev_encaps = stats->k_encaps;
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
    bool set_state = false, reset_state = false;

    DBState *state = static_cast<DBState *>
                      (e->GetState(part->parent(), intf_listener_id_));
    switch (intf->type()) {
    case Interface::VM_INTERFACE:
        if (e->IsDeleted() || ((intf->ipv4_active() == false) &&
                                (intf->l2_active() == false))) {
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
}

StatsManager::InterfaceStats::InterfaceStats()
    : name(""), speed(0), duplexity(0), in_pkts(0), in_bytes(0),
    out_pkts(0), out_bytes(0), prev_in_bytes(0),
    prev_out_bytes(0), prev_in_pkts(0), prev_out_pkts(0),
    prev_5min_in_bytes(0), prev_5min_out_bytes(0),
    prev_10min_in_bytes(0), prev_10min_out_bytes(10), stats_time(0) {
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
    : name(""), discards(0), resolves(0), receives(0),
    udp_tunnels(0), udp_mpls_tunnels(0), gre_mpls_tunnels(0),
    ecmp_composites(0), l3_mcast_composites(0),
    l2_mcast_composites(0), fabric_composites(0),
    multi_proto_composites(0), encaps(0), l2_encaps(0),
    prev_discards(0), prev_resolves(0), prev_receives(0),
    prev_udp_tunnels(0), prev_udp_mpls_tunnels(0),
    prev_gre_mpls_tunnels(0), prev_encaps(0),
    prev_ecmp_composites(0), prev_l3_mcast_composites(0),
    prev_l2_mcast_composites(0), prev_fabric_composites(0),
    prev_multi_proto_composites(0), prev_l2_encaps(0),
    k_discards(0), k_resolves(0), k_receives(0),
    k_gre_mpls_tunnels(0), k_encaps(0),
    k_ecmp_composites(0), k_l3_mcast_composites(0),
    k_l2_mcast_composites(0), k_fabric_composites(0),
    k_multi_proto_composites(0), k_l2_encaps(0) {
}
