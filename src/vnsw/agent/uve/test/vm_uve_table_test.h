/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_uve_table_test_h
#define vnsw_agent_vm_uve_table_test_h

#include <uve/vm_uve_table.h>

class VmUveTableTest : public VmUveTable {
public:
    VmUveTableTest(Agent *agent);
    virtual void DispatchVmMsg(const UveVirtualMachineAgent &uve);
    uint32_t send_count() const { return send_count_; }
    uint32_t delete_count() const { return delete_count_; }
    uint32_t VmUveCount() const { return uve_vm_map_.size(); }
    int GetVmUveInterfaceCount(const std::string &vm) const;
    void ClearCount();
    L4PortBitmap* GetVmUvePortBitmap(const VmEntry *vm);
    L4PortBitmap* GetVmIntfPortBitmap(const VmEntry *vm, const Interface* intf);
    UveVirtualMachineAgent* VmUveObject(const VmEntry *vm);
    virtual void VmStatCollectionStart(VmUveVmState *st, const VmEntry *vm) {}
    virtual void VmStatCollectionStop(VmUveVmState *state) {}
    uint32_t GetVmIntfFipCount(const VmEntry *vm, const Interface* intf);
    const VmUveEntry::FloatingIp *GetVmIntfFip(const VmEntry *vm, 
        const Interface* intf, const string &fip, const string &vn);
private:
    virtual VmUveEntryPtr Allocate(const VmEntry *vm);
    uint32_t send_count_;
    uint32_t delete_count_;
    DISALLOW_COPY_AND_ASSIGN(VmUveTableTest);
};

#endif // vnsw_agent_vm_uve_table_test_h
