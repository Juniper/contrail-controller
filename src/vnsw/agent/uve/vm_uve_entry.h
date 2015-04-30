/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vm_uve_entry_h
#define vnsw_agent_vm_uve_entry_h

#include <uve/vm_uve_entry_base.h>

//The class that defines data-structures to store VirtualMachine information
//required for sending VirtualMachine UVE.
class VmUveEntry : public VmUveEntryBase {
public:
    VmUveEntry(Agent *agent, const string &vm_name);
    virtual ~VmUveEntry();
    void UpdatePortBitmap(uint8_t proto, uint16_t sport, uint16_t dport);
    bool FrameVmStatsMsg(UveVirtualMachineAgent *uve);
    virtual void Reset();
protected:
    L4PortBitmap port_bitmap_;
private:
    bool SetVmPortBitmap(UveVirtualMachineAgent *uve);
    DISALLOW_COPY_AND_ASSIGN(VmUveEntry);
};
#endif // vnsw_agent_vm_uve_entry_h
