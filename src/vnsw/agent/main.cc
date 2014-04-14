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
#include <init/agent_init.h>

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
#include <vgw/vgw.h>
#include <boost/functional/factory.hpp>
#include <cmn/agent_factory.h>

namespace opt = boost::program_options;

void RouterIdDepInit() {
    InstanceInfoServiceServerInit(*(Agent::GetInstance()->GetEventManager()), Agent::GetInstance()->GetDB());

    // Parse config and then connect
    VNController::Connect();
    LOG(DEBUG, "Router ID Dependent modules (Nova and BGP) INITIALIZED");
}

bool GetBuildInfo(std::string &build_info_str) {
    return MiscUtils::GetBuildInfo(MiscUtils::Agent, BuildInfo, build_info_str);
}

void FactoryInit() {
    AgentObjectFactory::Register<AgentUve>(boost::factory<AgentUve *>());
    AgentObjectFactory::Register<KSync>(boost::factory<KSync *>());
}

int main(int argc, char *argv[]) {
    opt::options_description desc("Command line options");
    desc.add_options()
        ("help", "help message")
        ("config-file", opt::value<string>(), "Configuration file")
        ("disable-vhost", "Create vhost interface")
        ("disable-ksync", "Disable kernel synchronization")
        ("disable-services", "Disable services")
        ("disable-packet", "Disable packet services")
        ("log-local", "Enable local logging of sandesh messages")
        ("log-level", opt::value<string>()->default_value("SYS_DEBUG"),
         "Severity level for local logging of sandesh messages")
        ("log-category", opt::value<string>()->default_value(""),
         "Category filter for local logging of sandesh messages")
        ("collector", opt::value<string>(), "IP address of sandesh collector")
        ("collector-port", opt::value<int>(), "Port of sandesh collector")
        ("http-server-port",
         opt::value<int>()->default_value(ContrailPorts::HttpPortAgent),
         "Sandesh HTTP listener port")
        ("host-name", opt::value<string>(), "Specific Host Name")
        ("log-file", opt::value<string>(),
         "Filename for the logs to be written to")
        ("hypervisor", opt::value<string>(), "Type of hypervisor <kvm|xen>")
        ("xen-ll-port", opt::value<string>(), 
         "Port name on host for link-local network")
        ("xen-ll-ip-address", opt::value<string>(),
         "IP Address for the link local port")
        ("xen-ll-prefix-len", opt::value<int>(),
         "Prefix for link local IP Address")
        ("vmware-physical-port", opt::value<string>(),
         "Physical port used to connect to VMs in VMWare environment")
        ("version", "Display version information")
        ("debug", "Enable debug logging")
        ;
    opt::variables_map var_map;
    try {
        opt::store(opt::parse_command_line(argc, argv, desc), var_map);
        opt::notify(var_map);
    } catch (...) {
        cout << "Invalid arguments. ";
        cout << desc << endl;
        exit(0);
    }

    if (var_map.count("help")) {
        cout << desc << endl;
        exit(0);
    }

    if (var_map.count("version")) {
        string build_info;
        MiscUtils::GetBuildInfo(MiscUtils::Agent, BuildInfo, build_info);
        cout <<  build_info << endl;
        exit(0);
    }

    string init_file = "";
    if (var_map.count("config-file")) {
        init_file = var_map["config-file"].as<string>();
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

    // Create agent 
    Agent agent;

    // Read agent parameters from config file and arguments
    AgentParam param;
    param.Init(init_file, argv[0], var_map);

    // Initialize the agent-init control class
    AgentInit init;
    init.Init(&param, &agent, var_map);

    FactoryInit();

    // Initialize agent and kick start initialization
    agent.Init(&param, &init);

    Agent::GetInstance()->GetEventManager()->Run();

    return 0;
}
