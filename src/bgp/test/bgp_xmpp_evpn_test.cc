/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>

#include "base/test/task_test_util.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/enet/enet_route.h"
#include "bgp/enet/enet_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "schema/xmpp_enet_types.h"
#include "testing/gunit.h"
#include "xmpp/xmpp_server.h"

using namespace std;

//
// Fire state machine timers faster and reduce possible delay in this test
//
class StateMachineTest : public StateMachine {
public:
    explicit StateMachineTest(BgpPeer *peer) : StateMachine(peer) { }
    ~StateMachineTest() { }

    void StartConnectTimer(int seconds) {
        connect_timer_->Start(10,
            boost::bind(&StateMachine::ConnectTimerExpired, this),
            boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
    }

    void StartOpenTimer(int seconds) {
        open_timer_->Start(10,
            boost::bind(&StateMachine::OpenTimerExpired, this),
            boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
    }

    void StartIdleHoldTimer() {
        if (idle_hold_time_ <= 0)
            return;

        idle_hold_timer_->Start(10,
            boost::bind(&StateMachine::IdleHoldTimerExpired, this),
            boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
    }
};

class BgpXmppChannelMock : public BgpXmppChannel {
public:
    BgpXmppChannelMock(XmppChannel *channel, BgpServer *server, 
            BgpXmppChannelManager *manager) : 
        BgpXmppChannel(channel, server, manager), count_(0) {
            bgp_policy_ = RibExportPolicy(BgpProto::XMPP,
                                          RibExportPolicy::XMPP, -1, 0);
    }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        count_ ++;
        BgpXmppChannel::ReceiveUpdate(msg);
    }

    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }
    virtual ~BgpXmppChannelMock() { }

private:
    size_t count_;
};

class BgpXmppChannelManagerMock : public BgpXmppChannelManager {
public:
    BgpXmppChannelManagerMock(XmppServer *x, BgpServer *b) :
        BgpXmppChannelManager(x, b), count(0), channels(0) { }

    virtual void XmppHandleChannelEvent(XmppChannel *channel,
                                        xmps::PeerState state) {
         count++;
         BgpXmppChannelManager::XmppHandleChannelEvent(channel, state);
    }

    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel) {
        channel_[channels] = new BgpXmppChannelMock(channel, bgp_server_, this);
        channels++;
        return channel_[channels-1];
    }

    int Count() {
        return count;
    }
    int count;
    int channels;
    BgpXmppChannelMock *channel_[2];
};

static const autogen::EnetItemType *VerifyRouteUpdated(
    test::NetworkAgentMockPtr agent, const string net, const string prefix,
    const boost::crc_32_type &old_crc) {
    static int max_retry = 10000;

    int count = 0;
    autogen::EnetItemType *rt;
    boost::crc_32_type rt_crc;
    do {
        rt = const_cast<autogen::EnetItemType *>(agent->EnetRouteLookup(
            net, prefix));
        if (rt)
            rt->CalculateCrc(&rt_crc);
        usleep(1000);
    } while ((!rt || rt_crc.checksum() == old_crc.checksum()) &&
        (count++ < max_retry));

    EXPECT_NE(max_retry, count);
    return rt;
}

static const char *config_template_11 = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <virtual-network name='pink'>\
        <network-id>2</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
    <routing-instance name='pink'>\
        <virtual-network>pink</virtual-network>\
        <vrf-target>target:1:2</vrf-target>\
    </routing-instance>\
</config>\
";

