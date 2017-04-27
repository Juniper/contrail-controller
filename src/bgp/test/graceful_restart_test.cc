/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <list>
#include <signal.h>

#include "base/task_annotations.h"
#include "base/test/addr_test_util.h"

#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/bgp_xmpp_sandesh.h"
#include "bgp/inet/inet_table.h"
#include "bgp/inet6/inet6_table.h"
#include "bgp/inet6vpn/inet6vpn_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/xmpp_message_builder.h"

#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_server.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_factory.h"

#define XMPP_CONTROL_SERV   "bgp.contrail.com"
#define PUBSUB_NODE_ADDR "bgp-node.contrail.com"

using namespace std;
using namespace boost;
using namespace boost::asio;
using namespace boost::assign;
using namespace boost::program_options;
using   boost::any_cast;
using ::testing::TestWithParam;
using ::testing::Bool;
using ::testing::ValuesIn;
using ::testing::Combine;
using task_util::TaskFire;

static int d_instances = 4;
static int d_routes = 4;
static int d_agents = 4;
static int d_peers = 4;
static int d_targets = 4;
static int d_http_port_ = 0;
static bool d_no_sandesh_server_ = false;

static string d_log_category_ = "";
static string d_log_level_ = "SYS_WARN";
static bool d_log_local_enable_ = false;
static bool d_log_trace_enable_ = false;
static bool d_log_disable_ = false;

static vector<int>  n_instances = boost::assign::list_of(d_instances);
static vector<int>  n_routes    = boost::assign::list_of(d_routes);
static vector<int>  n_agents    = boost::assign::list_of(d_agents);
static vector<int>  n_peers     = boost::assign::list_of(d_peers);
static vector<int>  n_targets   = boost::assign::list_of(d_targets);

static char **gargv;
static int    gargc;
static int    n_db_walker_wait_usecs = 0;

#define GR_TEST_LOG(str)                                         \
do {                                                             \
    log4cplus::Logger logger = log4cplus::Logger::getRoot();     \
    LOG4CPLUS_DEBUG(logger, "GR_TEST_LOG: "                      \
                    << __FILE__  << ":"  << __FUNCTION__ << "()" \
                    << ":"  << __LINE__ << " " << str);          \
} while (false)

