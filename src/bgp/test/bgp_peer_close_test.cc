/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/algorithm/string.hpp>
#include <boost/assign/std/vector.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <list>

#include "base/logging.h"
#include "base/task.h"
#include "base/test/addr_test_util.h"
#include "base/test/task_test_util.h"
#include "base/util.h"
#include "io/event_manager.h"

#include "bgp/bgp_attr.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_debug.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer_close.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_proto.h"
#include "bgp/bgp_ribout.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/inet/inet_route.h"
#include "bgp/inet/inet_table.h"
#include "bgp/l3vpn/inetvpn_table.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/tunnel_encap/tunnel_encap.h"

#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "db/db.h"
#include "net/bgp_af.h"
#include "schema/xmpp_unicast_types.h"
#include "testing/gunit.h"

#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_state_machine.h"


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

static vector<int>  n_instances = boost::assign::list_of(1);
static vector<int>  n_routes    = boost::assign::list_of(1);
static vector<int>  n_peers     = boost::assign::list_of(1);
static vector<int>  n_agents    = boost::assign::list_of(1);
static vector<int>  n_targets   = boost::assign::list_of(1);
static vector<bool> xmpp_close_from_control_node =
                                  boost::assign::list_of(false);
static char **gargv;
static int    gargc;
static int    n_db_walker_wait_usecs = 0;
static int    wait_for_idle = 30; // Seconds

