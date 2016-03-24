/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>
#include <list>

#include "base/test/addr_test_util.h"

#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "bgp/xmpp_message_builder.h"

#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
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

static int d_instances = 4;
static int d_routes = 4;
static int d_agents = 4;
static int d_peers = 4;
static int d_targets = 1;

static string d_log_category_ = "";
static string d_log_level_ = "SYS_WARN";
static bool d_log_local_enable_ = false;
static bool d_log_trace_enable_ = false;
static bool d_log_enable_ = false;

static vector<int>  n_instances = boost::assign::list_of(d_instances);
static vector<int>  n_routes    = boost::assign::list_of(d_routes);
static vector<int>  n_agents    = boost::assign::list_of(d_agents);
static vector<int>  n_peers     = boost::assign::list_of(d_peers);
static vector<int>  n_targets   = boost::assign::list_of(d_targets);

static char **gargv;
static int    gargc;
static int    n_db_walker_wait_usecs = 0;

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
        ("log-category", value<string>()->default_value(d_log_category_),
            "set log category")
        ("log-disable", bool_switch(&d_log_enable_),
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

    if (!d_log_enable_) {
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
                d_log_enable_, d_log_level_, module_name, d_log_level_);
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
    explicit PeerCloseManagerTest(IPeer *peer) : PeerCloseManager(peer) { }
    ~PeerCloseManagerTest() { }
    void StartStaleTimer() { }
};

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

class GracefulRestartTest : public ::testing::TestWithParam<TestParams> {

public:
    bool IsPeerCloseGraceful(bool graceful) { return graceful; }
    void SetPeerCloseGraceful(bool graceful) {
        xmpp_server_->GetIsPeerCloseGraceful_fnc_ =
                    boost::bind(&GracefulRestartTest::IsPeerCloseGraceful, this,
                                graceful);
        server_->GetIsPeerCloseGraceful_fnc_ =
                    boost::bind(&GracefulRestartTest::IsPeerCloseGraceful, this,
                                graceful);
    }

protected:
    GracefulRestartTest() : thread_(&evm_) { }

    virtual void SetUp();
    virtual void TearDown();
    void AgentCleanup();
    void Configure();
    void PeerUp(BgpPeerTest *peer);
    void PeerDown(BgpPeerTest *peer);

    XmppChannelConfig *CreateXmppChannelCfg(const char *address, int port,
                                            const string &from,
                                            const string &to,
                                            bool isClient);

    void GracefulRestartTestStart();
    void GracefulRestartTestRun();
    string GetConfig(bool delete_config);
    ExtCommunitySpec *CreateRouteTargets();
    void AddRoutes();
    void CreateAgents();
    void Subscribe();
    void UnSubscribe();
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
    void CallStaleTimer(BgpXmppChannel *channel);
    void CallStaleTimer(BgpPeerTest *peer);
    void InitParams();
    void VerifyRoutes(int count);
    bool IsReady(bool ready);
    void WaitForAgentToBeEstablished(test::NetworkAgentMock *agent);
    void WaitForPeerToBeEstablished( BgpPeerTest *peer);
    void BgpPeersAdminUpOrDown(bool down);

    EventManager evm_;
    ServerThread thread_;
    boost::scoped_ptr<BgpServerTest> server_;
    XmppServerTest *xmpp_server_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> channel_manager_;
    scoped_ptr<BgpInstanceConfigTest> master_cfg_;
    RoutingInstance *master_instance_;
    std::vector<test::NetworkAgentMock *> xmpp_agents_;
    std::vector<BgpPeerTest *> bgp_peers_;
    std::vector<BgpServerTest *> bgp_servers_;
    std::vector<BgpXmppChannel *> bgp_xmpp_channels_;
    std::vector<BgpPeerTest *> bgp_server_peers_;
    int n_families_;
    std::vector<Address::Family> familes_;
    int n_instances_;
    int n_routes_;
    int n_agents_;
    int n_peers_;
    int n_targets_;

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
    void ProcessFlippingPeers(int &total_routes, int remaining_instances,
        vector<GRTestParams> &n_flipping_peers);

    std::vector<GRTestParams> n_flipped_agents_;
    std::vector<GRTestParams> n_flipped_peers_;
    std::vector<test::NetworkAgentMock *> n_down_from_agents_;
    std::vector<BgpPeerTest *> n_down_from_peers_;
    std::vector<int> instances_to_delete_before_gr_;
    std::vector<int> instances_to_delete_during_gr_;
};

void GracefulRestartTest::SetUp() {
    InitParams();

    server_.reset(new BgpServerTest(&evm_, "RTR0"));
    bgp_servers_.push_back(server_.get());

    for (int i = 1; i <= n_peers_; i++) {
        bgp_servers_.push_back(
            new BgpServerTest(&evm_, "RTR" + boost::lexical_cast<string>(i)));
    }

    xmpp_server_ = new XmppServerTest(&evm_, XMPP_CONTROL_SERV);
    channel_manager_.reset(new BgpXmppChannelManagerMock(
                                   xmpp_server_, server_.get()));
    master_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
        BgpConfigManager::kMasterInstance, "", ""));
    master_instance_ = static_cast<RoutingInstance *>(
        server_->routing_instance_mgr()->GetRoutingInstance(
            BgpConfigManager::kMasterInstance));
    n_families_ = 2;
    familes_.push_back(Address::INET);
    familes_.push_back(Address::INETVPN);

    for (int i = 0; i <= n_peers_; i++)
        bgp_servers_[i]->session_manager()->Initialize(0);
    xmpp_server_->Initialize(0, false);
    thread_.Start();
}

