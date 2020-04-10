/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include <boost/assign/list_of.hpp>

#include "bgp/bgp_factory.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "bgp/inet6vpn/inet6vpn_table.h"

using namespace std;
using boost::assign::list_of;

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
        BgpXmppChannelManager(x, b) { }

    virtual void XmppHandleChannelEvent(XmppChannel *channel,
                                        xmps::PeerState state) {
         BgpXmppChannelManager::XmppHandleChannelEvent(channel, state);
    }

    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel) {
        BgpXmppChannel *mock_channel =
            new BgpXmppChannelMock(channel, bgp_server_, this);
        return mock_channel;
    }
};

static const char *one_cn_unconnected_base_config = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
    </bgp-router>\
</config>\
";

static const char *one_cn_unconnected_instances_config = "\
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
class BgpXmppInet6Test : public ::testing::Test {
protected:
    BgpXmppInet6Test() : thread_(&evm_) { }

    virtual void SetUp() {
        bgp_server_.reset(new BgpServerTest(&evm_, "X"));
        xmpp_server_ =
            new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);

        bgp_server_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created server at port: " <<
            bgp_server_->session_manager()->GetPort());
        xmpp_server_->Initialize(0, false);

        bgp_channel_manager_.reset(
            new BgpXmppChannelManagerMock(xmpp_server_, bgp_server_.get()));

        thread_.Start();
    }

    virtual void TearDown() {
        xmpp_server_->Shutdown();
        task_util::WaitForIdle();
        bgp_server_->Shutdown();
        task_util::WaitForIdle();
        bgp_channel_manager_.reset();
        TcpServerManager::DeleteServer(xmpp_server_);
        xmpp_server_ = NULL;
        if (agent_a_) { agent_a_->Delete(); }
        if (agent_b_) { agent_b_->Delete(); }
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void Configure(const char *cfg_template =
                   one_cn_unconnected_instances_config) {
        bgp_server_->Configure(cfg_template);
    }

    bool VerifyRouteUpdateNexthop(string instance_name, string route,
            string nexthop, test::NetworkAgentMock *agent) {
        const autogen::ItemType *rt =
            agent->Inet6RouteLookup(instance_name, route);
        if (rt) {
            if (rt->entry.next_hops.next_hop.size() != 1) {
                return false;
            }
            string nh = rt->entry.next_hops.next_hop[0].address;
            if (nexthop.length() && (nexthop == nh)) {
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    bool VerifyRouteUpdateLabel(string instance_name, string route,
            int label, test::NetworkAgentMock *agent) {
        const autogen::ItemType *rt =
            agent->Inet6RouteLookup(instance_name, route);
        if (rt) {
            if (rt->entry.next_hops.next_hop.size() != 1) {
                return false;
            }
            int lbl = rt->entry.next_hops.next_hop[0].label;
            if ((label) && (label == lbl)) {
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bgp_server_;
    XmppServer *xmpp_server_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_b_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> bgp_channel_manager_;
};

// Single agent.
// Add one route and verify the next hop and label.
TEST_F(BgpXmppInet6Test, 1AgentRouteAdd) {
    Configure(one_cn_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register to blue instance.
    agent_a_->Inet6Subscribe("blue", 1);

    // Add route from agent.
    stringstream route_a;
    route_a << "2001:0db8:85a3:0000:0000:8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();
    usleep(1000);

    // Verify that route showed up on agent.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    const autogen::ItemType *rt =
        agent_a_->Inet6RouteLookup("blue", "2001:db8:85a3::8a2e:370:aaaa/128");
    TASK_UTIL_EXPECT_TRUE(rt != NULL);
    int label = rt->entry.next_hops.next_hop[0].label;
    string nh = rt->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh);
    TASK_UTIL_EXPECT_EQ("blue", rt->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(test::RouteAttributes::GetDefaultLocalPref(),
                        rt->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(test::RouteAttributes::GetDefaultSequence(),
                        rt->entry.sequence_number);

    // Delete route.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
}

TEST_F(BgpXmppInet6Test, 1AgentRouteUpdate) {
    Configure(one_cn_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register to blue instance.
    agent_a_->Inet6Subscribe("blue", 1);

    // Add route from agent.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();
    usleep(1000);

    // Verify that route showed up on agent.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt1 =
        agent_a_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt1 != NULL);
    int label1 = rt1->entry.next_hops.next_hop[0].label;
    string nh1 = rt1->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label1);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh1);
    TASK_UTIL_EXPECT_EQ("blue", rt1->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(test::RouteAttributes::GetDefaultLocalPref(),
                        rt1->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(test::RouteAttributes::GetDefaultSequence(),
                        rt1->entry.sequence_number);

    // Change the nexthop and attributes of the route.
    test::NextHop nexthop_a1("192.168.2.1");
    test::RouteAttributes attr(1000, 2000);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a1, attr);
    task_util::WaitForIdle();

    // Wait for the route to get updated on agent A.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateNexthop("blue",
        route_a.str(), "192.168.2.1", agent_a_.get()));
    const autogen::ItemType *rt2 =
        agent_a_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(rt2 != NULL);
    TASK_UTIL_EXPECT_EQ("blue", rt2->entry.virtual_network);
    int label2 = rt2->entry.next_hops.next_hop[0].label;
    string nh2 = rt2->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label2);
    TASK_UTIL_EXPECT_EQ("192.168.2.1", nh2);
    TASK_UTIL_EXPECT_NE(label1, label2);
    TASK_UTIL_EXPECT_EQ(1000, rt2->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(2000, rt2->entry.sequence_number);

    // Delete route.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
}

// Single agent.
// Make sure a duplicate delete is handled properly.
TEST_F(BgpXmppInet6Test, 1AgentRouteDelete) {
    Configure(one_cn_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register to blue instance.
    agent_a_->Inet6Subscribe("blue", 1);

    // Add route from agent.
    stringstream route_a;
    route_a << "2001:0db8:85a3:0000:0000:8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Verify that route showed up on agent.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));

    // Delete route.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());

    // Delete route again.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
}

TEST_F(BgpXmppInet6Test, 2AgentRouteAdd) {
    Configure(one_cn_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    test::RouteAttributes attr_a(1000, 1000);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream route_b;
    route_b << "2001:db8:85a3::8a2e:370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    test::RouteAttributes attr_b(2000, 2000);
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b, attr_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent B.
    agent_b_->DeleteInet6Route("blue", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Two agents.
// Route from 1 agent shows up on the other.
// Update route and verify the next hop and label.
TEST_F(BgpXmppInet6Test, 2AgentRouteUpdate) {
    Configure(one_cn_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register to blue instance.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    test::RouteAttributes attr_a(1000, 1001);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that the route showed up on agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt1 =
        agent_b_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt1 != NULL);
    int label1 = rt1->entry.next_hops.next_hop[0].label;
    string nh_address1 = rt1->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_address1);
    TASK_UTIL_EXPECT_EQ("blue", rt1->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt1->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt1->entry.sequence_number);

    // Change the route from agent A.
    test::NextHop nexthop_a1("192.168.2.1");
    test::RouteAttributes attr_a1(2000, 2001);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a1, attr_a1);
    task_util::WaitForIdle();

    // Wait for the route to get updated on agent B.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateNexthop("blue",
        route_a.str(), "192.168.2.1", agent_b_.get()));
    const autogen::ItemType *rt2 =
        agent_b_->Inet6RouteLookup("blue", route_a.str());

    // Verify that the route is updated on agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(rt2 != NULL);

    // Verify that the attributes have changed.
    int label2 = rt2->entry.next_hops.next_hop[0].label;
    string nh_address2 = rt2->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(label1, label2);
    TASK_UTIL_EXPECT_NE(nh_address1, nh_address2);
    TASK_UTIL_EXPECT_EQ("blue", rt2->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(2000, rt2->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(2001, rt2->entry.sequence_number);

    // Delete route from agent.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agent.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt3 =
        agent_b_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt3 == NULL);

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Two agents.
// Multiple routes from 2 agents are advertised to each other.
TEST_F(BgpXmppInet6Test, 2AgentMultipleRoutes1) {
    Configure(one_cn_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    // Add routes from agent A.
    test::NextHop nexthop_a("192.168.1.1");
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_a;
        route_a << "2001:0db8:85a3:0000:0000:8a2e:aaaa:" << idx << "/128";
        agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    }
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream route_b;
    test::NextHop nexthop_b("192.168.2.1");
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_b;
        route_b << "2001:0db8:85a3:0000:0000:8a2e:bbbb:" << idx << "/128";
        agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    }
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount("blue"));

    // Delete routes from agent A.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_a;
        route_a << "2001:0db8:85a3:0000:0000:8a2e:aaaa:" << idx << "/128";
        agent_a_->DeleteInet6Route("blue", route_a.str());
    }
    task_util::WaitForIdle();

    // Verify deletion.
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent B.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_b;
        route_b << "2001:0db8:85a3:0000:0000:8a2e:bbbb:" << idx << "/128";
        agent_b_->DeleteInet6Route("blue", route_b.str());
    }
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Two agents.
// Routes from 2 agents are advertised to each other.
// One agent subscribes to the instance after the other agent
// has already added routes.
TEST_F(BgpXmppInet6Test, 2AgentSubscribeLater) {
    Configure(one_cn_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register agent A to blue instance
    agent_a_->Inet6Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3:0000:0000:8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Verify that route showed up on agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));

    // Register agent B to blue instance
    agent_b_->Inet6Subscribe("blue", 1);

    // Verify that route showed up on both agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    // Add route from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3:0000:0000:8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent B.
    agent_b_->DeleteInet6Route("blue", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Two agents.
// Routes from 2 agents are advertised to each other.
// Both agents are subscribed to 2 instances and add the same
// prefixes in each instance.
TEST_F(BgpXmppInet6Test, 2AgentMultipleInstances) {
    Configure(one_cn_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register both agents to blue and pink instances.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);
    task_util::WaitForIdle();
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);
    task_util::WaitForIdle();

    // Add routes from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3:0000:0000:8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    agent_a_->AddInet6Route("pink", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Add routes from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3:0000:0000:8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    agent_b_->AddInet6Route("pink", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on agent A.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());

    // Verify that routes showed up on agent B.
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());

    // Delete routes from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    agent_a_->DeleteInet6Route("pink", route_a.str());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());

    // Delete routes from agent B.
    agent_b_->DeleteInet6Route("blue", route_b.str());
    agent_b_->DeleteInet6Route("pink", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent A.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());

    // Verify that there are no routes on agent B.
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Two agents.
// Routes from 2 agents are advertised to each other.
// One agent unsubscribes after routes have been exchanged.
TEST_F(BgpXmppInet6Test, 2AgentUnsubscribe) {
    Configure(one_cn_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3:0000:0000:8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3:0000:0000:8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.1");
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));

    // Unregister B to blue instance.
    agent_b_->Inet6Unsubscribe("blue", 1);
    task_util::WaitForIdle();

    // Verify that the route sent by B is gone.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Two agents.
// Routes from 2 agents are advertised to each other.
// One agent connects and subscribes to the instance after the other agent
// has already added routes.
TEST_F(BgpXmppInet6Test, 2AgentConnectLater) {
    Configure(one_cn_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register agent A to blue instance
    agent_a_->Inet6Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3:0000:0000:8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));

    // Create XMPP Agent B connected to XMPP server.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register agent B to blue instance
    agent_b_->Inet6Subscribe("blue", 1);

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    // Add route from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3:0000:0000:8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that the route sent by A is gone.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent B.
    agent_b_->DeleteInet6Route("blue", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Two agents.
// Routes from 2 agents are advertised to each other.
// One agent closes the session after routes are exchanged.
TEST_F(BgpXmppInet6Test, 2AgentSessionDown) {
    Configure(one_cn_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server_->GetPort(),
            "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3:0000:0000:8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3:0000:0000:8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());

    // Bring down the session to agent B.
    agent_b_->SessionDown();

    // Verify that the route sent by B is gone.
    if (xmpp_server_->IsPeerCloseGraceful()) {
        TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    } else {
        TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    }
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    if (xmpp_server_->IsPeerCloseGraceful()) {
        // agent_b's routes shall remain if agent_a is under graceful-restart.
        TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    } else {
        TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    }
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());

    // Close the sessions.
    agent_a_->SessionDown();
}

// Routes from 2 agents are advertised to each other.
// Routing instance is created on the BGP servers after the the 2 agents
// have already advertised routes.
TEST_F(BgpXmppInet6Test, CreateInstanceConfigLater) {
    Configure(one_cn_unconnected_base_config);

    // Create XMPP Agent A connected to XMPP server.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3:0000:0000:8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3:0000:0000:8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Configure the routing instances.
    Configure(one_cn_unconnected_instances_config);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent B.
    agent_b_->DeleteInet6Route("blue", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

static const char *one_cn_connected_instances_config = "\
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

// Single agent. Two routing instances with a connection. Add routes in both
// the instances and verify that routes are leaked
TEST_F(BgpXmppInet6Test, 1AgentConnectedInstances) {
    Configure(one_cn_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly.
    RoutingInstanceMgr *mgr = bgp_server_->routing_instance_mgr();
    RoutingInstance *blue_ri = mgr->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri->GetImportList().size());
    RoutingInstance *pink_ri = mgr->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server_->GetPort(),
            "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register to blue and pink instances.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Add route from agent to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    test::RouteAttributes attr_a(1000, 1001);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that route showed up in blue instance.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt =
        agent_a_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt != NULL);
    int label = rt->entry.next_hops.next_hop[0].label;
    string nh = rt->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh);
    TASK_UTIL_EXPECT_EQ("blue", rt->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt->entry.sequence_number);
    // Blue route should leak into pink
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));

    // Add route from agent to pink instance.
    stringstream route_b;
    route_b << "2001:db8:85a3::8a2e:370:bbbb/128";
    test::RouteAttributes attr_b(2000, 2001);
    agent_a_->AddInet6Route("pink", route_b.str(), nexthop_a, attr_b);
    task_util::WaitForIdle();

    // Verify that route showed up in blue instance.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("pink"));
    rt = agent_a_->Inet6RouteLookup("pink", route_b.str());
    TASK_UTIL_EXPECT_TRUE(rt != NULL);
    label = rt->entry.next_hops.next_hop[0].label;
    nh = rt->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh);
    TASK_UTIL_EXPECT_EQ("pink", rt->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(2000, rt->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(2001, rt->entry.sequence_number);
    // Pink route should leak into blue
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));

    // Delete route_a. Blue-ri should still have the pink route.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));

    // Delete route_b
    agent_a_->DeleteInet6Route("pink", route_b.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
}

static const char *two_cns_unconnected_instances_config = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>inet6-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>inet6-vpn</family>\
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

static const char *config_2_control_nodes_different_asn = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <autonomous-system>64511</autonomous-system>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>inet6-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <autonomous-system>64512</autonomous-system>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>inet6-vpn</family>\
            </address-families>\
        </session>\
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
// Control Nodes X and Y.
// Agents A and B.
//
class BgpXmppInet6Test2Peers : public ::testing::Test {
protected:
    BgpXmppInet6Test2Peers() : thread_(&evm_) { }

    virtual void SetUp() {
        bgp_server1_.reset(new BgpServerTest(&evm_, "X"));
        bgp_server1_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created BGP server at port: " <<
            bgp_server1_->session_manager()->GetPort());
        xmpp_server1_ =
            new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        xmpp_server1_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " << xmpp_server1_->GetPort());
        cm1_.reset(
            new BgpXmppChannelManagerMock(xmpp_server1_, bgp_server1_.get()));

        bgp_server2_.reset(new BgpServerTest(&evm_, "Y"));
        bgp_server2_->session_manager()->Initialize(0);
        LOG(DEBUG, "Created BGP server at port: " <<
            bgp_server2_->session_manager()->GetPort());
        xmpp_server2_ =
            new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        xmpp_server2_->Initialize(0, false);
        LOG(DEBUG, "Created XMPP server at port: " << xmpp_server2_->GetPort());
        cm2_.reset(
            new BgpXmppChannelManagerMock(xmpp_server2_, bgp_server2_.get()));

        // Insert the 'all' encaps in sorted order.
        all_encap_list.push_back("gre");
        all_encap_list.push_back("udp");
        thread_.Start();
    }

    virtual void TearDown() {
        xmpp_server1_->Shutdown();
        task_util::WaitForIdle();
        bgp_server1_->Shutdown();
        task_util::WaitForIdle();
        cm1_.reset();

        xmpp_server2_->Shutdown();
        task_util::WaitForIdle();
        bgp_server2_->Shutdown();
        task_util::WaitForIdle();
        cm2_.reset();

        TcpServerManager::DeleteServer(xmpp_server1_);
        xmpp_server1_ = NULL;
        TcpServerManager::DeleteServer(xmpp_server2_);
        xmpp_server2_ = NULL;

        if (agent_a_) {
            agent_a_->Delete();
        }
        if (agent_b_) {
            agent_b_->Delete();
        }
        if (agent_y1_) {
            agent_y1_->Delete();
        }
        if (agent_y2_) {
            agent_y2_->Delete();
        }

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

    void Configure(const char *cfg_template =
                   two_cns_unconnected_instances_config) {
        char config[4096];
        snprintf(config, sizeof(config), cfg_template,
                 bgp_server1_->session_manager()->GetPort(),
                 bgp_server2_->session_manager()->GetPort());
        bgp_server1_->Configure(config);
        bgp_server2_->Configure(config);
    }

    bool VerifyRouteUpdateNexthop(string instance_name, string route,
            string nexthop, test::NetworkAgentMock *agent) {
        const autogen::ItemType *rt =
            agent->Inet6RouteLookup(instance_name, route);
        if (rt) {
            if (rt->entry.next_hops.next_hop.size() != 1) {
                return false;
            }
            string nh = rt->entry.next_hops.next_hop[0].address;
            if (nexthop.length() && (nexthop == nh)) {
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    bool VerifyRouteUpdateLabel(string instance_name, string route,
            int label, test::NetworkAgentMock *agent) {
        const autogen::ItemType *rt =
            agent->Inet6RouteLookup(instance_name, route);
        if (rt) {
            if (rt->entry.next_hops.next_hop.size() != 1) {
                return false;
            }
            int lbl = rt->entry.next_hops.next_hop[0].label;
            if ((label) && (label == lbl)) {
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    bool VerifyRouteUpdateEncap(string instance_name, string route,
            string encap, test::NetworkAgentMock *agent) {
        const autogen::ItemType *rt =
            agent->Inet6RouteLookup(instance_name, route);
        if (rt) {
            autogen::TunnelEncapsulationListType rcvd_encap_info =
                rt->entry.next_hops.next_hop[0].tunnel_encapsulation_list;
            size_t received_size = rcvd_encap_info.tunnel_encapsulation.size();
            if (encap == "all_ipv6") {
                if (received_size != 2) {
                    return false;
                }
                std::vector<std::string> rcvd_encap_list =
                    rcvd_encap_info.tunnel_encapsulation;
                sort(rcvd_encap_list.begin(), rcvd_encap_list.end());
                if (rcvd_encap_list == all_encap_list) {
                    return true;
                } else {
                    return false;
                }
            } else {
                if (received_size != 1) {
                    return false;
                }
                string encap_value;
                encap_value = rcvd_encap_info.tunnel_encapsulation[0];
                if (encap_value.length() && (encap_value == encap)) {
                    return true;
                } else {
                    return false;
                }
            }
        } else {
            return false;
        }
    }

    bool VerifyRouteUpdateTagList(string instance_name, string route,
            vector<int> &tag_list, test::NetworkAgentMock *agent) {
        const autogen::ItemType *rt =
            agent->Inet6RouteLookup(instance_name, route);
        if (rt) {
            autogen::TagListType rcvd_tag_list =
                rt->entry.next_hops.next_hop[0].tag_list;
            if (rcvd_tag_list.tag.size() != tag_list.size())
                return false;
            for (size_t idx = 0; idx < tag_list.size(); ++idx) {
                if (rcvd_tag_list.tag[idx] != tag_list[idx]) {
                    return false;
                }
            }
            return true;
        } else {
            return false;
        }
    }

    bool VerifyRouteUpdateCommunities(string instance_name, string route,
            vector<string>& communities, test::NetworkAgentMock *agent) {
        const autogen::ItemType *rt =
            agent->Inet6RouteLookup(instance_name, route);
        if (rt) {
            vector<string> recd_communities =
                rt->entry.community_tag_list.community_tag;
            if (recd_communities == communities) {
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }


    bool VerifyRouteUpdateSGids(string instance_name, string route,
            vector<int>& sgids, test::NetworkAgentMock *agent) {
        const autogen::ItemType *rt =
            agent->Inet6RouteLookup(instance_name, route);
        if (rt) {
            vector<int> recd_sgids =
                rt->entry.security_group_list.security_group;
            if (recd_sgids == sgids) {
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    bool CheckRoute(test::NetworkAgentMock *agent, string net, string prefix,
                    string nexthop) {
        task_util::TaskSchedulerLock lock;
        const autogen::ItemType *rt = agent->Inet6RouteLookup(net, prefix);
        if (!rt) {
            return false;
        }
        if (rt->entry.next_hops.next_hop[0].address != nexthop) {
            return false;
        }
        return true;
    }

    void VerifyRouteExists(test::NetworkAgentMock *agent, string net,
                           string prefix, string nexthop) {
        TASK_UTIL_EXPECT_TRUE(CheckRoute(agent, net, prefix, nexthop));
    }

    // Check for a route's existence in l3vpn table.
    bool CheckL3VPNRouteExists(BgpServerTestPtr server, string prefix) {
        task_util::TaskSchedulerLock lock;
        BgpTable *table = static_cast<BgpTable *>(
            server->database()->FindTable("bgp.l3vpn-inet6.0"));
        if (!table)
            return false;

        boost::system::error_code error;
        Inet6VpnPrefix nlri = Inet6VpnPrefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        Inet6VpnTable::RequestKey key(nlri, NULL);
        BgpRoute *rt = static_cast<BgpRoute *>(table->Find(&key));
        return(rt != NULL);
    }

    void VerifyL3VPNRouteExists(BgpServerTestPtr server, string prefix) {
        TASK_UTIL_EXPECT_TRUE(CheckL3VPNRouteExists(server, prefix));
    }

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bgp_server1_;
    BgpServerTestPtr bgp_server2_;
    XmppServer *xmpp_server1_;
    XmppServer *xmpp_server2_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_b_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> cm1_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> cm2_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_y1_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_y2_;
    std::vector<std::string> all_encap_list; // statically configured at init
};

// Route from 1 agent shows up on the other.
// Verify nh and label values on both agents.
TEST_F(BgpXmppInet6Test2Peers, Add1Route) {
    Configure(two_cns_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:0:0/96";
    test::NextHop nexthop_a("192.168.1.1");
    test::RouteAttributes attr_a(1000, 1001);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that route showed up on agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt_a =
        agent_a_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_a != NULL);
    int label_a = rt_a->entry.next_hops.next_hop[0].label;
    string nh_a = rt_a->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_a);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_a);
    TASK_UTIL_EXPECT_EQ("blue", rt_a->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_a->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_a->entry.sequence_number);

    // Verify that route showed up on agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt_b =
        agent_b_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_b != NULL);
    int label_b = rt_b->entry.next_hops.next_hop[0].label;
    string nh_b = rt_b->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_b);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_b);
    TASK_UTIL_EXPECT_EQ("blue", rt_b->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_b->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_b->entry.sequence_number);

    // Verify that label and nh are the same on agents A and B.
    TASK_UTIL_EXPECT_EQ(label_a, label_b);
    TASK_UTIL_EXPECT_EQ(nh_a, nh_b);

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

TEST_F(BgpXmppInet6Test2Peers, Add1RouteTwice) {
    Configure(two_cns_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:0:0/96";
    test::NextHop nexthop_a("192.168.1.1");
    test::RouteAttributes attr_a(1000, 1001);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that route showed up on agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt_a = agent_a_->Inet6RouteLookup("blue",
                                                               route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_a != NULL);
    int label_a = rt_a->entry.next_hops.next_hop[0].label;
    string nh_a = rt_a->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_a);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_a);
    TASK_UTIL_EXPECT_EQ("blue", rt_a->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_a->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_a->entry.sequence_number);

    // Verify that route showed up on agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt_b = agent_b_->Inet6RouteLookup("blue",
                                                               route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_b != NULL);
    int label_b = rt_b->entry.next_hops.next_hop[0].label;
    string nh_b = rt_b->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_b);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_b);
    TASK_UTIL_EXPECT_EQ("blue", rt_b->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_b->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_b->entry.sequence_number);

    // Verify that label and nh are the same on agents A and B.
    TASK_UTIL_EXPECT_EQ(label_a, label_b);
    TASK_UTIL_EXPECT_EQ(nh_a, nh_b);

    // Send the same route again. AddInet6Route() will increment the label. So,
    // check for label difference to make sure we have processed the duplicate.
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateLabel("blue", route_a.str(),
                                                (label_a + 1), agent_a_.get()));

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Routes from 2 agents are advertised to each other.
TEST_F(BgpXmppInet6Test2Peers, Add2RoutesSameInstance) {
    Configure(two_cns_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3:0000:0000:8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3:0000:0000:8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Delete route from agent B.
    agent_b_->DeleteInet6Route("blue", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Add and then Update route and verify the next hop and label.
TEST_F(BgpXmppInet6Test2Peers, RouteUpdate) {
    Configure(two_cns_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    test::RouteAttributes attr_a(1000, 1001);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that the route showed up on agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    const autogen::ItemType *rt_b1 =
        agent_b_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_b1 != NULL);
    int label_b1 = rt_b1->entry.next_hops.next_hop[0].label;
    string nh_b1 = rt_b1->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_b1);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_b1);
    TASK_UTIL_EXPECT_EQ("blue", rt_b1->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_b1->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_b1->entry.sequence_number);

    // Change the nexthop and attributes of the route from agent A.
    test::NextHop nexthop_a1("192.168.2.1");
    test::RouteAttributes attr_a1(2000, 2001);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a1, attr_a1);
    task_util::WaitForIdle();

    // Wait for the route to get updated on agent B.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateNexthop("blue",
        route_a.str(), "192.168.2.1", agent_b_.get()));
    const autogen::ItemType *rt_b2 =
        agent_b_->Inet6RouteLookup("blue", route_a.str());

    // Verify that the route is updated on agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_TRUE(rt_b2 != NULL);

    int label_b2 = rt_b2->entry.next_hops.next_hop[0].label;
    string nh_b2 = rt_b2->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_EQ("192.168.2.1", nh_b2);
    TASK_UTIL_EXPECT_EQ("blue", rt_b2->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(2000, rt_b2->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(2001, rt_b2->entry.sequence_number);

    // Verify that label has changed.
    TASK_UTIL_EXPECT_NE(label_b1, label_b2);

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Multiple routes from 2 agents on 2 different servers are advertised to each
// other. Only the prefix changes.
TEST_F(BgpXmppInet6Test2Peers, AddMultipleRoutesSameInstance) {
    Configure(two_cns_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    // Add routes from agent A.
    test::NextHop nexthop_a("192.168.1.1");
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_a;
        route_a << "2001:0db8:85a3:0000:0000:8a2e:aaaa:" << idx << "/128";
        agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    }

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount("blue"));

    // Add route from agent B.
    test::NextHop nexthop_b("192.168.1.2");
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_b;
        route_b << "2001:0db8:85a3:0000:0000:8a2e:bbbb:" << idx << "/128";
        agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    }

    // Verify that routes showed up on the agents.
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount("blue"));

    // Delete routes from agent A.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_a;
        route_a << "2001:0db8:85a3::8a2e:aaaa:" << idx << "/128";
        agent_a_->DeleteInet6Route("blue", route_a.str());
    }

    // Verify deletion.
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent B.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_b;
        route_b << "2001:0db8:85a3::8a2e:bbbb:" << idx << "/128";
        agent_b_->DeleteInet6Route("blue", route_b.str());
    }

    // Verify that there are no routes on the agents.
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Multiple routes from 2 agents on 2 different servers are advertised to each
// other. The prefix and nh changes.
TEST_F(BgpXmppInet6Test2Peers, AddMultipleRoutesSameInstance2) {
    Configure(two_cns_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    // Add routes from agent A.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_a;
        route_a << "2001:0db8:85a3:0000:0000:8a2e:aaaa:" << idx << "/128";
        stringstream nh_ss;
        nh_ss << "192.168.1." << idx;
        test::NextHop nexthop_a(nh_ss.str());
        agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    }

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount("blue"));

    // Add routes from agent B.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_b;
        route_b << "2001:0db8:85a3:0000:0000:8a2e:bbbb:" << idx << "/128";
        stringstream nh_ss;
        nh_ss << "192.168.2." << idx;
        test::NextHop nexthop_b(nh_ss.str());
        agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    }

    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount("blue"));

    // Delete routes from agent A.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_a;
        route_a << "2001:0db8:85a3::8a2e:aaaa:" << idx << "/128";
        agent_a_->DeleteInet6Route("blue", route_a.str());
    }

    // Verify deletion.
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent B.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_b;
        route_b << "2001:0db8:85a3::8a2e:bbbb:" << idx << "/128";
        agent_b_->DeleteInet6Route("blue", route_b.str());
    }

    // Verify that there are no routes on the agents.
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// One agent subscribes to the instance after the other agent has already added
// routes.
TEST_F(BgpXmppInet6Test2Peers, SubscribeLater) {
    Configure(two_cns_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register agent A to blue instance
    agent_a_->Inet6Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3::8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Verify that route showed up on agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Register agent B to blue instance
    agent_b_->Inet6Subscribe("blue", 1);
    task_util::WaitForIdle();

    // Verify that route showed up on both agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    // Add route from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3:0000:0000:8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Delete route from agent B.
    agent_b_->DeleteInet6Route("blue", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Both agents are subscribed to 2 instances and add the same prefixes in each
// instance.
TEST_F(BgpXmppInet6Test2Peers, MultipleInstances) {
    Configure(two_cns_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register both agents to blue and pink instances.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);
    task_util::WaitForIdle();
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);
    task_util::WaitForIdle();

    // Add routes from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3::8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    agent_a_->AddInet6Route("pink", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Add routes from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3:0000:0000:8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    agent_b_->AddInet6Route("pink", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on both agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());

    // Delete routes from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    agent_a_->DeleteInet6Route("pink", route_a.str());
    task_util::WaitForIdle();

    // Delete routes from agent B.
    agent_b_->DeleteInet6Route("blue", route_b.str());
    agent_b_->DeleteInet6Route("pink", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on either agent.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Routes from 2 agents are advertised to each other.
// One agent unsubscribes after routes have been exchanged.
TEST_F(BgpXmppInet6Test2Peers, UnsubscribeOneInstance) {
    Configure(two_cns_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3::8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3:0000:0000:8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());

    // Unregister B to blue instance.
    agent_b_->Inet6Unsubscribe("blue", 1);
    task_util::WaitForIdle();

    // Verify that the route sent by B is gone.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// One agent connects and subscribes to the instance after the other agent
// has already added routes.
TEST_F(BgpXmppInet6Test2Peers, XmppConnectLater) {
    Configure(two_cns_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register agent A to blue instance
    agent_a_->Inet6Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3::8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register agent B to blue instance
    agent_b_->Inet6Subscribe("blue", 1);

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());

    // Add route from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3:0000:0000:8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());

    // Delete route from agent B.
    agent_b_->DeleteInet6Route("blue", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// One agent closes the session after routes are exchanged.
TEST_F(BgpXmppInet6Test2Peers, XmppSessionDown) {
    Configure(two_cns_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3::8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3:0000:0000:8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());

    // Bring down the session to agent B.
    agent_b_->SessionDown();
    task_util::WaitForIdle();

    // Verify that the route sent by B is gone.
    if (xmpp_server2_->IsPeerCloseGraceful()) {
        TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    } else {
        TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    }
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    if (xmpp_server2_->IsPeerCloseGraceful()) {
        TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    } else {
        TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    }

    // Clear agent_b_'s stale routes.
    if (xmpp_server2_->IsPeerCloseGraceful()) {
        agent_b_->SessionUp();
        task_util::WaitForIdle();
        agent_b_->Inet6Subscribe("blue", 1);
        task_util::WaitForIdle();
        agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
        task_util::WaitForIdle();
        agent_b_->DeleteInet6Route("blue", route_b.str());
    }
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());

    // Close the sessions.
    agent_a_->SessionDown();
}

static const char *two_unconnected_instances_add_config_x = "\
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

static const char *two_unconnected_instances_add_config_y = "\
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

static const char *two_bgp_peers_add_config = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>inet6-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>inet6-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
</config>\
";

// BGP session comes up after the the 2 agents have already advertised
// routes to the 2 XMPP servers.
TEST_F(BgpXmppInet6Test2Peers, BgpConnectLater) {
    // Configure the routing instances.
    Configure(bgp_server1_, two_unconnected_instances_add_config_x);
    Configure(bgp_server2_, two_unconnected_instances_add_config_y);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3::8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3::8a2e:0370:bbbb/128";
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_a);
    task_util::WaitForIdle();

    // Verify that routes are reflected to individual agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    // Configure the BGP session.
    Configure(two_bgp_peers_add_config);
    task_util::WaitForIdle();

    // Verify that routes showed up on the remote agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent B.
    agent_b_->DeleteInet6Route("blue", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Routing instances are created on the BGP servers (config) after the the 2
// agents have already advertised routes.
TEST_F(BgpXmppInet6Test2Peers, CreateInstanceConfigLater) {
    // Configure the BGP session.
    Configure(two_bgp_peers_add_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3::8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3::8a2e:0370:bbbb/128";
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_a);
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Configure the routing instances.
    Configure(two_unconnected_instances_add_config_x);
    Configure(two_unconnected_instances_add_config_y);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent B.
    agent_b_->DeleteInet6Route("blue", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// BGP session goes down and comes up after routes have been exchanged. The
// RI's are not connected.
TEST_F(BgpXmppInet6Test2Peers, BgpSessionBounce) {
    Configure(two_cns_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("blue", 1);

    // Add route from agent A.
    stringstream route_a;
    route_a << "2001:0db8:85a3::8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream route_b;
    route_b << "2001:0db8:85a3::8a2e:0370:bbbb/128";
    agent_b_->AddInet6Route("blue", route_b.str(), nexthop_a);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));

    // Bring down the BGP session.
    bgp_server1_->DisableAllPeers();
    bgp_server2_->DisableAllPeers();
    task_util::WaitForIdle();

    // Verify that routes from remote agents are cleaned up.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    // Bring up the BGP session.
    bgp_server1_->EnableAllPeers();
    bgp_server2_->EnableAllPeers();
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent A.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    // Delete route from agent B.
    agent_b_->DeleteInet6Route("blue", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

static const char *two_cns_connected_instances_config = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>inet6-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>inet-vpn</family>\
                <family>inet6-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <virtual-network name='pink'>\
        <network-id>2</network-id>\
    </virtual-network>\
    <virtual-network name='yellow'>\
        <network-id>3</network-id>\
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
    <routing-instance name='yellow'>\
        <virtual-network>yellow</virtual-network>\
        <vrf-target>target:1:3</vrf-target>\
    </routing-instance>\
</config>\
";

// 4 Agents. A & B with blue and pink and export-import policies between them.
// Y1 and Y2 with yellow.
// Create a route in blue and one in yellow.
TEST_F(BgpXmppInet6Test2Peers, MultipleInstancesLeakChecks) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Create XMPP Agent yellow1 connected to XMPP server 1.
    agent_y1_.reset(
        new test::NetworkAgentMock(&evm_, "yellow1", xmpp_server1_->GetPort(),
            "127.0.0.3", "127.0.0.3"));
    TASK_UTIL_EXPECT_TRUE(agent_y1_->IsEstablished());

    // Create XMPP Agent yellow2 connected to XMPP server 1.
    agent_y2_.reset(
        new test::NetworkAgentMock(&evm_, "yellow2", xmpp_server2_->GetPort(),
            "127.0.0.4", "127.0.0.4"));
    TASK_UTIL_EXPECT_TRUE(agent_y2_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Register to yellow from Y1 and Y2.
    agent_y1_->Inet6Subscribe("yellow", 1);
    agent_y2_->Inet6Subscribe("yellow", 1);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    test::RouteAttributes attr_a(1000, 1001);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that route showed up in blue instance on Agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt_a =
        agent_a_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_a != NULL);
    int label_a = rt_a->entry.next_hops.next_hop[0].label;
    string nh_a = rt_a->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_a);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_a);
    TASK_UTIL_EXPECT_EQ("blue", rt_a->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_a->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_a->entry.sequence_number);

    // Verify that route showed up in blue instance on Agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt_b =
        agent_b_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_b != NULL);
    int label_b = rt_b->entry.next_hops.next_hop[0].label;
    string nh_b = rt_b->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_b);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_b);
    TASK_UTIL_EXPECT_EQ("blue", rt_b->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_b->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_b->entry.sequence_number);

    // Verify that label and nh are the same on agents A and B.
    TASK_UTIL_EXPECT_EQ(label_a, label_b);
    TASK_UTIL_EXPECT_EQ(nh_a, nh_b);

    // Verify that route shows up in pink instance on A.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));

    // Verify that route shows up in pink instance on B.
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Verify that route does not leak to Y1 or Y2.
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("pink"));

    // Add route from agent Y1 to yellow instance.
    stringstream route_b;
    route_b << "2001:db8:85a3::8a2e:370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.3");
    test::RouteAttributes attr_b(2000, 2001);
    agent_y1_->AddInet6Route("yellow", route_b.str(), nexthop_b, attr_b);
    task_util::WaitForIdle();

    // Verify that route showed up in yellow instance on Agent Y1.
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount("yellow"));
    const autogen::ItemType *rt_y1 =
        agent_y1_->Inet6RouteLookup("yellow", route_b.str());
    TASK_UTIL_EXPECT_TRUE(rt_y1 != NULL);
    int label_y1 = rt_y1->entry.next_hops.next_hop[0].label;
    string nh_y1 = rt_y1->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_y1);
    TASK_UTIL_EXPECT_EQ("192.168.1.3", nh_y1);
    TASK_UTIL_EXPECT_EQ("yellow", rt_y1->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(2000, rt_y1->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(2001, rt_y1->entry.sequence_number);

    // Verify that route showed up in yellow instance on Agent Y2.
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount("yellow"));
    const autogen::ItemType *rt_y2 =
        agent_y2_->Inet6RouteLookup("yellow", route_b.str());
    TASK_UTIL_EXPECT_TRUE(rt_y2 != NULL);
    int label_y2 = rt_y2->entry.next_hops.next_hop[0].label;
    string nh_y2 = rt_y2->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_y2);
    TASK_UTIL_EXPECT_EQ("192.168.1.3", nh_y2);
    TASK_UTIL_EXPECT_EQ("yellow", rt_y2->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(2000, rt_y2->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(2001, rt_y2->entry.sequence_number);

    TASK_UTIL_EXPECT_EQ(label_y1, label_y2);
    TASK_UTIL_EXPECT_EQ(nh_y1, nh_y2);

    // Number of routes at agents A and B should not go up.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("yellow"));

    // Delete route.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent A.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));

    // Verify that there are no routes on agent B.
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Verify that Y1 and Y2 still have their route.
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount("yellow"));

    // Delete route from yellow.
    agent_y1_->DeleteInet6Route("yellow", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on Y1 and Y2.
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("yellow"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
    agent_y1_->SessionDown();
    agent_y2_->SessionDown();
}

// 4 Agents. A & B with blue and pink and export-import policies between them.
// Create a route in blue.
// Then create Y1 and Y2 with yellow and add a route in yellow.
// Same as MultipleInstancesLeakChecks except that the yellow agents are
// created after the route-add in blue.
TEST_F(BgpXmppInet6Test2Peers, MultipleInstancesLeakChecks1) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    test::RouteAttributes attr_a(1000, 1001);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that route showed up in blue instance on Agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt_a =
        agent_a_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_a != NULL);
    int label_a = rt_a->entry.next_hops.next_hop[0].label;
    string nh_a = rt_a->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_a);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_a);
    TASK_UTIL_EXPECT_EQ("blue", rt_a->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_a->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_a->entry.sequence_number);

    // Verify that route showed up in blue instance on Agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt_b =
        agent_b_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_b != NULL);
    int label_b = rt_b->entry.next_hops.next_hop[0].label;
    string nh_b = rt_b->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_b);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_b);
    TASK_UTIL_EXPECT_EQ("blue", rt_b->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_b->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_b->entry.sequence_number);

    // Verify that label and nh are the same on agents A and B.
    TASK_UTIL_EXPECT_EQ(label_a, label_b);
    TASK_UTIL_EXPECT_EQ(nh_a, nh_b);

    // Verify that route shows up in pink instance on A.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));

    // Verify that route shows up in pink instance on B.
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Create XMPP Agent yellow1 connected to XMPP server 1.
    agent_y1_.reset(
        new test::NetworkAgentMock(&evm_, "yellow1", xmpp_server1_->GetPort(),
            "127.0.0.3", "127.0.0.3"));
    TASK_UTIL_EXPECT_TRUE(agent_y1_->IsEstablished());

    // Create XMPP Agent yellow2 connected to XMPP server 1.
    agent_y2_.reset(
        new test::NetworkAgentMock(&evm_, "yellow2", xmpp_server2_->GetPort(),
            "127.0.0.4", "127.0.0.4"));
    TASK_UTIL_EXPECT_TRUE(agent_y2_->IsEstablished());

    // Register to yellow from Y1 and Y2.
    agent_y1_->Inet6Subscribe("yellow", 1);
    agent_y2_->Inet6Subscribe("yellow", 1);
    usleep(1000);

    // Verify that route does not leak to Y1 or Y2.
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("pink"));

    // Add route from agent Y1 to yellow instance.
    stringstream route_b;
    route_b << "2001:db8:85a3::8a2e:370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.3");
    test::RouteAttributes attr_b(2000, 2001);
    agent_y1_->AddInet6Route("yellow", route_b.str(), nexthop_b, attr_b);
    task_util::WaitForIdle();

    // Verify that route showed up in yellow instance on Agent Y1.
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount("yellow"));
    const autogen::ItemType *rt_y1 =
        agent_y1_->Inet6RouteLookup("yellow", route_b.str());
    TASK_UTIL_EXPECT_TRUE(rt_y1 != NULL);
    int label_y1 = rt_y1->entry.next_hops.next_hop[0].label;
    string nh_y1 = rt_y1->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_y1);
    TASK_UTIL_EXPECT_EQ("192.168.1.3", nh_y1);
    TASK_UTIL_EXPECT_EQ("yellow", rt_y1->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(2000, rt_y1->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(2001, rt_y1->entry.sequence_number);

    // Verify that route showed up in yellow instance on Agent Y2.
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount("yellow"));
    const autogen::ItemType *rt_y2 =
        agent_y2_->Inet6RouteLookup("yellow", route_b.str());
    TASK_UTIL_EXPECT_TRUE(rt_y2 != NULL);
    int label_y2 = rt_y2->entry.next_hops.next_hop[0].label;
    string nh_y2 = rt_y2->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_y2);
    TASK_UTIL_EXPECT_EQ("192.168.1.3", nh_y2);
    TASK_UTIL_EXPECT_EQ("yellow", rt_y2->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(2000, rt_y2->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(2001, rt_y2->entry.sequence_number);

    TASK_UTIL_EXPECT_EQ(label_y1, label_y2);
    TASK_UTIL_EXPECT_EQ(nh_y1, nh_y2);

    // Number of routes at agents A and B should not go up.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("yellow"));

    // Delete route.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent A.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));

    // Verify that there are no routes on agent B.
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Verify that Y1 and Y2 still have their route.
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount("yellow"));

    // Delete route from yellow.
    agent_y1_->DeleteInet6Route("yellow", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on Y1 and Y2.
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("yellow"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
    agent_y1_->SessionDown();
    agent_y2_->SessionDown();
}

// Combination of MultipleInstancesLeakChecks and SubscribeLater i.e.
// configure import/export, agent-A/B connect, A subs to blue, check counts, A
// subs to pink, check counts, B subs to blue, check counts, B subs to pink,
// check counts.
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithStaggeredSubscribeLater) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance from A.
    agent_a_->Inet6Subscribe("blue", 1);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:0db8:85a3::8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Verify that route shows up in blue on Agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));

    // Register to pink instance from A, verify that the route shows up in pink
    // Counts on B should be zero since it has not subscribed yet.
    agent_a_->Inet6Subscribe("pink", 2);
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Register to blue from B, check that route_a shows up in blue but not in
    // pink
    agent_b_->Inet6Subscribe("blue", 1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Register to pink from B, verify that route_a shows up in pink
    agent_b_->Inet6Subscribe("pink", 2);
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Add route from agent B to pink instance.
    stringstream route_b;
    route_b << "2001:0db8:85a3::8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    agent_b_->AddInet6Route("pink", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that both routes show up in both instances on both agents.
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("pink"));

    // Delete route_a.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Delete route_b.
    agent_b_->DeleteInet6Route("pink", route_b.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// A adds 4 routes in blue. B adds 4 routes in blue.
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithMultipleRoutes) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to the instances on both agents.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Add routes from agent A.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_a;
        route_a << "2001:0db8:85a3:0000:0000:8a2e:aaaa:" << idx << "/128";
        stringstream nh_ss;
        nh_ss << "192.168.1." << idx;
        test::NextHop nexthop_a(nh_ss.str());
        agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount("pink"));

    // Add routes from agent B.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_b;
        route_b << "2001:0db8:85a3:0000:0000:8a2e:bbbb:" << idx << "/128";
        stringstream nh_ss;
        nh_ss << "192.168.2." << idx;
        test::NextHop nexthop_b(nh_ss.str());
        agent_b_->AddInet6Route("blue", route_b.str(), nexthop_b);
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(16, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(16, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount("pink"));

    // Delete routes from agent A.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_a;
        route_a << "2001:0db8:85a3::8a2e:aaaa:" << idx << "/128";
        agent_a_->DeleteInet6Route("blue", route_a.str());
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount("pink"));

    // Delete route from agent B.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_b;
        route_b << "2001:0db8:85a3::8a2e:bbbb:" << idx << "/128";
        agent_b_->DeleteInet6Route("blue", route_b.str());
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// A adds 4 routes in blue. B adds 4 routes in pink. Staggered subscribes and
// unsubscribes
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithMultipleRoutes1) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // A registers to blue and B registers to pink
    agent_a_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Add routes from agent A in blue.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_a;
        route_a << "2001:0db8:85a3:0000:0000:8a2e:aaaa:" << idx << "/128";
        stringstream nh_ss;
        nh_ss << "192.168.1." << idx;
        test::NextHop nexthop_a(nh_ss.str());
        agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount("pink"));

    // Add routes from agent B.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_b;
        route_b << "2001:0db8:85a3:0000:0000:8a2e:bbbb:" << idx << "/128";
        stringstream nh_ss;
        nh_ss << "192.168.2." << idx;
        test::NextHop nexthop_b(nh_ss.str());
        agent_b_->AddInet6Route("pink", route_b.str(), nexthop_b);
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount());
    // A's blue contains 4 blue and 4 imported pink routes
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    // B's pink contains 4 pink and 4 imported blue routes
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount("pink"));

    agent_a_->Inet6Subscribe("pink", 2);
    agent_b_->Inet6Subscribe("blue", 1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(16, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount("blue"));
    // A's pink contains 4 blue and 4 imported pink routes
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(16, agent_b_->Inet6RouteCount());
    // B's blue contains 4 imported blue and 4 pink routes
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount("pink"));

    // B unsubs from blue
    agent_b_->Inet6Unsubscribe("blue", 1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(16, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount("pink"));

    // A unsubs from pink
    agent_a_->Inet6Unsubscribe("pink", 2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->Inet6RouteCount("pink"));

    // Delete blue routes from agent A.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_a;
        route_a << "2001:0db8:85a3::8a2e:aaaa:" << idx << "/128";
        agent_a_->DeleteInet6Route("blue", route_a.str());
    }
    task_util::WaitForIdle();
    // A is unsubscibed from pink. So, pink wont have any routes. A's blue will
    // have 4 imported pink routes.
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    // B is unsubscibed from blue. So, blue wont have any routes. B's pink will
    // have 4 pink routes.
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount("pink"));

    // Delete route from agent B.
    for (int idx = 1; idx <= 4; ++idx) {
        stringstream route_b;
        route_b << "2001:0db8:85a3::8a2e:bbbb:" << idx << "/128";
        agent_b_->DeleteInet6Route("pink", route_b.str());
    }
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// A and B add routes with encap. Then A changes the encap of the route a few
// times i.e.  cycle through udp, all and back to gre.
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithEncapAddChange) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1", 0, "gre");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Add route from agent B to pink instance.
    stringstream route_b;
    route_b << "2001:db8:85a3::8a2e:370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2", 0, "udp");
    agent_b_->AddInet6Route("pink", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that routes show up in both instances on Agent A.
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("pink"));

    // Verify that routes show up in both instances on Agent B.
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("pink"));

    // Verify that agent A has the right encap for all his routes.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(), "gre",
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(), "gre",
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_b.str(), "udp",
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_b.str(), "udp",
                                                 agent_a_.get()));

    // Verify that agent B has the right encap for all his routes.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(), "gre",
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(), "gre",
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_b.str(), "udp",
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_b.str(), "udp",
                                                 agent_b_.get()));

    // Change encap for route_a to 'udp'.
    test::NextHop nexthop_a1("192.168.1.1", 0, "udp");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a1);

    // Verify that the encap has changed on both agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(), "udp",
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(), "udp",
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(), "udp",
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(), "udp",
                                                 agent_b_.get()));

    // Change encap for route_a to 'all'.
    test::NextHop nexthop_a2("192.168.1.1", 0, "all_ipv6");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a2);

    // Verify that the encap has changed on both agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(),
                                                 "all_ipv6", agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(),
                                                 "all_ipv6", agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(),
                                                 "all_ipv6", agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(),
                                                 "all_ipv6", agent_b_.get()));

    // Change encap for route_a to 'gre'.
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);

    // Verify that the encap has changed on both agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(),
                                                 "gre", agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(),
                                                 "gre", agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(),
                                                 "gre", agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(),
                                                 "gre", agent_b_.get()));

    // Delete routes.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();
    agent_b_->DeleteInet6Route("pink", route_b.str());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Agent A adds a route, then xmpp goes down, route changes with new encap, XMPP
