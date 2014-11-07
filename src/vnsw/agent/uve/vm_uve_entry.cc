/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/vm_uve_entry.h>
#include <uve/agent_uve.h>

VmUveEntry::VmUveEntry(Agent *agent)
    : VmUveEntryBase(agent), port_bitmap_() {
}

VmUveEntry::~VmUveEntry() {
}

bool VmUveEntry::FrameInterfaceStatsMsg(const VmInterface *vm_intf,
                                        VmInterfaceStats *s_intf) const {
    uint64_t in_band, out_band;
    if (vm_intf->cfg_name() == agent_->NullString()) {
        return false;
    }
    s_intf->set_name(vm_intf->cfg_name());

    const Interface *intf = static_cast<const Interface *>(vm_intf);
    AgentUve *uve = static_cast<AgentUve *>(agent_->uve());
    AgentStatsCollector::InterfaceStats *s =
        uve->agent_stats_collector()->GetInterfaceStats(intf);
    if (s == NULL) {
        return false;
    }

    /* Only diff since previous send needs to be sent as we export
     * stats via StatsOracle infra provided by analytics module */
    uint64_t in_b, in_p, out_b, out_p;
    s->GetDiffStats(&in_b, &in_p, &out_b, &out_p);
    s_intf->set_in_pkts(in_p);
    s_intf->set_in_bytes(in_b);
    s_intf->set_out_pkts(out_p);
    s_intf->set_out_bytes(out_b);
    in_band = GetVmPortBandwidth(s, true);
    out_band = GetVmPortBandwidth(s, false);
    s_intf->set_in_bw_usage(in_band);
    s_intf->set_out_bw_usage(out_band);
    s->stats_time = UTCTimestampUsec();

    /* Make sure that update of prev_in_bytes and prev_out_bytes are done only
     * after GetVmPortBandwidth is done for both directions as they get used
     * in those APIs. */
    s->UpdatePrevStats();

    return true;
}

void VmUveEntry::UpdatePortBitmap(uint8_t proto, uint16_t sport,
                                  uint16_t dport) {
    //Update VM bitmap
    port_bitmap_.AddPort(proto, sport, dport);

    //Update vm interfaces bitmap
    InterfaceSet::iterator it = interface_tree_.begin();
    while(it != interface_tree_.end()) {
        (*it).get()->port_bitmap_.AddPort(proto, sport, dport);
        ++it;
    }
}

bool VmUveEntry::SetVmPortBitmap(UveVirtualMachineAgent *uve) {
    bool changed = false;

    vector<uint32_t> tcp_sport;
    if (port_bitmap_.tcp_sport_.Sync(tcp_sport)) {
        uve->set_tcp_sport_bitmap(tcp_sport);
        changed = true;
    }

    vector<uint32_t> tcp_dport;
    if (port_bitmap_.tcp_dport_.Sync(tcp_dport)) {
        uve->set_tcp_dport_bitmap(tcp_dport);
        changed = true;
    }

    vector<uint32_t> udp_sport;
    if (port_bitmap_.udp_sport_.Sync(udp_sport)) {
        uve->set_udp_sport_bitmap(udp_sport);
        changed = true;
    }

    vector<uint32_t> udp_dport;
    if (port_bitmap_.udp_dport_.Sync(udp_dport)) {
        uve->set_udp_dport_bitmap(udp_dport);
        changed = true;
    }
    return changed;
}

