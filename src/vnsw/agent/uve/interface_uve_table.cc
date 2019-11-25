/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface_common.h>
#include <oper/health_check.h>
#include <oper/tag.h>
#include <uve/interface_uve_table.h>
#include <uve/agent_uve_base.h>

InterfaceUveTable::InterfaceUveTable(Agent *agent, uint32_t default_intvl)
    : agent_(agent), interface_tree_(), interface_tree_mutex_(),
      intf_listener_id_(DBTableBase::kInvalidId),
      timer_last_visited_(""),
      timer_(TimerManager::CreateTimer
             (*(agent->event_manager())->io_service(),
              "InterfaceUveTimer",
              TaskScheduler::GetInstance()->GetTaskId(kTaskDBExclude), 0)) {
      expiry_time_ = default_intvl;
      timer_->Start(expiry_time_,
                    boost::bind(&InterfaceUveTable::TimerExpiry, this));
}

InterfaceUveTable::~InterfaceUveTable() {
}

bool InterfaceUveTable::TimerExpiry() {
    InterfaceMap::iterator it = interface_tree_.lower_bound(timer_last_visited_);
    if (it == interface_tree_.end()) {
        timer_last_visited_ = "";
        return true;
    }

    uint32_t count = 0;
    while (it != interface_tree_.end() &&
           count < AgentUveBase::kUveCountPerTimer) {
        string cfg_name = it->first;
        UveInterfaceEntry* entry = it->second.get();
        InterfaceMap::iterator prev = it;
        it++;
        count++;

        if (entry->deleted_) {
            SendInterfaceDeleteMsg(cfg_name);
            if (!entry->renewed_) {
                tbb::mutex::scoped_lock lock(interface_tree_mutex_);
                interface_tree_.erase(prev);
            } else {
                entry->deleted_ = false;
                entry->renewed_ = false;
                entry->changed_ = false;
                SendInterfaceMsg(cfg_name, entry);
                // Send Interface ACE stats
                SendInterfaceAceStats(cfg_name, entry);
            }
        } else {
            if (entry->changed_) {
                SendInterfaceMsg(cfg_name, entry);
                entry->changed_ = false;
                /* Clear renew flag to be on safer side. Not really required */
                entry->renewed_ = false;
            }
            // Send Interface ACE stats
            SendInterfaceAceStats(cfg_name, entry);
        }
    }


    if (it == interface_tree_.end()) {
        timer_last_visited_ = "";
        set_expiry_time(agent_->uve()->default_interval());
    } else {
        timer_last_visited_ = it->first;
        set_expiry_time(agent_->uve()->incremental_interval());
    }
    /* Return true to trigger auto-restart of timer */
    return true;
}

void InterfaceUveTable::set_expiry_time(int time) {
    if (time != expiry_time_) {
        expiry_time_ = time;
        timer_->Reschedule(expiry_time_);
    }
}

/* VM interfaces which are not created by Nova will not have VM name set.
 * In that case pick VM name from VM object instead of VMI object */
string InterfaceUveTable::UveInterfaceEntry::GetVmName() const {
    const VmEntry *vm = intf_->vm();
    if (!intf_->vm_name().empty()) {
        return intf_->vm_name();
    } else {
        if (vm) {
            return vm->GetCfgName();
        }
    }
    return "";
}

void InterfaceUveTable::UveInterfaceEntry::SetVnVmInfo(UveVMInterfaceAgent *uve)
                                                       const {
    const VmEntry *vm = intf_->vm();
    const string &vm_name = GetVmName();
    if (!vm_name.empty()) {
        uve->set_vm_name(vm_name);
    }
    if (intf_->vn() != NULL) {
        uve->set_virtual_network(intf_->vn()->GetName());
        uve->set_vn_uuid(to_string(intf_->vn()->GetUuid()));
    } else {
        uve->set_virtual_network("");
    }
    if (vm) {
        uve->set_vm_uuid(to_string(vm->GetUuid()));
    } else {
        uve->set_vm_uuid("");
    }
}

void InterfaceUveTable::UveInterfaceEntry::SetVMIStatsVnVm(VMIStats *uve)
                                                           const {
    const VmEntry *vm = intf_->vm();
    const string &vm_name = GetVmName();
    if (!vm_name.empty()) {
        uve->set_vm_name(vm_name);
    }
    if (intf_->vn() != NULL) {
        uve->set_virtual_network(intf_->vn()->GetName());
    } else {
        uve->set_virtual_network("");
    }
    if (vm) {
        uve->set_vm_uuid(to_string(vm->GetUuid()));
    } else {
        uve->set_vm_uuid("");
    }
}