//
// Single Control Node X.
// Agents A and B.
//
class BgpXmppEvpnTest1 : public ::testing::Test {
protected:
    BgpXmppEvpnTest1() : thread_(&evm_) { }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        xs_x_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);

        bs_x_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created server at port: " << 
            bs_x_->session_manager()->GetPort());
        xs_x_->Initialize(0, false);

        bgp_channel_manager_.reset(
            new BgpXmppChannelManagerMock(xs_x_, bs_x_.get()));

        thread_.Start();
    }

    virtual void TearDown() {
        xs_x_->Shutdown();
        task_util::WaitForIdle();
        bs_x_->Shutdown();
        task_util::WaitForIdle();
        bgp_channel_manager_.reset();
        TcpServerManager::DeleteServer(xs_x_);
        xs_x_ = NULL;
        if (agent_a_) { agent_a_->Delete(); }
        if (agent_b_) { agent_b_->Delete(); }
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void Configure(const char *cfg_template = config_template_11) {
        bs_x_->Configure(cfg_template);
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    XmppServer *xs_x_;
    test::NetworkAgentMockPtr agent_a_;
    test::NetworkAgentMockPtr agent_b_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> bgp_channel_manager_;
};

//
// Single agent.
// Add one route and verify the next hop and label.
//
TEST_F(BgpXmppEvpnTest1, 1AgentRouteAdd) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register to blue instance.
    agent_a_->EnetSubscribe("blue", 1);

    // Add route from agent.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route showed up on agent.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    const autogen::EnetItemType *rt =
        agent_a_->EnetRouteLookup("blue", "aa:00:00:00:00:01,10.1.1.1/32");
    TASK_UTIL_EXPECT_TRUE(rt != NULL);
    int label = rt->entry.next_hops.next_hop[0].label;
    string nh = rt->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh);
    TASK_UTIL_EXPECT_EQ("blue", rt->entry.virtual_network);

    // Delete route.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
}

//
// Single agent.
// Update route and verify the next hop and label.
//
TEST_F(BgpXmppEvpnTest1, 1AgentRouteUpdate) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register to blue instance.
    agent_a_->EnetSubscribe("blue", 1);

    // Add route from agent.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that the route showed up on the agent.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));
    autogen::EnetItemType *rt1 = const_cast<autogen::EnetItemType *>
        (agent_a_->EnetRouteLookup("blue", "aa:00:00:00:00:01,10.1.1.1/32"));
    TASK_UTIL_EXPECT_TRUE(rt1 != NULL);
    int label1 = rt1->entry.next_hops.next_hop[0].label;
    string nh1 = rt1->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh1);
    TASK_UTIL_EXPECT_EQ("blue", rt1->entry.virtual_network);

    // Remember the CRC for rt1.
    boost::crc_32_type rt1_crc;
    rt1->CalculateCrc(&rt1_crc);

    // Change the route.
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.2.1");
    task_util::WaitForIdle();

    // Wait for the route to get updated.
    const autogen::EnetItemType *rt2 = VerifyRouteUpdated(agent_a_, "blue",
        "aa:00:00:00:00:01,10.1.1.1/32", rt1_crc);

    // Verify that the route is updated.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(rt2 != NULL);
    TASK_UTIL_EXPECT_EQ("192.168.2.1",
        agent_a_->EnetRouteLookup("blue","aa:00:00:00:00:01,10.1.1.1/32")->entry.next_hops.next_hop[0].address);
    TASK_UTIL_EXPECT_EQ("blue",
        agent_a_->EnetRouteLookup("blue","aa:00:00:00:00:01,10.1.1.1/32")->entry.virtual_network);

    // Verify that next hop and label have changed.
    TASK_UTIL_EXPECT_NE(nh1,
        agent_a_->EnetRouteLookup("blue","aa:00:00:00:00:01,10.1.1.1/32")->entry.next_hops.next_hop[0].address);
    TASK_UTIL_EXPECT_NE(label1,
        agent_a_->EnetRouteLookup("blue","aa:00:00:00:00:01,10.1.1.1/32")->entry.next_hops.next_hop[0].label);

    // Delete route from agent.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agent.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    const autogen::EnetItemType *rt3 =
        agent_a_->EnetRouteLookup("blue", "aa:00:00:00:00:01,10.1.1.1/32");
    TASK_UTIL_EXPECT_TRUE(rt3 == NULL);

    // Delete non-existent route from agent.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
}

//
// Single agent.
// Make sure a duplicate delete is handled properly.
//
TEST_F(BgpXmppEvpnTest1, 1AgentRouteDelete) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register to blue instance.
    agent_a_->EnetSubscribe("blue", 1);

    // Add route from agent.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route showed up on agent.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));

    // Delete route.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());

    // Delete route again.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
}

//
// Two agents.
// Routes from 2 agents are advertised to each other.
//
TEST_F(BgpXmppEvpnTest1, 2AgentRouteAdd) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Delete route from agent B.
    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Two agents.