static void process_command_line_args(int argc, char **argv) {
    static bool cmd_line_processed;

    if (cmd_line_processed) return;
    cmd_line_processed = true;

    int instances = 1, routes = 1, peers = 1, agents = 1, targets = 1;
    bool close_from_control_node = false;
    bool cmd_line_arg_set = false;

    // Declare the supported options.
    options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("nroutes", value<int>(), "set number of routes")
        ("npeers", value<int>(), "set number of bgp peers")
        ("nagents", value<int>(), "set number of xmpp agents")
        ("ninstances", value<int>(), "set number of routing instances")
        ("ntargets", value<int>(), "set number of route targets")
        ("db-walker-wait-usecs", value<int>(), "set usecs delay in walker cb")
        ("wait-for-idle-time", value<int>(),
             "task_util::WaitForIdle() wait time, 0 to disable")
        ("close-from-control-node", bool_switch(&close_from_control_node),
             "Initiate xmpp session close from control-node")
        ;

    variables_map vm;
    store(parse_command_line(argc, argv, desc), vm);
    notify(vm);

    if (vm.count("help")) {
        cout << desc << "\n";
        exit(1);
    }

    if (close_from_control_node) {
        cmd_line_arg_set = true;
    }

    if (vm.count("ninstances")) {
        instances = vm["ninstances"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("nroutes")) {
        routes = vm["nroutes"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("npeers")) {
        peers = vm["npeers"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("nagents")) {
        agents = vm["nagents"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("ntargets")) {
        targets = vm["ntargets"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("db-walker-wait-usecs")) {
        n_db_walker_wait_usecs = vm["db-walker-wait-usecs"].as<int>();
        cmd_line_arg_set = true;
    }
    if (vm.count("wait-for-idle-time")) {
        wait_for_idle = vm["wait-for-idle-time"].as<int>();
        cmd_line_arg_set = true;
    }

    if (cmd_line_arg_set) {
        n_instances.clear();
        n_instances.push_back(instances);

        n_routes.clear();
        n_routes.push_back(routes);

        n_peers.clear();
        n_peers.push_back(peers);

        n_targets.clear();
        n_targets.push_back(targets);

        n_agents.clear();
        n_agents.push_back(agents);

        xmpp_close_from_control_node.clear();
        xmpp_close_from_control_node.push_back(close_from_control_node);
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

static vector<int> GetRouteParameters() {
    process_command_line_args(gargc, gargv);
    return n_routes;
}

static vector<int> GetPeerParameters() {
    process_command_line_args(gargc, gargv);
    return n_peers;
}

static vector<int> GetTargetParameters() {
    process_command_line_args(gargc, gargv);
    return n_targets;
}

static void WaitForIdle() {
    if (wait_for_idle) {
        usleep(10);
        task_util::WaitForIdle(wait_for_idle);
    }
}

class PeerCloseManagerTest : public PeerCloseManager {
public:
    explicit PeerCloseManagerTest(IPeer *peer) : PeerCloseManager(peer) { }
    ~PeerCloseManagerTest() { }

    //
    // Do not start the timer in test, as we right away call it in line from
    // within the tests
    //
    void StartStaleTimer() { }
};

class BgpNullPeer {
public:
    BgpNullPeer(BgpServerTest *server, const BgpInstanceConfig *instance_config,
                string name, RoutingInstance *rtinstance, int peer_id) {
        for (int i = 0; i < 20; i++) {
            ribout_creation_complete_.push_back(false);
        }

        autogen::BgpRouterParams params;
        params.Clear();
        params.address = "127.0.0.1";
        params.autonomous_system = 65412;
        params.port = peer_id;
        peer_id_ = peer_id;
        name_ = name;
        rtr_config_.SetProperty("bgp-router-parameters",
                                static_cast<AutogenProperty *>(&params));
        config_.reset(new BgpNeighborConfig(instance_config, name, "Local",
                                            &rtr_config_));
        peer_ = static_cast<BgpPeerTest *>
            (rtinstance->peer_manager()->PeerLocate(server, config_.get()));
        WaitForIdle();
    }
    BgpPeerTest *peer() { return peer_; }
    int peer_id() { return peer_id_; }
    bool ribout_creation_complete(Address::Family family) {
        return ribout_creation_complete_[family];
    }
    void ribout_creation_complete(Address::Family family, bool complete) {
        ribout_creation_complete_[family] = complete;
    }

    string name_;
    int peer_id_;
    BgpPeerTest *peer_;

private:
    autogen::BgpRouter rtr_config_;
    auto_ptr<BgpNeighborConfig> config_;
    std::vector<bool> ribout_creation_complete_;
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

typedef std::tr1::tuple<int, int, int, int, int, bool> TestParams;

class BgpPeerCloseTest : public ::testing::TestWithParam<TestParams> {

public:
    void CreateRibsDone(IPeer *ipeer, BgpTable *table, BgpNullPeer *npeer) {
        npeer->ribout_creation_complete(table->family(), true);
    }

    void DeleteRoutingInstance(RoutingInstance *rtinstance);
    bool IsPeerCloseGraceful(bool graceful) { return graceful; }
    void SetPeerCloseGraceful(bool graceful) {
        server_->GetIsPeerCloseGraceful_fnc_ =
                    boost::bind(&BgpPeerCloseTest::IsPeerCloseGraceful, this,
                                graceful);
        xmpp_server_->GetIsPeerCloseGraceful_fnc_ =
                    boost::bind(&BgpPeerCloseTest::IsPeerCloseGraceful, this,
                                graceful);
    }

protected:
    BgpPeerCloseTest() : thread_(&evm_) { }

    virtual void SetUp() {

        server_.reset(new BgpServerTest(&evm_, "test"));
        xmpp_server_ = new XmppServerTest(&evm_, XMPP_CONTROL_SERV);

        channel_manager_.reset(new BgpXmppChannelManagerMock(
                                       xmpp_server_, server_.get()));
        master_cfg_.reset(BgpTestUtil::CreateBgpInstanceConfig(
            BgpConfigManager::kMasterInstance, "", ""));
        rtinstance_ = static_cast<RoutingInstance *>(
            server_->routing_instance_mgr()->GetRoutingInstance(
                BgpConfigManager::kMasterInstance));
        n_families_ = 2;
        familes_.push_back(Address::INET);
        familes_.push_back(Address::INETVPN);

        server_->session_manager()->Initialize(0);
        xmpp_server_->Initialize(0, false);
        thread_.Start();
    }

    void AgentCleanup() {
        BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
            agent->Delete();
        }
    }

    void Cleanup() {
        BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
            delete npeer;
        }
        WaitForIdle();
    }

    virtual void TearDown() {
        SetPeerCloseGraceful(false);
        xmpp_server_->Shutdown();
        WaitForIdle();
        if (n_agents_) {
            TASK_UTIL_EXPECT_EQ(0, xmpp_server_->ConnectionsCount());
        }
        AgentCleanup();
        channel_manager_.reset();
        WaitForIdle();

        TcpServerManager::DeleteServer(xmpp_server_);
        xmpp_server_ = NULL;
        server_->Shutdown();
        WaitForIdle();
        Cleanup();
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();

        BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
            delete agent;
        }
    }

    void Configure() {
        server_->Configure(GetConfig().c_str());
        WaitForIdle();
        VerifyRoutingInstances();
    }

    XmppChannelConfig *CreateXmppChannelCfg(const char *address, int port,
                                            const string &from,
                                            const string &to,
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

    string GetConfig();
    BgpAttr *CreatePathAttr();
    void AddRoutes(BgpTable *table, BgpNullPeer *npeer);
    ExtCommunitySpec *CreateRouteTargets();
    void AddAllRoutes();
    void AddPeersWithRoutes(const BgpInstanceConfig *instance_config);
    void AddXmppPeersWithRoutes();
    void CreateAgents();
    void Subscribe();
    void UnSubscribe();
    void AddXmppRoutes();
    void DeleteXmppRoutes();
    void VerifyXmppRoutes(int routes);
    void DeleteAllRoutingInstances();
    void VerifyRoutingInstances();
    void XmppPeerClose();
    void CallStaleTimer(bool);
    void InitParams();
    void VerifyPeer(BgpServerTest *server, RoutingInstance *rtinstance,
                    BgpNullPeer *npeer, BgpPeerTest *peer);
    void VerifyPeers();
    void VerifyNoPeers();
    void VerifyRoutes(int count);
    void VerifyRibOutCreationCompletion();
    void VerifyXmppRouteNextHops();
    bool SendUpdate(BgpPeerTest *peer, const uint8_t *msg, size_t msgsize);
    bool IsReady(bool ready);
    bool MpNlriAllowed(BgpPeerTest *peer, uint16_t afi, uint8_t safi);

    EventManager evm_;
    ServerThread thread_;
    boost::scoped_ptr<BgpServerTest> server_;
    XmppServerTest *xmpp_server_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> channel_manager_;
    scoped_ptr<BgpInstanceConfigTest> master_cfg_;
    RoutingInstance *rtinstance_;
    std::list<BgpNullPeer *> peers_;
    std::list<test::NetworkAgentMock *> xmpp_agents_;
    std::list<BgpXmppChannel *> xmpp_peers_;
    int n_families_;
    std::vector<Address::Family> familes_;
    int n_instances_;
    int n_peers_;
    int n_routes_;
    int n_agents_;
    int n_targets_;
    bool xmpp_close_from_control_node_;
};

void BgpPeerCloseTest::VerifyPeer(BgpServerTest *server,
                                  RoutingInstance *rtinstance,
                                  BgpNullPeer *npeer, BgpPeerTest *peer) {
    EXPECT_EQ((server)->FindPeer(rtinstance->name().c_str(), npeer->name_),
              peer);
}

void BgpPeerCloseTest::VerifyPeers() {
    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        VerifyPeer(server_.get(), rtinstance_, npeer, npeer->peer());
    }
}

void BgpPeerCloseTest::VerifyNoPeers() {
    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        VerifyPeer(server_.get(), rtinstance_, npeer,
                   static_cast<BgpPeerTest *>(NULL));
    }
}

void BgpPeerCloseTest::VerifyRoutes(int count) {
    if (!n_peers_) return;

    for (int i = 0; i < n_families_; i++) {
        BgpTable *tb = rtinstance_->GetTable(familes_[i]);
        if (count && n_agents_ && n_peers_ &&
                familes_[i] == Address::INETVPN) {
            BGP_VERIFY_ROUTE_COUNT(tb, (n_instances_ + 1) * count);
        } else {
            BGP_VERIFY_ROUTE_COUNT(tb, count);
        }
    }
}

void BgpPeerCloseTest::VerifyRibOutCreationCompletion() {
    WaitForIdle();

    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        for (int i = 0; i < n_families_; i++) {
            EXPECT_TRUE(npeer->ribout_creation_complete(familes_[i]));
        }
    }
}

string BgpPeerCloseTest::GetConfig() {
    ostringstream out;

    out << 
    "<config>\
        <bgp-router name=\'A\'>\
            <identifier>192.168.0.1</identifier>\
            <address>127.0.0.1</address>\
            <port>" << server_->session_manager()->GetPort() << "</port>\
            <session to=\'B\'>\
                <address-families>\
                <family>inet-vpn</family>\
                <family>e-vpn</family>\
                <family>erm-vpn</family>\
                <family>route-target</family>\
                </address-families>\
            </session>\
    </bgp-router>\
    ";

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

bool BgpPeerCloseTest::SendUpdate(BgpPeerTest *peer, const uint8_t *msg,
                              size_t msgsize) {
    return true;
}

bool BgpPeerCloseTest::IsReady(bool ready) {
    return ready;
}

bool BgpPeerCloseTest::MpNlriAllowed(BgpPeerTest *peer, uint16_t afi,
                                 uint8_t safi) {
    if (afi ==  BgpAf::IPv4 && safi == BgpAf::Unicast) {
        return true;
    }

    if (afi ==  BgpAf::IPv4 && safi == BgpAf::Vpn) {
        return true;
    }

    if (afi ==  BgpAf::L2Vpn && safi == BgpAf::EVpn) {
        return true;
    }

    return false;
}

BgpAttr *BgpPeerCloseTest::CreatePathAttr() {
    BgpAttrSpec attr_spec;
    BgpAttrDB *db = server_->attr_db();
    BgpAttr *attr = new BgpAttr(db, attr_spec);

    attr->set_origin(BgpAttrOrigin::IGP);
    attr->set_med(5);
    attr->set_local_pref(10);

    AsPathSpec as_path;
    AsPathSpec::PathSegment *ps = new AsPathSpec::PathSegment;
    ps->path_segment_type = AsPathSpec::PathSegment::AS_SEQUENCE;
    ps->path_segment.push_back(20);
    ps->path_segment.push_back(30);
    as_path.path_segments.push_back(ps);

    attr->set_as_path(&as_path);

    return attr;
}

ExtCommunitySpec *BgpPeerCloseTest::CreateRouteTargets() {
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

void BgpPeerCloseTest::AddRoutes(BgpTable *table, BgpNullPeer *npeer) {
    DBRequest req;
    boost::scoped_ptr<ExtCommunitySpec> commspec;
    boost::scoped_ptr<BgpAttrLocalPref> local_pref;
    IPeer *peer = npeer->peer();

#if 0
    InetVpnPrefix vpn_prefix(InetVpnPrefix::FromString(
                                                "123:456:192.168.255.0/32"));
#endif

    Ip4Prefix prefix(Ip4Prefix::FromString("192.168.255.0/24"));
    InetVpnPrefix vpn_prefix(InetVpnPrefix::FromString("123:456:10." +
                boost::lexical_cast<string>(npeer->peer_id()) + ".1.1/32"));

    for (int rt = 0; rt < n_routes_; rt++,
        prefix = task_util::Ip4PrefixIncrement(prefix),
        vpn_prefix = task_util::InetVpnPrefixIncrement(vpn_prefix)) {
        req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;

        int localpref = 1 + (std::rand() % (n_peers_ + n_agents_));
        localpref = 100; // Default preference
        BgpAttrSpec attr_spec;
        local_pref.reset(new BgpAttrLocalPref(localpref));
        attr_spec.push_back(local_pref.get());

        BgpAttrNextHop nexthop(0x7f010000 + npeer->peer_id());
        attr_spec.push_back(&nexthop);
        TunnelEncap tun_encap(std::string("gre"));

        switch (table->family()) {
            case Address::INET:
                commspec.reset(new ExtCommunitySpec());
                commspec->communities.push_back(get_value(tun_encap.GetExtCommunity().begin(), 8));
                attr_spec.push_back(commspec.get());
                req.key.reset(new InetTable::RequestKey(prefix, peer));
                break;
            case Address::INETVPN:
                req.key.reset(new InetVpnTable::RequestKey(vpn_prefix, peer));
                commspec.reset(CreateRouteTargets());
                if (!commspec.get()) {
                    commspec.reset(new ExtCommunitySpec());
                }
                commspec->communities.push_back(get_value(tun_encap.GetExtCommunity().begin(), 8));
                attr_spec.push_back(commspec.get());
                break;
            default:
                assert(0);
                break;
        }

        uint32_t label = 20000 + rt;
        BgpAttrPtr attr = server_->attr_db()->Locate(attr_spec);
        req.data.reset(new InetTable::RequestData(attr, 0, label));
        table->Enqueue(&req);
    }
}

void BgpPeerCloseTest::AddAllRoutes() {
    RibExportPolicy policy(BgpProto::IBGP, RibExportPolicy::BGP, 1, 0);

    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        for (int i = 0; i < n_families_; i++) {
            BgpTable *table = rtinstance_->GetTable(familes_[i]);

            server_->membership_mgr()->Register(npeer->peer(), table, policy,
                    -1, boost::bind(&BgpPeerCloseTest::CreateRibsDone, this, _1,
                                    _2, npeer));

            // Add routes to RibIn
            AddRoutes(table, npeer);
        }
        npeer->peer()->set_vpn_tables_registered(true);
    }

    WaitForIdle();
}

void BgpPeerCloseTest::AddXmppPeersWithRoutes() {
    if (!n_agents_) return;

    CreateAgents();

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        TASK_UTIL_EXPECT_EQ(true, agent->IsEstablished());
    }

    WaitForIdle();
    Subscribe();
    VerifyXmppRoutes(0);
    AddXmppRoutes();
    WaitForIdle();
    VerifyXmppRoutes(n_instances_ * n_routes_);
}

void BgpPeerCloseTest::CreateAgents() {
    Ip4Prefix prefix(Ip4Prefix::FromString("127.0.0.1/32"));

    for (int i = 0; i < n_agents_; i++) {

        // create an XMPP client in server A
        xmpp_agents_.push_back(new test::NetworkAgentMock(&evm_,
            "agent" + boost::lexical_cast<string>(i) +
                "@vnsw.contrailsystems.com",
            xmpp_server_->GetPort(),
            prefix.ip4_addr().to_string()));
        WaitForIdle();

        TASK_UTIL_EXPECT_NE_MSG(static_cast<BgpXmppChannel *>(NULL),
                          channel_manager_->channel_,
                          "Waiting for channel_manager_->channel_ to be set");
        xmpp_peers_.push_back(channel_manager_->channel_);
        channel_manager_->channel_ = NULL;

        prefix = task_util::Ip4PrefixIncrement(prefix);
    }
}

void BgpPeerCloseTest::Subscribe() {

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        agent->Subscribe(BgpConfigManager::kMasterInstance, -1);
        for (int i = 1; i <= n_instances_; i++) {
            string instance_name = "instance" + boost::lexical_cast<string>(i);
            agent->Subscribe(instance_name, i);
        }
    }
    WaitForIdle();
}

void BgpPeerCloseTest::UnSubscribe() {

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        agent->Unsubscribe(BgpConfigManager::kMasterInstance);
        for (int i = 1; i <= n_instances_; i++) {
            string instance_name = "instance" + boost::lexical_cast<string>(i);
            agent->Unsubscribe(instance_name);
        }
    }
    VerifyXmppRoutes(0);
    WaitForIdle();
}

void BgpPeerCloseTest::AddXmppRoutes() {

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        for (int i = 1; i <= n_instances_; i++) {
            string instance_name = "instance" + boost::lexical_cast<string>(i);

            Ip4Prefix prefix(Ip4Prefix::FromString(
                        "10." + boost::lexical_cast<string>(i) + ".1.1/32"));
            for (int rt = 0; rt < n_routes_; rt++,
                prefix = task_util::Ip4PrefixIncrement(prefix)) {
                agent->AddRoute(instance_name, prefix.ToString());
            }
        }
    }
}

