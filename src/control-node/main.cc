/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <iostream>

#include <boost/asio/ip/host_name.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/program_options.hpp>
#include <pthread.h>

#include "base/logging.h"
#include "base/cpuinfo.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/routing-instance/routing_instance.h"
#include "control-node/control_node.h"
#include "db/db_graph.h"
#include "ifmap/ifmap_link_table.h"
#include "ifmap/ifmap_server_parser.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_xmpp.h"
#include "ifmap/client/ifmap_manager.h"
#include "io/event_manager.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_http.h"
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include "schema/vnc_cfg_types.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/sandesh/xmpp_peer_info_types.h"
#include "bgp/bgp_sandesh.h"
#include "base/contrail_ports.h"
#include "base/misc_utils.h"
#include "control-node/sandesh/control_node_types.h"
#include <control-node/buildinfo.h>

using namespace std;
using namespace boost::asio::ip;
namespace opt = boost::program_options;

static TaskTrigger *node_info_trigger;
static Timer *node_info_log_timer;
static uint64_t start_time;
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
                DiscoveryServiceClient *dsclient) {

    // Bring down bgp server, xmpp server, etc. in the right order.
    BgpServer *bgp_server = (*channel_manager)->bgp_server();
    XmppServer *xmpp_server = (*channel_manager)->xmpp_server();

    int cnt;

    // Shutdown Xmpp server first.
    xmpp_server->Shutdown();
    WaitForIdle();

    // Wait until all XMPP connections are cleaned up.
    for (cnt = 0; xmpp_server->ConnectionsCount() != 0 && cnt < 15; cnt++) {
        sleep(1);
    }

    // Shutdown BGP server.
    bgp_server->Shutdown();
    WaitForIdle();

    // Wait until all routing-instances are cleaned up.
    for (cnt = 0; bgp_server->routing_instance_mgr()->count() != 0 && cnt < 15;
            cnt++) {
        sleep(1);
    }

    channel_manager->reset();
    TcpServerManager::DeleteServer(xmpp_server);
    TimerManager::DeleteTimer(node_info_log_timer);
    delete node_info_trigger;

    //Shutdown Discovery Service Client
    ShutdownDiscoveryClient(dsclient);

    // Do sandesh cleanup.
    Sandesh::Uninit();
    WaitForIdle();
    SandeshHttp::Uninit();
    WaitForIdle();
}

bool ControlNodeInfoLogTimer() {
    node_info_trigger->Set();
    return false;
}

bool ControlNodeVersion(string &build_info_str) {
    return MiscUtils::GetBuildInfo(MiscUtils::ControlNode, BuildInfo, build_info_str);
}

static void FillProtoStats(IPeerDebugStats::ProtoStats &stats,
                           PeerProtoStats &proto_stats) {
    proto_stats.open = stats.open;
    proto_stats.keepalive = stats.keepalive;
    proto_stats.close = stats.close;
    proto_stats.update = stats.update;
    proto_stats.notification = stats.notification;
    proto_stats.total = stats.open + stats.keepalive + stats.close +
        stats.update + stats.notification;
}

static void FillRouteUpdateStats(IPeerDebugStats::UpdateStats &stats,
                                 PeerUpdateStats &rt_stats) {
    rt_stats.total = stats.total;
    rt_stats.reach = stats.reach;
    rt_stats.unreach = stats.unreach;
}

static void FillPeerDebugStats(IPeerDebugStats *peer_state,
                          PeerStatsInfo &stats) {
    PeerProtoStats proto_stats_tx;
    PeerProtoStats proto_stats_rx;
    PeerUpdateStats rt_stats_rx;
    PeerUpdateStats rt_stats_tx;

    IPeerDebugStats::ProtoStats stats_rx;
    peer_state->GetRxProtoStats(stats_rx);
    FillProtoStats(stats_rx, proto_stats_rx);

    IPeerDebugStats::ProtoStats stats_tx;
    peer_state->GetTxProtoStats(stats_tx);
    FillProtoStats(stats_tx, proto_stats_tx);

    IPeerDebugStats::UpdateStats update_stats_rx;
    peer_state->GetRxRouteUpdateStats(update_stats_rx);
    FillRouteUpdateStats(update_stats_rx, rt_stats_rx);

    IPeerDebugStats::UpdateStats update_stats_tx;
    peer_state->GetTxRouteUpdateStats(update_stats_tx);
    FillRouteUpdateStats(update_stats_tx, rt_stats_tx);

    stats.set_rx_proto_stats(proto_stats_rx);
    stats.set_tx_proto_stats(proto_stats_tx);
    stats.set_rx_update_stats(rt_stats_rx);
    stats.set_tx_update_stats(rt_stats_tx);
}