// Route from 1 agent shows up on the other.
// Update route and verify the next hop and label.
//
TEST_F(BgpXmppEvpnTest1, 2AgentRouteUpdate) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register to blue instance.
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that the route showed up on agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount("blue"));
    autogen::EnetItemType *rt1 = const_cast<autogen::EnetItemType *>
        (agent_b_->EnetRouteLookup("blue", "aa:00:00:00:00:01,10.1.1.1/32"));
    TASK_UTIL_EXPECT_TRUE(rt1 != NULL);
    int label1 = rt1->entry.next_hops.next_hop[0].label;
    string nh1 = rt1->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh1);
    TASK_UTIL_EXPECT_EQ("blue", rt1->entry.virtual_network);

    // Remember the CRC for rt1.
    boost::crc_32_type rt1_crc;
    rt1->CalculateCrc(&rt1_crc);

    // Change the route from agent A.
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.2.1");
    task_util::WaitForIdle();

    // Wait for the route to get updated on agent B.
    const autogen::EnetItemType *rt2 = VerifyRouteUpdated(agent_b_, "blue",
        "aa:00:00:00:00:01,10.1.1.1/32", rt1_crc);

    // Verify that the route is updated on agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(rt2 != NULL);
    TASK_UTIL_EXPECT_EQ("192.168.2.1",
        agent_b_->EnetRouteLookup("blue","aa:00:00:00:00:01,10.1.1.1/32")->entry.next_hops.next_hop[0].address);
    TASK_UTIL_EXPECT_EQ("blue",
        agent_b_->EnetRouteLookup("blue","aa:00:00:00:00:01,10.1.1.1/32")->entry.virtual_network);

    // Verify that next hop and label have changed.
    TASK_UTIL_EXPECT_NE(nh1,
        agent_b_->EnetRouteLookup("blue","aa:00:00:00:00:01,10.1.1.1/32")->entry.next_hops.next_hop[0].address);
    TASK_UTIL_EXPECT_NE(label1,
        agent_b_->EnetRouteLookup("blue","aa:00:00:00:00:01,10.1.1.1/32")->entry.next_hops.next_hop[0].label);

    // Delete route from agent.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agent.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    const autogen::EnetItemType *rt3 =
        agent_a_->EnetRouteLookup("blue", "aa:00:00:00:00:01,10.1.1.1/32");
    TASK_UTIL_EXPECT_TRUE(rt3 == NULL);

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Two agents.
// Multiple routes from 2 agents are advertised to each other.
// Different MAC address and IP address for each route.
//
TEST_F(BgpXmppEvpnTest1, 2AgentMultipleRoutes1) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add routes from agent A.
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_a;
	    eroute_a << "aa:00:00:00:00:0" << idx << ",10.1.1." << idx << "/32";
	    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    }
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_b;
	    eroute_b << "bb:00:00:00:00:0" << idx << ",10.1.2." << idx << "/32";
	    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    }
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(8, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_b_->EnetRouteCount("blue"));

    // Delete routes from agent A.
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_a;
	    eroute_a << "aa:00:00:00:00:0" << idx << ",10.1.1." << idx << "/32";
	    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    }
    task_util::WaitForIdle();

    // Verify deletion.
    TASK_UTIL_EXPECT_EQ(4, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent B.
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_b;
	    eroute_b << "bb:00:00:00:00:0" << idx << ",10.1.2." << idx << "/32";
	    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    }
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Two agents.
// Multiple routes from 2 agents are advertised to each other.
// Same MAC address and different IP address for each route.
//
TEST_F(BgpXmppEvpnTest1, 2AgentMultipleRoutes2) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add routes from agent A.
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_a;
	    eroute_a << "aa:00:00:00:00:01" << ",10.1.1." << idx << "/32";
	    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    }
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_b;
	    eroute_b << "bb:00:00:00:00:01" << ",10.1.2." << idx << "/32";
	    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    }
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(8, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_b_->EnetRouteCount("blue"));

    // Delete routes from agent A.
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_a;
	    eroute_a << "aa:00:00:00:00:01" << ",10.1.1." << idx << "/32";
	    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    }
    task_util::WaitForIdle();

    // Verify deletion.
    TASK_UTIL_EXPECT_EQ(4, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent B.
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_b;
	    eroute_b << "bb:00:00:00:00:01" << ",10.1.2." << idx << "/32";
	    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    }
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Two agents.
// Routes from 2 agents are advertised to each other.
// One agent subscribes to the instance after the other agent
// has already added routes.
//
TEST_F(BgpXmppEvpnTest1, 2AgentSubscribeLater) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register agent A to blue instance
    agent_a_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route showed up on agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));

    // Register agent B to blue instance
    agent_b_->EnetSubscribe("blue", 1);

    // Verify that route showed up on both agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount("blue"));

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Delete route from agent B.
    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Two agents.
