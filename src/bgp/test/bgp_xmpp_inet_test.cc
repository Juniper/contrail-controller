/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "base/task_annotations.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_update_sender.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "schema/bgp_schema_types.h"
#include "xmpp/xmpp_factory.h"

using std::string;
using std::auto_ptr;
using boost::assign::list_of;

static const char *config_2_control_nodes = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>inet</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>inet</family>\
            </address-families>\
        </session>\
    </bgp-router>\
</config>\
";

//
// Control Nodes X and Y.
// Agents A and B.
//
class BgpXmppInetTest : public ::testing::Test {
protected:
    static const int kRouteCount = 512;

    BgpXmppInetTest()
        : thread_(&evm_), xs_x_(NULL), xs_y_(NULL),
          master_(BgpConfigManager::kMasterInstance) {
    }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        bs_x_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created BGP server at port: " <<
            bs_x_->session_manager()->GetPort());
        xs_x_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        xs_x_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            xs_x_->GetPort());
        cm_x_.reset(new BgpXmppChannelManager(xs_x_, bs_x_.get()));

        bs_y_.reset(new BgpServerTest(&evm_, "Y"));
        bs_y_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created BGP server at port: " <<
            bs_y_->session_manager()->GetPort());
        xs_y_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        xs_y_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            xs_y_->GetPort());
        cm_y_.reset(new BgpXmppChannelManager(xs_y_, bs_y_.get()));

        thread_.Start();

        Configure();
        task_util::WaitForIdle();

        // Create XMPP Agent A connected to XMPP server X.
        agent_a_.reset(
            new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
                "127.0.0.1", "127.0.0.1"));
        TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

        // Create XMPP Agent B connected to XMPP server Y.
        agent_b_.reset(
            new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
                "127.0.0.2", "127.0.0.2"));
        TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    }

    virtual void TearDown() {
        xs_x_->Shutdown();
        task_util::WaitForIdle();
        bs_x_->Shutdown();
        task_util::WaitForIdle();
        cm_x_.reset();

        xs_y_->Shutdown();
        task_util::WaitForIdle();
        bs_y_->Shutdown();
        task_util::WaitForIdle();
        cm_y_.reset();

        TcpServerManager::DeleteServer(xs_x_);
        xs_x_ = NULL;
        TcpServerManager::DeleteServer(xs_y_);
        xs_y_ = NULL;

        if (agent_a_) { agent_a_->Delete(); }
        if (agent_b_) { agent_b_->Delete(); }

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void Configure(const char *cfg_template = config_2_control_nodes) {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
                 bs_x_->session_manager()->GetPort(),
                 bs_y_->session_manager()->GetPort());
        bs_x_->Configure(config);
        bs_y_->Configure(config);
    }

    bool CheckRoute(test::NetworkAgentMockPtr agent, string net,
        string prefix, string nexthop, int local_pref, int med,
        const vector<string> communities) {
        const autogen::ItemType *rt = agent->RouteLookup(net, prefix);
        if (!rt)
            return false;
        if (local_pref && rt->entry.local_preference != local_pref)
            return false;
        if (med && rt->entry.med != med)
            return false;
        if (rt->entry.next_hops.next_hop.size() != 1)
            return false;
        const autogen::NextHopType &nh = rt->entry.next_hops.next_hop[0];
        if (nh.address != nexthop)
            return false;
        if (!nh.tunnel_encapsulation_list.tunnel_encapsulation.empty())
            return false;
        if (!communities.empty() &&
            rt->entry.community_tag_list.community_tag != communities)
            return false;
        return true;
    }

    void VerifyRouteExists(test::NetworkAgentMockPtr agent, string net,
        string prefix, string nexthop, int local_pref = 0, int med = 0) {
        TASK_UTIL_EXPECT_TRUE(CheckRoute(
            agent, net, prefix, nexthop, local_pref, med, vector<string>()));
    }

    void VerifyRouteExists(test::NetworkAgentMockPtr agent, string net,
        string prefix, string nexthop, const vector<string> &communities) {
        TASK_UTIL_EXPECT_TRUE(
            CheckRoute(agent, net, prefix, nexthop, 0, 0, communities));
    }

    void VerifyRouteNoExists(test::NetworkAgentMockPtr agent, string net,
        string prefix) {
        TASK_UTIL_EXPECT_TRUE(agent->RouteLookup(net, prefix) == NULL);
    }

    const BgpXmppChannel *VerifyXmppChannelExists(
        BgpXmppChannelManager *cm, const string &name) {
        TASK_UTIL_EXPECT_TRUE(cm->FindChannel(name) != NULL);
        return cm->FindChannel(name);
    }

    void VerifyOutputQueueDepth(BgpServerTestPtr server, uint32_t depth) {
        ConcurrencyScope scope("bgp::Config");
        TASK_UTIL_EXPECT_EQ(depth, server->get_output_queue_depth());
    }

    BgpTable *VerifyTableExists(BgpServerTestPtr server, const string &name) {
        TASK_UTIL_EXPECT_TRUE(server->database()->FindTable(name) != NULL);
        return static_cast<BgpTable *>(server->database()->FindTable(name));
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    BgpServerTestPtr bs_y_;
    XmppServer *xs_x_;
    XmppServer *xs_y_;
    string master_;
    test::NetworkAgentMockPtr agent_a_;
    test::NetworkAgentMockPtr agent_b_;
    boost::scoped_ptr<BgpXmppChannelManager> cm_x_;
    boost::scoped_ptr<BgpXmppChannelManager> cm_y_;
};