// comes up. Check encaps.
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithEncapAddChangeXmppDown) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1", 0, "gre");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Verify that routes show up in both instances on Agent A.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));

    // Verify that routes show up in both instances on Agent B.
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Verify the encap on both agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(), "gre",
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(), "gre",
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(), "gre",
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(), "gre",
                                                 agent_b_.get()));

    // Bring down the session to agent B.
    agent_b_->SessionDown();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());

    // Change encap for route_a.
    test::NextHop nexthop_a1("192.168.1.1", 0, "all_ipv6");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a1);
    task_util::WaitForIdle();

    // Bring up the session to agent B.
    agent_b_->SessionUp();
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());

    // Verify that the encap has changed on both agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(),
                                                 "all_ipv6", agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(),
                                                 "all_ipv6", agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(),
                                                 "all_ipv6", agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(),
                                                 "all_ipv6", agent_b_.get()));

    // Delete routes.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

TEST_F(BgpXmppInet6Test2Peers, ImportExportWithEncapAddChangeUnsub) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1", 0, "gre");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Verify that routes show up in both instances on Agent A.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));

    // Verify that routes show up in both instances on Agent B.
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Verify the encap on both agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(), "gre",
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(), "gre",
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(), "gre",
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(), "gre",
                                                 agent_b_.get()));

    // Unsubscribe from pink.
    agent_b_->Inet6Unsubscribe("pink", 2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());

    // Change encap for route_a.
    test::NextHop nexthop_a1("192.168.1.1", 0, "all_ipv6");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a1);
    task_util::WaitForIdle();

    // Subscribe to pink again.
    agent_b_->Inet6Subscribe("pink", 2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());

    // Verify that the encap has changed on both agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(),
                                                 "all_ipv6", agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(),
                                                 "all_ipv6", agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(),
                                                 "all_ipv6", agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(),
                                                 "all_ipv6", agent_b_.get()));

    // Delete routes.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Agent A adds a route, then bgp peering goes down, route changes with new
