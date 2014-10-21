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
    BgpXmppBasicTest() : thread_(&evm_), xs_x_(NULL), xltm_x_(NULL) { }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        bs_x_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created BGP server at port: " <<
            bs_x_->session_manager()->GetPort());
        xs_x_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        xs_x_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            xs_x_->GetPort());
        xltm_x_ =
            dynamic_cast<XmppLifetimeManagerTest *>(xs_x_->lifetime_manager());
        assert(xltm_x_ != NULL);
        cm_x_.reset(new BgpXmppChannelManager(xs_x_, bs_x_.get()));
        thread_.Start();
    }

    virtual void TearDown() {
        xs_x_->Shutdown();
        task_util::WaitForIdle();
        bs_x_->Shutdown();
        task_util::WaitForIdle();
        cm_x_.reset();

        TcpServerManager::DeleteServer(xs_x_);
        xs_x_ = NULL;

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void CreateAgents() {
        agent_a_.reset(
            new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
                agent_a_addr_, "127.0.0.1"));
        agent_b_.reset(
            new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
                agent_b_addr_, "127.0.0.1"));
        agent_c_.reset(
            new test::NetworkAgentMock(&evm_, "agent-c", xs_x_->GetPort(),
                agent_c_addr_, "127.0.0.1"));
    }

    void DestroyAgents() {
        agent_a_->Delete();
        agent_b_->Delete();
        agent_c_->Delete();
    }

    void Configure(const char *cfg_template, int asn) {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
                 asn, bs_x_->session_manager()->GetPort());
        bs_x_->Configure(config);
    }

    int GetXmppConnectionFlapCount(const string &hostname) {
        TASK_UTIL_EXPECT_TRUE(xs_x_->FindConnectionEndpoint(hostname) != NULL);
        const XmppConnectionEndpoint *conn_endpoint =
            xs_x_->FindConnectionEndpoint(hostname);
        return conn_endpoint->flap_count();
    }

    size_t GetConnectionQueueSize(XmppServer *xs) {
        return xs->GetQueueSize();
    }

    void SetConnectionQueueDisable(XmppServer *xs, bool flag) {
        xs->SetQueueDisable(flag);
    }

    void SetLifetimeManagerDestroyDisable(bool disabled) {
        xltm_x_->set_destroy_not_ok(disabled);
    }

    void SetLifetimeManagerQueueDisable(bool disabled) {
        xltm_x_->SetQueueDisable(disabled);
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    XmppServer *xs_x_;
    XmppLifetimeManagerTest *xltm_x_;
    string agent_a_addr_;
    string agent_b_addr_;
    string agent_c_addr_;
    string agent_x1_addr_;
    string agent_x2_addr_;
    string agent_x3_addr_;
    test::NetworkAgentMockPtr agent_a_;
    test::NetworkAgentMockPtr agent_b_;
    test::NetworkAgentMockPtr agent_c_;
    test::NetworkAgentMockPtr agent_x1_;
    test::NetworkAgentMockPtr agent_x2_;
    test::NetworkAgentMockPtr agent_x3_;
    boost::scoped_ptr<BgpXmppChannelManager> cm_x_;
};

// Parameterize shared vs unique IP for each agent.

class BgpXmppBasicParamTest : public BgpXmppBasicTest,
    public ::testing::WithParamInterface<bool> {
protected:
    virtual void SetUp() {
        if (GetParam()) {
            agent_a_addr_ = "127.0.0.1";
            agent_b_addr_ = "127.0.0.1";
            agent_c_addr_ = "127.0.0.1";
        } else {
            agent_a_addr_ = "127.0.0.11";
            agent_b_addr_ = "127.0.0.12";
            agent_c_addr_ = "127.0.0.13";
        }
        BgpXmppBasicTest::SetUp();
    }

    virtual void TearDown() {
        BgpXmppBasicTest::TearDown();
    }
};