void BgpPeerCloseTest::DeleteXmppRoutes() {
    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        for (int i = 1; i <= n_instances_; i++) {
            string instance_name = "instance" + boost::lexical_cast<string>(i);

            Ip4Prefix prefix(Ip4Prefix::FromString(
                        "10." + boost::lexical_cast<string>(i) + ".1.1/32"));
            for (int rt = 0; rt < n_routes_; rt++,
                 prefix = task_util::Ip4PrefixIncrement(prefix)) {
                agent->DeleteRoute(instance_name, prefix.ToString());
            }
        }
    }
    WaitForIdle();
    // VerifyXmppRoutes(0);
}

void BgpPeerCloseTest::VerifyXmppRoutes(int routes) {
    if (!n_agents_) return;

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        for (int i = 1; i <= n_instances_; i++) {
            string instance_name = "instance" + boost::lexical_cast<string>(i);
            TASK_UTIL_EXPECT_EQ_MSG(routes, agent->RouteCount(instance_name),
                                    "Wait for routes in " + instance_name);
            ASSERT_TRUE(agent->RouteCount(instance_name) == routes);
        }
    }
    WaitForIdle();
}

void BgpPeerCloseTest::VerifyXmppRouteNextHops() {
    if (!n_agents_ || n_agents_ != n_peers_) return;

    int agent_id = 0;
    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        agent_id++;
        for (int i = 1; i <= n_instances_; i++) {
            string instance_name = "instance" + boost::lexical_cast<string>(i);
            TASK_UTIL_EXPECT_EQ_MSG(n_routes_, agent->RouteCount(instance_name),
                                    "Wait for routes in " + instance_name);
            for (int rt = 0; rt < n_routes_; rt++) {
                string prefix =
                    "10." + boost::lexical_cast<string>(agent_id) + ".1.1/32";

                //
                // We expect two next-hops, one from the agent and one from
                // the bgp peer
                //
                const test::NetworkAgentMock::RouteEntry *route;
                TASK_UTIL_EXPECT_TRUE(
                    (route = agent->RouteLookup(instance_name, prefix)) &&
                    route->entry.next_hops.next_hop.size() == 2);
            }
        }
    }
    WaitForIdle();
}

