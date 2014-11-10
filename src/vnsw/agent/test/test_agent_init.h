/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_test_agent_init_hpp
#define vnsw_test_agent_init_hpp

#include <boost/program_options.hpp>
#include <init/contrail_init_common.h>
#include <test/test_pkt0_interface.h>

class Agent;
class AgentParam;

// The class to drive agent initialization. 
// Defines control parameters used to enable/disable agent features
class TestAgentInit : public ContrailInitCommon {
public:
    TestAgentInit();
    virtual ~TestAgentInit();

    void ProcessOptions(const std::string &config_file,
                        const std::string &program_name);

    // Initialization virtual methods
    void FactoryInit();
    void CreateModules();

    // Shutdown virtual methods
    void KSyncShutdown();
    void UveShutdown();
    void WaitForIdle();

    TestPkt0Interface *pkt0() const { return pkt0_.get(); }
private:
    std::auto_ptr<KSync> ksync_;
    std::auto_ptr<AgentUveBase> uve_;
    std::auto_ptr<TestPkt0Interface> pkt0_;

    DISALLOW_COPY_AND_ASSIGN(TestAgentInit);
};

#endif // vnsw_test_agent_init_hpp
