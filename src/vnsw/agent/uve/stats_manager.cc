/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/stats_manager.h>
#include <uve/vn_uve_table.h>
#include <uve/interface_uve_stats_table.h>
#include <oper/vm_interface.h>
#include <uve/agent_uve_stats.h>

StatsManager::StatsManager(Agent* agent)
    : vrf_listener_id_(DBTableBase::kInvalidId),
      intf_listener_id_(DBTableBase::kInvalidId), agent_(agent),
      request_queue_(agent->task_scheduler()->GetTaskId("Agent::Uve"), 0,
                     boost::bind(&StatsManager::RequestHandler, this, _1),
                     DEFAULT_FUVE_REQUEST_QUEUE_SIZE),
      timer_(TimerManager::CreateTimer
        (*(agent->event_manager())->io_service(), "IntfFlowStatsUpdateTimer",
         TaskScheduler::GetInstance()->GetTaskId(kTaskFlowStatsUpdate), 1)) {
    request_queue_.SetBounded(true);
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
    if (timer_) {
        timer_->Cancel();
        TimerManager::DeleteTimer(timer_);
    }
}

StatsManager::InterfaceStats::InterfaceStats()
    : name(""), speed(0), duplexity(0), in_pkts(0), in_bytes(0),
    out_pkts(0), out_bytes(0), prev_in_bytes(0), prev_out_bytes(0)
    , prev_5min_in_bytes(0), prev_5min_out_bytes(0), stats_time(0), flow_info(),
    added(), deleted(), drop_stats_received(false) {
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
    prev_out_bytes = out_bytes;
}

