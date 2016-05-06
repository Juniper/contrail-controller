/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

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
#include <vrouter/flow_stats/test/flow_stats_collector_test.h>

#include "test_agent_init.h"
TestAgentInit::TestAgentInit() : ContrailInitCommon() {
}

TestAgentInit::~TestAgentInit() {
    ksync_.reset();
    uve_.reset();
    flow_stats_manager_.reset();
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

void TestAgentInit::ProcessComputeAddress(AgentParam *param) {
    ContrailInitCommon::ProcessComputeAddress(param);
}

/****************************************************************************
 * Initialization routines
 ***************************************************************************/
void TestAgentInit::FactoryInit() {
    AgentObjectFactory::Register<AgentUveBase>(boost::factory<AgentUveBaseTest *>());
    AgentObjectFactory::Register<KSync>(boost::factory<KSyncTest *>());
    AgentObjectFactory::Register<FlowStatsCollector>(boost::factory<FlowStatsCollectorTest *>());
}

// Create the basic modules for agent operation.
// Optional modules or modules that have different implementation are created
// by init module
void TestAgentInit::CreateModules() {
    ContrailInitCommon::CreateModules();
    pkt0_.reset(new TestPkt0Interface(agent(), "pkt0",
                *agent()->event_manager()->io_service()));
    agent()->pkt()->set_control_interface(pkt0_.get());

    uve_.reset(AgentObjectFactory::Create<AgentUveBase>
               (agent(), AgentUveBase::kBandwidthInterval,
                TestAgentInit::kDefaultInterval,
                TestAgentInit::kIncrementalInterval));
    agent()->set_uve(uve_.get());

    if (agent()->tsn_enabled() == false) {
        stats_collector_.reset(new AgentStatsCollectorTest(
                                   *(agent()->event_manager()->io_service()),
                                   agent()));
        agent()->set_stats_collector(stats_collector_.get());
    }

    flow_stats_manager_.reset(new FlowStatsManager(agent()));
    flow_stats_manager_->Init(agent()->params()->flow_stats_interval(),
            agent()->params()->flow_cache_timeout());
    agent()->set_flow_stats_manager(flow_stats_manager_.get());

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

void TestAgentInit::StatsCollectorShutdown() {
    if (agent()->stats_collector()) {
        agent()->stats_collector()->Shutdown();
    }
}

void TestAgentInit::FlowStatsCollectorShutdown() {
    if (agent()->flow_stats_manager()) {
        agent()->flow_stats_manager()->Shutdown();
    }
}

void TestAgentInit::WaitForIdle() {
    task_util::WaitForIdle(3);
}