static void process_command_line_args(int argc, char **argv) {
    static bool cmd_line_processed;

    if (cmd_line_processed) return;
    cmd_line_processed = true;

    int ninstances, nroutes, nagents, npeers, ntargets;
    bool cmd_line_arg_set = false;
    const unsigned long log_file_size = 1*1024*1024*1024; // 1GB
    const unsigned int log_file_index = 10;
    const string log_file = "<stdout>";
    bool log_file_uniquefy = false;

    // Declare the supported options.
    options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("http-port", value<int>(), "set http introspect server port number")
        ("log-category", value<string>()->default_value(d_log_category_),
            "set log category")
        ("log-disable", bool_switch(&d_log_disable_),
             "Disable logging")
        ("log-file", value<string>()->default_value(log_file),
             "Filename for the logs to be written to")
        ("log-file-index",
             value<unsigned int>()->default_value(log_file_index),
             "Maximum log file roll over index")
        ("log-file-size",
             value<unsigned long>()->default_value(log_file_size),
             "Maximum size of the log file")
        ("log-file-uniquefy", bool_switch(&log_file_uniquefy),
             "Use pid to make log-file name unique")
        ("log-level", value<string>()->default_value(d_log_level_),
            "set log level ")
        ("log-local-enable", bool_switch(&d_log_local_enable_),
             "Enable local logging")
        ("log-trace-enable", bool_switch(&d_log_trace_enable_),
             "Enable logging traces")
        ("no-sandesh-server", bool_switch(&d_no_sandesh_server_),
             "Do not add multicast routes")
        ("nroutes", value<int>()->default_value(d_routes),
             "set number of routes")
        ("nagents", value<int>()->default_value(d_agents),
             "set number of xmpp agents")
        ("npeers", value<int>()->default_value(d_peers),
             "set number of bgp peers")
        ("ninstances", value<int>()->default_value(d_instances),
             "set number of routing instances")
        ("ntargets", value<int>()->default_value(d_targets),
             "set number of route targets")
        ("db-walker-wait-usecs", value<int>(), "set usecs delay in walker cb")
        ;

    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    notify(vm);

    if (vm.count("help")) {
        cout << desc << "\n";
        exit(1);
    }

    if (vm.count("ninstances")) {
        ninstances = vm["ninstances"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("nroutes")) {
        nroutes = vm["nroutes"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("nagents")) {
        nagents = vm["nagents"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("npeers")) {
        npeers = vm["npeers"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("ntargets")) {
        ntargets = vm["ntargets"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("db-walker-wait-usecs")) {
        n_db_walker_wait_usecs = vm["db-walker-wait-usecs"].as<int>();
        cmd_line_arg_set = true;
    }

    if (vm.count("http-port")) {
        d_http_port_ = vm["http-port"].as<int>();
    }

    if (cmd_line_arg_set) {
        n_instances.clear();
        n_instances.push_back(ninstances);

        n_routes.clear();
        n_routes.push_back(nroutes);

        n_targets.clear();
        n_targets.push_back(ntargets);

        n_agents.clear();
        n_agents.push_back(nagents);

        n_peers.clear();
        n_peers.push_back(npeers);
    }

    // Retrieve logging params.
    if (vm.count("log-category"))
        d_log_category_ = vm["log-category"].as<string>();

    if (vm.count("log-level"))
        d_log_level_ = vm["log-level"].as<string>();

    if (d_log_disable_) {
        SetLoggingDisabled(true);
    }

    // Set Sandesh log category and level
    Sandesh::SetLoggingParams(d_log_local_enable_, d_log_category_,
                              d_log_level_, d_log_trace_enable_);

    if (!vm.count("log-file") || vm["log-file"].as<string>() == "<stdout>") {
        bgp_log_test::init();
    } else {
        ostringstream log_file;
        log_file << vm["log-file"].as<string>();
        if (log_file_uniquefy) {
            boost::system::error_code error;
            string hostname(boost::asio::ip::host_name(error));
            log_file << "." << hostname << "." << getpid();
        }

        Module::type module = Module::CONTROL_NODE;
        string module_name = g_vns_constants.ModuleNames.find(module)->second;
        bgp_log_test::init(log_file.str(),
            vm.count("log-file-size") ?
                vm["log-file-size"].as<unsigned long>() : log_file_size,
            vm.count("log-file-index") ?
                vm["log-file-index"].as<unsigned int>() : log_file_index,
                !d_log_disable_, d_log_level_, module_name, d_log_level_);
    }
}

static vector<int> GetInstanceParameters() {
    process_command_line_args(gargc, gargv);
    return n_instances;
}

static vector<int> GetAgentParameters() {
    process_command_line_args(gargc, gargv);
    return n_agents;
}

static vector<int> GetPeerParameters() {
    process_command_line_args(gargc, gargv);
    return n_peers;
}

static vector<int> GetRouteParameters() {
    process_command_line_args(gargc, gargv);
    return n_routes;
}

static vector<int> GetTargetParameters() {
    process_command_line_args(gargc, gargv);
    return n_targets;
}

class PeerCloseManagerTest : public PeerCloseManager {
public:
    explicit PeerCloseManagerTest(IPeerClose *peer_close) :
            PeerCloseManager(peer_close) {
    }
    ~PeerCloseManagerTest() { last_stats_ = stats(); }
    static Stats &last_stats() { return last_stats_; }
    static void reset_last_stats() {
        memset(&last_stats_, 0, sizeof(PeerCloseManagerTest::last_stats()));
    }

private:
    static Stats last_stats_;
};

PeerCloseManager::Stats PeerCloseManagerTest::last_stats_;

class BgpXmppChannelManagerMock : public BgpXmppChannelManager {
public:
    BgpXmppChannelManagerMock(XmppServer *x, BgpServer *b) :
        BgpXmppChannelManager(x, b), channel_(NULL) { }

    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel) {
        channel_ = new BgpXmppChannel(channel, bgp_server_, this);
        return channel_;
    }

    BgpXmppChannel *channel_;
};

typedef std::tr1::tuple<int, int, int, int, int> TestParams;

class SandeshServerTest : public SandeshServer {
public:
    SandeshServerTest(EventManager *evm) : SandeshServer(evm) { }
    virtual ~SandeshServerTest() { }
    virtual bool ReceiveSandeshMsg(SandeshSession *session,
                       const SandeshMessage *msg, bool rsc) {
        return true;
    }

private:
};

boost::scoped_ptr<BgpSandeshContext> sandesh_context_;
std::vector<BgpServerTest *> bgp_servers_;
std::vector<XmppServerTest *> xmpp_servers_;
std::vector<BgpXmppChannelManagerMock *> channel_managers_;

typedef std::map<XmppServerTest *, std::vector<test::NetworkAgentMock *> >
    XmppAgentsType;
XmppAgentsType xmpp_server_agents_;

class GracefulRestartTest : public ::testing::TestWithParam<TestParams> {
protected:
    GracefulRestartTest() : thread_(&evm_) { }

    virtual void SetUp();
    virtual void TearDown();
    void AgentCleanup();
    void AgentDelete();
    void Configure();
    void BgpPeerUp(BgpPeerTest *peer);
    void BgpPeerDown(BgpPeerTest *peer, TcpSession::Event event);
    void GracefulRestartTestStart();
    void GracefulRestartTestRun();
    string GetConfig(bool delete_config);
    ExtCommunitySpec *CreateRouteTargets();
    void AddRoutes();
    void CreateAgents();
    void Subscribe();
    void Unsubscribe();
    test::NextHops GetNextHops(test::NetworkAgentMock *agent, int instance_id);
    void AddOrDeleteXmppRoutes(bool add, int nroutes = -1,
                               int down_agents = -1);
    void AddOrDeleteBgpRoutes(bool add, int nroutes = -1, int down_agents = -1);
    void VerifyReceivedXmppRoutes(int routes);
    void DeleteRoutingInstances(int count,
            vector<test::NetworkAgentMock *> &dont_unsubscribe);
    void DeleteRoutingInstances(vector<int> instances,
            vector<test::NetworkAgentMock *> &dont_unsubscribe);
    void VerifyDeletedRoutingInstnaces(vector<int> instances);
    void VerifyRoutingInstances(BgpServer *server);
    void XmppAgentClose(int nagents = -1);
    void FireGRTimer(PeerCloseManagerTest *pc, bool is_ready);
    void FireGRTimer(BgpPeerTest *peer);
    void FireGRTimer(BgpXmppChannel *channel);
    void GRTimerCallback(PeerCloseManagerTest *pc);
    void InitParams();
    void VerifyRoutes(int count);
    bool IsReady(bool ready);
    void WaitForAgentToBeEstablished(test::NetworkAgentMock *agent);
    void WaitForPeerToBeEstablished( BgpPeerTest *peer);
    void BgpPeersAdminUpOrDown(bool down);
    bool AttemptGRHelperMode(BgpPeerTest *peer, int code, int subcode) const;

    void SandeshStartup();
    void SandeshShutdown();

    EventManager evm_;
    ServerThread thread_;
    BgpServerTest *server_;
    XmppServerTest *xmpp_server_;
    BgpXmppChannelManagerMock *channel_manager_;
    scoped_ptr<BgpInstanceConfigTest> master_cfg_;
    RoutingInstance *master_instance_;
    std::vector<test::NetworkAgentMock *> xmpp_agents_;
    std::vector<BgpPeerTest *> bgp_peers_;
    std::vector<BgpXmppChannel *> bgp_xmpp_channels_;
    std::vector<BgpPeerTest *> bgp_server_peers_;
    int n_families_;
    std::vector<Address::Family> familes_;
    int n_instances_;
    int n_routes_;
    int n_agents_;
    int n_peers_;
    int n_targets_;
    SandeshServerTest *sandesh_server_;

    struct GRTestParams {
        GRTestParams(test::NetworkAgentMock *agent, vector<int> instance_ids,
                        vector<int> nroutes, TcpSession::Event skip_tcp_event) {
            initialize(agent, NULL, instance_ids, nroutes, skip_tcp_event);
        }

        GRTestParams(test::NetworkAgentMock *agent, vector<int> instance_ids,
                        vector<int> nroutes) {
            initialize(agent, NULL, instance_ids, nroutes,
                       TcpSession::EVENT_NONE);
        }

        GRTestParams(test::NetworkAgentMock *agent) {
            initialize(agent, NULL, vector<int>(), vector<int>(),
                       TcpSession::EVENT_NONE);
        }

        GRTestParams(BgpPeerTest *peer, vector<int> instance_ids,
                     vector<int> nroutes, TcpSession::Event skip_tcp_event) {
            initialize(NULL, peer, instance_ids, nroutes, skip_tcp_event);
        }

        GRTestParams(BgpPeerTest *peer, vector<int> instance_ids,
                     vector<int> nroutes) {
            initialize(NULL, peer, instance_ids, nroutes,
                       TcpSession::EVENT_NONE);
        }

        GRTestParams(BgpPeerTest *peer) {
            initialize(NULL, peer, vector<int>(), vector<int>(),
                       TcpSession::EVENT_NONE);
        }

        void initialize(test::NetworkAgentMock *agent, BgpPeerTest *peer,
                        vector<int> instance_ids, vector<int> nroutes,
                        TcpSession::Event skip_tcp_event) {
            this->agent = agent;
            this->peer = peer;
            this->instance_ids = instance_ids;
            this->nroutes = nroutes;
            this->skip_tcp_event = skip_tcp_event;
            this->send_eor = true;
        }

        bool should_send_eor() const { return send_eor; }

        test::NetworkAgentMock *agent;
        BgpPeerTest *peer;
        vector<int> instance_ids;
        vector<int> nroutes;
        TcpSession::Event skip_tcp_event;
        bool send_eor;
    };
    void ProcessFlippingAgents(int &total_routes, int remaining_instances,
                               std::vector<GRTestParams> &n_flipping_agents);
    void ProcessVpnRoute(BgpPeerTest *peer, int instance,
                         int n_routes, bool add);
    void ProcessInetVpnRoute(BgpPeerTest *peer, int instance, int n_routes,
                             bool add);
    void ProcessInet6VpnRoute(BgpPeerTest *peer, int instance, int n_routes,
                              bool add);
    Inet6Prefix GetIPv6Prefix(int agent_id, int instance, int route_id) const;
    Inet6VpnPrefix GetIPv6VpnPrefix(int peer_id, int instance, int rt) const;
    string GetEnetPrefix(string inet_prefix) const;
    void ProcessFlippingPeers(int &total_routes, int remaining_instances,
        vector<GRTestParams> &n_flipping_peers);

    std::vector<GRTestParams> n_flipped_agents_;
    std::vector<GRTestParams> n_flipped_peers_;
    std::vector<test::NetworkAgentMock *> n_down_from_agents_;
    std::vector<BgpPeerTest *> n_down_from_peers_;
    std::vector<int> instances_to_delete_before_gr_;
    std::vector<int> instances_to_delete_during_gr_;
};

static int d_wait_for_idle_ = 30; // Seconds
static void WaitForIdle() {
    if (d_wait_for_idle_)
        task_util::WaitForIdle(d_wait_for_idle_, false, false);
}

static void SignalHandler (int sig) {
    if (sig != SIGUSR1)
        return;
    ifstream infile("/tmp/.gr_test_bgp_server_index");
    if (!infile)
        return;

    int i;
    infile >> i;
    sandesh_context_->bgp_server = bgp_servers_[i];
    sandesh_context_->xmpp_peer_manager = channel_managers_[i];
}

void GracefulRestartTest::SetUp() {
    InitParams();
    SandeshStartup();

    for (int i = 0; i <= n_peers_; i++) {
        bgp_servers_.push_back(new BgpServerTest(&evm_,
                    "RTR" + boost::lexical_cast<string>(i)));
        xmpp_servers_.push_back(new XmppServerTest(&evm_, XMPP_CONTROL_SERV));
        channel_managers_.push_back(new BgpXmppChannelManagerMock(
                                        xmpp_servers_[i], bgp_servers_[i]));

        // Disable GR helper mode in non DUTs.
        if (i) {
            bgp_servers_[i]->set_gr_helper_disable(true);
            xmpp_servers_[i]->set_gr_helper_disable(true);
        }
    }

    server_ = bgp_servers_[0];
    xmpp_server_ = xmpp_servers_[0];
    channel_manager_ = channel_managers_[0];

    master_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
        BgpConfigManager::kMasterInstance, "", ""));
    master_instance_ = static_cast<RoutingInstance *>(
        server_->routing_instance_mgr()->GetRoutingInstance(
            BgpConfigManager::kMasterInstance));
    n_families_ = 2;
    familes_.push_back(Address::INET);
    familes_.push_back(Address::INETVPN);

    server_->session_manager()->Initialize(0);
    xmpp_server_->Initialize(0, false);
    for (int i = 1; i <= n_peers_; i++) {
        xmpp_servers_[i]->Initialize(0, false);
        bgp_servers_[i]->session_manager()->Initialize(0);

        // Disable logging in non dut bgp servers.
        bgp_servers_[i]->set_logging_disabled(true);
    }

    sandesh_context_->bgp_server = bgp_servers_[0];
    sandesh_context_->xmpp_peer_manager = channel_managers_[0];

    thread_.Start();
}

void GracefulRestartTest::SandeshStartup() {
    sandesh_context_.reset(new BgpSandeshContext());
    RegisterSandeshShowXmppExtensions(sandesh_context_.get());
    if (d_no_sandesh_server_) {
        Sandesh::set_client_context(sandesh_context_.get());
        sandesh_server_ = NULL;
        return;
    }

    // Initialize SandeshServer.
    sandesh_server_ = new SandeshServerTest(&evm_);
    sandesh_server_->Initialize(0);

    boost::system::error_code error;
    string hostname(boost::asio::ip::host_name(error));
    Sandesh::InitGenerator("BgpUnitTestSandeshClient", hostname,
                           "BgpTest", "Test", &evm_,
                            d_http_port_, sandesh_context_.get());
    Sandesh::ConnectToCollector("127.0.0.1",
                                sandesh_server_->GetPort());
    GR_TEST_LOG("Introspect at http://localhost:" << Sandesh::http_port());
}

void GracefulRestartTest::SandeshShutdown() {
    if (d_no_sandesh_server_)
        return;

    Sandesh::Uninit();
    WaitForIdle();
    // TASK_UTIL_EXPECT_FALSE(sandesh_server_->HasSessions());
    sandesh_server_->Shutdown();
    WaitForIdle();
    TcpServerManager::DeleteServer(sandesh_server_);
    sandesh_server_ = NULL;
    WaitForIdle();
}

void GracefulRestartTest::TearDown() {
    WaitForIdle();

    for (int i = 1; i <= n_instances_; i++) {
        BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
            ProcessVpnRoute(peer, i, n_routes_, false);
        }
    }

    for (int i = 0; i <= n_peers_; i++)
        xmpp_servers_[i]->Shutdown();
    XmppAgentClose();
    WaitForIdle();

    VerifyRoutes(0);
    VerifyReceivedXmppRoutes(0);

    if (n_agents_) {
        TASK_UTIL_EXPECT_EQ(0, xmpp_server_->connection_map().size());
    }
    AgentCleanup();
    for (int i = 0; i <= n_peers_; i++) {
        TASK_UTIL_EXPECT_EQ(0, channel_managers_[i]->channel_map().size());
        delete channel_managers_[i];
    }
    WaitForIdle();

    for (int i = 0; i <= n_peers_; i++)
        bgp_servers_[i]->Shutdown();

    WaitForIdle();
    evm_.Shutdown();
    thread_.Join();
    WaitForIdle();

    for (int i = 0; i <= n_peers_; i++) {
        TcpServerManager::DeleteServer(xmpp_servers_[i]);
        delete bgp_servers_[i];
    }

    AgentDelete();
    SandeshShutdown();
    WaitForIdle();
    sandesh_context_.reset();
    bgp_servers_.clear();
    xmpp_servers_.clear();
    channel_managers_.clear();
    xmpp_server_agents_.clear();
}

void GracefulRestartTest::Configure() {
    string config = GetConfig(false);
    for (int i = 0; i <= n_peers_; i++)
        bgp_servers_[i]->Configure(config.c_str());
    WaitForIdle();

    for (int i = 0; i <= n_peers_; i++)
        VerifyRoutingInstances(bgp_servers_[i]);

    // Get peers to DUT (RTR0) to bgp_peers_ vector.
    for (int i = 1; i <= n_peers_; i++) {
        string uuid = BgpConfigParser::session_uuid("RTR0",
                          "RTR" + boost::lexical_cast<string>(i), 1);
        TASK_UTIL_EXPECT_NE(static_cast<BgpPeerTest *>(NULL),
                            bgp_servers_[i]->FindPeerByUuid(
                                BgpConfigManager::kMasterInstance, uuid));
        BgpPeerTest *peer = bgp_servers_[i]->FindPeerByUuid(
                                BgpConfigManager::kMasterInstance, uuid);
        peer->set_id(i-1);
        bgp_peers_.push_back(peer);
    }

    // Get peers in DUT (RTR0) to other bgp_servers.
    for (int i = 1; i <= n_peers_; i++) {
        string uuid = BgpConfigParser::session_uuid("RTR0",
                          "RTR" + boost::lexical_cast<string>(i), 1);
        TASK_UTIL_EXPECT_NE(static_cast<BgpPeerTest *>(NULL),
                            bgp_servers_[i]->FindPeerByUuid(
                                BgpConfigManager::kMasterInstance, uuid));
        BgpPeerTest *peer = bgp_servers_[0]->FindPeerByUuid(
                                BgpConfigManager::kMasterInstance, uuid);
        peer->set_id(i-1);
        peer->attempt_gr_helper_mode_fnc_ =
            boost::bind(&GracefulRestartTest::AttemptGRHelperMode, this, peer,
                        _1, _2);
        bgp_server_peers_.push_back(peer);
    }
}

void GracefulRestartTest::AgentCleanup() {
    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        agent->Delete();
    }

    BOOST_FOREACH(XmppAgentsType::value_type &i, xmpp_server_agents_) {
        BOOST_FOREACH(test::NetworkAgentMock *agent, i.second) {
            agent->Delete();
        }
    }
}

