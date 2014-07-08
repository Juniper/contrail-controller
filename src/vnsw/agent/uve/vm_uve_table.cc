/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface_common.h>
#include <uve/vm_uve_table.h>
#include <uve/agent_uve.h>

VmUveTable::VmUveTable(Agent *agent)
    : uve_vm_map_(), agent_(agent), 
      intf_listener_id_(DBTableBase::kInvalidId),
      vm_listener_id_(DBTableBase::kInvalidId) {
    event_queue_.reset(new WorkQueue<VmStatData *>
            (TaskScheduler::GetInstance()->GetTaskId("Agent::Uve"), 0,
             boost::bind(&VmUveTable::Process, this, _1)));
}

VmUveTable::~VmUveTable() {
}

VmUveEntry* VmUveTable::Add(const VmEntry *vm, bool vm_notify) {
    VmUveEntryPtr uve = Allocate(vm);
    pair<UveVmMap::iterator, bool> ret;
    ret = uve_vm_map_.insert(UveVmPair(vm, uve));
    UveVmMap::iterator it = ret.first;
    VmUveEntry* entry = it->second.get();
    if (!entry->add_by_vm_notify()) {
        entry->set_add_by_vm_notify(vm_notify);
    }
    return entry;
}

void VmUveTable::Delete(const VmEntry *vm) {
    UveVmMap::iterator it = uve_vm_map_.find(vm);
    if (it == uve_vm_map_.end()) {
        return;
    }

    UveVirtualMachineAgent uve;
    VirtualMachineStats stats_uve;
    BuildVmDeleteMsg(vm, &uve, &stats_uve);

    DispatchVmMsg(uve);
    DispatchVmStatsMsg(stats_uve);

    uve_vm_map_.erase(it);
}

VmUveTable::VmUveEntryPtr VmUveTable::Allocate(const VmEntry *vm) {
    VmUveEntryPtr uve(new VmUveEntry(agent_));
    return uve;
}

VmUveEntry* VmUveTable::UveEntryFromVm(const VmEntry *vm) {
    UveVmMap::iterator it = uve_vm_map_.find(vm);
    if (it == uve_vm_map_.end()) {
        return NULL;
    }
    return it->second.get();
}

void VmUveTable::BuildVmDeleteMsg(const VmEntry *vm, 
                                  UveVirtualMachineAgent *uve,
                                  VirtualMachineStats *stats_uve) {
    bool stats_uve_changed = false;
    VmUveEntry* entry = UveEntryFromVm(vm);
    if (entry == NULL) {
        return;
    }
    uve->set_name(vm->GetCfgName());
    uve->set_deleted(true);
    stats_uve->set_name(vm->GetCfgName());
    stats_uve->set_deleted(true);
    entry->FrameVmStatsMsg(vm, uve, stats_uve, &stats_uve_changed);
    entry->FrameVmMsg(vm, uve);
}

void VmUveTable::DispatchVmMsg(const UveVirtualMachineAgent &uve) {
    UveVirtualMachineAgentTrace::Send(uve);
}

void VmUveTable::DispatchVmStatsMsg(const VirtualMachineStats &uve) {
    VirtualMachineStatsTrace::Send(uve);
}

void VmUveTable::SendVmMsg(const VmEntry *vm) {
    VmUveEntry* entry = UveEntryFromVm(vm);
    if (entry == NULL) {
        return;
    }
    UveVirtualMachineAgent uve;

    bool send = entry->FrameVmMsg(vm, &uve);
    if (send) {
        DispatchVmMsg(uve);
    }
}

void VmUveTable::SendVmStatsMsg(const VmEntry *vm) {
    VmUveEntry* entry = UveEntryFromVm(vm);
    if (entry == NULL) {
        return;
    }
    UveVirtualMachineAgent uve;
    VirtualMachineStats stats_uve;
    bool stats_uve_send = false;

    /* Two VM UVEs will be sent for all VM stats. VirtualMachineStats will
     * have VM stats and UveVirtualMachineAgent will have port bitmap for VM
     * and all its containing interfaces */
    bool send = entry->FrameVmStatsMsg(vm, &uve, &stats_uve, &stats_uve_send);
    if (send) {
        DispatchVmMsg(uve);
    }
    if (stats_uve_send) {
        DispatchVmStatsMsg(stats_uve);
    }
}

