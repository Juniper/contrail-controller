/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrouter_uve_entry_test_h
#define vnsw_agent_vrouter_uve_entry_test_h

#include <uve/vrouter_uve_entry.h>

class VrouterUveEntryTest : public VrouterUveEntry {
public:
    VrouterUveEntryTest(Agent *agent);
    virtual ~VrouterUveEntryTest();
    uint32_t compute_state_send_count() const 
        { return compute_state_send_count_; }
    void clear_count();
    void DispatchVrouterMsg(const VrouterAgent &uve) const {}
    void DispatchVrouterStatsMsg(const VrouterStatsAgent &uve) const {}
    void DispatchComputeCputStateMsg(const ComputeCpuState &ccs);
private:
    uint32_t compute_state_send_count_;
    DISALLOW_COPY_AND_ASSIGN(VrouterUveEntryTest);
};

#endif // vnsw_agent_vrouter_uve_entry_test_h