bool InterfaceUveTable::UveInterfaceEntry::FrameInterfaceMsg(const string &name,
    UveVMInterfaceAgent *s_intf) const {
    s_intf->set_name(name);
    SetVnVmInfo(s_intf);
    s_intf->set_ip_address(intf_->primary_ip_addr().to_string());
    s_intf->set_mac_address(intf_->vm_mac().ToString());
    s_intf->set_ip6_address(intf_->primary_ip6_addr().to_string());
    s_intf->set_ip6_active(intf_->ipv6_active());
    s_intf->set_is_health_check_active(intf_->is_hc_active());
    s_intf->set_tx_vlan(intf_->tx_vlan_id());
    s_intf->set_rx_vlan(intf_->rx_vlan_id());
    s_intf->set_vhostuser_mode(intf_->vhostuser_mode());
    if (intf_->GetAnalyzer().empty()) {
        s_intf->set_port_mirror_enabled(false);
    } else {
        s_intf->set_port_mirror_enabled(true);
    }
    const Interface *parent = intf_->parent();
    if (parent) {
        const VmInterface *p_vmi = dynamic_cast<const VmInterface*>(parent);
        if (p_vmi) {
            s_intf->set_parent_interface(p_vmi->cfg_name());
        } else {
            s_intf->set_parent_interface(parent->name());
        }
    }

    vector<VmFloatingIPAgent> uve_fip_list;
    if (intf_->HasFloatingIp(Address::INET)) {
        const VmInterface::FloatingIpList fip_list =
            intf_->floating_ip_list();
        VmInterface::FloatingIpSet::const_iterator it =
            fip_list.list_.begin();
        while(it != fip_list.list_.end()) {
            const VmInterface::FloatingIp &ip = *it;
            /* Don't export FIP entry if it is not installed. When FIP entry
             * is not installed it will have NULL VN pointer. We can receive
             * notifications for VM interface with un-installed FIP entries
             * when the VM interface is not "L3 Active".
             */
            if (ip.Installed()) {
                VmFloatingIPAgent uve_fip;
                uve_fip.set_ip_address(ip.floating_ip_.to_string());
                uve_fip.set_virtual_network(ip.vn_.get()->GetName());
                uve_fip_list.push_back(uve_fip);
            }
            it++;
        }
    }
    s_intf->set_floating_ips(uve_fip_list);

    vector<VmHealthCheckInstance> uve_hc_list;
    const VmInterface::HealthCheckInstanceSet hc_list =
        intf_->hc_instance_set();
    VmInterface::HealthCheckInstanceSet::const_iterator hc_it =
        hc_list.begin();
    while (hc_it != hc_list.end()) {
        HealthCheckInstanceBase *inst = (*hc_it);
        hc_it++;
        HealthCheckService *svc = inst->service();
        if (!svc) {
            continue;
        }
        VmHealthCheckInstance uve_inst;
        uve_inst.set_name(svc->name());
        uve_inst.set_uuid(to_string(svc->uuid()));
        uve_inst.set_status(inst->active() ? "Active" : "InActive");
        uve_inst.set_is_running(inst->IsRunning());
        uve_hc_list.push_back(uve_inst);
    }
    s_intf->set_health_check_instance_list(uve_hc_list);

    s_intf->set_label(intf_->label());
    s_intf->set_ip4_active(intf_->ipv4_active());
    s_intf->set_l2_active(intf_->l2_active());
    s_intf->set_active(intf_->IsUveActive());
    s_intf->set_admin_state(intf_->admin_state());

    s_intf->set_uuid(to_string(intf_->vmi_cfg_uuid()));
    string gw;
    if (GetVmInterfaceGateway(intf_, gw)) {
        s_intf->set_gateway(gw);
    }

    std::vector<std::string> fixed_ip4_list;
    intf_->BuildIpStringList(Address::INET, &fixed_ip4_list);
    s_intf->set_fixed_ip4_list(fixed_ip4_list);

    std::vector<std::string> fixed_ip6_list;
    intf_->BuildIpStringList(Address::INET6, &fixed_ip6_list);
    s_intf->set_fixed_ip6_list(fixed_ip6_list);
    if (intf_->hbs_intf_type() == VmInterface::HBS_INTF_LEFT) {
        s_intf->set_hbf_intf_type("left");
    } else if (intf_->hbs_intf_type() == VmInterface::HBS_INTF_RIGHT) {
        s_intf->set_hbf_intf_type("right");
    } else if (intf_->hbs_intf_type() == VmInterface::HBS_INTF_MGMT) {
        s_intf->set_hbf_intf_type("mgmt");
    }
    return true;
}

void InterfaceUveTable::UveInterfaceEntry::Reset() {
    intf_ = NULL;
    port_bitmap_.Reset();
    prev_fip_tree_.clear();
    fip_tree_.clear();
    ace_set_.clear();
    security_policy_stats_map_.clear();

    ace_stats_changed_ = false;
    deleted_ = true;
    renewed_ = false;
}

void InterfaceUveTable::UveInterfaceEntry::HandleTagListChange() {
    assert(intf_);
    /* No action is required if tags are not added yet. Addition of tags is
     * handled via FlowStats module notification */
    if (local_tagset_.size() != 0) {
        TagList new_tag_list;
        intf_->CopyTagIdList(&new_tag_list);
        if (local_tagset_ != new_tag_list) {
            /* Do not update local_tagset_ with new values. This is handled
             * as part of FlowStats module notification */
            local_tagset_.clear();
        }
    }
}

void InterfaceUveTable::UveInterfaceEntry::UpdatePortBitmap
    (uint8_t proto, uint16_t sport, uint16_t dport) {
    tbb::mutex::scoped_lock lock(mutex_);
    /* No need to update stats if the entry is marked for delete and not
     * renewed */
    if (deleted_ && !renewed_) {
        return;
    }
    port_bitmap_.AddPort(proto, sport, dport);
}

