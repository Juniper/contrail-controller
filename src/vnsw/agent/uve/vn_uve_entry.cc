/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/vn_uve_entry.h>
#include <uve/agent_uve.h>

VnUveEntry::VnUveEntry(Agent *agent, const VnEntry *vn) 
    : agent_(agent), vn_(vn), port_bitmap_(), uve_info_(), 
      interface_tree_(), vm_tree_(), inter_vn_stats_(), mutex_(), 
      prev_stats_update_time_(0), prev_in_bytes_(0), prev_out_bytes_(0) { 
}

VnUveEntry::VnUveEntry(Agent *agent) 
    : agent_(agent), vn_(NULL), port_bitmap_(), 
      uve_info_(), interface_tree_(),  vm_tree_(), 
      prev_stats_update_time_(0), prev_in_bytes_(0), 
      prev_out_bytes_(0) { 
}

VnUveEntry::~VnUveEntry() {
}

void VnUveEntry::VmAdd(const string &vm) {
    if (vm != agent_->NullString()) {
        VmSet::iterator it = vm_tree_.find(vm);
        if (it == vm_tree_.end()) {
            vm_tree_.insert(vm);
        }
    }
}

void VnUveEntry::VmDelete(const string &vm) {
    if (vm != agent_->NullString()) {
        VmSet::iterator vm_it = vm_tree_.find(vm);
        if (vm_it != vm_tree_.end()) {
            vm_tree_.erase(vm_it);
        }
    }
}

void VnUveEntry::InterfaceAdd(const Interface *intf) {
    InterfaceSet::iterator it = interface_tree_.find(intf);
    if (it == interface_tree_.end()) {
        interface_tree_.insert(intf);
    }
}

void VnUveEntry::InterfaceDelete(const Interface *intf) {
    InterfaceSet::iterator intf_it = interface_tree_.find(intf);
    if (intf_it != interface_tree_.end()) {
        interface_tree_.erase(intf_it);
    }
}

void VnUveEntry::UpdatePortBitmap(uint8_t proto, uint16_t sport,
                                  uint16_t dport) {
    port_bitmap_.AddPort(proto, sport, dport);
}

void VnUveEntry::UpdateInterVnStats(const string &dst_vn, uint64_t bytes, 
                                    uint64_t pkts, bool outgoing) {
    tbb::mutex::scoped_lock lock(mutex_);
    VnStatsPtr key(new VnStats(dst_vn, 0, 0, false));
    VnStatsSet::iterator stats_it = inter_vn_stats_.find(key);
    if (stats_it == inter_vn_stats_.end()) {
        VnStatsPtr stats(new VnStats(dst_vn, bytes, pkts, outgoing));
        inter_vn_stats_.insert(stats);
    } else {
        VnStatsPtr stats_ptr(*stats_it);
        VnStats *stats = stats_ptr.get();
        if (outgoing) {
            stats->out_bytes_ += bytes;
            stats->out_pkts_ += pkts;
        } else {
            stats->in_bytes_ += bytes;
            stats->in_pkts_ += pkts;
        }
    }
}

void VnUveEntry::ClearInterVnStats() {
    /* Remove all the elements of map entry value which is a set */
    VnStatsSet::iterator stats_it = inter_vn_stats_.begin();
    VnStatsSet::iterator del_it;
    while(stats_it != inter_vn_stats_.end()) {
        del_it = stats_it;
        stats_it++;
        inter_vn_stats_.erase(del_it);
    }
}

bool VnUveEntry::BuildInterfaceVmList(UveVirtualNetworkAgent &s_vn) {
    bool changed = false;

    s_vn.set_name(vn_->GetName());
    vector<string> vm_list;

    VmSet::iterator vm_it = vm_tree_.begin();
    while (vm_it != vm_tree_.end()) {
        vm_list.push_back(*vm_it);
        ++vm_it;
    }
    if (UveVnVmListChanged(vm_list)) {
        s_vn.set_virtualmachine_list(vm_list);
        uve_info_.set_virtualmachine_list(vm_list);
        changed = true;
    }

    vector<string> intf_list;
    InterfaceSet::iterator intf_it = interface_tree_.begin();
    while (intf_it != interface_tree_.end()) {
        const Interface *intf = *intf_it;
        const VmInterface *vm_port = 
            static_cast<const VmInterface *>(intf);
        intf_list.push_back(vm_port->cfg_name());
        intf_it++;
    }
    if (UveVnInterfaceListChanged(intf_list)) {
        s_vn.set_interface_list(intf_list);
        uve_info_.set_interface_list(intf_list);
        changed = true;
    }
   
    return changed;
}

