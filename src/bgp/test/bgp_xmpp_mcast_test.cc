/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>

#include "base/test/task_test_util.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/inetmcast/inetmcast_route.h"
#include "bgp/inetmcast/inetmcast_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "io/test/event_manager_test.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "sandesh/sandesh.h"
#include "schema/xmpp_multicast_types.h"
#include "testing/gunit.h"
#include "xmpp/xmpp_server.h"

using namespace autogen;
using namespace boost::asio;
using namespace boost::assign;
using namespace std;
using namespace test;

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
    BgpXmppChannelMock *channel_[3];
};


class BgpXmppMcastTest : public ::testing::Test {
protected:
    static const char *config_tmpl;

    static void ValidateShowRouteResponse(Sandesh *sandesh,
        vector<size_t> &result) {
        ShowRouteResp *resp = dynamic_cast<ShowRouteResp *>(sandesh);
        EXPECT_NE((ShowRouteResp *)NULL, resp);

        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_tables().size());

        cout << "*******************************************************"<<endl;
        for (size_t i = 0; i < resp->get_tables().size(); i++) {
            TASK_UTIL_EXPECT_EQ(result[i], resp->get_tables()[i].routes.size());
            cout << resp->get_tables()[i].routing_instance << " "
                 << resp->get_tables()[i].routing_table_name << endl;
            for (size_t j = 0; j < resp->get_tables()[i].routes.size(); j++) {
                cout << resp->get_tables()[i].routes[j].prefix << " "
                        << resp->get_tables()[i].routes[j].paths.size() << endl;
            }
        }
        cout << "*******************************************************"<<endl;
        validate_done_ = 1;
    }

    BgpXmppMcastTest() : thread_(&evm_) { }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        xs_x_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);

        bs_x_->session_manager()->Initialize(0);
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
        if (agent_c_) { agent_c_->Delete(); }
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void Configure() {
        char config[4096];
        snprintf(config, sizeof(config), config_tmpl,
                 bs_x_->session_manager()->GetPort());
        bs_x_->Configure(config);
    }

    bool CheckOListElem(const test::NetworkAgentMock *agent,
            const string &net, const string &prefix, size_t olist_size,
            const string &address, const string &encap) {
        const NetworkAgentMock::McastRouteEntry *rt =
                agent->McastRouteLookup(net, prefix);
        if (olist_size == 0 && rt != NULL)
            return false;
        if (olist_size == 0)
            return true;
        if (rt == NULL)
            return false;

        const autogen::OlistType &olist = rt->entry.olist;
        if (olist.next_hop.size() != olist_size)
            return false;

        vector<string> tunnel_encapsulation;
        if (encap == "all") {
            tunnel_encapsulation.push_back("gre");
            tunnel_encapsulation.push_back("udp");
        } else if (!encap.empty()) {
            tunnel_encapsulation.push_back(encap);
        }
        sort(tunnel_encapsulation.begin(), tunnel_encapsulation.end());

        for (autogen::OlistType::const_iterator it = olist.begin();
             it != olist.end(); ++it) {
            if (it->address == address) {
                return (it->tunnel_encapsulation_list.tunnel_encapsulation ==
                        tunnel_encapsulation);
            }
        }

        return false;
    }

    void VerifyOListElem(const test::NetworkAgentMock *agent,
            const string &net, const string &prefix, size_t olist_size,
            const string &address, const string &encap = "") {
        TASK_UTIL_EXPECT_TRUE(
                CheckOListElem(agent, net, prefix, olist_size, address, encap));
    }

    EventManager evm_;
    ServerThread thread_;
    boost::scoped_ptr<BgpServerTest> bs_x_;
    XmppServer *xs_x_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_b_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_c_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> bgp_channel_manager_;

    static int validate_done_;
};

int BgpXmppMcastTest::validate_done_;

const char *BgpXmppMcastTest::config_tmpl = "\
<config>\
    <bgp-router name=\'A\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
    </bgp-router>\
    <routing-instance name='blue'>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
    <routing-instance name='pink'>\
        <vrf-target>target:1:2</vrf-target>\
    </routing-instance>\
</config>\
";

class BgpXmppMcastErrorTest : public BgpXmppMcastTest {
protected:
    virtual void SetUp() {
        BgpXmppMcastTest::SetUp();

        Configure();
        task_util::WaitForIdle();

        // Create agent a and register to multicast table.
        agent_a_.reset(new test::NetworkAgentMock(
                &evm_, "agent-a", xs_x_->GetPort(), "127.0.0.1"));
        TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
        agent_a_->McastSubscribe("blue", 1);
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        // Trigger TCP close on server and wait for channel to be deleted.
        agent_a_->SessionDown();
        task_util::WaitForIdle();

        BgpXmppMcastTest::TearDown();
    }
};

