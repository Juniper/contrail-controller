/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

// in Boost this macro defaults to 6 but we're using FACTORY_TYPE_N8 so we need to define it manually
#define BOOST_FUNCTIONAL_FORWARD_ADAPTER_MAX_ARITY 8

#include <boost/functional/forward_adapter.hpp>

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
#include <vrouter/flow_stats/session_stats_collector.h>
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
            (boost::forward_adapter<boost::factory<AgentUveStats *> >
                (boost::factory<AgentUveStats *>()));
    } else {
        AgentObjectFactory::Register<AgentUveBase>
            (boost::forward_adapter<boost::factory<AgentUve *> >(boost::factory<AgentUve *>()));
    }
    if (agent_param()->vrouter_on_nic_mode() || agent_param()->vrouter_on_host_dpdk()) {
#ifdef _WIN32
        LOG(ERROR, "KSyncTcp is not supported on Windows");
        assert(0);
#else
#ifdef AGENT_VROUTER_TCP
        AgentObjectFactory::Register<KSync>
            (boost::forward_adapter<boost::factory<KSyncTcp *> >(boost::factory<KSyncTcp *>()));
#else
        AgentObjectFactory::Register<KSync>
            (boost::forward_adapter<boost::factory<KSyncUds *> >(boost::factory<KSyncUds *>()));
#endif
#endif
    } else {
        AgentObjectFactory::Register<KSync>
            (boost::forward_adapter<boost::factory<KSync *> >(boost::factory<KSync *>()));
    }
    AgentObjectFactory::Register<FlowStatsCollector>
        (boost::forward_adapter<boost::factory<FlowStatsCollector *> >
            (boost::factory<FlowStatsCollector *>()));
    AgentObjectFactory::Register<SessionStatsCollector>
        (boost::forward_adapter<boost::factory<SessionStatsCollector *> >
            (boost::factory<SessionStatsCollector *>()));
}

void ContrailAgentInit::CreateModules() {
    ContrailInitCommon::CreateModules();
    if (agent_param()->cat_is_agent_mocked()) {
        Pkt0Socket::CreateMockAgent(agent()->AgentGUID());
    }

    if (agent_param()->vrouter_on_host_dpdk()) {
#ifdef _WIN32
        LOG(ERROR, "Pkt0Socket is not supported on Windows");
        assert(0);
#else
        pkt0_.reset(new Pkt0Socket("unix",
                    agent()->event_manager()->io_service()));
#endif
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
                agent()->params()->vmi_vm_vn_uve_interval_msecs(),
                AgentUveBase::kIncrementalInterval));
    agent()->set_uve(uve_.get());

    if (agent()->tsn_enabled() == false) {
        stats_collector_.reset(new AgentStatsCollector
                                   (*(agent()->event_manager()->io_service()),
                                    agent()));
        agent()->set_stats_collector(stats_collector_.get());
    }
    flow_stats_manager_.reset(new FlowStatsManager(agent()));
    agent()->set_flow_stats_manager(flow_stats_manager_.get());

    if (!agent_param()->cat_is_dpdk_mocked()) {
        ksync_.reset(AgentObjectFactory::Create<KSync>(agent()));
        agent()->set_ksync(ksync_.get());
    }
    else {
      ksync_.reset();
    }

    std::string newkPortsDir =  PortIpcHandler::kPortsDir;

    if (agent_param()->cat_is_agent_mocked()) {
       newkPortsDir = "/tmp/" + agent()->AgentGUID() + newkPortsDir;
    }

    port_ipc_handler_.reset(new PortIpcHandler(agent(),
                                               newkPortsDir));
    agent()->set_port_ipc_handler(port_ipc_handler_.get());

    rest_server_.reset(new RESTServer(agent()));
    agent()->set_rest_server(rest_server_.get());
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

    if (agent()->port_ipc_handler()) {
        agent()->port_ipc_handler()->InitDone();
    }

    if (agent()->rest_server()) {
        /* Open REST API port for port add/change/deletes */
        agent()->rest_server()->InitDone();
    }
    /* Reads and processes port information written by nova-compute */
    PortIpcHandler *pih = agent()->port_ipc_handler();
    if (pih) {
        pih->ReloadAllPorts(!agent_param()->vrouter_on_host_dpdk());
    }
}

void ContrailAgentInit::ModulesShutdown() {
    ContrailInitCommon::ModulesShutdown();

    if (agent()->rest_server()) {
        agent()->rest_server()->Shutdown();
    }

    if (agent()->port_ipc_handler()) {
        agent()->port_ipc_handler()->Shutdown();
    }
}