// encap, peering comes up. Check encaps.
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithEncapAddChangeBgpBounce) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1", 0, "gre");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Verify that routes show up in both instances on Agent A.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));

    // Verify that routes show up in both instances on Agent B.
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Verify the encap on both agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(), "gre",
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(), "gre",
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(), "gre",
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(), "gre",
                                                 agent_b_.get()));

    // Bring down the BGP session.
    // B should not have routes now.
    bgp_server1_->DisableAllPeers();
    bgp_server2_->DisableAllPeers();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());

    // Change encap for route_a.
    test::NextHop nexthop_a1("192.168.1.1", 0, "all_ipv6");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a1);
    task_util::WaitForIdle();

    // Bring up the BGP session.
    // B should have routes now.
    bgp_server1_->EnableAllPeers();
    bgp_server2_->EnableAllPeers();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());

    // Verify that the encap has changed on both agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(),
                                                 "all_ipv6", agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(),
                                                 "all_ipv6", agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("blue", route_a.str(),
                                                 "all_ipv6", agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateEncap("pink", route_a.str(),
                                                 "all_ipv6", agent_b_.get()));

    // Delete routes.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// A and B add routes with sgid-lists. Agent A changes the route with new longer
// sgid-list, then A changes the route with shorter sgid-list.
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithSGidAddChange) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    vector<int> sgids_a;
    sgids_a.push_back(111);
    test::RouteAttributes attr_a(sgids_a);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Add route from agent B to pink instance.
    stringstream route_b;
    route_b << "2001:db8:85a3::8a2e:370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    vector<int> sgids_b;
    sgids_b.push_back(211);
    test::RouteAttributes attr_b(sgids_b);
    agent_b_->AddInet6Route("pink", route_b.str(), nexthop_b, attr_b);
    task_util::WaitForIdle();

    // Verify that routes show up in both instances on Agent A.
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("pink"));

    // Verify that routes show up in both instances on Agent B.
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("pink"));

    // Verify that agent A has the right sgid-list for both routes.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_b.str(), sgids_b,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_b.str(), sgids_b,
                                                 agent_a_.get()));

    // Verify that agent B has the right sgid-list for both routes.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_b.str(), sgids_b,
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_b.str(), sgids_b,
                                                 agent_b_.get()));

    // Increase the size of the sgid-list for route_a and verify at agent.
    sgids_a.push_back(112);
    sgids_a.push_back(113);
    attr_a.SetSg(sgids_a);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_b_.get()));

    // Decrease the size of the sgid-list for route_a and verify at agent.
    sgids_a.pop_back();
    attr_a.SetSg(sgids_a);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_b_.get()));

    // Delete routes.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();
    agent_b_->DeleteInet6Route("pink", route_b.str());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Add route with community
