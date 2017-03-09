/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface_common.h>
#include <uve/interface_uve_stats_table.h>
#include <uve/agent_uve_stats.h>

InterfaceUveStatsTable::InterfaceUveStatsTable(Agent *agent,
                                               uint32_t default_intvl)
    : InterfaceUveTable(agent, default_intvl) {
}

InterfaceUveStatsTable::~InterfaceUveStatsTable() {
}

bool InterfaceUveStatsTable::FrameInterfaceStatsMsg(UveInterfaceEntry* entry,
                                            UveVMInterfaceAgent *uve) const {
    uint64_t in_band = 0, out_band = 0;
    bool diff_fip_list_non_zero = false;
    VmInterfaceStats if_stats;
    vector<VmFloatingIPStats> agg_fip_list;
    vector<VmFloatingIPStats> diff_fip_list;

    const VmInterface *vm_intf = entry->intf_;
    assert(!entry->deleted_);
    if (vm_intf->cfg_name().empty()) {
        return false;
    }
    uve->set_name(vm_intf->cfg_name());
    entry->SetVnVmInfo(uve);

    const Interface *intf = static_cast<const Interface *>(vm_intf);
    AgentUveStats *agent_uve = static_cast<AgentUveStats *>(agent_->uve());
    StatsManager::InterfaceStats *s =
        agent_uve->stats_manager()->GetInterfaceStats(intf);
    if (s == NULL) {
        return false;
    }

    AgentDropStats ds;
    agent_uve->stats_manager()->BuildDropStats(s->drop_stats, ds);
    uve->set_raw_drop_stats(ds);

    /* Send aggregate interface stats always */
    if_stats.set_in_pkts(s->in_pkts);
    if_stats.set_in_bytes(s->in_bytes);
    if_stats.set_out_pkts(s->out_pkts);
    if_stats.set_out_bytes(s->out_bytes);
    uve->set_raw_if_stats(if_stats);

    /* Compute bandwidth only if there is change in statistics */
    uint64_t in_b, out_b;
    s->GetDiffStats(&in_b, &out_b);
    if ((in_b != 0) || (out_b != 0)) {
        in_band = GetVmPortBandwidth(s, true);
        out_band = GetVmPortBandwidth(s, false);
    }

    if (entry->InBandChanged(in_band)) {
        uve->set_in_bw_usage(in_band);
        entry->uve_info_.set_in_bw_usage(in_band);
    }
    if (entry->OutBandChanged(out_band)) {
        uve->set_out_bw_usage(out_band);
        entry->uve_info_.set_out_bw_usage(out_band);
    }
    s->stats_time = UTCTimestampUsec();

    /* Make sure that update of prev_in_bytes and prev_out_bytes are done only
     * after GetVmPortBandwidth is done for both directions as they get used
     * in those APIs. */
    s->UpdatePrevStats();

    PortBucketBitmap map;
    L4PortBitmap &port_bmap = entry->port_bitmap_;
    port_bmap.Encode(map);
    if (entry->PortBitmapChanged(map)) {
        uve->set_port_bucket_bmap(map);
        entry->uve_info_.set_port_bucket_bmap(map);
    }

    FrameFipStatsMsg(vm_intf, agg_fip_list, diff_fip_list,
                     diff_fip_list_non_zero);
    if (entry->FipAggStatsChanged(agg_fip_list)) {
        uve->set_fip_agg_stats(agg_fip_list);
        entry->uve_info_.set_fip_agg_stats(agg_fip_list);
    }
    /* Diff stats need not be sent if the value of the stats is 0.
     * If any of the entry in diff_fip_list has non-zero stats, then
     * diff_fip_list_non_zero is expected to be true */
    if (diff_fip_list_non_zero) {
        uve->set_fip_diff_stats(diff_fip_list);
    }

    VrouterFlowRate flow_rate;
    uint64_t created = 0, aged = 0;
    uint32_t active_flows = 0;
    agent_->pkt()->get_flow_proto()->InterfaceFlowCount(vm_intf, &created,
                                                        &aged, &active_flows);
    bool built = agent_uve->stats_manager()->BuildFlowRate(s->added, s->deleted,
                                                           s->flow_info,
                                                           flow_rate);
    if (built) {
        flow_rate.set_active_flows(active_flows);
        uve->set_flow_rate(flow_rate);
    }

    return true;
}

void InterfaceUveStatsTable::SendInterfaceStatsMsg(UveInterfaceEntry* entry) {
    if (entry->deleted_) {
        return;
    }
    UveVMInterfaceAgent uve;

    bool send = FrameInterfaceStatsMsg(entry, &uve);
    if (send) {
        DispatchInterfaceMsg(uve);
    }
}

