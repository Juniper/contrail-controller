/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_uve_entry_test_h
#define vnsw_agent_vm_uve_entry_test_h

class VmUveEntryTest : public VmUveEntry {
public:
    VmUveEntryTest(Agent *agent, const string &vm_name)
        : VmUveEntry(agent, vm_name) {}
    virtual ~VmUveEntryTest() {}
    int InterfaceCount() const { return interface_tree_.size(); }
    L4PortBitmap* port_bitmap() { return &port_bitmap_; }
    UveVirtualMachineAgent* uve_info() { return &uve_info_; }
private:
    DISALLOW_COPY_AND_ASSIGN(VmUveEntryTest);
};

#endif // vnsw_agent_vm_uve_entry_test_h