void BgpPeerCloseTest::DeleteAllRoutingInstances() {
    ostringstream out;
    out << "<delete>";
    for (int i = 1; i <= n_instances_; i++) {
        out << "<routing-instance name='instance" << i << "'>\n";
        for (int j = 1; j <= n_targets_; j++) {
            out << "    <vrf-target>target:1:" << j << "</vrf-target>\n";
        }
        out << "</routing-instance>\n";
    }

    out << "</delete>";


    server_->Configure(out.str().c_str());
    WaitForIdle();
}

void BgpPeerCloseTest::VerifyRoutingInstances() {
    for (int i = 1; i <= n_instances_; i++) {
        string instance_name = "instance" + boost::lexical_cast<string>(i);
        TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
                            server_->routing_instance_mgr()->\
                                GetRoutingInstance(instance_name));
    }

    //
    // Verify 'default' master routing-instance
    //
    TASK_UTIL_EXPECT_NE(static_cast<RoutingInstance *>(NULL),
                        server_->routing_instance_mgr()->GetRoutingInstance(
                               BgpConfigManager::kMasterInstance));
}

void BgpPeerCloseTest::AddPeersWithRoutes(
        const BgpInstanceConfig *instance_config) {

    Configure();
    SetPeerCloseGraceful(false);

    //
    // Add XmppPeers with routes as well
    //
    AddXmppPeersWithRoutes();

    for (int p = 1; p <= n_peers_; p++) {
        ostringstream oss;

        oss << "NullPeer" << p;
        BgpNullPeer *npeer =
            new BgpNullPeer(server_.get(), instance_config, oss.str(),
                            rtinstance_, p);
        VerifyPeer(server_.get(), rtinstance_, npeer, npeer->peer());

        // Override certain default routines to customize behavior that we want
        // in this test.
        npeer->peer()->SendUpdate_fnc_ =
            boost::bind(&BgpPeerCloseTest::SendUpdate, this, npeer->peer(),
                        _1, _2);
        npeer->peer()->MpNlriAllowed_fnc_ =
            boost::bind(&BgpPeerCloseTest::MpNlriAllowed, this, npeer->peer(),
                        _1, _2);
        npeer->peer()->IsReady_fnc_ =
            boost::bind(&BgpPeerCloseTest::IsReady, this, true);
        peers_.push_back(npeer);
    }

    AddAllRoutes();
    VerifyXmppRouteNextHops();
}