bool VmUveEntry::FrameVmStatsMsg(const VmEntry* vm,
                                 UveVirtualMachineAgent *uve,
                                 VirtualMachineStats *stats_uve,
                                 bool *stats_uve_changed) {
    bool changed = false;
    uve->set_name(vm->GetCfgName());
    stats_uve->set_name(vm->GetCfgName());
    vector<VmInterfaceStats> s_intf_list;
    vector<VmInterfaceAgentBMap> if_bmap_list;
    vector<VmFloatingIPStats> s_fip_list;
    vector<VmFloatingIPStats> fip_list;
    vector<VmFloatingIPStatSamples> s_diff_list;
    vector<VmFloatingIPStatSamples> diff_list;

    InterfaceSet::iterator it = interface_tree_.begin();
    while(it != interface_tree_.end()) {
        VmInterfaceStats s_intf;
        const Interface *intf = (*it).get()->intf_;
        const VmInterface *vm_port =
            static_cast<const VmInterface *>(intf);
        if (FrameInterfaceStatsMsg(vm_port, &s_intf)) {
            s_intf_list.push_back(s_intf);
        }
        PortBucketBitmap map;
        VmInterfaceAgentBMap vmif_map;
        L4PortBitmap &port_bmap = (*it).get()->port_bitmap_;
        port_bmap.Encode(map);
        vmif_map.set_name(vm_port->cfg_name());
        vmif_map.set_port_bucket_bmap(map);
        if_bmap_list.push_back(vmif_map);

        fip_list.clear();
        diff_list.clear();
        if (FrameFipStatsMsg(vm_port, fip_list, diff_list)) {
            s_fip_list.insert(s_fip_list.end(), fip_list.begin(),
                              fip_list.end());
        }
        s_diff_list.insert(s_diff_list.end(), diff_list.begin(),
                           diff_list.end());

        ++it;
    }

    if (uve_info_.get_if_bmap_list() != if_bmap_list) {
        uve->set_if_bmap_list(if_bmap_list);
        uve_info_.set_if_bmap_list(if_bmap_list);
        changed = true;
    }

    if (SetVmPortBitmap(uve)) {
        changed = true;
    }

    if (UveVmFipStatsListChanged(s_fip_list)) {
        uve->set_fip_stats_list(s_fip_list);
        uve_info_.set_fip_stats_list(s_fip_list);
        changed = true;
    }
    /* VirtualMachineStats are sent always regardless of whether there are
     * any changes are not. */
    stats_uve->set_if_stats(s_intf_list);
    stats_uve->set_fip_stats(s_diff_list);
    *stats_uve_changed = true;

    return changed;
}

uint64_t VmUveEntry::GetVmPortBandwidth(AgentStatsCollector::InterfaceStats *s,
                                        bool dir_in) const {
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

void VmUveEntry::UpdateFloatingIpStats(const FipInfo &fip_info) {
    Interface *intf = InterfaceTable::GetInstance()->FindInterface
                              (fip_info.flow_->stats().fip_vm_port_id);
    UveInterfaceEntryPtr entry(new UveInterfaceEntry(intf));
    InterfaceSet::iterator intf_it = interface_tree_.find(entry);

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
        (*intf_it).get()->UpdateFloatingIpStats(fip_info);
    }
}

bool VmUveEntry::FrameFipStatsMsg(const VmInterface *vm_intf,
                          vector<VmFloatingIPStats> &fip_list,
                          vector<VmFloatingIPStatSamples> &diff_list) const {
    bool changed = false;
    UveInterfaceEntryPtr entry(new UveInterfaceEntry(vm_intf));
    InterfaceSet::iterator intf_it = interface_tree_.find(entry);

    if (intf_it != interface_tree_.end()) {
        changed = (*intf_it).get()->FillFloatingIpStats(fip_list, diff_list);
    }
    return changed;
}

bool VmUveEntry::UveVmFipStatsListChanged
    (const vector<VmFloatingIPStats> &new_list) const {
    if (new_list != uve_info_.get_fip_stats_list()) {
        return true;
    }
    return false;
}

VmUveEntryBase::FloatingIp * VmUveEntry::FipEntry(uint32_t fip, const string &vn,
                                              Interface *intf) {
    UveInterfaceEntryPtr entry(new UveInterfaceEntry(intf));
    InterfaceSet::iterator intf_it = interface_tree_.find(entry);

    assert (intf_it != interface_tree_.end());
    return (*intf_it).get()->FipEntry(fip, vn);
}

