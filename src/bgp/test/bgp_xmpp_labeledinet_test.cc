/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/task_annotations.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_update_sender.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/inet/inet_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "xmpp/xmpp_factory.h"

using namespace std;
using ::testing::TestWithParam;

static const char *config_2_control_nodes = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>inet-labeled</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>inet-labeled</family>\
            </address-families>\
        </session>\
    </bgp-router>\
</config>\
";

static const char *config_2_control_nodes_different_asn = "\
<config>\
    <bgp-router name=\'X\'>\
        <autonomous-system>64511</autonomous-system>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <router-type>external-control-node</router-type>\
        <session to=\'Y\'>\
            <address-families>\
                <family>inet-labeled</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <autonomous-system>64512</autonomous-system>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
        <router-type>external-control-node</router-type>\
        <session to=\'X\'>\
            <address-families>\
                <family>inet-labeled</family>\
            </address-families>\
        </session>\
    </bgp-router>\
</config>\
";


//
// Control Nodes X and Y.
// Agents A and B.
//
class BgpXmppLabeledInet2ControlNodeTest : public
    ::testing::TestWithParam<const char *> {
protected:
    BgpXmppLabeledInet2ControlNodeTest()
        : thread_(&evm_), xs_x_(NULL), xs_y_(NULL) {
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
        Configure(GetParam());
        task_util::WaitForIdle();
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

    void Configure(const char *cfg_template) {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
                 bs_x_->session_manager()->GetPort(),
                 bs_y_->session_manager()->GetPort());
        bs_x_->Configure(config);
        bs_y_->Configure(config);
    }
   bool CheckRoute(test::NetworkAgentMockPtr agent, string net,
        string prefix, string nexthop, int label) {
        const autogen::ItemType *rt = agent->LabeledInetRouteLookup(net,
                                                                    prefix);
        if (!rt)
            return false;
        if (rt->entry.next_hops.next_hop[0].address != nexthop)
            return false;
        if (rt->entry.next_hops.next_hop[0].label != label)
            return false;
        return true;
    }

    void VerifyRouteExists(test::NetworkAgentMockPtr agent, string net,
        string prefix, string nexthop, int label) {
        TASK_UTIL_EXPECT_TRUE(CheckRoute(agent, net, prefix, nexthop, label));
    }

    void VerifyRouteNoExists(test::NetworkAgentMockPtr agent, string net,
        string prefix) {
        TASK_UTIL_EXPECT_TRUE(agent->RouteLookup(net, prefix) == NULL);
    }


    // Check for a route's existence in inet.3 table.
    bool CheckLabeledInetRouteExists(BgpServerTestPtr server, string prefix) {
        task_util::TaskSchedulerLock lock;
        BgpTable *table = static_cast<BgpTable *>(
            server->database()->FindTable("inet.3"));
        if (!table)
            return false;

        boost::system::error_code error;
        Ip4Prefix nlri = Ip4Prefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        InetTable::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return(rt != NULL);
    }

    void VerifyLabeledInetRouteExists(BgpServerTestPtr server, string prefix) {
        TASK_UTIL_EXPECT_TRUE(CheckLabeledInetRouteExists(server, prefix));
    }

    void VerifyLabeledInetRouteNoExists(BgpServerTestPtr server,
                                        string prefix) {
        TASK_UTIL_EXPECT_FALSE(CheckLabeledInetRouteExists(server, prefix));
    }
    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    BgpServerTestPtr bs_y_;
    XmppServer *xs_x_;
    XmppServer *xs_y_;
    test::NetworkAgentMockPtr agent_a_;
    test::NetworkAgentMockPtr agent_b_;
    boost::scoped_ptr<BgpXmppChannelManager> cm_x_;
    boost::scoped_ptr<BgpXmppChannelManager> cm_y_;
};