void GracefulRestartTest::AgentDelete() {
    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        delete agent;
    }

    BOOST_FOREACH(XmppAgentsType::value_type &i, xmpp_server_agents_) {
        BOOST_FOREACH(test::NetworkAgentMock *agent, i.second) {
            delete agent;
        }
    }
}

void GracefulRestartTest::VerifyRoutes(int count) {
    for (int i = 0; i < n_families_; i++) {
        BgpTable *tb = master_instance_->GetTable(familes_[i]);
        if (count && n_agents_ && familes_[i] == Address::INETVPN) {
            BGP_VERIFY_ROUTE_COUNT(tb,
                (n_agents_ + n_peers_) * n_instances_ * count);
        }
    }
}

string GracefulRestartTest::GetConfig(bool delete_config) {
    ostringstream out;

    if (delete_config)
        out << "<delete>";
    else
        out << "<config>";

    out << "<global-system-config>\
               <graceful-restart-parameters>\
                   <enable>true</enable>\
                   <restart-time>600</restart-time>\
                   <long-lived-restart-time>60000</long-lived-restart-time>\
                   <end-of-rib-timeout>120</end-of-rib-timeout>\
                   <bgp-helper-enable>true</bgp-helper-enable>\
                   <xmpp-helper-enable>true</xmpp-helper-enable>\
               </graceful-restart-parameters>\
           </global-system-config>";

    for (int i = 0; i <= n_peers_; i++) {
        out << "<bgp-router name=\'RTR" << i << "\'>\
                    <identifier>192.168.0." << i << "</identifier>\
                    <address>127.0.0.1</address>" <<
                    "<autonomous-system>" << (i+1) << "</autonomous-system>\
                    <port>" << bgp_servers_[i]->session_manager()->GetPort();
        out <<      "</port>";

        for (int j = 0; j <= n_peers_; j++) {

            // Do not peer with self
            if (i == j)
                continue;

            // Peer only with DUT
            if (i > 0 && j > 0)
                break;

            // Create a session between DUT and other BgpServers.
            out << "<session to=\'RTR" << j << "\'>\
                        <admin-down>false</admin-down>\
                        <address-families>\
                            <family>inet-vpn</family>\
                            <family>inet6-vpn</family>\
                            <family>e-vpn</family>\
                            <family>erm-vpn</family>\
                            <family>route-target</family>\
                        </address-families>\
                    </session>";
        }
        out << "</bgp-router>";
    }

    for (int i = 1; i <= n_instances_; i++) {
        out << "<routing-instance name='instance" << i << "'>\n";
        for (int j = 1; j <= n_targets_; j++) {
            out << "    <vrf-target>target:1:" << j << "</vrf-target>\n";
        }
        out << "</routing-instance>\n";
    }

    out << "</config>";
    BGP_DEBUG_UT("Applying config" << out.str());

    return out.str();
}

void GracefulRestartTest::BgpPeersAdminUpOrDown(bool down) {
    ostringstream out;

    out << "<config>";
    for (int i = 1; i <= n_peers_; i++) {
        out << "<bgp-router name=\'RTR" << i << "\'>";

        // Mark all sessions to DUT (RTR0) as down initially.
        string admin_down = down ? "true" : "false";
        out << "<session to=\'RTR0\'>\
                   <admin-down>" << admin_down << "</admin-down>\
               </session></bgp-router>";
    }
    out << "</config>";

    BGP_DEBUG_UT("Applying config" << out.str());
    for (int i = 1; i <= n_peers_; i++)
        bgp_servers_[i]->Configure(out.str().c_str());
    WaitForIdle();
}

bool GracefulRestartTest::IsReady(bool ready) {
    return ready;
}

