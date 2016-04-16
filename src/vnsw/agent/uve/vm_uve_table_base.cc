/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface_common.h>
#include <uve/vm_uve_table_base.h>
#include <uve/agent_uve_base.h>

VmUveTableBase::VmUveTableBase(Agent *agent, uint32_t default_intvl)
    : uve_vm_map_(), agent_(agent), uve_vm_map_mutex_(),
      intf_listener_id_(DBTableBase::kInvalidId),
      vm_listener_id_(DBTableBase::kInvalidId), timer_last_visited_(nil_uuid()),
      timer_(TimerManager::CreateTimer
             (*(agent->event_manager())->io_service(),
              "VmUveTimer",
              TaskScheduler::GetInstance()->GetTaskId(kTaskDBExclude), 0)) {
      expiry_time_ = default_intvl;
      timer_->Start(expiry_time_,
                    boost::bind(&VmUveTableBase::TimerExpiry, this));
}

VmUveTableBase::~VmUveTableBase() {
}

bool VmUveTableBase::TimerExpiry() {
    UveVmMap::iterator it = uve_vm_map_.lower_bound(timer_last_visited_);
    if (it == uve_vm_map_.end()) {
        timer_last_visited_ = nil_uuid();
        return true;
    }

    uint32_t count = 0;
    while (it != uve_vm_map_.end() && count < AgentUveBase::kUveCountPerTimer) {
        VmUveEntryBase* entry = it->second.get();
        const boost::uuids::uuid u= it->first;
        UveVmMap::iterator prev = it;
        it++;
        count++;

        if (entry->deleted()) {
            SendVmDeleteMsg(entry->vm_config_name());
            if (!entry->renewed()) {
                tbb::mutex::scoped_lock lock(uve_vm_map_mutex_);
                uve_vm_map_.erase(prev);
            } else {
                entry->set_deleted(false);
                entry->set_renewed(false);
                entry->set_changed(false);
                SendVmMsg(entry, u);
            }
        } else if (entry->changed()) {
            SendVmMsg(entry, u);
            entry->set_changed(false);
            /* Clear renew flag to be on safer side. Not really required */
            entry->set_renewed(false);
        }
    }

    if (it == uve_vm_map_.end()) {
        timer_last_visited_ = nil_uuid();
        set_expiry_time(agent_->uve()->default_interval());
    } else {
        timer_last_visited_ = it->first;
        set_expiry_time(agent_->uve()->incremental_interval());
    }
    /* Return true to trigger auto-restart of timer */
    return true;
}

void VmUveTableBase::set_expiry_time(int time) {
    if (time != expiry_time_) {
        expiry_time_ = time;
        timer_->Reschedule(expiry_time_);
    }
}

void VmUveTableBase::SendVmDeleteMsg(const string &vm_config_name) {
    UveVirtualMachineAgent uve;
    uve.set_name(vm_config_name);
    uve.set_deleted(true);
    DispatchVmMsg(uve);
}

VmUveEntryBase* VmUveTableBase::Add(const VmEntry *vm, bool vm_notify) {
    VmUveEntryPtr uve = Allocate(vm);
    pair<UveVmMap::iterator, bool> ret;
    ret = uve_vm_map_.insert(UveVmPair(vm->GetUuid(), uve));
    UveVmMap::iterator it = ret.first;
    VmUveEntryBase* entry = it->second.get();
    if (!entry->add_by_vm_notify()) {
        entry->set_add_by_vm_notify(vm_notify);
    }
    if (entry->deleted()) {
        entry->set_renewed(true);
    }
    return entry;
}

void VmUveTableBase::Delete(const boost::uuids::uuid &u) {
    UveVmMap::iterator it = uve_vm_map_.find(u);
    if (it == uve_vm_map_.end()) {
        return;
    }
    VmUveEntryBase* entry = it->second.get();
    /* We need to reset all non-key fields to ensure that they have right
     * values since the entry is getting re-used. Also update the 'deleted_'
     * and 'renewed_' flags */
    entry->Reset();
    return;
}

void VmUveTableBase::Change(const VmEntry *vm) {
    VmUveEntryBase* entry = UveEntryFromVm(vm->GetUuid());
    if (entry == NULL) {
        return;
    }

    bool send = entry->Update(vm);
    if (send) {
        entry->set_changed(true);
    }
}

VmUveTableBase::VmUveEntryPtr VmUveTableBase::Allocate(const VmEntry *vm) {
    VmUveEntryPtr uve(new VmUveEntryBase(agent_, vm->GetCfgName()));
    return uve;
}

VmUveEntryBase* VmUveTableBase::UveEntryFromVm(const boost::uuids::uuid &u) {
    UveVmMap::iterator it = uve_vm_map_.find(u);
    if (it == uve_vm_map_.end()) {
        return NULL;
    }
    return it->second.get();
}

void VmUveTableBase::DispatchVmMsg(const UveVirtualMachineAgent &uve) {
    UveVirtualMachineAgentTrace::Send(uve);
}

