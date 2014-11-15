/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface_common.h>
#include <uve/vm_uve_table_base.h>
#include <uve/agent_uve_base.h>

VmUveTableBase::VmUveTableBase(Agent *agent)
    : uve_vm_map_(), agent_(agent),
      intf_listener_id_(DBTableBase::kInvalidId),
      vm_listener_id_(DBTableBase::kInvalidId) {
}

VmUveTableBase::~VmUveTableBase() {
}

VmUveEntryBase* VmUveTableBase::Add(const VmEntry *vm, bool vm_notify) {
    VmUveEntryPtr uve = Allocate(vm);
    pair<UveVmMap::iterator, bool> ret;
    ret = uve_vm_map_.insert(UveVmPair(vm, uve));
    UveVmMap::iterator it = ret.first;
    VmUveEntryBase* entry = it->second.get();
    if (!entry->add_by_vm_notify()) {
        entry->set_add_by_vm_notify(vm_notify);
    }
    return entry;
}

void VmUveTableBase::Delete(const VmEntry *vm) {
    UveVmMap::iterator it = uve_vm_map_.find(vm);
    if (it == uve_vm_map_.end()) {
        return;
    }

    SendVmDeleteMsg(vm);

    uve_vm_map_.erase(it);
}

VmUveTableBase::VmUveEntryPtr VmUveTableBase::Allocate(const VmEntry *vm) {
    VmUveEntryPtr uve(new VmUveEntryBase(agent_));
    return uve;
}

VmUveEntryBase* VmUveTableBase::UveEntryFromVm(const VmEntry *vm) {
    UveVmMap::iterator it = uve_vm_map_.find(vm);
    if (it == uve_vm_map_.end()) {
        return NULL;
    }
    return it->second.get();
}

void VmUveTableBase::SendVmDeleteMsg(const VmEntry *vm) {
    UveVirtualMachineAgent uve;
    VmUveEntryBase* entry = UveEntryFromVm(vm);
    if (entry == NULL) {
        return;
    }
    uve.set_name(vm->GetCfgName());
    uve.set_deleted(true);
    entry->FrameVmMsg(vm, &uve);

    DispatchVmMsg(uve);
}

void VmUveTableBase::DispatchVmMsg(const UveVirtualMachineAgent &uve) {
    UveVirtualMachineAgentTrace::Send(uve);
}

void VmUveTableBase::SendVmMsg(const VmEntry *vm) {
    VmUveEntryBase* entry = UveEntryFromVm(vm);
    if (entry == NULL) {
        return;
    }
    UveVirtualMachineAgent uve;

    bool send = entry->FrameVmMsg(vm, &uve);
    if (send) {
        DispatchVmMsg(uve);
    }
}

void VmUveTableBase::InterfaceAddHandler(const VmEntry* vm, const Interface* itf,
                                  const VmInterface::FloatingIpSet &old_list) {
    VmUveEntryBase *vm_uve_entry = Add(vm, false);

    vm_uve_entry->InterfaceAdd(itf, old_list);
    SendVmMsg(vm);
}

void VmUveTableBase::InterfaceDeleteHandler(const VmEntry* vm,
                                        const Interface* intf) {
    VmUveEntryBase* entry = UveEntryFromVm(vm);
    if (entry == NULL) {
        return;
    }

    entry->InterfaceDelete(intf);
    SendVmMsg(vm);
    if (!entry->add_by_vm_notify()) {
        Delete(vm);
    }
}

const VmEntry *VmUveTableBase::VmUuidToVm(const boost::uuids::uuid u) {
    VmKey key(u);
    const VmEntry *vm = static_cast<VmEntry *>(agent_->vm_table()
            ->FindActiveEntry(&key));
    return vm;
}

void VmUveTableBase::InterfaceNotify(DBTablePartBase *partition, DBEntryBase *e) {
    const VmInterface *vm_port = dynamic_cast<const VmInterface*>(e);
    if (vm_port == NULL) {
        return;
    }

    VmUveInterfaceState *state = static_cast<VmUveInterfaceState *>
                      (e->GetState(partition->parent(), intf_listener_id_));
    if (e->IsDeleted() || ((vm_port->vm() == NULL))) {
        if (state) {
            const VmEntry *vm = VmUuidToVm(state->vm_uuid_);
            /* If vm is marked for delete or if vm is deleted, required
             * UVEs will be sent as part of Vm Delete Notification */
            if (vm != NULL) {
                InterfaceDeleteHandler(vm, vm_port);
                state->fip_list_.clear();
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
                                            vm_port->floating_ip_list().list_);
            e->SetState(partition->parent(), intf_listener_id_, state);
        } else {
            old_list = state->fip_list_;
            state->fip_list_ = vm_port->floating_ip_list().list_;
        }
        /* Handle Change of VM in a given VM interface */
        if (vm->GetUuid() != state->vm_uuid_) {
            //Handle disassociation of old VM from the VMI
            const VmEntry *old_vm = VmUuidToVm(state->vm_uuid_);
            /* If vm is marked for delete or if vm is deleted, required
             * UVEs will be sent as part of Vm Delete Notification */
            if (vm != NULL) {
                InterfaceDeleteHandler(old_vm, vm_port);
            }
        }
        InterfaceAddHandler(vm, vm_port, old_list);
    }
}

void VmUveTableBase::VmNotify(DBTablePartBase *partition, DBEntryBase *e) {
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

void VmUveTableBase::VmStatCollectionStart(VmUveVmState *state, const VmEntry *vm) {
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
    agent_->vm_table()->Unregister(vm_listener_id_);
    agent_->interface_table()->Unregister(intf_listener_id_);
}



