/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_uve_test_h
#define vnsw_agent_uve_test_h

#include <uve/agent_uve.h>

class AgentUveTest : public AgentUve {
public:
    AgentUveTest(Agent *agent, uint64_t intvl);
    virtual ~AgentUveTest();
private:
    DISALLOW_COPY_AND_ASSIGN(AgentUveTest);
};
#endif //vnsw_agent_uve_test_h