TEST_P(BgpXmppBasicParamTest, ClearAllConnections) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    int client_flap_a = agent_a_->flap_count();
    int client_flap_b = agent_b_->flap_count();
    int client_flap_c = agent_c_->flap_count();

    int server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    int server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    int server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    TaskScheduler::GetInstance()->Stop();
    xs_x_->ClearAllConnections();
    TaskScheduler::GetInstance()->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() > client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) > server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) > server_flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ClearConnection) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    int client_flap_a = agent_a_->flap_count();
    int client_flap_b = agent_b_->flap_count();
    int client_flap_c = agent_c_->flap_count();

    int server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    int server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    int server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    TaskScheduler::GetInstance()->Stop();
    EXPECT_TRUE(xs_x_->ClearConnection("agent-b"));
    TaskScheduler::GetInstance()->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() == client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() == client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) == server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) == server_flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ClearNonExistentConnection) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    int client_flap_a = agent_a_->flap_count();
    int client_flap_b = agent_b_->flap_count();
    int client_flap_c = agent_c_->flap_count();

    int server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    int server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    int server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    TaskScheduler::GetInstance()->Stop();
    EXPECT_FALSE(xs_x_->ClearConnection("agent-bx"));
    EXPECT_FALSE(xs_x_->ClearConnection("agent-"));
    EXPECT_FALSE(xs_x_->ClearConnection("all"));
    EXPECT_FALSE(xs_x_->ClearConnection("*"));
    TaskScheduler::GetInstance()->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() == client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() == client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() == client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) == server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) == server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) == server_flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ChangeAsNumber) {
    Configure(bgp_config_template, 64512);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(64512, bs_x_->autonomous_system());

    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    int client_flap_a = agent_a_->flap_count();
    int client_flap_b = agent_b_->flap_count();
    int client_flap_c = agent_c_->flap_count();

    int server_flap_a = GetXmppConnectionFlapCount(agent_a_->hostname());
    int server_flap_b = GetXmppConnectionFlapCount(agent_b_->hostname());
    int server_flap_c = GetXmppConnectionFlapCount(agent_c_->hostname());

    Configure(bgp_config_template, 64513);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(64513, bs_x_->autonomous_system());

    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > client_flap_a);
    TASK_UTIL_EXPECT_TRUE(agent_b_->flap_count() > client_flap_b);
    TASK_UTIL_EXPECT_TRUE(agent_c_->flap_count() > client_flap_c);

    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_a_->hostname()) > server_flap_a);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_b_->hostname()) > server_flap_b);
    TASK_UTIL_EXPECT_TRUE(
        GetXmppConnectionFlapCount(agent_c_->hostname()) > server_flap_c);

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ShutdownServer1) {

    // Create agents and wait for them to come up.
    CreateAgents();
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    // Shutdown the server.
    xs_x_->Shutdown();

    // Agents should fail to connect.
    TASK_UTIL_EXPECT_TRUE(agent_a_->get_connect_error() >= 3);
    TASK_UTIL_EXPECT_TRUE(agent_b_->get_connect_error() >= 3);
    TASK_UTIL_EXPECT_TRUE(agent_c_->get_connect_error() >= 3);

    // Check that the queue does not build up and agents don't come up.
    TASK_UTIL_EXPECT_EQ(0, GetConnectionQueueSize(xs_x_));
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(0, xs_x_->ConnectionCount());
    TASK_UTIL_EXPECT_FALSE(xs_x_->deleter()->HasDependents());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ShutdownServer2) {

    // Shutdown the server and create agents.
    xs_x_->Shutdown();
    CreateAgents();

    // Agents should fail to connect.
    TASK_UTIL_EXPECT_TRUE(agent_a_->get_connect_error() >= 3);
    TASK_UTIL_EXPECT_TRUE(agent_b_->get_connect_error() >= 3);
    TASK_UTIL_EXPECT_TRUE(agent_c_->get_connect_error() >= 3);

    // Check that the queue does not build up and agents don't come up.
    TASK_UTIL_EXPECT_EQ(0, GetConnectionQueueSize(xs_x_));
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(0, xs_x_->ConnectionCount());
    TASK_UTIL_EXPECT_FALSE(xs_x_->deleter()->HasDependents());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, ShutdownServer3) {

    // Create agents, wait for a little bit and shutdown the server.
    // Idea is that agents may or may not have come up, sessions and
    // connections may have been queued etc.
    CreateAgents();
    usleep(15000);
    xs_x_->Shutdown();

    // Agents should fail to connect.
    TASK_UTIL_EXPECT_TRUE(agent_a_->get_connect_error() >= 3);
    TASK_UTIL_EXPECT_TRUE(agent_b_->get_connect_error() >= 3);
    TASK_UTIL_EXPECT_TRUE(agent_c_->get_connect_error() >= 3);

    // Check that the queue does not build up and agents don't come up.
    TASK_UTIL_EXPECT_EQ(0, GetConnectionQueueSize(xs_x_));
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(0, xs_x_->ConnectionCount());
    TASK_UTIL_EXPECT_FALSE(xs_x_->deleter()->HasDependents());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, DisableConnectionQueue1) {

    // Disable the connection queue and create agents.
    SetConnectionQueueDisable(xs_x_, true);
    CreateAgents();

    // Check that the queue has built up and agents haven't come up.
    TASK_UTIL_EXPECT_TRUE(GetConnectionQueueSize(xs_x_) >= 3);
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());

    // Enable the connection queue.
    SetConnectionQueueDisable(xs_x_, false);

    // Wait for queue to get drained and all agents to come up.
    TASK_UTIL_EXPECT_EQ(0, GetConnectionQueueSize(xs_x_));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(3, xs_x_->ConnectionCount());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, DisableConnectionQueue2) {

    // Disable the connection queue and create agents.
    SetConnectionQueueDisable(xs_x_, true);
    CreateAgents();

    // Check that the queue has built up and agents haven't come up.
    TASK_UTIL_EXPECT_TRUE(GetConnectionQueueSize(xs_x_) >= 3);
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());

    // Bounce the agents a few times and verify that the queue builds up.
    for (int idx = 0; idx < 3; ++idx) {
        size_t queue_size = GetConnectionQueueSize(xs_x_);
        agent_a_->SessionDown();
        agent_b_->SessionDown();
        agent_c_->SessionDown();
        agent_a_->SessionUp();
        agent_b_->SessionUp();
        agent_c_->SessionUp();
        TASK_UTIL_EXPECT_TRUE(GetConnectionQueueSize(xs_x_) >= queue_size + 3);
    }

    // Enable the connection queue.
    SetConnectionQueueDisable(xs_x_, false);

    // Wait for queue to get drained and all agents to come up.
    TASK_UTIL_EXPECT_EQ(0, GetConnectionQueueSize(xs_x_));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(3, xs_x_->ConnectionCount());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, DisableConnectionQueue3) {

    // Disable the connection queue and create agents.
    SetConnectionQueueDisable(xs_x_, true);
    CreateAgents();

    // Check that the queue has built up and agents haven't come up.
    TASK_UTIL_EXPECT_TRUE(GetConnectionQueueSize(xs_x_) >= 3);
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());

    // Shutdown the server and verify that the queue and connections
    // don't go away.
    xs_x_->Shutdown();
    size_t queue_size = GetConnectionQueueSize(xs_x_);
    TASK_UTIL_EXPECT_TRUE(xs_x_->deleter()->HasDependents());
    usleep(50000);
    TASK_UTIL_EXPECT_EQ(queue_size, GetConnectionQueueSize(xs_x_));
    TASK_UTIL_EXPECT_TRUE(xs_x_->deleter()->HasDependents());

    // Enable the connection queue.
    SetConnectionQueueDisable(xs_x_, false);

    // Verify that the queue gets drained and all connections are gone.
    TASK_UTIL_EXPECT_EQ(0, GetConnectionQueueSize(xs_x_));
    TASK_UTIL_EXPECT_EQ(0, xs_x_->ConnectionCount());
    TASK_UTIL_EXPECT_FALSE(xs_x_->deleter()->HasDependents());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, DisableConnectionDestroy) {

    // Create and bring up agents.
    CreateAgents();
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(3, xs_x_->ConnectionCount());

    // Disable destroy of xmpp managed objects.
    SetLifetimeManagerDestroyDisable(true);

    // Clear all connections.
    TaskScheduler::GetInstance()->Stop();
    xs_x_->ClearAllConnections();
    TaskScheduler::GetInstance()->Start();

    // Verify that the connection count goes up.  The still to be destroyed
    // connections should be in the set, but new connections should come up.
    TASK_UTIL_EXPECT_TRUE(xs_x_->ConnectionCount() >= 6);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    // Enable destroy of xmpp managed objects.
    SetLifetimeManagerDestroyDisable(false);

    // Verify that there are no connections waiting to be destroyed.
    TASK_UTIL_EXPECT_EQ(3, xs_x_->ConnectionCount());
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest, DisableConnectionShutdown) {

    // Create and bring up agents.
    CreateAgents();
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(3, xs_x_->ConnectionCount());

    // Disable xmpp lifetime manager queue processing.
    SetLifetimeManagerQueueDisable(true);

    // Clear all connections.
    TaskScheduler::GetInstance()->Stop();
    xs_x_->ClearAllConnections();
    TaskScheduler::GetInstance()->Start();

    // Verify that the connection count goes up.  The still to be shutdown
    // connections should be in the map, and should prevent new connections
    // from the same endpoint.
    TASK_UTIL_EXPECT_TRUE(xs_x_->ConnectionCount() >= 9);
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_FALSE(agent_c_->IsEstablished());

    // Enable xmpp lifetime manager queue processing.
    SetLifetimeManagerQueueDisable(false);

    // Verify that there are no connections waiting to be shutdown.
    TASK_UTIL_EXPECT_EQ(3, xs_x_->ConnectionCount());
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    DestroyAgents();
}

