/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_uve_test_h
#define vnsw_agent_uve_test_h

#include <uve/agent_uve_stats.h>

class AgentUveBaseTest : public AgentUveStats {
public:
    AgentUveBaseTest(Agent *agent, uint64_t intvl, uint32_t default_intvl,
                     uint32_t incremental_intvl);
    virtual ~AgentUveBaseTest();
private:
    DISALLOW_COPY_AND_ASSIGN(AgentUveBaseTest);
};
#endif //vnsw_agent_uve_test_h