void BgpPeerCloseTest::CallStaleTimer(bool bgp_peers_ready) {


    // Invoke stale timer callbacks as evm is not running in this unit test
    BOOST_FOREACH(BgpNullPeer *peer, peers_) {
        peer->peer()->IsReady_fnc_ =
            boost::bind(&BgpPeerCloseTest::IsReady, this, bgp_peers_ready);
        peer->peer()->peer_close()->close_manager()->StaleTimerCallback();
    }

    BOOST_FOREACH(BgpXmppChannel *peer, xmpp_peers_) {
        peer->Peer()->peer_close()->close_manager()->StaleTimerCallback();
    }

    WaitForIdle();
}

void BgpPeerCloseTest::XmppPeerClose() {

    if (xmpp_close_from_control_node_) {
        BOOST_FOREACH(BgpXmppChannel *peer, xmpp_peers_) {
            peer->Peer()->Close();
        }
    } else {
        BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
            // agent->client()->Shutdown();
            agent->SessionDown();
        }
    }

    WaitForIdle();
    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        TASK_UTIL_EXPECT_TRUE(!agent->IsEstablished());
    }
}

void BgpPeerCloseTest::InitParams() {
    n_instances_ = ::std::tr1::get<0>(GetParam());
    n_routes_ = ::std::tr1::get<1>(GetParam());
    n_peers_ = ::std::tr1::get<2>(GetParam());
    n_agents_ = ::std::tr1::get<3>(GetParam());
    n_targets_ = ::std::tr1::get<4>(GetParam());
    xmpp_close_from_control_node_ = ::std::tr1::get<5>(GetParam());
}

