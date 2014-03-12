/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <io/event_manager.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>
#include <base/misc_utils.h>

#include <cmn/buildinfo.h>
#include <cmn/agent_cmn.h>

#include <init/agent_param.h>
#include <init/agent_init.h>

#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <openstack/instance_service_server.h>

void RouterIdDepInit() {
    InstanceInfoServiceServerInit(*(Agent::GetInstance()->GetEventManager()), 
                                  Agent::GetInstance()->GetDB());

    // Parse config and then connect
    VNController::Connect();
    LOG(DEBUG, "Router ID Dependent modules (Nova and BGP) INITIALIZED");
}

bool GetBuildInfo(std::string &build_info_str) {
    return MiscUtils::GetBuildInfo(MiscUtils::Agent, BuildInfo, build_info_str);
}

int main(int argc, char *argv[]) {
    string build_info;
    GetBuildInfo(build_info);
    MiscUtils::LogVersionInfo(build_info, Category::VROUTER);

    // Create agent 
    Agent agent;

    // Read agent parameters from config file and arguments
    AgentParam param;
    param.Init(argc, argv);

    // Initialize the agent-init control class
    AgentInit init;
    init.Init(&param, &agent);

    // Initialize agent and kick start initialization
    agent.Init(&param, &init);

    Agent::GetInstance()->GetEventManager()->Run();

    return 0;
}
