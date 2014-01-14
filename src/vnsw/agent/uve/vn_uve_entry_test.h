/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vn_uve_entry_test_h
#define vnsw_agent_vn_uve_entry_test_h

class VnUveEntryTest : public VnUveEntry {
public:
    VnUveEntryTest(Agent *agent, const VnEntry *vn) : VnUveEntry(agent, vn) {}
    VnUveEntryTest(Agent *agent) : VnUveEntry(agent) {}
    virtual ~VnUveEntryTest() {}
    const VnStatsSet* inter_vn_stats() const { return &inter_vn_stats_; }
    int VmCount() const { return vm_tree_.size(); }
    int InterfaceCount() const { return interface_tree_.size(); }
    L4PortBitmap* port_bitmap() { return &port_bitmap_; }
    UveVirtualNetworkAgent* uve_info() { return &uve_info_; }
private:
    DISALLOW_COPY_AND_ASSIGN(VnUveEntryTest);
};

#endif // vnsw_agent_vn_uve_entry_test_h
