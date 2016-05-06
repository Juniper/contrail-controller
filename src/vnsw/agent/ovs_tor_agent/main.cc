/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include <base/misc_utils.h>
#include <cmn/buildinfo.h>
#include <cmn/agent_cmn.h>
#include <init/agent_param.h>
#include <ovs_tor_agent/tor_agent_param.h>
#include <ovs_tor_agent/tor_agent_init.h>

namespace opt = boost::program_options;
using std::cout;
using std::endl;
using std::string;

bool GetBuildInfo(std::string &build_info_str) {
    return MiscUtils::GetBuildInfo(MiscUtils::Agent, BuildInfo, build_info_str);
}

int main(int argc, char *argv[]) {
    TorAgentParam params;

    try {
        // Add ToR speicific options
        params.AddOptions();
        params.ParseArguments(argc, argv);
    } catch (...) {
        cout << "Invalid arguments";
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
        cout <<  build_info << endl;
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

    // Read agent parameters from config file and arguments
    params.Init(init_file, argv[0]);

    // Initialize TBB
    // Call to GetScheduler::GetInstance() will also create Task Scheduler
    TaskScheduler::Initialize(params.tbb_thread_count());

    // Initialize the agent-init control class
    TorAgentInit init;
    Agent *agent = init.agent();
    init.set_agent_param(&params);
    agent->set_agent_init(&init);

    string build_info;
    GetBuildInfo(build_info);
    MiscUtils::LogVersionInfo(build_info, Category::VROUTER);

    // kick start initialization
    int ret = 0;
    if ((ret = init.Start()) != 0) {
        return ret;
    }

    agent->event_manager()->RunWithExceptionHandling();

    return 0;
}
