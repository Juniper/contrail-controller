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
#include "discovery_client.h"

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
// IO (evm) is shutdown first. Afterwards, main() resumes, shutting down rest of the
// objects, and eventually exit()s.
void ControlNodeShutdown() {
    static bool shutdown_;

    if (shutdown_) return;
    shutdown_ = true;

    // Shutdown event manager first to stop all IO activities.
    evm.Shutdown();
}

// Read command line options from a file for ease of use. These options read
// from file are appended to those specified in the command line.
int ReadCommandLineOptionsFromFile(int argc, char **argv,
                                   std::vector<string> &tokens, string &line,
                                   char **p_argv, size_t p_argv_size) {
    int p_argc = argc;

    // Copy command line options
    for (int i = 0; i < argc; i++) {
        p_argv[i] = argv[i];
    }

    for (int j = 0; j < argc; j++) {

        // Check for --options-file option
        if (!boost::iequals("--options-file", argv[j])) continue;

        // Make sure there is a file name specified.
        if ((j + 1) >= argc) break;

        // Read text from the file and split into different tokens.
        line = FileRead(argv[j + 1]);
        boost::split(tokens, line, boost::is_any_of(" \n\t"));
        for (size_t k = 0; k < tokens.size(); k++) {

            // Ignore empty strings.
            if (tokens[k].empty()) continue;

            // Make sure that there is enough room in the output vector.
            assert((size_t) p_argc < p_argv_size);

            // Retrieve char * token into output vector.
            p_argv[p_argc++] = const_cast<char *>(tokens[k].c_str());
        }
        break;
    }

    // Return the updated number of command line options to use.
    return p_argc;
}

