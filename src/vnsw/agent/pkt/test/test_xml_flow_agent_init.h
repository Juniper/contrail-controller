/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_pkt_test_test_xml_flow_agent_init_hpp
#define vnsw_agent_pkt_test_test_xml_flow_agent_init_hpp

#include <boost/program_options.hpp>
#include <init/contrail_init_common.h>
#include <test/test_pkt0_interface.h>
#include <test/test_agent_init.h>

class Agent;
class AgentParam;
class TestClient;

TestClient *XmlPktParseTestInit(const char *init_file, bool ksync_init);

// The class to drive agent initialization.
// Defines control parameters used to enable/disable agent features
class TestFlowXmlAgentInit : public TestAgentInit {
public:
    TestFlowXmlAgentInit();
    virtual ~TestFlowXmlAgentInit();

    // Initialization virtual methods
    void CreateModules();
    void CreateDBTables();
    void RegisterDBClients();

private:
    DISALLOW_COPY_AND_ASSIGN(TestFlowXmlAgentInit);
};

#endif //  vnsw_agent_pkt_test_test_xml_flow_agent_init_hpp
