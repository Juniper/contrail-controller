/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_uve_h
#define vnsw_agent_uve_h

#include <uve/agent_uve_base.h>

//The class to drive UVE module initialization for agent
//Defines objects required for statistics collection from vrouter and
//objects required for sending UVE information to collector.
class AgentUve : public AgentUveBase {
public:
    AgentUve(Agent *agent, uint64_t intvl, uint32_t default_intvl,
             uint32_t incremental_intvl);
    virtual ~AgentUve();
    typedef std::map<string, uint64_t> DerivedStatsMap;
    typedef std::pair<string, uint64_t> DerivedStatsPair;

private:
    DISALLOW_COPY_AND_ASSIGN(AgentUve);
};

#endif //vnsw_agent_uve_h