static string BuildPrefix(uint32_t idx) {
    assert(idx <= 65535);
    string prefix = string("10.1.") +
        integerToString(idx / 255) + "." + integerToString(idx % 255) + "/32";
    return prefix;
}

//
// Basic route exchange.
//
TEST_F(BgpXmppInetTest, Basic) {
    // Subscribe to master instance
    agent_a_->Subscribe(master_, 0);
    agent_b_->Subscribe(master_, 0);

    // Add route from agent A.
    test::NextHop nexthop(true, "192.168.1.1");
    test::RouteAttributes attr;
    agent_a_->AddRoute(master_, BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B.
    VerifyRouteExists(agent_a_, master_, BuildPrefix(1), "192.168.1.1", 100);
    VerifyRouteExists(agent_b_, master_, BuildPrefix(1), "192.168.1.1", 100);

    // Delete route from agent A.
    agent_a_->DeleteRoute(master_, BuildPrefix(1));
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, master_, BuildPrefix(1));
    VerifyRouteNoExists(agent_b_, master_, BuildPrefix(1));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Change local preference.
//
TEST_F(BgpXmppInetTest, RouteChangeLocalPref) {
    // Subscribe to master instance
    agent_a_->Subscribe(master_, 0);
    agent_b_->Subscribe(master_, 0);

    // Add route from agent A.
    test::NextHop nexthop(true, "192.168.1.1");
    test::RouteAttributes attr(200, 0, 0);
    agent_a_->AddRoute(master_, BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B.
    VerifyRouteExists(agent_a_, master_, BuildPrefix(1), "192.168.1.1", 200);
    VerifyRouteExists(agent_b_, master_, BuildPrefix(1), "192.168.1.1", 200);

    // Change route from agent A.
    attr = test::RouteAttributes(300, 0, 0);
    agent_a_->AddRoute(master_, BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route changed on agents A and B.
    VerifyRouteExists(agent_a_, master_, BuildPrefix(1), "192.168.1.1", 300);
    VerifyRouteExists(agent_b_, master_, BuildPrefix(1), "192.168.1.1", 300);

    // Add route from agent A without local pref.
    attr = test::RouteAttributes();
    agent_a_->AddRoute(master_, BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route changed on agents A and B.
    VerifyRouteExists(agent_a_, master_, BuildPrefix(1), "192.168.1.1", 100);
    VerifyRouteExists(agent_b_, master_, BuildPrefix(1), "192.168.1.1", 100);

    // Delete route from agent A.
    agent_a_->DeleteRoute(master_, BuildPrefix(1));
    task_util::WaitForIdle();

    // Verify that routes are deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, master_, BuildPrefix(1));
    VerifyRouteNoExists(agent_b_, master_, BuildPrefix(1));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Route added with explicit med has expected med.
//
TEST_F(BgpXmppInetTest, RouteExplicitMed) {
    // Subscribe to master instance
    agent_a_->Subscribe(master_, 0);
    agent_b_->Subscribe(master_, 0);

    // Add route from agent A with local preference 100 and med 500.
    test::NextHop nexthop(true, "192.168.1.1");
    test::RouteAttributes attr(100, 500, 0);
    agent_a_->AddRoute(master_, BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with expected med.
    VerifyRouteExists(agent_a_, master_, BuildPrefix(1), "192.168.1.1", 0, 500);
    VerifyRouteExists(agent_b_, master_, BuildPrefix(1), "192.168.1.1", 0, 500);

    // Delete route from agent A.
    agent_a_->DeleteRoute(master_, BuildPrefix(1));
    task_util::WaitForIdle();

    // Verify that routes is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, master_, BuildPrefix(1));
    VerifyRouteNoExists(agent_b_, master_, BuildPrefix(1));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Route added with local preference and no med has auto calculated med.
//
TEST_F(BgpXmppInetTest, RouteLocalPrefToMed) {
    // Subscribe to master instance
    agent_a_->Subscribe(master_, 0);
    agent_b_->Subscribe(master_, 0);

    // Add route from agent A with local preference 100.
    test::NextHop nexthop(true, "192.168.1.1");
    test::RouteAttributes attr(100, 0, 0);
    agent_a_->AddRoute(master_, BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with expected med.
    VerifyRouteExists(agent_a_, master_, BuildPrefix(1), "192.168.1.1", 0, 200);
    VerifyRouteExists(agent_b_, master_, BuildPrefix(1), "192.168.1.1", 0, 200);

    // Change route from agent A to have local preference 200.
    attr = test::RouteAttributes(200, 0, 0);
    agent_a_->AddRoute(master_, BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B with expected med.
    VerifyRouteExists(agent_a_, master_, BuildPrefix(1), "192.168.1.1", 0, 100);
    VerifyRouteExists(agent_b_, master_, BuildPrefix(1), "192.168.1.1", 0, 100);

    // Delete route from agent A.
    agent_a_->DeleteRoute(master_, BuildPrefix(1));
    task_util::WaitForIdle();

    // Verify that routes is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, master_, BuildPrefix(1));
    VerifyRouteNoExists(agent_b_, master_, BuildPrefix(1));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Route added with community list.
//
TEST_F(BgpXmppInetTest, RouteWithCommunity) {
    // Subscribe to master instance
    agent_a_->Subscribe(master_, 0);
    agent_b_->Subscribe(master_, 0);

    // Add route from agent A.
    test::NextHop nexthop(true, "192.168.1.1");
    vector<string> comm = list_of("64512:101")("64512:102") ;
    test::RouteAttributes attr(comm);
    agent_a_->AddRoute(master_, BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B.
    VerifyRouteExists(agent_a_, master_, BuildPrefix(1), "192.168.1.1", comm);
    VerifyRouteExists(agent_b_, master_, BuildPrefix(1), "192.168.1.1", comm);

    // Change route from agent A.
    comm = list_of("64512:201")("64512:202") ;
    attr = test::RouteAttributes(comm);
    agent_a_->AddRoute(master_, BuildPrefix(1), nexthop, attr);
    task_util::WaitForIdle();

    // Verify that route showed up on agents A and B.
    VerifyRouteExists(agent_a_, master_, BuildPrefix(1), "192.168.1.1", comm);
    VerifyRouteExists(agent_b_, master_, BuildPrefix(1), "192.168.1.1", comm);

    // Delete route from agent A.
    agent_a_->DeleteRoute(master_, BuildPrefix(1));
    task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, master_, BuildPrefix(1));
    VerifyRouteNoExists(agent_b_, master_, BuildPrefix(1));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Multiple routes are added/updated.
//
TEST_F(BgpXmppInetTest, MultipleRoutes) {
    // Subscribe to master instance
    agent_a_->Subscribe(master_, 0);
    agent_b_->Subscribe(master_, 0);

    // Add multiple routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        test::NextHop nexthop(true, "192.168.1.1");
        test::RouteAttributes attr;
        agent_a_->AddRoute(master_, BuildPrefix(idx), nexthop, attr);
    }

    // Verify that routes showed up on agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(agent_a_, master_, BuildPrefix(idx), "192.168.1.1");
        VerifyRouteExists(agent_b_, master_, BuildPrefix(idx), "192.168.1.1");
    }

    // Updates routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        test::NextHop nexthop(true, "192.168.1.2");
        test::RouteAttributes attr;
        agent_a_->AddRoute(master_, BuildPrefix(idx), nexthop, attr);
    }

    // Verify that routes are updated up on agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(agent_a_, master_, BuildPrefix(idx), "192.168.1.2");
        VerifyRouteExists(agent_b_, master_, BuildPrefix(idx), "192.168.1.2");
    }

    // Delete route from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->DeleteRoute(master_, BuildPrefix(idx));
    }

    // Verify that routes are deleted at agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteNoExists(agent_a_, master_, BuildPrefix(idx));
        VerifyRouteNoExists(agent_b_, master_, BuildPrefix(idx));
    }

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Multiple routes are advertised/withdrawn properly on Join/Leave.
//
TEST_F(BgpXmppInetTest, JoinLeave) {
    // Subscribe agent A to master instance.
    agent_a_->Subscribe(master_, 0);

    // Add routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        test::NextHop nexthop(true, "192.168.1.1");
        test::RouteAttributes attr;
        agent_a_->AddRoute(master_, BuildPrefix(idx), nexthop, attr);
    }

    // Verify that routes showed up on agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(agent_a_, master_, BuildPrefix(idx), "192.168.1.1");
    }

    // Register agent B to master instance.
    agent_b_->Subscribe(master_, 0);
    task_util::WaitForIdle();

    // Verify that routes are present at agent A and showed up on agent B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(agent_a_, master_, BuildPrefix(idx), "192.168.1.1");
        VerifyRouteExists(agent_b_, master_, BuildPrefix(idx), "192.168.1.1");
    }

    // Unregister agent B from master instance.
    agent_b_->Unsubscribe(master_);
    task_util::WaitForIdle();

    // Verify that routes are present at agent A and deleted at agent B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteExists(agent_a_, master_, BuildPrefix(idx), "192.168.1.1");
        VerifyRouteNoExists(agent_b_, master_, BuildPrefix(1));
    }

    // Delete routes from agent A.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        agent_a_->DeleteRoute(master_, BuildPrefix(idx));
    }

    // Verify that routes are deleted at agents A and B.
    for (int idx = 0; idx < kRouteCount; ++idx) {
        VerifyRouteNoExists(agent_a_, master_, BuildPrefix(idx));
        VerifyRouteNoExists(agent_b_, master_, BuildPrefix(idx));
    }

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Agent flaps a route by changing it repeatedly.
//
TEST_F(BgpXmppInetTest, RouteFlap) {
    // Subscribe to master instance
    agent_a_->Subscribe(master_, 0);
    agent_b_->Subscribe(master_, 0);

    // Add route from agent A and change it repeatedly.
    for (int idx = 0; idx < 128; ++idx) {
        test::NextHop nexthop1(true, "192.168.1.1");
        test::NextHop nexthop2(true, "192.168.1.2");
        test::RouteAttributes attr;
        agent_a_->AddRoute(master_, BuildPrefix(1), nexthop1, attr);
        agent_a_->AddRoute(master_, BuildPrefix(1), nexthop2, attr);
    }

    // Delete route from agent A.
    agent_a_->DeleteRoute(master_, BuildPrefix(1));
    task_util::WaitForIdle();

    // Verify that routes is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, master_, BuildPrefix(1));
    VerifyRouteNoExists(agent_b_, master_, BuildPrefix(1));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
};

static void SetUp() {
    BgpServer::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
    BgpObjectFactory::Register<BgpXmppMessageBuilder>(
        boost::factory<BgpXmppMessageBuilder *>());
    XmppObjectFactory::Register<XmppStateMachine>(
        boost::factory<XmppStateMachineTest *>());
}
static void TearDown() {
    BgpServer::Terminate();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new TestEnvironment());
    DB::SetPartitionCount(1);
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();

    return result;
}
