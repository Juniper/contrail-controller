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
#include "base/connection_info.h"
#include "base/cpuinfo.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/routing-instance/rtarget_group_mgr.h"
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
#include "base/misc_utils.h"
#include "control-node/options.h"
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
    for (cnt = 0; xmpp_server->ConnectionCount() != 0 && cnt < 15; cnt++) {
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

    // Shutdown Discovery Service Client
    ShutdownDiscoveryClient(dsclient);

    ConnectionStateManager<ControlNodeStatus, ControlNodeProcessStatus>::
        GetInstance()->Shutdown();

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
    return MiscUtils::GetBuildInfo(MiscUtils::ControlNode, BuildInfo,
                                   build_info_str);
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

// Get control-node's connectivity status with other servers which are critical
// to the normal operation. conenction_info library periodically sends this
// information as UVEs to the collector for user visibility and assistance
// during trouble-shooting.
static void ControlNodeGetConnectivityStatus(
    const std::vector<ConnectionInfo> &cinfos,
    ConnectivityStatus::type &cstatus, std::string &message) {

    // Determine if the number of connections is as expected. At the moment, we
    // consider connections to collector, discovery server and IFMap (irond)
    // servers as critical to the normal functionality of control-node.
    //
    // 1. Collector client
    // 2. Discovery Server publish XmppServer
    // 3. Discovery Server subscribe Collector
    // 4. Discovery Server subscribe IfmapServer
    // 5. IFMap Server (irond)
    size_t expected_connections = 5;
    size_t num_connections(cinfos.size());
    if (num_connections != expected_connections) {
        cstatus = ConnectivityStatus::NON_FUNCTIONAL;
        message = "Number of connections:" + integerToString(num_connections) +
                  ", Expected: " + integerToString(expected_connections);
        return;
    }
    std::string cup(g_connection_info_constants.ConnectionStatusNames.
                    find(ConnectionStatus::UP)->second);

    // Iterate to determine process connectivity status
    for (std::vector<ConnectionInfo>::const_iterator it = cinfos.begin();
         it != cinfos.end(); it++) {
        const ConnectionInfo &cinfo(*it);
        const std::string &conn_status(cinfo.get_status());
        if (conn_status != cup) {
            cstatus = ConnectivityStatus::NON_FUNCTIONAL;
            message = cinfo.get_type() + ":" + cinfo.get_name();
            return;
        }
    }

    // All critical connections are in good condition.
    cstatus = ConnectivityStatus::FUNCTIONAL;
    return;
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
    LoggingInit(options.log_file(), options.log_file_size(),
                options.log_files_count(), options.use_syslog(),
                options.syslog_facility(), module_name);

    TaskScheduler::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpSandeshContext sandesh_context;

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

    if (sandesh_generator_init) {
        NodeType::type node_type = 
            g_vns_constants.Module2NodeType.find(module)->second;
        if (options.collectors_configured()) {
            Sandesh::InitGenerator(
                    module_name,
                    options.hostname(),
                    g_vns_constants.NodeTypeNames.find(node_type)->second,
                    g_vns_constants.INSTANCE_ID_DEFAULT,
                    &evm,
                    options.http_server_port(), 0,
                    options.collector_server_list(),
                    &sandesh_context);
        } else {
            Sandesh::InitGenerator(
                    g_vns_constants.ModuleNames.find(module)->second,
                    options.hostname(),
                    g_vns_constants.NodeTypeNames.find(node_type)->second,
                    g_vns_constants.INSTANCE_ID_DEFAULT,
                    &evm,
                    options.http_server_port(),
                    &sandesh_context);
        }
    }

    Sandesh::SetLoggingParams(options.log_local(), options.log_category(),
                              options.log_level());

    // XXX Disable logging -- for test purposes only
    if (options.log_disable()) {
        SetLoggingDisabled(true);
    }

    ControlNode::SetTestMode(options.test_mode());

    boost::scoped_ptr<BgpServer> bgp_server(new BgpServer(&evm));
    sandesh_context.bgp_server = bgp_server.get();

    DB config_db;
    DBGraph config_graph;
    IFMapServer ifmap_server(&config_db, &config_graph, evm.io_service());
    sandesh_context.ifmap_server = &ifmap_server;
    IFMap_Initialize(&ifmap_server);

    bgp_server->config_manager()->Initialize(&config_db,
            &config_graph, options.hostname());
    ControlNode::SetHostname(options.hostname());
    BgpConfigParser parser(&config_db);
    parser.Parse(FileRead(options.bgp_config_file().c_str()));

    bgp_server->rtarget_group_mgr()->Initialize();
    // TODO:  Initialize throws an exception (via boost) in case the
    // user does not have permissions to bind to the port.

    LOG(DEBUG, "Starting Bgp Server at port " << options.bgp_port());
    bgp_server->session_manager()->Initialize(options.bgp_port());

    XmppServer *xmpp_server = new XmppServer(&evm, options.hostname());
    XmppInit init;
    XmppChannelConfig xmpp_cfg(false);
    xmpp_cfg.endpoint.port(options.xmpp_port());
    xmpp_cfg.FromAddr = XmppInit::kControlNodeJID;
    init.AddXmppChannelConfig(&xmpp_cfg);
    init.InitServer(xmpp_server, options.xmpp_port(), true);

    // Register XMPP channel peers 
    boost::scoped_ptr<BgpXmppChannelManager> bgp_peer_manager(
                    new BgpXmppChannelManager(xmpp_server, bgp_server.get()));
    sandesh_context.xmpp_peer_manager = bgp_peer_manager.get();
    IFMapChannelManager ifmap_channel_mgr(xmpp_server, &ifmap_server);
    ifmap_server.set_ifmap_channel_manager(&ifmap_channel_mgr);

    //Register services with Discovery Service Server
    DiscoveryServiceClient *ds_client = NULL; 
    if (!options.discovery_server().empty()) {
        tcp::endpoint dss_ep;
        boost::system::error_code error;
        dss_ep.address(address::from_string(options.discovery_server(), error));
        dss_ep.port(options.discovery_port());
        string subscriber_name = 
            g_vns_constants.ModuleNames.find(Module::CONTROL_NODE)->second;
        ds_client = new DiscoveryServiceClient(&evm, dss_ep, subscriber_name); 
        ds_client->Init();
        ControlNode::SetDiscoveryServiceClient(ds_client); 
  
        // publish xmpp-server service
        ControlNode::SetSelfIp(options.host_ip());
        if (!options.host_ip().empty()) {
            stringstream pub_ss;
            pub_ss << "<xmpp-server><ip-address>" << options.host_ip() <<
                      "</ip-address><port>" << options.xmpp_port() <<
                      "</port></xmpp-server>";
            string pub_msg;
            pub_msg = pub_ss.str();
            ds_client->Publish(DiscoveryServiceClient::XmppService, pub_msg);
        }

        // subscribe to collector service if not configured
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
            list.clear();
            Sandesh::InitGenerator(subscriber_name,
                                   options.hostname(),
                                   node_type_name,
                                   g_vns_constants.INSTANCE_ID_DEFAULT, 
                                   &evm,
                                   options.http_server_port(),
                                   csf,
                                   list,
                                   &sandesh_context);
        }
    }

    IFMapServerParser *ifmap_parser = IFMapServerParser::GetInstance("vnc_cfg");

    IFMapManager *ifmapmgr = new IFMapManager(&ifmap_server,
                options.ifmap_server_url(), options.ifmap_user(),
                options.ifmap_password(), options.ifmap_certs_store(),
                boost::bind(&IFMapServerParser::Receive, ifmap_parser,
                            &config_db, _1, _2, _3), evm.io_service(),
                ds_client);
    ifmap_server.set_ifmap_manager(ifmapmgr);

    CpuLoadData::Init();
    start_time = UTCTimestampUsec();
    ConnectionStateManager<ControlNodeStatus, ControlNodeProcessStatus>::
        GetInstance()->Init(*evm.io_service(), options.hostname(),
            g_vns_constants.ModuleNames.find(Module::CONTROL_NODE)->second,
            Sandesh::instance_id(),
            boost::bind(&ControlNodeGetConnectivityStatus, _1, _2, _3));
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
