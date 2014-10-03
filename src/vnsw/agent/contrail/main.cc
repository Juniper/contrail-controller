/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/string_generator.hpp>
#include <boost/program_options.hpp>
#include <base/logging.h>
#include <base/contrail_ports.h>

#include <pugixml/pugixml.hpp>

#include <base/task.h>
#include <io/event_manager.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>
#include <base/misc_utils.h>

#include <cmn/buildinfo.h>
#include <cmn/agent_cmn.h>

#include <cfg/cfg_init.h>
#include <cfg/cfg_mirror.h>
#include <cfg/discovery_agent.h>

#include <init/agent_param.h>

#include <oper/operdb_init.h>
#include <oper/vrf.h>
#include <oper/multicast.h>
#include <oper/mirror_table.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <ksync/ksync_init.h>
#include <openstack/instance_service_server.h>
#include <uve/agent_uve.h>
#include <kstate/kstate.h>
#include <pkt/proto.h>
#include <diag/diag.h>
#include <boost/functional/factory.hpp>
#include <cmn/agent_factory.h>

#include "contrail_agent_init.h"
namespace opt = boost::program_options;

void RouterIdDepInit(Agent *agent) {
    InstanceInfoServiceServerInit(agent);

    // Parse config and then connect
    Agent::GetInstance()->controller()->Connect();
    LOG(DEBUG, "Router ID Dependent modules (Nova and BGP) INITIALIZED");
}

bool GetBuildInfo(std::string &build_info_str) {
    return MiscUtils::GetBuildInfo(MiscUtils::Agent, BuildInfo, build_info_str);
}

int main(int argc, char *argv[]) {
    // Initialize the agent-init control class
    ContrailAgentInit init;
    Agent *agent = init.agent();
    AgentParam params(agent);

    try {
        params.ParseArguments(argc, argv);
    } catch (...) {
        cout << "Invalid arguments. ";
        cout << params.options() << endl;
        exit(0);
    }

    opt::variables_map var_map = params.var_map();
    if (var_map.count("help")) {
        cout << params.options() << endl;
        exit(0);
    }

    if (var_map.count("version")) {
        string build_info;
        MiscUtils::GetBuildInfo(MiscUtils::Agent, BuildInfo, build_info);
        cout << params.options() << endl;
        exit(0);
    }

    string init_file = "";
    if (var_map.count("config_file")) {
        init_file = var_map["config_file"].as<string>();
        struct stat s;
        if (stat(init_file.c_str(), &s) != 0) {
            LOG(ERROR, "Error opening config file <" << init_file 
                << ">. Error number <" << errno << ">");
            exit(EINVAL);
        }
    }

    string build_info;
    GetBuildInfo(build_info);
    MiscUtils::LogVersionInfo(build_info, Category::VROUTER);

    init.set_agent_param(&params);
    // Read agent parameters from config file and arguments
    init.ProcessOptions(init_file, argv[0]);

    // kick start initialization
    int ret = 0;
    if ((ret = init.Start()) != 0) {
        return ret;
    }

    agent->event_manager()->RunWithExceptionHandling();

    return 0;
}
