/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_vrouter_uve_entry_test_h
#define vnsw_agent_vrouter_uve_entry_test_h

#include <uve/vrouter_uve_entry.h>

class VrouterUveEntryTest : public VrouterUveEntry {
public:
    VrouterUveEntryTest(Agent *agent);
    virtual ~VrouterUveEntryTest() {}
    void DispatchVrouterMsg(const VrouterAgent &uve) {}
    void DispatchVrouterStatsMsg(const VrouterStatsAgent &uve) {}
    static VrouterUveEntryTest* GetInstance() { return singleton_; }
private:
    static VrouterUveEntryTest* singleton_;
    DISALLOW_COPY_AND_ASSIGN(VrouterUveEntryTest);
};

#endif // vnsw_agent_vrouter_uve_entry_test_h