// Routes from 2 agents are advertised to each other.
// Both agents are subscribed to 2 instances and add the same
// prefixes in each instance.
//
TEST_F(BgpXmppEvpnTest1, 2AgentMultipleInstances) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register both agents to blue and pink instances.
    agent_a_->EnetSubscribe("blue", 1);
    agent_a_->EnetSubscribe("pink", 2);
    task_util::WaitForIdle();
    agent_b_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("pink", 2);
    task_util::WaitForIdle();

    // Add routes from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    agent_a_->AddEnetRoute("pink", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add routes from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    agent_b_->AddEnetRoute("pink", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on agent A.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_a_->EnetRouteCount());

    // Verify that routes showed up on agent B.
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->EnetRouteCount());

    // Delete routes from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    agent_a_->DeleteEnetRoute("pink", eroute_a.str());
    task_util::WaitForIdle();

    // Delete routes from agent B.
    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    agent_b_->DeleteEnetRoute("pink", eroute_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent A.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());

    // Verify that there are no routes on agent B.
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Two agents.
// Routes from 2 agents are advertised to each other.
// One agent unsubscribes after routes have been exchanged.
//
TEST_F(BgpXmppEvpnTest1, 2AgentUnsubscribe) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance.
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());

    // Unregister B to blue instance.
    agent_b_->EnetUnsubscribe("blue", 1);

    // Verify that the route sent by B is gone.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Two agents.
// Routes from 2 agents are advertised to each other.
// One agent connects and subscribes to the instance after the other agent
// has already added routes.
//
TEST_F(BgpXmppEvpnTest1, 2AgentConnectLater) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register agent A to blue instance
    agent_a_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Create an XMPP Agent connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register agent B to blue instance
    agent_b_->EnetSubscribe("blue", 1);

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount());

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Delete route from agent B.
    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Two agents.
// Routes from 2 agents are advertised to each other.
// One agent closes the session after routes are exchanged.
//
TEST_F(BgpXmppEvpnTest1, 2AgentSessionDown) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());

    // Bring down the session to agent B.
    agent_b_->SessionDown();

    // Verify that the route sent by B is gone.
    if (!xs_x_->IsPeerCloseGraceful()) {
        TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    } else {
        TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    }
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    if (!xs_x_->IsPeerCloseGraceful()) {
        TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    } else {

        // agent_b's routes shall remain if agent_a is under graceful-restart.
        TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    }
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());

    // Close the sessions.
    agent_a_->SessionDown();
}