void GracefulRestartTest::TearDown() {
    task_util::WaitForIdle();
    SetPeerCloseGraceful(false);
    XmppAgentClose();
    xmpp_server_->Shutdown();
    task_util::WaitForIdle();

    VerifyRoutes(0);
    VerifyReceivedXmppRoutes(0);

    if (n_agents_) {
        TASK_UTIL_EXPECT_EQ(0, xmpp_server_->connection_map().size());
    }
    AgentCleanup();
    TASK_UTIL_EXPECT_EQ(0, channel_manager_->channel_map().size());
    channel_manager_.reset();
    task_util::WaitForIdle();

    TcpServerManager::DeleteServer(xmpp_server_);
    xmpp_server_ = NULL;

    for (int i = 1; i <= n_instances_; i++) {
        BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
            ProcessVpnRoute(peer, i, n_routes_, false);
        }
    }

    for (int i = 0; i <= n_peers_; i++)
        bgp_servers_[i]->Shutdown();

    task_util::WaitForIdle();
    evm_.Shutdown();
    thread_.Join();
    task_util::WaitForIdle();

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        delete agent;
    }
}

void GracefulRestartTest::Configure() {
    string config = GetConfig(false);
    for (int i = 0; i <= n_peers_; i++)
        bgp_servers_[i]->Configure(config.c_str());
    task_util::WaitForIdle();
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
        bgp_server_peers_.push_back(peer);
    }
}

XmppChannelConfig *GracefulRestartTest::CreateXmppChannelCfg(
        const char *address, int port, const string &from, const string &to,
        bool isClient) {
    XmppChannelConfig *cfg = new XmppChannelConfig(isClient);
    boost::system::error_code ec;
    cfg->endpoint.address(ip::address::from_string(address, ec));
    cfg->endpoint.port(port);
    cfg->ToAddr = to;
    cfg->FromAddr = from;
    if (!isClient) cfg->NodeAddr = PUBSUB_NODE_ADDR;
    return cfg;
}

void GracefulRestartTest::AgentCleanup() {
    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        agent->Delete();
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

    for (int i = 0; i <= n_peers_; i++) {
        out << "<bgp-router name=\'RTR" << i << "\'>\
                    <identifier>192.168.0." << i << "</identifier>\
                    <address>127.0.0.1</address>\
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
    task_util::WaitForIdle();
}

bool GracefulRestartTest::IsReady(bool ready) {
    return ready;
}

void GracefulRestartTest::WaitForAgentToBeEstablished(
        test::NetworkAgentMock *agent) {
    TASK_UTIL_EXPECT_TRUE(agent->IsChannelReady());
    TASK_UTIL_EXPECT_TRUE(agent->IsEstablished());
}

void GracefulRestartTest::WaitForPeerToBeEstablished(BgpPeerTest *peer) {
    TASK_UTIL_EXPECT_TRUE(peer->IsReady());
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
    task_util::WaitForIdle();
    VerifyReceivedXmppRoutes(n_instances_ * (n_agents_ + n_peers_) * n_routes_);
}

void GracefulRestartTest::CreateAgents() {
    Ip4Prefix prefix(Ip4Prefix::FromString("127.0.0.1/32"));

    for (int i = 0; i < n_agents_; i++) {

        // create an XMPP client in server A
        test::NetworkAgentMock *agent = new test::NetworkAgentMock(&evm_,
            "agent" + boost::lexical_cast<string>(i) +
                "@vnsw.contrailsystems.com",
            xmpp_server_->GetPort(),
            prefix.ip4_addr().to_string());
        agent->set_id(i);
        xmpp_agents_.push_back(agent);
        task_util::WaitForIdle();

        TASK_UTIL_EXPECT_NE_MSG(static_cast<BgpXmppChannel *>(NULL),
                          channel_manager_->channel_,
                          "Waiting for channel_manager_->channel_ to be set");
        bgp_xmpp_channels_.push_back(channel_manager_->channel_);
        channel_manager_->channel_ = NULL;

        prefix = task_util::Ip4PrefixIncrement(prefix);
    }
}

void GracefulRestartTest::Subscribe() {

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        agent->Subscribe(BgpConfigManager::kMasterInstance, -1);
        for (int i = 1; i <= n_instances_; i++) {
            string instance_name = "instance" + boost::lexical_cast<string>(i);
            agent->Subscribe(instance_name, i);
        }
    }
    task_util::WaitForIdle();
}

