/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_test_xml_agent_init_hpp
#define vnsw_test_xml_agent_init_hpp

#include <boost/program_options.hpp>
#include <init/contrail_init_common.h>
#include <test/test_pkt0_interface.h>
#include <test/test_agent_init.h>

class Agent;
class AgentParam;
class TestClient;

TestClient *PhysicalDeviceTestInit(const char *init_file, bool ksync_init);

// The class to drive agent initialization.
// Defines control parameters used to enable/disable agent features
class TestXmlAgentInit : public TestAgentInit {
public:
    TestXmlAgentInit();
    virtual ~TestXmlAgentInit();

    // Initialization virtual methods
    void CreateModules();
    void CreateDBTables();
    void RegisterDBClients();

private:
    DISALLOW_COPY_AND_ASSIGN(TestXmlAgentInit);
};

#endif // vnsw_test_xml_agent_init_hpp