// Peer flaps
//
// Run all tests with  n peers, n routes, and address-families combined
//
// 1. Close with RibIn delete
// 2. Close with RibIn Stale, followed by RibIn Delete
// 3. Close with RibIn Stale, followed by RibIn Sweep
//
// Config deletions
//
// 1. Peer(s) deletion from config
// 2. Routing Instance(s) deletion from config
// 3. Delete entire bgp routing config
//
//
// Config modifications
//
// 1. Modify router-id
// 2. Toggle graceful restart options for address families selectively
// 3. Modify graceful restart timer values for address families selectively
// 4. Neighbor inbound policy channge (In future..)
//
// 1. Repeated close in each of the above case before the close is complete
// 2. Repeated close in each of the above case after the close is complete (?)

TEST_P(BgpPeerCloseTest, ClosePeers) {
    SCOPED_TRACE(__FUNCTION__);
    InitParams();

    AddPeersWithRoutes(master_cfg_.get());
    VerifyPeers();
    VerifyRoutes(n_routes_);
    VerifyRibOutCreationCompletion();

    // BgpShow::Instance(server_.get());

    // Trigger ribin deletes
    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        npeer->peer()->Clear(BgpProto::Notification::AdminReset);
    }

    XmppPeerClose();

    // Assert that all ribins for all families have been deleted correctly
    VerifyPeers();
    VerifyRoutes(0);
}