void GracefulRestartTest::UnSubscribe() {

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        agent->Unsubscribe(BgpConfigManager::kMasterInstance);
        for (int i = 1; i <= n_instances_; i++) {
            string instance_name = "instance" + boost::lexical_cast<string>(i);
            agent->Unsubscribe(instance_name);
        }
    }
    VerifyReceivedXmppRoutes(0);
    task_util::WaitForIdle();
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
    task_util::WaitForIdle();
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
                if (add) {
                    agent->AddRoute(instance_name, prefix.ToString(),
                                    GetNextHops(agent, i));
                } else {
                    agent->DeleteRoute(instance_name, prefix.ToString());
                }
            }
        }
    }
    task_util::WaitForIdle();
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
                                    "Wait for routes in " + instance_name);
        }
    }
    task_util::WaitForIdle();
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
    task_util::WaitForIdle();

    // Unsubscribe from all agents who have subscribed
    BOOST_FOREACH(int i, instances) {
        string instance_name = "instance" + boost::lexical_cast<string>(i);
        BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
            if (!agent->IsEstablished() || !agent->HasSubscribed(instance_name))
                continue;
            if (std::find(dont_unsubscribe.begin(), dont_unsubscribe.end(),
                          agent) == dont_unsubscribe.end())
                agent->Unsubscribe(instance_name);
        }

        BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
            if (!peer->IsReady())
                continue;
            ProcessVpnRoute(peer, i, n_routes_, false);
        }
    }
    task_util::WaitForIdle();
}