// Parameterize shared vs unique IP for each agent.

class BgpXmppBasicParamTest2 : public BgpXmppBasicTest,
    public ::testing::WithParamInterface<bool> {
protected:
    virtual void SetUp() {
        if (GetParam()) {
            agent_x1_addr_ = "127.0.0.1";
            agent_x2_addr_ = "127.0.0.1";
            agent_x3_addr_ = "127.0.0.1";
        } else {
            agent_x1_addr_ = "127.0.0.21";
            agent_x2_addr_ = "127.0.0.22";
            agent_x3_addr_ = "127.0.0.23";
        }
        BgpXmppBasicTest::SetUp();
    }

    virtual void TearDown() {
        BgpXmppBasicTest::TearDown();
    }

    void CreateAgents() {
        agent_x1_.reset(
            new test::NetworkAgentMock(&evm_, "agent-x", xs_x_->GetPort(),
                agent_x1_addr_, "127.0.0.1"));
        agent_x1_->SessionDown();
        agent_x2_.reset(
            new test::NetworkAgentMock(&evm_, "agent-x", xs_x_->GetPort(),
                agent_x2_addr_, "127.0.0.1"));
        agent_x2_->SessionDown();
        agent_x3_.reset(
            new test::NetworkAgentMock(&evm_, "agent-x", xs_x_->GetPort(),
                agent_x3_addr_, "127.0.0.1"));
        agent_x3_->SessionDown();
    }

    void DestroyAgents() {
        agent_x1_->Delete();
        agent_x2_->Delete();
        agent_x3_->Delete();
    }
};

