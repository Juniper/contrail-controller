/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <iostream>

#include <boost/asio/ip/host_name.hpp>
#include <boost/program_options.hpp>
#include <boost/uuid/string_generator.hpp>
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

static int agent_main(int argc, char *argv[]) {
    boost::system::error_code error;

    string config_file = "/etc/contrail/vrouter.conf";
    string hostname(boost::asio::ip::host_name(error));
    bool log_local = false;
    bool disable_vhost = false;
    bool disable_ksync = false;
    bool disable_services = false;
    bool disable_packet_services = false;

    // Command line only options.
    opt::options_description generic("Generic options");
    generic.add_options()
        ("conf-file", opt::value<string>()->default_value(config_file),
             "Configuration file")
        ("help", "help message")
        ("version", "Display version information")
    ;

    // Command line and config file options.
    opt::options_description config("Configuration options");
    config.add_options()
       ("help", "help message")
       ("config-file", opt::value<string>(), "Configuration file")
       ("version", "Display version information")

       ("COLLECTOR.port", opt::value<uint16_t>()->default_value(ContrailPorts::CollectorPort),
            "Port of sandesh collector")
       ("COLLECTOR.server", opt::value<string>()->default_value(""),
            "IP address of sandesh collector")

       ("DEFAULTS.config-file", opt::value<string>(), "Agent Configuration file")
       ("DEFAULTS.hostname", opt::value<string>()->default_value(hostname),
            "Specific Host Name")
       ("DEFAULTS.http-server-port",
            opt::value<uint16_t>()->default_value(ContrailPorts::HttpPortAgent),
            "Sandesh HTTP listener port")

       ("HYPERVISOR.type", opt::value<string>()->default_value("kvm"),
            "Type of hypervisor <kvm|xen>")
       ("HYPERVISOR.xen-ll-port",
           opt::value<string>()->default_value(""), 
           "Port name on host for link-local network")
       ("HYPERVISOR.xen-ll-ip-address",
            opt::value<string>()->default_value(""),
            "IP Address for the link local port")
       ("HYPERVISOR.xen-ll-prefix-len",
            opt::value<int>()->default_value(0),
            "IP Prefix Length for the link local address")
       ("HYPERVISOR.vmware-physical-port",
            opt::value<string>()->default_value(""), 
            "Physical port used to connect to VMs in VMWare environement")

       ("KERNEL.disable-vhost", opt::bool_switch(&disable_vhost),
            "Disable vhost interface")
       ("KERNEL.disable-ksync", opt::bool_switch(&disable_ksync),
            "Disable kernel synchronization")
       ("KERNEL.disable-services", opt::bool_switch(&disable_services),
            "Disable services")
       ("KERNEL.disable-packet", opt::bool_switch(&disable_packet_services),
            "Disable packet services")

       ("LOG.category", opt::value<string>()->default_value(""),
           "Category filter for local logging of sandesh messages")
       ("LOG.file", opt::value<string>()->default_value("<stdout>"),
        "Filename for the logs to be written to")
       ("LOG.level", opt::value<string>()->default_value("SYS_DEBUG"),
           "Severity level for local logging of sandesh messages")
       ("LOG.local", opt::bool_switch(&log_local),
            "Enable local logging of sandesh messages")
       ;

  
    opt::options_description config_file_options;
    config_file_options.add(config);
  
    opt::options_description cmdline_options("Allowed options");
    cmdline_options.add(generic).add(config);
  
    vector<string> tokens;
    string line;
    opt::variables_map var_map;
  
    // Process options off command line first.
    opt::store(opt::parse_command_line(argc, argv, cmdline_options), var_map);

    // Process options off configuration file.
    GetOptValue<string>(var_map, config_file, "conf-file", "");
    ifstream config_file_in;
    config_file_in.open(config_file.c_str());
    if (config_file_in.good()) {
        opt::store(opt::parse_config_file(config_file_in, config_file_options),
                   var_map);
    }
    config_file_in.close();

    opt::notify(var_map);
  
    if (var_map.count("help")) {
        std::cout << cmdline_options << std::endl;
        exit(0);
    }
  
    if (var_map.count("version")) {
        string build_info;
        MiscUtils::GetBuildInfo(MiscUtils::Agent, BuildInfo, build_info);
        std::cout <<  build_info << std::endl;
        exit(0);
    }

    string init_file = "";
    if (var_map.count("DEFAULTS.config-file")) {
        GetOptValue<string>(var_map, init_file, "DEFAULTS.config-file", "");
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
    param.Init(init_file, argv[0], var_map, log_local);

    // Initialize the agent-init control class
    AgentInit init;
    init.Init(&param, &agent, disable_vhost, disable_ksync, disable_services,
              disable_packet_services);

    // Initialize agent and kick start initialization
    agent.Init(&param, &init);

    Agent::GetInstance()->GetEventManager()->Run();

    return 0;
}

int main(int argc, char *argv[]) {
    try {
        return agent_main(argc, argv);
    } catch (boost::program_options::error &e) {
        LOG(ERROR, "Error " << e.what());
        cout << "Error " << e.what();
    } catch (...) {
        LOG(ERROR, "Options Parser: Caught fatal unknown exception");
        cout << "Options Parser: Caught fatal unknown exception";
    }

    return(-1);
}