void GracefulRestartTest::WaitForAgentToBeEstablished(
        test::NetworkAgentMock *agent) {
    TASK_UTIL_EXPECT_TRUE(agent->IsChannelReady());
    TASK_UTIL_EXPECT_TRUE(agent->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(bgp_xmpp_channels_[agent->id()]->Peer()->IsReady());
}

void GracefulRestartTest::WaitForPeerToBeEstablished(BgpPeerTest *peer) {
    TASK_UTIL_EXPECT_TRUE(peer->IsReady());
    TASK_UTIL_EXPECT_TRUE(bgp_server_peers_[peer->id()]->IsReady());
}

ExtCommunitySpec *GracefulRestartTest::CreateRouteTargets() {
    auto_ptr<ExtCommunitySpec> commspec(new ExtCommunitySpec());

    for (int i = 1; i <= n_targets_; i++) {
        RouteTarget tgt = RouteTarget::FromString(
                "target:1:" + boost::lexical_cast<string>(i));
        const ExtCommunity::ExtCommunityValue &extcomm =
            tgt.GetExtCommunity();
        uint64_t value = get_value(extcomm.data(), extcomm.size());
        commspec->communities.push_back(value);
    }

    if (commspec->communities.empty()) return NULL;
    return commspec.release();
}

void GracefulRestartTest::AddRoutes() {
    if (!n_agents_ && !n_peers_)
        return;

    CreateAgents();
    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        WaitForAgentToBeEstablished(agent);
    }

    BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
        BGP_WAIT_FOR_PEER_STATE(peer, StateMachine::ESTABLISHED);
    }

    Subscribe();
    VerifyReceivedXmppRoutes(0);
    AddOrDeleteBgpRoutes(true);
    AddOrDeleteXmppRoutes(true);
    WaitForIdle();
    VerifyReceivedXmppRoutes(n_instances_ * (n_agents_ + n_peers_) * n_routes_);
}

void GracefulRestartTest::CreateAgents() {
    Ip4Prefix prefix(Ip4Prefix::FromString("127.0.0.1/32"));

    for (int i = 0; i < n_agents_; i++) {

        // create an XMPP client in server A
        test::NetworkAgentMock *agent = new test::NetworkAgentMock(&evm_,
            "agent" + boost::lexical_cast<string>(i) +
                "@vnsw.contrailsystems.com",
            xmpp_server_->GetPort(), prefix.ip4_addr().to_string());
        agent->set_id(i);
        xmpp_agents_.push_back(agent);
        WaitForIdle();

        TASK_UTIL_EXPECT_NE_MSG(static_cast<BgpXmppChannel *>(NULL),
                          channel_manager_->channel_,
                          "Waiting for channel_manager_->channel_ to be set");
        bgp_xmpp_channels_.push_back(channel_manager_->channel_);
        channel_manager_->channel_ = NULL;
        prefix = task_util::Ip4PrefixIncrement(prefix);
    }

    for (int j = 1; j <= n_peers_; j++) {
        xmpp_server_agents_.insert(make_pair(xmpp_servers_[j],
                                   vector<test::NetworkAgentMock *>()));
        for (int i = 0; i < n_agents_; i++) {

            // Create a dummy agent for other bgp speakers too.
            test::NetworkAgentMock *agent = new test::NetworkAgentMock(&evm_,
                "dummy_agent" + boost::lexical_cast<string>(i) +
                "@vnsw.contrailsystems.com",
                xmpp_servers_[j]->GetPort(), prefix.ip4_addr().to_string());
            xmpp_server_agents_[xmpp_servers_[j]].push_back(agent);
            prefix = task_util::Ip4PrefixIncrement(prefix);
        }
    }

    for (int j = 1; j <= n_peers_; j++) {
        BOOST_FOREACH(test::NetworkAgentMock *agent,
                xmpp_server_agents_[xmpp_servers_[j]]) {
            WaitForAgentToBeEstablished(agent);
            for (int k = 1; k <= n_instances_; k++) {
                string instance_name =
                    "instance" + boost::lexical_cast<string>(k);
                agent->SubscribeAll(instance_name, k);
            }
        }
    }
}

void GracefulRestartTest::Subscribe() {

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        agent->SubscribeAll(BgpConfigManager::kMasterInstance, -1);
        for (int i = 1; i <= n_instances_; i++) {
            string instance_name = "instance" + boost::lexical_cast<string>(i);
            agent->SubscribeAll(instance_name, i);
        }
    }
    WaitForIdle();
}

void GracefulRestartTest::Unsubscribe() {

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        agent->UnsubscribeAll(BgpConfigManager::kMasterInstance);
        for (int i = 1; i <= n_instances_; i++) {
            string instance_name = "instance" + boost::lexical_cast<string>(i);
            agent->UnsubscribeAll(instance_name);
        }
    }
    VerifyReceivedXmppRoutes(0);
    WaitForIdle();
}

test::NextHops GracefulRestartTest::GetNextHops (test::NetworkAgentMock *agent,
                                                 int instance_id) {
    test::NextHops nexthops;
    nexthops.push_back(test::NextHop("100.100.100." +
                           boost::lexical_cast<string>(agent->id()),
                           10000 + instance_id));
    return nexthops;
}

void GracefulRestartTest::ProcessVpnRoute(BgpPeerTest *peer, int instance,
                                          int n_routes, bool add) {
    ProcessInetVpnRoute(peer, instance, n_routes, add);
    ProcessInet6VpnRoute(peer, instance, n_routes, add);
}

void GracefulRestartTest::ProcessInetVpnRoute(BgpPeerTest *peer, int instance,
                                              int n_routes, bool add) {

    RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
        peer->server()->routing_instance_mgr()->GetRoutingInstance(
            BgpConfigManager::kMasterInstance));
    BgpTable *table = rtinstance->GetTable(Address::INETVPN);


    InetVpnPrefix vpn_prefix(InetVpnPrefix::FromString(
        "123:" + boost::lexical_cast<string>(instance) + ":" +
        "20." + boost::lexical_cast<string>(instance) + "." +
        boost::lexical_cast<string>(peer->id()) + ".1/32"));

    DBRequest req;
    boost::scoped_ptr<BgpAttrLocalPref> local_pref;
    boost::scoped_ptr<ExtCommunitySpec> commspec;

    for (int rt = 0; rt < n_routes; rt++,
        vpn_prefix = task_util::InetVpnPrefixIncrement(vpn_prefix)) {

        req.key.reset(new InetVpnTable::RequestKey(vpn_prefix, NULL));
        req.oper = add ? DBRequest::DB_ENTRY_ADD_CHANGE :
                         DBRequest::DB_ENTRY_DELETE;

        local_pref.reset(new BgpAttrLocalPref(100));

        BgpAttrSpec attr_spec;
        attr_spec.push_back(local_pref.get());

        BgpAttrNextHop nexthop(0x7f010000 + peer->id());
        attr_spec.push_back(&nexthop);

        commspec.reset(CreateRouteTargets());

        TunnelEncap tun_encap(std::string("gre"));
        commspec->communities.push_back(get_value(
                    tun_encap.GetExtCommunity().begin(), 8));
        attr_spec.push_back(commspec.get());
        BgpAttrPtr attr = peer->server()->attr_db()->Locate(attr_spec);

        req.data.reset(new InetTable::RequestData(attr, 0,
                                                  1000*instance + rt));
        table->Enqueue(&req);
    }
    WaitForIdle();
}

Inet6VpnPrefix GracefulRestartTest::GetIPv6VpnPrefix(int peer_id,
        int instance, int rt) const {
    string pre_prefix = "65412:" + integerToString(peer_id) +
        ":2001:bbbb:bbbb::";
    string peer_id_str = integerToHexString(peer_id);
    string instance_id_str = integerToHexString(instance);
    string route_id_str = integerToHexString(rt + 1);
    string prefix_str = pre_prefix + peer_id_str + ":" +
                        instance_id_str + ":" + route_id_str + "/128";
    return Inet6VpnPrefix::FromString(prefix_str);
}

