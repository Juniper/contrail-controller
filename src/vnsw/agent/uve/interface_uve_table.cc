/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface_common.h>
#include <uve/interface_uve_table.h>
#include <uve/agent_uve_base.h>

InterfaceUveTable::InterfaceUveTable(Agent *agent, uint32_t default_intvl)
    : agent_(agent), interface_tree_(),
      intf_listener_id_(DBTableBase::kInvalidId),
      timer_last_visited_(""),
      timer_(TimerManager::CreateTimer
             (*(agent->event_manager())->io_service(),
              "InterfaceUveTimer",
              TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0)) {
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
                interface_tree_.erase(prev);
            } else {
                entry->deleted_ = false;
                entry->renewed_ = false;
                entry->changed_ = false;
                SendInterfaceMsg(cfg_name, entry);
            }
        } else if (entry->changed_) {
            SendInterfaceMsg(cfg_name, entry);
            entry->changed_ = false;
            /* Clear renew flag to be on safer side. Not really required */
            entry->renewed_ = false;
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

void InterfaceUveTable::UveInterfaceEntry::SetVnVmName(UveVMInterfaceAgent *uve)
                                                       const {
    /* VM interfaces which are not created by Nova will not have VM name set.
     * In that case pick VM name from VM object instead of VMI object */
    if (!intf_->vm_name().empty()) {
        uve->set_vm_name(intf_->vm_name());
    } else {
        const VmEntry *vm = intf_->vm();
        if (vm) {
            uve->set_vm_name(vm->GetCfgName());
        }
    }
    if (intf_->vn() != NULL) {
        uve->set_virtual_network(intf_->vn()->GetName());
    } else {
        uve->set_virtual_network("");
    }
}


bool InterfaceUveTable::UveInterfaceEntry::FrameInterfaceMsg(const string &name,
    UveVMInterfaceAgent *s_intf) const {
    s_intf->set_name(name);
    SetVnVmName(s_intf);
    s_intf->set_ip_address(intf_->ip_addr().to_string());
    s_intf->set_mac_address(intf_->vm_mac());
    s_intf->set_ip6_address(intf_->ip6_addr().to_string());
    s_intf->set_ip6_active(intf_->ipv6_active());

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
            if (ip.installed_) {
                VmFloatingIPAgent uve_fip;
                uve_fip.set_ip_address(ip.floating_ip_.to_string());
                uve_fip.set_virtual_network(ip.vn_.get()->GetName());
                uve_fip_list.push_back(uve_fip);
            }
            it++;
        }
    }
    s_intf->set_floating_ips(uve_fip_list);

    s_intf->set_label(intf_->label());
    s_intf->set_active(intf_->ipv4_active());
    s_intf->set_l2_active(intf_->l2_active());
    s_intf->set_uuid(to_string(intf_->GetUuid()));
    string gw;
    if (GetVmInterfaceGateway(intf_, gw)) {
        s_intf->set_gateway(gw);
    }

    return true;
}

void InterfaceUveTable::UveInterfaceEntry::Reset() {
    intf_ = NULL;
    port_bitmap_.Reset();
    prev_fip_tree_.clear();
    fip_tree_.clear();

    deleted_ = true;
    renewed_ = false;
}