void InterfaceUveStatsTable::SendInterfaceStats(void) {
    InterfaceMap::iterator it = interface_tree_.begin();
    while (it != interface_tree_.end()) {
        UveInterfaceEntry* entry = it->second.get();
        SendInterfaceStatsMsg(entry);
        it++;
    }
}

uint64_t InterfaceUveStatsTable::GetVmPortBandwidth
    (StatsManager::InterfaceStats *s, bool dir_in) const {
    if (s->stats_time == 0) {
        return 0;
    }
    uint64_t bits;
    if (dir_in) {
        bits = (s->in_bytes - s->prev_in_bytes) * 8;
    } else {
        bits = (s->out_bytes - s->prev_out_bytes) * 8;
    }
    uint64_t cur_time = UTCTimestampUsec();
    uint64_t b_intvl = agent_->uve()->bandwidth_intvl();
    uint64_t diff_seconds = (cur_time - s->stats_time) / b_intvl;
    if (diff_seconds == 0) {
        return 0;
    }
    return bits/diff_seconds;
}

void InterfaceUveStatsTable::UpdateFloatingIpStats(const FipInfo &fip_info) {
    Interface *intf = dynamic_cast<Interface *>
        (agent_->interface_table()->FindActiveEntry(&fip_info.fip_vmi_));
    if (intf == NULL) {
        return;
    }
    tbb::mutex::scoped_lock lock(interface_tree_mutex_);
    VmInterface *vmi = static_cast<VmInterface *>(intf);
    InterfaceMap::iterator intf_it = interface_tree_.find(vmi->cfg_name());

    /*
     *  1. VM interface with floating-ip becomes active
     *  2. Flow is created on this interface and interface floating ip info is
     *     stored in flow record
     *  3. VM Interface is disassociated from VM
     *  4. VM Interface info is removed from interface_tree_ because of
     *     disassociation
     *  5. FlowStats collection task initiates export of flow stats
     *  6. Since interface is absent in interface_tree_ we cannot update
     *     stats in this case
     */
    if (intf_it != interface_tree_.end()) {
        UveInterfaceEntry *entry = intf_it->second.get();
        entry->UpdateFloatingIpStats(fip_info);
    }
}

bool InterfaceUveStatsTable::FrameFipStatsMsg(const VmInterface *itf,
                          vector<VmFloatingIPStats> &fip_list,
                          vector<VmFloatingIPStats> &diff_list,
                          bool &diff_list_send) const {
    bool changed = false;
    diff_list_send = false;
    InterfaceMap::const_iterator it = interface_tree_.find(itf->cfg_name());

    if (it != interface_tree_.end()) {
        UveInterfaceEntry *entry = it->second.get();
        changed = entry->FillFloatingIpStats(fip_list, diff_list,
                                             diff_list_send);
    }
    return changed;
}


void InterfaceUveStatsTable::UpdatePortBitmap
    (const string &name, uint8_t proto, uint16_t sport, uint16_t dport) {
    tbb::mutex::scoped_lock lock(interface_tree_mutex_);
    InterfaceMap::const_iterator it = interface_tree_.find(name);

    if (it != interface_tree_.end()) {
        UveInterfaceEntry *entry = it->second.get();
        entry->UpdatePortBitmap(proto, sport, dport);
    }
}

InterfaceUveTable::FloatingIp * InterfaceUveStatsTable::FipEntry
    (uint32_t fip, const string &vn, Interface *intf) {
    tbb::mutex::scoped_lock lock(interface_tree_mutex_);
    VmInterface *vmi = static_cast<VmInterface *>(intf);
    InterfaceMap::iterator intf_it = interface_tree_.find(vmi->cfg_name());

    assert (intf_it != interface_tree_.end());
    UveInterfaceEntry *entry = intf_it->second.get();
    return entry->FipEntry(fip, vn);
}

void InterfaceUveStatsTable::IncrInterfaceAceStats(const std::string &itf,
                                                   const std::string &u) {
    if (itf.empty() || u.empty()) {
        return;
    }
    InterfaceMap::iterator intf_it = interface_tree_.find(itf);

    if (intf_it != interface_tree_.end()) {
        UveInterfaceEntry *entry = intf_it->second.get();
        entry->UpdateInterfaceAceStats(u);
    }
}

void InterfaceUveStatsTable::SendInterfaceAceStats(const string &name,
                                                   UveInterfaceEntry *entry) {
    UveVMInterfaceAgent uve;
    if (entry->FrameInterfaceAceStatsMsg(name, &uve)) {
        DispatchInterfaceMsg(uve);
    }
}
