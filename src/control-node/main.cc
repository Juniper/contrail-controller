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

bool ControlNodeInfoLogTimer(TaskTrigger *node_info_trigger) {
    node_info_trigger->Set();
    // Periodic timer. Restart
    return true;
}

bool ControlNodeVersion(string &build_info_str) {
    return MiscUtils::GetBuildInfo(MiscUtils::ControlNode, BuildInfo,
                                   build_info_str);
}

static void FillProtoStats(const IPeerDebugStats::ProtoStats &stats,
                           PeerProtoStats *proto_stats) {
    proto_stats->open = stats.open;
    proto_stats->keepalive = stats.keepalive;
    proto_stats->close = stats.close;
    proto_stats->update = stats.update;
    proto_stats->notification = stats.notification;
    proto_stats->total = stats.open + stats.keepalive + stats.close +
        stats.update + stats.notification;
}

static void FillRouteUpdateStats(const IPeerDebugStats::UpdateStats &stats,
                                 PeerUpdateStats *rt_stats) {
    rt_stats->total = stats.total;
    rt_stats->reach = stats.reach;
    rt_stats->unreach = stats.unreach;
}

static void FillRxErrorStats(const IPeerDebugStats::RxErrorStats &src,
                             PeerRxErrorStats *dest) {
    dest->inet6_error_stats.bad_inet6_xml_token_count =
        src.inet6_bad_xml_token_count;
    dest->inet6_error_stats.bad_inet6_prefix_count =
        src.inet6_bad_prefix_count;
    dest->inet6_error_stats.bad_inet6_nexthop_count =
        src.inet6_bad_nexthop_count;
    dest->inet6_error_stats.bad_inet6_afi_safi_count =
        src.inet6_bad_afi_safi_count;
}

static void FillPeerDebugStats(const IPeerDebugStats *peer_state,
                               PeerStatsInfo *stats) {
    PeerProtoStats proto_stats_tx;
    PeerProtoStats proto_stats_rx;
    PeerUpdateStats rt_stats_rx;
    PeerUpdateStats rt_stats_tx;
    PeerRxErrorStats dest_error_stats_rx;

    IPeerDebugStats::ProtoStats stats_rx;
    peer_state->GetRxProtoStats(&stats_rx);
    FillProtoStats(stats_rx, &proto_stats_rx);

    IPeerDebugStats::ProtoStats stats_tx;
    peer_state->GetTxProtoStats(&stats_tx);
    FillProtoStats(stats_tx, &proto_stats_tx);

    IPeerDebugStats::UpdateStats update_stats_rx;
    peer_state->GetRxRouteUpdateStats(&update_stats_rx);
    FillRouteUpdateStats(update_stats_rx, &rt_stats_rx);

    IPeerDebugStats::UpdateStats update_stats_tx;
    peer_state->GetTxRouteUpdateStats(&update_stats_tx);
    FillRouteUpdateStats(update_stats_tx, &rt_stats_tx);

    IPeerDebugStats::RxErrorStats src_error_stats_rx;
    peer_state->GetRxErrorStats(&src_error_stats_rx);
    FillRxErrorStats(src_error_stats_rx, &dest_error_stats_rx);

    stats->set_rx_proto_stats(proto_stats_rx);
    stats->set_tx_proto_stats(proto_stats_tx);
    stats->set_rx_update_stats(rt_stats_rx);
    stats->set_tx_update_stats(rt_stats_tx);
    stats->set_rx_error_stats(dest_error_stats_rx);
}

void FillXmppPeerStats(BgpServer *server, BgpXmppChannel *channel) {
    PeerStatsInfo stats;
    FillPeerDebugStats(channel->Peer()->peer_stats(), &stats);

    XmppPeerInfoData peer_info;
    peer_info.set_name(channel->Peer()->ToUVEKey());
    peer_info.set_peer_stats_info(stats);
    XMPPPeerInfo::Send(peer_info);
}