bool InterfaceUveTable::UveInterfaceEntry::FipAggStatsChanged
    (const vector<VmFloatingIPStats>  &list) const {
    if (list != uve_stats_.get_fip_agg_stats()) {
        return true;
    }
    return false;
}

bool InterfaceUveTable::UveInterfaceEntry::GetVmInterfaceGateway(
    const VmInterface *vm_intf, string &gw) const {
    const VnEntry *vn = vm_intf->vn();
    if (vn == NULL) {
        return false;
    }
    const vector<VnIpam> &list = vn->GetVnIpam();
    Ip4Address vm_addr = vm_intf->primary_ip_addr();
    unsigned int i;
    for (i = 0; i < list.size(); i++) {
        if (list[i].IsSubnetMember(vm_addr))
            break;
    }
    if (i == list.size()) {
        return false;
    }
    gw = list[i].default_gw.to_string();
    return true;
}

void InterfaceUveTable::SendInterfaceDeleteMsg(const string &config_name) {
    UveVMInterfaceAgent uve;
    uve.set_name(config_name);
    uve.set_deleted(true);
    DispatchInterfaceMsg(uve);

    VMITags tags_uve;
    tags_uve.set_name(config_name);
    tags_uve.set_deleted(true);
    DispatchVMITagsMsg(tags_uve);

    VMIStats s_uve;
    s_uve.set_name(config_name);
    s_uve.set_deleted(true);
    DispatchVMIStatsMsg(s_uve);
}

bool InterfaceUveTable::UveInterfaceEntry::FrameTagsUveMsg
(Agent *agent, const string &name, VMITags *obj) {
    bool changed = false;
    AgentUveBase *uve = agent->uve();
    obj->set_name(name);
    TagList new_tag_list;
    intf_->CopyTagIdList(&new_tag_list);
    UveTagData tinfo(UveTagData::VECTOR);
    uve->BuildTagNamesFromList(new_tag_list, &tinfo);
    if ((!prev_tags_uve_.__isset.app) ||
        (prev_tags_uve_.__isset.app && (prev_tags_uve_.get_app() !=
                                        tinfo.application))) {
        obj->set_app(tinfo.application);
        prev_tags_uve_.set_app(tinfo.application);
        changed = true;
    }
    if ((!prev_tags_uve_.__isset.tier) ||
        (prev_tags_uve_.__isset.tier && (prev_tags_uve_.get_tier() !=
                                         tinfo.tier))) {
        obj->set_tier(tinfo.tier);
        prev_tags_uve_.set_tier(tinfo.tier);
        changed = true;
    }
    if ((!prev_tags_uve_.__isset.site) ||
        (prev_tags_uve_.__isset.site && (prev_tags_uve_.get_site() !=
                                         tinfo.site))) {
        obj->set_site(tinfo.site);
        prev_tags_uve_.set_site(tinfo.site);
        changed = true;
    }
    if ((!prev_tags_uve_.__isset.deployment) ||
        (prev_tags_uve_.__isset.deployment &&
         (prev_tags_uve_.get_deployment() != tinfo.deployment))) {
        obj->set_deployment(tinfo.deployment);
        prev_tags_uve_.set_deployment(tinfo.deployment);
        changed = true;
    }
    if ((!prev_tags_uve_.__isset.labels) ||
        (prev_tags_uve_.__isset.labels && (prev_tags_uve_.get_labels() !=
                                           tinfo.label_vector))) {
        obj->set_labels(tinfo.label_vector);
        prev_tags_uve_.set_labels(tinfo.label_vector);
        changed = true;
    }
    if ((!prev_tags_uve_.__isset.custom_tags) ||
        (prev_tags_uve_.__isset.custom_tags &&
         (prev_tags_uve_.get_custom_tags() != tinfo.custom_tag_vector))) {
        obj->set_custom_tags(tinfo.custom_tag_vector);
        prev_tags_uve_.set_custom_tags(tinfo.custom_tag_vector);
        changed = true;
    }
    return changed;
}

InterfaceUveTable::UveInterfaceEntryPtr
InterfaceUveTable::Allocate(const VmInterface *itf) {
    UveInterfaceEntryPtr uve(new UveInterfaceEntry(itf));
    return uve;
}

void InterfaceUveTable::DispatchInterfaceMsg(const UveVMInterfaceAgent &uve) {
    UveVMInterfaceAgentTrace::Send(uve);
}

void InterfaceUveTable::DispatchVMIStatsMsg(const VMIStats  &uve) {
    UVEVMIStats::Send(uve);
}

void InterfaceUveTable::DispatchVMITagsMsg(const VMITags &uve) const {
    UVEVMITags::Send(uve);
}

void InterfaceUveTable::DispatchInterfaceObjectLog(EndpointSecurityStats *obj) {
    ENDPOINT_SECURITY_STATS_SEND_SANDESH(obj);
}

void InterfaceUveTable::SendInterfaceMsg(const string &name,
                                         UveInterfaceEntry *entry) {
    UveVMInterfaceAgent uve;
    if (entry->FrameInterfaceMsg(name, &uve)) {
        DispatchInterfaceMsg(uve);
    }
    VMITags tags_uve;
    if (entry->FrameTagsUveMsg(agent_, name, &tags_uve)) {
        DispatchVMITagsMsg(tags_uve);
    }
}

