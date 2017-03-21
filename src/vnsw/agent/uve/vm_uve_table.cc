/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface_common.h>
#include <uve/vm_uve_table.h>
#include <uve/vm_uve_entry.h>
#include <uve/agent_uve.h>
#include <uve/vm_stat_kvm.h>
#include <uve/vm_stat_docker.h>

VmUveTable::VmUveTable(Agent *agent, uint32_t default_intvl)
    : VmUveTableBase(agent, default_intvl) {
    event_queue_.reset(new WorkQueue<VmStatData *>
            (TaskScheduler::GetInstance()->GetTaskId("Agent::Uve"), 0,
             boost::bind(&VmUveTable::Process, this, _1)));
    event_queue_->set_name("Virtual-Machine UVE");
}

VmUveTable::~VmUveTable() {
}

void VmUveTable::UpdateBitmap(const VmEntry* vm, uint8_t proto,
                              uint16_t sport, uint16_t dport) {
    tbb::mutex::scoped_lock lock(uve_vm_map_mutex_);
    UveVmMap::iterator it = uve_vm_map_.find(vm->GetUuid());
    if (it != uve_vm_map_.end()) {
        VmUveEntry *entry = static_cast<VmUveEntry *>(it->second.get());
        entry->UpdatePortBitmap(proto, sport, dport);
    }
}

VmUveTableBase::VmUveEntryPtr VmUveTable::Allocate(const VmEntry *vm) {
    VmUveEntryPtr uve(new VmUveEntry(agent_, vm->GetCfgName()));
    return uve;
}

void VmUveTable::SendVmStatsMsg(const boost::uuids::uuid &u) {
    VmUveEntry* entry = static_cast<VmUveEntry*>(UveEntryFromVm(u));
    if (entry == NULL) {
        return;
    }
    if (entry->deleted()) {
        /* Skip entry marked for delete because the 'vm' pointer could be
         * invalid */
        return;
    }
    UveVirtualMachineAgent uve;

    bool send = entry->FrameVmStatsMsg(&uve);
    if (send) {
        DispatchVmMsg(uve);
    }
}

void VmUveTable::VmStatCollectionStart(VmUveVmState *state, const VmEntry *vm) {
    //Create object to poll for VM stats
    VmStat *stat = NULL;
    if (agent_->isKvmMode()) {
        stat = new VmStatKvm(agent_, vm->GetUuid());
    } else if (agent_->isDockerMode()) {
        stat = new VmStatDocker(agent_, vm->GetUuid());
    }

    if (stat) {
        stat->Start();
        state->stat_ = stat;
    }
}

void VmUveTable::VmStatCollectionStop(VmUveVmState *state) {
    if (state->stat_) {
        state->stat_->Stop();
        state->stat_ = NULL;
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

void VmUveTable::DispatchVmStatsMsg(const VirtualMachineStats &uve) {
    VirtualMachineStatsTrace::Send(uve);
}

void VmUveTable::SendVmStats(void) {
    UveVmMap::iterator it = uve_vm_map_.begin();
    while (it != uve_vm_map_.end()) {
        SendVmStatsMsg(it->first);
        it++;
    }
}

void VmUveTable::SendVmDeleteMsg(const string &vm_config_name) {
    VmUveTableBase::SendVmDeleteMsg(vm_config_name);

    VirtualMachineStats stats_uve;
    stats_uve.set_name(vm_config_name);
    stats_uve.set_deleted(true);
    DispatchVmStatsMsg(stats_uve);
}
