/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/string_generator.hpp>
#include <boost/program_options.hpp>
#include <base/logging.h>
#include <base/contrail_ports.h>

#include <base/task.h>
#include <io/event_manager.h>
#include <cmn/agent_cmn.h>
#include <cfg/init_config.h>
#include <oper/operdb_init.h>
#include <controller/controller_init.h>
#include <controller/controller_vrf_export.h>
#include <pkt/pkt_init.h>
#include <services/services_init.h>
#include <cfg/discovery_agent.h>
#include <ksync/ksync_init.h>
#include <openstack/instance_service_server.h>
#include <oper/vrf.h>
#include <oper/multicast.h>
#include <pugixml/pugixml.hpp>
#include <uve/uve_init.h>
#include <uve/uve_client.h>
#include <kstate/kstate.h>
#include <cfg/mirror_cfg.h>
#include <oper/mirror_table.h>
#include <pkt/proto.h>
#include <diag/diag.h>
#include <sandesh/common/vns_types.h>
#include <sandesh/common/vns_constants.h>
#include <base/misc_utils.h>
#include <cmn/buildinfo.h>
#include <vgw/vgw.h>

namespace opt = boost::program_options;

void RouterIdDepInit() {
    InstanceInfoServiceServerInit(*(Agent::GetInstance()->GetEventManager()), Agent::GetInstance()->GetDB());

    // Parse config and then connect
    VNController::Connect();
    LOG(DEBUG, "Router ID Dependent modules (Nova and BGP) INITIALIZED");
}

bool AgentInit::Run() {
    /*
     * Initialization sequence
     * 0> Read config store from kernel, if available, to replay the Nova config
     * 1> Create all DB table
     * 2> Register listener for DB tables
     * 3> Initialize data which updates entries in DB table
     * By above init sequence no notification to client will be lost
     */

    switch(state_) {
        case MOD_INIT:

            InitModules();
            state_ = STATIC_OBJ_OPERDB;
            // Continue with the next state

        case STATIC_OBJ_OPERDB: {
            VGwTable::CreateStaticObjects();
            OperDB::CreateStaticObjects(boost::bind(&AgentInit::Trigger, this));
            state_ = STATIC_OBJ_PKT;
            break;
        }

        case STATIC_OBJ_PKT:
            PktModule::CreateStaticObjects();
            state_ = CONFIG_INIT;
            // Continue with the next state

        case CONFIG_INIT:
            state_ = CONFIG_RUN;
            if (init_file_) {
                AgentConfig::Init(Agent::GetInstance()->GetDB(), init_file_, 
                                  boost::bind(&AgentInit::Trigger, this));
                ServicesModule::ConfigInit();
                break;
            }
            // else Continue with the next state

        case CONFIG_RUN:
            DiagTable::Init();
            if (create_vhost_) {
                //Update mac address of vhost interface with
                //that of ethernet interface
                KSync::UpdateVhostMac();
            }
 
            if (ksync_init_) {
                KSync::VnswIfListenerInit();
            }

            if (Agent::GetInstance()->GetRouterIdConfigured()) {
                RouterIdDepInit();
            } else {
                LOG(DEBUG, 
                    "Router ID Dependent modules (Nova & BGP) not initialized");
            }

            if (Agent::GetInstance()->GetDiscoveryServer().empty()) {
                Sandesh::InitGenerator(
                    g_vns_constants.ModuleNames.find(Module::VROUTER_AGENT)->second,
                    Agent::GetInstance()->GetHostName(), Agent::GetInstance()->GetEventManager(),
                    sandesh_port_);

                if ((collector_server_port_ != 0) && (!collector_server_.empty())) {
                    Sandesh::ConnectToCollector(collector_server_, collector_server_port_);
                }
            }
            Sandesh::SetLoggingParams(log_locally_, log_category_, log_level_);

            //Discover Services
            DiscoveryAgentClient::DiscoverServices();

            state_ = INIT_DONE;
            // Continue with the next state

        case INIT_DONE: {
            break;
        }

        default:
            assert(0);
    }

    return true;
}

void AgentInit::InitModules() {
    if (ksync_init_) {
        KSync::NetlinkInit();
        KSync::VRouterInterfaceSnapshot();
        KSync::ResetVRouter();
    }

    if (create_vhost_) {
        KSync::CreateVhostIntf();
    }

    CfgModule::CreateDBTables(Agent::GetInstance()->GetDB());
    OperDB::CreateDBTables(Agent::GetInstance()->GetDB());
    CfgModule::RegisterDBClients(Agent::GetInstance()->GetDB());
    Agent::GetInstance()->SetMirrorCfgTable(MirrorCfgTable::CreateMirrorCfgTable());
    Agent::GetInstance()->SetIntfMirrorCfgTable(IntfMirrorCfgTable::CreateIntfMirrorCfgTable());

    if (ksync_init_) {
        KSync::RegisterDBClients(Agent::GetInstance()->GetDB());
    }

    if (pkt_init_) {
        PktModule::Init(ksync_init_);
    }

    if (services_init_) {
        ServicesModule::Init(ksync_init_);
    }

    AgentUve::Init();
    MulticastHandler::Register();
}