TEST_F(BgpXmppInet6Test2Peers, RouteWithCommunity) {
    Configure(two_cns_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue from A.
    agent_a_->Inet6Subscribe("blue", 1);

    // Register to blue from B.
    agent_b_->Inet6Subscribe("blue", 1);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");

    vector<std::string> community_a;
    community_a.push_back("no-reoriginate");
    test::RouteAttributes attr_a(community_a);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that routes show up on Agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));

    // Verify that routes show up on Agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    // Verify the community-list at both the agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateCommunities("blue", route_a.str(),
                                               community_a, agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateCommunities("blue", route_a.str(),
                                               community_a, agent_b_.get()));

    // Add one more community to the route
    community_a.push_back("64512:8888");
    attr_a.SetCommunities(community_a);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    sort(community_a.begin(), community_a.end());

    // Verify the community-list at both the agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateCommunities("blue", route_a.str(),
                                               community_a, agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateCommunities("blue", route_a.str(),
                                               community_a, agent_b_.get()));

    // Delete route
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Add route with NO_EXPORT community and verify that route is
// not published to other control-node
TEST_F(BgpXmppInet6Test2Peers, RouteWithNoExportCommunity) {
    Configure(config_2_control_nodes_different_asn);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue from A.
    agent_a_->Inet6Subscribe("blue", 1);

    // Register to blue from B.
    agent_b_->Inet6Subscribe("blue", 1);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");

    vector<std::string> community_a;
    community_a.push_back("no-export");
    test::RouteAttributes attr_a(community_a);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that routes show up on Agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));

    // Verify that route doesn't show up on Agent B.
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Verify the community-list at agent A
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateCommunities("blue", route_a.str(),
                                               community_a, agent_a_.get()));
    // Delete route
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}