void GracefulRestartTest::ProcessInet6VpnRoute(BgpPeerTest *peer, int instance,
                                               int n_routes, bool add) {
    RoutingInstance *rtinstance = static_cast<RoutingInstance *>(
        peer->server()->routing_instance_mgr()->GetRoutingInstance(
            BgpConfigManager::kMasterInstance));
    BgpTable *table = rtinstance->GetTable(Address::INET6VPN);

    DBRequest req;
    boost::scoped_ptr<BgpAttrLocalPref> local_pref;
    boost::scoped_ptr<ExtCommunitySpec> commspec;

    for (int rt = 0; rt < n_routes; rt++) {
        Inet6VpnPrefix inet6vpn_prefix =
            GetIPv6VpnPrefix(peer->id(), instance, rt + 1);
        req.key.reset(new Inet6VpnTable::RequestKey(inet6vpn_prefix, NULL));
        req.oper = add ? DBRequest::DB_ENTRY_ADD_CHANGE :
                         DBRequest::DB_ENTRY_DELETE;

        local_pref.reset(new BgpAttrLocalPref(100));

        BgpAttrSpec attr_spec;
        attr_spec.push_back(local_pref.get());

        BgpAttrNextHop nexthop(0x7f010000 + peer->id());
        attr_spec.push_back(&nexthop);

        commspec.reset(CreateRouteTargets());

        TunnelEncap tun_encap(std::string("gre"));
        commspec->communities.push_back(get_value(
                    tun_encap.GetExtCommunity().begin(), 8));
        attr_spec.push_back(commspec.get());
        BgpAttrPtr attr = peer->server()->attr_db()->Locate(attr_spec);

        req.data.reset(new Inet6Table::RequestData(attr, 0,
                                                   1000*instance + rt));
        table->Enqueue(&req);
    }
    WaitForIdle();
}

void GracefulRestartTest::AddOrDeleteBgpRoutes(bool add, int n_routes,
                                               int down_peers) {
    if (n_routes ==-1)
        n_routes = n_routes_;

    if (down_peers == -1)
        down_peers = n_peers_;

    BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
        if (down_peers-- < 1)
            continue;
        for (int i = 1; i <= n_instances_; i++)
            ProcessVpnRoute(peer, i, n_routes, add);
    }
}

Inet6Prefix GracefulRestartTest::GetIPv6Prefix(int agent_id, int instance_id,
        int route_id) const {
    string inet6_prefix_str = "2001:aaaa:aaaa::" +
        integerToHexString(agent_id) + ":" + integerToHexString(instance_id) +
        ":" + integerToHexString(route_id + 1) + "/128";
    return Inet6Prefix::FromString(inet6_prefix_str);
}

// Generate enet address from a inet address.
string GracefulRestartTest::GetEnetPrefix(string inet_prefix) const {
    vector<string> octets;
    boost::split(octets, inet_prefix, boost::is_any_of("/."));

    char buf[32];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:00:00",
             atoi(octets[0].c_str()), atoi(octets[1].c_str()),
             atoi(octets[2].c_str()), atoi(octets[3].c_str()));
    return string(buf);
}

void GracefulRestartTest::AddOrDeleteXmppRoutes(bool add, int n_routes,
                                                int down_agents) {
    if (n_routes ==-1)
        n_routes = n_routes_;

    if (down_agents == -1)
        down_agents = n_agents_;

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        if (down_agents-- < 1)
            continue;

        for (int i = 1; i <= n_instances_; i++) {
            string instance_name = "instance" + boost::lexical_cast<string>(i);

            Ip4Prefix prefix(Ip4Prefix::FromString(
                "10." + boost::lexical_cast<string>(i) + "." +
                boost::lexical_cast<string>(agent->id()) + ".1/32"));

            for (int rt = 0; rt < n_routes; rt++,
                prefix = task_util::Ip4PrefixIncrement(prefix)) {
                Inet6Prefix inet6_prefix = GetIPv6Prefix(agent->id(), i,
                                                         rt + 1);
                if (add) {
                    agent->AddRoute(instance_name, prefix.ToString(),
                                    GetNextHops(agent, i));
                    agent->AddEnetRoute(instance_name,
                                        GetEnetPrefix(prefix.ToString()),
                                        GetNextHops(agent, i));
                    agent->AddInet6Route(instance_name, inet6_prefix.ToString(),
                                         GetNextHops(agent, i));
                } else {
                    agent->DeleteRoute(instance_name, prefix.ToString());
                    agent->DeleteEnetRoute(instance_name,
                                           GetEnetPrefix(prefix.ToString()));
                    agent->DeleteInet6Route(instance_name,
                                            inet6_prefix.ToString());
                }
            }
        }
    }
    WaitForIdle();
}

void GracefulRestartTest::VerifyReceivedXmppRoutes(int routes) {
    if (!n_agents_) return;

    int agent_id = 0;
    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        agent_id++;
        if (routes > 0 && !agent->IsEstablished())
            continue;
        for (int i = 1; i <= n_instances_; i++) {
            string instance_name = "instance" + boost::lexical_cast<string>(i);
            if (!agent->HasSubscribed(instance_name))
                continue;
            TASK_UTIL_EXPECT_EQ_MSG(routes, agent->RouteCount(instance_name),
                "Agent " + agent->ToString() +
                ": Wait for ipv4 routes in " + instance_name);
            TASK_UTIL_EXPECT_EQ_MSG(routes, agent->Inet6RouteCount(
                instance_name), "Agent " + agent->ToString() +
                ": Wait for ipv6 routes in " + instance_name);
        }
    }
    WaitForIdle();
}

void GracefulRestartTest::DeleteRoutingInstances(int count,
        vector<test::NetworkAgentMock *> &dont_unsubscribe) {
    if (!count)
        return;
    vector<int> instances = vector<int>();
    for (int i = 1; i <= count; i++)
        instances.push_back(i);
    DeleteRoutingInstances(instances, dont_unsubscribe);
}

void GracefulRestartTest::DeleteRoutingInstances(vector<int> instances,
         vector<test::NetworkAgentMock *> &dont_unsubscribe) {
    if (instances.empty())
        return;

    ostringstream out;
    out << "<delete>";
    BOOST_FOREACH(int i, instances) {
        out << "<routing-instance name='instance" << i << "'>\n";
        for (int j = 1; j <= n_targets_; j++) {
            out << "    <vrf-target>target:1:" << j << "</vrf-target>\n";
        }
        out << "</routing-instance>\n";
    }
    out << "</delete>";

    server_->Configure(out.str().c_str());
    WaitForIdle();

    // Unsubscribe from all agents who have subscribed
    BOOST_FOREACH(int i, instances) {
        string instance_name = "instance" + boost::lexical_cast<string>(i);
        BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
            if (!agent->IsEstablished() || !agent->HasSubscribed(instance_name))
                continue;
            if (std::find(dont_unsubscribe.begin(), dont_unsubscribe.end(),
                          agent) == dont_unsubscribe.end())
                agent->UnsubscribeAll(instance_name);
        }

        BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
            if (!peer->IsReady())
                continue;
            ProcessVpnRoute(peer, i, n_routes_, false);
        }
    }
    WaitForIdle();
}

void GracefulRestartTest::VerifyDeletedRoutingInstnaces(vector<int> instances) {
    BOOST_FOREACH(int i, instances) {
        string instance_name = "instance" + boost::lexical_cast<string>(i);
        TASK_UTIL_EXPECT_EQ(static_cast<RoutingInstance *>(NULL),
                            server_->routing_instance_mgr()->\
                                GetRoutingInstance(instance_name));
    }
    WaitForIdle();
}

void GracefulRestartTest::VerifyRoutingInstances(BgpServer *server) {
    for (int i = 1; i <= n_instances_; i++) {
        string instance_name = "instance" + boost::lexical_cast<string>(i);
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
                            server->routing_instance_mgr()->\
                                GetRoutingInstance(instance_name));
    }

    // Verify 'default' master routing-instance
    TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
                        server->routing_instance_mgr()->GetRoutingInstance(
                               BgpConfigManager::kMasterInstance));
}

bool GracefulRestartTest::AttemptGRHelperMode(BgpPeerTest *peer, int code,
                                              int subcode) const {
    if (code == BgpProto::Notification::Cease &&
            subcode == BgpProto::Notification::AdminShutdown)
        return true;
    return peer->AttemptGRHelperModeDefault(code, subcode);
}

// Invoke stale timer callbacks directly to speed up.
void GracefulRestartTest::GRTimerCallback(PeerCloseManagerTest *pc) {
    CHECK_CONCURRENCY("timer::TimerTask");

    // Fire the timer.
    assert(!pc->RestartTimerCallback());
}

void GracefulRestartTest::FireGRTimer(PeerCloseManagerTest *pc, bool is_ready) {
    if (pc->state() != PeerCloseManager::GR_TIMER)
        return;
    if (is_ready) {
        uint64_t sweep = pc->stats().sweep;
        TaskFire(boost::bind(&GracefulRestartTest::GRTimerCallback, this, pc),
                 "timer::TimerTask");
        TASK_UTIL_EXPECT_EQ(sweep + 1, pc->stats().sweep);
        TASK_UTIL_EXPECT_EQ(PeerCloseManager::NONE, pc->state());
        WaitForIdle();
        return;
    }

    uint64_t deletes = pc->stats().deletes;
    PeerCloseManager::Stats stats;
    bool is_xmpp = pc->peer_close()->peer()->IsXmppPeer();
    WaitForIdle();
    PeerCloseManagerTest::reset_last_stats();
    while (true) {
        if (pc->state() == PeerCloseManager::GR_TIMER ||
                pc->state() == PeerCloseManager::LLGR_TIMER)
            TaskFire(boost::bind(&GracefulRestartTest::GRTimerCallback,
                                 this, pc), "timer::TimerTask");
        WaitForIdle();
        stats = is_xmpp ? PeerCloseManagerTest::last_stats() : pc->stats();
        if (stats.deletes > deletes)
            break;
    }
    EXPECT_GT(stats.deletes, deletes);
}

