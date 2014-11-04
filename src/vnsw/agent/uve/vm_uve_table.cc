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

void VmUveTable::UpdateFloatingIpStats(const FlowEntry *flow,
                                       uint64_t bytes, uint64_t pkts) {
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