TEST_P(BgpPeerCloseTest, DeletePeers) {
    SCOPED_TRACE(__FUNCTION__);
    InitParams();
    AddPeersWithRoutes(master_cfg_.get());
    WaitForIdle();
    VerifyPeers();
    VerifyRoutes(n_routes_);
    VerifyRibOutCreationCompletion();

    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        npeer->peer()->ManagedDelete();
    }

    XmppPeerClose();

    // Assert that all ribins have been deleted correctly
    VerifyNoPeers();
    VerifyRoutes(0);
}

TEST_P(BgpPeerCloseTest, DeleteXmppRoutes) {
    SCOPED_TRACE(__FUNCTION__);
    InitParams();
    AddPeersWithRoutes(master_cfg_.get());
    WaitForIdle();
    VerifyPeers();
    VerifyRoutes(n_routes_);
    VerifyRibOutCreationCompletion();

    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        npeer->peer()->ManagedDelete();
    }

    DeleteXmppRoutes();

    // Assert that all ribins have been deleted correctly
    VerifyNoPeers();
    VerifyRoutes(0);
}

TEST_P(BgpPeerCloseTest, Unsubscribe) {
    SCOPED_TRACE(__FUNCTION__);
    InitParams();
    AddPeersWithRoutes(master_cfg_.get());
    WaitForIdle();
    VerifyPeers();
    VerifyRoutes(n_routes_);
    VerifyRibOutCreationCompletion();

    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        npeer->peer()->ManagedDelete();
    }

    UnSubscribe();

    // Assert that all ribins have been deleted correctly
    VerifyNoPeers();
    VerifyRoutes(0);
}

