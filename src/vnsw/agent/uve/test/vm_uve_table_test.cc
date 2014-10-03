/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/test/vm_uve_table_test.h>
#include <uve/test/vm_uve_entry_test.h>

VmUveTableTest::VmUveTableTest(Agent *agent) 
    : VmUveTable(agent) {
}

int VmUveTableTest::GetVmUveInterfaceCount(const std::string &vm) const {
    UveVmMap::const_iterator it = uve_vm_map_.begin();
    while (it != uve_vm_map_.end()) {
        const VmEntry *vme = it->first;
        if (vme->GetCfgName().compare(vm) == 0) {
            VmUveEntryTest *uve = static_cast<VmUveEntryTest *>
                (it->second.get());
            return uve->InterfaceCount();
        }
        it++;
    }
    return 0;
}

L4PortBitmap* VmUveTableTest::GetVmUvePortBitmap(const VmEntry *vm) {
    UveVmMap::iterator it = uve_vm_map_.find(vm);
    if (it != uve_vm_map_.end()) {
        VmUveEntryTest *entry = static_cast<VmUveEntryTest *>(
                it->second.get());
        return entry->port_bitmap();
    }
    return NULL;
}

L4PortBitmap* VmUveTableTest::GetVmIntfPortBitmap(const VmEntry *vm, 
                                                  const Interface *intf) {
    UveVmMap::iterator it = uve_vm_map_.find(vm);
    if (it != uve_vm_map_.end()) {
        VmUveEntryTest *entry = static_cast<VmUveEntryTest *>(
                it->second.get());
        return entry->InterfaceBitmap(intf);
    }
    return NULL;
}

VmUveTable::VmUveEntryPtr VmUveTableTest::Allocate(const VmEntry *vm) {
    VmUveEntryPtr uve(new VmUveEntryTest(agent_));
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
    UveVmMap::iterator it = uve_vm_map_.find(vm);
    if (it == uve_vm_map_.end()) {
        return NULL;
    }

    VmUveEntryTest *uve = static_cast<VmUveEntryTest *>(it->second.get());
    return uve->uve_info();
}

uint32_t VmUveTableTest::GetVmIntfFipCount(const VmEntry *vm,
                                           const Interface* intf) {
    UveVmMap::iterator it = uve_vm_map_.find(vm);
    if (it != uve_vm_map_.end()) {
        VmUveEntryTest *entry = static_cast<VmUveEntryTest *>(
                it->second.get());
        return entry->FloatingIpCount(intf);
    }
    return 0;
}

const VmUveEntry::FloatingIp *VmUveTableTest::GetVmIntfFip
    (const VmEntry *vm, const Interface* intf, const string &fip,
     const string &vn) {
    UveVmMap::iterator it = uve_vm_map_.find(vm);
    if (it != uve_vm_map_.end()) {
        VmUveEntryTest *entry = static_cast<VmUveEntryTest *>(
                it->second.get());
        return entry->IntfFloatingIp(intf, fip, vn);
    }
    return NULL;
}

void VmUveTableTest::DispatchVmStatsMsg(const VirtualMachineStats &uve) {
    vm_stats_send_count_++;
    if (uve.get_deleted()) {
        vm_stats_delete_count_++;
    }
    stats_uve_ = uve;
}