//
// Routes from 2 agents are advertised to each other.
// Routing instance is created on the BGP servers afte the the 2 agents
// have already advertised routes.
//
TEST_F(BgpXmppEvpnTest1, CreateInstanceLater) {
    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create an XMPP Agent connected to XMPP server X.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_x_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Configure the routing instances.
    Configure();
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Delete route from agent B.
    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

static const char *config_template_12 = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <virtual-network name='pink'>\
        <network-id>2</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
        <vrf-target>\
            target:1:2\
            <import-export>import</import-export>\
        </vrf-target>\
    </routing-instance>\
    <routing-instance name='pink'>\
        <virtual-network>pink</virtual-network>\
        <vrf-target>target:1:2</vrf-target>\
        <vrf-target>\
            target:1:1\
            <import-export>import</import-export>\
        </vrf-target>\
    </routing-instance>\
</config>\
";

//
// Single agent.
// Two routing instances with a connection.
// Add route and verify that enet routes are not leaked even though the
// instances are connected.
//
TEST_F(BgpXmppEvpnTest1, 1AgentConnectedInstances) {
    Configure(config_template_12);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly.
    RoutingInstanceMgr *mgr = bs_x_->routing_instance_mgr();
    RoutingInstance *blue = mgr->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue != NULL);
    TASK_UTIL_EXPECT_EQ(1, blue->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(2, blue->GetImportList().size());
    RoutingInstance *pink = mgr->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink != NULL);
    TASK_UTIL_EXPECT_EQ(1, pink->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(2, pink->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register to blue and pink instances.
    agent_a_->EnetSubscribe("blue", 1);
    agent_a_->EnetSubscribe("pink", 2);

    // Add routes from agent to blue instance.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route showed up in blue instance.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));
    const autogen::EnetItemType *rt =
        agent_a_->EnetRouteLookup("blue", "aa:00:00:00:00:01,10.1.1.1/32");
    TASK_UTIL_EXPECT_TRUE(rt != NULL);
    int label = rt->entry.next_hops.next_hop[0].label;
    string nh = rt->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh);
    TASK_UTIL_EXPECT_EQ("blue", rt->entry.virtual_network);

    // Verify that route does not show up in pink instance.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("pink"));

    // Delete route.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
}

static const char *config_template_21 = "\
<config>\
    <bgp-router name=\'X\'>\
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
    </bgp-router>\
    <bgp-router name=\'Y\'>\
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
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <virtual-network name='pink'>\
        <network-id>2</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
    <routing-instance name='pink'>\
        <virtual-network>pink</virtual-network>\
        <vrf-target>target:1:2</vrf-target>\
    </routing-instance>\
</config>\
";

//
// Control Nodes X and Y.
// Agents A and B.
//
class BgpXmppEvpnTest2 : public ::testing::Test {
protected:
    BgpXmppEvpnTest2() : thread_(&evm_) { }

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
            new BgpXmppChannelManagerMock(xs_x_, bs_x_.get()));

        bs_y_.reset(new BgpServerTest(&evm_, "Y"));
        bs_y_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created BGP server at port: " <<
            bs_y_->session_manager()->GetPort());
        xs_y_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        xs_y_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " <<
            xs_y_->GetPort());
        cm_y_.reset(
            new BgpXmppChannelManagerMock(xs_y_, bs_y_.get()));

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

        if (agent_a_) { agent_a_->Delete(); }
        if (agent_b_) { agent_b_->Delete(); }

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
    boost::scoped_ptr<BgpXmppChannelManagerMock> cm_x_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> cm_y_;
};

