/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "base/test/task_test_util.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "testing/gunit.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_factory.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_state_machine.h"

using boost::system::error_code;
using namespace std;

static const char *bgp_config_template = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <autonomous-system>%d</autonomous-system>\
        <port>%d</port>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
</config>\
";

//
// 1 BGP and XMPP server X.
// Agents A, B, C.
//
class BgpXmppBasicTest : public ::testing::Test {
protected:
    BgpXmppBasicTest() : thread_(&evm_), xs_x_(NULL) { }

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
        thread_.Start();

        agent_a_.reset(
            new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
                "127.0.0.11", "127.0.0.1"));
        agent_b_.reset(
            new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
                "127.0.0.12", "127.0.0.1"));
        agent_c_.reset(
            new test::NetworkAgentMock(&evm_, "agent-c", xs_x_->GetPort(),
                "127.0.0.13", "127.0.0.1"));
    }

    virtual void TearDown() {
        xs_x_->Shutdown();
        task_util::WaitForIdle();
        bs_x_->Shutdown();
        task_util::WaitForIdle();
        cm_x_.reset();

        TcpServerManager::DeleteServer(xs_x_);
        xs_x_ = NULL;

        agent_a_->Delete();
        agent_b_->Delete();
        agent_c_->Delete();

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void Configure(const char *cfg_template, int asn) {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
                 asn, bs_x_->session_manager()->GetPort());
        bs_x_->Configure(config);
    }

    int GetXmppConnectionFlapCount(const string &address) {
        boost::system::error_code ec;
        Ip4Address ip4addr = Ip4Address::from_string(address, ec);
        TASK_UTIL_EXPECT_TRUE(xs_x_->FindConnectionEndpoint(ip4addr) != NULL);
        const XmppConnectionEndpoint *conn_endpoint =
            xs_x_->FindConnectionEndpoint(ip4addr);
        return conn_endpoint->flap_count();
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    XmppServer *xs_x_;
    test::NetworkAgentMockPtr agent_a_;
    test::NetworkAgentMockPtr agent_b_;
    test::NetworkAgentMockPtr agent_c_;
    boost::scoped_ptr<BgpXmppChannelManager> cm_x_;
};

TEST_F(BgpXmppBasicTest, ClearAllConnections) {
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    int flap_a = GetXmppConnectionFlapCount(agent_a_->localaddr());
    int flap_b = GetXmppConnectionFlapCount(agent_b_->localaddr());
    int flap_c = GetXmppConnectionFlapCount(agent_c_->localaddr());

    xs_x_->ClearAllConnections();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->localaddr()) > flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->localaddr()) > flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->localaddr()) > flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
}

TEST_F(BgpXmppBasicTest, ClearConnection) {
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    int flap_a = GetXmppConnectionFlapCount(agent_a_->localaddr());
    int flap_b = GetXmppConnectionFlapCount(agent_b_->localaddr());
    int flap_c = GetXmppConnectionFlapCount(agent_c_->localaddr());

    EXPECT_TRUE(xs_x_->ClearConnection("agent-b"));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->localaddr()) > flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->localaddr()) == flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->localaddr()) == flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
}

TEST_F(BgpXmppBasicTest, ClearNonExistentConnection) {
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    EXPECT_FALSE(xs_x_->ClearConnection("agent-bx"));
    EXPECT_FALSE(xs_x_->ClearConnection("agent-"));
    EXPECT_FALSE(xs_x_->ClearConnection("all"));
    EXPECT_FALSE(xs_x_->ClearConnection("*"));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
}

TEST_F(BgpXmppBasicTest, ChangeAsNumber) {
    Configure(bgp_config_template, 64512);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(64512, bs_x_->autonomous_system());

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    int flap_a = GetXmppConnectionFlapCount(agent_a_->localaddr());
    int flap_b = GetXmppConnectionFlapCount(agent_b_->localaddr());
    int flap_c = GetXmppConnectionFlapCount(agent_c_->localaddr());

    Configure(bgp_config_template, 64513);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(64513, bs_x_->autonomous_system());

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->localaddr()) > flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->localaddr()) > flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->localaddr()) > flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
    virtual void SetUp() {
    }
    virtual void TearDown() {
    }
};

static void SetUp() {
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    XmppObjectFactory::Register<XmppStateMachine>(
        boost::factory<XmppStateMachineTest *>());
}

static void TearDown() {
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
