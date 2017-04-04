/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */


#include <csignal>
#include <fstream>
#include <iostream>

#include <boost/asio/ip/host_name.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/program_options.hpp>

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "nodeinfo_types.h"
#include "base/connection_info.h"
#include "base/logging.h"
#include "base/misc_utils.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_ifmap.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_ifmap_sandesh.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_xmpp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/xmpp_message_builder.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "control-node/buildinfo.h"
#include "control-node/control_node.h"
#include "control-node/options.h"
#include "db/db_graph.h"
#include "ifmap/client/config_client_manager.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_sandesh_context.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_xmpp.h"
#include "io/event_manager.h"
#include "sandesh/common/vns_constants.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/sandesh_http.h"
#include "sandesh/sandesh_trace.h"
#include "schema/bgp_schema_types.h"
#include "schema/vnc_cfg_types.h"
#include "xmpp/sandesh/xmpp_peer_info_types.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_sandesh.h"
#include "xmpp/xmpp_server.h"

using namespace std;
using namespace boost::asio::ip;
using process::ConnectionInfo;
using process::ConnectionStateManager;
using process::GetProcessStateCb;
using process::ProcessState;
using process::ConnectionType;
using process::ConnectionTypeName;
using process::g_process_info_constants;

static EventManager evm;
static Options options;

