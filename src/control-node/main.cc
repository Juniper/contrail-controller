/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <iostream>

#include <boost/asio/ip/host_name.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/program_options.hpp>

#include "base/connection_info.h"
#include "base/cpuinfo.h"
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
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
#include "control-node/buildinfo.h"
#include "control-node/control_node.h"
#include "control-node/options.h"
#include "control-node/sandesh/control_node_types.h"
#include "db/db_graph.h"
#include "ifmap/client/ifmap_manager.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_sandesh_context.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/ifmap_xmpp.h"
#include "io/event_manager.h"
#include "sandesh/common/vns_constants.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_http.h"
#include "sandesh/sandesh_trace.h"
#include "sandesh/sandesh_types.h"
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

static string FileRead(const char *filename) {
    ifstream file(filename);
    string content((istreambuf_iterator<char>(file)),
                   istreambuf_iterator<char>());
    return content;
}

static void IFMap_Initialize(IFMapServer *server) {
    IFMapLinkTable_Init(server->database(), server->graph());
    IFMapServerParser *parser = IFMapServerParser::GetInstance("vnc_cfg");
    vnc_cfg_ParserInit(parser);
    vnc_cfg_Server_ModuleInit(server->database(), server->graph());
    bgp_schema_ParserInit(parser);
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
    xmpp_cfg->gr_helper_enable = options->gr_helper_xmpp_enable();

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

static void ShutdownDiscoveryClient(DiscoveryServiceClient *client) {
    if (client) {
        client->Shutdown();
        delete client;
    }
}

// Shutdown various server objects used in the control-node.
static void ShutdownServers(
    boost::scoped_ptr<BgpXmppChannelManager> *channel_manager,
    DiscoveryServiceClient *dsclient,
    Timer *node_info_log_timer) {

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
    if (node_info_log_timer) {
        TimerManager::DeleteTimer(node_info_log_timer);
    }
    // Shutdown Discovery Service Client
    ShutdownDiscoveryClient(dsclient);

    ConnectionStateManager<NodeStatusUVE, NodeStatus>::
        GetInstance()->Shutdown();

    // Do sandesh cleanup.
    Sandesh::Uninit();
    WaitForIdle();
    SandeshHttp::Uninit();
    WaitForIdle();
}

static bool ControlNodeInfoLogTimer(TaskTrigger *node_info_trigger) {
    node_info_trigger->Set();
    // Periodic timer. Restart
    return true;
}

static bool ControlNodeInfoLogger(BgpServer *server,
                                  BgpXmppChannelManager *xmpp_channel_mgr,
                                  IFMapServer *ifmap_server,
                                  Timer *node_info_log_timer) {
    // Send CPU usage Information.
    CpuLoadInfo cpu_load_info;
    CpuLoadData::FillCpuInfo(cpu_load_info, false);
    SendCpuInfoStat<ControlCpuStateTrace, ControlCpuState>(server->localname(),
                                                           cpu_load_info);

    static bool first = true;
    static BgpRouterState state;
    bool change = false;

    state.set_name(server->localname());

    // Send self information.
    uint64_t start_time = UTCTimestampUsec();
    if (first || start_time != state.get_uptime()) {
        state.set_uptime(start_time);
        change = true;
    }

    vector<string> ip_list;
    ip_list.push_back(ControlNode::GetSelfIp());
    if (first || state.get_bgp_router_ip_list() != ip_list) {
        state.set_bgp_router_ip_list(ip_list);
        change = true;
    }

    vector<string> list;
    MiscUtils::GetCoreFileList(ControlNode::GetProgramName(), list);
    if (first || state.get_core_files_list() != list) {
        state.set_core_files_list(list);
        change = true;
    }

    // Send Build information.
    string build_info;
    MiscUtils::GetBuildInfo(MiscUtils::ControlNode, BuildInfo, build_info);
    if (first || build_info != state.get_build_info()) {
        state.set_build_info(build_info);
        change = true;
    }

    change |= server->CollectStats(&state, first);
    change |= xmpp_channel_mgr->CollectStats(&state, first);
    change |= ifmap_server->CollectStats(&state, first);

    if (change) {
        BGPRouterInfo::Send(state);

        // Reset changed flags in the uve structure.
        memset(&state.__isset, 0, sizeof(state.__isset));
    }

    first = false;
    return true;
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
    const IFMapManager *ifmap_manager,
    const std::vector<ConnectionInfo> &cinfos,
    ProcessState::type &state, std::string &message,
    std::vector<ConnectionTypeName> expected_connections) {
    GetProcessStateCb(cinfos, state, message, expected_connections);
    if (state == ProcessState::NON_FUNCTIONAL)
        return;
    if (!ifmap_manager->GetEndOfRibComputed()) {
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

static bool ControlNodeReEvalPublishCb(const BgpServer *bgp_server,
    const IFMapManager *ifmap_manager, std::string &message) {
    if (!ifmap_manager->GetEndOfRibComputed()) {
        message = "IFMap Server End-Of-RIB not computed";
        return false;
    }
    if (!bgp_server->HasSelfConfiguration()) {
        message = "No BGP configuration for self";
        return false;
    }
    if (bgp_server->admin_down()) {
        message = "BGP is administratively down";
        return false;
    }
    message = "OK";
    return true;
}

int main(int argc, char *argv[]) {
    Options options;
    bool sandesh_generator_init = true;

    // Process options from command-line and configuration file.
    if (!options.Parse(evm, argc, argv)) {
        exit(-1);
    }

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

    TaskScheduler::Initialize();
    TaskScheduler::GetInstance()->SetTrackRunTime(
        options.task_track_run_time());
    ControlNode::SetDefaultSchedulingPolicy();

    /* If Sandesh initialization is not being done via discovery we need to
     * initialize here. We need to do sandesh initialization here for cases
     * (i) When both Discovery and Collectors are configured.
     * (ii) When both are not configured (to initialize introspect)
     * (iii) When only collector is configured
     */
    if (!options.discovery_server().empty() &&
        !options.collectors_configured()) {
        sandesh_generator_init = false;
    }

    BgpSandeshContext sandesh_context;
    RegisterSandeshShowIfmapHandlers(&sandesh_context);
    RegisterSandeshShowXmppExtensions(&sandesh_context);
    Sandesh::set_send_rate_limit(options.sandesh_send_rate_limit());
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
                    options.http_server_port(), 0,
                    options.collector_server_list(),
                    &sandesh_context);
        } else {
            success = Sandesh::InitGenerator(
                    g_vns_constants.ModuleNames.find(module)->second,
                    options.hostname(),
                    g_vns_constants.NodeTypeNames.find(node_type)->second,
                    g_vns_constants.INSTANCE_ID_DEFAULT,
                    &evm,
                    options.http_server_port(),
                    &sandesh_context);
        }
        if (!success) {
            LOG(ERROR, "SANDESH: Initialization FAILED ... exiting");
            Sandesh::Uninit();
            exit(1);
        }
    }

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
    bgp_server->set_gr_helper_enable(options.gr_helper_bgp_enable());

    DB config_db;
    DBGraph config_graph;
    IFMapServer ifmap_server(&config_db, &config_graph, evm.io_service());
    IFMap_Initialize(&ifmap_server);

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

    // Create IFMapManager and associate with the IFMapServer.
    IFMapServerParser *ifmap_parser = IFMapServerParser::GetInstance("vnc_cfg");
    IFMapManager *ifmap_manager = new IFMapManager(&ifmap_server,
        options.ifmap_config_options(),
        boost::bind(
            &IFMapServerParser::Receive, ifmap_parser, &config_db, _1, _2, _3),
        evm.io_service());
    ifmap_server.set_ifmap_manager(ifmap_manager);

    // Determine if the number of connections is as expected. At the moment,
    // consider connections to collector, discovery server and IFMap (irond)
    // servers as critical to the normal functionality of control-node.
    //
    // 1. Collector client
    // 2. Discovery Server publish XmppServer
    // 3. Discovery Server subscribe Collector
    // 4. Discovery Server subscribe IfmapServer
    // 5. IFMap Server (irond)
    std::vector<ConnectionTypeName> expected_connections = boost::assign::list_of
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::DISCOVERY)->second,
                             g_vns_constants.COLLECTOR_DISCOVERY_SERVICE_NAME))
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::COLLECTOR)->second, ""))
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::DISCOVERY)->second,
                             g_vns_constants.IFMAP_SERVER_DISCOVERY_SERVICE_NAME))
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::IFMAP)->second, "IFMapServer"))
         (ConnectionTypeName(g_process_info_constants.ConnectionTypeNames.find(
                             ConnectionType::DISCOVERY)->second,
                             g_vns_constants.XMPP_SERVER_DISCOVERY_SERVICE_NAME));
    ConnectionStateManager<NodeStatusUVE, NodeStatus>::GetInstance()->Init(
        *evm.io_service(), options.hostname(),
        module_name, g_vns_constants.INSTANCE_ID_DEFAULT,
        boost::bind(&ControlNodeGetProcessStateCb,
                    bgp_server.get(), ifmap_manager, _1, _2, _3,
                    expected_connections));

    // Parse discovery server configuration.
    DiscoveryServiceClient *ds_client = NULL;
    tcp::endpoint dss_ep;
    if (DiscoveryServiceClient::ParseDiscoveryServerConfig(
        options.discovery_server(), options.discovery_port(), &dss_ep)) {

        // Create and initialize discovery client.
        string subscriber_name =
            g_vns_constants.ModuleNames.find(Module::CONTROL_NODE)->second;
        ds_client = new DiscoveryServiceClient(&evm, dss_ep, subscriber_name);
        ds_client->Init();

        // Publish xmpp-server service.
        ControlNode::SetSelfIp(options.host_ip());
        if (!options.host_ip().empty()) {
            stringstream pub_ss;
            const std::string &sname(
                g_vns_constants.XMPP_SERVER_DISCOVERY_SERVICE_NAME);
            pub_ss << "<" << sname << "><ip-address>" << options.host_ip() <<
                "</ip-address><port>" << options.xmpp_port() <<
                "</port></" << sname << ">";
            string pub_msg;
            pub_msg = pub_ss.str();
            ds_client->Publish(sname, pub_msg,
                boost::bind(&ControlNodeReEvalPublishCb,
                    bgp_server.get(), ifmap_manager, _1));
        }

        // Subscribe to collector service if collector isn't explicitly configured.
        if (!options.collectors_configured()) {
            Module::type module = Module::CONTROL_NODE;
            NodeType::type node_type =
                g_vns_constants.Module2NodeType.find(module)->second;
            string subscriber_name =
                g_vns_constants.ModuleNames.find(module)->second;
            string node_type_name =
                g_vns_constants.NodeTypeNames.find(node_type)->second;
            Sandesh::CollectorSubFn csf = 0;
            csf = boost::bind(&DiscoveryServiceClient::Subscribe,
                              ds_client, _1, _2, _3);
            vector<string> list;
            bool success(Sandesh::InitGenerator(subscriber_name,
                                   options.hostname(),
                                   node_type_name,
                                   g_vns_constants.INSTANCE_ID_DEFAULT,
                                   &evm,
                                   options.http_server_port(),
                                   csf,
                                   list,
                                   &sandesh_context));
            if (!success) {
                LOG(ERROR, "SANDESH: Initialization FAILED ... exiting");
                ShutdownServers(&bgp_peer_manager, ds_client, NULL);
                exit(1);
            }
        }
    } else {
        LOG(ERROR, "Invalid Discovery Server hostname or address " <<
            options.discovery_server());
    }
    ControlNode::SetDiscoveryServiceClient(ds_client);

    // Initialize discovery mechanism for IFMapManager.
    // Must happen after call to ConnectionStateManager::Init to ensure that
    // ConnectionState is not updated before the ConnectionStateManager gets
    // initialized.
    ifmap_manager->InitializeDiscovery(ds_client, options.ifmap_server_url());

    CpuLoadData::Init();

    std::auto_ptr<Timer> node_info_log_timer(
        TimerManager::CreateTimer(
            *evm.io_service(), "ControlNode Info log timer"));

    std::auto_ptr<TaskTrigger> node_info_trigger(
        new TaskTrigger(
            boost::bind(&ControlNodeInfoLogger,
                        bgp_server.get(), bgp_peer_manager.get(),
                        &ifmap_server, node_info_log_timer.get()),
            TaskScheduler::GetInstance()->GetTaskId("bgp::Uve"), 0));

    // Start periodic timer to send BGPRouterInfo UVE.
    node_info_log_timer->Start(
        60 * 1000,
        boost::bind(&ControlNodeInfoLogTimer, node_info_trigger.get()),
        NULL);

    // Event loop.
    evm.Run();

    ShutdownServers(&bgp_peer_manager, ds_client, node_info_log_timer.get());
    return 0;
}