void InterfaceUveTable::InterfaceAddHandler(const VmInterface* itf,
                                  const VmInterface::FloatingIpSet &old_list) {
    UveInterfaceEntryPtr uve = Allocate(itf);
    pair<InterfaceMap::iterator, bool> ret;
    ret = interface_tree_.insert(InterfacePair(itf->cfg_name(), uve));
    InterfaceMap::iterator it = ret.first;
    UveInterfaceEntry* entry = it->second.get();
    if (entry->deleted_) {
        entry->renewed_ = true;
        entry->intf_ = itf;
    }

    /* Mark the entry as changed to account for change in any fields of VMI */
    entry->changed_ = true;

    const VmInterface::FloatingIpSet &new_list = itf->floating_ip_list().list_;
    /* Remove old entries, by checking entries which are present in old list,
     * but not in new list */
    VmInterface::FloatingIpSet::const_iterator old_it = old_list.begin();
    while (old_it != old_list.end()) {
        VmInterface::FloatingIp fip = *old_it;
        ++old_it;
        /* Skip entries which are not installed as they wouldn't have been
         * added
         */
        if (!fip.Installed()) {
            continue;
        }
        VmInterface::FloatingIpSet::const_iterator new_it = new_list.find(fip);
        if (new_it == new_list.end()) {
            entry->RemoveFloatingIp(fip);
        }
    }
    /* Add entries in new list. Ignore if entries already present */
    VmInterface::FloatingIpSet::const_iterator fip_it = new_list.begin();
    while (fip_it != new_list.end()) {
        VmInterface::FloatingIp fip = *fip_it;
        ++fip_it;
        /* Skip entries which are not installed as they wouldn't have been
         * added
         */
        if (!fip.Installed()) {
            continue;
        }
        entry->AddFloatingIp(fip);
    }
}

void InterfaceUveTable::InterfaceDeleteHandler(const string &name) {
    InterfaceMap::iterator it = interface_tree_.find(name);
    if (it == interface_tree_.end()) {
        return;
    }
    UveInterfaceEntry* entry = it->second.get();
    tbb::mutex::scoped_lock lock(entry->mutex_);
    /* We need to reset all non-key fields to ensure that they have right
     * values since the entry is getting re-used. Also update the 'deleted_'
     * and 'renewed_' flags */
    entry->Reset();
    return;
}

void InterfaceUveTable::HandleVmiTagListChange(const string &name) {
    InterfaceMap::iterator it = interface_tree_.find(name);
    if (it == interface_tree_.end()) {
        return;
    }
    UveInterfaceEntry* entry = it->second.get();
    entry->HandleTagListChange();
    return;
}

void InterfaceUveTable::InterfaceNotify(DBTablePartBase *partition,
                                        DBEntryBase *e) {
    const VmInterface *vm_port = dynamic_cast<const VmInterface*>(e);
    if (vm_port == NULL) {
        return;
    }

    UveInterfaceState *state = static_cast<UveInterfaceState *>
                      (e->GetState(partition->parent(), intf_listener_id_));
    if (e->IsDeleted()) {
        if (state) {
            InterfaceDeleteHandler(state->cfg_name_);
            state->fip_list_.clear();
            e->ClearState(partition->parent(), intf_listener_id_);
            delete state;
        }
    } else {
        VmInterface::FloatingIpSet old_list;
        if (!state && vm_port->cfg_name().empty()) {
            /* Skip Add/change notifications if the config_name is empty */
            return;
        }

        if (!state) {
            state = new UveInterfaceState(vm_port);
            e->SetState(partition->parent(), intf_listener_id_, state);
        } else {
            old_list = state->fip_list_;
            state->fip_list_ = vm_port->floating_ip_list().list_;
        }
        if (state->cfg_name_ != vm_port->cfg_name()) {
            InterfaceDeleteHandler(state->cfg_name_);
            state->cfg_name_ = vm_port->cfg_name();
            old_list.clear();
        }
        if (!vm_port->cfg_name().empty()) {
            InterfaceAddHandler(vm_port, old_list);
            HandleVmiTagListChange(vm_port->cfg_name());
        }
    }
}

void InterfaceUveTable::RegisterDBClients() {
    InterfaceTable *intf_table = agent_->interface_table();
    intf_listener_id_ = intf_table->Register
                  (boost::bind(&InterfaceUveTable::InterfaceNotify, this, _1, _2));

}

void InterfaceUveTable::Shutdown(void) {
    if (intf_listener_id_ != DBTableBase::kInvalidId)
        agent_->interface_table()->Unregister(intf_listener_id_);

    if (timer_) {
        timer_->Cancel();
        TimerManager::DeleteTimer(timer_);
        timer_ = NULL;
    }
}

bool InterfaceUveTable::UveInterfaceEntry::PortBitmapChanged
    (const PortBucketBitmap &bmap) const {
    if (!uve_stats_.__isset.port_bucket_bmap) {
        return true;
    }
    if (bmap != uve_stats_.get_port_bucket_bmap()) {
        return true;
    }
    return false;
}