void VmUveTable::InterfaceAddHandler(const VmEntry* vm, const Interface* itf,
                                  const VmInterface::FloatingIpSet &old_list) {
    VmUveEntry *vm_uve_entry = Add(vm, false);

    vm_uve_entry->InterfaceAdd(itf, old_list);
    SendVmMsg(vm);
}

void VmUveTable::InterfaceDeleteHandler(const VmEntry* vm, 
                                        const Interface* intf) {
    VmUveEntry* entry = UveEntryFromVm(vm);
    if (entry == NULL) {
        return;
    }

    entry->InterfaceDelete(intf);
    SendVmMsg(vm);
    if (!entry->add_by_vm_notify()) {
        Delete(vm);
    }
}

void VmUveTable::InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const VmInterface *vm_port = dynamic_cast<const VmInterface*>(e);
    if (vm_port == NULL) {
        return;
    }

    VmUveInterfaceState *state = static_cast<VmUveInterfaceState *>
                      (e->GetState(partition->parent(), intf_listener_id_));
    if (e->IsDeleted() || ((vm_port->ipv4_active() == false) &&
                           (vm_port->l2_active() == false))) {
        if (state) {
            if (e->IsDeleted() || ((state->ipv4_active_ == true) ||
                                    (state->l2_active_ == true))) {
                VmKey key(state->vm_uuid_);
                const VmEntry *vm = static_cast<VmEntry *>(agent_->vm_table()
                     ->FindActiveEntry(&key));
                /* If vm is marked for delete or if vm is deleted, required
                 * UVEs will be sent as part of Vm Delete Notification */
                if (vm != NULL) {
                    InterfaceDeleteHandler(vm, vm_port);
                    state->ipv4_active_ = false;
                    state->l2_active_ = false;
                }
            }
            if (e->IsDeleted()) {
                e->ClearState(partition->parent(), intf_listener_id_);
                delete state;
            }
        }
    } else {
        const VmEntry *vm = vm_port->vm();
        VmInterface::FloatingIpSet old_list;

        if (!state) {
            state = new VmUveInterfaceState(vm->GetUuid(), 
                                            vm_port->ipv4_active(),
                                            vm_port->l2_active(),
                                            vm_port->floating_ip_list().list_);
            e->SetState(partition->parent(), intf_listener_id_, state);
        } else {
            state->ipv4_active_ = vm_port->ipv4_active();
            state->l2_active_ = vm_port->l2_active();
            old_list = state->fip_list_;
            state->fip_list_ = vm_port->floating_ip_list().list_;
        }
        /* Change of VM in a given VM interface is not supported now */
        if (vm->GetUuid() != state->vm_uuid_) {
            assert(0);
        }
        InterfaceAddHandler(vm, vm_port, old_list);
    }
}

void VmUveTable::VmNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const VmEntry *vm = static_cast<const VmEntry *>(e);

    VmUveVmState *state = static_cast<VmUveVmState *>
        (e->GetState(partition->parent(), vm_listener_id_));

    if (e->IsDeleted()) {
        if (state) {
            Delete(vm);

            VmStatCollectionStop(state);

            e->ClearState(partition->parent(), vm_listener_id_);
            delete state;
        }
        return;
    }

    if (!state) {
        state = new VmUveVmState();
        e->SetState(partition->parent(), vm_listener_id_, state);

        Add(vm, true);

        VmStatCollectionStart(state, vm);
    }
    SendVmMsg(vm);
}

void VmUveTable::VmStatCollectionStart(VmUveVmState *state, const VmEntry *vm) {
    //Create object to poll for VM stats
    VmStat *stat = new VmStat(agent_, vm->GetUuid());
    stat->Start();
    state->stat_ = stat;
}

void VmUveTable::VmStatCollectionStop(VmUveVmState *state) {
    state->stat_->Stop();
    state->stat_ = NULL;
}

