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
    void ResetCpuStatsCount();
    void ResetPortBitmap();
    bool first_uve_dispatched() const {
        return first_uve_dispatched_;
    }
    uint32_t vrouter_msg_count() const {
        return vrouter_msg_count_;
    }
    uint32_t vrouter_stats_msg_count() const {
        return vrouter_stats_msg_count_;
    }
    uint32_t compute_state_send_count() const 
        { return compute_state_send_count_; }
    const VrouterStatsAgent &last_sent_stats() const {
        return last_sent_vrouter_stats_;
    }
    const VrouterAgent &last_sent_vrouter() const {
        return last_sent_vrouter_;
    }
    const VrouterAgent &first_vrouter_uve() const {
        return first_vrouter_uve_;
    }
    VrouterStatsAgent &prev_stats() {
        return prev_stats_;
    }
    int bandwidth_count() const {
        return bandwidth_count_;
    };
    void set_bandwidth_count(uint8_t ctr) { bandwidth_count_ = ctr; }
    void clear_count();
    void DispatchVrouterMsg(const VrouterAgent &uve);
    void DispatchVrouterStatsMsg(const VrouterStatsAgent &uve);
    void DispatchComputeCpuStateMsg(const ComputeCpuState &ccs);
private:
    bool first_uve_dispatched_;
    uint32_t vrouter_msg_count_;
    uint32_t vrouter_stats_msg_count_;
    uint32_t compute_state_send_count_;
    VrouterStatsAgent last_sent_vrouter_stats_;
    VrouterAgent last_sent_vrouter_;
    VrouterAgent first_vrouter_uve_;
    DISALLOW_COPY_AND_ASSIGN(VrouterUveEntryTest);
};

#endif // vnsw_agent_vrouter_uve_entry_test_h