void GracefulRestartTest::VerifyDeletedRoutingInstnaces(vector<int> instances) {
    BOOST_FOREACH(int i, instances) {
        string instance_name = "instance" + boost::lexical_cast<string>(i);
        TASK_UTIL_EXPECT_EQ(static_cast<RoutingInstance *>(NULL),
                            server_->routing_instance_mgr()->\
                                GetRoutingInstance(instance_name));
    }
    task_util::WaitForIdle();
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

// Invoke stale timer callbacks directly as evm is not running in this unit test
void GracefulRestartTest::CallStaleTimer(BgpXmppChannel *channel) {
    channel->Peer()->peer_close()->close_manager()->RestartTimerCallback();
    task_util::WaitForIdle();
}

// Invoke stale timer callbacks directly as evm is not running in this unit test
void GracefulRestartTest::CallStaleTimer(BgpPeerTest *peer) {
    peer->peer_close()->close_manager()->RestartTimerCallback();
    task_util::WaitForIdle();
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
    SetPeerCloseGraceful(false);
    Configure();

    //  Bring up n_agents_ in n_instances_ and advertise n_routes_ per session
    AddRoutes();
    VerifyRoutes(n_routes_);
}

void GracefulRestartTest::PeerUp(BgpPeerTest *peer) {
    TASK_UTIL_EXPECT_FALSE(peer->IsReady());
    peer->SetAdminState(false);
    TASK_UTIL_EXPECT_TRUE(peer->IsReady());
}

void GracefulRestartTest::PeerDown(BgpPeerTest *peer) {
    TASK_UTIL_EXPECT_TRUE(peer->IsReady());
    peer->SetAdminState(true);
    TASK_UTIL_EXPECT_FALSE(peer->IsReady());

    // Also delete the routes
    for (int i = 1; i <= n_instances_; i++)
        ProcessVpnRoute(peer, i, n_routes_, false);
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
            agent->Subscribe(BgpConfigManager::kMasterInstance, -1);

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
                agent->Subscribe(instance_name, instance_id);

                // Subset of routes are [re]advertised after restart
                Ip4Prefix prefix(Ip4Prefix::FromString(
                    "10." + boost::lexical_cast<string>(instance_id) + "." +
                    boost::lexical_cast<string>(agent->id()) + ".1/32"));
                int nroutes = gr_test_param.nroutes[i];
                for (int rt = 0; rt < nroutes; rt++,
                    prefix = task_util::Ip4PrefixIncrement(prefix)) {
                    agent->AddRoute(instance_name, prefix.ToString(),
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
        if (gr_test_param.send_eor && agent->IsEstablished()) {
            agent->SendEorMarker();
        } else {
            PeerCloseManager *pc =
                bgp_xmpp_channels_[agent->id()]->Peer()->peer_close()->close_manager();

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
            CallStaleTimer(bgp_xmpp_channels_[agent->id()]);
        }
    }
    task_util::WaitForIdle();
}

void GracefulRestartTest::ProcessFlippingPeers(int &total_routes,
        int remaining_instances, vector<GRTestParams> &n_flipping_peers) {
    int flipping_count = 3;

    for (int f = 0; f < flipping_count; f++) {
        BOOST_FOREACH(GRTestParams gr_test_param, n_flipping_peers) {
            PeerUp(gr_test_param.peer);
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
            StateMachineTest::set_skip_tcp_event(gr_test_param.skip_tcp_event);

            PeerDown(peer);
            TASK_UTIL_EXPECT_EQ(TcpSession::EVENT_NONE,
                                StateMachineTest::get_skip_tcp_event());

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
        if (gr_test_param.send_eor && peer->IsReady()) {
            peer->SendEorMarker();
        } else {
            PeerCloseManager *pc =
                bgp_server_peers_[peer->id()]->peer_close()->close_manager();

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
            CallStaleTimer(bgp_server_peers_[peer->id()]);
        }
    }
    task_util::WaitForIdle();
}

void GracefulRestartTest::GracefulRestartTestRun () {
    int total_routes = n_instances_ * (n_agents_ + n_peers_) * n_routes_;

    //  Verify that n_agents_ * n_instances_ * n_routes_ routes are received in
    //  agent in each instance
    VerifyReceivedXmppRoutes(total_routes);

    // TODO Only a subset of agents support GR
    // BOOST_FOREACH(test::NetworkAgentMock *agent, n_gr_supported_agents)
        SetPeerCloseGraceful(true);


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
        TASK_UTIL_EXPECT_FALSE(agent->IsEstablished());
        total_routes -= remaining_instances * n_routes_;
    }

    // Subset of peers go down permanently (Triggered from peers)
    BOOST_FOREACH(BgpPeerTest *peer, n_down_from_peers_) {
        WaitForPeerToBeEstablished(peer);
        PeerDown(peer);
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
        StateMachineTest::set_skip_tcp_event(gr_test_param.skip_tcp_event);
        PeerDown(peer);
        TASK_UTIL_EXPECT_EQ(TcpSession::EVENT_NONE,
                            StateMachineTest::get_skip_tcp_event());
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
        StateMachineTest::set_skip_tcp_event(gr_test_param.skip_tcp_event);
        PeerDown(peer);
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
    StateMachineTest::set_skip_tcp_event(TcpSession::EVENT_NONE);

    BOOST_FOREACH(GRTestParams gr_test_param, n_flipped_agents) {
        test::NetworkAgentMock *agent = gr_test_param.agent;
        TASK_UTIL_EXPECT_FALSE(agent->IsEstablished());
        agent->SessionUp();
        WaitForAgentToBeEstablished(agent);
    }

    BOOST_FOREACH(GRTestParams gr_test_param, n_flipped_peers) {
        BgpPeerTest *peer = gr_test_param.peer;
        TASK_UTIL_EXPECT_FALSE(peer->IsReady());
        PeerUp(peer);
    }

    BOOST_FOREACH(GRTestParams gr_test_param, n_flipped_agents) {
        test::NetworkAgentMock *agent = gr_test_param.agent;
        WaitForAgentToBeEstablished(agent);

        // Subset of subscriptions after restart
        agent->Subscribe(BgpConfigManager::kMasterInstance, -1);
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
            agent->Subscribe(instance_name, instance_id);

            // Subset of routes are [re]advertised after restart
            Ip4Prefix prefix(Ip4Prefix::FromString(
                "10." + boost::lexical_cast<string>(instance_id) + "." +
                boost::lexical_cast<string>(agent->id()) + ".1/32"));
            int nroutes = gr_test_param.nroutes[i];
            for (int rt = 0; rt < nroutes; rt++,
                prefix = task_util::Ip4PrefixIncrement(prefix)) {
                agent->AddRoute(instance_name, prefix.ToString(),
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
        if (gr_test_param.send_eor)
            agent->SendEorMarker();
        else
            CallStaleTimer(bgp_xmpp_channels_[agent->id()]);
    }

    // Send EoR marker or trigger GR timer for peers which came back up and
    // sent desired routes.
    BOOST_FOREACH(GRTestParams gr_test_param, n_flipped_peers) {
        BgpPeerTest *peer = gr_test_param.peer;
        if (false && gr_test_param.send_eor)
            peer->SendEorMarker();
        else
            CallStaleTimer(bgp_server_peers_[peer->id()]);
    }

    // Process agents which keep flipping and trigger LLGR..
    ProcessFlippingAgents(total_routes, remaining_instances, n_flipping_agents);

    // Process peers which keep flipping and trigger LLGR..
    ProcessFlippingPeers(total_routes, remaining_instances, n_flipping_peers);

    // Trigger GR timer for agents which went down permanently.
    BOOST_FOREACH(test::NetworkAgentMock *agent, n_down_from_agents_) {
        CallStaleTimer(bgp_xmpp_channels_[agent->id()]);
    }

    // Trigger GR timer for peers which went down permanently.
    BOOST_FOREACH(BgpPeerTest *peer, n_down_from_peers_) {
        CallStaleTimer(bgp_server_peers_[peer->id()]);
    }

    VerifyReceivedXmppRoutes(total_routes);
    VerifyDeletedRoutingInstnaces(instances_to_delete_before_gr_);
    VerifyDeletedRoutingInstnaces(instances_to_delete_during_gr_);
}

// None of the agents goes down or flip
TEST_P(GracefulRestartTest, GracefulRestart_Down_1) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();
    GracefulRestartTestRun();
}

// All agents go down permanently
TEST_P(GracefulRestartTest, GracefulRestart_Down_2) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    n_down_from_agents_ = xmpp_agents_;
    n_down_from_peers_ = bgp_peers_;
    GracefulRestartTestRun();
}

// Some agents go down permanently
TEST_P(GracefulRestartTest, GracefulRestart_Down_3) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size()/2; i++)
        n_down_from_agents_.push_back(xmpp_agents_[i]);
    for (size_t i = 0; i < bgp_peers_.size()/2; i++)
        n_down_from_peers_.push_back(bgp_peers_[i]);
    GracefulRestartTestRun();
}

// Some agents go down permanently and some flip (which sends no routes)
TEST_P(GracefulRestartTest, GracefulRestart_Down_4) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size(); i++) {
        if (i <= xmpp_agents_.size()/2)
            n_down_from_agents_.push_back(xmpp_agents_[i]);
        else
            n_flipped_agents_.push_back(GRTestParams(xmpp_agents_[i]));
    }

    for (size_t i = 0; i < bgp_peers_.size(); i++) {
        if (i <= bgp_peers_.size()/2)
            n_down_from_peers_.push_back(bgp_peers_[i]);
        else
            n_flipped_peers_.push_back(GRTestParams(bgp_peers_[i]));
    }
    GracefulRestartTestRun();
}