void FillBgpPeerStats(BgpServer *server, BgpPeer *peer) {
    PeerStatsInfo stats;
    FillPeerDebugStats(peer->peer_stats(), &stats);

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

bool ControlNodeInfoLogger(BgpServer *server,
                           BgpXmppChannelManager *xmpp_channel_mgr,
                           IFMapServer *ifmap_server,
                           uint64_t start_time,
                           Timer *node_info_log_timer) {
    LogControlNodePeerStats(server, xmpp_channel_mgr);

    BgpRouterState state;
    static BgpRouterState prev_state;
    static bool first = true, build_info_set = false;
    bool change = false;

    CpuLoadInfo cpu_load_info;
    CpuLoadData::FillCpuInfo(cpu_load_info, false);
    state.set_name(server->localname());

    uint32_t admin_down = server->admin_down();
    if (admin_down != prev_state.get_admin_down() || first) {
        state.set_admin_down(admin_down);
        prev_state.set_admin_down(admin_down);
        change = true;
    }

    string router_id = server->bgp_identifier_string();
    if (router_id != prev_state.get_router_id() || first) {
        state.set_router_id(router_id);
        prev_state.set_router_id(router_id);
        change = true;
    }

    uint32_t local_asn = server->local_autonomous_system();
    if (local_asn != prev_state.get_local_asn() || first) {
        state.set_local_asn(local_asn);
        prev_state.set_local_asn(local_asn);
        change = true;
    }

    uint32_t global_asn = server->autonomous_system();
    if (global_asn != prev_state.get_global_asn() || first) {
        state.set_global_asn(global_asn);
        prev_state.set_global_asn(global_asn);
        change = true;
    }

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

    SendCpuInfoStat<ControlCpuStateTrace, ControlCpuState>(server->localname(),
        cpu_load_info);

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

    uint32_t num_deleting_xmpp = xmpp_channel_mgr->deleting_count();
    if (num_deleting_xmpp != prev_state.get_num_deleting_xmpp_peer() || first) {
        state.set_num_deleting_xmpp_peer(num_deleting_xmpp);
        prev_state.set_num_deleting_xmpp_peer(num_deleting_xmpp);
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

    uint32_t num_deleting_bgp_peer = server->num_deleting_bgp_peer();
    if (num_deleting_bgp_peer != prev_state.get_num_deleting_bgp_peer() ||
        first) {
        state.set_num_deleting_bgp_peer(num_deleting_bgp_peer);
        prev_state.set_num_deleting_bgp_peer(num_deleting_bgp_peer);
        change = true;
    }

    uint32_t num_ri = server->num_routing_instance();
    if (num_ri != prev_state.get_num_routing_instance() || first) {
        state.set_num_routing_instance(num_ri);
        prev_state.set_num_routing_instance(num_ri);
        change = true;
    }

    uint32_t num_deleted_ri = server->num_deleted_routing_instance();
    if (num_deleted_ri != prev_state.get_num_deleted_routing_instance() ||
        first) {
        state.set_num_deleted_routing_instance(num_deleted_ri);
        prev_state.set_num_deleted_routing_instance(num_deleted_ri);
        change = true;
    }

    uint32_t num_service_chains = server->num_service_chains();
    if (num_service_chains != prev_state.get_num_service_chains() ||
        first) {
        state.set_num_service_chains(num_service_chains);
        prev_state.set_num_service_chains(num_service_chains);
        change = true;
    }

    uint32_t num_down_service_chains = server->num_down_service_chains();
    if (num_down_service_chains != prev_state.get_num_down_service_chains() ||
        first) {
        state.set_num_down_service_chains(num_down_service_chains);
        prev_state.set_num_down_service_chains(num_down_service_chains);
        change = true;
    }

    uint32_t num_static_routes = server->num_static_routes();
    if (num_static_routes != prev_state.get_num_static_routes() ||
        first) {
        state.set_num_static_routes(num_static_routes);
        prev_state.set_num_static_routes(num_static_routes);
        change = true;
    }

    uint32_t num_down_static_routes = server->num_down_static_routes();
    if (num_down_static_routes != prev_state.get_num_down_static_routes() ||
        first) {
        state.set_num_down_static_routes(num_down_static_routes);
        prev_state.set_num_down_static_routes(num_down_static_routes);
        change = true;
    }

    IFMapPeerServerInfoUI peer_server_info;
    ifmap_server->get_ifmap_manager()->GetPeerServerInfo(peer_server_info);
    if (peer_server_info != prev_state.get_ifmap_info() || first) {
        state.set_ifmap_info(peer_server_info);
        prev_state.set_ifmap_info(peer_server_info);
        change = true;
    }

    IFMapServerInfoUI server_info;
    ifmap_server->GetUIInfo(&server_info);
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
    size_t expected_connections) {
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
        options.ifmap_server_url(), options.ifmap_user(),
        options.ifmap_password(), options.ifmap_certs_store(),
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
    ConnectionStateManager<NodeStatusUVE, NodeStatus>::GetInstance()->Init(
        *evm.io_service(), options.hostname(),
        module_name, g_vns_constants.INSTANCE_ID_DEFAULT,
        boost::bind(&ControlNodeGetProcessStateCb,
                    bgp_server.get(), ifmap_manager, _1, _2, _3, 5));

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
    uint64_t start_time = UTCTimestampUsec();

    std::auto_ptr<Timer> node_info_log_timer(
        TimerManager::CreateTimer(
            *evm.io_service(), "ControlNode Info log timer"));

    std::auto_ptr<TaskTrigger> node_info_trigger(
        new TaskTrigger(
            boost::bind(&ControlNodeInfoLogger,
                        bgp_server.get(), bgp_peer_manager.get(),
                        &ifmap_server,
                        start_time, node_info_log_timer.get()),
            TaskScheduler::GetInstance()->GetTaskId("bgp::Config"), 0));

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
