/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/vm_uve_table_test.h>
#include <uve/vm_uve_entry_test.h>

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
