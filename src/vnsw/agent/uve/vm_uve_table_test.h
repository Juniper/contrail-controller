/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_uve_table_test_h
#define vnsw_agent_vm_uve_table_test_h

#include <uve/vm_uve_table.h>

class VmUveTableTest : public VmUveTable {
public:
    VmUveTableTest(Agent *agent);
    virtual void DispatchVmMsg(const UveVirtualMachineAgent &uve) {}
    static VmUveTableTest *GetInstance() {return singleton_;}
    int GetVmUveInterfaceCount(const std::string &vm) const;
    L4PortBitmap* GetVmUvePortBitmap(const VmEntry *vm);
    L4PortBitmap* GetVmIntfPortBitmap(const VmEntry *vm, const Interface* intf);
private:
    virtual VmUveEntryPtr Allocate(const VmEntry *vm);
    static VmUveTableTest * singleton_;
    DISALLOW_COPY_AND_ASSIGN(VmUveTableTest);
};

#endif // vnsw_agent_vm_uve_table_test_h