bool InterfaceUveTable::UveInterfaceEntry::InBandChanged(uint64_t in_band)
    const {
    if (!uve_stats_.__isset.in_bw_usage) {
        return true;
    }
    if (in_band != uve_stats_.get_in_bw_usage()) {
        return true;
    }
    return false;
}

bool InterfaceUveTable::UveInterfaceEntry::OutBandChanged(uint64_t out_band)
    const {
    if (!uve_stats_.__isset.out_bw_usage) {
        return true;
    }
    if (out_band != uve_stats_.get_out_bw_usage()) {
        return true;
    }
    return false;
}

void InterfaceUveTable::UveInterfaceEntry::UpdateFloatingIpStats
                                    (const FipInfo &fip_info) {
    tbb::mutex::scoped_lock lock(mutex_);
    /* No need to update stats if the entry is marked for delete and not
     * renewed */
    if (deleted_ && !renewed_) {
        return;
    }
    FloatingIp *entry = FipEntry(fip_info.fip_, fip_info.vn_);
    /* Ignore stats update request if it comes after entry is removed */
    if (entry == NULL) {
        return;
    }
    entry->UpdateFloatingIpStats(fip_info);
}

InterfaceUveTable::FloatingIp *InterfaceUveTable::UveInterfaceEntry::FipEntry
    (uint32_t ip, const string &vn) {
    Ip4Address addr(ip);
    FloatingIpPtr key(new FloatingIp(addr, vn));
    FloatingIpSet::iterator fip_it =  fip_tree_.find(key);
    if (fip_it == fip_tree_.end()) {
        return NULL;
    } else {
        return (*fip_it).get();
    }
}

void InterfaceUveTable::FloatingIp::UpdateFloatingIpStats(const FipInfo &fip_info) {
    if (fip_info.is_local_flow_) {
        if (fip_info.is_reverse_flow_) {
            out_bytes_ += fip_info.bytes_;
            out_packets_ += fip_info.packets_;

            if (fip_info.rev_fip_) {
                /* This is the case where Source and Destination VMs (part of
                 * same compute node) ping to each other to their respective
                 * Floating IPs. In this case for each flow we need to increment
                 * stats for both the VMs */
                fip_info.rev_fip_->out_bytes_ += fip_info.bytes_;
                fip_info.rev_fip_->out_packets_ += fip_info.packets_;
            }
        } else {
            in_bytes_ += fip_info.bytes_;
            in_packets_ += fip_info.packets_;
            if (fip_info.rev_fip_) {
                /* This is the case where Source and Destination VMs (part of
                 * same compute node) ping to each other to their respective
                 * Floating IPs. In this case for each flow we need to increment
                 * stats for both the VMs */
                fip_info.rev_fip_->in_bytes_ += fip_info.bytes_;
                fip_info.rev_fip_->in_packets_ += fip_info.packets_;
            }
        }
    } else {
        if (fip_info.is_ingress_flow_) {
            in_bytes_ += fip_info.bytes_;
            in_packets_ += fip_info.packets_;
        } else {
            out_bytes_ += fip_info.bytes_;
            out_packets_ += fip_info.packets_;
        }
    }
}


bool InterfaceUveTable::UveInterfaceEntry::FillFloatingIpStats
    (vector<VmFloatingIPStats> &result,
     vector<VmFloatingIPStats> &diff_list,
     bool &diff_list_send) {
    const VmInterface *vm_intf = static_cast<const VmInterface *>(intf_);
    if (vm_intf->HasFloatingIp()) {
        const VmInterface::FloatingIpList fip_list =
            vm_intf->floating_ip_list();
        VmInterface::FloatingIpSet::const_iterator it =
            fip_list.list_.begin();
        while(it != fip_list.list_.end()) {
            const VmInterface::FloatingIp &ip = *it;
            /* Skip FIP entries which are not yet activated */
            if (!ip.Installed()) {
                it++;
                continue;
            }
            VmFloatingIPStats uve_fip;
            VmFloatingIPStats diff_uve;
            uve_fip.set_ip_address(ip.floating_ip_.to_string());
            uve_fip.set_virtual_network(ip.vn_.get()->GetName());

            diff_uve.set_ip_address(ip.floating_ip_.to_string());
            diff_uve.set_virtual_network(ip.vn_.get()->GetName());

            FloatingIpPtr key(new FloatingIp(ip.floating_ip_,
                                             ip.vn_.get()->GetName()));
            FloatingIpSet::iterator fip_it =  fip_tree_.find(key);
            if (fip_it == fip_tree_.end()) {
                SetStats(uve_fip, 0, 0, 0, 0);
                SetDiffStats(diff_uve, 0, 0, 0, 0, diff_list_send);
            } else {
                FloatingIp *fip = (*fip_it).get();
                SetStats(uve_fip, fip->in_bytes_, fip->in_packets_,
                         fip->out_bytes_, fip->out_packets_);
                FloatingIpSet::iterator prev_it = prev_fip_tree_.find(key);
                if (prev_it == prev_fip_tree_.end()) {
                    SetDiffStats(diff_uve, fip->in_bytes_, fip->in_packets_,
                                 fip->out_bytes_, fip->out_packets_,
                                 diff_list_send);
                    FloatingIpPtr prev_fip_ptr(new FloatingIp(ip.floating_ip_,
                                                      ip.vn_.get()->GetName(),
                                                      fip->in_bytes_,
                                                      fip->in_packets_,
                                                      fip->out_bytes_,
                                                      fip->out_packets_));
                    prev_fip_tree_.insert(prev_fip_ptr);
                } else {
                    FloatingIp *pfip = (*prev_it).get();
                    SetDiffStats(diff_uve, (fip->in_bytes_ - pfip->in_bytes_),
                                 (fip->in_packets_ - pfip->in_packets_),
                                 (fip->out_bytes_ - pfip->out_bytes_),
                                 (fip->out_packets_ - pfip->out_packets_),
                                 diff_list_send);
                    pfip->in_bytes_ = fip->in_bytes_;
                    pfip->in_packets_ = fip->in_packets_;
                    pfip->out_bytes_ = fip->out_bytes_;
                    pfip->out_packets_ = fip->out_packets_;
                }

            }
            result.push_back(uve_fip);
            diff_list.push_back(diff_uve);
            it++;
        }
        return true;
    }
    return false;
}