void GracefulRestartTest::FireGRTimer(BgpPeerTest *peer) {
    FireGRTimer(dynamic_cast<PeerCloseManagerTest *>(peer->close_manager()),
                peer->IsReady());
}

void GracefulRestartTest::FireGRTimer(BgpXmppChannel *channel) {
    FireGRTimer(dynamic_cast<PeerCloseManagerTest *>(channel->close_manager()),
                channel->Peer()->IsReady());
}

void GracefulRestartTest::XmppAgentClose(int nagents) {
    if (nagents < 1)
        nagents = xmpp_agents_.size();

    int down_count = nagents;
    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        agent->SessionDown();
        if (!--down_count)
            break;
    }

    down_count = nagents;
    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        TASK_UTIL_EXPECT_EQ(down_count < 1, agent->IsEstablished());
        down_count--;
    }
}

void GracefulRestartTest::InitParams() {
    n_instances_ = ::std::tr1::get<0>(GetParam());
    n_routes_    = ::std::tr1::get<1>(GetParam());
    n_agents_    = ::std::tr1::get<2>(GetParam());
    n_peers_     = ::std::tr1::get<3>(GetParam());
    n_targets_   = ::std::tr1::get<4>(GetParam());
}

// Bring up n_agents_ in n_instances_ and advertise
//     n_routes_ (v4 and v6) in each connection
// Verify that n_agents_ * n_instances_ * n_routes_ routes are received in
//     agent in each instance
// * Subset * picked serially/randomly
// Subset of agents support GR
// Subset of routing-instances are deleted
// Subset of agents go down permanently (Triggered from agents)
// Subset of agents flip (go down and come back up) (Triggered from agents)
// Subset of agents go down permanently (Triggered from control-node)
// Subset of agents flip (Triggered from control-node)
//     Subset of subscriptions after restart
//     Subset of routes are [re]advertised after restart
//     Subset of routing-instances are deleted (during GR)
void GracefulRestartTest::GracefulRestartTestStart () {
    Configure();

    //  Bring up n_agents_ in n_instances_ and advertise n_routes_ per session
    AddRoutes();
    VerifyRoutes(n_routes_);
}

void GracefulRestartTest::BgpPeerUp(BgpPeerTest *peer) {
    TASK_UTIL_EXPECT_FALSE(peer->IsReady());
    peer->SetAdminState(false);
    TASK_UTIL_EXPECT_TRUE(peer->IsReady());
}

void GracefulRestartTest::ProcessFlippingAgents(int &total_routes,
        int remaining_instances,
        vector<GRTestParams> &n_flipping_agents) {
    int flipping_count = 3;

    for (int f = 0; f < flipping_count; f++) {
        BOOST_FOREACH(GRTestParams gr_test_param, n_flipping_agents) {
            test::NetworkAgentMock *agent = gr_test_param.agent;
            TASK_UTIL_EXPECT_FALSE(agent->IsEstablished());
            agent->SessionUp();
            WaitForAgentToBeEstablished(agent);
        }

        BOOST_FOREACH(GRTestParams gr_test_param, n_flipping_agents) {
            test::NetworkAgentMock *agent = gr_test_param.agent;
            WaitForAgentToBeEstablished(agent);

            // Subset of subscriptions after restart
            agent->SubscribeAll(BgpConfigManager::kMasterInstance, -1);

            for (size_t i = 0; i < gr_test_param.instance_ids.size(); i++) {
                int instance_id = gr_test_param.instance_ids[i];
                if (std::find(instances_to_delete_before_gr_.begin(),
                          instances_to_delete_before_gr_.end(), instance_id) !=
                        instances_to_delete_before_gr_.end())
                    continue;
                if (std::find(instances_to_delete_during_gr_.begin(),
                          instances_to_delete_during_gr_.end(), instance_id) !=
                        instances_to_delete_during_gr_.end())
                    continue;

                string instance_name = "instance" +
                    boost::lexical_cast<string>(instance_id);
                agent->SubscribeAll(instance_name, instance_id);

                // Subset of routes are [re]advertised after restart
                Ip4Prefix prefix(Ip4Prefix::FromString(
                    "10." + boost::lexical_cast<string>(instance_id) + "." +
                    boost::lexical_cast<string>(agent->id()) + ".1/32"));
                int nroutes = gr_test_param.nroutes[i];
                for (int rt = 0; rt < nroutes; rt++,
                    prefix = task_util::Ip4PrefixIncrement(prefix)) {
                    Inet6Prefix inet6_prefix =
                        GetIPv6Prefix(agent->id(), instance_id, rt + 1);
                    agent->AddRoute(instance_name, prefix.ToString(),
                                        GetNextHops(agent, instance_id));
                    agent->AddEnetRoute(instance_name,
                                        GetEnetPrefix(prefix.ToString()),
                                        GetNextHops(agent, instance_id));
                    agent->AddInet6Route(instance_name, inet6_prefix.ToString(),
                                         GetNextHops(agent, instance_id));
                }
                total_routes += nroutes;
            }
        }

        // Bring back half of the flipping agents to established state and send
        // routes. Rest do not come back up (nested closures and LLGR)
        int count = n_flipping_agents.size();
        if (f == flipping_count - 1)
            count /= 2;
        int k = 0;
        BOOST_FOREACH(GRTestParams gr_test_param, n_flipping_agents) {
            if (k++ >= count)
                break;
            test::NetworkAgentMock *agent = gr_test_param.agent;
            WaitForAgentToBeEstablished(agent);

            XmppStateMachineTest::set_skip_tcp_event(
                    gr_test_param.skip_tcp_event);

            agent->SessionDown();
            TASK_UTIL_EXPECT_FALSE(agent->IsEstablished());
            TASK_UTIL_EXPECT_EQ(TcpSession::EVENT_NONE,
                                XmppStateMachineTest::get_skip_tcp_event());
            if (gr_test_param.skip_tcp_event == TcpSession::EVENT_NONE) {
                TASK_UTIL_EXPECT_FALSE(
                        bgp_xmpp_channels_[agent->id()]->Peer()->IsReady());
            }

            for (size_t i = 0; i < gr_test_param.instance_ids.size(); i++) {
                int instance_id = gr_test_param.instance_ids[i];
                if (std::find(instances_to_delete_before_gr_.begin(),
                          instances_to_delete_before_gr_.end(), instance_id) !=
                        instances_to_delete_before_gr_.end())
                    continue;
                if (std::find(instances_to_delete_during_gr_.begin(),
                          instances_to_delete_during_gr_.end(), instance_id) !=
                        instances_to_delete_during_gr_.end())
                    continue;
                int nroutes = gr_test_param.nroutes[i];
                total_routes -= nroutes;
            }
        }
    }

    // Send EoR marker or trigger GR timer for agents which came back up and
    // sent desired routes.
    BOOST_FOREACH(GRTestParams gr_test_param, n_flipping_agents) {
        test::NetworkAgentMock *agent = gr_test_param.agent;
        if (!agent->down())
            WaitForAgentToBeEstablished(agent);

        if (gr_test_param.should_send_eor() && agent->IsEstablished()) {
            agent->SendEorMarker();
        } else {
            PeerCloseManager *pc = bgp_xmpp_channels_[agent->id()]->close_manager();

            // If the session is down and TCP down event was meant to be skipped
            // then we do not expect control-node to be unaware of it. Hold
            // timer must have expired by then. Trigger the hold-timer expiry
            // first in order to bring the peer down in the controller and then
            // call the GR timer callback.
            if (!agent->IsEstablished()) {
                if (gr_test_param.skip_tcp_event != TcpSession::EVENT_NONE) {
                    uint64_t stale = pc->stats().stale;
                    const XmppStateMachine *sm = bgp_xmpp_channels_[
                        agent->id()]->channel()->connection()->state_machine();
                    const_cast<XmppStateMachine *>(sm)->HoldTimerExpired();
                    TASK_UTIL_EXPECT_EQ(stale + 1, pc->stats().stale);
                }
                TASK_UTIL_EXPECT_FALSE(
                        bgp_xmpp_channels_[agent->id()]->Peer()->IsReady());
                TASK_UTIL_EXPECT_EQ(PeerCloseManager::GR_TIMER, pc->state());
            }
            FireGRTimer(bgp_xmpp_channels_[agent->id()]);
        }
    }
    WaitForIdle();
}

