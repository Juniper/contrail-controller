/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <base/test/task_test_util.h>

#include <cmn/agent_cmn.h>
#include <cmn/agent_factory.h>
#include <init/agent_param.h>
#include <cfg/cfg_init.h>

#include <oper/operdb_init.h>
#include <ksync/ksync_init.h>
#include <ksync/test/ksync_test.h>
#include <uve/agent_uve.h>
#include <uve/test/agent_uve_test.h>

#include "test_agent_init.h"
TestAgentInit::TestAgentInit() : ContrailInitCommon() {
}

TestAgentInit::~TestAgentInit() {
    ksync_.reset();
    uve_.reset();
}

void TestAgentInit::ProcessOptions
    (const std::string &config_file, const std::string &program_name) {

    ContrailInitCommon::ProcessOptions(config_file, program_name);

    boost::program_options::variables_map var_map = agent_param()->var_map();
    if (var_map.count("disable-vhost")) {
        set_create_vhost(false);
    }

    if (var_map.count("disable-ksync")) {
        set_ksync_enable(false);
    }

    if (var_map.count("disable-services")) {
        set_services_enable(false);
    }

    if (var_map.count("disable-packet")) {
        set_packet_enable(false);
    }
}

/****************************************************************************
 * Initialization routines
 ***************************************************************************/
void TestAgentInit::FactoryInit() {
    AgentObjectFactory::Register<AgentUve>(boost::factory<AgentUveTest *>());
    AgentObjectFactory::Register<KSync>(boost::factory<KSyncTest *>());
}

// Create the basic modules for agent operation.
// Optional modules or modules that have different implementation are created
// by init module
void TestAgentInit::CreateModules() {
    ContrailInitCommon::CreateModules();
    pkt0_.reset(new TestPkt0Interface(agent(), "pkt0",
                *agent()->event_manager()->io_service()));
    agent()->pkt()->set_control_interface(pkt0_.get());

    uve_.reset(AgentObjectFactory::Create<AgentUve>
               (agent(), AgentUve::kBandwidthInterval));
    agent()->set_uve(uve_.get());

    ksync_.reset(AgentObjectFactory::Create<KSync>(agent()));
    agent()->set_ksync(ksync_.get());
}

/****************************************************************************
 * Shutdown routines
 ***************************************************************************/
void TestAgentInit::KSyncShutdown() {
    if (agent()->ksync()) {
        KSyncTest *ksync = static_cast<KSyncTest *>(agent()->ksync());
        ksync->Shutdown();
    }
}

void TestAgentInit::UveShutdown() {
    if (agent()->uve()) {
        agent()->uve()->Shutdown();
    }
}

void TestAgentInit::WaitForIdle() {
    task_util::WaitForIdle();
}