//
// Route from 1 agent shows up on the other.
// Verify nh and label values on both agents.
//
TEST_F(BgpXmppEvpnTest2, RouteAdd1) {
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

    // Register to blue instance
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route showed up on agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));
    const autogen::EnetItemType *rt_a =
        agent_a_->EnetRouteLookup("blue", "aa:00:00:00:00:01,10.1.1.1/32");
    TASK_UTIL_EXPECT_TRUE(rt_a != NULL);
    int label_a = rt_a->entry.next_hops.next_hop[0].label;
    string nh_a = rt_a->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_a);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_a);
    TASK_UTIL_EXPECT_EQ("blue", rt_a->entry.virtual_network);

    // Verify that route showed up on agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount("blue"));
    const autogen::EnetItemType *rt_b =
        agent_b_->EnetRouteLookup("blue", "aa:00:00:00:00:01,10.1.1.1/32");
    TASK_UTIL_EXPECT_TRUE(rt_b != NULL);
    int label_b = rt_b->entry.next_hops.next_hop[0].label;
    string nh_b = rt_b->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_b);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_b);
    TASK_UTIL_EXPECT_EQ("blue", rt_b->entry.virtual_network);

    // Verify that label and nh are the same on agents A and B.
    TASK_UTIL_EXPECT_EQ(label_a, label_b);
    TASK_UTIL_EXPECT_EQ(nh_a, nh_b);

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Routes from 2 agents are advertised to each other.
//
TEST_F(BgpXmppEvpnTest2, RouteAdd2) {
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

    // Register to blue instance
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Delete route from agent B.
    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Route from 1 agent shows up on the other.
// Update route and verify the next hop and label.
//
TEST_F(BgpXmppEvpnTest2, RouteUpdate) {
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

    // Register to blue instance
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that the route showed up on agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount("blue"));
    autogen::EnetItemType *rt1 = const_cast<autogen::EnetItemType *>
        (agent_b_->EnetRouteLookup("blue", "aa:00:00:00:00:01,10.1.1.1/32"));
    TASK_UTIL_EXPECT_TRUE(rt1 != NULL);
    int label1 = rt1->entry.next_hops.next_hop[0].label;
    string nh1 = rt1->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh1);
    TASK_UTIL_EXPECT_EQ("blue", rt1->entry.virtual_network);

    // Remember the CRC for rt1.
    boost::crc_32_type rt1_crc;
    rt1->CalculateCrc(&rt1_crc);

    // Change the route from agent A.
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.2.1");
    task_util::WaitForIdle();

    // Wait for the route to get updated on agent B.
    const autogen::EnetItemType *rt2 = VerifyRouteUpdated(agent_b_, "blue",
        "aa:00:00:00:00:01,10.1.1.1/32", rt1_crc);

    // Verify that the route is updated on agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(rt2 != NULL);

    TASK_UTIL_EXPECT_EQ("192.168.2.1",
         agent_b_->EnetRouteLookup("blue","aa:00:00:00:00:01,10.1.1.1/32")->entry.next_hops.next_hop[0].address);
    TASK_UTIL_EXPECT_EQ("blue",
         agent_b_->EnetRouteLookup("blue","aa:00:00:00:00:01,10.1.1.1/32")->entry.virtual_network);

    // Verify that next hop and label have changed.
    TASK_UTIL_EXPECT_NE(label1,
            agent_b_->EnetRouteLookup("blue","aa:00:00:00:00:01,10.1.1.1/32")->entry.next_hops.next_hop[0].label);

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Multiple routes from 2 agents are advertised to each other.
// Different MAC address and IP address for each route.
//
TEST_F(BgpXmppEvpnTest2, MultipleRoutes1) {
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

    // Register to blue instance
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add routes from agent A.
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_a;
	    eroute_a << "aa:00:00:00:00:0" << idx << ",10.1.1." << idx << "/32";
	    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    }
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_b;
	    eroute_b << "bb:00:00:00:00:0" << idx << ",10.1.2." << idx << "/32";
	    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    }
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(8, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_b_->EnetRouteCount("blue"));

    // Delete routes from agent A.
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_a;
	    eroute_a << "aa:00:00:00:00:0" << idx << ",10.1.1." << idx << "/32";
	    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    }
    task_util::WaitForIdle();

    // Verify deletion.
    TASK_UTIL_EXPECT_EQ(4, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent B.
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_b;
	    eroute_b << "bb:00:00:00:00:0" << idx << ",10.1.2." << idx << "/32";
	    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    }
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Multiple routes from 2 agents are advertised to each other.
// Same MAC address and different IP address for each route.
//
TEST_F(BgpXmppEvpnTest2, MultipleRoutes2) {
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

    // Register to blue instance
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add routes from agent A.
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_a;
	    eroute_a << "aa:00:00:00:00:01" << ",10.1.1." << idx << "/32";
	    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    }
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_b;
	    eroute_b << "bb:00:00:00:00:01" << ",10.1.2." << idx << "/32";
	    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    }
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(8, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_b_->EnetRouteCount("blue"));

    // Delete routes from agent A.
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_a;
	    eroute_a << "aa:00:00:00:00:01" << ",10.1.1." << idx << "/32";
	    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    }
    task_util::WaitForIdle();

    // Verify deletion.
    TASK_UTIL_EXPECT_EQ(4, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent B.
    for (int idx = 1; idx <= 4;  idx++) {
	    stringstream eroute_b;
            eroute_b << "bb:00:00:00:00:01" << ",10.1.2." << idx << "/32";
	    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    }
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Routes from 2 agents are advertised to each other.
// One agent subscribes to the instance after the other agent
// has already added routes.
//
TEST_F(BgpXmppEvpnTest2, SubscribeLater) {
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

    // Register agent A to blue instance
    agent_a_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route showed up on agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));

    // Register agent B to blue instance
    agent_b_->EnetSubscribe("blue", 1);

    // Verify that route showed up on both agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount("blue"));

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Delete route from agent B.
    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Routes from 2 agents are advertised to each other.