// Add route with tag-list
TEST_F(BgpXmppInet6Test2Peers, RouteWithTagList) {
    Configure(two_cns_unconnected_instances_config);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue from A.
    agent_a_->Inet6Subscribe("blue", 1);

    // Register to blue from B.
    agent_b_->Inet6Subscribe("blue", 1);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    vector<int> tag_list = list_of (1)(2);
    test::NextHop nexthop_a("192.168.1.1", 0, tag_list);

    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Verify that routes show up on Agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));

    // Verify that routes show up on Agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));

    // Verify the community-list at both the agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateTagList("blue", route_a.str(),
                                               tag_list, agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateTagList("blue", route_a.str(),
                                               tag_list, agent_b_.get()));

    // Delete route
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Agent A adds route with sgid-lists. Agent B's xmpp session goes down, agent A
// changes the route with new longer sgid-list, xmpp session comes up.
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithSGidAddChangeXmppDown) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    vector<int> sgids_a;
    sgids_a.push_back(111);
    test::RouteAttributes attr_a(sgids_a);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that routes show up in both instances on Agent A.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));

    // Verify that routes show up in both instances on Agent B.
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Verify the sgid-list at both the agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_b_.get()));

    // Bring down the session to agent B.
    agent_b_->SessionDown();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());

    // Increase the size of the sgid-list for route_a and verify at agent A. B
    // already has no routes.
    sgids_a.push_back(112);
    sgids_a.push_back(113);
    attr_a.SetSg(sgids_a);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_a_.get()));

    // Bring up the session to agent B. B should have the new sgid-list now.
    agent_b_->SessionUp();
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_b_.get()));

    // Delete routes.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Agent A adds route with sgid-lists. Agent B unsubs from pink, agent A