void FillXmppPeerStats(BgpServer *server, BgpXmppChannel *channel) {
    PeerStatsInfo stats;
    FillPeerDebugStats(channel->Peer()->peer_stats(), stats);

    XmppPeerInfoData peer_info;
    peer_info.set_name(channel->Peer()->ToUVEKey());
    peer_info.set_peer_stats_info(stats);
    XMPPPeerInfo::Send(peer_info);
}

void FillBgpPeerStats(BgpServer *server, BgpPeer *peer) {
    PeerStatsInfo stats;
    FillPeerDebugStats(peer->peer_stats(), stats);

    BgpPeerInfoData peer_info;
    peer_info.set_name(peer->ToUVEKey());
    peer_info.set_peer_stats_info(stats);
    BGPPeerInfo::Send(peer_info);
}

void LogControlNodePeerStats(BgpServer *server,
                             BgpXmppChannelManager *xmpp_channel_mgr) {
    xmpp_channel_mgr->VisitChannels(boost::bind(FillXmppPeerStats,
                                                server, _1));
    server->VisitBgpPeers(boost::bind(FillBgpPeerStats, server, _1));
}

bool ControlNodeInfoLogger(BgpSandeshContext &ctx) {
    BgpServer *server = ctx.bgp_server;
    BgpXmppChannelManager *xmpp_channel_mgr = ctx.xmpp_peer_manager;

    LogControlNodePeerStats(server, xmpp_channel_mgr);

    BgpRouterState state;
    static BgpRouterState prev_state;
    static bool first = true, build_info_set = false;
    bool change = false;

    CpuLoadInfo cpu_load_info;
    CpuLoadData::FillCpuInfo(cpu_load_info, false);
    state.set_name(server->localname());
    if (first) {
        state.set_uptime(start_time);
        vector<string> ip_list;
        ip_list.push_back(ControlNode::GetSelfIp());
        state.set_bgp_router_ip_list(ip_list);
        vector<string> list;
        MiscUtils::GetCoreFileList(ControlNode::GetProgramName(), list);
        if (list.size()) {
            state.set_core_files_list(list);
        }
    }
    if (!build_info_set) {
        string build_info;
        build_info_set = ControlNodeVersion(build_info);
        if (build_info != prev_state.get_build_info()) {
            state.set_build_info(build_info);
            prev_state.set_build_info(build_info);
            change = true;
        }
    }

    if (cpu_load_info != prev_state.get_cpu_info() || first) {
        state.set_cpu_info(cpu_load_info);
        if (cpu_load_info.get_cpu_share() != 
                prev_state.get_cpu_info().get_cpu_share()) {
            state.set_cpu_share(cpu_load_info.get_cpu_share());
        }
        if (prev_state.get_cpu_info().get_meminfo() != 
                cpu_load_info.get_meminfo()) {
            state.set_virt_mem(cpu_load_info.get_meminfo().get_virt());
        }
        prev_state.set_cpu_info(cpu_load_info);
        change = true;
    }

    ControlCpuState  astate;
    astate.set_name(server->localname());

    ProcessCpuInfo ainfo;
    ainfo.set_module_id(Sandesh::module());
    ainfo.set_inst_id(Sandesh::instance_id());
    ainfo.set_cpu_share(cpu_load_info.get_cpu_share());
    ainfo.set_mem_virt(cpu_load_info.get_meminfo().get_virt());
    vector<ProcessCpuInfo> aciv;
    aciv.push_back(ainfo);
    astate.set_cpu_info(aciv);
    ControlCpuStateTrace::Send(astate);

    uint32_t num_xmpp = xmpp_channel_mgr->count();
    if (num_xmpp != prev_state.get_num_xmpp_peer() || first) {
        state.set_num_xmpp_peer(num_xmpp);
        prev_state.set_num_xmpp_peer(num_xmpp);
        change = true;
    }

    uint32_t num_up_xmpp = xmpp_channel_mgr->NumUpPeer();
    if (num_up_xmpp != prev_state.get_num_up_xmpp_peer() || first) {
        state.set_num_up_xmpp_peer(num_up_xmpp);
        prev_state.set_num_up_xmpp_peer(num_up_xmpp);
        change = true;
    }

    uint32_t num_bgp = server->num_bgp_peer();
    if (num_bgp != prev_state.get_num_bgp_peer() || first) {
        state.set_num_bgp_peer(num_bgp);
        prev_state.set_num_bgp_peer(num_bgp);
        change = true;
    }

    uint32_t num_up_bgp_peer = server->NumUpPeer();
    if (num_up_bgp_peer != prev_state.get_num_up_bgp_peer() || first) {
        state.set_num_up_bgp_peer(num_up_bgp_peer);
        prev_state.set_num_up_bgp_peer(num_up_bgp_peer);
        change = true;
    }

    uint32_t num_ri = server->num_routing_instance();
    if (num_ri != prev_state.get_num_routing_instance() || first) {
        state.set_num_routing_instance(num_ri);
        prev_state.set_num_routing_instance(num_ri);
        change = true;
    }

    IFMapPeerServerInfoUI peer_server_info;
    ctx.ifmap_server->get_ifmap_manager()->GetPeerServerInfo(peer_server_info);
    if (peer_server_info != prev_state.get_ifmap_info() || first) {
        state.set_ifmap_info(peer_server_info);
        prev_state.set_ifmap_info(peer_server_info);
        change = true;
    }

    IFMapServerInfoUI server_info;
    ctx.ifmap_server->GetUIInfo(&server_info);
    if (server_info != prev_state.get_ifmap_server_info() || first) {
        state.set_ifmap_server_info(server_info);
        prev_state.set_ifmap_server_info(server_info);
        change = true;
    }

    uint32_t out_load = server->get_output_queue_depth();
    if (out_load != prev_state.get_output_queue_depth() || first) {
        state.set_output_queue_depth(out_load);
        prev_state.set_output_queue_depth(out_load);
        change = true;
    }

    if (change)
        BGPRouterInfo::Send(state);

    if (first) first = false;

    node_info_log_timer->Cancel();
    node_info_log_timer->Start(60*1000, boost::bind(&ControlNodeInfoLogTimer),
                               NULL);
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

static int control_node_main(int argc, char *argv[]) {
    boost::system::error_code error;

    // Specify defaults for all options possible.
    uint16_t bgp_port = ContrailPorts::ControlBgp;
    string collector_server;
    uint16_t collector_port = ContrailPorts::CollectorPort;
    string bgp_config_file = "bgp_config.xml";
    string discovery_server;
    uint16_t discovery_port = ContrailPorts::DiscoveryServerPort;
    string hostname(host_name(error));
    string host_ip = GetHostIp(evm.io_service(), hostname);
    uint16_t http_server_port = ContrailPorts::HttpPortControl;
    string log_category = "";
    bool disable_logging = false;
    string log_file = "<stdout>";
    int log_file_index = 10;
    long log_file_size = 10*1024*1024; // 10MB
    string log_level = "SYS_NOTICE";
    bool enable_local_logging = false;
    string ifmap_server_url;
    string ifmap_password = "control-user-passwd";
    string ifmap_user = "control-user";
    string certs_store = "";
    uint16_t xmpp_port = ContrailPorts::ControlXmpp;
    string config_file = "/etc/contrail/control-node.conf";
    bool test_mode = false;

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
        ("BGP.config-file",
             opt::value<string>()->default_value(bgp_config_file),
             "BGP Configuration file")
        ("BGP.port", opt::value<uint16_t>()->default_value(bgp_port),
             "BGP listener port")

        ("COLLECTOR.port",
             opt::value<uint16_t>()->default_value(collector_port),
             "Port of sandesh collector")
        ("COLLECTOR.server",
             opt::value<string>()->default_value(collector_server),
             "IP address of sandesh collector")

        ("DEFAULTS.hostip", opt::value<string>(), "IP address of control-node")
        ("DEFAULTS.hostname", opt::value<string>()->default_value(hostname),
             "Hostname of control-node")
        ("DEFAULTS.http-server-port",
             opt::value<uint16_t>()->default_value(http_server_port),
             "Sandesh HTTP listener port")
        ("DEFAULTS.test-mode", opt::bool_switch(&test_mode),
             "Enable running of daemon in test-mode")
        ("DEFAULTS.xmpp-server-port",
            opt::value<uint16_t>()->default_value(xmpp_port), "XMPP listener port")

        ("DISCOVERY.port", opt::value<uint16_t>()->default_value(discovery_port),
            "Port of Discovery Server")
        ("DISCOVERY.server",
             opt::value<string>()->default_value(discovery_server),
             "IP address of Discovery Server")

        ("IFMAP.password", opt::value<string>()->default_value(ifmap_password),
             "IFMAP server password")
        ("IFMAP.server-url",
             opt::value<string>()->default_value(ifmap_server_url),
             "IFMAP server URL")
        ("IFMAP.user", opt::value<string>()->default_value(ifmap_user),

             "IFMAP server username")
        ("IFMAP.certs-store", opt::value<string>()->default_value(certs_store),
             "Certificates store to use for communication with IFMAP server")

        ("LOG.category", opt::value<string>()->default_value(log_category),
             "Category filter for local logging of sandesh messages")
        ("LOG.disable", opt::bool_switch(&disable_logging),
             "Disable sandesh logging")
        ("LOG.file", opt::value<string>()->default_value(log_file),
             "Filename for the logs to be written to")
        ("LOG.file-index",
             opt::value<int>()->default_value(log_file_index),
             "Maximum log file roll over index")
        ("LOG.file-size",
             opt::value<long>()->default_value(log_file_size),
             "Maximum size of the log file")
        ("LOG.level", opt::value<string>()->default_value(log_level),
             "Severity level for local logging of sandesh messages")
        ("LOG.local", opt::bool_switch(&enable_local_logging),
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
        cout << cmdline_options << endl;
        exit(0);
    }

    if (var_map.count("version")) {
        string build_info;
        ControlNodeVersion(build_info);
        cout << build_info << endl;
        exit(0);
    }

    GetOptValue<string>(var_map, bgp_config_file, "BGP.config-file", "");
    GetOptValue<uint16_t>(var_map, bgp_port, "BGP.port", 0);
    GetOptValue<uint16_t>(var_map, collector_port, "COLLECTOR.port", 0);
    GetOptValue<string>(var_map, collector_server, "COLLECTOR.server", "");

    GetOptValue<string>(var_map, host_ip, "DEFAULTS.hostip", "");
    GetOptValue<string>(var_map, hostname, "DEFAULTS.hostname", "");

    GetOptValue<uint16_t>(var_map, http_server_port, "DEFAULTS.http-server-port", 0);
    GetOptValue<uint16_t>(var_map, xmpp_port, "DEFAULTS.xmpp-server-port", 0);

    GetOptValue<uint16_t>(var_map, discovery_port, "DISCOVERY.port", 0);
    GetOptValue<string>(var_map, discovery_server, "DISCOVERY.server", "");


    GetOptValue<string>(var_map, log_category, "LOG.category", "");
    GetOptValue<string>(var_map, log_file, "LOG.file", "");
    GetOptValue<int>(var_map, log_file_index, "LOG.file-index", 0);
    GetOptValue<long>(var_map, log_file_size, "LOG.file-size", 0);
    GetOptValue<string>(var_map, log_level, "LOG.level", "");

    GetOptValue<string>(var_map, ifmap_password, "IFMAP.password", "");
    GetOptValue<string>(var_map, ifmap_server_url, "IFMAP.server-url", "");
    GetOptValue<string>(var_map, ifmap_user, "IFMAP.user", "");
    GetOptValue<string>(var_map, certs_store, "IFMAP.certs-store", "");

    ControlNode::SetProgramName(argv[0]);
    if (log_file == "<stdout>") {
        LoggingInit();
    } else {
        LoggingInit(log_file, log_file_size, log_file_index);
    }
    TaskScheduler::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpSandeshContext sandesh_context;

    if (discovery_server.empty()) {
        Module::type module = Module::CONTROL_NODE;
        NodeType::type node_type = 
            g_vns_constants.Module2NodeType.find(module)->second; 
        Sandesh::InitGenerator(
            g_vns_constants.ModuleNames.find(module)->second,
            hostname, 
            g_vns_constants.NodeTypeNames.find(node_type)->second,
            g_vns_constants.INSTANCE_ID_DEFAULT, 
            &evm,
            http_server_port,
            &sandesh_context);
    }

    if (!collector_server.empty()) {
        Sandesh::ConnectToCollector(collector_server, collector_port);
    }
    Sandesh::SetLoggingParams(enable_local_logging, log_category, log_level);

    // XXX Disable logging -- for test purposes only
    if (disable_logging) {
        SetLoggingDisabled(true);
    }

    ControlNode::SetTestMode(test_mode);

    boost::scoped_ptr<BgpServer> bgp_server(new BgpServer(&evm));
    sandesh_context.bgp_server = bgp_server.get();

    DB config_db;
    DBGraph config_graph;
    IFMapServer ifmap_server(&config_db, &config_graph, evm.io_service());
    sandesh_context.ifmap_server = &ifmap_server;
    IFMap_Initialize(&ifmap_server);

    bgp_server->config_manager()->Initialize(&config_db, &config_graph, hostname);
    ControlNode::SetHostname(hostname);
    BgpConfigParser parser(&config_db);
    parser.Parse(FileRead(bgp_config_file.c_str()));

    // TODO:  Initialize throws an exception (via boost) in case the
    // user does not have permissions to bind to the port.

    LOG(DEBUG, "Starting Bgp Server at port " << bgp_port);
    bgp_server->session_manager()->Initialize(bgp_port);

    XmppServer *xmpp_server = new XmppServer(&evm, hostname);
    XmppInit init;
    XmppChannelConfig xmpp_cfg(false);
    xmpp_cfg.endpoint.port(xmpp_port);
    xmpp_cfg.FromAddr = XmppInit::kControlNodeJID;
    init.AddXmppChannelConfig(&xmpp_cfg);
    init.InitServer(xmpp_server, xmpp_port, true);

    // Register XMPP channel peers 
    boost::scoped_ptr<BgpXmppChannelManager> bgp_peer_manager(
                    new BgpXmppChannelManager(xmpp_server, bgp_server.get()));
    sandesh_context.xmpp_peer_manager = bgp_peer_manager.get();
    IFMapChannelManager ifmap_channel_mgr(xmpp_server, &ifmap_server);
    ifmap_server.set_ifmap_channel_manager(&ifmap_channel_mgr);

    //Register services with Discovery Service Server
    DiscoveryServiceClient *ds_client = NULL; 
    if (!discovery_server.empty()) {
        tcp::endpoint dss_ep;
        dss_ep.address(address::from_string(discovery_server, error));
        dss_ep.port(discovery_port);
        string subscriber_name = 
            g_vns_constants.ModuleNames.find(Module::CONTROL_NODE)->second;
        ds_client = new DiscoveryServiceClient(&evm, dss_ep, subscriber_name); 
        ds_client->Init();
        ControlNode::SetDiscoveryServiceClient(ds_client); 
  
        // publish xmpp-server service
        ControlNode::SetSelfIp(host_ip);
        if (!host_ip.empty()) {
            stringstream pub_ss;
            pub_ss << "<xmpp-server><ip-address>" << host_ip <<
                      "</ip-address><port>" << xmpp_port <<
                      "</port></xmpp-server>";
            string pub_msg;
            pub_msg = pub_ss.str();
            ds_client->Publish(DiscoveryServiceClient::XmppService, pub_msg);
        }

        // subscribe to collector service if not configured
        if (collector_server.empty()) {
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
            list.clear();
            Sandesh::InitGenerator(subscriber_name,
                                   hostname,
                                   node_type_name,
                                   g_vns_constants.INSTANCE_ID_DEFAULT, 
                                   &evm,
                                   http_server_port,
                                   csf,
                                   list,
                                   &sandesh_context);
        }
    }

    IFMapServerParser *ifmap_parser = IFMapServerParser::GetInstance("vnc_cfg");

    IFMapManager *ifmapmgr = new IFMapManager(&ifmap_server,
                ifmap_server_url, ifmap_user, ifmap_password, certs_store,
                boost::bind(&IFMapServerParser::Receive, ifmap_parser,
                            &config_db, _1, _2, _3), evm.io_service(),
                ds_client);
    ifmap_server.set_ifmap_manager(ifmapmgr);

    CpuLoadData::Init();
    start_time = UTCTimestampUsec();
    node_info_trigger = 
        new TaskTrigger(boost::bind(&ControlNodeInfoLogger, sandesh_context),
            TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0);

    node_info_log_timer = TimerManager::CreateTimer(*evm.io_service(), 
        "ControlNode Info log timer");
    node_info_log_timer->Start(60*1000, boost::bind(&ControlNodeInfoLogTimer),
                               NULL);
    evm.Run();
    ShutdownServers(&bgp_peer_manager, ds_client);

    init.Reset();
    return 0;
}

int main(int argc, char *argv[]) {
    try {
        return control_node_main(argc, argv);
    } catch (boost::program_options::error &e) {
        LOG(ERROR, "Error " << e.what());
        cout << "Error " << e.what() << endl;
    } catch (...) {
        LOG(ERROR, "Options Parser: Caught fatal unknown exception");
        cout << "Options Parser: Caught fatal unknown exception" << endl;
    }

    return(-1);
}
