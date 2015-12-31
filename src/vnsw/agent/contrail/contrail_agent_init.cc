/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <cmn/agent_cmn.h>
#include <cmn/agent_factory.h>
#include <init/agent_param.h>

#include <cfg/cfg_init.h>

#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <vrouter/ksync/ksync_init.h>
#include <uve/agent_uve.h>
#include <uve/agent_uve_stats.h>
#include <vrouter/stats_collector/agent_stats_collector.h>
#include <vrouter/flow_stats/flow_stats_collector.h>
#include <openstack/instance_service_server.h>
#include <port_ipc/rest_server.h>
#include <port_ipc/port_ipc_handler.h>

#include "contrail_agent_init.h"

ContrailAgentInit::ContrailAgentInit() : ContrailInitCommon() {
}

ContrailAgentInit::~ContrailAgentInit() {
    ksync_.reset();
    uve_.reset();
    stats_collector_.reset();
    flow_stats_manager_.reset();
}

void ContrailAgentInit::ProcessOptions
    (const std::string &config_file, const std::string &program_name) {
    ContrailInitCommon::ProcessOptions(config_file, program_name);
}

/****************************************************************************
 * Initialization routines
****************************************************************************/
void ContrailAgentInit::FactoryInit() {
    if (agent()->tsn_enabled() == false) {
        AgentObjectFactory::Register<AgentUveBase>
            (boost::factory<AgentUveStats *>());
    } else {
        AgentObjectFactory::Register<AgentUveBase>
            (boost::factory<AgentUve *>());
    }
    if (agent_param()->vrouter_on_nic_mode() || agent_param()->vrouter_on_host_dpdk()) {
        AgentObjectFactory::Register<KSync>(boost::factory<KSyncTcp *>());
    } else {
        AgentObjectFactory::Register<KSync>(boost::factory<KSync *>());
    }
    AgentObjectFactory::Register<FlowStatsCollector>(boost::factory<FlowStatsCollector *>());
}

void ContrailAgentInit::CreateModules() {
    ContrailInitCommon::CreateModules();

    if (agent_param()->vrouter_on_host_dpdk()) {
        pkt0_.reset(new Pkt0Socket("unix",
                    agent()->event_manager()->io_service()));
    } else if (agent_param()->vrouter_on_nic_mode()) {
        pkt0_.reset(new Pkt0RawInterface("pkt0",
                    agent()->event_manager()->io_service()));
    } else {
        pkt0_.reset(new Pkt0Interface("pkt0",
                    agent()->event_manager()->io_service()));
    }
    agent()->pkt()->set_control_interface(pkt0_.get());

    uve_.reset(AgentObjectFactory::Create<AgentUveBase>
               (agent(), AgentUveBase::kBandwidthInterval,
                AgentUveBase::kDefaultInterval,
                AgentUveBase::kIncrementalInterval));
    agent()->set_uve(uve_.get());

    if (agent()->tsn_enabled() == false) {
        stats_collector_.reset(new AgentStatsCollector(
                                   *(agent()->event_manager()->io_service()),
                                     agent()));
        agent()->set_stats_collector(stats_collector_.get());
        flow_stats_manager_.reset(new FlowStatsManager(agent()));
        flow_stats_manager_->Init(agent()->params()->flow_stats_interval(),
                                 agent()->params()->flow_cache_timeout());
        agent()->set_flow_stats_manager(flow_stats_manager_.get());
    }

    ksync_.reset(AgentObjectFactory::Create<KSync>(agent()));
    agent()->set_ksync(ksync_.get());

    rest_server_.reset(new RESTServer(agent()));
    agent()->set_rest_server(rest_server_.get());
}

void ContrailAgentInit::ConnectToController() {
    InstanceInfoServiceServerInit(agent());
}

/****************************************************************************
 * Shutdown routines
 ***************************************************************************/
void ContrailAgentInit::KSyncShutdown() {
    if (agent()->ksync()) {
        agent()->ksync()->Shutdown();
    }
}

void ContrailAgentInit::UveShutdown() {
    if (agent()->uve()) {
        agent()->uve()->Shutdown();
    }
}

void ContrailAgentInit::StatsCollectorShutdown() {
    if (agent()->stats_collector()) {
        agent()->stats_collector()->Shutdown();
    }
}

void ContrailAgentInit::FlowStatsCollectorShutdown() {
    if (agent()->flow_stats_manager()) {
        agent()->flow_stats_manager()->Shutdown();
    }
}

void ContrailAgentInit::WaitForIdle() {
    sleep(5);
}

void ContrailAgentInit::InitDone() {
    ContrailInitCommon::InitDone();

    /* Reads and processes port information written by nova-compute */
    PortIpcHandler pih(agent(), PortIpcHandler::kPortsDir,
                       !agent_param()->vrouter_on_host_dpdk());
    pih.ReloadAllPorts();
}

void ContrailAgentInit::ModulesShutdown() {
    ContrailInitCommon::ModulesShutdown();

    if (agent()->rest_server()) {
        agent()->rest_server()->Shutdown();
    }
}