// All agents come back up but do not subscribe to any instance
TEST_P(GracefulRestartTest, GracefulRestart_Flap_1) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        n_flipped_agents_.push_back(GRTestParams(agent));
    }
    BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
        n_flipped_peers_.push_back(GRTestParams(peer));
    }
    GracefulRestartTestRun();
}

// All agents come back up and subscribe to all instances and sends all routes
// Agent session tcp down event is detected at the server
TEST_P(GracefulRestartTest, GracefulRestart_Flap_2) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int i = 1; i <= n_instances_; i++) {
            instance_ids.push_back(i);
            nroutes.push_back(n_routes_);
        }

        // Trigger the case of compute-node hard reset where in tcp fin event
        // never reaches control-node
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
    }

    BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int i = 1; i <= n_instances_; i++) {
            instance_ids.push_back(i);
            nroutes.push_back(n_routes_);
        }

        // Trigger the case of compute-node hard reset where in tcp fin event
        // never reaches control-node
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));
    }
    GracefulRestartTestRun();
}

// All agents come back up and subscribe to all instances but sends no routes
// Agent session tcp down event is detected at the server
TEST_P(GracefulRestartTest, GracefulRestart_Flap_3) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int i = 1; i <= n_instances_; i++) {
            instance_ids.push_back(i);
            nroutes.push_back(0);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
    }

    BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int i = 1; i <= n_instances_; i++) {
            instance_ids.push_back(i);
            nroutes.push_back(0);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));
    }
    GracefulRestartTestRun();
}

// All agents come back up and subscribe to all instances and sends some routes
// Agent session tcp down event is detected at the server
TEST_P(GracefulRestartTest, GracefulRestart_Flap_4) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int i = 1; i <= n_instances_; i++) {
            instance_ids.push_back(i);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
    }

    BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int i = 1; i <= n_instances_; i++) {
            instance_ids.push_back(i);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));
    }
    GracefulRestartTestRun();
}

// All agents come back up and subscribe to all instances and sends all routes
// Agent session tcp down event is not detected at the server
TEST_P(GracefulRestartTest, GracefulRestart_Flap_5) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int i = 1; i <= n_instances_; i++) {
            instance_ids.push_back(i);
            nroutes.push_back(n_routes_);
        }

        // Trigger the case of compute-node hard reset where in tcp fin event
        // never reaches control-node
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes,
                                                    TcpSession::CLOSE));
    }

    BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int i = 1; i <= n_instances_; i++) {
            instance_ids.push_back(i);
            nroutes.push_back(n_routes_);
        }

        // Trigger the case of compute-node hard reset where in tcp fin event
        // never reaches control-node
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes,
                                                TcpSession::CLOSE));
    }
    GracefulRestartTestRun();
}

// All agents come back up and subscribe to all instances but sends no routes
// Agent session tcp down event is not detected at the server
TEST_P(GracefulRestartTest, GracefulRestart_Flap_6) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int i = 1; i <= n_instances_; i++) {
            instance_ids.push_back(i);
            nroutes.push_back(0);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes,
                                                    TcpSession::CLOSE));
    }

    BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int i = 1; i <= n_instances_; i++) {
            instance_ids.push_back(i);
            nroutes.push_back(0);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes,
                                                TcpSession::CLOSE));
    }
    GracefulRestartTestRun();
}

// All agents come back up and subscribe to all instances and sends some routes
// Agent session tcp down event is not detected at the server
TEST_P(GracefulRestartTest, GracefulRestart_Flap_7) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int i = 1; i <= n_instances_; i++) {
            instance_ids.push_back(i);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes,
                                                    TcpSession::CLOSE));
    }

    BOOST_FOREACH(BgpPeerTest *peer, bgp_peers_) {
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int i = 1; i <= n_instances_; i++) {
            instance_ids.push_back(i);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes,
                                                TcpSession::CLOSE));
    }
    GracefulRestartTestRun();
}


// Some agents come back up but do not subscribe to any instance
TEST_P(GracefulRestartTest, GracefulRestart_Flap_Some_1) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size()/2; i++) {
        test::NetworkAgentMock *agent = xmpp_agents_[i];
        n_flipped_agents_.push_back(GRTestParams(agent));
    }

    for (size_t i = 0; i < bgp_peers_.size()/2; i++) {
        BgpPeerTest *peer = bgp_peers_[i];
        n_flipped_peers_.push_back(GRTestParams(peer));
    }
    GracefulRestartTestRun();
}