TEST_P(BgpXmppBasicParamTest2, DuplicateEndpointName1) {
    CreateAgents();

    // Bring up one agent with given name.
    agent_x1_->SessionUp();
    TASK_UTIL_EXPECT_TRUE(agent_x1_->IsEstablished());

    int client_x1_session_close = agent_x1_->get_session_close();
    int client_x2_session_close = agent_x2_->get_session_close();
    int client_x3_session_close = agent_x3_->get_session_close();

    // Attempt to bring up two more agents with the same name.
    agent_x2_->SessionUp();
    agent_x3_->SessionUp();

    // Make sure that latter two agents see sessions getting closed.
    TASK_UTIL_EXPECT_TRUE(
        agent_x2_->get_session_close() >= client_x2_session_close + 3);
    TASK_UTIL_EXPECT_TRUE(
        agent_x3_->get_session_close() >= client_x3_session_close + 3);
    TASK_UTIL_EXPECT_TRUE(
        agent_x1_->get_session_close() == client_x1_session_close);

    DestroyAgents();
}

TEST_P(BgpXmppBasicParamTest2, DuplicateEndpointName2) {
    CreateAgents();

    // Bring up one agent with given name.
    agent_x1_->SessionUp();
    TASK_UTIL_EXPECT_TRUE(agent_x1_->IsEstablished());

    int client_x1_session_close = agent_x1_->get_session_close();
    int client_x2_session_close = agent_x2_->get_session_close();

    // Attempt to bring up another agent with the same name.
    agent_x2_->SessionUp();

    // Make sure that second agent sees sessions getting closed.
    TASK_UTIL_EXPECT_TRUE(
        agent_x2_->get_session_close() >= client_x2_session_close + 3);
    TASK_UTIL_EXPECT_TRUE(
        agent_x1_->get_session_close() == client_x1_session_close);

    // Bring down the first agent and make sure that second comes up.
    agent_x1_->SessionDown();
    TASK_UTIL_EXPECT_TRUE(agent_x2_->IsEstablished());

    DestroyAgents();
}

INSTANTIATE_TEST_CASE_P(Instance, BgpXmppBasicParamTest, ::testing::Bool());

INSTANTIATE_TEST_CASE_P(Instance, BgpXmppBasicParamTest2, ::testing::Bool());

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
    XmppObjectFactory::Register<XmppLifetimeManager>(
        boost::factory<XmppLifetimeManagerTest *>());
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
