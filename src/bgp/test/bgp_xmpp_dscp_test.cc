/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
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

using std::string;

static const char *bgp_config_template = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <autonomous-system>1</autonomous-system>\
        <local-autonomous-system>2</local-autonomous-system>\
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

static const char *bgp_unconfig = "\
<delete>\
    <bgp-router name=\'X\'>\
    </bgp-router>\
</delete>\
";

static const char *bgp_dscp_config_template = "\
<config>\
    <global-qos-config>\
    <control-traffic-dscp>\
          <control>%d</control>\
          <analytics>56</analytics>\
          <dns>65</dns>\
    </control-traffic-dscp>\
    </global-qos-config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <autonomous-system>1</autonomous-system>\
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
class BgpXmppDscpTest : public ::testing::Test {
public:

    BgpXmppDscpTest() :
        thread_(&evm_),
        xs_x_(NULL),
        xltm_x_(NULL) {
    }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        bs_x_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created BGP server at port: " <<
            bs_x_->session_manager()->GetPort());
        Configure();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_TRUE(bs_x_->HasSelfConfiguration());

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
                "127.0.0.11", "127.0.0.1", false));
        agent_b_.reset(
            new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
                "127.0.0.12", "127.0.0.1", false));
        agent_c_.reset(
            new test::NetworkAgentMock(&evm_, "agent-c", xs_x_->GetPort(),
                "127.0.0.13", "127.0.0.1", false));
    }

    void DestroyAgents() {
        agent_a_->Delete();
        agent_b_->Delete();
        agent_c_->Delete();
    }

    void Configure() {
        char config[4096];
        snprintf(config, sizeof(config), bgp_config_template,
                 bs_x_->session_manager()->GetPort());
        bs_x_->Configure(config);
    }

    void Unconfigure() {
        bs_x_->Configure(bgp_unconfig);
    }

    void ConfigureDscp(uint8_t dscp) {
        char config[8192];
        snprintf(config, sizeof(config), bgp_dscp_config_template, dscp,
                 bs_x_->session_manager()->GetPort());
        bs_x_->Configure(config);
    }

    bool VerifySessionDscp(const string &host, uint8_t dscp) {
        TASK_UTIL_EXPECT_TRUE(xs_x_->FindConnectionEndpoint(host) != NULL);
        const XmppConnectionEndpoint *conn_endpoint =
            xs_x_->FindConnectionEndpoint(host);
        const XmppConnection *con = conn_endpoint->connection();
        EXPECT_TRUE((con != NULL));
        const XmppSession *sess = con->session();
        uint8_t value = sess->GetDscpValue();
        EXPECT_TRUE((value == dscp));
        return true;
    }

protected:
    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    XmppServer *xs_x_;
    XmppLifetimeManagerTest *xltm_x_;
    test::NetworkAgentMockPtr agent_a_;
    test::NetworkAgentMockPtr agent_b_;
    test::NetworkAgentMockPtr agent_c_;
    boost::scoped_ptr<BgpXmppChannelManager> cm_x_;
};

TEST_F(BgpXmppDscpTest, VerifyBgpXmppDscp_default) {
    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    //Verify DSCP value in XmppSession is set to default value of 0
    VerifySessionDscp(agent_a_->hostname(), 0);
    VerifySessionDscp(agent_b_->hostname(), 0);
    VerifySessionDscp(agent_c_->hostname(), 0);

    DestroyAgents();
}

TEST_F(BgpXmppDscpTest, VerifyBgpXmppDscp_change) {
    ConfigureDscp(44);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(44, bs_x_->global_qos()->control_dscp());
    TASK_UTIL_EXPECT_EQ(44, xs_x_->dscp_value());

    CreateAgents();

    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    //Verify DSCP value in XmppSession is set to configured value
    VerifySessionDscp(agent_a_->hostname(), 44);
    VerifySessionDscp(agent_b_->hostname(), 44);
    VerifySessionDscp(agent_c_->hostname(), 44);

    ConfigureDscp(60);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(60, bs_x_->global_qos()->control_dscp());
    TASK_UTIL_EXPECT_EQ(60, xs_x_->dscp_value());

    //Verify DSCP value in XmppSession is set to configured value
    VerifySessionDscp(agent_a_->hostname(), 60);
    VerifySessionDscp(agent_b_->hostname(), 60);
    VerifySessionDscp(agent_c_->hostname(), 60);

    ConfigureDscp(0);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, bs_x_->global_qos()->control_dscp());
    TASK_UTIL_EXPECT_EQ(0, xs_x_->dscp_value());

    //Verify DSCP value in XmppSession is set to configured value
    VerifySessionDscp(agent_a_->hostname(), 0);
    VerifySessionDscp(agent_b_->hostname(), 0);
    VerifySessionDscp(agent_c_->hostname(), 0);

    ConfigureDscp(44);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(44, bs_x_->global_qos()->control_dscp());
    TASK_UTIL_EXPECT_EQ(44, xs_x_->dscp_value());

    //Verify DSCP value in XmppSession is set to configured value
    VerifySessionDscp(agent_a_->hostname(), 44);
    VerifySessionDscp(agent_b_->hostname(), 44);
    VerifySessionDscp(agent_c_->hostname(), 44);

    DestroyAgents();
    task_util::WaitForIdle();

    Unconfigure();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(bs_x_->HasSelfConfiguration());
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