// Some agents come back up and subscribe to all instances and sends all routes
TEST_P(GracefulRestartTest, GracefulRestart_Flap_Some_2) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size()/2; i++) {
        test::NetworkAgentMock *agent = xmpp_agents_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
        // All flipped agents send EoR.
        n_flipped_agents_[i].send_eor = true;
    }

    for (size_t i = 0; i < bgp_peers_.size()/2; i++) {
        BgpPeerTest *peer = bgp_peers_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));

        // All flipped peers send EoR.
        n_flipped_peers_[i].send_eor = true;
    }
    GracefulRestartTestRun();
}

// Some agents come back up and subscribe to all instances and sends all routes
TEST_P(GracefulRestartTest, GracefulRestart_Flap_Some_2_2) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size()/2; i++) {
        test::NetworkAgentMock *agent = xmpp_agents_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
        // None of the flipped agents sends EoR.
        n_flipped_agents_[i].send_eor = false;
    }

    for (size_t i = 0; i < bgp_peers_.size()/2; i++) {
        BgpPeerTest *peer = bgp_peers_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));

        // None of the flipped peers sends EoR.
        n_flipped_peers_[i].send_eor = false;
    }
    GracefulRestartTestRun();
}

// Some agents come back up and subscribe to all instances and sends all routes
TEST_P(GracefulRestartTest, GracefulRestart_Flap_Some_2_3) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size()/2; i++) {
        test::NetworkAgentMock *agent = xmpp_agents_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
        // Only even flipped agents send EoR.
        n_flipped_agents_[i].send_eor = ((i%2) == 0);
    }

    for (size_t i = 0; i < bgp_peers_.size()/2; i++) {
        BgpPeerTest *peer = bgp_peers_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));

        // Only even flipped peers send EoR.
        n_flipped_peers_[i].send_eor = ((i%2) == 0);
    }
    GracefulRestartTestRun();
}

// Some agents come back up and subscribe to all instances but sends no routes
TEST_P(GracefulRestartTest, GracefulRestart_Flap_Some_3) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size()/2; i++) {
        test::NetworkAgentMock *agent = xmpp_agents_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(0);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
        // All flipped agents send EoR.
        n_flipped_agents_[i].send_eor = true;
    }

    for (size_t i = 0; i < bgp_peers_.size()/2; i++) {
        BgpPeerTest *peer = bgp_peers_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(0);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));

        // All flipped peers send EoR.
        n_flipped_peers_[i].send_eor = true;
    }
    GracefulRestartTestRun();
}

// Some agents come back up and subscribe to all instances but sends no routes
TEST_P(GracefulRestartTest, GracefulRestart_Flap_Some_3_2) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size()/2; i++) {
        test::NetworkAgentMock *agent = xmpp_agents_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(0);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
        // None of the flipped agents sends EoR.
        n_flipped_agents_[i].send_eor = false;
    }

    for (size_t i = 0; i < bgp_peers_.size()/2; i++) {
        BgpPeerTest *peer = bgp_peers_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(0);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));

        // None of the flipped peers sends EoR.
        n_flipped_peers_[i].send_eor = false;
    }
    GracefulRestartTestRun();
}

// Some agents come back up and subscribe to all instances but sends no routes
TEST_P(GracefulRestartTest, GracefulRestart_Flap_Some_3_3) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size()/2; i++) {
        test::NetworkAgentMock *agent = xmpp_agents_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(0);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
        // Only even flipped agents send EoR.
        n_flipped_agents_[i].send_eor = ((i%2) == 0);
    }

    for (size_t i = 0; i < bgp_peers_.size()/2; i++) {
        BgpPeerTest *peer = bgp_peers_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(0);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));

        // Only even flipped peers send EoR.
        n_flipped_peers_[i].send_eor = ((i%2) == 0);
    }
    GracefulRestartTestRun();
}

// Some agents come back up and subscribe to all instances and sends some routes
TEST_P(GracefulRestartTest, GracefulRestart_Flap_Some_4) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size()/2; i++) {
        test::NetworkAgentMock *agent = xmpp_agents_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids, nroutes));

        // All flipped agents send EoR.
        n_flipped_agents_[i].send_eor = true;
    }

    for (size_t i = 0; i < bgp_peers_.size()/2; i++) {
        BgpPeerTest *peer = bgp_peers_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));

        // All flipped peers send EoR.
        n_flipped_peers_[i].send_eor = true;
    }
    GracefulRestartTestRun();
}

// Some agents come back up and subscribe to all instances and sends some routes
TEST_P(GracefulRestartTest, GracefulRestart_Flap_Some_4_2) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size()/2; i++) {
        test::NetworkAgentMock *agent = xmpp_agents_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));

        // None of the flipped agents sends EoR.
        n_flipped_agents_[i].send_eor = false;
    }

    for (size_t i = 0; i < bgp_peers_.size()/2; i++) {
        BgpPeerTest *peer = bgp_peers_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));

        // None of the flipped peers sends EoR.
        n_flipped_peers_[i].send_eor = false;
    }
    GracefulRestartTestRun();
}

