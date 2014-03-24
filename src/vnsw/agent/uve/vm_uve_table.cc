/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

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
    BuildVmDeleteMsg(vm, uve);

    DispatchVmMsg(uve);
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
                                  UveVirtualMachineAgent &uve) {
    VmUveEntry* entry = UveEntryFromVm(vm);
    if (entry == NULL) {
        return;
    }
    uve.set_name(vm->GetCfgName());
    uve.set_deleted(true); 
    entry->FrameVmStatsMsg(vm, uve);
    entry->FrameVmMsg(vm, uve);
}

void VmUveTable::DispatchVmMsg(const UveVirtualMachineAgent &uve) {
    UveVirtualMachineAgentTrace::Send(uve);
}

void VmUveTable::SendVmMsg(const VmEntry *vm) {
    VmUveEntry* entry = UveEntryFromVm(vm);
    if (entry == NULL) {
        return;
    }
    UveVirtualMachineAgent uve;

    bool send = entry->FrameVmMsg(vm, uve);
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

    bool send = entry->FrameVmStatsMsg(vm, uve);
    if (send) {
        DispatchVmMsg(uve);
    }
}

void VmUveTable::InterfaceAddHandler(const VmEntry* vm, const Interface* itf) {
    VmUveEntry *vm_uve_entry = Add(vm, false);

    vm_uve_entry->InterfaceAdd(itf);
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
                InterfaceDeleteHandler(state->vm_, vm_port);
                state->ipv4_active_ = false;
                state->l2_active_ = false;
            }
            if (e->IsDeleted()) {
                e->ClearState(partition->parent(), intf_listener_id_);
                delete state;
            }
        }
    } else {
        const VmEntry *vm = vm_port->vm();

        if (!state) {
            state = new VmUveInterfaceState(vm, vm_port->ipv4_active(), 
                                            vm_port->l2_active());
            e->SetState(partition->parent(), intf_listener_id_, state);
        } else {
            state->ipv4_active_ = vm_port->ipv4_active();
            state->l2_active_ = vm_port->l2_active();
        }
        /* Change of VM in a given VM interface is not supported now */
        if (vm != state->vm_) {
            assert(0);
        }
        InterfaceAddHandler(vm, vm_port);
    }
}

void VmUveTable::VmNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const VmEntry *vm = static_cast<const VmEntry *>(e);

    VmUveVmState *state = static_cast<VmUveVmState *>
        (e->GetState(partition->parent(), vm_listener_id_));

    if (e->IsDeleted()) {
        if (state) {
            Delete(vm);

            if (agent_->IsTestMode() == false) {
                state->stat_->Stop();
                state->stat_ = NULL;
            }
            e->ClearState(partition->parent(), vm_listener_id_);
            delete state;
        }
        return;
    }

    if (!state) {
        state = new VmUveVmState();
        e->SetState(partition->parent(), vm_listener_id_, state);

        Add(vm, true);

        //Create object to poll for VM stats
        if (agent_->IsTestMode() == false) { 
            VmStat *stat = new VmStat(agent_, vm->GetUuid());
            stat->Start();
            state->stat_ = stat;
        }
    }
    SendVmMsg(vm);
}

void VmUveTable::RegisterDBClients() {
    InterfaceTable *intf_table = agent_->GetInterfaceTable();
    intf_listener_id_ = intf_table->Register
                  (boost::bind(&VmUveTable::InterfaceNotify, this, _1, _2));

    VmTable *vm_table = agent_->GetVmTable();
    vm_listener_id_ = vm_table->Register
        (boost::bind(&VmUveTable::VmNotify, this, _1, _2));
}

void VmUveTable::Shutdown(void) {
    agent_->GetVmTable()->Unregister(vm_listener_id_);
    agent_->GetInterfaceTable()->Unregister(intf_listener_id_);
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