bool InterfaceUveTable::UveInterfaceEntry::FipAggStatsChanged
    (const vector<VmFloatingIPStats>  &list) const {
    if (list != uve_info_.get_fip_agg_stats()) {
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
    Ip4Address vm_addr = vm_intf->ip_addr();
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
}

InterfaceUveTable::UveInterfaceEntryPtr
InterfaceUveTable::Allocate(const VmInterface *itf) {
    UveInterfaceEntryPtr uve(new UveInterfaceEntry(itf));
    return uve;
}

void InterfaceUveTable::DispatchInterfaceMsg(const UveVMInterfaceAgent &uve) {
    UveVMInterfaceAgentTrace::Send(uve);
}

void InterfaceUveTable::SendInterfaceMsg(const string &name,
                                         UveInterfaceEntry *entry) {
    UveVMInterfaceAgent uve;
    if (entry->FrameInterfaceMsg(name, &uve)) {
        DispatchInterfaceMsg(uve);
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

    /* We need to handle only floating-ip deletion. The add of floating-ip is
     * taken care when stats are available for them during flow stats
     * collection */
    const VmInterface::FloatingIpSet &new_list = itf->floating_ip_list().list_;
    /* We need to look for entries which are present in old_list and not
     * in new_list */
    VmInterface::FloatingIpSet::const_iterator old_it = old_list.begin();
    while (old_it != old_list.end()) {
        VmInterface::FloatingIp fip = *old_it;
        ++old_it;
        /* Skip entries which are not installed as they wouldn't have been
         * added
         */
        if (!fip.installed_) {
            continue;
        }
        VmInterface::FloatingIpSet::const_iterator new_it = new_list.find(fip);
        if (new_it == new_list.end()) {
            entry->RemoveFloatingIp(fip);
        }
    }
}

void InterfaceUveTable::InterfaceDeleteHandler(const string &name) {
    InterfaceMap::iterator it = interface_tree_.find(name);
    if (it == interface_tree_.end()) {
        return;
    }
    UveInterfaceEntry* entry = it->second.get();
    /* We need to reset all non-key fields to ensure that they have right
     * values since the entry is getting re-used. Also update the 'deleted_'
     * and 'renewed_' flags */
    entry->Reset();
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
    if (e->IsDeleted() && state) {
        InterfaceDeleteHandler(state->cfg_name_);
        state->fip_list_.clear();
        e->ClearState(partition->parent(), intf_listener_id_);
        delete state;
    } else {
        VmInterface::FloatingIpSet old_list;
        if (vm_port->cfg_name().empty()) {
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
        }
        InterfaceAddHandler(vm_port, old_list);
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

void InterfaceUveTable::UveInterfaceEntry::UpdateFloatingIpStats
                                    (const FipInfo &fip_info) {
    tbb::mutex::scoped_lock lock(mutex_);
    FloatingIp *entry = FipEntry(fip_info.fip_, fip_info.vn_);
    entry->UpdateFloatingIpStats(fip_info);
}

InterfaceUveTable::FloatingIp *InterfaceUveTable::UveInterfaceEntry::FipEntry
    (uint32_t ip, const string &vn) {
    Ip4Address addr(ip);
    FloatingIpPtr key(new FloatingIp(addr, vn));
    FloatingIpSet::iterator fip_it =  fip_tree_.find(key);
    if (fip_it == fip_tree_.end()) {
        fip_tree_.insert(key);
        return key.get();
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
     vector<VmFloatingIPStats> &diff_list) {
    const VmInterface *vm_intf = static_cast<const VmInterface *>(intf_);
    tbb::mutex::scoped_lock lock(mutex_);
    if (vm_intf->HasFloatingIp()) {
        const VmInterface::FloatingIpList fip_list =
            vm_intf->floating_ip_list();
        VmInterface::FloatingIpSet::const_iterator it =
            fip_list.list_.begin();
        while(it != fip_list.list_.end()) {
            const VmInterface::FloatingIp &ip = *it;
            /* Skip FIP entries which are not yet activated */
            if (!ip.installed_) {
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
                SetDiffStats(diff_uve, 0, 0, 0, 0);
            } else {
                FloatingIp *fip = (*fip_it).get();
                SetStats(uve_fip, fip->in_bytes_, fip->in_packets_,
                         fip->out_bytes_, fip->out_packets_);
                FloatingIpSet::iterator prev_it = prev_fip_tree_.find(key);
                if (prev_it == prev_fip_tree_.end()) {
                    SetDiffStats(diff_uve, fip->in_bytes_, fip->in_packets_,
                                 fip->out_bytes_, fip->out_packets_);
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
                                 (fip->out_packets_ - pfip->out_packets_));
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
     uint64_t out_bytes, uint64_t out_pkts) const {
    fip.set_in_bytes(in_bytes);
    fip.set_in_pkts(in_pkts);
    fip.set_out_bytes(out_bytes);
    fip.set_out_pkts(out_pkts);
}

void InterfaceUveTable::UveInterfaceEntry::SetStats
    (VmFloatingIPStats &fip, uint64_t in_bytes, uint64_t in_pkts,
     uint64_t out_bytes, uint64_t out_pkts) const {
    fip.set_in_bytes(in_bytes);
    fip.set_in_pkts(in_pkts);
    fip.set_out_bytes(out_bytes);
    fip.set_out_pkts(out_pkts);
}

void InterfaceUveTable::UveInterfaceEntry::RemoveFloatingIp
    (const VmInterface::FloatingIp &fip) {
    tbb::mutex::scoped_lock lock(mutex_);
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