// Some agents come back up and subscribe to all instances and sends some routes
TEST_P(GracefulRestartTest, GracefulRestart_Flap_Some_4_3) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (size_t i = 0; i < xmpp_agents_.size()/2; i++) {
        test::NetworkAgentMock *agent = xmpp_agents_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));

        // Only even flipped agents send EoR.
        n_flipped_agents_[i].send_eor = ((i%2) == 0);
    }

    for (size_t i = 0; i < bgp_peers_.size()/2; i++) {
        BgpPeerTest *peer = bgp_peers_[i];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));

        // Only even flipped peers send EoR.
        n_flipped_peers_[i].send_eor = ((i%2) == 0);
    }
    GracefulRestartTestRun();
}

// Some routing instances are first deleted. Subscribed agents remain up and
// running.. This is the common case which happens most of the time during
// normal functioning of the software.
TEST_P(GracefulRestartTest, GracefulRestart_Delete_RoutingInstances_1) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (int i = 1; i <= n_instances_/4; i++)
        instances_to_delete_before_gr_.push_back(i);
    GracefulRestartTestRun();
}

// Some routing instances are deleted. Then some of the agents permanently go
// down and they do not come back up (GR is triggered and should get cleaned up
// when the GR timer fires)
TEST_P(GracefulRestartTest, GracefulRestart_Delete_RoutingInstances_2) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (int i = 1; i <= n_instances_/4; i++)
        instances_to_delete_before_gr_.push_back(i);

    for (size_t i = 1; i <= xmpp_agents_.size(); i++) {

        // agents from 2nd half remain up through out this test
        if (i > xmpp_agents_.size()/2)
            continue;

        // agents from 1st quarter go down permantently
        if (i <= xmpp_agents_.size()/4) {
            n_down_from_agents_.push_back(xmpp_agents_[i-1]);
            continue;
        }
    }

    for (size_t i = 1; i <= bgp_peers_.size(); i++) {

        // peers from 2nd half remain up through out this test
        if (i > bgp_peers_.size()/2)
            continue;

        // peers from 1st quarter go down permantently
        if (i <= bgp_peers_.size()/4) {
            n_down_from_peers_.push_back(bgp_peers_[i-1]);
            continue;
        }
    }
    GracefulRestartTestRun();
}

// Some routing instances are deleted. Then some of the agents permanently go
// down and they do not come back up (GR is triggered and should get cleaned up
// when the GR timer fires). During this GR, additional instances are deleted
TEST_P(GracefulRestartTest, GracefulRestart_Delete_RoutingInstances_3) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (int i = 1; i <= n_instances_/4; i++)
        instances_to_delete_before_gr_.push_back(i);

    for (size_t i = 1; i <= xmpp_agents_.size(); i++) {

        // agents from 2nd half remain up through out this test
        if (i > xmpp_agents_.size()/2)
            continue;

        // agents from 1st quarter go down permantently
        if (i <= xmpp_agents_.size()/4) {
            n_down_from_agents_.push_back(xmpp_agents_[i-1]);
            continue;
        }
    }

    for (size_t i = 1; i <= bgp_peers_.size(); i++) {

        // peers from 2nd half remain up through out this test
        if (i > bgp_peers_.size()/2)
            continue;

        // peers from 1st quarter go down permantently
        if (i <= bgp_peers_.size()/4) {
            n_down_from_peers_.push_back(bgp_peers_[i-1]);
            continue;
        }
    }
    for (int i = n_instances_/4 + 1; i <= n_instances_/2; i++)
        instances_to_delete_during_gr_.push_back(i);
    GracefulRestartTestRun();
}

// Some routing instances are deleted. Then some of the agents permanently go
// down and they do not come back up (GR is triggered and should get cleaned up
// when the GR timer fires). Some of the other agents go down and then come
// back up advertising "all" the routes again.
TEST_P(GracefulRestartTest, GracefulRestart_Delete_RoutingInstances_4) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (int i = 1; i <= n_instances_/4; i++)
        instances_to_delete_before_gr_.push_back(i);

    for (size_t i = 1; i <= xmpp_agents_.size(); i++) {

        // agents from 2nd half remain up through out this test
        if (i > xmpp_agents_.size()/2)
            continue;

        // agents from 1st quarter go down permantently
        if (i <= xmpp_agents_.size()/4) {
            n_down_from_agents_.push_back(xmpp_agents_[i-1]);
            continue;
        }

        // agents from 2nd quarter flip with gr
        test::NetworkAgentMock *agent = xmpp_agents_[i-1];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
    }

    for (size_t i = 1; i <= bgp_peers_.size(); i++) {

        // peers from 2nd half remain up through out this test
        if (i > bgp_peers_.size()/2)
            continue;

        // peers from 1st quarter go down permantently
        if (i <= bgp_peers_.size()/4) {
            n_down_from_peers_.push_back(bgp_peers_[i-1]);
            continue;
        }

        // peers from 2nd quarter flip with gr
        BgpPeerTest *peer = bgp_peers_[i-1];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));
    }
    GracefulRestartTestRun();
}

