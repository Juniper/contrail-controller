/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/os.h>
#include <base/test/task_test_util.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent_factory.h>
#include <init/agent_param.h>
#include <cfg/cfg_init.h>

#include <oper/operdb_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/ksync/test/ksync_test.h>
#include <uve/agent_uve.h>
#include <uve/test/agent_uve_test.h>
#include "test/test_init.h"
#include <test/test_agent_init.h>
#include "test_xml_agent_init.h"

TestXmlAgentInit::TestXmlAgentInit() : TestAgentInit() {
}

TestXmlAgentInit::~TestXmlAgentInit() {
}

/****************************************************************************
 * Initialization routines
 ***************************************************************************/
// Create the basic modules for agent operation.
// Optional modules or modules that have different implementation are created
// by init module
void TestXmlAgentInit::CreateModules() {
    TestAgentInit::CreateModules();
}

void TestXmlAgentInit::CreateDBTables() {
    TestAgentInit::CreateDBTables();
}

void TestXmlAgentInit::RegisterDBClients() {
    TestAgentInit::RegisterDBClients();
}

TestClient *PhysicalDeviceTestInit(const char *init_file, bool ksync_init) {
    TestClient *client = new TestClient(new TestXmlAgentInit());
    TestXmlAgentInit *init =
        static_cast<TestXmlAgentInit *>(client->agent_init());
    Agent *agent = client->agent();

    AgentParam *param = client->param();
    init->set_agent_param(param);
    // Read agent parameters from config file and arguments
    init->ProcessOptions(init_file, "test");
    param->set_agent_stats_interval(AgentParam::kAgentStatsInterval);
    param->set_flow_stats_interval(AgentParam::kFlowStatsInterval);
    param->set_vrouter_stats_interval(AgentParam::kVrouterStatsInterval);

    // Initialize the agent-init control class
    int introspect_port = 0;
    Sandesh::InitGeneratorTest("VNSWAgent", "Agent", "Test", "Test",
                               agent->event_manager(), introspect_port, NULL);

    init->set_ksync_enable(false);
    init->set_packet_enable(true);
    init->set_services_enable(true);
    init->set_create_vhost(false);
    init->set_uve_enable(true);
    init->set_vgw_enable(false);
    init->set_router_id_dep_enable(false);
    param->set_test_mode(true);
    agent->set_ksync_sync_mode(true);

    // Initialize agent and kick start initialization
    init->Start();
    WaitForInitDone(agent);

    client->Init();
    client->WaitForIdle();
    client->SetFlowFlushExclusionPolicy();

    AsioRun();

    return client;
}