void InterfaceUveTable::UveInterfaceEntry::SetDiffStats
    (VmFloatingIPStats &fip, uint64_t in_bytes, uint64_t in_pkts,
     uint64_t out_bytes, uint64_t out_pkts, bool &diff_list_send) const {
    fip.set_in_bytes(in_bytes);
    fip.set_in_pkts(in_pkts);
    fip.set_out_bytes(out_bytes);
    fip.set_out_pkts(out_pkts);
    if ((in_bytes != 0) || (in_pkts != 0) || (out_bytes != 0) ||
        (out_pkts != 0)) {
        diff_list_send = true;
    }
}

void InterfaceUveTable::UveInterfaceEntry::SetStats
    (VmFloatingIPStats &fip, uint64_t in_bytes, uint64_t in_pkts,
     uint64_t out_bytes, uint64_t out_pkts) const {
    fip.set_in_bytes(in_bytes);
    fip.set_in_pkts(in_pkts);
    fip.set_out_bytes(out_bytes);
    fip.set_out_pkts(out_pkts);
}

void InterfaceUveTable::UveInterfaceEntry::AddFloatingIp
    (const VmInterface::FloatingIp &fip) {
    FloatingIpPtr key(new FloatingIp(fip.floating_ip_,
                                     fip.vn_.get()->GetName()));
    FloatingIpSet::iterator it = fip_tree_.find(key);
    if (it != fip_tree_.end()) {
        return;
    }
    fip_tree_.insert(key);
}

void InterfaceUveTable::UveInterfaceEntry::RemoveFloatingIp
    (const VmInterface::FloatingIp &fip) {
    FloatingIpPtr key(new FloatingIp(fip.floating_ip_, fip.vn_.get()->GetName()));
    FloatingIpSet::iterator it = fip_tree_.find(key);
    if (it != fip_tree_.end()) {
        fip_tree_.erase(it);
    }
    FloatingIpSet::iterator prev_it = prev_fip_tree_.find(key);
    if (prev_it != prev_fip_tree_.end()) {
        prev_fip_tree_.erase(prev_it);
    }
}

void InterfaceUveTable::UveInterfaceEntry::UpdateInterfaceAceStats
    (const std::string &ace_uuid) {
    AceStats key(ace_uuid);
    ace_stats_changed_ = true;
    AceStatsSet::const_iterator it = ace_set_.find(key);
    if (it != ace_set_.end()) {
        it->count++;
        return;
    }
    key.count = 1;
    ace_set_.insert(key);
}

void InterfaceUveTable::UveInterfaceEntry::UpdateCounters
    (const FlowUveFwPolicyInfo &info, UveSecurityPolicyStats *obj) {
    if (info.added_) {
        ++obj->added;
    } else {
        ++obj->deleted;
    }
    if (obj->added >= obj->deleted) {
        obj->active = obj->added - obj->deleted;
    }
}

void InterfaceUveTable::UveInterfaceEntry::UpdateInterfaceFwPolicyStats
    (const FlowUveFwPolicyInfo &info) {
    ace_stats_changed_ = true;
    tbb::mutex::scoped_lock lock(mutex_);
    /* If TagList of VMI has changed clear the earlier statistics as they are
     * valid only for previous TagList.
     */
    if (info.added_) {
        local_tagset_ = info.local_tagset_;
    }
    SecurityPolicyStatsMap::iterator it = security_policy_stats_map_.
        find(info.fw_policy_);
    UveSecurityPolicyStatsPtr ep_key(new UveSecurityPolicyStats
                                         (info.local_tagset_,
                                          info.remote_tagset_,
                                          info.remote_prefix_, info.remote_vn_,
                                          info.local_vn_, info.action_));
    if (it != security_policy_stats_map_.end()) {
        EndpointStatsContainer &cont = it->second;
        InterfaceUveTable::SecurityPolicyStatsSet &remote_ep_list =
            cont.ToList(info.initiator_);
        SecurityPolicyStatsSet::iterator ep_it = remote_ep_list.find(ep_key);
        if (ep_it != remote_ep_list.end()) {
            UveSecurityPolicyStatsPtr entry = *ep_it;
            UpdateCounters(info, entry.get());
            return;
        } else {
            /* Ignore delete counter increment request when the entry itself is
             * absent
             */
            if (!info.added_) {
                return;
            }
            UpdateCounters(info, ep_key.get());
            remote_ep_list.insert(ep_key);
        }
    } else {
        UpdateCounters(info, ep_key.get());
        EndpointStatsContainer cont;
        SecurityPolicyStatsSet &remote_ep_list = cont.ToList(info.initiator_);
        remote_ep_list.insert(ep_key);
        security_policy_stats_map_.insert(SecurityPolicyStatsPair
                                          (info.fw_policy_, cont));
    }
}

