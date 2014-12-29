/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface_common.h>
#include <uve/vm_uve_table.h>
#include <uve/vm_uve_entry.h>
#include <uve/agent_uve.h>

VmUveTable::VmUveTable(Agent *agent)
    : VmUveTableBase(agent) {
    event_queue_.reset(new WorkQueue<VmStatData *>
            (TaskScheduler::GetInstance()->GetTaskId("Agent::Uve"), 0,
             boost::bind(&VmUveTable::Process, this, _1)));
}

VmUveTable::~VmUveTable() {
}

void VmUveTable::UpdateBitmap(const VmEntry* vm, uint8_t proto,
                                  uint16_t sport, uint16_t dport) {
    UveVmMap::iterator it = uve_vm_map_.find(vm);
    if (it != uve_vm_map_.end()) {
        VmUveEntry *entry = static_cast<VmUveEntry *>(it->second.get());
        entry->UpdatePortBitmap(proto, sport, dport);
    }
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

    return static_cast<VmUveEntry *>(it->second.get());
}

VmUveTableBase::VmUveEntryPtr VmUveTable::Allocate(const VmEntry *vm) {
    VmUveEntryPtr uve(new VmUveEntry(agent_));
    return uve;
}

void VmUveTable::SendVmStatsMsg(const VmEntry *vm) {
    VmUveEntry* entry = static_cast<VmUveEntry*>(UveEntryFromVm(vm));
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

void VmUveTable::DispatchVmStatsMsg(const VirtualMachineStats &uve) {
    VirtualMachineStatsTrace::Send(uve);
}

void VmUveTable::SendVmDeleteMsg(const VmEntry *vm) {
    bool stats_uve_changed = false;
    UveVirtualMachineAgent uve;
    VirtualMachineStats stats_uve;
    VmUveEntry* entry = static_cast<VmUveEntry*>(UveEntryFromVm(vm));
    if (entry == NULL) {
        return;
    }
    uve.set_name(vm->GetCfgName());
    uve.set_deleted(true);
    stats_uve.set_name(vm->GetCfgName());
    stats_uve.set_deleted(true);
    entry->FrameVmStatsMsg(vm, &uve, &stats_uve, &stats_uve_changed);
    entry->FrameVmMsg(vm, &uve);

    DispatchVmMsg(uve);
    DispatchVmStatsMsg(stats_uve);
}

void VmUveTable::SendVmStats(void) {
    UveVmMap::iterator it = uve_vm_map_.begin();
    while (it != uve_vm_map_.end()) {
        SendVmStatsMsg(it->first);
        it++;
    }
}