// Route added on Agent A is seen on Agent A and B as well as on BGP Servers
// X and Y, also verify that the route is deleted properly
// The test is run with X and Y in the same AS as well as different AS's
TEST_P(BgpXmppLabeledInet2ControlNodeTest, RouteAddBasic) {

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

    // Register to Master instance
    agent_a_->LabeledInetSubscribe(BgpConfigManager::kMasterInstance, 1);
    agent_b_->LabeledInetSubscribe(BgpConfigManager::kMasterInstance, 1);

    task_util::WaitForIdle();
    // Add route from agent A.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    // Add route with label 3
    agent_a_->AddLabeledInetRoute(BgpConfigManager::kMasterInstance,
                                  route_a.str(), 3, "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route showed up on BGP servers A and B
    VerifyLabeledInetRouteExists(bs_x_, "10.1.1.1/32");
    VerifyLabeledInetRouteExists(bs_y_, "10.1.1.1/32");
    // Verify that route showed up on agents A and B.
    VerifyRouteExists(agent_a_, BgpConfigManager::kMasterInstance,
                      route_a.str(), "192.168.1.1", 3);
    VerifyRouteExists(agent_b_, BgpConfigManager::kMasterInstance,
                      route_a.str(), "192.168.1.1", 3);
    // Delete route from agent A.
    agent_a_->DeleteLabeledInetRoute(BgpConfigManager::kMasterInstance,
                                     route_a.str()); task_util::WaitForIdle();

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, BgpConfigManager::kMasterInstance,
                        route_a.str());
    VerifyRouteNoExists(agent_b_, BgpConfigManager::kMasterInstance,
                        route_a.str());
    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

TEST_P(BgpXmppLabeledInet2ControlNodeTest, RouteAddInvalidLabel0) {

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

    // Register to Master instance
    agent_a_->LabeledInetSubscribe(BgpConfigManager::kMasterInstance, 1);
    agent_b_->LabeledInetSubscribe(BgpConfigManager::kMasterInstance, 1);

    task_util::WaitForIdle();
    // Add route from agent A.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    // Add route with label 3
    agent_a_->AddLabeledInetRoute(BgpConfigManager::kMasterInstance,
                                  route_a.str(), 0, "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route showed up on BGP servers A and B
    VerifyLabeledInetRouteNoExists(bs_x_, "10.1.1.1/32");
    VerifyLabeledInetRouteNoExists(bs_y_, "10.1.1.1/32");

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, BgpConfigManager::kMasterInstance,
                        route_a.str());
    VerifyRouteNoExists(agent_b_, BgpConfigManager::kMasterInstance,
                        route_a.str());
    // Delete route from agent A.
    agent_a_->DeleteLabeledInetRoute(BgpConfigManager::kMasterInstance,
                                     route_a.str()); task_util::WaitForIdle();
    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

TEST_P(BgpXmppLabeledInet2ControlNodeTest, RouteAddInvalidLabelHigh) {

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

    // Register to Master instance
    agent_a_->LabeledInetSubscribe(BgpConfigManager::kMasterInstance, 1);
    agent_b_->LabeledInetSubscribe(BgpConfigManager::kMasterInstance, 1);

    task_util::WaitForIdle();
    // Add route from agent A.
    stringstream route_a;
    route_a << "10.1.1.1/32";
    // Add route with label 3
    agent_a_->AddLabeledInetRoute(BgpConfigManager::kMasterInstance,
                                  route_a.str(), 0x1FFFFF, "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route showed up on BGP servers A and B
    VerifyLabeledInetRouteNoExists(bs_x_, "10.1.1.1/32");
    VerifyLabeledInetRouteNoExists(bs_y_, "10.1.1.1/32");

    // Verify that route is deleted at agents A and B.
    VerifyRouteNoExists(agent_a_, BgpConfigManager::kMasterInstance,
                        route_a.str());
    VerifyRouteNoExists(agent_b_, BgpConfigManager::kMasterInstance,
                        route_a.str());
    // Delete route from agent A.
    agent_a_->DeleteLabeledInetRoute(BgpConfigManager::kMasterInstance,
                                     route_a.str()); task_util::WaitForIdle();
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

INSTANTIATE_TEST_CASE_P(BgpXmppLabeledInetTestWithParams,
                        BgpXmppLabeledInet2ControlNodeTest,
                        ::testing::Values(config_2_control_nodes,
                                          config_2_control_nodes_different_asn));

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