void GracefulRestartTest::BgpPeerDown(BgpPeerTest *peer,
                                      TcpSession::Event event) {
    BgpPeerTest *server_peer = bgp_server_peers_[peer->id()];
    StateMachineTest *state_machine =
        dynamic_cast<StateMachineTest *>(server_peer->state_machine());

    if (event != TcpSession::EVENT_NONE) {
        state_machine->set_skip_tcp_event(event);

        // If TCP Close needs to be skipped, then also skip bgp notification
        // messages as they produce the same effect of session termination.
        if (event == TcpSession::CLOSE)
            state_machine->set_skip_bgp_notification_msg(true);
    }

    TASK_UTIL_EXPECT_TRUE(peer->IsReady());
    peer->SetAdminState(true);
    TASK_UTIL_EXPECT_FALSE(peer->IsReady());

    if (event == TcpSession::EVENT_NONE)
        TASK_UTIL_EXPECT_FALSE(server_peer->IsReady());

    // Also delete the routes
    for (int i = 1; i <= n_instances_; i++)
        ProcessVpnRoute(peer, i, n_routes_, false);

    if (event == TcpSession::EVENT_NONE)
        return;

    TASK_UTIL_EXPECT_EQ(TcpSession::EVENT_NONE,
                        state_machine->skip_tcp_event());
    TASK_UTIL_EXPECT_FALSE(state_machine->skip_bgp_notification_msg());
}

void GracefulRestartTest::ProcessFlippingPeers(int &total_routes,
        int remaining_instances, vector<GRTestParams> &n_flipping_peers) {
    int flipping_count = 3;

    for (int f = 0; f < flipping_count; f++) {
        BOOST_FOREACH(GRTestParams gr_test_param, n_flipping_peers) {
            BgpPeerUp(gr_test_param.peer);
        }

        BOOST_FOREACH(GRTestParams gr_test_param, n_flipping_peers) {
            BgpPeerTest *peer = gr_test_param.peer;
            WaitForPeerToBeEstablished(peer);

            for (size_t i = 0; i < gr_test_param.instance_ids.size(); i++) {
                int instance_id = gr_test_param.instance_ids[i];
                if (std::find(instances_to_delete_before_gr_.begin(),
                          instances_to_delete_before_gr_.end(), instance_id) !=
                        instances_to_delete_before_gr_.end())
                    continue;
                if (std::find(instances_to_delete_during_gr_.begin(),
                          instances_to_delete_during_gr_.end(), instance_id) !=
                        instances_to_delete_during_gr_.end())
                    continue;

                int nroutes = gr_test_param.nroutes[i];
                ProcessVpnRoute(peer, instance_id, nroutes, true);
                total_routes += nroutes;
            }
        }

        // Bring back half of the flipping peers to established state and send
        // routes. Rest do not come back up (nested closures and LLGR)
        int count = n_flipping_peers.size();
        if (f == flipping_count - 1)
            count /= 2;
        int k = 0;
        BOOST_FOREACH(GRTestParams gr_test_param, n_flipping_peers) {
            if (k++ >= count)
                break;

            BgpPeerTest *peer = gr_test_param.peer;
            WaitForPeerToBeEstablished(peer);
            BgpPeerDown(peer, gr_test_param.skip_tcp_event);

            // Make sure that session did not flip on one side as tcp down
            // events were meant to be skipped (to simulate cold reboot)
            if (gr_test_param.skip_tcp_event != TcpSession::EVENT_NONE)
                WaitForPeerToBeEstablished(bgp_server_peers_[peer->id()]);

            for (size_t i = 0; i < gr_test_param.instance_ids.size(); i++) {
                int instance_id = gr_test_param.instance_ids[i];
                if (std::find(instances_to_delete_before_gr_.begin(),
                          instances_to_delete_before_gr_.end(), instance_id) !=
                        instances_to_delete_before_gr_.end())
                    continue;
                if (std::find(instances_to_delete_during_gr_.begin(),
                          instances_to_delete_during_gr_.end(), instance_id) !=
                        instances_to_delete_during_gr_.end())
                    continue;
                total_routes -= gr_test_param.nroutes[i];
            }
        }
    }

    // Send EoR marker or trigger GR timer for peers which came back up and
    // sent desired routes.
    BOOST_FOREACH(GRTestParams gr_test_param, n_flipping_peers) {
        BgpPeerTest *peer = gr_test_param.peer;
        if (gr_test_param.should_send_eor() && peer->IsReady()) {
            peer->SendEndOfRIB();
        } else {
            PeerCloseManager *pc =
                bgp_server_peers_[peer->id()]->close_manager();

            // If the session is down and TCP down event was meant to be skipped
            // then we do not expect control-node to be unaware of it. Hold
            // timer must have expired by then. Trigger the hold-timer expiry
            // first in order to bring the peer down in the controller and then
            // call the GR timer callback.
            if (!peer->IsReady()) {
                if (gr_test_param.skip_tcp_event != TcpSession::EVENT_NONE) {
                    uint64_t stale = pc->stats().stale;
                    const StateMachine *sm = bgp_server_peers_[
                        peer->id()]->state_machine();
                    const_cast<StateMachine *>(sm)->HoldTimerExpired();
                    TASK_UTIL_EXPECT_EQ(stale + 1, pc->stats().stale);
                }
                TASK_UTIL_EXPECT_FALSE(bgp_server_peers_[
                                               peer->id()]->IsReady());
                TASK_UTIL_EXPECT_EQ(PeerCloseManager::GR_TIMER, pc->state());
            }
            FireGRTimer(bgp_server_peers_[peer->id()]);
        }
    }
    WaitForIdle();
}