TEST_P(BgpPeerCloseTest, DeleteRoutingInstances) {
    SCOPED_TRACE(__FUNCTION__);
    InitParams();
    AddPeersWithRoutes(master_cfg_.get());
    WaitForIdle();
    VerifyPeers();
    VerifyRoutes(n_routes_);
    VerifyRibOutCreationCompletion();

    // Delete the routing instance
    DeleteAllRoutingInstances();
    UnSubscribe();
    WaitForIdle();

    TASK_UTIL_EXPECT_EQ_MSG(1, server_->routing_instance_mgr()->count(),
        "Waiting for the completion of routing-instances' deletion");
}

TEST_P(BgpPeerCloseTest, DISABLED_ClosePeersWithRouteStalingAndDelete) {
    SCOPED_TRACE(__FUNCTION__);
    InitParams();
    AddPeersWithRoutes(master_cfg_.get());
    WaitForIdle();
    VerifyPeers();
    VerifyRoutes(n_routes_);
    VerifyRibOutCreationCompletion();

    SetPeerCloseGraceful(true);

    // Trigger ribin deletes
    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        npeer->peer()->Clear(BgpProto::Notification::AdminReset);
    }

    XmppPeerClose();

    //
    // Wait for xmpp sessions to go down in the server
    //
    BOOST_FOREACH(BgpXmppChannel *peer, xmpp_peers_) {
        TASK_UTIL_EXPECT_FALSE(peer->Peer()->IsReady());
    }

    CallStaleTimer(false);

    // Assert that all ribins have been deleted correctly
    WaitForIdle();
    VerifyPeers();
    VerifyRoutes(0);
}

TEST_P(BgpPeerCloseTest, DISABLED_ClosePeersWithRouteStaling) {
    SCOPED_TRACE(__FUNCTION__);
    InitParams();

    //
    // Graceful restart is not supported yet from xmpp agents
    //
    AddPeersWithRoutes(master_cfg_.get());
    WaitForIdle();
    VerifyPeers();
    VerifyRoutes(n_routes_);
    VerifyRibOutCreationCompletion();

    SetPeerCloseGraceful(true);

    // Trigger ribin deletes
    BOOST_FOREACH(BgpNullPeer *npeer, peers_) { npeer->peer()->Close(); }
    XmppPeerClose();

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        TASK_UTIL_EXPECT_FALSE(agent->IsEstablished());
    }

    // Verify that routes are still there (staled)
    VerifyRoutes(n_routes_);
    // VerifyXmppRoutes(n_instances_ * n_routes_);

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        agent->SessionUp();
    }

    WaitForIdle();

    BOOST_FOREACH(BgpNullPeer *npeer, peers_) {
        TASK_UTIL_EXPECT_TRUE(npeer->peer()->IsReady());
    }

    BOOST_FOREACH(test::NetworkAgentMock *agent, xmpp_agents_) {
        TASK_UTIL_EXPECT_TRUE(agent->IsEstablished());
    }

    // Feed the routes again - stale flag should be reset now
    AddAllRoutes();

    WaitForIdle();
    Subscribe();
    AddXmppRoutes();
    WaitForIdle();
    // VerifyXmppRoutes(n_instances_ * n_routes_);

    // Invoke stale timer callbacks as evm is not running in this unit test
    CallStaleTimer(true);

    WaitForIdle();
    VerifyPeers();
    VerifyRoutes(n_routes_);
    // VerifyXmppRoutes(n_instances_ * n_routes_);

    SetPeerCloseGraceful(false);
    UnSubscribe();
    WaitForIdle();
    XmppPeerClose();
    WaitForIdle();
}

#define COMBINE_PARAMS \
    Combine(ValuesIn(GetInstanceParameters()),                      \
            ValuesIn(GetRouteParameters()),                         \
            ValuesIn(GetPeerParameters()),                          \
            ValuesIn(GetAgentParameters()),                         \
            ValuesIn(GetTargetParameters()),                        \
            ValuesIn(xmpp_close_from_control_node))

INSTANTIATE_TEST_CASE_P(BgpPeerCloseTestWithParams, BgpPeerCloseTest,
                        COMBINE_PARAMS);

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<PeerCloseManager>(
        boost::factory<PeerCloseManagerTest *>());
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