void VmUveTableBase::SendVmMsg(VmUveEntryBase *entry,
                               const boost::uuids::uuid &u) {
    UveVirtualMachineAgent uve;
    if (entry->FrameVmMsg(u, &uve)) {
        DispatchVmMsg(uve);
    }
}

void VmUveTableBase::MarkChanged(const boost::uuids::uuid &u) {
    VmUveEntryBase* entry = UveEntryFromVm(u);
    if (entry == NULL) {
        return;
    }
    entry->set_changed(true);
    return;
}

void VmUveTableBase::InterfaceAddHandler(const VmEntry* vm,
                                         const VmInterface *vmi) {
    VmUveEntryBase *vm_uve_entry = Add(vm, false);

    vm_uve_entry->InterfaceAdd(vmi->cfg_name());
    vm_uve_entry->set_vm_name(vmi->vm_name());
    vm_uve_entry->set_changed(true);
}

void VmUveTableBase::InterfaceDeleteHandler(const boost::uuids::uuid &u,
                                            const string &intf_cfg_name) {
    VmUveEntryBase* entry = UveEntryFromVm(u);
    if (entry == NULL) {
        return;
    }

    entry->InterfaceDelete(intf_cfg_name);
    entry->set_changed(true);
}

void VmUveTableBase::UpdateVmName(const boost::uuids::uuid &u,
                                  const string &vm_name) {
    VmUveEntryBase* entry = UveEntryFromVm(u);
    if (entry == NULL) {
        return;
    }

    entry->set_vm_name(vm_name);
    entry->set_changed(true);
}

void VmUveTableBase::InterfaceNotify(DBTablePartBase *partition,
                                     DBEntryBase *e) {
    const VmInterface *vm_port = dynamic_cast<const VmInterface*>(e);
    if (vm_port == NULL) {
        return;
    }

    VmUveInterfaceState *state = static_cast<VmUveInterfaceState *>
                      (e->GetState(partition->parent(), intf_listener_id_));
    if (e->IsDeleted() || ((vm_port->vm() == NULL))) {
        if (state) {
            InterfaceDeleteHandler(state->vm_uuid_, state->interface_cfg_name_);
            e->ClearState(partition->parent(), intf_listener_id_);
            delete state;
        }
    } else {
        const VmEntry *vm = vm_port->vm();
        VmInterface::FloatingIpSet old_list;

        if (!state) {
            /* Skip Add notification if it does not have config name */
            if (vm_port->cfg_name().empty()) {
                return;
            }
            state = new VmUveInterfaceState(nil_uuid(), "");
            e->SetState(partition->parent(), intf_listener_id_, state);
        }
        /* Handle Change of VM in a given VM interface */
        if ((vm->GetUuid() != state->vm_uuid_) ||
            (vm_port->cfg_name() != state->interface_cfg_name_)) {
            //Handle disassociation of old VM from the VMI
            if (state->vm_uuid_ != nil_uuid() &&
                !state->interface_cfg_name_.empty()) {
                InterfaceDeleteHandler(state->vm_uuid_,
                                       state->interface_cfg_name_);
            }
            if (vm->GetUuid() != nil_uuid() &&
                !vm_port->cfg_name().empty()) {
                InterfaceAddHandler(vm, vm_port);
            }
            state->vm_uuid_ = vm->GetUuid();
            state->interface_cfg_name_ = vm_port->cfg_name();
            state->vm_name_ = vm_port->vm_name();
        } else if (vm_port->vm_name() != state->vm_name_) {
            UpdateVmName(state->vm_uuid_, vm_port->vm_name());
            state->vm_name_ = vm_port->vm_name();
        }
    }
}

void VmUveTableBase::VmNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const VmEntry *vm = static_cast<const VmEntry *>(e);

    VmUveVmState *state = static_cast<VmUveVmState *>
        (e->GetState(partition->parent(), vm_listener_id_));

    if (e->IsDeleted()) {
        if (state) {
            Delete(vm->GetUuid());

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
        MarkChanged(vm->GetUuid());
    } else {
        Change(vm);
    }
}

void VmUveTableBase::VmStatCollectionStart(VmUveVmState *state,
                                           const VmEntry *vm) {
}

void VmUveTableBase::VmStatCollectionStop(VmUveVmState *state) {
}

void VmUveTableBase::RegisterDBClients() {
    InterfaceTable *intf_table = agent_->interface_table();
    intf_listener_id_ = intf_table->Register
                  (boost::bind(&VmUveTableBase::InterfaceNotify, this, _1, _2));

    VmTable *vm_table = agent_->vm_table();
    vm_listener_id_ = vm_table->Register
        (boost::bind(&VmUveTableBase::VmNotify, this, _1, _2));
}

void VmUveTableBase::Shutdown(void) {
    if (vm_listener_id_ != DBTableBase::kInvalidId)
        agent_->vm_table()->Unregister(vm_listener_id_);
    if (intf_listener_id_ != DBTableBase::kInvalidId)
        agent_->interface_table()->Unregister(intf_listener_id_);

    if (timer_) {
        timer_->Cancel();
        TimerManager::DeleteTimer(timer_);
        timer_ = NULL;
    }
}