// changes the route with new longer sgid-list, agent A subs to pink, agent A
// changes the route with shorted sgid-list.
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithSGidAddChangeUnsub) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    vector<int> sgids_a;
    sgids_a.push_back(111);
    test::RouteAttributes attr_a(sgids_a);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that routes show up in both instances on Agent A.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));

    // Verify that routes show up in both instances on Agent B.
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Verify the sgid-list at both the agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_b_.get()));

    // Unsubscribe from pink.
    agent_b_->Inet6Unsubscribe("pink", 2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());

    // Increase the size of the sgid-list for route_a and verify at agent A.
    // Only blue should see the route with the new sgid-list in agent B.
    sgids_a.push_back(112);
    sgids_a.push_back(113);
    attr_a.SetSg(sgids_a);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_b_.get()));

    // Subscribe for pink again. B's pink should have the new sgid-list now.
    agent_b_->Inet6Subscribe("pink", 2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_b_.get()));

    // Reduce the size of the sgid-list for route_a and verify at agents.
    sgids_a.pop_back();
    attr_a.SetSg(sgids_a);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_b_.get()));

    // Delete routes.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Agent A adds route with sgid-lists. Bgp-peering goes down. Agent A
// changes the route with new longer sgid-list, bgp peering comes up.
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithSGidAddChangeBgpBounce) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    vector<int> sgids_a;
    sgids_a.push_back(111);
    test::RouteAttributes attr_a(sgids_a);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that routes show up in both instances on Agent A.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));

    // Verify that routes show up in both instances on Agent B.
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Verify the sgid-list at both the agents.
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_b_.get()));

    // Bring down the BGP session.
    bgp_server1_->DisableAllPeers();
    bgp_server2_->DisableAllPeers();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());

    // Increase the size of the sgid-list for route_a and verify at agent A. B
    // already has no routes.
    sgids_a.push_back(112);
    sgids_a.push_back(113);
    attr_a.SetSg(sgids_a);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_a_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_a_.get()));

    // Bring up the BGP session.
    // B should have the new sgid-list now.
    bgp_server1_->EnableAllPeers();
    bgp_server2_->EnableAllPeers();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("blue", route_a.str(), sgids_a,
                                                 agent_b_.get()));
    TASK_UTIL_EXPECT_TRUE(VerifyRouteUpdateSGids("pink", route_a.str(), sgids_a,
                                                 agent_b_.get()));

    // Delete routes.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// two_cns_connected_instances_config