int main(int argc, char *argv[]) {
    bool enable_local_logging = false;
    const string default_log_file = "<stdout>";
    string log_file;
    
    opt::options_description desc("Command line options");
    desc.add_options()
            ("help", "help message")
            ("config-file", opt::value<string>(), "Configuration file")
            ("create-vhost", "Create vhost interface")
            ("kernel-sync", "Disable kernel synchronization")
            ("services", "Disable services")
            ("packet-services", "Disable packet services")
            ("log-local", opt::bool_switch(&enable_local_logging),
                 "Enable local logging of sandesh messages")
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
            ("log-file", opt::value<string>()->default_value(default_log_file),
             "Filename for the logs to be written to")
            ("hypervisor", opt::value<string>(), "Type of hypervisor <kvm|xen>")
            ("xen-ll-port", opt::value<string>(), 
             "Port name on host for link-local network")
            ("xen-ll-ip-address", opt::value<string>(),
             "IP Address for the link local port")
            ("xen-ll-prefix-len", opt::value<int>(),
             "Prefix for link local IP Address")
            ("version", "Display version information")
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

    log_file = var_map["log-file"].as<string>();
    if (log_file == default_log_file) {
        LoggingInit();
    } else {
        LoggingInit(log_file);
    }

    bool ksync_init = true;
    if (var_map.count("kernel-sync")) {
        ksync_init = false;
    }
    bool services_init = true;
    if (var_map.count("services")) {
        services_init = false;
    }
    bool pkt_init = true;
    if (var_map.count("packet-services")) {
        pkt_init = false;
    }

    bool create_vhost = false;
    if (var_map.count("create-vhost")) {
        create_vhost = true;
    }

    int collector_port = 0;
    if (var_map.count("collector-port")) {
        collector_port = var_map["collector-port"].as<int>();
    }
    std::string collector_server;
    if (var_map.count("collector")) {
        collector_server = var_map["collector"].as<string>();
    }
    const char *init_file = NULL;
    if (var_map.count("config-file")) {
        init_file = var_map["config-file"].as<string>().c_str();
    }
    int sandesh_http_port = var_map["http-server-port"].as<int>();
    std::string log_level = var_map["log-level"].as<string>();
    std::string log_category = var_map["log-category"].as<string>();

    string hostname;
    if (var_map.count("host-name")) {
        hostname = var_map["host-name"].as<string>().c_str();
    } else {
        boost::system::error_code error;
        hostname = boost::asio::ip::host_name(error);
    }

    // Read config file
    AgentCmdLineParams cmd_line(collector_server, collector_port, log_file, 
                                init_file, enable_local_logging, log_level, 
                                log_category, sandesh_http_port, hostname);
    AgentConfig::Mode mode = AgentConfig::MODE_INVALID;
    bool xen_config_set = false;
    string port_name = "";
    int plen = -1;
    Ip4Address addr = Ip4Address::from_string("0.0.0.0");
    if (var_map.count("hypervisor")) {
        const std::string mode_str = var_map["hypervisor"].as<string>();
        if (mode_str == "kvm") {
            mode = AgentConfig::MODE_KVM;
        } else if (mode_str == "xen") {
            mode = AgentConfig::MODE_XEN;

            if (var_map.count("xen-ll-port")) {
                port_name = var_map["xen-ll-port"].as<string>();
            }

            if (var_map.count("xen-ll-ip-address")) {
                string addr_str = var_map["xen-ll-ip-address"].as<string>();
                boost::system::error_code ec;
                addr = Ip4Address::from_string(addr_str, ec);
                if (ec.value() != 0) {
                    LOG(ERROR, "Error parsing argument for xen-ll-ip-address "
                        "from <" << addr_str << ">");
                    exit(EINVAL);
                }
            }

            if (var_map.count("xen-ll-prefix-len")) {
                plen = var_map["xen-ll-prefix-len"].as<int>();
                if (plen <= 0 || plen >= 32) {
                    LOG(ERROR, "Error parsing argument for xen-ll-prefix-len "
                        "<" << plen << ">");
                    exit(EINVAL);
                }
            }
            xen_config_set = true;
        } else {
            LOG(ERROR, "Error unknown hypervisor <" << mode_str << ">");
            exit(EINVAL);
        }

    }
    /*
     * Do Agent::Init before AgentConfig::InitConfig because some of the
     * methods of Agent class could be invoked during AgentConfig::InitConfig
     */
    Agent::Init();
    Agent::GetInstance()->SetHostName(hostname);
    Agent::GetInstance()->SetProgramName(argv[0]);
    Agent::GetInstance()->SetSandeshPort(sandesh_http_port);
    string build_info;
    Agent::GetInstance()->GetBuildInfo(build_info);
    MiscUtils::LogVersionInfo(build_info, Category::VROUTER);

    //Read the config file
    AgentConfig::InitConfig(init_file, cmd_line);
    if (xen_config_set)
        AgentConfig::GetInstance()->SetXenInfo(port_name, addr, plen);
    if (mode != AgentConfig::MODE_INVALID)
        AgentConfig::GetInstance()->SetMode(mode);
    AgentConfig::GetInstance()->LogConfig();

    AgentStats::Init();

    AgentInit::Init(ksync_init, pkt_init, 
            services_init, init_file, sandesh_http_port, enable_local_logging,
            log_category, log_level, collector_server, collector_port,
            create_vhost);
    AgentInit::GetInstance()->Trigger();

    Agent::GetInstance()->GetEventManager()->Run();
    return 0;
}
