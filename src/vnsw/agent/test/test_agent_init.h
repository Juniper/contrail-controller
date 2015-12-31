/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_test_agent_init_hpp
#define vnsw_test_agent_init_hpp

#include <boost/program_options.hpp>
#include <init/contrail_init_common.h>
#include <test/test_pkt0_interface.h>
#include <uve/test/agent_stats_collector_test.h>
#include <vrouter/flow_stats/flow_stats_collector.h>

class Agent;
class AgentParam;

// The class to drive agent initialization. 
// Defines control parameters used to enable/disable agent features
class TestAgentInit : public ContrailInitCommon {
public:
    TestAgentInit();
    virtual ~TestAgentInit();

    static const uint32_t kDefaultInterval = 30 * 60 * 1000;
    static const uint32_t kIncrementalInterval = 30 * 60 * 1000;

    void ProcessOptions(const std::string &config_file,
                        const std::string &program_name);

    // Initialization virtual methods
    void FactoryInit();
    void CreateModules();

    // Shutdown virtual methods
    virtual void KSyncShutdown();
    void UveShutdown();
    void StatsCollectorShutdown();
    void FlowStatsCollectorShutdown();
    void WaitForIdle();

    TestPkt0Interface *pkt0() const { return pkt0_.get(); }
    void ProcessComputeAddress(AgentParam *param);
private:
    std::auto_ptr<KSync> ksync_;
    std::auto_ptr<AgentUveBase> uve_;
    std::auto_ptr<TestPkt0Interface> pkt0_;
    std::auto_ptr<AgentStatsCollectorTest> stats_collector_;
    std::auto_ptr<FlowStatsManager> flow_stats_manager_;

    DISALLOW_COPY_AND_ASSIGN(TestAgentInit);
};

#endif // vnsw_test_agent_init_hpp