// Both agents are subscribed to 2 instances and add the same
// prefixes in each instance.
//
TEST_F(BgpXmppEvpnTest2, MultipleInstances) {
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

    // Register both agents to blue and pink instances.
    agent_a_->EnetSubscribe("blue", 1);
    agent_a_->EnetSubscribe("pink", 2);
    task_util::WaitForIdle();
    agent_b_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("pink", 2);
    task_util::WaitForIdle();

    // Add routes from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    agent_a_->AddEnetRoute("pink", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add routes from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    agent_b_->AddEnetRoute("pink", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on agent A.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_a_->EnetRouteCount());

    // Verify that routes showed up on agent B.
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->EnetRouteCount());

    // Delete routes from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    agent_a_->DeleteEnetRoute("pink", eroute_a.str());
    task_util::WaitForIdle();

    // Delete routes from agent B.
    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    agent_b_->DeleteEnetRoute("pink", eroute_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent A.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());

    // Verify that there are no routes on agent B.
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Routes from 2 agents are advertised to each other.
// One agent unsubscribes after routes have been exchanged.
//
TEST_F(BgpXmppEvpnTest2, Unsubscribe) {
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

    // Register to blue instance.
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());

    // Unregister B to blue instance.
    agent_b_->EnetUnsubscribe("blue", 1);

    // Verify that the route sent by B is gone.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Routes from 2 agents are advertised to each other.
// One agent connects and subscribes to the instance after the other agent
// has already added routes.
//
TEST_F(BgpXmppEvpnTest2, XmppConnectLater) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register agent A to blue instance
    agent_a_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Create an XMPP Agent connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register agent B to blue instance
    agent_b_->EnetSubscribe("blue", 1);

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount());

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Delete route from agent B.
    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Routes from 2 agents are advertised to each other.
// One agent closes the session after routes are exchanged.
//
TEST_F(BgpXmppEvpnTest2, XmppSessionDown) {
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

    // Register to blue instance
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());

    // Bring down the session to agent B.
    agent_b_->SessionDown();

    // Verify that the route sent by B is gone.
    if (!xs_y_->IsPeerCloseGraceful()) {
        TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    } else {
        TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    }
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    if (!xs_y_->IsPeerCloseGraceful()) {
        TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    } else {
        TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    }

    // Clear agent_b_'s stale routes.
    if (xs_y_->IsPeerCloseGraceful()) {
        agent_b_->SessionUp();
        task_util::WaitForIdle();
        agent_b_->EnetSubscribe("blue", 1);
        task_util::WaitForIdle();
        agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
        task_util::WaitForIdle();
        agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    }
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());

    // Close the sessions.
    agent_a_->SessionDown();
}

static const char *config_template_22_x = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <virtual-network name='pink'>\
        <network-id>2</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
    <routing-instance name='pink'>\
        <virtual-network>pink</virtual-network>\
        <vrf-target>target:1:2</vrf-target>\
    </routing-instance>\
</config>\
";

static const char *config_template_22_y = "\
<config>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <virtual-network name='pink'>\
        <network-id>2</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
    <routing-instance name='pink'>\
        <virtual-network>pink</virtual-network>\
        <vrf-target>target:1:2</vrf-target>\
    </routing-instance>\
</config>\
";

static const char *config_template_23 = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>route-target</family>\
                <family>e-vpn</family>\
                <family>inet-vpn</family>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>route-target</family>\
                <family>e-vpn</family>\
                <family>inet-vpn</family>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
</config>\
";

//
// Routes from 2 agents are advertised to each other.
// BGP session comes up after the the 2 agents have already advertised
// routes to the 2 XMPP servers.
//
TEST_F(BgpXmppEvpnTest2, BgpConnectLater) {
    // Configure individual bgp-routers and routing instances but no session.
    Configure(bs_x_, config_template_22_x);
    Configure(bs_y_, config_template_22_y);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create an XMPP Agent connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes are reflected to individual agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount("blue"));

    // Configure the BGP session.
    Configure(config_template_23);
    task_util::WaitForIdle();

    // Verify that routes showed up on the remote agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Delete route from agent B.
    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

