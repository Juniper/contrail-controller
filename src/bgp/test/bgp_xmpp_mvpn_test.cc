/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "bgp/bgp_factory.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_xmpp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/mvpn/mvpn_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "io/test/event_manager_test.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "schema/xmpp_mvpn_types.h"
#include "xmpp/xmpp_factory.h"
#include "xmpp/test/xmpp_test_util.h"

using namespace autogen;
using namespace boost::asio;
using namespace boost::assign;
using namespace std;
using namespace test;

class BgpXmppMvpnTest : public ::testing::Test {
protected:
    BgpXmppMvpnTest() : thread_(&evm_), xs_x_(NULL) { }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        xs_x_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        bs_x_->session_manager()->Initialize(0);
        xs_x_->Initialize(0, false);
        bcm_x_.reset(new BgpXmppChannelManager(xs_x_, bs_x_.get()));

        thread_.Start();
    }

    virtual void TearDown() {
        xs_x_->Shutdown();
        task_util::WaitForIdle();
        bs_x_->Shutdown();
        task_util::WaitForIdle();
        bcm_x_.reset();
        TcpServerManager::DeleteServer(xs_x_);
        xs_x_ = NULL;

        agent_xa_->Delete();
        agent_xb_->Delete();
        agent_xc_->Delete();

        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    virtual void SessionUp() {
        agent_xa_.reset(new test::NetworkAgentMock(
            &evm_, "agent-xa", xs_x_->GetPort(), "127.0.0.1", "127.0.0.101"));
        TASK_UTIL_EXPECT_TRUE(agent_xa_->IsEstablished());
        agent_xb_.reset(new test::NetworkAgentMock(
            &evm_, "agent-xb", xs_x_->GetPort(), "127.0.0.2", "127.0.0.101"));
        TASK_UTIL_EXPECT_TRUE(agent_xb_->IsEstablished());
        agent_xc_.reset(new test::NetworkAgentMock(
            &evm_, "agent-xc", xs_x_->GetPort(), "127.0.0.3", "127.0.0.101"));
        TASK_UTIL_EXPECT_TRUE(agent_xc_->IsEstablished());
    }

    virtual void SessionDown() {
        agent_xa_->SessionDown();
        agent_xb_->SessionDown();
        agent_xc_->SessionDown();
        task_util::WaitForIdle();
    }

    virtual void Subscribe(const string net, int id) {
        agent_xa_->MvpnSubscribe(net, id);
        agent_xb_->MvpnSubscribe(net, id);
        agent_xc_->MvpnSubscribe(net, id);
        task_util::WaitForIdle();
    }

    virtual void Configure(const char *config_tmpl) {
        char config[8192];
        snprintf(config, sizeof(config), config_tmpl,
            bs_x_->session_manager()->GetPort());
        bs_x_->Configure(config);
    }

    size_t GetVrfTableSize(BgpServerTestPtr server, const string &name) {
        RoutingInstanceMgr *rim = server->routing_instance_mgr();
        TASK_UTIL_EXPECT_TRUE(rim->GetRoutingInstance(name) != NULL);
        const RoutingInstance *rtinstance = rim->GetRoutingInstance(name);
        TASK_UTIL_EXPECT_TRUE(rtinstance->GetTable(Address::MVPN) != NULL);
        const BgpTable *table = rtinstance->GetTable(Address::MVPN);
        return table->Size();
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    XmppServer *xs_x_;
    boost::scoped_ptr<BgpXmppChannelManager> bcm_x_;
    boost::shared_ptr<test::NetworkAgentMock> agent_xa_;
    boost::shared_ptr<test::NetworkAgentMock> agent_xb_;
    boost::shared_ptr<test::NetworkAgentMock> agent_xc_;

    static int validate_done_;
};

int BgpXmppMvpnTest::validate_done_;

static const char *config_tmpl1 = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.101</identifier>\
        <address>127.0.0.101</address>\
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

class BgpXmppMvpnErrorTest : public BgpXmppMvpnTest {
protected:
    virtual void SetUp() {
        BgpXmppMvpnTest::SetUp();

        Configure(config_tmpl1);
        task_util::WaitForIdle();

        BgpXmppMvpnTest::SessionUp();
        BgpXmppMvpnTest::Subscribe("blue", 1);
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        BgpXmppMvpnTest::SessionDown();
        BgpXmppMvpnTest::TearDown();
    }
};

TEST_F(BgpXmppMvpnErrorTest, BadGroupAddress) {
    agent_xa_->AddMvpnRoute("blue", "225.0.0,90.1.1.1");
    task_util::WaitForIdle();
    MvpnTable *blue_table_ = static_cast<MvpnTable *>(
        bs_x_->database()->FindTable("blue.mvpn.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
    TASK_UTIL_EXPECT_EQ(0, GetVrfTableSize(bs_x_, "blue"));
}

TEST_F(BgpXmppMvpnErrorTest, BadSourceAddress) {
    agent_xa_->AddMvpnRoute("blue", "225.0.0.1,90.1.1");
    task_util::WaitForIdle();
    MvpnTable *blue_table_ = static_cast<MvpnTable *>(
        bs_x_->database()->FindTable("blue.mvpn.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

class BgpXmppMvpnSubscriptionTest : public BgpXmppMvpnTest {
protected:
    static const int kTimeoutSeconds = 15;
    virtual void SetUp() {
        BgpXmppMvpnTest::SetUp();

        Configure(config_tmpl1);
        task_util::WaitForIdle();
        BgpXmppMvpnTest::SessionUp();
    }

    virtual void TearDown() {
        BgpXmppMvpnTest::SessionDown();
        BgpXmppMvpnTest::TearDown();
    }
};

TEST_F(BgpXmppMvpnSubscriptionTest, PendingSubscribe) {
    const char *mroute = "225.0.0.1,20.1.1.10";

    // Register agent a to the multicast table and add a mvpn route
    // without waiting for the subscription to be processed.
    agent_xa_->MvpnSubscribe("blue", 1);
    agent_xa_->AddMvpnRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify that the route gets added
    WAIT_FOR(1000, 100, 1 == GetVrfTableSize(bs_x_, "blue"));

    // Add the route again, there should still be only 1 route
    agent_xb_->MvpnSubscribe("blue", 1);
    agent_xb_->AddMvpnRoute("blue", mroute);
    agent_xa_->AddMvpnRoute("blue", mroute);
    task_util::WaitForIdle();
    WAIT_FOR(1000, 100, 1 == GetVrfTableSize(bs_x_, "blue"));

    // Delete mvpn route from one agent, there should still be a route
    agent_xa_->DeleteMvpnRoute("blue", mroute);
    task_util::WaitForIdle();
    WAIT_FOR(1000, 100, 1 == GetVrfTableSize(bs_x_, "blue"));

    // Delete route from second agent, it should get deleted
    agent_xb_->DeleteMvpnRoute("blue", mroute);
    task_util::WaitForIdle();
    WAIT_FOR(1000, 100, 0 == GetVrfTableSize(bs_x_, "blue"));
}

TEST_F(BgpXmppMvpnSubscriptionTest, PendingUnsubscribe) {
    const char *mroute = "225.0.0.1,10.1.1.10";

    // Register agent a to the multicast table and add a mvpn route
    // without waiting for the subscription to be processed. Then go
    // ahead and unsubscribe right away.
    agent_xa_->MvpnSubscribe("blue", 1);
    agent_xa_->AddMvpnRoute("blue", mroute);
    agent_xa_->MvpnUnsubscribe("blue");
    task_util::WaitForIdle();

    // Verify number of routes.
    WAIT_FOR(1000, 100, 0 == GetVrfTableSize(bs_x_, "blue"));
}

TEST_F(BgpXmppMvpnSubscriptionTest, SubsequentSubscribeUnsubscribe) {
    const char *mroute = "225.0.0.1,10.1.1.10";

    // Register agent b to the multicast table and add a mvpn route
    // after waiting for the subscription to be processed.
    agent_xb_->MvpnSubscribe("blue", 1);
    task_util::WaitForIdle();
    agent_xb_->AddMvpnRoute("blue", mroute);

    // Register agent a to the multicast table and add a mvpn route
    // without waiting for the subscription to be processed. Then go
    // ahead and unsubscribe right away. Then subscribe again with a
    // different id and add the route again.
    agent_xa_->MvpnSubscribe("blue", 1);
    agent_xa_->AddMvpnRoute("blue", mroute);
    agent_xa_->MvpnUnsubscribe("blue");
    agent_xa_->MvpnSubscribe("blue", 2);
    agent_xa_->AddMvpnRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    WAIT_FOR(1000, 100, 1 == GetVrfTableSize(bs_x_, "blue"));

    // Verify that agent a mvpn route was added.
    const char *route = "7-0:0,0,10.1.1.10,225.0.0.1";
    MvpnPrefix prefix(MvpnPrefix::FromString(route));
    MvpnTable::RequestKey key(prefix, NULL);
    MvpnTable *blue_table_ = static_cast<MvpnTable *>(
        bs_x_->database()->FindTable("blue.mvpn.0"));
    TASK_UTIL_EXPECT_TRUE(
        dynamic_cast<MvpnRoute *>(blue_table_->Find(&key)) != NULL);

    // Delete mvpn route for all agents.
    agent_xa_->DeleteMvpnRoute("blue", mroute);
    agent_xb_->DeleteMvpnRoute("blue", mroute);
    task_util::WaitForIdle();
};

class BgpXmppMvpnMultiAgentTest : public BgpXmppMvpnTest {
protected:
    virtual void SetUp() {
        BgpXmppMvpnTest::SetUp();

        Configure(config_tmpl1);
        task_util::WaitForIdle();

        BgpXmppMvpnTest::SessionUp();
        BgpXmppMvpnTest::Subscribe("blue", 1);
    }

    virtual void TearDown() {
        BgpXmppMvpnTest::SessionDown();
        BgpXmppMvpnTest::TearDown();
    }
};

TEST_F(BgpXmppMvpnMultiAgentTest, MultipleRoutes) {
    const char *mroute_list[] = {
        "225.0.0.1,90.1.1.1",
        "225.0.0.1,90.1.1.2",
        "225.0.0.2,90.1.1.1",
        "225.0.0.2,90.1.1.2"
    };

    // Add mvpn routes for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->AddMvpnRoute("blue", mroute);
        agent_xb_->AddMvpnRoute("blue", mroute);
        task_util::WaitForIdle();
    }

    // Verify that all routes are added once.
    WAIT_FOR(1000, 100, GetVrfTableSize(bs_x_, "blue") == sizeof(mroute_list) /
	    sizeof(mroute_list[0]));

    // Delete mvpn route for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->DeleteMvpnRoute("blue", mroute);
        agent_xb_->DeleteMvpnRoute("blue", mroute);
        task_util::WaitForIdle();
    }
};

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
    SetUp();
    int result = RUN_ALL_TESTS();
    TearDown();

    return result;
}