int main(int argc, char *argv[]) {
    bool enable_local_logging = false;
    bool disable_logging = false;
    boost::system::error_code error;
    string hostname(host_name(error));
    const string default_log_file = "<stdout>";
    const unsigned long default_log_file_size = 10*1024*1024; // 10MB
    const unsigned int default_log_file_index = 10;
    opt::options_description desc("Command line options");
    desc.add_options()
        ("bgp-port",
         opt::value<int>()->default_value(BgpConfigManager::kDefaultPort),
         "BGP listener port")
        ("collector", opt::value<string>(),
            "IP address of sandesh collector")
        ("collector-port", opt::value<int>(),
            "Port of sandesh collector")
        ("config-file", opt::value<string>()->default_value("bgp_config.xml"),
            "Configuration file")
        ("discovery-server", opt::value<string>(),
            "IP address of Discovery Server")
        ("discovery-port", 
            opt::value<int>()->default_value(ContrailPorts::DiscoveryServerPort),
            "Port of Discovery Server")
        ("help", "help message")
        ("hostname", opt::value<string>()->default_value(hostname),
            "Hostname of control-node")
        ("host-ip", opt::value<string>(), "IP address of control-node")
        ("http-server-port",
            opt::value<int>()->default_value(ContrailPorts::HttpPortControl),
            "Sandesh HTTP listener port")
        ("log-category", opt::value<string>()->default_value(""),
            "Category filter for local logging of sandesh messages")
        ("log-disable", opt::bool_switch(&disable_logging),
             "Disable sandesh logging")
        ("log-file", opt::value<string>()->default_value(default_log_file),
             "Filename for the logs to be written to")
        ("log-file-index",
             opt::value<unsigned int>()->default_value(default_log_file_index),
             "Maximum log file roll over index")
        ("log-file-size",
             opt::value<unsigned long>()->default_value(default_log_file_size),
             "Maximum size of the log file")
        ("log-level", opt::value<string>()->default_value("SYS_NOTICE"),
            "Severity level for local logging of sandesh messages")
        ("log-local", opt::bool_switch(&enable_local_logging),
             "Enable local logging of sandesh messages")
        ("map-password", opt::value<string>(), "MAP server password")
        ("map-server-url", opt::value<string>(), "MAP server URL")
        ("map-user", opt::value<string>(), "MAP server username")
        ("options-file", opt::value<string>(), "Command line options file")
        ("use-certs", opt::value<string>(),
            "Certificates store to use for communication with MAP server")
        ("xmpp-port",
            opt::value<int>()->default_value(ContrailPorts::ControlXmpp),
            "XMPP listener port")
        ("version", "Display version information")
        ("use-certs", opt::value<string>(),
            "Use certificates to communicate with MAP server; Specify certificate store")
        ;

    std::vector<string> tokens;
    string line;
    char *p_argv[argc + 128];
    int p_argc;

    // Optionally, retrieve command line options from a file.
    p_argc = ReadCommandLineOptionsFromFile(argc, argv, tokens, line, p_argv,
                                            sizeof(p_argv)/sizeof(p_argv[0]));

    // Process collective options those specified in the command line and those
    // in the file (optionl).
    opt::variables_map var_map;
    opt::store(opt::parse_command_line(p_argc, p_argv, desc), var_map);
    opt::notify(var_map);

    if (var_map.count("help")) {
        cout << desc << endl;
        exit(0);
    }

    if (var_map.count("version")) {
        string build_info;
        ControlNodeVersion(build_info);
        cout << build_info << endl;
        exit(0);
    }

    ControlNode::SetProgramName(argv[0]);
    unsigned long log_file_size = default_log_file_size;
    unsigned long log_file_index = default_log_file_index;
    if (var_map.count("log-file-size")) {
        log_file_size = var_map["log-file-size"].as<unsigned long>();
    }
    if (var_map.count("log-file-index")) {
        log_file_index = var_map["log-file-index"].as<unsigned int>();
    }
    if (var_map["log-file"].as<string>() == default_log_file) {
        LoggingInit();
    } else {
        LoggingInit(var_map["log-file"].as<string>(), log_file_size,
                    log_file_index);
    }
    TaskScheduler::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpSandeshContext sandesh_context;

    if (!var_map.count("discovery-server")) {
        Module::type module = Module::CONTROL_NODE;
        NodeType::type node_type = 
            g_vns_constants.Module2NodeType.find(module)->second; 
        Sandesh::InitGenerator(
            g_vns_constants.ModuleNames.find(module)->second,
            hostname, 
            g_vns_constants.NodeTypeNames.find(node_type)->second,
            g_vns_constants.INSTANCE_ID_DEFAULT, 
            &evm,
            var_map["http-server-port"].as<int>(), &sandesh_context);
    }

    if (var_map.count("collector-port") && var_map.count("collector")) {
        int collector_port = var_map["collector-port"].as<int>();
        std::string collector_server = var_map["collector"].as<string>();
        Sandesh::ConnectToCollector(collector_server, collector_port);
    }
    Sandesh::SetLoggingParams(enable_local_logging,
                              var_map["log-category"].as<string>(),
                              var_map["log-level"].as<string>());

    //
    // XXX Disable logging -- for test purposes only
    //
    if (disable_logging) {
        SetLoggingDisabled(true);
    }

    boost::scoped_ptr<BgpServer> bgp_server(new BgpServer(&evm));
    sandesh_context.bgp_server = bgp_server.get();

    DB config_db;
    DBGraph config_graph;
    IFMapServer ifmap_server(&config_db, &config_graph, evm.io_service());
    sandesh_context.ifmap_server = &ifmap_server;
    IFMap_Initialize(&ifmap_server);

    bgp_server->config_manager()->Initialize(&config_db, &config_graph,
                                        var_map["hostname"].as<string>());
    ControlNode::SetHostname(var_map["hostname"].as<string>());
    BgpConfigParser parser(&config_db);
    parser.Parse(FileRead(var_map["config-file"].as<string>().c_str()));

    // TODO:  Initialize throws an exception (via boost) in case the
    // user does not have permissions to bind to the port.

    int bgp_port = var_map["bgp-port"].as<int>();

    LOG(DEBUG, "Starting Bgp Server at port " << bgp_port);
    bgp_server->session_manager()->Initialize(bgp_port);

    XmppServer *xmpp_server = new XmppServer(&evm, hostname);
    XmppInit init;
    XmppChannelConfig xmpp_cfg(false);
    xmpp_cfg.endpoint.port(var_map["xmpp-port"].as<int>());
    xmpp_cfg.FromAddr = XmppInit::kControlNodeJID;
    init.AddXmppChannelConfig(&xmpp_cfg);
    init.InitServer(xmpp_server, var_map["xmpp-port"].as<int>(), true);

    // Register XMPP channel peers 
    boost::scoped_ptr<BgpXmppChannelManager> bgp_peer_manager(
                    new BgpXmppChannelManager(xmpp_server, bgp_server.get()));
    sandesh_context.xmpp_peer_manager = bgp_peer_manager.get();
    IFMapChannelManager ifmap_channel_mgr(xmpp_server, &ifmap_server);
    ifmap_server.set_ifmap_channel_manager(&ifmap_channel_mgr);

    //Register services with Discovery Service Server
    DiscoveryServiceClient *ds_client = NULL; 
    if (var_map.count("discovery-server")) { 
        tcp::endpoint dss_ep;
        dss_ep.address(
            address::from_string(var_map["discovery-server"].as<string>(), 
            error));
        dss_ep.port(var_map["discovery-port"].as<int>()); 
        string subscriber_name = 
            g_vns_constants.ModuleNames.find(Module::CONTROL_NODE)->second;
        ds_client = new DiscoveryServiceClient(&evm, dss_ep, subscriber_name); 
        ds_client->Init();
  
        // publish xmpp-server service
        std::string self_ip;
        if (var_map.count("host-ip")) {
            self_ip = var_map["host-ip"].as<string>(); 
        } else {
            tcp::resolver resolver(*evm.io_service());
            tcp::resolver::query query(boost::asio::ip::host_name(), "");
            tcp::resolver::iterator iter = resolver.resolve(query);
            self_ip = iter->endpoint().address().to_string();
        }

        // verify string is a valid ip before publishing
        boost::asio::ip::address::from_string(self_ip, error);
        if (error) {
            self_ip.clear();
        } else { 
            ControlNode::SetSelfIp(self_ip);
        }

        if (!self_ip.empty()) {
            stringstream pub_ss;
            pub_ss << "<xmpp-server><ip-address>" << self_ip <<
                      "</ip-address><port>" << var_map["xmpp-port"].as<int>() <<
                      "</port></xmpp-server>";
            std::string pub_msg;
            pub_msg = pub_ss.str();
            ds_client->Publish(DiscoveryServiceClient::XmppService, pub_msg);
        }

        //subscribe to collector service if not configured
        if (!var_map.count("collector")) {
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
                                   var_map["http-server-port"].as<int>(),
                                   csf,
                                   list,
                                   &sandesh_context);
        }
    }

    std::string certstore = var_map.count("use-certs") ? 
                            var_map["use-certs"].as<string>() : string("");
    std::string map_server_url;
    if (var_map.count("map-server-url")) {
        map_server_url = var_map["map-server-url"].as<string>();
    }
    IFMapServerParser *ifmap_parser = IFMapServerParser::GetInstance("vnc_cfg");

    IFMapManager *ifmapmgr = new IFMapManager(&ifmap_server, map_server_url,
                var_map["map-user"].as<string>(),
                var_map["map-password"].as<string>(), certstore,
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
