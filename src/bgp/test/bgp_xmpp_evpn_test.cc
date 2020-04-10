/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>

#include <sstream>

#include "bgp/bgp_factory.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/evpn/evpn_route.h"
#include "bgp/evpn/evpn_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"
#include "schema/xmpp_enet_types.h"

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

static const char *config_template_10 = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
    </bgp-router>\
</config>\
";

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
    BgpXmppEvpnTest1() : thread_(&evm_), xs_x_(NULL) { }

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

    void ConfigureWithoutRoutingInstances() {
        bs_x_->Configure(config_template_10);
    }

    EvpnTable *GetBgpTable(BgpServerTestPtr bs,
        const std::string &instance_name) {
        string table_name = instance_name + ".evpn.0";
        return static_cast<EvpnTable *>(
            bs->database()->FindTable(table_name));
    }

    EvpnRoute *BgpRouteLookup(BgpServerTestPtr bs,
        const string &instance_name, const string &prefix) {
        EvpnTable *table = GetBgpTable(bs, instance_name);
        EXPECT_TRUE(table != NULL);
        if (table == NULL)
            return NULL;
        boost::system::error_code error;
        EvpnPrefix nlri = EvpnPrefix::FromString(prefix, &error);
        EXPECT_FALSE(error);
        EvpnTable::RequestKey key(nlri, NULL);
        DBEntry *db_entry = table->Find(&key);
        if (db_entry == NULL)
            return NULL;
        return dynamic_cast<EvpnRoute *>(db_entry);
    }

    bool CheckBgpRouteExists(BgpServerTestPtr bs, const string &instance,
        const string &prefix) {
        task_util::TaskSchedulerLock lock;
        EvpnRoute *rt = BgpRouteLookup(bs, instance, prefix);
        return (rt && rt->BestPath() != NULL);
    }

    EvpnRoute *VerifyBgpRouteExists(BgpServerTestPtr bs, const string &instance,
        const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(CheckBgpRouteExists(bs, instance, prefix));
        return BgpRouteLookup(bs, instance, prefix);
    }

    bool CheckBgpRouteNoExists(BgpServerTestPtr bs, const string &instance,
        const string &prefix) {
        task_util::TaskSchedulerLock lock;
        EvpnRoute *rt = BgpRouteLookup(bs, instance, prefix);
        return !rt;
    }

    void VerifyBgpRouteNoExists(BgpServerTestPtr bs, const string &instance,
        const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(CheckBgpRouteNoExists(bs, instance, prefix));
    }
    void AgentRouteAdd(const string &enet_prefix, int count = 1);

    EventManager evm_;
    ServerThread thread_;
    BgpServerTestPtr bs_x_;
    XmppServer *xs_x_;
    test::NetworkAgentMockPtr agent_a_;
    test::NetworkAgentMockPtr agent_b_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> bgp_channel_manager_;
};