bool VnUveEntry::SetVnPortBitmap(UveVirtualNetworkAgent &uve) {
    bool changed = false;

    vector<uint32_t> tcp_sport;
    if (port_bitmap_.tcp_sport_.Sync(tcp_sport)) {
        uve.set_tcp_sport_bitmap(tcp_sport);
        changed = true;
    }

    vector<uint32_t> tcp_dport;
    if (port_bitmap_.tcp_dport_.Sync(tcp_dport)) {
        uve.set_tcp_dport_bitmap(tcp_dport);
        changed = true;
    }

    vector<uint32_t> udp_sport;
    if (port_bitmap_.udp_sport_.Sync(udp_sport)) {
        uve.set_udp_sport_bitmap(udp_sport);
        changed = true;
    }

    vector<uint32_t> udp_dport;
    if (port_bitmap_.udp_dport_.Sync(udp_dport)) {
        uve.set_udp_dport_bitmap(udp_dport);
        changed = true;
    }
    return changed;
}

bool VnUveEntry::UveVnAclChanged(const string &name) const { 
    if (!uve_info_.__isset.acl) {
        return true;
    }
    if (name.compare(uve_info_.get_acl()) != 0) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveVnMirrorAclChanged(const string &name) const  {
    if (!uve_info_.__isset.mirror_acl) {
        return true;
    }
    if (name.compare(uve_info_.get_mirror_acl()) != 0) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveVnInterfaceListChanged(const vector<string> &new_list) 
                                           const {
    if (!uve_info_.__isset.interface_list) {
        return true;
    }
    if (new_list != uve_info_.get_interface_list()) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveVnVmListChanged(const vector<string> &new_list) const {
    if (!uve_info_.__isset.virtualmachine_list) {
        return true;
    }
    if (new_list != uve_info_.get_virtualmachine_list()) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveVnAclRuleCountChanged(int32_t size) const {
    if (!uve_info_.__isset.total_acl_rules) {
        return true;
    }
    if (size != uve_info_.get_total_acl_rules()) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveVnFipCountChanged(int32_t size) const {
    if (!uve_info_.__isset.associated_fip_count) {
        return true;
    }
    if (size != uve_info_.get_associated_fip_count()) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveVnInterfaceInStatsChanged(uint64_t bytes, uint64_t pkts) 
                                              const {
    if (!uve_info_.__isset.in_bytes || !uve_info_.__isset.in_tpkts) {
        return true;
    }
    uint64_t bytes2 = (uint64_t)uve_info_.get_in_bytes();
    uint64_t  pkts2 = (uint64_t)uve_info_.get_in_tpkts();

    if ((bytes != bytes2) || (pkts != pkts2)) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveVnInterfaceOutStatsChanged(uint64_t bytes, uint64_t pkts) 
                                               const {
    if (!uve_info_.__isset.out_bytes || !uve_info_.__isset.out_tpkts) {
        return true;
    }
    uint64_t bytes2 = (uint64_t)uve_info_.get_out_bytes();
    uint64_t  pkts2 = (uint64_t)uve_info_.get_out_tpkts();

    if ((bytes != bytes2) || (pkts != pkts2)) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveVnInBandChanged(uint64_t in_band) const {
    if (!uve_info_.__isset.in_bandwidth_usage) {
        return true;
    }
    if (in_band != uve_info_.get_in_bandwidth_usage()) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveVnOutBandChanged(uint64_t out_band) const {
    if (!uve_info_.__isset.out_bandwidth_usage) {
        return true;
    }
    if (out_band != uve_info_.get_out_bandwidth_usage()) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveVnVrfStatsChanged(const vector<UveVrfStats> &vlist) const {
    if (!uve_info_.__isset.vrf_stats_list) {
        return true;
    }
    if (vlist != uve_info_.get_vrf_stats_list()) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveInterVnStatsChanged(const vector<InterVnStats> &new_list) 
                                        const {
    if (!uve_info_.__isset.vn_stats) {
        return true;
    }
    if (new_list != uve_info_.get_vn_stats()) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveInterVnInStatsChanged(const vector<UveInterVnStats> 
                                          &new_list) const {
    if (!uve_info_.__isset.in_stats) {
        return true;
    }
    if (new_list != uve_info_.get_in_stats()) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveVnInFlowCountChanged(uint32_t size) { 
    if (!uve_info_.__isset.ingress_flow_count) {
        return true;
    }
    if (size != uve_info_.get_ingress_flow_count()) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveVnOutFlowCountChanged(uint32_t size) {
    if (!uve_info_.__isset.egress_flow_count) {
        return true;
    }
    if (size != uve_info_.get_egress_flow_count()) {
        return true;
    }
    return false;
}

bool VnUveEntry::UveInterVnOutStatsChanged(const vector<UveInterVnStats> 
                                           &new_list) const {
    if (!uve_info_.__isset.out_stats) {
        return true;
    }
    if (new_list != uve_info_.get_out_stats()) {
        return true;
    }
    return false;
}

bool VnUveEntry::UpdateVnFlowCount(const VnEntry *vn, 
                                   UveVirtualNetworkAgent &s_vn) {
    bool changed = false;
    uint32_t in_count, out_count;
    agent_->pkt()->flow_table()->VnFlowCounters(vn, &in_count, &out_count);
    if (UveVnInFlowCountChanged(in_count)) {
        s_vn.set_ingress_flow_count(in_count);
        uve_info_.set_ingress_flow_count(in_count);
        changed = true;
    }
    if (UveVnOutFlowCountChanged(out_count)) {
        s_vn.set_egress_flow_count(out_count);
        uve_info_.set_egress_flow_count(out_count);
        changed = true;
    }
    return changed;
}

bool VnUveEntry::UpdateVnFipCount(int count, UveVirtualNetworkAgent &s_vn) {
    if (UveVnFipCountChanged(count)) {
        s_vn.set_associated_fip_count(count);
        uve_info_.set_associated_fip_count(count);
        return true;
    }
    return false;
}

bool VnUveEntry::PopulateInterVnStats(UveVirtualNetworkAgent &s_vn) {
    bool changed = false;
    /* Aggregate/total current stats are sent in the following fields */
    vector<UveInterVnStats> in_list;
    vector<UveInterVnStats> out_list;
    /* Only diff since previous dispatch, is sent as part of the following 
     * list */
    vector<InterVnStats> vn_stats_list;
   
    {
        tbb::mutex::scoped_lock lock(mutex_);
        VnStatsSet::iterator it = inter_vn_stats_.begin();
        VnStats *stats;
        VnStatsPtr stats_ptr;
        while (it != inter_vn_stats_.end()) {
            stats_ptr = *it;
            stats = stats_ptr.get();
            UveInterVnStats uve_stats;
            uve_stats.set_other_vn(stats->dst_vn_);

            uve_stats.set_tpkts(stats->in_pkts_);
            uve_stats.set_bytes(stats->in_bytes_);
            in_list.push_back(uve_stats);

            uve_stats.set_tpkts(stats->out_pkts_);
            uve_stats.set_bytes(stats->out_bytes_);
            out_list.push_back(uve_stats);

            InterVnStats diff_stats;
            diff_stats.set_other_vn(stats->dst_vn_);
            diff_stats.set_vrouter(agent_->host_name());
            diff_stats.set_in_tpkts(stats->in_pkts_ - stats->prev_in_pkts_);
            diff_stats.set_in_bytes(stats->in_bytes_ - stats->prev_in_bytes_);
            diff_stats.set_out_tpkts(stats->out_pkts_ - stats->prev_out_pkts_);
            diff_stats.set_out_bytes(stats->out_bytes_ - 
                                     stats->prev_out_bytes_);
            vn_stats_list.push_back(diff_stats);

            stats->prev_in_pkts_ = stats->in_pkts_;
            stats->prev_in_bytes_ = stats->in_bytes_;
            stats->prev_out_pkts_ = stats->out_pkts_;
            stats->prev_out_bytes_ = stats->out_bytes_;
            it++;
        }
    }
    if (!in_list.empty()) {
        if (UveInterVnInStatsChanged(in_list)) {
            s_vn.set_in_stats(in_list);
            uve_info_.set_in_stats(in_list);
            changed = true;
        }
    }
    if (!out_list.empty()) {
        if (UveInterVnOutStatsChanged(out_list)) {
            s_vn.set_out_stats(out_list);
            uve_info_.set_out_stats(out_list);
            changed = true;
        }
    }
    if (!vn_stats_list.empty()) {
        if (UveInterVnStatsChanged(vn_stats_list)) {
            s_vn.set_vn_stats(vn_stats_list);
            uve_info_.set_vn_stats(vn_stats_list);
            changed = true;
        }
    }
    return changed;
}

bool VnUveEntry::FillVrfStats(int vrf_id, UveVirtualNetworkAgent &s_vn) {
    bool changed = false;
    UveVrfStats vrf_stats;
    vector<UveVrfStats> vlist;

    AgentStatsCollector::VrfStats *s = 
        agent_->uve()->agent_stats_collector()->GetVrfStats(vrf_id);
    if (s != NULL) {
        vrf_stats.set_name(s->name);
        vrf_stats.set_discards(s->discards);
        vrf_stats.set_resolves(s->resolves);
        vrf_stats.set_receives(s->receives);
        vrf_stats.set_udp_tunnels(s->udp_tunnels);
        vrf_stats.set_udp_mpls_tunnels(s->udp_mpls_tunnels);
        vrf_stats.set_gre_mpls_tunnels(s->gre_mpls_tunnels);
        vrf_stats.set_ecmp_composites(s->ecmp_composites);
        vrf_stats.set_l2_mcast_composites(s->l2_mcast_composites);
        vrf_stats.set_l3_mcast_composites(s->l3_mcast_composites);
        vrf_stats.set_multi_proto_composites(s->multi_proto_composites);
        vrf_stats.set_fabric_composites(s->fabric_composites);
        vrf_stats.set_encaps(s->encaps);
        vrf_stats.set_l2_encaps(s->l2_encaps);
        vlist.push_back(vrf_stats);
        if (UveVnVrfStatsChanged(vlist)) {
            s_vn.set_vrf_stats_list(vlist);
            uve_info_.set_vrf_stats_list(vlist);
            changed = true;
        }
    }
    return changed;
}

bool VnUveEntry::UpdateVrfStats(const VnEntry *vn, 
                                UveVirtualNetworkAgent &s_vn) {
    bool changed = false;
    VrfEntry *vrf = vn->GetVrf();
    if (vrf) {
        changed = FillVrfStats(vrf->vrf_id(), s_vn);
    } else {
        vector<UveVrfStats> vlist;
        if (UveVnVrfStatsChanged(vlist)) {
            s_vn.set_vrf_stats_list(vlist);
            uve_info_.set_vrf_stats_list(vlist);
            changed = true;
        }
    }

    return changed;
}

bool VnUveEntry::FrameVnMsg(const VnEntry *vn, UveVirtualNetworkAgent &uve) {
    bool changed = false;
    uve.set_name(vn->GetName());
    
    string acl_name;
    int acl_rule_count;
    if (vn->GetAcl()) {
        acl_name = vn->GetAcl()->GetName();
        acl_rule_count = vn->GetAcl()->Size();
    } else {
        acl_name = agent_->NullString();
        acl_rule_count = 0;
    }

    if (UveVnAclChanged(acl_name)) {  
        uve.set_acl(acl_name);
        uve_info_.set_acl(acl_name);
        changed = true;
    }
    
    if (UveVnAclRuleCountChanged(acl_rule_count)) {
        uve.set_total_acl_rules(acl_rule_count);
        uve_info_.set_total_acl_rules(acl_rule_count);
        changed = true;
    }

    if (vn->GetMirrorCfgAcl()) {
        acl_name = vn->GetMirrorCfgAcl()->GetName();
    } else {
        acl_name = agent_->NullString();
    }
    if (UveVnMirrorAclChanged(acl_name)) {
        uve.set_mirror_acl(acl_name);
        uve_info_.set_mirror_acl(acl_name);
        changed = true;
    }

    if (SetVnPortBitmap(uve)) {
        changed = true;
    }

    return changed;
}

bool VnUveEntry::FrameVnStatsMsg(const VnEntry *vn, 
                                 UveVirtualNetworkAgent &uve,
                                 bool only_vrf_stats) {
    bool changed = false;
    uve.set_name(vn->GetName());

    uint64_t in_pkts = 0;
    uint64_t in_bytes = 0;
    uint64_t out_pkts = 0;
    uint64_t out_bytes = 0;

    if (UpdateVrfStats(vn, uve)) {
        changed = true;
    }

    if (only_vrf_stats) {
        return changed;
    }

    int fip_count = 0;
    InterfaceSet::iterator it = interface_tree_.begin();
    while (it != interface_tree_.end()) {
        const Interface *intf = *it;
        ++it;

        const VmInterface *vm_port = static_cast<const VmInterface *>(intf);
        fip_count += vm_port->GetFloatingIpCount();

        const AgentStatsCollector::InterfaceStats *s = 
            agent_->uve()->agent_stats_collector()->GetInterfaceStats(intf);
        if (s == NULL) {
            continue;
        }
        in_pkts += s->in_pkts;
        in_bytes += s->in_bytes;
        out_pkts += s->out_pkts;
        out_bytes += s->out_bytes;
    }
    
    uint64_t diff_in_bytes = 0;
    if (UveVnInterfaceInStatsChanged(in_bytes, in_pkts)) {
        uve.set_in_tpkts(in_pkts);
        uve.set_in_bytes(in_bytes);
        uve_info_.set_in_tpkts(in_pkts);
        uve_info_.set_in_bytes(in_bytes);
        changed = true;
    }

    uint64_t diff_out_bytes = 0;
    if (UveVnInterfaceOutStatsChanged(out_bytes, out_pkts)) {
        uve.set_out_tpkts(out_pkts);
        uve.set_out_bytes(out_bytes);
        uve_info_.set_out_tpkts(out_pkts);
        uve_info_.set_out_bytes(out_bytes);
        changed = true;
    }

    uint64_t diff_seconds = 0;
    uint64_t cur_time = UTCTimestampUsec();
    bool send_bandwidth = false;
    uint64_t in_band, out_band;
    uint64_t b_intvl = agent_->uve()->bandwidth_intvl();
    if (prev_stats_update_time_ == 0) {
        in_band = out_band = 0;
        send_bandwidth = true;
        prev_stats_update_time_ = cur_time;
    } else {
        diff_seconds = (cur_time - prev_stats_update_time_) / b_intvl;
        if (diff_seconds > 0) {
            diff_in_bytes = in_bytes - prev_in_bytes_;
            diff_out_bytes = out_bytes - prev_out_bytes_;
            in_band = (diff_in_bytes * 8)/diff_seconds;
            out_band = (diff_out_bytes * 8)/diff_seconds;
            prev_stats_update_time_ = cur_time;
            prev_in_bytes_ = in_bytes;
            prev_out_bytes_ = out_bytes;
            send_bandwidth = true;
        }
    }
    if (send_bandwidth && UveVnInBandChanged(in_band)) {
        uve.set_in_bandwidth_usage(in_band);
        uve_info_.set_in_bandwidth_usage(in_band);
        changed = true;
    }

    if (send_bandwidth && UveVnOutBandChanged(out_band)) {
        uve.set_out_bandwidth_usage(out_band);
        uve_info_.set_out_bandwidth_usage(out_band);
        changed = true;
    }

    int acl_rule_count;
    if (vn->GetAcl()) {
        acl_rule_count = vn->GetAcl()->Size();
    } else {
        acl_rule_count = 0;
    }
    /* We have not registered for ACL notification. So total_acl_rules
     * field is updated during stats updation
     */
    if (UveVnAclRuleCountChanged(acl_rule_count)) {
        uve.set_total_acl_rules(acl_rule_count);
        uve_info_.set_total_acl_rules(acl_rule_count);
        changed = true;
    }

    if (UpdateVnFlowCount(vn, uve)) {
        changed = true;
    }

    if (UpdateVnFipCount(fip_count, uve)) {
        changed = true;
    }

    /* VM interface list for VN is sent whenever any VM is added or
     * removed from VN. That message has only two fields set - vn name
     * and virtualmachine_list */

    if (PopulateInterVnStats(uve)) {
        changed = true;
    }

    if (SetVnPortBitmap(uve)) {
        changed = true;
    }

    return changed;
}

void VnUveEntry::GetInStats(uint64_t *in_bytes, uint64_t *in_pkts) const {
    *in_bytes = uve_info_.get_in_bytes();
    *in_pkts = uve_info_.get_in_tpkts();
}

void VnUveEntry::GetOutStats(uint64_t *out_bytes, uint64_t *out_pkts) const {
    *out_bytes = uve_info_.get_out_bytes();
    *out_pkts = uve_info_.get_out_tpkts();
}