void GracefulRestartTest::GracefulRestartTestRun () {
    int total_routes = n_instances_ * (n_agents_ + n_peers_) * n_routes_;

    //  Verify that n_agents_ * n_instances_ * n_routes_ routes are received in
    //  agent in each instance
    VerifyReceivedXmppRoutes(total_routes);

    vector<test::NetworkAgentMock *> dont_unsubscribe =
        vector<test::NetworkAgentMock *>();

    DeleteRoutingInstances(instances_to_delete_before_gr_, dont_unsubscribe);
    int remaining_instances = n_instances_;
    remaining_instances -= instances_to_delete_before_gr_.size();
    total_routes -= n_routes_ * (n_agents_ + n_peers_) *
                    instances_to_delete_before_gr_.size();

    // Subset of agents go down permanently (Triggered from agents)
    BOOST_FOREACH(test::NetworkAgentMock *agent, n_down_from_agents_) {
        WaitForAgentToBeEstablished(agent);
        agent->SessionDown();
        dont_unsubscribe.push_back(agent);
        TASK_UTIL_EXPECT_FALSE(agent->IsEstablished());
        total_routes -= remaining_instances * n_routes_;
    }

    // Subset of peers go down permanently (Triggered from peers)
    BOOST_FOREACH(BgpPeerTest *peer, n_down_from_peers_) {
        WaitForPeerToBeEstablished(peer);
        BgpPeerDown(peer, TcpSession::EVENT_NONE);
        total_routes -= remaining_instances * n_routes_;
    }

    // Divide flipped agents into two parts. Agents in the first part flip
    // once and come back up (normal GR). Those in the second part keep
    // flipping. Eventually half the second part come back to normal up state.
    // Rest (1/4th overall) remain down triggering LLGR during the whole time.
    vector<GRTestParams> n_flipped_agents = vector<GRTestParams>();
    vector<GRTestParams> n_flipping_agents = vector<GRTestParams>();
    for (size_t i = 0; i < n_flipped_agents_.size(); i++) {
        if (i < n_flipped_agents_.size()/2)
            n_flipped_agents.push_back(n_flipped_agents_[i]);
        else
            n_flipping_agents.push_back(n_flipped_agents_[i]);
    }

    vector<GRTestParams> n_flipped_peers = vector<GRTestParams>();
    vector<GRTestParams> n_flipping_peers = vector<GRTestParams>();
    for (size_t i = 0; i < n_flipped_peers_.size(); i++) {
        if (i < n_flipped_peers_.size()/2)
            n_flipped_peers.push_back(n_flipped_peers_[i]);
        else
            n_flipping_peers.push_back(n_flipped_peers_[i]);
    }

    // Subset of agents flip (Triggered from agents)
    BOOST_FOREACH(GRTestParams gr_test_param, n_flipped_agents) {
        test::NetworkAgentMock *agent = gr_test_param.agent;
        WaitForAgentToBeEstablished(agent);
        XmppStateMachineTest::set_skip_tcp_event(
                gr_test_param.skip_tcp_event);
        agent->SessionDown();
        dont_unsubscribe.push_back(agent);
        TASK_UTIL_EXPECT_FALSE(agent->IsEstablished());
        TASK_UTIL_EXPECT_EQ(TcpSession::EVENT_NONE,
                            XmppStateMachineTest::get_skip_tcp_event());
        total_routes -= remaining_instances * n_routes_;

    }

    // Subset of peers flip (Triggered from peers)
    BOOST_FOREACH(GRTestParams gr_test_param, n_flipped_peers) {
        BgpPeerTest *peer = gr_test_param.peer;
        WaitForPeerToBeEstablished(peer);
        BgpPeerDown(peer, gr_test_param.skip_tcp_event);
        total_routes -= remaining_instances * n_routes_;
    }

    // Subset of agents flip (Triggered from agents)
    BOOST_FOREACH(GRTestParams gr_test_param, n_flipping_agents) {
        test::NetworkAgentMock *agent = gr_test_param.agent;
        WaitForAgentToBeEstablished(agent);
        XmppStateMachineTest::set_skip_tcp_event(gr_test_param.skip_tcp_event);
        agent->SessionDown();
        dont_unsubscribe.push_back(agent);
        TASK_UTIL_EXPECT_FALSE(agent->IsEstablished());
        total_routes -= remaining_instances * n_routes_;
    }

    // Subset of peers flip (Triggered from peers)
    BOOST_FOREACH(GRTestParams gr_test_param, n_flipping_peers) {
        BgpPeerTest *peer = gr_test_param.peer;
        WaitForPeerToBeEstablished(peer);
        BgpPeerDown(peer, gr_test_param.skip_tcp_event);
        total_routes -= remaining_instances * n_routes_;
    }

    // Delete some of the routing-instances when the agent is still down.
    // It is expected that agents upon restart only subscribe to those that
    // were not deleted.
    DeleteRoutingInstances(instances_to_delete_during_gr_, dont_unsubscribe);

    // Account for agents (which do not flip) who usubscribe explicitly
    total_routes -= n_routes_ *
        (n_agents_ - n_flipped_agents.size() - n_flipping_agents.size() -
         n_down_from_agents_.size()) * instances_to_delete_during_gr_.size();

    total_routes -= n_routes_ *
        (n_peers_ - n_flipped_peers.size() - n_flipping_peers.size() -
         n_down_from_peers_.size()) * instances_to_delete_during_gr_.size();

    XmppStateMachineTest::set_skip_tcp_event(TcpSession::EVENT_NONE);

    BOOST_FOREACH(GRTestParams gr_test_param, n_flipped_agents) {
        test::NetworkAgentMock *agent = gr_test_param.agent;
        TASK_UTIL_EXPECT_FALSE(agent->IsEstablished());
        agent->SessionUp();
        WaitForAgentToBeEstablished(agent);
    }

    BOOST_FOREACH(GRTestParams gr_test_param, n_flipped_peers) {
        BgpPeerTest *peer = gr_test_param.peer;
        TASK_UTIL_EXPECT_FALSE(peer->IsReady());
        BgpPeerUp(peer);
    }

    BOOST_FOREACH(GRTestParams gr_test_param, n_flipped_agents) {
        test::NetworkAgentMock *agent = gr_test_param.agent;
        WaitForAgentToBeEstablished(agent);

        // Subset of subscriptions after restart
        agent->SubscribeAll(BgpConfigManager::kMasterInstance, -1);
        for (size_t i = 0; i < gr_test_param.instance_ids.size(); i++) {
            int instance_id = gr_test_param.instance_ids[i];
            if (std::find(instances_to_delete_before_gr_.begin(),
                          instances_to_delete_before_gr_.end(), instance_id) !=
                    instances_to_delete_before_gr_.end())
                continue;
            if (std::find(instances_to_delete_during_gr_.begin(),
                         instances_to_delete_during_gr_.end(), instance_id) !=
                    instances_to_delete_during_gr_.end())
                continue;
            string instance_name = "instance" +
                boost::lexical_cast<string>(instance_id);
            agent->SubscribeAll(instance_name, instance_id);

            // Subset of routes are [re]advertised after restart
            Ip4Prefix prefix(Ip4Prefix::FromString(
                "10." + boost::lexical_cast<string>(instance_id) + "." +
                boost::lexical_cast<string>(agent->id()) + ".1/32"));
            int nroutes = gr_test_param.nroutes[i];
            for (int rt = 0; rt < nroutes; rt++,
                prefix = task_util::Ip4PrefixIncrement(prefix)) {
                Inet6Prefix inet6_prefix =
                    GetIPv6Prefix(agent->id(), instance_id, rt + 1);
                agent->AddRoute(instance_name, prefix.ToString(),
                                    GetNextHops(agent, instance_id));
                agent->AddEnetRoute(instance_name,
                                    GetEnetPrefix(prefix.ToString()),
                                    GetNextHops(agent, instance_id));
                agent->AddInet6Route(instance_name, inet6_prefix.ToString(),
                                     GetNextHops(agent, instance_id));
            }
            total_routes += nroutes;
        }
    }

    BOOST_FOREACH(GRTestParams gr_test_param, n_flipped_peers) {
        BgpPeerTest *peer = gr_test_param.peer;
        WaitForPeerToBeEstablished(peer);

        for (size_t i = 0; i < gr_test_param.instance_ids.size(); i++) {
            int instance_id = gr_test_param.instance_ids[i];
            if (std::find(instances_to_delete_before_gr_.begin(),
                          instances_to_delete_before_gr_.end(), instance_id) !=
                    instances_to_delete_before_gr_.end())
                continue;
            if (std::find(instances_to_delete_during_gr_.begin(),
                          instances_to_delete_during_gr_.end(), instance_id) !=
                    instances_to_delete_during_gr_.end())
                continue;

            // Subset of routes are [re]advertised after restart
            int nroutes = gr_test_param.nroutes[i];
            ProcessVpnRoute(peer, instance_id, nroutes, true);
            total_routes += nroutes;
        }
    }

    // Send EoR marker or trigger GR timer for agents which came back up and
    // sent desired routes.
    BOOST_FOREACH(GRTestParams gr_test_param, n_flipped_agents) {
        test::NetworkAgentMock *agent = gr_test_param.agent;
        if (gr_test_param.should_send_eor())
            agent->SendEorMarker();
        else
            FireGRTimer(bgp_xmpp_channels_[agent->id()]);
    }

    // Send EoR marker or trigger GR timer for peers which came back up and
    // sent desired routes.
    BOOST_FOREACH(GRTestParams gr_test_param, n_flipped_peers) {
        BgpPeerTest *peer = gr_test_param.peer;
        if (gr_test_param.should_send_eor())
            peer->SendEndOfRIB();
        else
            FireGRTimer(bgp_server_peers_[peer->id()]);
    }

    // Process agents which keep flipping and trigger LLGR..
    ProcessFlippingAgents(total_routes, remaining_instances, n_flipping_agents);

    // Process peers which keep flipping and trigger LLGR..
    ProcessFlippingPeers(total_routes, remaining_instances, n_flipping_peers);

    // Trigger GR timer for agents which went down permanently.
    BOOST_FOREACH(test::NetworkAgentMock *agent, n_down_from_agents_) {
        FireGRTimer(bgp_xmpp_channels_[agent->id()]);
    }

    // Trigger GR timer for peers which went down permanently.
    BOOST_FOREACH(BgpPeerTest *peer, n_down_from_peers_) {
        FireGRTimer(bgp_server_peers_[peer->id()]);
    }

    VerifyReceivedXmppRoutes(total_routes);
    VerifyDeletedRoutingInstnaces(instances_to_delete_before_gr_);
    VerifyDeletedRoutingInstnaces(instances_to_delete_during_gr_);
}

#define COMBINE_PARAMS \
    Combine(ValuesIn(GetInstanceParameters()),                      \
            ValuesIn(GetRouteParameters()),                         \
            ValuesIn(GetAgentParameters()),                         \
            ValuesIn(GetPeerParameters()),                          \
            ValuesIn(GetTargetParameters()))                        \


INSTANTIATE_TEST_CASE_P(GracefulRestartTestWithParams, GracefulRestartTest,
                        COMBINE_PARAMS);

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    BgpServerTest::GlobalSetUp();
    BgpServer::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<PeerCloseManager>(
        boost::factory<PeerCloseManagerTest *>());
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
    XmppObjectFactory::Register<XmppStateMachine>(
        boost::factory<XmppStateMachineTest *>());
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
}

static void TearDown() {
    BgpServer::Terminate();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    signal(SIGUSR1, SignalHandler);
    gargc = argc;
    gargv = argv;

    bgp_log_test::init();
    ::testing::InitGoogleTest(&gargc, gargv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