void VmUveTable::RegisterDBClients() {
    InterfaceTable *intf_table = agent_->interface_table();
    intf_listener_id_ = intf_table->Register
                  (boost::bind(&VmUveTable::InterfaceNotify, this, _1, _2));

    VmTable *vm_table = agent_->vm_table();
    vm_listener_id_ = vm_table->Register
        (boost::bind(&VmUveTable::VmNotify, this, _1, _2));
}

void VmUveTable::Shutdown(void) {
    agent_->vm_table()->Unregister(vm_listener_id_);
    agent_->interface_table()->Unregister(intf_listener_id_);
}

void VmUveTable::SendVmStats(void) {
    UveVmMap::iterator it = uve_vm_map_.begin();
    while (it != uve_vm_map_.end()) {
        SendVmStatsMsg(it->first);
        it++;
    }
}

void VmUveTable::UpdateBitmap(const VmEntry* vm, uint8_t proto, uint16_t sport,
                              uint16_t dport) {
    UveVmMap::iterator it = uve_vm_map_.find(vm);
    if (it != uve_vm_map_.end()) {
        VmUveEntry *entry = it->second.get();
        entry->UpdatePortBitmap(proto, sport, dport);
    }
}

void VmUveTable::EnqueueVmStatData(VmStatData *data) {
    event_queue_->Enqueue(data);
}

bool VmUveTable::Process(VmStatData* vm_stat_data) {
    if (vm_stat_data->vm_stat()->marked_delete()) {
        delete vm_stat_data->vm_stat();
    } else {
        vm_stat_data->vm_stat()->ProcessData();
    }
    delete vm_stat_data;
    return true;
}

void VmUveTable::UpdateFloatingIpStats(const FlowEntry *flow, uint64_t bytes,
                                       uint64_t pkts) {
    VmUveEntry::FipInfo fip_info;

    /* Ignore Non-Floating-IP flow */
    if (!flow->stats().fip ||
        flow->stats().fip_vm_port_id == Interface::kInvalidIndex) {
        return;
    }

    VmUveEntry *entry = InterfaceIdToVmUveEntry(flow->stats().fip_vm_port_id);
    if (entry == NULL) {
        return;
    }

    fip_info.bytes_ = bytes;
    fip_info.packets_ = pkts;
    fip_info.flow_ = flow;
    fip_info.rev_fip_ = NULL;
    if (flow->stats().fip != flow->reverse_flow_fip()) {
        /* This is the case where Source and Destination VMs (part of
         * same compute node) ping to each other to their respective
         * Floating IPs. In this case for each flow we need to increment
         * stats for both the VMs */
        fip_info.rev_fip_ = ReverseFlowFip(fip_info);
    }

    entry->UpdateFloatingIpStats(fip_info);
}

VmUveEntry::FloatingIp *VmUveTable::ReverseFlowFip
    (const VmUveEntry::FipInfo &fip_info) {
    uint32_t fip = fip_info.flow_->reverse_flow_fip();
    const string &vn = fip_info.flow_->data().source_vn;
    uint32_t intf_id = fip_info.flow_->reverse_flow_vmport_id();
    Interface *intf = InterfaceTable::GetInstance()->FindInterface(intf_id);

    VmUveEntry *entry = InterfaceIdToVmUveEntry(intf_id);
    if (entry != NULL) {
        return entry->FipEntry(fip, vn, intf);
    }
    return NULL;
}

VmUveEntry *VmUveTable::InterfaceIdToVmUveEntry(uint32_t id) {
    Interface *intf = InterfaceTable::GetInstance()->FindInterface(id);
    if (!intf || intf->type() != Interface::VM_INTERFACE) {
        return NULL;
    }
    VmInterface *vm_intf = static_cast<VmInterface *>(intf);
    /* Ignore if Vm interface does not have a VM */
    if (vm_intf->vm() == NULL) {
        return NULL;
    }

    UveVmMap::iterator it = uve_vm_map_.find(vm_intf->vm());
    if (it == uve_vm_map_.end()) {
        return NULL;
    }

    return it->second.get();
}
