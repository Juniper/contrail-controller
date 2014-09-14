/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#include <cmn/agent_cmn.h>
#include <cmn/agent_factory.h>
#include <cmn/agent_param.h>

#include <cfg/cfg_init.h>

#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <ksync/ksync_init.h>
#include <uve/agent_uve.h>
#include <openstack/instance_service_server.h>

#include "contrail_agent_init.h"

ContrailAgentInit::ContrailAgentInit() : ContrailInitCommon() {
}

ContrailAgentInit::~ContrailAgentInit() {
    ksync_.reset();
    uve_.reset();
}

void ContrailAgentInit::ProcessOptions
    (const std::string &config_file, const std::string &program_name,
     const boost::program_options::variables_map &var_map) {

    ContrailInitCommon::ProcessOptions(config_file, program_name, var_map);
}

/****************************************************************************
 * Initialization routines
****************************************************************************/
void ContrailAgentInit::FactoryInit() {
    AgentObjectFactory::Register<AgentUve>(boost::factory<AgentUve *>());
    AgentObjectFactory::Register<KSync>(boost::factory<KSync *>());
}

void ContrailAgentInit::CreateModules() {
    ContrailInitCommon::CreateModules();

    pkt0_.reset(new Pkt0Interface("pkt0",
                                  agent()->event_manager()->io_service()));
    agent()->pkt()->set_control_interface(pkt0_.get());

    uve_.reset(AgentObjectFactory::Create<AgentUve>
               (agent(), AgentUve::kBandwidthInterval));
    agent()->set_uve(uve_.get());

    ksync_.reset(AgentObjectFactory::Create<KSync>(agent()));
    agent()->set_ksync(ksync_.get());

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

void ContrailAgentInit::WaitForIdle() {
    sleep(5);
}
