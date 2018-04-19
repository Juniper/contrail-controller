/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/test/vm_uve_table_test.h>
#include <uve/test/vm_uve_entry_test.h>

VmUveTableTest::VmUveTableTest(Agent *agent, uint32_t default_intvl)
    : VmUveTable(agent, default_intvl) {
}

int VmUveTableTest::GetVmUveInterfaceCount(const std::string &vm) const {
    UveVmMap::const_iterator it = uve_vm_map_.begin();
    while (it != uve_vm_map_.end()) {
        VmUveEntryTest *uve = static_cast<VmUveEntryTest *>
                (it->second.get());
        if (uve->vm_config_name().compare(vm) == 0) {
            return uve->InterfaceCount();
        }
        it++;
    }
    return 0;
}

L4PortBitmap* VmUveTableTest::GetVmUvePortBitmap(const VmEntry *vm) {
    UveVmMap::iterator it = uve_vm_map_.find(vm->GetUuid());
    if (it != uve_vm_map_.end()) {
        VmUveEntryTest *entry = static_cast<VmUveEntryTest *>(
                it->second.get());
        return entry->port_bitmap();
    }
    return NULL;
}


VmUveTable::VmUveEntryPtr VmUveTableTest::Allocate(const VmEntry *vm) {
    VmUveEntryPtr uve(new VmUveEntryTest(agent_, vm->GetCfgName()));
    return uve;
}

void VmUveTableTest::DispatchVmMsg(const UveVirtualMachineAgent &uve) {
    send_count_++;
    if (uve.get_deleted()) {
        delete_count_++;
    }
    uve_ = uve;
}

void VmUveTableTest::ClearCount() {
    send_count_ = 0;
    delete_count_ = 0;
    vm_stats_send_count_ = 0;
    vm_stats_delete_count_ = 0;
}

UveVirtualMachineAgent* VmUveTableTest::VmUveObject(const VmEntry *vm) {
    UveVmMap::iterator it = uve_vm_map_.find(vm->GetUuid());
    if (it == uve_vm_map_.end()) {
        return NULL;
    }

    VmUveEntryTest *uve = static_cast<VmUveEntryTest *>(it->second.get());
    return uve->uve_info();
}

void VmUveTableTest::DispatchVmStatsMsg(const VirtualMachineStats &uve) {
    vm_stats_send_count_++;
    if (uve.get_deleted()) {
        vm_stats_delete_count_++;
    }
    stats_uve_ = uve;
}