bool InterfaceUveTable::UveInterfaceEntry::FrameInterfaceAceStatsMsg
    (const std::string &name, VMIStats *s_intf) {
    if (!ace_stats_changed_) {
        return false;
    }
    std::vector<SgAclRuleStats> list;
    AceStatsSet::iterator it = ace_set_.begin();
    bool changed = false;
    while (it != ace_set_.end()) {
        SgAclRuleStats item;
        item.set_rule(it->ace_uuid);
        uint64_t diff_count = it->count - it->prev_count;
        item.set_count(diff_count);
        //Update prev_count
        it->prev_count = it->count;
        list.push_back(item);
        ++it;
        /* If diff_count is non-zero for any rule entry, we send the entire
         * list */
        if (diff_count) {
            changed = true;
        }
    }
    /* If all the entries in the list has 0 diff_stats, then UVE won't be
     * sent */
    if (changed) {
        s_intf->set_name(name);
        SetVMIStatsVnVm(s_intf);
        s_intf->set_sg_rule_stats(list);
        ace_stats_changed_ = false;
        return true;
    }
    return false;
}

void InterfaceUveTable::UveInterfaceEntry::UpdateSecurityPolicyStats
    (const EndpointStatsInfo &info) {
    tbb::mutex::scoped_lock lock(mutex_);
    local_tagset_ = info.local_tagset;
    UveSecurityPolicyStatsPtr stats(new UveSecurityPolicyStats
                                    (info.local_tagset, info.remote_tagset,
                                     info.remote_prefix, info.remote_vn,
                                     info.local_vn, info.action));
    SecurityPolicyStatsMap::iterator it =
        security_policy_stats_map_.find(info.policy);
    if (it == security_policy_stats_map_.end()) {
        UpdateSecurityPolicyStatsInternal(info, stats.get());
        EndpointStatsContainer cont;
        SecurityPolicyStatsSet &stats_set = cont.ToList(info.client);
        stats_set.insert(stats);
        security_policy_stats_map_.insert(SecurityPolicyStatsPair(info.policy,
                                                                  cont));
    } else {
        EndpointStatsContainer &cont = it->second;
        SecurityPolicyStatsSet &stats_set = cont.ToList(info.client);
        std::pair<SecurityPolicyStatsSet::iterator,bool> ret =
           stats_set.insert(stats);
        UveSecurityPolicyStatsPtr entry(*ret.first);
        /* Update the statistics and action for the entry */
        UpdateSecurityPolicyStatsInternal(info, entry.get());
    }
}


void InterfaceUveTable::UveInterfaceEntry::UpdateSecurityPolicyStatsInternal
    (const EndpointStatsInfo &info, UveSecurityPolicyStats *stats) {
    stats->action = info.action;
    if (info.in_stats) {
        stats->in_bytes += info.diff_bytes;
        stats->in_pkts += info.diff_pkts;
    } else {
        stats->out_bytes += info.diff_bytes;
        stats->out_pkts += info.diff_pkts;
    }
}

void InterfaceUveTable::UveInterfaceEntry::FillEndpointStats
    (Agent *agent, EndpointSecurityStats *obj) {
    std::map<std::string, EndpointStats> eps;

    obj->set_name(intf_->cfg_name());
    SecurityPolicyStatsMap::const_iterator it =
        security_policy_stats_map_.begin();
    while (it != security_policy_stats_map_.end()) {
        std::vector<SecurityPolicyFlowStats> traffic_list;
        const EndpointStatsContainer &cont = it->second;
        EndpointStats value;
        value.set_workload(GetVmName());
        FillSecurityPolicyList(agent, cont.client_list, &value.client);
        FillSecurityPolicyList(agent, cont.server_list, &value.server);
        eps.insert(make_pair(it->first, value));
        ++it;
    }
    obj->set_eps(eps);
}

