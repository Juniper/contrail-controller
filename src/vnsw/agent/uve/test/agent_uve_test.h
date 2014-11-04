/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_uve_test_h
#define vnsw_agent_uve_test_h

#include <uve/agent_uve.h>

class AgentUveBaseTest : public AgentUve {
public:
    AgentUveBaseTest(Agent *agent, uint64_t intvl);
    virtual ~AgentUveBaseTest();
private:
    DISALLOW_COPY_AND_ASSIGN(AgentUveBaseTest);
};
#endif //vnsw_agent_uve_test_h