void StatsManager::InterfaceStats::GetDiffStats(uint64_t *in_b,
                                                uint64_t *out_b) const {
    *in_b = in_bytes - prev_in_bytes;
    *out_b = out_bytes - prev_out_bytes;
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

void StatsManager::AddFlow(const FlowUveStatsRequest *req) {
    FlowAceTree::iterator it = flow_ace_tree_.find(req->uuid());
    AgentUveStats *uve = static_cast<AgentUveStats *>(agent_->uve());
    InterfaceUveStatsTable *itable = static_cast<InterfaceUveStatsTable *>
        (uve->interface_uve_table());
    VnUveTable *vtable = static_cast<VnUveTable *>(uve->vn_uve_table());
    FlowUveFwPolicyInfo fw_info = req->fw_policy_info();
    if (fw_info.is_valid_) {
        assert(fw_info.added_);
    }
    if (it == flow_ace_tree_.end()) {
        InterfaceStats stats;
        itable->IncrInterfaceAceStats(req);
        bool fw_valid = itable->IncrInterfaceEndpointHits(req->interface(),
                                                          fw_info);
        fw_info.is_valid_ = fw_valid;
        vtable->IncrVnAceStats(req->vn_ace_info());
        FlowRuleMatchInfo info(req->interface(), req->sg_rule_uuid(), fw_info,
                               req->vn_ace_info());
        flow_ace_tree_.insert(FlowAcePair(req->uuid(), info));
    } else {
        FlowRuleMatchInfo &info = it->second;
        bool intf_changed = false;
        const string old_itf = info.interface;
        if (req->interface() != info.interface) {
            info.interface = req->interface();
            intf_changed = true;
        }
        if (intf_changed || (req->sg_rule_uuid() != info.sg_rule_uuid)) {
            itable->IncrInterfaceAceStats(req);
            info.sg_rule_uuid = req->sg_rule_uuid();
        }
        if (intf_changed ||
            (fw_info.is_valid_ && !info.IsFwPolicyInfoEqual(fw_info))) {
            /* When there is change either in interface-name or key of
             * Endpoint record, treat it as delete for old key and add for new
             * key.
             * (a) Increment added counter for new interface and new key
             * (b) Increment deleted counter for old interface and old key
             * (c) Update flow to point to new key
             */
            if (itable->IncrInterfaceEndpointHits(req->interface(), fw_info)) {
                /* Increment deleted counter for old interface and old key */
                FlowUveFwPolicyInfo old_info = info.fw_policy_info;
                old_info.added_ = false;
                itable->IncrInterfaceEndpointHits(old_itf, old_info);
                /* Update the flow with new key of endpoint after delete
                 * counter is incremented */
                info.fw_policy_info = fw_info;
            }
        }
        if (!info.IsVnAceInfoEqual(req->vn_ace_info())) {
            vtable->IncrVnAceStats(req->vn_ace_info());
            info.vn_ace_info = req->vn_ace_info();
        }
    }
}

void StatsManager::DeleteFlow(const FlowUveStatsRequest *req) {
    FlowAceTree::iterator it = flow_ace_tree_.find(req->uuid());
    if (it == flow_ace_tree_.end()) {
        return;
    }
    FlowRuleMatchInfo &old_fw_info = it->second;
    const FlowUveFwPolicyInfo &new_fw_info = req->fw_policy_info();
    /* Increment deleted counter only if add counter was incremented earlier
     * for this. This is determined by checking whether old and new keys are
     * equal */
    if (old_fw_info.IsFwPolicyInfoEqual(new_fw_info)) {
        AgentUveStats *uve = static_cast<AgentUveStats *>(agent_->uve());
        InterfaceUveStatsTable *itable = static_cast<InterfaceUveStatsTable *>
            (uve->interface_uve_table());
        itable->IncrInterfaceEndpointHits(req->interface(), new_fw_info);
    }
    flow_ace_tree_.erase(it);
}

void StatsManager::EnqueueEvent(const boost::shared_ptr<FlowUveStatsRequest>
                                &req) {
    request_queue_.Enqueue(req);
}

bool StatsManager::RequestHandler(boost::shared_ptr<FlowUveStatsRequest> req) {
    switch (req->event()) {
    case FlowUveStatsRequest::ADD_FLOW:
        AddFlow(req.get());
        break;
    case FlowUveStatsRequest::DELETE_FLOW:
        DeleteFlow(req.get());
        break;
    default:
        assert(0);
    }
    return true;
}

bool StatsManager::BuildFlowRate(AgentStats::FlowCounters &created,
                                 AgentStats::FlowCounters &aged,
                                 FlowRateComputeInfo &flow_info,
                                 VrouterFlowRate &flow_rate) const {
    uint64_t max_add_rate = 0, min_add_rate = 0;
    uint64_t max_del_rate = 0, min_del_rate = 0;
    uint64_t cur_time = UTCTimestampUsec();
    if (flow_info.prev_time_) {
        uint64_t diff_time = cur_time - flow_info.prev_time_;
        uint64_t diff_secs = diff_time / 1000000;
        if (diff_secs) {
            uint64_t created_flows = created.prev_flow_count -
                flow_info.prev_flow_created_;
            uint64_t aged_flows = aged.prev_flow_count -
                flow_info.prev_flow_aged_;
            //Flow setup/delete rate are always sent
            if (created_flows) {
                max_add_rate = created.max_flows_per_second;
                min_add_rate = created.min_flows_per_second;
                if (max_add_rate == AgentStats::kInvalidFlowCount) {
                    LOG(WARN, "Invalid max_flow_adds_per_second " << max_add_rate);
                    max_add_rate = 0;
                }
                if (min_add_rate == AgentStats::kInvalidFlowCount) {
                    LOG(WARN, "Invalid min_flow_adds_per_second " << min_add_rate);
                    min_add_rate = 0;
                }
            }
            if (aged_flows) {
                max_del_rate = aged.max_flows_per_second;
                min_del_rate = aged.min_flows_per_second;
                if (max_del_rate == AgentStats::kInvalidFlowCount) {
                    LOG(WARN, "Invalid max_flow_deletes_per_second " << max_del_rate);
                    max_del_rate = 0;
                }
                if (min_del_rate == AgentStats::kInvalidFlowCount) {
                    LOG(WARN, "Invalid min_flow_deletes_per_second " << min_del_rate);
                    min_del_rate = 0;
                }
            }

            flow_rate.set_added_flows(created_flows);
            flow_rate.set_max_flow_adds_per_second(max_add_rate);
            flow_rate.set_min_flow_adds_per_second(min_add_rate);
            flow_rate.set_deleted_flows(aged_flows);
            flow_rate.set_max_flow_deletes_per_second(max_del_rate);
            flow_rate.set_min_flow_deletes_per_second(min_del_rate);
            agent_->stats()->ResetFlowMinMaxStats(created);
            agent_->stats()->ResetFlowMinMaxStats(aged);
            flow_info.prev_time_ = cur_time;
            flow_info.prev_flow_created_ = created.prev_flow_count;
            flow_info.prev_flow_aged_ = aged.prev_flow_count;
            flow_rate.set_hold_flows(agent_->stats()->hold_flow_count());
            return true;
        }
    } else {
        flow_info.prev_time_ = cur_time;
    }
    return false;
}

bool StatsManager::FlowStatsUpdate() {
    InterfaceStatsTree::iterator it;
    it = if_stats_tree_.begin();
    while (it != if_stats_tree_.end()) {
        InterfaceStats &s = it->second;
        uint64_t created = 0, aged = 0;
        uint32_t dummy; //not used
        agent_->pkt()->get_flow_proto()->InterfaceFlowCount(it->first, &created,
                                                            &aged, &dummy);
        agent_->stats()->UpdateFlowMinMaxStats(created, s.added);
        agent_->stats()->UpdateFlowMinMaxStats(aged, s.deleted);
        ++it;
    }
    return true;
}

void StatsManager::InitDone() {
    timer_->Start(agent_->stats()->flow_stats_update_timeout(),
        boost::bind(&StatsManager::FlowStatsUpdate, this));
}

void StatsManager::BuildDropStats(const vr_drop_stats_req &req,
                                  AgentDropStats &ds) const {
    ds.set_ds_discard(req.get_vds_discard());
    uint64_t drop_pkts = ds.get_ds_discard();

    ds.set_ds_pull(req.get_vds_pull());
    drop_pkts += ds.get_ds_pull();

    ds.set_ds_invalid_if(req.get_vds_invalid_if());
    drop_pkts += ds.get_ds_invalid_if();

    ds.set_ds_invalid_arp(req.get_vds_invalid_arp());
    drop_pkts += ds.get_ds_invalid_arp();

    ds.set_ds_trap_no_if(req.get_vds_trap_no_if());
    drop_pkts += ds.get_ds_trap_no_if();

    ds.set_ds_nowhere_to_go(req.get_vds_nowhere_to_go());
    drop_pkts += ds.get_ds_nowhere_to_go();

    ds.set_ds_flow_queue_limit_exceeded(req.get_vds_flow_queue_limit_exceeded());
    drop_pkts += ds.get_ds_flow_queue_limit_exceeded();

    ds.set_ds_flow_no_memory(req.get_vds_flow_no_memory());
    drop_pkts += ds.get_ds_flow_no_memory();

    ds.set_ds_flow_invalid_protocol(req.get_vds_flow_invalid_protocol());
    drop_pkts += ds.get_ds_flow_invalid_protocol();

    ds.set_ds_flow_nat_no_rflow(req.get_vds_flow_nat_no_rflow());
    drop_pkts += ds.get_ds_flow_nat_no_rflow();

    ds.set_ds_flow_action_drop(req.get_vds_flow_action_drop());
    drop_pkts += ds.get_ds_flow_action_drop();

    ds.set_ds_flow_action_invalid(req.get_vds_flow_action_invalid());
    drop_pkts += ds.get_ds_flow_action_invalid();

    ds.set_ds_flow_unusable(req.get_vds_flow_unusable());
    drop_pkts += ds.get_ds_flow_unusable();

    ds.set_ds_flow_table_full(req.get_vds_flow_table_full());
    drop_pkts += ds.get_ds_flow_table_full();

    ds.set_ds_interface_tx_discard(req.get_vds_interface_tx_discard());
    drop_pkts += ds.get_ds_interface_tx_discard();

    ds.set_ds_interface_drop(req.get_vds_interface_drop());
    drop_pkts += ds.get_ds_interface_drop();

    ds.set_ds_duplicated(req.get_vds_duplicated());
    drop_pkts += ds.get_ds_duplicated();

    ds.set_ds_push(req.get_vds_push());
    drop_pkts += ds.get_ds_push();

    ds.set_ds_ttl_exceeded(req.get_vds_ttl_exceeded());
    drop_pkts += ds.get_ds_ttl_exceeded();

    ds.set_ds_invalid_nh(req.get_vds_invalid_nh());
    drop_pkts += ds.get_ds_invalid_nh();

    ds.set_ds_invalid_label(req.get_vds_invalid_label());
    drop_pkts += ds.get_ds_invalid_label();

    ds.set_ds_invalid_protocol(req.get_vds_invalid_protocol());
    drop_pkts += ds.get_ds_invalid_protocol();

    ds.set_ds_interface_rx_discard(req.get_vds_interface_rx_discard());
    drop_pkts += ds.get_ds_interface_rx_discard();

    ds.set_ds_invalid_mcast_source(req.get_vds_invalid_mcast_source());
    drop_pkts += ds.get_ds_invalid_mcast_source();

    ds.set_ds_head_alloc_fail(req.get_vds_head_alloc_fail());
    drop_pkts += ds.get_ds_head_alloc_fail();

    ds.set_ds_pcow_fail(req.get_vds_pcow_fail());
    drop_pkts += ds.get_ds_pcow_fail();

    ds.set_ds_mcast_clone_fail(req.get_vds_mcast_clone_fail());
    drop_pkts += ds.get_ds_mcast_clone_fail();

    ds.set_ds_mcast_df_bit(req.get_vds_mcast_df_bit());
    drop_pkts += ds.get_ds_mcast_df_bit();

    ds.set_ds_no_memory(req.get_vds_no_memory());
    drop_pkts += ds.get_ds_no_memory();

    ds.set_ds_rewrite_fail(req.get_vds_rewrite_fail());
    drop_pkts += ds.get_ds_rewrite_fail();

    ds.set_ds_misc(req.get_vds_misc());
    drop_pkts += ds.get_ds_misc();

    ds.set_ds_invalid_packet(req.get_vds_invalid_packet());
    drop_pkts += ds.get_ds_invalid_packet();

    ds.set_ds_cksum_err(req.get_vds_cksum_err());
    drop_pkts += ds.get_ds_cksum_err();

    ds.set_ds_no_fmd(req.get_vds_no_fmd());
    drop_pkts += ds.get_ds_no_fmd();

    ds.set_ds_invalid_vnid(req.get_vds_invalid_vnid());
    drop_pkts += ds.get_ds_invalid_vnid();

    ds.set_ds_frag_err(req.get_vds_frag_err());
    drop_pkts += ds.get_ds_frag_err();

    ds.set_ds_invalid_source(req.get_vds_invalid_source());
    drop_pkts += ds.get_ds_invalid_source();

    ds.set_ds_l2_no_route(req.get_vds_l2_no_route());
    drop_pkts += ds.get_ds_l2_no_route();

    ds.set_ds_fragment_queue_fail(req.get_vds_fragment_queue_fail());
    drop_pkts += ds.get_ds_fragment_queue_fail();

    ds.set_ds_vlan_fwd_tx(req.get_vds_vlan_fwd_tx());
    drop_pkts += ds.get_ds_vlan_fwd_tx();

    ds.set_ds_vlan_fwd_enq(req.get_vds_vlan_fwd_enq());
    drop_pkts += ds.get_ds_vlan_fwd_enq();

    ds.set_ds_drop_new_flow(req.get_vds_drop_new_flow());
    drop_pkts += ds.get_ds_drop_new_flow();

    ds.set_ds_flow_evict(req.get_vds_flow_evict());
    drop_pkts += ds.get_ds_flow_evict();

    ds.set_ds_trap_original(req.get_vds_trap_original());
    drop_pkts += ds.get_ds_trap_original();

    ds.set_ds_no_frag_entry(req.get_vds_no_frag_entry());
    drop_pkts += ds.get_ds_no_frag_entry();

    ds.set_ds_icmp_error(req.get_vds_icmp_error());
    drop_pkts += ds.get_ds_icmp_error();

    ds.set_ds_clone_fail(req.get_vds_clone_fail());
    drop_pkts += ds.get_ds_clone_fail();

    ds.set_ds_drop_pkts(drop_pkts);
}