static string FileRead(const char *filename) {
    ifstream file(filename);
    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

static void IFMap_Initialize(IFMapServer *server, ConfigClientManager *mgr) {
    IFMapLinkTable_Init(server->database(), server->graph());
    vnc_cfg_JsonParserInit(mgr->config_json_parser());
    vnc_cfg_Server_ModuleInit(server->database(), server->graph());
    bgp_schema_JsonParserInit(mgr->config_json_parser());
    bgp_schema_Server_ModuleInit(server->database(), server->graph());
    server->Initialize();
}

static XmppServer *CreateXmppServer(EventManager *evm, Options *options,
                                    XmppChannelConfig *xmpp_cfg) {

    // XmppChannel Configuration
    xmpp_cfg->endpoint.port(options->xmpp_port());
    xmpp_cfg->FromAddr = XmppInit::kControlNodeJID;
    xmpp_cfg->auth_enabled = options->xmpp_auth_enabled();
    xmpp_cfg->tcp_hold_time = options->tcp_hold_time();
    xmpp_cfg->gr_helper_disable = options->gr_helper_xmpp_disable();

    if (xmpp_cfg->auth_enabled) {
        xmpp_cfg->path_to_server_cert = options->xmpp_server_cert();
        xmpp_cfg->path_to_server_priv_key = options->xmpp_server_key();
        xmpp_cfg->path_to_ca_cert = options->xmpp_ca_cert();
    }

    // Create XmppServer
    XmppServer *xmpp_server;
    xmpp_server = new XmppServer(evm, options->hostname(), xmpp_cfg);
    if (!xmpp_server->Initialize(options->xmpp_port(), true)) {
        return NULL;
    } else {
        return (xmpp_server);
    }
}

static void WaitForIdle() {
    static const int kTimeout = 15;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    for (int i = 0; i < (kTimeout * 1000); i++) {
        if (scheduler->IsEmpty()) {
            break;
        }
        usleep(1000);
    }
}

// Shutdown various server objects used in the control-node.
static void ShutdownServers(
    boost::scoped_ptr<BgpXmppChannelManager> *channel_manager) {

    // Bring down bgp server, xmpp server, etc. in the right order.
    BgpServer *bgp_server = (*channel_manager)->bgp_server();
    XmppServer *xmpp_server = (*channel_manager)->xmpp_server();

    // Shutdown Xmpp server first.
    xmpp_server->Shutdown();
    WaitForIdle();

    // Wait until all XMPP connections are cleaned up.
    for (int cnt = 0; xmpp_server->ConnectionCount() != 0 && cnt < 15; cnt++) {
        sleep(1);
    }

    // Shutdown BGP server.
    bgp_server->Shutdown();
    WaitForIdle();

    // Wait until all routing-instances are cleaned up.
    for (int cnt = 0;
         bgp_server->routing_instance_mgr()->count() != 0 && cnt < 15;
         cnt++) {
        sleep(1);
    }

    channel_manager->reset();
    TcpServerManager::DeleteServer(xmpp_server);
    ControlNode::Shutdown();

    ConnectionStateManager::
        GetInstance()->Shutdown();

    // Do sandesh cleanup.
    Sandesh::Uninit();
    WaitForIdle();
    SandeshHttp::Uninit();
    WaitForIdle();
}

// Trigger graceful shutdown of control-node process.
//
// IO (evm) is shutdown first. Afterwards, main() resumes, shutting down rest
// of the objects, and eventually exit()s.
void ControlNodeShutdown() {
    static bool shutdown_;

    if (shutdown_) return;
    shutdown_ = true;

    // Shutdown event manager first to stop all IO activities.
    evm.Shutdown();
}

static void ControlNodeGetProcessStateCb(const BgpServer *bgp_server,
    const ConfigClientManager *config_client_manager,
    const std::vector<ConnectionInfo> &cinfos,
    ProcessState::type &state, std::string &message,
    std::vector<ConnectionTypeName> expected_connections) {
    GetProcessStateCb(cinfos, state, message, expected_connections);
    if (state == ProcessState::NON_FUNCTIONAL)
        return;
    if (!config_client_manager->GetEndOfRibComputed()) {
        state = ProcessState::NON_FUNCTIONAL;
        message = "IFMap Server End-Of-RIB not computed";
    } else if (!bgp_server->HasSelfConfiguration()) {
        state = ProcessState::NON_FUNCTIONAL;
        message = "No BGP configuration for self";
    } else if (bgp_server->admin_down()) {
        state = ProcessState::NON_FUNCTIONAL;
        message = "BGP is administratively down";
    }
}

void ReConfigSignalHandler(int signum) {
    options.ParseReConfig();
}

void InitializeSignalHandlers() {
    srand(unsigned(time(NULL)));
    signal(SIGHUP, ReConfigSignalHandler);
}

int main(int argc, char *argv[]) {
    bool sandesh_generator_init = true;

    // Process options from command-line and configuration file.
    if (!options.Parse(evm, argc, argv)) {
        exit(-1);
    }

    InitializeSignalHandlers();

    ControlNode::SetProgramName(argv[0]);
    Module::type module = Module::CONTROL_NODE;
    string module_name = g_vns_constants.ModuleNames.find(module)->second;
    std::string log_property_file = options.log_property_file();
    if (log_property_file.size()) {
        LoggingInit(log_property_file);
    } else {
        LoggingInit(options.log_file(), options.log_file_size(),
                    options.log_files_count(), options.use_syslog(),
                    options.syslog_facility(), module_name,
                    SandeshLevelTolog4Level(
                        Sandesh::StringToLevel(options.log_level())));
    }

    int num_threads_to_tbb = TaskScheduler::GetDefaultThreadCount() +
        ConfigClientManager::GetNumWorkers();
    TaskScheduler::Initialize(num_threads_to_tbb, &evm);
    TaskScheduler::GetInstance()->SetTrackRunTime(
        options.task_track_run_time());
    BgpServer::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();

    BgpSandeshContext sandesh_context;
    RegisterSandeshShowIfmapHandlers(&sandesh_context);
    RegisterSandeshShowXmppExtensions(&sandesh_context);
    Sandesh::set_send_rate_limit(options.sandesh_send_rate_limit());
    Sandesh::SetLoggingParams(options.log_local(), options.log_category(),
                              options.log_level());

    // Disable logging -- for test purposes only.
    if (options.log_disable()) {
        SetLoggingDisabled(true);
    }

    ControlNode::SetTestMode(options.test_mode());

    boost::scoped_ptr<BgpServer> bgp_server(new BgpServer(&evm));
    sandesh_context.set_test_mode(ControlNode::GetTestMode());
    sandesh_context.bgp_server = bgp_server.get();
    bgp_server->set_gr_helper_disable(options.gr_helper_bgp_disable());

    ConnectionStateManager::GetInstance();

    DB config_db(TaskScheduler::GetInstance()->GetTaskId("db::IFMapTable"));
    DBGraph config_graph;
    IFMapServer ifmap_server(&config_db, &config_graph, evm.io_service());

    // TODO Coming Soon
    ConfigClientManager *config_client_manager =
        new ConfigClientManager(&evm, &ifmap_server, options.hostname(),
                                module_name, options.configdb_options());
    IFMap_Initialize(&ifmap_server, config_client_manager);
    ifmap_server.set_config_manager(config_client_manager);

    BgpIfmapConfigManager *config_manager =
            static_cast<BgpIfmapConfigManager *>(bgp_server->config_manager());
    config_manager->Initialize(&config_db, &config_graph, options.hostname());
    ControlNode::SetHostname(options.hostname());
    BgpConfigParser parser(&config_db);
    parser.Parse(FileRead(options.bgp_config_file().c_str()));

    // TODO:  Initialize throws an exception (via boost) in case the
    // user does not have permissions to bind to the port.
    bgp_server->rtarget_group_mgr()->Initialize();
    LOG(DEBUG, "Starting Bgp Server at port " << options.bgp_port());
    if (!bgp_server->session_manager()->Initialize(options.bgp_port()))
        exit(1);

    // Create Xmpp Server.
    XmppChannelConfig xmpp_cfg(false);
    xmpp_cfg.dscp_value = bgp_server->global_qos()->control_dscp();
    XmppServer *xmpp_server = CreateXmppServer(&evm, &options, &xmpp_cfg);
    if (xmpp_server == NULL) {
        exit(1);
    }

    // Create BGP and IFMap channel managers.
    boost::scoped_ptr<BgpXmppChannelManager> bgp_peer_manager(
        new BgpXmppChannelManager(xmpp_server, bgp_server.get()));
    sandesh_context.xmpp_peer_manager = bgp_peer_manager.get();
    IFMapChannelManager ifmap_channel_mgr(xmpp_server, &ifmap_server);
    ifmap_server.set_ifmap_channel_manager(&ifmap_channel_mgr);

    XmppSandeshContext xmpp_sandesh_context;
    xmpp_sandesh_context.xmpp_server = xmpp_server;
    Sandesh::set_module_context("XMPP", &xmpp_sandesh_context);
    IFMapSandeshContext ifmap_sandesh_context(&ifmap_server);
    Sandesh::set_module_context("IFMap", &ifmap_sandesh_context);

    // Determine if the number of connections is as expected. At the moment,
    // consider connections to collector, cassandra and rabbit servers
    //
    // 1. Collector client
    // 2. Cassandra Server
    // 3. AMQP Server
    std::vector<ConnectionTypeName> expected_connections;
    expected_connections = boost::assign::list_of
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::COLLECTOR)->second, "Collector"))
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::DATABASE)->second, "Cassandra"))
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::DATABASE)->second, "RabbitMQ"));

    ConnectionStateManager::GetInstance()->Init(
        *evm.io_service(), options.hostname(),
        module_name, g_vns_constants.INSTANCE_ID_DEFAULT,
        boost::bind(&ControlNodeGetProcessStateCb,
                    bgp_server.get(), config_client_manager, _1, _2, _3,
                    expected_connections), "ObjectBgpRouter");

    if (!options.collectors_configured()) {
        sandesh_generator_init = false;
    }

    if (sandesh_generator_init) {
        NodeType::type node_type = 
            g_vns_constants.Module2NodeType.find(module)->second;
        bool success;
        if (options.collectors_configured()) {
            success = Sandesh::InitGenerator(
                    module_name,
                    options.hostname(),
                    g_vns_constants.NodeTypeNames.find(node_type)->second,
                    g_vns_constants.INSTANCE_ID_DEFAULT,
                    &evm,
                    options.http_server_port(),
                    options.randomized_collector_server_list(),
                    &sandesh_context,
                    Sandesh::DerivedStats(),
                    options.sandesh_config());
        } else {
            success = Sandesh::InitGenerator(
                    g_vns_constants.ModuleNames.find(module)->second,
                    options.hostname(),
                    g_vns_constants.NodeTypeNames.find(node_type)->second,
                    g_vns_constants.INSTANCE_ID_DEFAULT,
                    &evm,
                    options.http_server_port(),
                    &sandesh_context,
                    Sandesh::DerivedStats(),
                    options.sandesh_config());
        }
        if (!success) {
            LOG(ERROR, "SANDESH: Initialization FAILED ... exiting");
            Sandesh::Uninit();
            exit(1);
        }
    }

    // Set BuildInfo.
    string build_info;
    MiscUtils::GetBuildInfo(MiscUtils::ControlNode, BuildInfo, build_info);
    ControlNode::StartControlNodeInfoLogger(evm, 60 * 1000,
                                            bgp_server.get(),
                                            bgp_peer_manager.get(),
                                            &ifmap_server, build_info);

    config_client_manager->Initialize();

    // Event loop.
    evm.Run();

    ShutdownServers(&bgp_peer_manager);
    BgpServer::Terminate();
    return 0;
}