TEST_F(BgpXmppMcastErrorTest, BadGroupAddress) {
    agent_a_->AddMcastRoute("blue", "225.0.0,10.1.1.1", "7.7.7.7", "1000-2000");
    task_util::WaitForIdle();
    InetMcastTable *blue_table_ = static_cast<InetMcastTable *>(
        bs_x_->database()->FindTable("blue.inetmcast.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

TEST_F(BgpXmppMcastErrorTest, BadSourceAddress) {
    agent_a_->AddMcastRoute("blue", "225.0.0.1,10.1.1", "7.7.7.7", "1000-2000");
    task_util::WaitForIdle();
    InetMcastTable *blue_table_ = static_cast<InetMcastTable *>(
        bs_x_->database()->FindTable("blue.inetmcast.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

TEST_F(BgpXmppMcastErrorTest, BadNexthopAddress) {
    agent_a_->AddMcastRoute("blue", "225.0.0.1,10.1.1.1", "7.7", "1000-2000");
    task_util::WaitForIdle();
    InetMcastTable *blue_table_ = static_cast<InetMcastTable *>(
        bs_x_->database()->FindTable("blue.inetmcast.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

TEST_F(BgpXmppMcastErrorTest, BadLabelBlock1) {
    agent_a_->AddMcastRoute("blue", "225.0.0.1,10.1.1.1", "7.7.7.7", "100,200");
    task_util::WaitForIdle();
    InetMcastTable *blue_table_ = static_cast<InetMcastTable *>(
        bs_x_->database()->FindTable("blue.inetmcast.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

TEST_F(BgpXmppMcastErrorTest, BadLabelBlock2) {
    agent_a_->AddMcastRoute("blue", "225.0.0.1,10.1.1.1", "7.7.7.7", "1-2-3");
    task_util::WaitForIdle();
    InetMcastTable *blue_table_ = static_cast<InetMcastTable *>(
        bs_x_->database()->FindTable("blue.inetmcast.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

class BgpXmppMcastSubscriptionTest : public BgpXmppMcastTest {
protected:
    virtual void SetUp() {
        BgpXmppMcastTest::SetUp();

        Configure();
        task_util::WaitForIdle();

        // Create agents and wait for the sessions to be Established.
        // Do not register agents to the multicast table.
        agent_a_.reset(new test::NetworkAgentMock(
                &evm_, "agent-a", xs_x_->GetPort(), "127.0.0.1"));
        TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

        agent_b_.reset(new test::NetworkAgentMock(
                &evm_, "agent-b", xs_x_->GetPort(), "127.0.0.2"));
        TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
    }

    virtual void TearDown() {
        // Trigger TCP close on server and wait for channel to be deleted.
        agent_a_->SessionDown();
        agent_b_->SessionDown();
        task_util::WaitForIdle();

        BgpXmppMcastTest::TearDown();
    }
};

TEST_F(BgpXmppMcastSubscriptionTest, PendingSubscribe) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Register agent b to the multicast table and add a mcast route
    // after waiting for the subscription to be processed.
    agent_b_->McastSubscribe("blue", 1);
    task_util::WaitForIdle();
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");

    // Register agent a to the multicast table and add a mcast route
    // without waiting for the subscription to be processed.
    agent_a_->McastSubscribe("blue", 1);
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 1, "8.8.8.8");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
}

TEST_F(BgpXmppMcastSubscriptionTest, PendingUnsubscribe) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Register agent b to the multicast table and add a mcast route
    // after waiting for the subscription to be processed.
    agent_b_->McastSubscribe("blue", 1);
    task_util::WaitForIdle();
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");

    // Register agent a to the multicast table and add a mcast route
    // without waiting for the subscription to be processed. Then go
    // ahead and unsubscribe right away.
    agent_a_->McastSubscribe("blue", 1);
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_a_->McastUnsubscribe("blue");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->McastRouteCount());

    // Delete mcast route for agent b.
    agent_b_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
}

TEST_F(BgpXmppMcastSubscriptionTest, SubsequentSubscribeUnsubscribe) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Register agent b to the multicast table and add a mcast route
    // after waiting for the subscription to be processed.
    agent_b_->McastSubscribe("blue", 1);
    task_util::WaitForIdle();
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");

    // Register agent a to the multicast table and add a mcast route
    // without waiting for the subscription to be processed. Then go
    // ahead and unsubscribe right away. Then subscribe again with a
    // different id and add the route again.
    agent_a_->McastSubscribe("blue", 1);
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_a_->McastUnsubscribe("blue");
    agent_a_->McastSubscribe("blue", 2);
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 1, "8.8.8.8");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7");

    // Verify that agent a mcast route was added with instance_id = 2.
    InetMcastTable *blue_table_ = static_cast<InetMcastTable *>(
            bs_x_->database()->FindTable("blue.inetmcast.0"));
    const char *route = "127.0.0.1:2:225.0.0.1,0.0.0.0";
    InetMcastPrefix prefix(InetMcastPrefix::FromString(route));
    InetMcastTable::RequestKey key(prefix, NULL);
    TASK_UTIL_EXPECT_TRUE(
        dynamic_cast<InetMcastRoute *>(blue_table_->Find(&key)) != NULL);

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

class BgpXmppMcastMultiAgentTest : public BgpXmppMcastTest {
protected:
    virtual void SetUp() {
        BgpXmppMcastTest::SetUp();

        Configure();
        task_util::WaitForIdle();

        // Create agents and register to multicast table.
        agent_a_.reset(new test::NetworkAgentMock(
                &evm_, "agent-a", xs_x_->GetPort(), "127.0.0.1"));
        TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
        agent_a_->McastSubscribe("blue", 1);

        agent_b_.reset(new test::NetworkAgentMock(
                &evm_, "agent-b", xs_x_->GetPort(), "127.0.0.2"));
        TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());
        agent_b_->McastSubscribe("blue", 1);

        agent_c_.reset(new test::NetworkAgentMock(
                &evm_, "agent-c", xs_x_->GetPort(), "127.0.0.3"));
        TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());
        agent_c_->McastSubscribe("blue", 1);
    }

    virtual void TearDown() {
        // Trigger TCP close on server and wait for channel to be deleted.
        agent_a_->SessionDown();
        agent_b_->SessionDown();
        agent_c_->SessionDown();
        task_util::WaitForIdle();

        BgpXmppMcastTest::TearDown();
    }
};

TEST_F(BgpXmppMcastMultiAgentTest, SourceAndGroup) {
    const char *mroute = "225.0.0.1,10.1.1.1";

    // Add mcast route for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_c_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, GroupOnly) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_c_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, MultipleRoutes) {
    const char *mroute1 = "225.0.0.1,10.1.1.1";
    const char *mroute2 = "225.0.0.1,10.1.1.2";

    // Add mcast routes for all agents.
    agent_a_->AddMcastRoute("blue", mroute1, "7.7.7.7", "10000-20000");
    agent_b_->AddMcastRoute("blue", mroute1, "8.8.8.8", "40000-60000");
    agent_c_->AddMcastRoute("blue", mroute1, "9.9.9.9", "60000-80000");
    agent_a_->AddMcastRoute("blue", mroute2, "7.7.7.7", "10000-20000");
    agent_b_->AddMcastRoute("blue", mroute2, "8.8.8.8", "40000-60000");
    agent_c_->AddMcastRoute("blue", mroute2, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify that all agents have both routes.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_c_->McastRouteCount());

    // Verify all OList elements for the route 1 on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute1, 2, "8.8.8.8");
    VerifyOListElem(agent_a_.get(), "blue", mroute1, 2, "9.9.9.9");
    VerifyOListElem(agent_b_.get(), "blue", mroute1, 1, "7.7.7.7");
    VerifyOListElem(agent_c_.get(), "blue", mroute1, 1, "7.7.7.7");

    // Verify all OList elements for the route 2 on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute2, 2, "8.8.8.8");
    VerifyOListElem(agent_a_.get(), "blue", mroute2, 2, "9.9.9.9");
    VerifyOListElem(agent_b_.get(), "blue", mroute2, 1, "7.7.7.7");
    VerifyOListElem(agent_c_.get(), "blue", mroute2, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute1);
    agent_b_->DeleteMcastRoute("blue", mroute1);
    agent_c_->DeleteMcastRoute("blue", mroute1);
    agent_a_->DeleteMcastRoute("blue", mroute2);
    agent_b_->DeleteMcastRoute("blue", mroute2);
    agent_c_->DeleteMcastRoute("blue", mroute2);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, Join) {
    const char *mroute = "225.0.0.1,10.1.1.1";

    // Add mcast route for agents a and b.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_c_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 1, "8.8.8.8");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 0, "0.0.0.0");

    // Add mcast route for agent c.
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_c_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, Leave) {
    const char *mroute = "225.0.0.1,10.1.1.1";

    // Add mcast route for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_c_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7");

    // Delete mcast route for agent c.
    agent_c_->DeleteMcastRoute("blue", mroute);

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_c_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 1, "8.8.8.8");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 0, "0.0.0.0");

    // Delete mcast route for agents a and b.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, Introspect) {
    const char *mroute = "225.0.0.1,10.1.1.1";

    // Add mcast route for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify that all agents have the route.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_c_->McastRouteCount());

    // Verify routes via sandesh.
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = bs_x_.get();
    sandesh_context.xmpp_peer_manager = bgp_channel_manager_.get();
    Sandesh::set_client_context(&sandesh_context);

    // First get all tables.
    std::vector<size_t> result = list_of(3)(1);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    ShowRouteReq *show_req = new ShowRouteReq;
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);

    // Now get blue.inetmcast.0.
    result = list_of(3);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.inetmcast.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify that no agents have the route.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_b_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_c_->McastRouteCount());

    // Get blue.inetmcast.0 again.
    result.resize(0);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.inetmcast.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);
};

TEST_F(BgpXmppMcastMultiAgentTest, ChangeNexthop) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_c_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7");

    // Update mcast route for agent_a - change nexthop to 1.1.1.1.
    agent_a_->AddMcastRoute("blue", mroute, "1.1.1.1", "10000-20000");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_b_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_c_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "1.1.1.1");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "1.1.1.1");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, MultipleNetworks) {
    const char *mroute = "225.0.0.1,10.1.1.1";

    // Subscribe to another network.
    agent_a_->McastSubscribe("pink", 2);
    agent_b_->McastSubscribe("pink", 2);
    agent_c_->McastSubscribe("pink", 2);
    task_util::WaitForIdle();

    // Add mcast routes in blue and pink for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    agent_a_->AddMcastRoute("pink", mroute, "7.7.7.7", "10000-20000");
    agent_b_->AddMcastRoute("pink", mroute, "8.8.8.8", "40000-60000");
    agent_c_->AddMcastRoute("pink", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify that all agents have both routes.
    TASK_UTIL_EXPECT_EQ(2, agent_a_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_b_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_c_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->McastRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->McastRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_c_->McastRouteCount("blue"));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->McastRouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(1, agent_b_->McastRouteCount("pink"));
    TASK_UTIL_EXPECT_EQ(1, agent_c_->McastRouteCount("pink"));

    // Verify all OList elements for the route in blue on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7");

    // Verify all OList elements for the route in pink on all agents.
    VerifyOListElem(agent_a_.get(), "pink", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_a_.get(), "pink", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_b_.get(), "pink", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_c_.get(), "pink", mroute, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    agent_a_->DeleteMcastRoute("pink", mroute);
    agent_b_->DeleteMcastRoute("pink", mroute);
    agent_c_->DeleteMcastRoute("pink", mroute);
    task_util::WaitForIdle();
};

class BgpXmppMcastEncapTest : public BgpXmppMcastMultiAgentTest {
protected:
};

TEST_F(BgpXmppMcastEncapTest, ImplicitOnly) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ExplicitSingle) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "gre");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "gre");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000", "gre");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8", "gre");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9", "gre");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7", "gre");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7", "gre");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ExplicitAll) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "all");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "all");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000", "all");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8", "all");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9", "all");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7", "all");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7", "all");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ExplicitMixed1) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "all");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "gre");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000", "udp");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8", "gre");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9", "udp");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7", "all");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7", "all");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ExplicitMixed2) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "gre");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "all");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000", "all");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8", "all");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9", "all");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7", "gre");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7", "gre");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ImplicitAndExplicitMixed1) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "all");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7", "all");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7", "all");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ImplicitAndExplicitMixed2) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "gre");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000", "all");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8", "gre");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9", "all");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ImplicitAndExplicitMixed3) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "all");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "gre");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8", "gre");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7", "all");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7", "all");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, Change) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7");

    // Update mcast route for all agents - change encaps to all.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "all");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "all");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000", "all");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8", "all");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9", "all");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7", "all");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7", "all");

    // Update mcast route for all agents - change encaps to gre.
    agent_a_->AddMcastRoute("blue", mroute, "7.7.7.7", "10000-20000", "gre");
    agent_b_->AddMcastRoute("blue", mroute, "8.8.8.8", "40000-60000", "gre");
    agent_c_->AddMcastRoute("blue", mroute, "9.9.9.9", "60000-80000", "gre");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "8.8.8.8", "gre");
    VerifyOListElem(agent_a_.get(), "blue", mroute, 2, "9.9.9.9", "gre");
    VerifyOListElem(agent_b_.get(), "blue", mroute, 1, "7.7.7.7", "gre");
    VerifyOListElem(agent_c_.get(), "blue", mroute, 1, "7.7.7.7", "gre");

    // Delete mcast route for all agents.
    agent_a_->DeleteMcastRoute("blue", mroute);
    agent_b_->DeleteMcastRoute("blue", mroute);
    agent_c_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

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
