/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>

#include <sstream>

#include "bgp/bgp_factory.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"


static const char *config_template_21 = "\
<config>\
    <bgp-router name=\'X\'>\
        <autonomous-system>1</autonomous-system>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>e-vpn</family>\
                <family>route-target</family>\
                <family>inet-vpn</family>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
        <sub-cluster name='subcluster1'/>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <autonomous-system>1</autonomous-system>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>e-vpn</family>\
                <family>route-target</family>\
                <family>inet-vpn</family>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
        <sub-cluster name='subcluster2'/>\
    </bgp-router>\
    <sub-cluster name='subcluster1'>\
        <sub-cluster-asn>199</sub-cluster-asn>\
    </sub-cluster>\
    <sub-cluster name='subcluster2'>\
        <sub-cluster-asn>299</sub-cluster-asn>\
    </sub-cluster>\
</config>\
";

//
// Control Nodes X and Y.
// Agents A and B.
//
class BgpSubClusterIntegrationTest : public ::testing::Test {
protected:
    BgpSubClusterIntegrationTest() :
            thread_(&evm_), xs_x_(NULL), xs_y_(NULL) {
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
        cm_x_.reset(
            new BgpXmppChannelManager(xs_x_, bs_x_.get()));

        bs_y_.reset(new BgpServerTest(&evm_, "Y"));
        bs_y_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created BGP server at port: " <<
            bs_y_->session_manager()->GetPort());
        xs_y_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        xs_y_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            xs_y_->GetPort());
        cm_y_.reset(
            new BgpXmppChannelManager(xs_y_, bs_y_.get()));

        thread_.Start();
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

        if (agent_a_)
            agent_a_->Delete();
        if (agent_b_)
            agent_b_->Delete();
        if (agent_c_)
            agent_c_->Delete();
        if (agent_d_)
            agent_d_->Delete();

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void Configure(BgpServerTestPtr server, const char *cfg_template) {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
                 server->session_manager()->GetPort());
        server->Configure(config);
    }

    void Configure(const char *cfg_template = config_template_21) {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
                 bs_x_->session_manager()->GetPort(),
                 bs_y_->session_manager()->GetPort());
        bs_x_->Configure(config);
        bs_y_->Configure(config);
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    BgpServerTestPtr bs_y_;
    XmppServer *xs_x_;
    XmppServer *xs_y_;
    test::NetworkAgentMockPtr agent_a_;
    test::NetworkAgentMockPtr agent_b_;
    test::NetworkAgentMockPtr agent_c_;
    test::NetworkAgentMockPtr agent_d_;
    boost::scoped_ptr<BgpXmppChannelManager> cm_x_;
    boost::scoped_ptr<BgpXmppChannelManager> cm_y_;
};

// Bring up agents with matching subcluster names.
TEST_F(BgpSubClusterIntegrationTest, ClusterNameMatch1) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1", false, "subcluster1"));

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2", false, "subcluster2"));

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Bring up agents with matching subcluster names. Then send a new connection
// request with mis-matching subcluster names. This new connection request
// should be ignored.
TEST_F(BgpSubClusterIntegrationTest, ClusterNameMatch2) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1", false, "subcluster1"));

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2", false, "subcluster2"));

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Try a new connection with mismatch in subcluster name.
    agent_c_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1", false, "subcluster11"));
    agent_d_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2", false, "subcluster21"));

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_d_->IsEstablished());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
    agent_c_->SessionDown();
    agent_d_->SessionDown();
}

// Bring up agents with mis-matching subcluster names.
TEST_F(BgpSubClusterIntegrationTest, ClusterNameMisMatch1) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1", false, "subcluster11"));

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2", false, "subcluster21"));

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Start with sub-cluster name mismatch and then update to match.
TEST_F(BgpSubClusterIntegrationTest, ClusterNameMisMatch2) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1", false, "subcluster11"));

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2", false, "subcluster21"));

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());

    agent_c_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1", false, "subcluster1"));

    // Create XMPP Agent B connected to XMPP server Y.
    agent_d_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2", false, "subcluster2"));

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_d_->IsEstablished());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
    agent_c_->SessionDown();
    agent_d_->SessionDown();
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
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();

    return result;
}