// A adds 2 routes in blue, check counts, B unsubs pink, check counts, B unsubs
// blue, check counts, A unsubs pink, check counts.
TEST_F(BgpXmppInet6Test2Peers, ImportExportAgentUnsub) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to the instances on both agents.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Add route from agent A to blue instance.
    stringstream route_a1;
    route_a1 << "2001:0db8:85a3::8a2e:0370:aaa1/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a1.str(), nexthop_a);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Add second route from agent A to blue instance.
    stringstream route_a2;
    route_a2 << "2001:0db8:85a3::8a2e:0370:aaa2/128";
    agent_a_->AddInet6Route("blue", route_a2.str(), nexthop_a);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("pink"));

    agent_b_->Inet6Unsubscribe("pink", 2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    agent_b_->Inet6Unsubscribe("blue", 1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    agent_a_->Inet6Unsubscribe("pink", 2);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    agent_a_->Inet6Unsubscribe("blue", 1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Combination of MultipleInstancesLeakChecks and XmppSessionDown i.e.
// configure import/export, add routes in both blue and pink, check counts,
// bring one xmpp session down.
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithXmppDown) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:0db8:85a3::8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Add route from agent B to pink instance.
    stringstream route_b;
    route_b << "2001:0db8:85a3::8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    agent_b_->AddInet6Route("pink", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that routes show up in both instances on Agent A.
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("pink"));

    // Verify that routes show up in both instances on Agent B.
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("pink"));

    // Bring down the session to agent B.
    agent_b_->SessionDown();

    if (xmpp_server1_->IsPeerCloseGraceful()) {
        TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
        TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
        TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("pink"));
    } else {
        TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
        TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
        TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));
    }
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Delete route.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent A.
    if (xmpp_server1_->IsPeerCloseGraceful()) {
        TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());
        TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
        TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));
    } else {
        TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
        TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
        TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    }
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Combination of MultipleInstancesLeakChecks and XmppConnectLater i.e.
// configure import/export, A connects, adds route in blue, check counts,
// B connects, check counts, adds routes in pink, check counts.
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithXmppConnectLater) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:0db8:85a3::8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Verify that routes show up in both instances on Agent A.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue from B and check counts.
    agent_b_->Inet6Subscribe("blue", 1);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Verify that route_a shows up in both instances on Agent B.
    agent_b_->Inet6Subscribe("pink", 2);
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Add route from agent B to pink instance.
    stringstream route_b;
    route_b << "2001:0db8:85a3::8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.2");
    agent_b_->AddInet6Route("pink", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that routes show up in both instances on both agents.
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("pink"));

    // Delete route_a.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Delete route_b.
    agent_b_->DeleteInet6Route("pink", route_b.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

// Same as MultipleInstancesLeakChecks except that we unconfigure bgp-peering
// and then configure it again between the route-adds and route-deletes.
// While the peering is down, we add one route.
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithBgpBounce) {
    Configure(two_cns_connected_instances_config);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Create XMPP Agent yellow1 connected to XMPP server 1.
    agent_y1_.reset(
        new test::NetworkAgentMock(&evm_, "yellow1", xmpp_server1_->GetPort(),
            "127.0.0.3", "127.0.0.3"));
    TASK_UTIL_EXPECT_TRUE(agent_y1_->IsEstablished());

    // Create XMPP Agent yellow2 connected to XMPP server 1.
    agent_y2_.reset(
        new test::NetworkAgentMock(&evm_, "yellow2", xmpp_server2_->GetPort(),
            "127.0.0.4", "127.0.0.4"));
    TASK_UTIL_EXPECT_TRUE(agent_y2_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Register to yellow from Y1 and Y2.
    agent_y1_->Inet6Subscribe("yellow", 1);
    agent_y2_->Inet6Subscribe("yellow", 1);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    test::RouteAttributes attr_a(1000, 1001);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that route showed up in blue instance on Agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt_a_blue =
        agent_a_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_a_blue != NULL);
    int label_a_blue = rt_a_blue->entry.next_hops.next_hop[0].label;
    string nh_a_blue = rt_a_blue->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_a_blue);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_a_blue);
    TASK_UTIL_EXPECT_EQ("blue", rt_a_blue->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_a_blue->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_a_blue->entry.sequence_number);

    // Verify that route showed up in blue instance on Agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt_b_blue =
        agent_b_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_b_blue != NULL);
    int label_b_blue = rt_b_blue->entry.next_hops.next_hop[0].label;
    string nh_b_blue = rt_b_blue->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_b_blue);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_b_blue);
    TASK_UTIL_EXPECT_EQ("blue", rt_b_blue->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_b_blue->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_b_blue->entry.sequence_number);

    // Verify that label and nh are the same on agents A and B.
    TASK_UTIL_EXPECT_EQ(label_a_blue, label_b_blue);
    TASK_UTIL_EXPECT_EQ(nh_a_blue, nh_b_blue);

    // Verify that route shows up in pink instance on A.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));
    const autogen::ItemType *rt_a_pink =
        agent_a_->Inet6RouteLookup("pink", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_a_pink != NULL);
    int label_a_pink = rt_a_pink->entry.next_hops.next_hop[0].label;
    string nh_a_pink = rt_a_pink->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_a_pink);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_a_pink);
    TASK_UTIL_EXPECT_EQ("blue", rt_a_pink->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_a_pink->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_a_pink->entry.sequence_number);

    // Verify that route shows up in pink instance on B.
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));
    const autogen::ItemType *rt_b_pink =
        agent_b_->Inet6RouteLookup("pink", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_b_pink != NULL);
    int label_b_pink = rt_b_pink->entry.next_hops.next_hop[0].label;
    string nh_b_pink = rt_b_pink->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_b_pink);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_b_pink);
    TASK_UTIL_EXPECT_EQ("blue", rt_b_pink->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_b_pink->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_b_pink->entry.sequence_number);

    TASK_UTIL_EXPECT_EQ(label_a_pink, label_b_pink);
    TASK_UTIL_EXPECT_EQ(nh_a_pink, nh_b_pink);

    // Verify that route does not leak to Y1 or Y2.
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount());

    // Add route from agent Y1 to yellow instance.
    stringstream route_b;
    route_b << "2001:db8:85a3::8a2e:370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.3");
    agent_y1_->AddInet6Route("yellow", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that route showed up in yellow instance on Agent Y1.
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount("yellow"));
    const autogen::ItemType *rt_y1 =
        agent_y1_->Inet6RouteLookup("yellow", route_b.str());
    TASK_UTIL_EXPECT_TRUE(rt_y1 != NULL);
    int label_y1 = rt_y1->entry.next_hops.next_hop[0].label;
    string nh_y1 = rt_y1->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_y1);
    TASK_UTIL_EXPECT_EQ("192.168.1.3", nh_y1);
    TASK_UTIL_EXPECT_EQ("yellow", rt_y1->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(test::RouteAttributes::GetDefaultLocalPref(),
                        rt_y1->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(test::RouteAttributes::GetDefaultSequence(),
                        rt_y1->entry.sequence_number);

    // Verify that route showed up in yellow instance on Agent Y2.
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount("yellow"));
    const autogen::ItemType *rt_y2 =
        agent_y2_->Inet6RouteLookup("yellow", route_b.str());
    TASK_UTIL_EXPECT_TRUE(rt_y2 != NULL);
    int label_y2 = rt_y2->entry.next_hops.next_hop[0].label;
    string nh_y2 = rt_y2->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_y2);
    TASK_UTIL_EXPECT_EQ("192.168.1.3", nh_y2);
    TASK_UTIL_EXPECT_EQ("yellow", rt_y2->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(test::RouteAttributes::GetDefaultLocalPref(),
                        rt_y2->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(test::RouteAttributes::GetDefaultSequence(),
                        rt_y2->entry.sequence_number);

    TASK_UTIL_EXPECT_EQ(label_y1, label_y2);
    TASK_UTIL_EXPECT_EQ(nh_y1, nh_y2);

    // Number of routes at agents A and B should not go up.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("yellow"));

    // Bring down the BGP session.
    bgp_server1_->DisableAllPeers();
    bgp_server2_->DisableAllPeers();
    task_util::WaitForIdle();

    // B and Y2 should have no routes. No change at A and Y1.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("yellow"));

    // Add route from agent B to pink instance while the peering is down.
    stringstream route_c;
    route_c << "2001:0db8:85a3::8a2e:0370:cccc/128";
    agent_b_->AddInet6Route("pink", route_c.str(), nexthop_a);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("yellow"));

    // Bring up the BGP session.
    // B and Y2 should have routes now.
    bgp_server1_->EnableAllPeers();
    bgp_server2_->EnableAllPeers();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(4, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount("yellow"));

    // Delete route.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent A.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));

    // Verify that there are no routes on agent B.
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Verify that Y1 and Y2 still have their 'yellow' route.
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount("yellow"));

    // Delete route from yellow.
    agent_y1_->DeleteInet6Route("yellow", route_b.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on Y1 and Y2.
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("yellow"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
    agent_y1_->SessionDown();
    agent_y2_->SessionDown();
}

static const char *three_instances_add_config_x = "\
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
    <virtual-network name='yellow'>\
        <network-id>3</network-id>\
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
    <routing-instance name='yellow'>\
        <virtual-network>yellow</virtual-network>\
        <vrf-target>target:1:3</vrf-target>\
    </routing-instance>\
</config>\
";

static const char *three_instances_add_config_y = "\
<config>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
    </bgp-router>\
    <virtual-network name='blue'>\
        <network-id>1</network-id>\
    </virtual-network>\
    <virtual-network name='pink'>\
        <network-id>2</network-id>\
    </virtual-network>\
    <virtual-network name='yellow'>\
        <network-id>3</network-id>\
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
    <routing-instance name='yellow'>\
        <virtual-network>yellow</virtual-network>\
        <vrf-target>target:1:3</vrf-target>\
    </routing-instance>\
</config>\
";

// Combination of MultipleInstancesLeakChecks and CreateInstanceConfigLater i.e.
// export + instances are created after the routes have been advertised
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithCreateInstanceConfigLater) {
    // Configure the bgp peers.
    Configure(two_bgp_peers_add_config);

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Create XMPP Agent yellow1 connected to XMPP server 1.
    agent_y1_.reset(
        new test::NetworkAgentMock(&evm_, "yellow1", xmpp_server1_->GetPort(),
            "127.0.0.3", "127.0.0.3"));
    TASK_UTIL_EXPECT_TRUE(agent_y1_->IsEstablished());

    // Create XMPP Agent yellow2 connected to XMPP server 1.
    agent_y2_.reset(
        new test::NetworkAgentMock(&evm_, "yellow2", xmpp_server2_->GetPort(),
            "127.0.0.4", "127.0.0.4"));
    TASK_UTIL_EXPECT_TRUE(agent_y2_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Register to yellow from Y1 and Y2.
    agent_y1_->Inet6Subscribe("yellow", 1);
    agent_y2_->Inet6Subscribe("yellow", 1);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:0db8:85a3::8a2e:0370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Add route from agent Y1 to yellow instance.
    stringstream route_b;
    route_b << "2001:0db8:85a3::8a2e:0370:bbbb/128";
    test::NextHop nexthop_b("192.168.1.3");
    agent_y1_->AddInet6Route("yellow", route_b.str(), nexthop_b);
    task_util::WaitForIdle();

    // Verify that there are no routes on the agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("yellow"));

    // Configure the routing instances.
    Configure(bgp_server1_, three_instances_add_config_x);
    Configure(bgp_server2_, three_instances_add_config_y);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount("yellow"));

    // Delete the routes
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y1_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_y2_->Inet6RouteCount("yellow"));

    agent_y1_->DeleteInet6Route("yellow", route_b.str());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("yellow"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
    agent_y1_->SessionDown();
    agent_y2_->SessionDown();
}

