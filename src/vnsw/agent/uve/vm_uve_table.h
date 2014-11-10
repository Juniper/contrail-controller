/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_flow_vm_uve_table_h
#define vnsw_agent_flow_vm_uve_table_h

#include <uve/vm_uve_table_base.h>
#include <uve/vm_uve_entry.h>
#include <uve/l4_port_bitmap.h>
#include <pkt/flow_proto.h>
#include <pkt/flow_table.h>

class VmUveTable : public VmUveTableBase {
public:
    VmUveTable(Agent *agent);
    virtual ~VmUveTable();
    void UpdateBitmap(const VmEntry* vm, uint8_t proto, uint16_t sport,
                      uint16_t dport);
    void UpdateFloatingIpStats(const FlowEntry *flow, uint64_t bytes,
                               uint64_t pkts);
    void EnqueueVmStatData(VmStatData *data);
    bool Process(VmStatData *vm_stat_data);
    void SendVmStats(void);
    virtual void DispatchVmStatsMsg(const VirtualMachineStats &uve);
protected:
    virtual void VmStatCollectionStart(VmUveVmState *state, const VmEntry *vm);
    virtual void VmStatCollectionStop(VmUveVmState *state);
private:
    virtual VmUveEntryPtr Allocate(const VmEntry *vm);
    VmUveEntry::FloatingIp *ReverseFlowFip(const VmUveEntry::FipInfo &fip_info);
    VmUveEntry *InterfaceIdToVmUveEntry(uint32_t id);
    virtual void SendVmDeleteMsg(const VmEntry *vm);
    void SendVmStatsMsg(const VmEntry *vm);

    boost::scoped_ptr<WorkQueue<VmStatData *> > event_queue_;
    DISALLOW_COPY_AND_ASSIGN(VmUveTable);
};

#endif // vnsw_agent_flow_vm_uve_table_h