void InterfaceUveTable::UveInterfaceEntry::FillSecurityPolicyList
    (Agent *agent, const SecurityPolicyStatsSet &ilist,
     std::vector<SecurityPolicyFlowStats> *olist) {
    const AgentUveBase *uve = agent->uve();
    SecurityPolicyStatsSet::const_iterator sit = ilist.begin();
    while (sit != ilist.end()) {
        SecurityPolicyFlowStats item;
        UveSecurityPolicyStatsPtr entry(*sit);
        UveTagData tinfo;
        uve->BuildTagNamesFromList(entry->local_tagset, &tinfo);
        item.set_app(tinfo.application);
        item.set_tier(tinfo.tier);
        item.set_site(tinfo.site);
        item.set_deployment(tinfo.deployment);
        item.set_labels(tinfo.labels);
        item.set_custom_tags(tinfo.custom_tags);
        tinfo.Reset();
        uve->BuildTagIdsFromList(entry->remote_tagset, &tinfo);
        item.set_remote_app_id(tinfo.application);
        item.set_remote_tier_id(tinfo.tier);
        item.set_remote_site_id(tinfo.site);
        item.set_remote_deployment_id(tinfo.deployment);
        item.set_remote_label_ids(tinfo.labels);
        item.set_remote_custom_tag_ids(tinfo.custom_tags);

        item.set_remote_vn(entry->remote_vn);
        item.set_local_vn(entry->local_vn);
        item.set_action(entry->action);
        item.set_added(entry->added - entry->prev_added);
        item.set_deleted(entry->deleted - entry->prev_deleted);
        item.set_active(entry->active);
        item.set_in_bytes(entry->in_bytes - entry->prev_in_bytes);
        item.set_in_pkts(entry->in_pkts - entry->prev_in_pkts);
        item.set_out_bytes(entry->out_bytes - entry->prev_out_bytes);
        item.set_out_pkts(entry->out_pkts - entry->prev_out_pkts);
        olist->push_back(item);

        entry->prev_in_bytes = entry->in_bytes;
        entry->prev_in_pkts = entry->in_pkts;
        entry->prev_out_bytes = entry->out_bytes;
        entry->prev_out_pkts = entry->out_pkts;
        entry->prev_added = entry->added;
        entry->prev_deleted = entry->deleted;

        ++sit;
    }
}

void InterfaceUveTable::UveInterfaceEntry::FillTagSetAndPolicyList
    (VMIStats *obj) {

    vector<string> rule_list;
    SecurityPolicyStatsMap::const_iterator it =
        security_policy_stats_map_.begin();
    while (it != security_policy_stats_map_.end()) {
        rule_list.push_back(it->first);
        ++it;
    }
    obj->set_policy_rules(rule_list);
}

void InterfaceUveTable::UveInterfaceEntry::BuildSandeshUveTagList
    (const TagList &list, vector<SandeshUveTagInfo> *rts) const {
    TagList::const_iterator it = list.begin();
    while (it != list.end()) {
        SandeshUveTagInfo tag_entry;
        uint32_t ttype = (uint32_t)*it >> TagEntry::kTagTypeBitShift;
        tag_entry.set_type(TagEntry::GetTypeStr(ttype));
        tag_entry.set_id(*it);
        AgentDBTable *table = static_cast<AgentDBTable *>(intf_->get_table());
        tag_entry.set_name(table->agent()->tag_table()->TagName(*it));
        rts->push_back(tag_entry);
        ++it;
    }
}

void InterfaceUveTable::UveInterfaceEntry::BuildInterfaceUveSecurityPolicyList
    (const SecurityPolicyStatsSet &ilist,
     vector<SandeshUveRemoteEndpoint> *olist) const {
    SecurityPolicyStatsSet::const_iterator sit = ilist.begin();
    while (sit != ilist.end()) {
        SandeshUveRemoteEndpoint rep;
        UveSecurityPolicyStatsPtr entry(*sit);
        ++sit;

        rep.set_remote_vn(entry->remote_vn);
        rep.set_local_vn(entry->local_vn);
        rep.set_remote_prefix(entry->remote_prefix);

        vector<SandeshUveTagInfo> lts;
        BuildSandeshUveTagList(entry->local_tagset, &lts);
        rep.set_local_tagset(lts);
        vector<SandeshUveTagInfo> rts;
        BuildSandeshUveTagList(entry->remote_tagset, &rts);
        rep.set_remote_tagset(rts);
        rep.set_added(entry->added);
        rep.set_deleted(entry->deleted);
        rep.set_active(entry->active);
        rep.set_dropped_short(entry->dropped_short);
        rep.set_in_bytes(entry->in_bytes);
        rep.set_in_pkts(entry->in_pkts);
        rep.set_out_bytes(entry->out_bytes);
        rep.set_out_pkts(entry->out_pkts);
        rep.set_prev_in_bytes(entry->prev_in_bytes);
        rep.set_prev_in_pkts(entry->prev_in_pkts);
        rep.set_prev_out_bytes(entry->prev_out_bytes);
        rep.set_prev_out_pkts(entry->prev_out_pkts);
        rep.set_action(entry->action);

        olist->push_back(rep);
    }
}

void InterfaceUveTable::UveInterfaceEntry::BuildInterfaceUveInfo
    (InterfaceUveInfo *r) const {
    r->set_name(intf_->cfg_name());

    vector<SandeshUveTagInfo> lts;
    BuildSandeshUveTagList(local_tagset_, &lts);
    r->set_local_tagset(lts);

    vector<SandeshUvePolicyInfo> policy_list;
    SecurityPolicyStatsMap::const_iterator it =
        security_policy_stats_map_.begin();
    while (it != security_policy_stats_map_.end()) {
        SandeshUvePolicyInfo item;
        item.set_name(it->first);
        const EndpointStatsContainer &cont = it->second;
        BuildInterfaceUveSecurityPolicyList(cont.client_list,
                                            &item.client_list);
        BuildInterfaceUveSecurityPolicyList(cont.server_list,
                                            &item.server_list);
        policy_list.push_back(item);
        ++it;
    }
    r->set_policy_list(policy_list);
}