// Some routing instances are deleted. Then some of the agents permanently go
// down and they do not come back up (GR is triggered and should get cleaned up
// when the GR timer fires). Some of the other agents go down and then come
// back up advertising "all" the routes again. During this GR, additional
// instances are deleted
TEST_P(GracefulRestartTest, GracefulRestart_Delete_RoutingInstances_5) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (int i = 1; i <= n_instances_/4; i++)
        instances_to_delete_before_gr_.push_back(i);

    for (size_t i = 1; i <= xmpp_agents_.size(); i++) {

        // agents from 2nd half remain up through out this test
        if (i > xmpp_agents_.size()/2)
            continue;

        // agents from 1st quarter go down permantently
        if (i <= xmpp_agents_.size()/4) {
            n_down_from_agents_.push_back(xmpp_agents_[i-1]);
            continue;
        }

        // agents from 2nd quarter flip with gr
        test::NetworkAgentMock *agent = xmpp_agents_[i-1];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
    }

    for (size_t i = 1; i <= bgp_peers_.size(); i++) {

        // peers from 2nd half remain up through out this test
        if (i > bgp_peers_.size()/2)
            continue;

        // peers from 1st quarter go down permantently
        if (i <= bgp_peers_.size()/4) {
            n_down_from_peers_.push_back(bgp_peers_[i-1]);
            continue;
        }

        // peers from 2nd quarter flip with gr
        BgpPeerTest *peer = bgp_peers_[i-1];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));
    }
    for (int i = n_instances_/4 + 1; i <= n_instances_/2; i++)
        instances_to_delete_during_gr_.push_back(i);
    GracefulRestartTestRun();
}

// Some routing instances are deleted. Then some of the agents permanently go
// down and they do not come back up (GR is triggered and should get cleaned up
// when the GR timer fires). Some of the other agents go down and then come
// back up advertising some of the routes again (not all). During this GR,
// additional instances are deleted
TEST_P(GracefulRestartTest, GracefulRestart_Delete_RoutingInstances_6) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (int i = 1; i <= n_instances_/4; i++)
        instances_to_delete_before_gr_.push_back(i);

    for (size_t i = 1; i <= xmpp_agents_.size(); i++) {

        // agents from 2nd half remain up through out this test
        if (i > xmpp_agents_.size()/2)
            continue;

        // agents from 1st quarter go down permantently
        if (i <= xmpp_agents_.size()/4) {
            n_down_from_agents_.push_back(xmpp_agents_[i-1]);
            continue;
        }

        // agents from 2nd quarter flip with gr
        test::NetworkAgentMock *agent = xmpp_agents_[i-1];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
    }

    for (size_t i = 1; i <= bgp_peers_.size(); i++) {

        // peers from 2nd half remain up through out this test
        if (i > bgp_peers_.size()/2)
            continue;

        // peers from 1st quarter go down permantently
        if (i <= bgp_peers_.size()/4) {
            n_down_from_peers_.push_back(bgp_peers_[i-1]);
            continue;
        }

        // peers from 2nd quarter flip with gr
        BgpPeerTest *peer = bgp_peers_[i-1];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));
    }
    GracefulRestartTestRun();
}

// Some routing instances are deleted. Then some of the agents permanently go
// down and they do not come back up (GR is triggered and should get cleaned up
// when the GR timer fires). Some of the other agents go down and then come
// back up advertising some of the routes again (not all). During this GR,
// additional instances are deleted
TEST_P(GracefulRestartTest, GracefulRestart_Delete_RoutingInstances_7) {
    SCOPED_TRACE(__FUNCTION__);
    GracefulRestartTestStart();

    for (int i = 1; i <= n_instances_/4; i++)
        instances_to_delete_before_gr_.push_back(i);

    for (size_t i = 1; i <= xmpp_agents_.size(); i++) {

        // agents from 2nd half remain up through out this test
        if (i > xmpp_agents_.size()/2)
            continue;

        // agents from 1st quarter go down permantently
        if (i <= xmpp_agents_.size()/4) {
            n_down_from_agents_.push_back(xmpp_agents_[i-1]);
            continue;
        }

        // agents from 2nd quarter flip with gr
        test::NetworkAgentMock *agent = xmpp_agents_[i-1];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_agents_.push_back(GRTestParams(agent, instance_ids,
                                                    nroutes));
    }

    for (size_t i = 1; i <= bgp_peers_.size(); i++) {

        // peers from 2nd half remain up through out this test
        if (i > bgp_peers_.size()/2)
            continue;

        // peers from 1st quarter go down permantently
        if (i <= bgp_peers_.size()/4) {
            n_down_from_peers_.push_back(bgp_peers_[i-1]);
            continue;
        }

        // peers from 2nd quarter flip with gr
        BgpPeerTest *peer = bgp_peers_[i-1];
        vector<int> instance_ids = vector<int>();
        vector<int> nroutes = vector<int>();
        for (int j = 1; j <= n_instances_; j++) {
            instance_ids.push_back(j);
            nroutes.push_back(n_routes_/2);
        }
        n_flipped_peers_.push_back(GRTestParams(peer, instance_ids, nroutes));
    }
    for (int i = n_instances_/4 + 1; i <= n_instances_/2; i++)
        instances_to_delete_during_gr_.push_back(i);
    GracefulRestartTestRun();
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
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
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
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
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