//
// Routes from 2 agents are advertised to each other.
// Routing instances are created on the BGP servers after the the 2 agents
// have already advertised routes.
//
TEST_F(BgpXmppEvpnTest2, CreateInstanceLater) {
    // Configure the BGP session.
    Configure(config_template_23);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create an XMPP Agent connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Configure the routing instances.
    Configure(bs_x_, config_template_22_x);
    Configure(bs_y_, config_template_22_y);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Delete route from agent B.
    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

static const char *config_template_24 = "\
<delete>\
    <bgp-router name=\'X\'>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
    </bgp-router>\
</delete>\
";

//
// Routes from 2 agents are advertised to each other.
// BGP session goes down and comes up after routes have been exchanged.
//
TEST_F(BgpXmppEvpnTest2, BgpSessionBounce) {
    Configure();
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create an XMPP Agent connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("blue", 1);

    // Add route from agent A.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Unconfigure the BGP session.
    bs_x_->Configure(config_template_24);
    bs_y_->Configure(config_template_24);
    task_util::WaitForIdle();

    // Verify that routes from remote agents are cleaned up.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount("blue"));

    // Configure the BGP session.
    Configure();
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Delete route from agent B.
    agent_b_->DeleteEnetRoute("blue", eroute_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

static const char *config_template_25 = "\
<config>\
    <bgp-router name=\'X\'>\
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
    </bgp-router>\
    <bgp-router name=\'Y\'>\
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
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <virtual-network name='pink'>\
        <network-id>2</network-id>\
    </virtual-network>\
    <routing-instance name='blue'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:1</vrf-target>\
        <vrf-target>\
            target:1:2\
            <import-export>import</import-export>\
        </vrf-target>\
    </routing-instance>\
    <routing-instance name='pink'>\
        <virtual-network>pink</virtual-network>\
        <vrf-target>target:1:2</vrf-target>\
        <vrf-target>\
            target:1:1\
            <import-export>import</import-export>\
        </vrf-target>\
    </routing-instance>\
</config>\
";

//
// Two routing instances with a connection.
// Add route and verify that enet routes are not leaked even though the
// instances are connected.
//
TEST_F(BgpXmppEvpnTest2, ConnectedInstances) {
    Configure(config_template_25);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on X.
    RoutingInstanceMgr *mgr_x = bs_x_->routing_instance_mgr();
    RoutingInstance *blue_x = mgr_x->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_x != NULL);
    TASK_UTIL_EXPECT_EQ(1, blue_x->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(2, blue_x->GetImportList().size());
    RoutingInstance *pink_x = mgr_x->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_x != NULL);
    TASK_UTIL_EXPECT_EQ(1, pink_x->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(2, pink_x->GetImportList().size());

    // Make sure that the config got applied properly on Y.
    RoutingInstanceMgr *mgr_y = bs_y_->routing_instance_mgr();
    RoutingInstance *blue_y = mgr_y->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_y != NULL);
    TASK_UTIL_EXPECT_EQ(1, blue_y->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(2, blue_y->GetImportList().size());
    RoutingInstance *pink_y = mgr_y->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_y != NULL);
    TASK_UTIL_EXPECT_EQ(1, pink_y->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(2, pink_y->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_x_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create an XMPP Agent connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_y_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->EnetSubscribe("blue", 1);
    agent_a_->EnetSubscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->EnetSubscribe("blue", 1);
    agent_b_->EnetSubscribe("pink", 2);

    // Add route from agent A to blue instance.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route showed up in blue instance on Agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));
    const autogen::EnetItemType *rt_a =
        agent_a_->EnetRouteLookup("blue", "aa:00:00:00:00:01,10.1.1.1/32");
    TASK_UTIL_EXPECT_TRUE(rt_a != NULL);
    int label_a = rt_a->entry.next_hops.next_hop[0].label;
    string nh_a = rt_a->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_a);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_a);
    TASK_UTIL_EXPECT_EQ("blue", rt_a->entry.virtual_network);

    // Verify that route showed up in blue instance on Agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount("blue"));
    const autogen::EnetItemType *rt_b =
        agent_b_->EnetRouteLookup("blue", "aa:00:00:00:00:01,10.1.1.1/32");
    TASK_UTIL_EXPECT_TRUE(rt_b != NULL);
    int label_b = rt_b->entry.next_hops.next_hop[0].label;
    string nh_b = rt_b->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_b);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_b);
    TASK_UTIL_EXPECT_EQ("blue", rt_b->entry.virtual_network);

    // Verify that label and nh are the same on agents A and B.
    TASK_UTIL_EXPECT_EQ(label_a, label_b);
    TASK_UTIL_EXPECT_EQ(nh_a, nh_b);

    // Verify that route does not show up in pink instance on A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("pink"));

    // Verify that route does not show up in pink instance on B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("pink"));

    // Delete route.
    agent_a_->DeleteEnetRoute("blue", eroute_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent A.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->EnetRouteCount("pink"));

    // Verify that there are no routes on agent B.
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->EnetRouteCount("pink"));

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
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
    BgpObjectFactory::Register<StateMachine>(
        boost::factory<StateMachineTest *>());
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