// Combination of MultipleInstancesLeakChecks and BgpConnectLater i.e.
// export + BGP session comes up after the routes have been advertised
TEST_F(BgpXmppInet6Test2Peers, ImportExportWithBgpConnectLater) {
    // Configure the routing instances.
    Configure(bgp_server1_, three_instances_add_config_x);
    Configure(bgp_server2_, three_instances_add_config_y);
    task_util::WaitForIdle();

    // Make sure that the config got applied properly on bgp-server 1.
    RoutingInstanceMgr *mgr_1 = bgp_server1_->routing_instance_mgr();
    RoutingInstance *blue_ri1 = mgr_1->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri1->GetImportList().size());
    RoutingInstance *pink_ri1 = mgr_1->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri1 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri1->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri1->GetImportList().size());

    // Make sure that the config got applied properly on bgp-server 2.
    RoutingInstanceMgr *mgr_2 = bgp_server2_->routing_instance_mgr();
    RoutingInstance *blue_ri2 = mgr_2->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_ri2->GetImportList().size());
    RoutingInstance *pink_ri2 = mgr_2->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_ri2 != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_ri2->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_ri2->GetImportList().size());

    // Create XMPP Agent A connected to XMPP server 1.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server 2.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Create XMPP Agent yellow1 connected to XMPP server 1.
    agent_y1_.reset(
        new test::NetworkAgentMock(&evm_, "yellow1", xmpp_server1_->GetPort(),
            "127.0.0.3", "127.0.0.3"));
    TASK_UTIL_EXPECT_TRUE(agent_y1_->IsEstablished());

    // Create XMPP Agent yellow2 connected to XMPP server 1.
    agent_y2_.reset(
        new test::NetworkAgentMock(&evm_, "yellow2", xmpp_server2_->GetPort(),
            "127.0.0.4", "127.0.0.4"));
    TASK_UTIL_EXPECT_TRUE(agent_y2_->IsEstablished());

    // Register to blue and pink instances from A.
    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->Inet6Subscribe("pink", 2);

    // Register to blue and pink instances from B.
    agent_b_->Inet6Subscribe("blue", 1);
    agent_b_->Inet6Subscribe("pink", 2);

    // Register to yellow from Y1 and Y2.
    agent_y1_->Inet6Subscribe("yellow", 1);
    agent_y2_->Inet6Subscribe("yellow", 1);

    // Add route from agent A to blue instance.
    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:370:aaaa/128";
    test::NextHop nexthop_a("192.168.1.1");
    test::RouteAttributes attr_a(1000, 1001);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a, attr_a);
    task_util::WaitForIdle();

    // Verify that route showed up in blue instance on Agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt_a =
        agent_a_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_a != NULL);
    int label_a = rt_a->entry.next_hops.next_hop[0].label;
    string nh_a = rt_a->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_a);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_a);
    TASK_UTIL_EXPECT_EQ("blue", rt_a->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_a->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_a->entry.sequence_number);

    // Check counts for all agents for xmpp_server1_
    TASK_UTIL_EXPECT_EQ(2, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount("yellow"));

    // No BGP peering yet. Check counts for all agents for xmpp_server2_
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("yellow"));

    // Configure the BGP session.
    Configure(two_bgp_peers_add_config);
    task_util::WaitForIdle();

    // Verify that route showed up in blue instance on Agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    const autogen::ItemType *rt_b =
        agent_b_->Inet6RouteLookup("blue", route_a.str());
    TASK_UTIL_EXPECT_TRUE(rt_b != NULL);
    int label_b = rt_b->entry.next_hops.next_hop[0].label;
    string nh_b = rt_b->entry.next_hops.next_hop[0].address;
    TASK_UTIL_EXPECT_NE(0xFFFFF, label_b);
    TASK_UTIL_EXPECT_EQ("192.168.1.1", nh_b);
    TASK_UTIL_EXPECT_EQ("blue", rt_b->entry.virtual_network);
    TASK_UTIL_EXPECT_EQ(1000, rt_b->entry.local_preference);
    TASK_UTIL_EXPECT_EQ(1001, rt_b->entry.sequence_number);

    // Verify that label and nh are the same on agents A and B.
    TASK_UTIL_EXPECT_EQ(label_a, label_b);
    TASK_UTIL_EXPECT_EQ(nh_a, nh_b);

    // Verify that route shows up in pink instance on B.
    TASK_UTIL_EXPECT_EQ(2, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("pink"));

    // Verify that route does not leak to Y1 or Y2.
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("pink"));

    // Delete route.
    agent_a_->DeleteInet6Route("blue", route_a.str());
    task_util::WaitForIdle();

    // Verify that there are no routes on agent A.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount("pink"));

    // Verify that there are no routes on agent B.
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(0, agent_b_->Inet6RouteCount("pink"));

    // Verify that Y1 and Y2 still have no routes.
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y1_->Inet6RouteCount("yellow"));
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_y2_->Inet6RouteCount("yellow"));

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
    agent_y1_->SessionDown();
    agent_y2_->SessionDown();
}

static const char *config_1_cluster_seed_1_vn = "\
<config>\
    <global-system-config>\
        <rd-cluster-seed>100</rd-cluster-seed>\
    </global-system-config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
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

static const char *config_2_cluster_seed_1_vn = "\
<config>\
    <global-system-config>\
        <rd-cluster-seed>200</rd-cluster-seed>\
    </global-system-config>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.2</identifier>\
        <address>127.0.0.2</address>\
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

static const char *config_1_cluster_seed_1_vn_new_seed = "\
<config>\
    <global-system-config>\
        <rd-cluster-seed>101</rd-cluster-seed>\
    </global-system-config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
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
// Two agents connected to two different xmpp/bgp servers: same VN on
// different CNs. Each CN has a different route distinguisher cluster
// seed to create unique RD values. Even though the agents advertise
// the same route, the l3vpn table will store them as two different
// VPN prefixes.
//
TEST_F(BgpXmppInet6Test2Peers, ClusterSeedTest) {
    Configure(bgp_server1_, config_1_cluster_seed_1_vn);
    task_util::WaitForIdle();
    Configure(bgp_server2_, config_2_cluster_seed_1_vn);
    task_util::WaitForIdle();

    // Create XMPP Agent A connected to XMPP server X.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xmpp_server1_->GetPort(),
            "127.0.0.1", "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Create XMPP Agent B connected to XMPP server Y.
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xmpp_server2_->GetPort(),
            "127.0.0.2", "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // Register to blue instance
    agent_a_->Inet6Subscribe("blue", 1);
    task_util::WaitForIdle();
    agent_b_->Inet6Subscribe("blue", 1);
    task_util::WaitForIdle();

    stringstream route_a;
    route_a << "2001:db8:85a3::8a2e:0:0/96";
    test::NextHop nexthop_a("192.168.1.1");

    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();
    // Verify that route showed up in blue instance on Agent A.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount("blue"));
    VerifyRouteExists(agent_a_.get(), "blue", route_a.str(), "192.168.1.1");

    agent_b_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();

    // Verify that route showed up in blue instance on Agent B.
    TASK_UTIL_EXPECT_EQ(1, agent_b_->Inet6RouteCount("blue"));
    VerifyRouteExists(agent_b_.get(), "blue", route_a.str(), "192.168.1.1");

    // Check l3vpn table to verify route with correct RD
    VerifyL3VPNRouteExists(bgp_server1_, "0.100.1.1:1:2001:db8:85a3::8a2e:0:0/96");
    task_util::WaitForIdle();

    VerifyL3VPNRouteExists(bgp_server2_, "0.200.1.1:1:2001:db8:85a3::8a2e:0:0/96");
    task_util::WaitForIdle();

    // Reconfigure bgp_server1 with a different cluster seed.
    Configure(bgp_server1_, config_1_cluster_seed_1_vn_new_seed);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(101, bgp_server1_->global_config()->rd_cluster_seed());

    // Check the agent connection is no longer in Established state.
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());

    // Check the agent connection is back up.
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Inet6Subscribe("blue", 1);
    agent_a_->AddInet6Route("blue", route_a.str(), nexthop_a);
    task_util::WaitForIdle();
    VerifyRouteExists(agent_a_.get(), "blue", route_a.str(), "192.168.1.1");

    // Check l3vpn table to verify route with correct RD
    VerifyL3VPNRouteExists(bgp_server1_, "0.101.1.1:1:2001:db8:85a3::8a2e:0:0/96");
    task_util::WaitForIdle();

    // Close the sessions.
    agent_a_->SessionDown();
    agent_b_->SessionDown();
}

class BgpXmppInet6ErrorTest : public BgpXmppInet6Test {
protected:
    virtual void SetUp() {
        BgpXmppInet6Test::SetUp();

        Configure(one_cn_unconnected_instances_config);
        task_util::WaitForIdle();

        agent_a_.reset(new test::NetworkAgentMock(
                       &evm_, "agent-a", xmpp_server_->GetPort(), "127.0.0.1"));
        TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
        agent_a_->Inet6Subscribe("blue", 1);
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        agent_a_->SessionDown();
        task_util::WaitForIdle();

        BgpXmppInet6Test::TearDown();
    }
};

TEST_F(BgpXmppInet6ErrorTest, BadPrefix) {
    BgpXmppChannel *channel = bgp_channel_manager_->FindChannel("agent-a");
    const BgpXmppChannel::ErrorStats &err_stats = channel->error_stats();
    EXPECT_TRUE(channel != NULL);
    TASK_UTIL_EXPECT_EQ(err_stats.get_inet6_rx_bad_prefix_count(), 0U);
    // Prefix has two "::"'s.
    test::NextHop nexthop_a("192.168.1.1");
    agent_a_->AddInet6Route("blue", "2001:db8:85a3::8a2e::370:aaaa/128",
                            nexthop_a);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(err_stats.get_inet6_rx_bad_prefix_count(), 1U);
}

TEST_F(BgpXmppInet6ErrorTest, BadNexthop) {
    BgpXmppChannel *channel = bgp_channel_manager_->FindChannel("agent-a");
    const BgpXmppChannel::ErrorStats &err_stats = channel->error_stats();
    EXPECT_TRUE(channel != NULL);
    TASK_UTIL_EXPECT_EQ(err_stats.get_inet6_rx_bad_nexthop_count(), 0U);
    // Nexthop is formatted incorrectly.
    test::NextHop nexthop_a("192.168.11");
    agent_a_->AddInet6Route("blue", "2001:db8:85a3::8a2e:370:aaaa/128",
                            nexthop_a);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(err_stats.get_inet6_rx_bad_nexthop_count(), 1U);
}

TEST_F(BgpXmppInet6ErrorTest, BadRouteAfiSafi) {
    BgpXmppChannel *channel = bgp_channel_manager_->FindChannel("agent-a");
    const BgpXmppChannel::ErrorStats &err_stats = channel->error_stats();
    EXPECT_TRUE(channel != NULL);
    TASK_UTIL_EXPECT_EQ(err_stats.get_inet6_rx_bad_afi_safi_count(), 0U);
    // Create a route with incorrect afi.
    agent_a_->AddBogusInet6Route("blue", "2001:db8:85a3::8a2e:370:aaaa/128",
                                 "192.168.1.1", test::ROUTE_AF_ERROR);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(err_stats.get_inet6_rx_bad_afi_safi_count(), 1U);

    // Create a route with incorrect safi.
    agent_a_->AddBogusInet6Route("blue", "2001:db8:85a3::8a2e::370:aaaa/128",
                                 "192.168.1.1", test::ROUTE_SAFI_ERROR);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(err_stats.get_inet6_rx_bad_afi_safi_count(), 2U);
}

TEST_F(BgpXmppInet6ErrorTest, BadXmlToken) {
    BgpXmppChannel *channel = bgp_channel_manager_->FindChannel("agent-a");
    const BgpXmppChannel::ErrorStats &err_stats = channel->error_stats();
    EXPECT_TRUE(channel != NULL);
    TASK_UTIL_EXPECT_EQ(err_stats.get_inet6_rx_bad_xml_token_count(), 0U);
    // Create a route that sends an incorrect xml message to the controller.
    agent_a_->AddBogusInet6Route("blue", "2001:db8:85a3::8a2e:370:aaaa/128",
                                 "192.168.1.1", test::XML_TOKEN_ERROR);
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(err_stats.get_inet6_rx_bad_xml_token_count(), 1U);
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