void BgpXmppEvpnTest1::AgentRouteAdd(const string &enet_prefix, int count) {
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
    eroute_a << enet_prefix;
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route showed up on agent.
    TASK_UTIL_EXPECT_EQ(count, agent_a_->EnetRouteCount());
    if (count) {
        const autogen::EnetItemType *rt =
            agent_a_->EnetRouteLookup("blue", enet_prefix);
        TASK_UTIL_EXPECT_TRUE(rt != NULL);
        int label = rt->entry.next_hops.next_hop[0].label;
        string nh = rt->entry.next_hops.next_hop[0].address;
        TASK_UTIL_EXPECT_NE(0xFFFFF, label);
        TASK_UTIL_EXPECT_EQ("192.168.1.1", nh);
        TASK_UTIL_EXPECT_EQ("blue", rt->entry.virtual_network);
    }

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
// Add one type-5 route and verify the next hop and label.
//
TEST_F(BgpXmppEvpnTest1, 1AgentRouteAdd) {
    //AgentRouteAdd("aa:00:00:00:00:01,10.1.1.1,232.1.1.1");
    AgentRouteAdd("00:00:00:00:00:00,10.1.1.1/32");
}

//
// Single agent.
// Add one type-5 inet host route and verify the next hop and label.
//
TEST_F(BgpXmppEvpnTest1, 1AgentType5InetHostRouteAdd) {
    AgentRouteAdd("00:00:00:00:00:00,10.1.1.1/32");
}

//
// Single agent.
// Add one type-5 inet prefix route and verify the next hop and label.
//
TEST_F(BgpXmppEvpnTest1, 1AgentType5InetPrefixAdd) {
    AgentRouteAdd("00:00:00:00:00:00,10.1.1.0/24");
}

//
// Single agent.
// Add one inet default route and verify the next hop and label.
//
TEST_F(BgpXmppEvpnTest1, 1AgentType5InetDefaultRouteAdd) {
    AgentRouteAdd("00:00:00:00:00:00,0.0.0.0/0");
}

//
// Single agent.
// Add one invalid inet route and verify the next hop and label.
//
TEST_F(BgpXmppEvpnTest1, 1AgentType5InetInvalidRouteAdd) {
    AgentRouteAdd("00:00:00:00:00:00,10.1.1.1/323", 0);
}

//
// Single agent.
// Add one inet6 type-5 host route and verify the next hop and label.
//
TEST_F(BgpXmppEvpnTest1, 1AgentType5Inet6HostRouteAdd) {
    AgentRouteAdd("00:00:00:00:00:00,dead::beef/128");
}

//
// Single agent.
// Add one inet6 type-5 prefix route and verify the next hop and label.
//
TEST_F(BgpXmppEvpnTest1, 1AgentType5Inet6PrefixRouteAdd) {
    AgentRouteAdd("00:00:00:00:00:00,dead:beef::/64");
}

//
// Single agent.
// Add one inet6 type5 default route and verify the next hop and label.
//
TEST_F(BgpXmppEvpnTest1, 1AgentType5Inet6DefaultRouteAdd) {
    AgentRouteAdd("00:00:00:00:00:00,::/0");
}

//
// Single agent.
// Add one invalid inet6 route and verify the next hop and label.
//
TEST_F(BgpXmppEvpnTest1, 1AgentType5Inet6InvalidRouteAdd) {
    AgentRouteAdd("00:00:00:00:00:00,dead:beef::/648", 0);
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
// Single agent.
// Add/Delete route with only the MAC.
// Address field is null.
//
TEST_F(BgpXmppEvpnTest1, 1AgentMacOnlyRouteAddDelete1) {
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
    eroute_a << "aa:00:00:00:00:01";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route showed up on agent.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    const autogen::EnetItemType *rt =
        agent_a_->EnetRouteLookup("blue", "aa:00:00:00:00:01,0.0.0.0/32");
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
// Single agent.
// Add/Delete route with only the MAC.
// Address field is 0.0.0.0/32.
//
TEST_F(BgpXmppEvpnTest1, 1AgentMacOnlyRouteAddDelete2) {
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
    eroute_a << "aa:00:00:00:00:01,0.0.0.0/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Verify that route showed up on agent.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    const autogen::EnetItemType *rt =
        agent_a_->EnetRouteLookup("blue", "aa:00:00:00:00:01,0.0.0.0/32");
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
    agent_b_->SessionDown();
}

//
// Routes from 2 agents are advertised to each other.
// Routing instance is created on the BGP servers afte the the 2 agents
// have already advertised routes.
//
TEST_F(BgpXmppEvpnTest1, CreateInstanceLater) {
    ConfigureWithoutRoutingInstances();
    task_util::WaitForIdle();

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

//
// Two agents.
// They are active/backup TOR Agents responsible for the same TOR.
// Hence they advertise the broadcast route with the same TOR address.
// Verify that they generate the same Inclusive Multicast route with
// different path ids.
//
TEST_F(BgpXmppEvpnTest1, 2TorAgentSameTor) {
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
    agent_a_->EnetSubscribe("blue", 99);
    agent_b_->EnetSubscribe("blue", 99);

    // Add broadcast routes from agents A and B.
    // The TOR IP is 10.1.1.1 and the TSN IP is 10.1.1.5.
    string eroute_tor("0-ff:ff:ff:ff:ff:ff,10.1.1.1/32");
    test::RouteParams tor_params;
    tor_params.replicator_address = "10.1.1.5";
    agent_a_->AddEnetRoute("blue", eroute_tor, "10.1.1.1", &tor_params);
    agent_b_->AddEnetRoute("blue", eroute_tor, "10.1.1.1", &tor_params);
    task_util::WaitForIdle();

    // Verify that a single inclusive multicast route got added with 2 paths.
    string eroute_im("3-10.1.1.1:99-0-10.1.1.1");
    EvpnRoute *rt = VerifyBgpRouteExists(bs_x_, "blue", eroute_im);
    TASK_UTIL_EXPECT_EQ(2U, rt->count());
    boost::system::error_code ec;
    uint32_t path_id_a =
        IpAddress::from_string("127.0.0.1", ec).to_v4().to_ulong();
    uint32_t path_id_b =
        IpAddress::from_string("127.0.0.2", ec).to_v4().to_ulong();
    TASK_UTIL_EXPECT_TRUE(rt->FindPath(BgpPath::Local, path_id_a) != NULL);
    TASK_UTIL_EXPECT_TRUE(rt->FindPath(BgpPath::Local, path_id_b) != NULL);

    // Delete broadcast routes from agents A and B.
    agent_a_->DeleteEnetRoute("blue", eroute_tor);
    agent_b_->DeleteEnetRoute("blue", eroute_tor);
    task_util::WaitForIdle();

    // Verify that the inclusive multicast route got deleted.
    VerifyBgpRouteNoExists(bs_x_, "blue", eroute_im);

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
    TASK_UTIL_EXPECT_EQ(1U, blue->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue->GetImportList().size());
    RoutingInstance *pink = mgr->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink->GetImportList().size());

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
    BgpXmppEvpnTest2() : thread_(&evm_), xs_x_(NULL), xs_y_(NULL) { }

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

    bool CheckRouteExists(test::NetworkAgentMockPtr agent,
        const string &network, const string &prefix) {
        task_util::TaskSchedulerLock lock;
        const autogen::EnetItemType *rt =
            agent->EnetRouteLookup(network, prefix);
        if (!rt)
            return false;
        return true;
    }

    bool CheckRouteMacLabels(test::NetworkAgentMockPtr agent,
        const string &network, const string &prefix, const string &mac,
        int label, int l3_label) {
        task_util::TaskSchedulerLock lock;
        const autogen::EnetItemType *rt =
            agent->EnetRouteLookup(network, prefix);
        if (!rt)
            return false;
        if (!mac.empty() && rt->entry.next_hops.next_hop[0].mac != mac)
            return false;
        if (label && rt->entry.next_hops.next_hop[0].label != label)
            return false;
        if (l3_label && rt->entry.next_hops.next_hop[0].l3_label != l3_label)
            return false;
        return true;
    }

    bool CheckRouteSecurityGroup(test::NetworkAgentMockPtr agent,
        const string &network, const string &prefix, vector<int> &sg) {
        task_util::TaskSchedulerLock lock;
        const autogen::EnetItemType *rt =
            agent->EnetRouteLookup(network, prefix);
        if (!rt)
            return false;
        if (rt->entry.security_group_list.security_group != sg)
            return false;
        return true;
    }

    bool CheckTagList(autogen::EnetTagListType &rt_tag_list,
                      const vector<int> &tag_list) {
        if (rt_tag_list.tag.size() != tag_list.size())
            return false;
        for (size_t idx = 0; idx < tag_list.size(); ++idx) {
            if (rt_tag_list.tag[idx] != tag_list[idx]) {
                return false;
            }
        }
        return true;
    }

    bool CheckRouteTagList(test::NetworkAgentMockPtr agent,
        const string &network, const string &prefix, vector<int> &tag_list) {
        task_util::TaskSchedulerLock lock;
        const autogen::EnetItemType *rt =
            agent->EnetRouteLookup(network, prefix);
        if (!rt)
            return false;
        autogen::EnetTagListType rt_tag =
            rt->entry.next_hops.next_hop[0].tag_list;
        if (!tag_list.empty() && !CheckTagList(rt_tag, tag_list))
            return false;
        return true;
    }

    bool CheckRouteLocalPrefMobilityETreeLeaf(test::NetworkAgentMockPtr agent,
        const string &network, const string &prefix,
        int local_pref, int sequence, bool sticky, bool leaf) {
        task_util::TaskSchedulerLock lock;
        const autogen::EnetItemType *rt =
            agent->EnetRouteLookup(network, prefix);
        if (!rt)
            return false;
        if (rt->entry.local_preference != local_pref)
            return false;
        if (rt->entry.mobility.seqno != sequence)
            return false;
        if (rt->entry.mobility.sticky != sticky)
            return false;
        if (rt->entry.etree_leaf != leaf)
            return false;
        return true;
    }

    void VerifyRouteExists(test::NetworkAgentMockPtr agent,
        const string &network, const string &prefix) {
        TASK_UTIL_EXPECT_TRUE(CheckRouteExists(agent, network, prefix));
    }

    void VerifyRouteMacLabels(test::NetworkAgentMockPtr agent,
        const string &network, const string &prefix, const string &mac,
        int label, int l3_label) {
        TASK_UTIL_EXPECT_TRUE(
            CheckRouteMacLabels(agent, network, prefix, mac, label, l3_label));
    }

    void VerifyRouteSecurityGroup(test::NetworkAgentMockPtr agent,
        const string &network, const string &prefix, vector<int> &sg) {
        TASK_UTIL_EXPECT_TRUE(
            CheckRouteSecurityGroup(agent, network, prefix, sg));
    }

    void VerifyRouteLocalPrefSequence(test::NetworkAgentMockPtr agent,
        const string &network, const string &prefix,
        int local_pref, uint32_t sequence) {
        TASK_UTIL_EXPECT_TRUE(CheckRouteLocalPrefMobilityETreeLeaf(
            agent, network, prefix, local_pref, sequence,
            test::RouteAttributes::kDefaultSticky,
            test::RouteAttributes::kDefaultETreeLeaf));
    }

    void VerifyRouteLocalPrefMobilityETreeLeaf(test::NetworkAgentMockPtr agent,
        const string &network, const string &prefix,
        int local_pref, uint32_t sequence, bool sticky, bool etree_leaf) {
        TASK_UTIL_EXPECT_TRUE(CheckRouteLocalPrefMobilityETreeLeaf(
            agent, network, prefix, local_pref, sequence, sticky, etree_leaf));
    }


    void VerifyRouteTagList(test::NetworkAgentMockPtr agent,
        const string &network, const string &prefix, vector<int> &tag_list) {
        TASK_UTIL_EXPECT_TRUE(
            CheckRouteTagList(agent, network, prefix, tag_list));
    }
    void BgpConnectLaterCommon(const string &enet_prefix_a,
                               const string &enet_prefix_b);


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
// Routes from 2 agents are advertised to each other.
//
TEST_F(BgpXmppEvpnTest2, RouteAddWithSecurityGroup) {
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
    vector<int> sg1 = {100, 101, 102};
    test::NextHop nexthop1("192.168.1.1");
    test::RouteAttributes attr1(sg1);
    agent_a_->AddEnetRoute("blue", eroute_a.str(), nexthop1, attr1);
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    vector<int> sg2 = {200, 201, 202};
    test::NextHop nexthop2("192.168.1.2");
    test::RouteAttributes attr2(sg2);
    agent_b_->AddEnetRoute("blue", eroute_b.str(), nexthop2, attr2);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Verify security groups on A.
    VerifyRouteSecurityGroup(agent_a_, "blue", eroute_a.str(), sg1);
    VerifyRouteSecurityGroup(agent_a_, "blue", eroute_b.str(), sg2);

    // Verify local pref and sequence on A.
    VerifyRouteLocalPrefSequence(agent_a_, "blue", eroute_a.str(),
        test::RouteAttributes::kDefaultLocalPref,
        test::RouteAttributes::kDefaultSequence);
    VerifyRouteLocalPrefSequence(agent_a_, "blue", eroute_b.str(),
        test::RouteAttributes::kDefaultLocalPref,
        test::RouteAttributes::kDefaultSequence);

    // Verify security groups on B.
    VerifyRouteSecurityGroup(agent_b_, "blue", eroute_a.str(), sg1);
    VerifyRouteSecurityGroup(agent_b_, "blue", eroute_b.str(), sg2);

    // Verify local pref and sequence on B.
    VerifyRouteLocalPrefSequence(agent_b_, "blue", eroute_a.str(),
        test::RouteAttributes::kDefaultLocalPref,
        test::RouteAttributes::kDefaultSequence);
    VerifyRouteLocalPrefSequence(agent_b_, "blue", eroute_b.str(),
        test::RouteAttributes::kDefaultLocalPref,
        test::RouteAttributes::kDefaultSequence);

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
//
TEST_F(BgpXmppEvpnTest2, RouteAddWithLocalPrefSequence) {
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
    test::NextHop nexthop1("192.168.1.1");
    test::RouteAttributes attr1(101, 1001);
    agent_a_->AddEnetRoute("blue", eroute_a.str(), nexthop1, attr1);
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    test::NextHop nexthop2("192.168.1.2");
    test::RouteAttributes attr2(202, 2002);
    agent_b_->AddEnetRoute("blue", eroute_b.str(), nexthop2, attr2);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Verify local pref and sequence on A.
    VerifyRouteLocalPrefSequence(agent_a_, "blue", eroute_a.str(), 101, 1001);
    VerifyRouteLocalPrefSequence(agent_a_, "blue", eroute_b.str(), 202, 2002);

    // Verify local pref and sequence on B.
    VerifyRouteLocalPrefSequence(agent_b_, "blue", eroute_a.str(), 101, 1001);
    VerifyRouteLocalPrefSequence(agent_b_, "blue", eroute_b.str(), 202, 2002);

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
// Routes with mobility info from 2 agents are advertised to each other.
//
TEST_F(BgpXmppEvpnTest2, RouteAddWithStickyBit) {
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

    // Add route from agent A with sticky bit and ETree Leaf mode
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    test::NextHop nexthop1("192.168.1.1");
    test::RouteAttributes attr1(100, 100, 99, true);
    agent_a_->AddEnetRoute("blue", eroute_a.str(), nexthop1, attr1);
    task_util::WaitForIdle();

    // Add route from agent B with non-sticky bit and ETree Root mode
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    test::NextHop nexthop2("192.168.1.2");
    test::RouteAttributes attr2(100, 100, 88, false);
    agent_b_->AddEnetRoute("blue", eroute_b.str(), nexthop2, attr2);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Verify local pref and sequence on A.
    VerifyRouteLocalPrefMobilityETreeLeaf(agent_a_, "blue", eroute_a.str(),
                     100, 99, true, test::RouteAttributes::kDefaultETreeLeaf);
    VerifyRouteLocalPrefMobilityETreeLeaf(agent_a_, "blue", eroute_b.str(),
                     100, 88, false, test::RouteAttributes::kDefaultETreeLeaf);

    // Verify local pref and sequence on B.
    VerifyRouteLocalPrefMobilityETreeLeaf(agent_b_, "blue", eroute_a.str(),
                  100, 99, true, test::RouteAttributes::kDefaultETreeLeaf);
    VerifyRouteLocalPrefMobilityETreeLeaf(agent_b_, "blue", eroute_b.str(),
                  100, 88, false, test::RouteAttributes::kDefaultETreeLeaf);

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
// Routes with mobility info and etree-leaf info from 2 agents are
// advertised to each other.
//
TEST_F(BgpXmppEvpnTest2, RouteAddWithETreeLeaf) {
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

    // Add route from agent A with sticky bit and ETree Leaf mode
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    test::NextHop nexthop1("192.168.1.1");
    test::RouteAttributes attr1(202, 202, 2002, true, true);
    agent_a_->AddEnetRoute("blue", eroute_a.str(), nexthop1, attr1);
    task_util::WaitForIdle();

    // Add route from agent B with non-sticky bit and ETree Root mode
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    test::NextHop nexthop2("192.168.1.2");
    test::RouteAttributes attr2(202, 202, 2002, false, false);
    agent_b_->AddEnetRoute("blue", eroute_b.str(), nexthop2, attr2);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Verify local pref and sequence on A.
    VerifyRouteLocalPrefMobilityETreeLeaf(agent_a_, "blue", eroute_a.str(), 202, 2002, true, true);
    VerifyRouteLocalPrefMobilityETreeLeaf(agent_a_, "blue", eroute_b.str(), 202, 2002, false, false);

    // Verify local pref and sequence on B.
    VerifyRouteLocalPrefMobilityETreeLeaf(agent_b_, "blue", eroute_a.str(), 202, 2002, true, true);
    VerifyRouteLocalPrefMobilityETreeLeaf(agent_b_, "blue", eroute_b.str(), 202, 2002, false, false);

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
// Routes with tag-list info from 2 agents are advertised to each other.
//
TEST_F(BgpXmppEvpnTest2, RouteAddWithTagList) {
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

    // Add route from agent A with TagList
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    vector<int> tag_list = list_of (1)(2);
    test::NextHop next_hop1("192.168.1.1", 0, tag_list);
    agent_a_->AddEnetRoute("blue", eroute_a.str(), next_hop1);
    task_util::WaitForIdle();

    // Add route from agent B with TagList
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    vector<int> tag_list_1 = list_of (3)(4);
    test::NextHop next_hop2("192.168.1.1", 0, tag_list_1);
    agent_b_->AddEnetRoute("blue", eroute_b.str(), next_hop2);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Verify the TagList
    VerifyRouteTagList(agent_a_, "blue", eroute_a.str(), tag_list);
    VerifyRouteTagList(agent_a_, "blue", eroute_b.str(), tag_list_1);

    // Verify the TagList
    VerifyRouteTagList(agent_b_, "blue", eroute_a.str(), tag_list);
    VerifyRouteTagList(agent_b_, "blue", eroute_b.str(), tag_list_1);

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
// Routes from 2 agents are advertised with router MAC and L3 label.
//
TEST_F(BgpXmppEvpnTest2, RouteAddRouterMacL3Label) {
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
    task_util::WaitForIdle();

    // Add route from agent A with mac mac_a, label 101 and l3-label 200.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    string mac_a("aa:aa:aa:00:00:01");
    test::NextHop nh_a("192.168.1.1", mac_a, 101, 200);
    agent_a_->AddEnetRoute("blue", eroute_a.str(), nh_a);
    task_util::WaitForIdle();

    // Add route from agent B with mac mac_b, label 102 and l3-label 200.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    string mac_b("bb:bb:bb:00:00:01");
    test::NextHop nh_b("192.168.2.1", mac_b, 102, 200);
    agent_b_->AddEnetRoute("blue", eroute_b.str(), nh_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Verify mac, label and l3-label values.
    VerifyRouteMacLabels(agent_a_, "blue", eroute_a.str(), mac_a, 101, 200);
    VerifyRouteMacLabels(agent_a_, "blue", eroute_b.str(), mac_b, 102, 200);
    VerifyRouteMacLabels(agent_b_, "blue", eroute_a.str(), mac_a, 101, 200);
    VerifyRouteMacLabels(agent_b_, "blue", eroute_b.str(), mac_b, 102, 200);

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
// Routes from 2 agents are updated when L3 label is updated.
//
TEST_F(BgpXmppEvpnTest2, RouteUpdateL3Label) {
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
    task_util::WaitForIdle();

    // Add route from agent A with mac mac_a, label 101 and l3-label 200.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    string mac_a("aa:aa:aa:00:00:01");
    test::NextHop nh_a("192.168.1.1", mac_a, 101, 200);
    agent_a_->AddEnetRoute("blue", eroute_a.str(), nh_a);
    task_util::WaitForIdle();

    // Add route from agent B with mac mac_b, label 102 and l3-label 200..
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    string mac_b("bb:bb:bb:00:00:01");
    test::NextHop nh_b("192.168.2.1", mac_b, 102, 200);
    agent_b_->AddEnetRoute("blue", eroute_b.str(), nh_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Verify mac, label and l3-label values.
    VerifyRouteMacLabels(agent_a_, "blue", eroute_a.str(), mac_a, 101, 200);
    VerifyRouteMacLabels(agent_a_, "blue", eroute_b.str(), mac_b, 102, 200);
    VerifyRouteMacLabels(agent_b_, "blue", eroute_a.str(), mac_a, 101, 200);
    VerifyRouteMacLabels(agent_b_, "blue", eroute_b.str(), mac_b, 102, 200);

    // Update l3-label of route from agent A to 300.
    nh_a = test::NextHop("192.168.1.1", mac_a, 101, 300);
    agent_a_->AddEnetRoute("blue", eroute_a.str(), nh_a);
    task_util::WaitForIdle();

    // Update l3-label of route from agent B to 300.
    nh_b = test::NextHop("192.168.2.1", mac_b, 102, 300);
    agent_b_->AddEnetRoute("blue", eroute_b.str(), nh_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Verify mac, label and l3-label values.
    VerifyRouteMacLabels(agent_a_, "blue", eroute_a.str(), mac_a, 101, 300);
    VerifyRouteMacLabels(agent_a_, "blue", eroute_b.str(), mac_b, 102, 300);
    VerifyRouteMacLabels(agent_b_, "blue", eroute_a.str(), mac_a, 101, 300);
    VerifyRouteMacLabels(agent_b_, "blue", eroute_b.str(), mac_b, 102, 300);

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
// Routes from 2 agents are updated when router MAC is updated.
//
TEST_F(BgpXmppEvpnTest2, RouteUpdateRouterMac) {
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
    task_util::WaitForIdle();

    // Add route from agent A with mac mac_a1, label 101 and l3-label 200.
    stringstream eroute_a;
    eroute_a << "aa:00:00:00:00:01,10.1.1.1/32";
    string mac_a1("aa:aa:aa:00:00:01");
    test::NextHop nh_a("192.168.1.1", mac_a1, 101, 200);
    agent_a_->AddEnetRoute("blue", eroute_a.str(), nh_a);
    task_util::WaitForIdle();

    // Add route from agent B with mac mac_b1, label 102 and l3-label 200.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,10.1.2.1/32";
    string mac_b1("bb:bb:bb:00:00:01");
    test::NextHop nh_b("192.168.2.1", mac_b1, 102, 200);
    agent_b_->AddEnetRoute("blue", eroute_b.str(), nh_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Verify mac, label and l3-label values.
    VerifyRouteMacLabels(agent_a_, "blue", eroute_a.str(), mac_a1, 101, 200);
    VerifyRouteMacLabels(agent_a_, "blue", eroute_b.str(), mac_b1, 102, 200);
    VerifyRouteMacLabels(agent_b_, "blue", eroute_a.str(), mac_a1, 101, 200);
    VerifyRouteMacLabels(agent_b_, "blue", eroute_b.str(), mac_b1, 102, 200);

    // Update mac of route from agent A to mac_a2.
    string mac_a2("aa:aa:aa:00:00:02");
    nh_a = test::NextHop("192.168.1.1", mac_a2, 101, 200);
    agent_a_->AddEnetRoute("blue", eroute_a.str(), nh_a);
    task_util::WaitForIdle();

    // Update mac of route from agent B to mac_b2.
    string mac_b2("bb:bb:bb:00:00:02");
    nh_b = test::NextHop("192.168.2.1", mac_b2, 102, 200);
    agent_b_->AddEnetRoute("blue", eroute_b.str(), nh_b);
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Verify mac, label and l3-label values.
    VerifyRouteMacLabels(agent_a_, "blue", eroute_a.str(), mac_a2, 101, 200);
    VerifyRouteMacLabels(agent_a_, "blue", eroute_b.str(), mac_b2, 102, 200);
    VerifyRouteMacLabels(agent_b_, "blue", eroute_a.str(), mac_a2, 101, 200);
    VerifyRouteMacLabels(agent_b_, "blue", eroute_b.str(), mac_b2, 102, 200);

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
// Add/Delete routes from 2 agents with only MAC.
// Address field is null.
//
TEST_F(BgpXmppEvpnTest2, MacOnlyRouteAddDelete1) {
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
    eroute_a << "aa:00:00:00:00:01";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01";
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
// Add/Delete routes from 2 agents with only MAC.
// Address field is 0.0.0.0/32.
//
TEST_F(BgpXmppEvpnTest2, MacOnlyRouteAddDelete2) {
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
    eroute_a << "aa:00:00:00:00:01,0.0.0.0/32";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,0.0.0.0/32";
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
// Add/Delete routes from 2 agents with only MAC.
// Address field is 0.0.0.0/0.
// This is not really a valid host IP address but we treat it the same as
// 0.0.0.0/32 for backward compatibility.
//
TEST_F(BgpXmppEvpnTest2, MacOnlyRouteAddDelete3) {
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
    eroute_a << "aa:00:00:00:00:01,0.0.0.0/0";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,0.0.0.0/0";
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
// Add/Delete routes from 2 agents with inet6 address.
//
TEST_F(BgpXmppEvpnTest2, Inet6RouteAddDelete) {
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
    eroute_a << "aa:00:00:00:00:01,aa00:db8:85a3::8a2e:370:fad1/128";
    agent_a_->AddEnetRoute("blue", eroute_a.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b;
    eroute_b << "bb:00:00:00:00:01,bb00:db8:85a3::8a2e:370:fad1/128";
    agent_b_->AddEnetRoute("blue", eroute_b.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->EnetRouteCount("blue"));

    // Verify routes on A.
    VerifyRouteExists(agent_a_, "blue", eroute_a.str());
    VerifyRouteExists(agent_a_, "blue", eroute_b.str());

    // Verify routes on B.
    VerifyRouteExists(agent_b_, "blue", eroute_a.str());
    VerifyRouteExists(agent_b_, "blue", eroute_b.str());

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
// Add/Delete routes from 2 agents with inet and inet6 address.
//
TEST_F(BgpXmppEvpnTest2, InetInet6RouteAddDelete) {
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
    stringstream eroute_a1, eroute_a2;
    eroute_a1 << "aa:00:00:00:00:01,10.1.1.1/32";
    agent_a_->AddEnetRoute("blue", eroute_a1.str(), "192.168.1.1");
    eroute_a2 << "aa:00:00:00:00:01,aa00:db8:85a3::8a2e:370:fad1/128";
    agent_a_->AddEnetRoute("blue", eroute_a2.str(), "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    stringstream eroute_b1, eroute_b2;
    eroute_b1 << "bb:00:00:00:00:01,10.1.1.2/32";
    agent_b_->AddEnetRoute("blue", eroute_b1.str(), "192.168.1.2");
    eroute_b2 << "bb:00:00:00:00:01,bb00:db8:85a3::8a2e:370:fad1/128";
    agent_b_->AddEnetRoute("blue", eroute_b2.str(), "192.168.1.2");
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(4, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(4, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(4, agent_b_->EnetRouteCount("blue"));

    // Verify routes on A.
    VerifyRouteExists(agent_a_, "blue", eroute_a1.str());
    VerifyRouteExists(agent_a_, "blue", eroute_a2.str());
    VerifyRouteExists(agent_a_, "blue", eroute_b1.str());
    VerifyRouteExists(agent_a_, "blue", eroute_b2.str());

    // Verify routes on B.
    VerifyRouteExists(agent_b_, "blue", eroute_a1.str());
    VerifyRouteExists(agent_b_, "blue", eroute_a2.str());
    VerifyRouteExists(agent_b_, "blue", eroute_b1.str());
    VerifyRouteExists(agent_b_, "blue", eroute_b2.str());

    // Delete routes from agent A.
    agent_a_->DeleteEnetRoute("blue", eroute_a1.str());
    agent_a_->DeleteEnetRoute("blue", eroute_a2.str());
    task_util::WaitForIdle();

    // Delete routes from agent B.
    agent_b_->DeleteEnetRoute("blue", eroute_b1.str());
    agent_b_->DeleteEnetRoute("blue", eroute_b2.str());
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
// Multiple MACs from 2 agents are advertised to each other.
// Each MAC address has 2 routes - 1 for inet and 1 for inet6.
//
TEST_F(BgpXmppEvpnTest2, InetInet6MultipleRoutes) {
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
        stringstream eroute_a1, eroute_a2;
        eroute_a1 << "aa:00:00:00:00:0" << idx << ",10.1.1." << idx << "/32";
        agent_a_->AddEnetRoute("blue", eroute_a1.str(), "192.168.1.1");
        eroute_a2 << "aa:00:00:00:00:0" << idx;
        eroute_a2 << ",aa00:db8:85a3::8a2e:370:fad" << idx << "/128";
        agent_a_->AddEnetRoute("blue", eroute_a2.str(), "192.168.1.1");
    }
    task_util::WaitForIdle();

    // Add route from agent B.
    for (int idx = 1; idx <= 4;  idx++) {
        stringstream eroute_b1, eroute_b2;
        eroute_b1 << "bb:00:00:00:00:0" << idx << ",10.1.2." << idx << "/32";
        agent_b_->AddEnetRoute("blue", eroute_b1.str(), "192.168.1.2");
        eroute_b2 << "bb:00:00:00:00:0" << idx;
        eroute_b2 << ",bb00:db8:85a3::8a2e:370:fad" << idx << "/128";
        agent_b_->AddEnetRoute("blue", eroute_b2.str(), "192.168.1.2");
    }
    task_util::WaitForIdle();

    // Verify that routes showed up on the agents.
    TASK_UTIL_EXPECT_EQ(16, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(16, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(16, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(16, agent_b_->EnetRouteCount("blue"));

    // Delete routes from agent A.
    for (int idx = 1; idx <= 4;  idx++) {
        stringstream eroute_a1, eroute_a2;
        eroute_a1 << "aa:00:00:00:00:0" << idx << ",10.1.1." << idx << "/32";
        agent_a_->DeleteEnetRoute("blue", eroute_a1.str());
        eroute_a2 << "aa:00:00:00:00:0" << idx;
        eroute_a2 << ",aa00:db8:85a3::8a2e:370:fad" << idx << "/128";
        agent_a_->DeleteEnetRoute("blue", eroute_a2.str());
    }
    task_util::WaitForIdle();

    // Verify deletion.
    TASK_UTIL_EXPECT_EQ(8, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(8, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(8, agent_b_->EnetRouteCount("blue"));

    // Delete route from agent B.
    for (int idx = 1; idx <= 4;  idx++) {
        stringstream eroute_b1, eroute_b2;
        eroute_b1 << "bb:00:00:00:00:0" << idx << ",10.1.2." << idx << "/32";
        agent_b_->DeleteEnetRoute("blue", eroute_b1.str());
        eroute_b2 << "bb:00:00:00:00:0" << idx;
        eroute_b2 << ",bb00:db8:85a3::8a2e:370:fad" << idx << "/128";
        agent_b_->DeleteEnetRoute("blue", eroute_b2.str());
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

void BgpXmppEvpnTest2::BgpConnectLaterCommon(const string &enet_prefix_a,
                                             const string &enet_prefix_b) {
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
    agent_a_->AddEnetRoute("blue", enet_prefix_a, "192.168.1.1");
    task_util::WaitForIdle();

    // Add route from agent B.
    agent_b_->AddEnetRoute("blue", enet_prefix_b, "192.168.1.2");
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
    agent_a_->DeleteEnetRoute("blue", enet_prefix_a);
    task_util::WaitForIdle();

    // Delete route from agent B.
    agent_b_->DeleteEnetRoute("blue", enet_prefix_b);
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
// BGP session comes up after the the 2 agents have already advertised
// routes to the 2 XMPP servers.
//
TEST_F(BgpXmppEvpnTest2, BgpConnectLater) {
    BgpConnectLaterCommon("aa:00:00:00:00:01,10.1.1.1/32",
                          "bb:00:00:00:00:01,10.1.2.1/32");
}

//
// Routes from 2 agents are advertised to each other.
// BGP session comes up after the the 2 agents have already advertised
// routes to the 2 XMPP servers.
//
TEST_F(BgpXmppEvpnTest2, BgpConnectLaterType5InetHostRoute) {
    BgpConnectLaterCommon("00:00:00:00:00:00,10.1.1.1/32",
                          "00:00:00:00:00:00,10.1.2.1/32");
}

//
// Routes from 2 agents are advertised to each other.
// BGP session comes up after the the 2 agents have already advertised
// routes to the 2 XMPP servers.
//
TEST_F(BgpXmppEvpnTest2, BgpConnectLaterType5InetPrefixRoute) {
    BgpConnectLaterCommon("00:00:00:00:00:00,10.1.1.0/28",
                          "00:00:00:00:00:00,10.1.2.0/28");
}

//
// Routes from 2 agents are advertised to each other.
// BGP session comes up after the the 2 agents have already advertised
// routes to the 2 XMPP servers.
//
TEST_F(BgpXmppEvpnTest2, BgpConnectLaterType5InetDefaultRoute) {
    BgpConnectLaterCommon("00:00:00:00:00:00,10.1.1.0/28",
                          "00:00:00:00:00:00,0.0.0.0/0");
}

//
// Routes from 2 agents are advertised to each other.
// BGP session comes up after the the 2 agents have already advertised
// routes to the 2 XMPP servers.
//
TEST_F(BgpXmppEvpnTest2, BgpConnectLaterType5Inet6HostRoute) {
    BgpConnectLaterCommon("00:00:00:00:00:00,dead::beef/128",
                          "00:00:00:00:00:00,beef::dead/128");
}

//
// Routes from 2 agents are advertised to each other.
// BGP session comes up after the the 2 agents have already advertised
// routes to the 2 XMPP servers.
//
TEST_F(BgpXmppEvpnTest2, BgpConnectLaterType5Inet6HostPrefix) {
    BgpConnectLaterCommon("00:00:00:00:00:00,deaf:beef::0/68",
                          "00:00:00:00:00:00,beef:dead::0/48");
}

//
// Routes from 2 agents are advertised to each other.
// BGP session comes up after the the 2 agents have already advertised
// routes to the 2 XMPP servers.
//
TEST_F(BgpXmppEvpnTest2, BgpConnectLaterType5Inet6DefaultRoute) {
    BgpConnectLaterCommon("00:00:00:00:00:00,deaf:beef::0/68",
                          "00:00:00:00:00:00,::/0");
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

    // Bring down the BGP session.
    bs_x_->DisableAllPeers();
    bs_y_->DisableAllPeers();
    task_util::WaitForIdle();

    // Verify that routes from remote agents are cleaned up.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->EnetRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->EnetRouteCount("blue"));

    // Bring up the BGP session.
    bs_x_->EnableAllPeers();
    bs_y_->EnableAllPeers();
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
    TASK_UTIL_EXPECT_EQ(1U, blue_x->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_x->GetImportList().size());
    RoutingInstance *pink_x = mgr_x->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_x != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_x->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_x->GetImportList().size());

    // Make sure that the config got applied properly on Y.
    RoutingInstanceMgr *mgr_y = bs_y_->routing_instance_mgr();
    RoutingInstance *blue_y = mgr_y->GetRoutingInstance("blue");
    TASK_UTIL_ASSERT_TRUE(blue_y != NULL);
    TASK_UTIL_EXPECT_EQ(1U, blue_y->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, blue_y->GetImportList().size());
    RoutingInstance *pink_y = mgr_y->GetRoutingInstance("pink");
    TASK_UTIL_ASSERT_TRUE(pink_y != NULL);
    TASK_UTIL_EXPECT_EQ(1U, pink_y->GetExportList().size());
    TASK_UTIL_EXPECT_EQ(4U, pink_y->GetImportList().size());

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
