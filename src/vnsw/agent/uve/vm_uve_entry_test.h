/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_uve_entry_test_h
#define vnsw_agent_vm_uve_entry_test_h

class VmUveEntryTest : public VmUveEntry {
public:
    VmUveEntryTest(Agent *agent) : VmUveEntry(agent) {}
    ~VmUveEntryTest() {}
    int InterfaceCount() const { return interface_tree_.size(); }
    L4PortBitmap* port_bitmap() { return &port_bitmap_; }
    L4PortBitmap* InterfaceBitmap(const Interface *intf) {
        UveInterfaceEntryPtr entry(new UveInterfaceEntry(intf));
        InterfaceSet::iterator it = interface_tree_.find(entry);
        if (it != interface_tree_.end()) {
            return &((*it)->port_bitmap_);
        }
        return NULL;
    }
private:
    DISALLOW_COPY_AND_ASSIGN(VmUveEntryTest);
};

#endif // vnsw_agent_vm_uve_entry_test_h
