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
#include <boost/functional/factory.hpp>
#include <cmn/agent_factory.h>

namespace opt = boost::program_options;

void RouterIdDepInit(Agent *agent) {
    InstanceInfoServiceServerInit(agent);

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
    uint16_t http_server_port = ContrailPorts::HttpPortAgent;

    opt::options_description desc("Command line options");
    desc.add_options()
        ("help", "help message")
        ("config_file", 
         opt::value<string>()->default_value(Agent::DefaultConfigFile()), 
         "Configuration file")
        ("version", "Display version information")
        ("COLLECTOR.server", opt::value<string>(), 
         "IP address of sandesh collector")
        ("COLLECTOR.port", opt::value<uint16_t>(), "Port of sandesh collector")
        ("CONTROL-NODE.server", 
         opt::value<std::vector<std::string> >()->multitoken(),
         "IP addresses of control nodes."
         " Max of 2 Ip addresses can be configured")
        ("DEFAULT.debug", "Enable debug logging")
        ("DEFAULT.flow_cache_timeout", 
         opt::value<uint16_t>()->default_value(Agent::kDefaultFlowCacheTimeout),
         "Flow aging time in seconds")
        ("DEFAULT.hostname", opt::value<string>(), 
         "Hostname of compute-node")
        ("DEFAULT.http_server_port", 
         opt::value<uint16_t>()->default_value(http_server_port), 
         "Sandesh HTTP listener port")
        ("DEFAULT.log_category", opt::value<string>()->default_value("*"),
         "Category filter for local logging of sandesh messages")
        ("DEFAULT.log_file", 
         opt::value<string>()->default_value(Agent::DefaultLogFile()),
         "Filename for the logs to be written to")
        ("DEFAULT.log_level", opt::value<string>()->default_value("SYS_DEBUG"),
         "Severity level for local logging of sandesh messages")
        ("DEFAULT.log_local", "Enable local logging of sandesh messages")
        ("DEFAULT.tunnel_type", opt::value<string>()->default_value("MPLSoGRE"),
         "Tunnel Encapsulation type <MPLSoGRE|MPLSoUDP|VXLAN>")
        ("DISCOVERY.server", opt::value<string>(), 
         "IP address of discovery server")
        ("DISCOVERY.max_control_nodes", opt::value<uint16_t>(), 
         "Maximum number of control node info to be provided by discovery "
         "service <1|2>")
        ("DNS.server", opt::value<std::vector<std::string> >()->multitoken(),
         "IP addresses of dns nodes. Max of 2 Ip addresses can be configured")
        ("HYPERVISOR.type", opt::value<string>()->default_value("kvm"), 
         "Type of hypervisor <kvm|xen|vmware>")
        ("HYPERVISOR.xen_ll_interface", opt::value<string>(), 
         "Port name on host for link-local network")
        ("HYPERVISOR.xen_ll_ip", opt::value<string>(),
         "IP Address and prefix or the link local port in ip/prefix format")
        ("HYPERVISOR.vmware_physical_port", opt::value<string>(),
         "Physical port used to connect to VMs in VMWare environment")
        ("LINK-LOCAL.max_system_flows", opt::value<uint16_t>(), 
         "Maximum number of link-local flows allowed across all VMs")
        ("LINK-LOCAL.max_vm_flows", opt::value<uint16_t>(), 
         "Maximum number of link-local flows allowed per VM")
        ("METADATA.metadata_proxy_secret", opt::value<string>(),
         "Shared secret for metadata proxy service")
        ("NETWORKS.control_network_ip", opt::value<string>(),
         "control-channel IP address used by WEB-UI to connect to vnswad")
        ("VIRTUAL-HOST-INTERFACE.name", opt::value<string>(),
         "Name of virtual host interface")
        ("VIRTUAL-HOST-INTERFACE.ip", opt::value<string>(), 
         "IP address and prefix in ip/prefix_len format")
        ("VIRTUAL-HOST-INTERFACE.gateway", opt::value<string>(), 
         "Gateway IP address for virtual host")
        ("VIRTUAL-HOST-INTERFACE.physical_interface", opt::value<string>(), 
         "Physical interface name to which virtual host interface maps to")
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

    // Create agent 
    Agent agent;

    // Read agent parameters from config file and arguments
    AgentParam param(&agent);
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
