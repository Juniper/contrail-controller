/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "bgp/bgp_factory.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_xmpp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "io/test/event_manager_test.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "schema/xmpp_multicast_types.h"
#include "xmpp/xmpp_factory.h"

using namespace autogen;
using namespace boost::asio;
using namespace boost::assign;
using namespace std;
using namespace test;

class BgpXmppMcastTest : public ::testing::Test {
protected:
    static void ValidateShowRouteResponse(Sandesh *sandesh,
        vector<size_t> &result) {
        ShowRouteResp *resp = dynamic_cast<ShowRouteResp *>(sandesh);
        EXPECT_NE((ShowRouteResp *)NULL, resp);

        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_tables().size());

        cout << "*******************************************************"<<endl;
        for (size_t i = 0; i < resp->get_tables().size(); i++) {
            cout << resp->get_tables()[i].routing_instance << " "
                 << resp->get_tables()[i].routing_table_name << endl;
            for (size_t j = 0; j < resp->get_tables()[i].routes.size(); j++) {
                cout << resp->get_tables()[i].routes[j].prefix << " "
                        << resp->get_tables()[i].routes[j].paths.size() << endl;
            }
            TASK_UTIL_EXPECT_EQ(result[i], resp->get_tables()[i].routes.size());
        }
        cout << "*******************************************************"<<endl;
        validate_done_ = 1;
    }

    static void ValidateShowManagerMulticastDetailResponse(Sandesh *sandesh,
        vector<string> &result) {
        ShowMulticastManagerDetailResp *resp =
            dynamic_cast<ShowMulticastManagerDetailResp *>(sandesh);
        EXPECT_NE((ShowMulticastManagerDetailResp *)NULL, resp);

        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_trees().size());
        cout << "*******************************************************"<<endl;
        for (size_t i = 0; i < resp->get_trees().size(); ++i) {
            cout << resp->get_trees()[i].log() << endl;
            bool found = false;
            BOOST_FOREACH(const string &group, result) {
                if (resp->get_trees()[i].get_group() == group) {
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found);
        }
        cout << "*******************************************************"<<endl;
        validate_done_ = 1;
    }

    BgpXmppMcastTest() : thread_(&evm_), xs_x_(NULL) { }

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
        agent_xa_->McastSubscribe(net, id);
        agent_xb_->McastSubscribe(net, id);
        agent_xc_->McastSubscribe(net, id);
        task_util::WaitForIdle();
    }

    virtual void Configure(const char *config_tmpl) {
        char config[8192];
        snprintf(config, sizeof(config), config_tmpl,
            bs_x_->session_manager()->GetPort());
        bs_x_->Configure(config);
    }

    virtual void Configure(BgpServerTestPtr server, const char *config_tmpl) {
        char config[8192];
        snprintf(config, sizeof(config), config_tmpl,
            server->session_manager()->GetPort());
        server->Configure(config);
        task_util::WaitForIdle();
    }

    int ExtractLabel(boost::shared_ptr<const test::NetworkAgentMock> agent,
        const string &net, const string &prefix) {
        task_util::TaskSchedulerLock lock;

        const NetworkAgentMock::McastRouteEntry *rt =
            agent->McastRouteLookup(net, prefix);
        return (rt ? rt->entry.nlri.source_label : 0);
    }

    int VerifyLabel(boost::shared_ptr<const test::NetworkAgentMock> agent,
        const string &net, const string &prefix,
        int first_label = 0, int last_label = 0) {
        TASK_UTIL_EXPECT_TRUE(ExtractLabel(agent, net, prefix) >= first_label);
        TASK_UTIL_EXPECT_TRUE(ExtractLabel(agent, net, prefix) <= last_label);
        return ExtractLabel(agent, net, prefix);
    }

    int GetLabel(const test::NetworkAgentMock *agent,
        const string &net, const string &prefix) {
        const NetworkAgentMock::McastRouteEntry *rt =
            agent->McastRouteLookup(net, prefix);
        return (rt ? rt->entry.nlri.source_label : 0);
    }

    bool CheckOListElem(boost::shared_ptr<const test::NetworkAgentMock> agent,
        const string &net, const string &prefix, size_t olist_size,
        const string &address, const string &encap,
        const test::NetworkAgentMock *other_agent) {
        task_util::TaskSchedulerLock lock;

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

        int label = other_agent ? GetLabel(other_agent, net, prefix) : 0;
        if (other_agent && label == 0)
            return false;

        vector<string> tunnel_encapsulation;
        if (encap == "all") {
            tunnel_encapsulation.push_back("gre");
            tunnel_encapsulation.push_back("udp");
        } else if (!encap.empty()) {
            tunnel_encapsulation.push_back(encap);
        }
        sort(tunnel_encapsulation.begin(), tunnel_encapsulation.end());

        string label_str = integerToString(label);
        for (autogen::OlistType::const_iterator it = olist.begin();
            it != olist.end(); ++it) {
            if (it->address == address) {
                if (it->tunnel_encapsulation_list.tunnel_encapsulation !=
                    tunnel_encapsulation)
                    return false;
                if (label != 0 && it->label != label_str)
                    return false;
                return true;
            }
        }

        return false;
    }

    void VerifyOListElem(boost::shared_ptr<const test::NetworkAgentMock> agent,
        const string &net, const string &prefix, size_t olist_size) {
        TASK_UTIL_EXPECT_TRUE(
            CheckOListElem(agent, net, prefix, olist_size, "", "", NULL));
    }

    void VerifyOListElem(boost::shared_ptr<const test::NetworkAgentMock> agent,
        const string &net, const string &prefix,
        size_t olist_size, const string &address,
        boost::shared_ptr<test::NetworkAgentMock> other_agent,
        const string &encap = "") {
        TASK_UTIL_EXPECT_TRUE(
            CheckOListElem(agent, net, prefix, olist_size, address, encap,
                other_agent.get()));
    }

    size_t GetVrfTableSize(BgpServerTestPtr server, const string &name) {
        RoutingInstanceMgr *rim = server->routing_instance_mgr();
        TASK_UTIL_EXPECT_TRUE(rim->GetRoutingInstance(name) != NULL);
        const RoutingInstance *rtinstance = rim->GetRoutingInstance(name);
        TASK_UTIL_EXPECT_TRUE(rtinstance->GetTable(Address::ERMVPN) != NULL);
        const BgpTable *table = rtinstance->GetTable(Address::ERMVPN);
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

int BgpXmppMcastTest::validate_done_;

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

class BgpXmppMcastErrorTest : public BgpXmppMcastTest {
protected:
    virtual void SetUp() {
        BgpXmppMcastTest::SetUp();

        Configure(config_tmpl1);
        task_util::WaitForIdle();

        BgpXmppMcastTest::SessionUp();
        BgpXmppMcastTest::Subscribe("blue", 1);
        task_util::WaitForIdle();
    }

    virtual void TearDown() {
        BgpXmppMcastTest::SessionDown();
        BgpXmppMcastTest::TearDown();
    }
};

TEST_F(BgpXmppMcastErrorTest, BadGroupAddress) {
    agent_xa_->AddMcastRoute("blue", "225.0.0,90.1.1.1", "10.1.1.1", "10-20");
    task_util::WaitForIdle();
    ErmVpnTable *blue_table_ = static_cast<ErmVpnTable *>(
        bs_x_->database()->FindTable("blue.ermvpn.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

TEST_F(BgpXmppMcastErrorTest, BadSourceAddress) {
    agent_xa_->AddMcastRoute("blue", "225.0.0.1,90.1.1", "10.1.1.1", "10-20");
    task_util::WaitForIdle();
    ErmVpnTable *blue_table_ = static_cast<ErmVpnTable *>(
        bs_x_->database()->FindTable("blue.ermvpn.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

TEST_F(BgpXmppMcastErrorTest, BadNexthopAddress) {
    agent_xa_->AddMcastRoute("blue", "225.0.0.1,90.1.1.1", "10.1", "10-20");
    task_util::WaitForIdle();
    ErmVpnTable *blue_table_ = static_cast<ErmVpnTable *>(
        bs_x_->database()->FindTable("blue.ermvpn.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

TEST_F(BgpXmppMcastErrorTest, BadLabelBlock1) {
    agent_xa_->AddMcastRoute("blue", "225.0.0.1,90.1.1.1", "10.1.1.1", "10,20");
    task_util::WaitForIdle();
    ErmVpnTable *blue_table_ = static_cast<ErmVpnTable *>(
        bs_x_->database()->FindTable("blue.ermvpn.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

TEST_F(BgpXmppMcastErrorTest, BadLabelBlock2) {
    agent_xa_->AddMcastRoute("blue", "225.0.0.1,90.1.1.1", "10.1.1.1", "1-2-3");
    task_util::WaitForIdle();
    ErmVpnTable *blue_table_ = static_cast<ErmVpnTable *>(
        bs_x_->database()->FindTable("blue.ermvpn.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);
}

class BgpXmppMcastSubscriptionTest : public BgpXmppMcastTest {
protected:
    virtual void SetUp() {
        BgpXmppMcastTest::SetUp();

        Configure(config_tmpl1);
        task_util::WaitForIdle();
        BgpXmppMcastTest::SessionUp();
    }

    virtual void TearDown() {
        BgpXmppMcastTest::SessionDown();
        BgpXmppMcastTest::TearDown();
    }
};

TEST_F(BgpXmppMcastSubscriptionTest, PendingSubscribe) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Register agent b to the multicast table and add a mcast route
    // after waiting for the subscription to be processed.
    agent_xb_->McastSubscribe("blue", 1);
    task_util::WaitForIdle();
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");

    // Register agent a to the multicast table and add a mcast route
    // without waiting for the subscription to be processed.
    agent_xa_->McastSubscribe("blue", 1);
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Verify labels.
    VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
    VerifyLabel(agent_xb_, "blue", mroute, 20000, 29999);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
}

TEST_F(BgpXmppMcastSubscriptionTest, PendingUnsubscribe) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Register agent b to the multicast table and add a mcast route
    // after waiting for the subscription to be processed.
    agent_xb_->McastSubscribe("blue", 1);
    task_util::WaitForIdle();
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");

    // Register agent a to the multicast table and add a mcast route
    // without waiting for the subscription to be processed. Then go
    // ahead and unsubscribe right away.
    agent_xa_->McastSubscribe("blue", 1);
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xa_->McastUnsubscribe("blue");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());

    // Delete mcast route for agent b.
    agent_xb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
}

TEST_F(BgpXmppMcastSubscriptionTest, SubsequentSubscribeUnsubscribe) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Register agent b to the multicast table and add a mcast route
    // after waiting for the subscription to be processed.
    agent_xb_->McastSubscribe("blue", 1);
    task_util::WaitForIdle();
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");

    // Register agent a to the multicast table and add a mcast route
    // without waiting for the subscription to be processed. Then go
    // ahead and unsubscribe right away. Then subscribe again with a
    // different id and add the route again.
    agent_xa_->McastSubscribe("blue", 1);
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xa_->McastUnsubscribe("blue");
    agent_xa_->McastSubscribe("blue", 2);
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Verify labels.
    VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
    VerifyLabel(agent_xb_, "blue", mroute, 20000, 29999);

    // Verify that agent a mcast route was added with instance_id = 2.
    ErmVpnTable *blue_table_ = static_cast<ErmVpnTable *>(
            bs_x_->database()->FindTable("blue.ermvpn.0"));
    const char *route = "0-127.0.0.1:2-0.0.0.0,225.0.0.1,0.0.0.0";
    ErmVpnPrefix prefix(ErmVpnPrefix::FromString(route));
    ErmVpnTable::RequestKey key(prefix, NULL);
    TASK_UTIL_EXPECT_TRUE(
        dynamic_cast<ErmVpnRoute *>(blue_table_->Find(&key)) != NULL);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

class BgpXmppMcastMultiAgentTest : public BgpXmppMcastTest {
protected:
    virtual void SetUp() {
        BgpXmppMcastTest::SetUp();

        Configure(config_tmpl1);
        task_util::WaitForIdle();

        BgpXmppMcastTest::SessionUp();
        BgpXmppMcastTest::Subscribe("blue", 1);
    }

    virtual void TearDown() {
        BgpXmppMcastTest::SessionDown();
        BgpXmppMcastTest::TearDown();
    }
};

TEST_F(BgpXmppMcastMultiAgentTest, SourceAndGroup) {
    const char *mroute = "225.0.0.1,90.1.1.1";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, GroupOnly) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Verify the labels used by all agents.
    VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
    VerifyLabel(agent_xb_, "blue", mroute, 20000, 29999);
    VerifyLabel(agent_xc_, "blue", mroute, 30000, 39999);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, MultipleRoutes) {
    const char *mroute_list[] = {
        "225.0.0.1,90.1.1.1",
        "225.0.0.1,90.1.1.2",
        "225.0.0.2,90.1.1.1",
        "225.0.0.2,90.1.1.2"
    };

    // Add mcast routes for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
        agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
        agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
        task_util::WaitForIdle();
    }

    // Verify that all agents have all routes.
    TASK_UTIL_EXPECT_EQ(sizeof(mroute_list) / sizeof(mroute_list[0]),
        agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(sizeof(mroute_list) / sizeof(mroute_list[0]),
        agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(sizeof(mroute_list) / sizeof(mroute_list[0]),
        agent_xc_->McastRouteCount());

    // Verify all OList elements for all routes on all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_);
        VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

        // Verify the labels used for the route by all agents.
        VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
        VerifyLabel(agent_xb_, "blue", mroute, 20000, 29999);
        VerifyLabel(agent_xc_, "blue", mroute, 30000, 39999);
    }

    // Delete mcast route for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->DeleteMcastRoute("blue", mroute);
        agent_xb_->DeleteMcastRoute("blue", mroute);
        agent_xc_->DeleteMcastRoute("blue", mroute);
        task_util::WaitForIdle();
    }
};

TEST_F(BgpXmppMcastMultiAgentTest, LabelExhaustion1) {
    const char *mroute_list[] = {
        "225.0.0.1,90.1.1.1",
        "225.0.0.1,90.1.1.2",
    };

    // Add mcast route 0 for all agents.
    agent_xa_->AddMcastRoute("blue", mroute_list[0], "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute_list[0], "10.1.1.2", "20000-29999");
    agent_xc_->AddMcastRoute("blue", mroute_list[0], "10.1.1.3", "30000-30000");
    task_util::WaitForIdle();

    // Verify that all native, local and global routes got added.
    TASK_UTIL_EXPECT_EQ(5, GetVrfTableSize(bs_x_, "blue"));

    // Verify all OList elements for route 0 on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute_list[0], 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xa_, "blue", mroute_list[0], 2, "10.1.1.3", agent_xc_);
    VerifyOListElem(agent_xb_, "blue", mroute_list[0], 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute_list[0], 1, "10.1.1.1", agent_xa_);

    // Verify the labels used for route 0 by all agents.
    VerifyLabel(agent_xa_, "blue", mroute_list[0], 10000, 19999);
    VerifyLabel(agent_xb_, "blue", mroute_list[0], 20000, 29999);
    VerifyLabel(agent_xc_, "blue", mroute_list[0], 30000, 30000);

    // Add mcast route 1 for all agents.
    agent_xa_->AddMcastRoute("blue", mroute_list[1], "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute_list[1], "10.1.1.2", "20000-29999");
    agent_xc_->AddMcastRoute("blue", mroute_list[1], "10.1.1.3", "30000-30000");
    task_util::WaitForIdle();

    // Verify that all native, local and global routes got added.
    TASK_UTIL_EXPECT_EQ(10, GetVrfTableSize(bs_x_, "blue"));

    // Verify all OList elements for route 1 on agents a and b.
    // Verify that there's no route on agent c.
    VerifyOListElem(agent_xa_, "blue", mroute_list[1], 1, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xb_, "blue", mroute_list[1], 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute_list[1], 0);

    // Verify the labels used for route 1 by agents a and b.
    VerifyLabel(agent_xa_, "blue", mroute_list[1], 10000, 19999);
    VerifyLabel(agent_xb_, "blue", mroute_list[1], 20000, 29999);

    // Verify that agents a and b have 2 routes, but agent c has only 1.
    TASK_UTIL_EXPECT_EQ(2, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Delete mcast route for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->DeleteMcastRoute("blue", mroute);
        agent_xb_->DeleteMcastRoute("blue", mroute);
        agent_xc_->DeleteMcastRoute("blue", mroute);
        task_util::WaitForIdle();
    }
};

TEST_F(BgpXmppMcastMultiAgentTest, LabelExhaustion2) {
    const char *mroute_list[] = {
        "225.0.0.1,90.1.1.1",
        "225.0.0.1,90.1.1.2",
    };

    // Add mcast route 0 for all agents.
    agent_xa_->AddMcastRoute("blue", mroute_list[0], "10.1.1.1", "10000-10000");
    agent_xb_->AddMcastRoute("blue", mroute_list[0], "10.1.1.2", "20000-29999");
    agent_xc_->AddMcastRoute("blue", mroute_list[0], "10.1.1.3", "30000-39999");
    task_util::WaitForIdle();

    // Verify that all native and local routes got added.
    TASK_UTIL_EXPECT_EQ(5, GetVrfTableSize(bs_x_, "blue"));

    // Verify all OList elements for route 0 on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute_list[0], 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xa_, "blue", mroute_list[0], 2, "10.1.1.3", agent_xc_);
    VerifyOListElem(agent_xb_, "blue", mroute_list[0], 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute_list[0], 1, "10.1.1.1", agent_xa_);

    // Verify the labels used for route 0 by all agents.
    VerifyLabel(agent_xa_, "blue", mroute_list[0], 10000, 10000);
    VerifyLabel(agent_xb_, "blue", mroute_list[0], 20000, 29999);
    VerifyLabel(agent_xc_, "blue", mroute_list[0], 30000, 39999);

    // Add mcast route 1 for all agents.
    agent_xa_->AddMcastRoute("blue", mroute_list[1], "10.1.1.1", "10000-10000");
    agent_xb_->AddMcastRoute("blue", mroute_list[1], "10.1.1.2", "20000-29999");
    agent_xc_->AddMcastRoute("blue", mroute_list[1], "10.1.1.3", "30000-39999");
    task_util::WaitForIdle();

    // Verify that all native and local routes got added.
    TASK_UTIL_EXPECT_EQ(10, GetVrfTableSize(bs_x_, "blue"));

    // Verify all OList elements for route 1 on agents b and c.
    // Verify that there's no route on agent c.
    VerifyOListElem(agent_xb_, "blue", mroute_list[1], 1, "10.1.1.3", agent_xc_);
    VerifyOListElem(agent_xc_, "blue", mroute_list[1], 1, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xa_, "blue", mroute_list[1], 0);

    // Verify the labels used for route 1 by agents b and c.
    VerifyLabel(agent_xb_, "blue", mroute_list[1], 20000, 29999);
    VerifyLabel(agent_xc_, "blue", mroute_list[1], 30000, 39999);

    // Verify that agents a and c have 2 routes, but agent a has only 1.
    TASK_UTIL_EXPECT_EQ(2, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_xc_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());

    // Delete mcast route for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->DeleteMcastRoute("blue", mroute);
        agent_xb_->DeleteMcastRoute("blue", mroute);
        agent_xc_->DeleteMcastRoute("blue", mroute);
        task_util::WaitForIdle();
    }
};

TEST_F(BgpXmppMcastMultiAgentTest, LabelExhaustion3) {
    const char *mroute_list[] = {
        "225.0.0.1,90.1.1.1",
        "225.0.0.1,90.1.1.2",
    };

    // Add mcast route 0 for all agents.
    agent_xa_->AddMcastRoute("blue", mroute_list[0], "10.1.1.1", "10000-10000");
    agent_xb_->AddMcastRoute("blue", mroute_list[0], "10.1.1.2", "20000-20000");
    agent_xc_->AddMcastRoute("blue", mroute_list[0], "10.1.1.3", "30000-30000");
    task_util::WaitForIdle();

    // Verify that all native and local routes got added.
    TASK_UTIL_EXPECT_EQ(5, GetVrfTableSize(bs_x_, "blue"));

    // Verify all OList elements for route 0 on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute_list[0], 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xa_, "blue", mroute_list[0], 2, "10.1.1.3", agent_xc_);
    VerifyOListElem(agent_xb_, "blue", mroute_list[0], 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute_list[0], 1, "10.1.1.1", agent_xa_);

    // Verify the labels used for route 0 by all agents.
    VerifyLabel(agent_xa_, "blue", mroute_list[0], 10000, 10000);
    VerifyLabel(agent_xb_, "blue", mroute_list[0], 20000, 20000);
    VerifyLabel(agent_xc_, "blue", mroute_list[0], 30000, 30000);

    // Add mcast route 1 for all agents.
    agent_xa_->AddMcastRoute("blue", mroute_list[1], "10.1.1.1", "10000-10000");
    agent_xb_->AddMcastRoute("blue", mroute_list[1], "10.1.1.2", "20000-20000");
    agent_xc_->AddMcastRoute("blue", mroute_list[1], "10.1.1.3", "30000-30000");
    task_util::WaitForIdle();

    // Verify that all native routes got added.
    // There should be a local route for only the first mcast route.
    TASK_UTIL_EXPECT_EQ(8, GetVrfTableSize(bs_x_, "blue"));
    task_util::WaitForIdle();

    // Verify that there's no route on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute_list[1], 0);
    VerifyOListElem(agent_xb_, "blue", mroute_list[1], 0);
    VerifyOListElem(agent_xc_, "blue", mroute_list[1], 0);

    // Verify that all agents have only 1 route.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Delete mcast route for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->DeleteMcastRoute("blue", mroute);
        agent_xb_->DeleteMcastRoute("blue", mroute);
        agent_xc_->DeleteMcastRoute("blue", mroute);
        task_util::WaitForIdle();
    }
};

TEST_F(BgpXmppMcastMultiAgentTest, Join) {
    const char *mroute = "225.0.0.1,90.1.1.1";
    int label_xa, label_xb, label_xc;

    // Add mcast route for agents a and b.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute, 0);

    // Verify the labels used for route by all agents.
    label_xa = VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
    label_xb = VerifyLabel(agent_xb_, "blue", mroute, 20000, 29999);
    label_xc = VerifyLabel(agent_xc_, "blue", mroute);

    // Add mcast route for agent c.
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Make sure that labels have changed for all agents.
    TASK_UTIL_EXPECT_NE(label_xa,
        VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999));
    TASK_UTIL_EXPECT_NE(label_xb,
        VerifyLabel(agent_xb_, "blue", mroute, 20000, 29999));
    TASK_UTIL_EXPECT_NE(label_xc,
        VerifyLabel(agent_xc_, "blue", mroute, 30000, 39999));

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, Leave) {
    const char *mroute = "225.0.0.1,90.1.1.1";
    int label_xa, label_xb, label_xc;

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Get the labels used for route by all agents.
    label_xa = VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
    label_xb = VerifyLabel(agent_xb_, "blue", mroute, 20000, 29999);
    label_xc = VerifyLabel(agent_xc_, "blue", mroute, 30000, 39999);

    // Delete mcast route for agent c.
    agent_xc_->DeleteMcastRoute("blue", mroute);

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute, 0);

    // Make sure that labels have changed for all agents.
    TASK_UTIL_EXPECT_NE(label_xa,
        VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999));
    TASK_UTIL_EXPECT_NE(label_xb,
        VerifyLabel(agent_xb_, "blue", mroute, 20000, 29999));
    TASK_UTIL_EXPECT_NE(label_xc,
        VerifyLabel(agent_xc_, "blue", mroute));

    // Delete mcast route for agents a and b.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, ValidateShowRoute) {
    const char *mroute = "225.0.0.1,90.1.1.1";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
    task_util::WaitForIdle();

    // Verify that all agents have the route.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Verify routes via sandesh.
    BgpSandeshContext sandesh_context;
    RegisterSandeshShowXmppExtensions(&sandesh_context);
    sandesh_context.bgp_server = bs_x_.get();
    sandesh_context.xmpp_peer_manager = bcm_x_.get();
    Sandesh::set_client_context(&sandesh_context);

    // First get all tables - blue.ermvpn.0, bgp.ermvpn.0 and bgp.rtarget.0.
    std::vector<size_t> result = {5, 2, 4};
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteResponse, _1, result));
    ShowRouteReq *show_req = new ShowRouteReq;
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);

    // Now get blue.ermvpn.0.
    result = list_of(5).convert_to_container<vector<size_t> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteResponse, _1, result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.ermvpn.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);

    // Now get bgp.ermvpn.0.
    result = list_of(2).convert_to_container<vector<size_t> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteResponse, _1, result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("bgp.ermvpn.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify that no agents have the route.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_xc_->McastRouteCount());

    // Get blue.ermvpn.0 again.
    result.resize(0);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteResponse, _1, result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.ermvpn.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);
};

TEST_F(BgpXmppMcastMultiAgentTest, ValidateShowMulticastManagerDetail) {
    const char *mroutes[] = { "225.0.0.1,90.1.1.1", "225.0.0.2,90.1.1.1" };

    // Add mcast routes for all agents.
    BOOST_FOREACH(const char *mroute, mroutes) {
        agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
        agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
        agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
        task_util::WaitForIdle();
    }

    // Verify that all agents have the routes.
    TASK_UTIL_EXPECT_EQ(2, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(2, agent_xc_->McastRouteCount());

    // Verify multicast manager detail via sandesh.
    BgpSandeshContext sandesh_context;
    RegisterSandeshShowXmppExtensions(&sandesh_context);
    sandesh_context.bgp_server = bs_x_.get();
    sandesh_context.xmpp_peer_manager = bcm_x_.get();
    Sandesh::set_client_context(&sandesh_context);
    vector<string> result = {"225.0.0.1", "225.0.0.2"};
    Sandesh::set_response_callback(
        boost::bind(ValidateShowManagerMulticastDetailResponse, _1, result));
    ShowMulticastManagerDetailReq *show_req = new ShowMulticastManagerDetailReq;
    validate_done_ = 0;
    show_req->set_name("blue.ermvpn.0");
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);

    result.clear();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowManagerMulticastDetailResponse, _1, result));
    show_req = new ShowMulticastManagerDetailReq;
    validate_done_ = 0;
    show_req->set_name("pink.ermvpn.0");
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);
};

TEST_F(BgpXmppMcastMultiAgentTest, ChangeNexthop) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Update mcast route for agent_xa - change nexthop to 10.1.1.101.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.101", "10000-19999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.101", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.101", agent_xa_);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastMultiAgentTest, MultipleNetworks) {
    const char *mroute = "225.0.0.1,90.1.1.1";
    const char *networks[] = { "blue", "pink" };

    // Subscribe all agents to pink network.
    // All agents already subscribed to blue during test setup.
    Subscribe("pink", 2);

    // Add mcast routes for all agents.
    BOOST_FOREACH(const char *net, networks) {
        agent_xa_->AddMcastRoute(net, mroute, "10.1.1.1", "10000-19999");
        agent_xb_->AddMcastRoute(net, mroute, "10.1.1.2", "20000-29999");
        agent_xc_->AddMcastRoute(net, mroute, "10.1.1.3", "30000-39999");
        task_util::WaitForIdle();
    }

    // Verify all OList elements for the route on all agents.
    BOOST_FOREACH(const char *net, networks) {
        VerifyOListElem(agent_xa_, net, mroute, 2, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xa_, net, mroute, 2, "10.1.1.3", agent_xc_);
        VerifyOListElem(agent_xb_, net, mroute, 1, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xc_, net, mroute, 1, "10.1.1.1", agent_xa_);
    }

    // Delete mcast route for all agents.
    BOOST_FOREACH(const char *net, networks) {
        agent_xa_->DeleteMcastRoute(net, mroute);
        agent_xb_->DeleteMcastRoute(net, mroute);
        agent_xc_->DeleteMcastRoute(net, mroute);
        task_util::WaitForIdle();
    }
};

class BgpXmppMcastEncapTest : public BgpXmppMcastMultiAgentTest {
protected:
};

TEST_F(BgpXmppMcastEncapTest, ImplicitOnly) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ExplicitSingle) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999", "gre");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999", "gre");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999", "gre");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_, "gre");
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_, "gre");
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "gre");
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "gre");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ExplicitAll) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999", "all");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999", "all");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999", "all");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_, "all");
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_, "all");
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "all");
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "all");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ExplicitMixed1) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999", "all");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999", "gre");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999", "udp");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_, "gre");
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_, "udp");
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "all");
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "all");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ExplicitMixed2) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999", "gre");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999", "all");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999", "all");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_, "all");
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_, "all");
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "gre");
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "gre");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ImplicitAndExplicitMixed1) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999", "all");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "all");
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "all");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ImplicitAndExplicitMixed2) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999", "gre");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999", "all");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_, "gre");
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_, "all");
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, ImplicitAndExplicitMixed3) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999", "all");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999", "gre");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_, "gre");
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "all");
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "all");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcastEncapTest, Change) {
    const char *mroute = "255.255.255.255,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Update mcast route for all agents - change encaps to all.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999", "all");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999", "all");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999", "all");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_, "all");
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_, "all");
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "all");
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "all");

    // Update mcast route for all agents - change encaps to gre.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999", "gre");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999", "gre");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999", "gre");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_, "gre");
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_, "gre");
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "gre");
    VerifyOListElem(agent_xc_, "blue", mroute, 1, "10.1.1.1", agent_xa_, "gre");

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

static const char *config_tmpl2 = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.101</identifier>\
        <address>127.0.0.101</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.102</identifier>\
        <address>127.0.0.102</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
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

class BgpXmppMcast2ServerTestBase : public BgpXmppMcastTest {
protected:
    virtual void Configure(const char *config_tmpl) {
        char config[8192];
        snprintf(config, sizeof(config), config_tmpl,
            bs_x_->session_manager()->GetPort(),
            bs_y_->session_manager()->GetPort());
        bs_x_->Configure(config);
        bs_y_->Configure(config);
        task_util::WaitForIdle();
    }

    virtual void SetUp() {
        bs_y_.reset(new BgpServerTest(&evm_, "Y"));
        xs_y_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        bs_y_->session_manager()->Initialize(0);
        xs_y_->Initialize(0, false);
        bcm_y_.reset(new BgpXmppChannelManager(xs_y_, bs_y_.get()));

        BgpXmppMcastTest::SetUp();
    }

    virtual void TearDown() {
        xs_y_->Shutdown();
        task_util::WaitForIdle();
        bs_y_->Shutdown();
        task_util::WaitForIdle();
        bcm_y_.reset();
        TcpServerManager::DeleteServer(xs_y_);
        xs_y_ = NULL;

        agent_ya_->Delete();
        agent_yb_->Delete();
        agent_yc_->Delete();

        BgpXmppMcastTest::TearDown();
    }

    virtual void SessionUp() {
        BgpXmppMcastTest::SessionUp();

        agent_ya_.reset(new test::NetworkAgentMock(
            &evm_, "agent-ya", xs_y_->GetPort(), "127.0.0.4", "127.0.0.102"));
        TASK_UTIL_EXPECT_TRUE(agent_ya_->IsEstablished());
        agent_yb_.reset(new test::NetworkAgentMock(
            &evm_, "agent-yb", xs_y_->GetPort(), "127.0.0.5", "127.0.0.102"));
        TASK_UTIL_EXPECT_TRUE(agent_yb_->IsEstablished());
        agent_yc_.reset(new test::NetworkAgentMock(
            &evm_, "agent-yc", xs_y_->GetPort(), "127.0.0.6", "127.0.0.102"));
        TASK_UTIL_EXPECT_TRUE(agent_yc_->IsEstablished());
    }

    virtual void SessionDown() {
        BgpXmppMcastTest::SessionDown();

        agent_ya_->SessionDown();
        agent_yb_->SessionDown();
        agent_yc_->SessionDown();
        task_util::WaitForIdle();
    }

    virtual void Subscribe(const string net, int id) {
        BgpXmppMcastTest::Subscribe(net, id);

        agent_ya_->McastSubscribe(net, id);
        agent_yb_->McastSubscribe(net, id);
        agent_yc_->McastSubscribe(net, id);
        task_util::WaitForIdle();
    }

    BgpServerTestPtr bs_y_;
    XmppServer *xs_y_;
    boost::scoped_ptr<BgpXmppChannelManager> bcm_y_;
    boost::shared_ptr<test::NetworkAgentMock> agent_ya_;
    boost::shared_ptr<test::NetworkAgentMock> agent_yb_;
    boost::shared_ptr<test::NetworkAgentMock> agent_yc_;
};

class BgpXmppMcast2ServerTest : public BgpXmppMcast2ServerTestBase {
protected:
    virtual void SetUp() {
        BgpXmppMcast2ServerTestBase::SetUp();

        Configure(config_tmpl2);
        task_util::WaitForIdle();

        BgpXmppMcast2ServerTestBase::SessionUp();
        BgpXmppMcast2ServerTestBase::Subscribe("blue", 1);
    }

    virtual void TearDown() {
        BgpXmppMcast2ServerTestBase::SessionDown();
        BgpXmppMcast2ServerTestBase::TearDown();
    }
};

TEST_F(BgpXmppMcast2ServerTest, SingleAgent) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for agent xa.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());

    // Add mcast route for agent ya.
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_ya_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.4", agent_ya_);
    VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Verify the labels used by all agents.
    VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
    VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);

    // Delete mcast route for agent ya.
    agent_ya_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());

    // Delete mcast route for agent xa.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());
};

TEST_F(BgpXmppMcast2ServerTest, SingleAgentXmppSessionDown) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.4", agent_ya_);
    VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Verify the labels used by all agents.
    VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
    VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);

    // Bring down the session to agent xa.
    agent_xa_->SessionDown();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_ya_->McastRouteCount());

    // Delete mcast route for agent ya.
    agent_ya_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());
};

TEST_F(BgpXmppMcast2ServerTest, SingleAgentMultipleRoutes) {
    const char *mroute_list[] = {
        "225.0.0.1,90.1.1.1",
        "225.0.0.1,90.1.1.2",
        "225.0.0.2,90.1.1.1",
        "225.0.0.2,90.1.1.2"
    };

    // Add mcast routes for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
        agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
        task_util::WaitForIdle();
    }

    // Verify that all agents have all routes.
    TASK_UTIL_EXPECT_EQ(sizeof(mroute_list) / sizeof(mroute_list[0]),
        agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(sizeof(mroute_list) / sizeof(mroute_list[0]),
        agent_ya_->McastRouteCount());

    // Verify all OList elements and labels for all routes on all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

        VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
        VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);
    }

    // Delete mcast route for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->DeleteMcastRoute("blue", mroute);
        agent_ya_->DeleteMcastRoute("blue", mroute);
        task_util::WaitForIdle();
    }
};

TEST_F(BgpXmppMcast2ServerTest, MultipleAgent1) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for agent xa and ya.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    task_util::WaitForIdle();
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.4", agent_ya_);
    VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Add mcast route for agent xb and yb.
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    task_util::WaitForIdle();
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xb_, "blue", mroute, 2, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xb_, "blue", mroute, 2, "10.1.1.5", agent_yb_);
    VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
    VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
    VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

    // Verify the labels used by all agents.
    VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
    VerifyLabel(agent_xb_, "blue", mroute, 20000, 29999);
    VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);
    VerifyLabel(agent_yb_, "blue", mroute, 50000, 59999);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcast2ServerTest, MultipleAgent2) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for agent xa and xb.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    task_util::WaitForIdle();

    // Add mcast route for agent ya and yb.
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xb_, "blue", mroute, 2, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xb_, "blue", mroute, 2, "10.1.1.5", agent_yb_);
    VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
    VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
    VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

    // Verify the labels used by all agents.
    VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
    VerifyLabel(agent_xb_, "blue", mroute, 20000, 29999);
    VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);
    VerifyLabel(agent_yb_, "blue", mroute, 50000, 59999);

    // Delete mcast route for agents xb and yb.
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.4", agent_ya_);
    VerifyOListElem(agent_xb_, "blue", mroute, 0);
    VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_yb_, "blue", mroute, 0);

    // Verify the labels used by agent xa and ya.
    VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
    VerifyLabel(agent_xb_, "blue", mroute);
    VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);
    VerifyLabel(agent_yb_, "blue", mroute);

    // Delete mcast route for agents ya and yb.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcast2ServerTest, MultipleAgent3) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
    task_util::WaitForIdle();
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    agent_yc_->AddMcastRoute("blue", mroute, "10.1.1.6", "60000-69999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_ya_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_yb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_yc_->McastRouteCount());

    // Verify all OList elements on all agents, including labels.
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute, 2, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xc_, "blue", mroute, 2, "10.1.1.6", agent_yc_);

    VerifyOListElem(agent_ya_, "blue", mroute, 2, "10.1.1.5", agent_yb_);
    VerifyOListElem(agent_ya_, "blue", mroute, 2, "10.1.1.6", agent_yc_);
    VerifyOListElem(agent_yb_, "blue", mroute, 1, "10.1.1.4", agent_ya_);
    VerifyOListElem(agent_yc_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
    VerifyOListElem(agent_yc_, "blue", mroute, 2, "10.1.1.3", agent_xc_);

    // Verify the labels used by agent xa/xb/xc and ya/yb/yc.
    VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
    VerifyLabel(agent_xb_, "blue", mroute, 20000, 29999);
    VerifyLabel(agent_xc_, "blue", mroute, 30000, 39999);
    VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);
    VerifyLabel(agent_yb_, "blue", mroute, 50000, 59999);
    VerifyLabel(agent_yc_, "blue", mroute, 60000, 69999);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_yc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

TEST_F(BgpXmppMcast2ServerTest, MultipleAgentMultipleRoutes) {
    const char *mroute_list[] = {
        "225.0.0.1,90.1.1.1",
        "225.0.0.1,90.1.1.2",
        "225.0.0.2,90.1.1.1",
        "225.0.0.2,90.1.1.2"
    };

    // Add mcast routes for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
        agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
        agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
        agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
        agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
        agent_yc_->AddMcastRoute("blue", mroute, "10.1.1.6", "60000-69999");
        task_util::WaitForIdle();
    }

    // Verify all OList elements for all routes on all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_);
        VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xc_, "blue", mroute, 2, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xc_, "blue", mroute, 2, "10.1.1.6", agent_yc_);

        VerifyOListElem(agent_ya_, "blue", mroute, 2, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_ya_, "blue", mroute, 2, "10.1.1.6", agent_yc_);
        VerifyOListElem(agent_yb_, "blue", mroute, 1, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yc_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yc_, "blue", mroute, 2, "10.1.1.3", agent_xc_);
    }

    // Delete mcast route for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->DeleteMcastRoute("blue", mroute);
        agent_xb_->DeleteMcastRoute("blue", mroute);
        agent_xc_->DeleteMcastRoute("blue", mroute);
        agent_ya_->DeleteMcastRoute("blue", mroute);
        agent_yb_->DeleteMcastRoute("blue", mroute);
        agent_yc_->DeleteMcastRoute("blue", mroute);
        task_util::WaitForIdle();
    }
};

TEST_F(BgpXmppMcast2ServerTest, MultipleAgentMultipleNetworks) {
    const char *mroute = "225.0.0.1,90.1.1.1";
    const char *networks[] = { "blue", "pink" };

    // Subscribe all agents to pink network.
    // All agents already subscribed to blue during test setup.
    Subscribe("pink", 2);

    // Add mcast routes for all agents.
    BOOST_FOREACH(const char *net, networks) {
        agent_xa_->AddMcastRoute(net, mroute, "10.1.1.1", "10000-19999");
        agent_xb_->AddMcastRoute(net, mroute, "10.1.1.2", "20000-29999");
        agent_xc_->AddMcastRoute(net, mroute, "10.1.1.3", "30000-39999");
        agent_ya_->AddMcastRoute(net, mroute, "10.1.1.4", "40000-49999");
        agent_yb_->AddMcastRoute(net, mroute, "10.1.1.5", "50000-59999");
        agent_yc_->AddMcastRoute(net, mroute, "10.1.1.6", "60000-69999");
        task_util::WaitForIdle();
    }

    // Verify all OList elements for the route on all agents.
    BOOST_FOREACH(const char *net, networks) {
        VerifyOListElem(agent_xa_, net, mroute, 2, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xa_, net, mroute, 2, "10.1.1.3", agent_xc_);
        VerifyOListElem(agent_xb_, net, mroute, 1, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xc_, net, mroute, 2, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xc_, net, mroute, 2, "10.1.1.6", agent_yc_);

        VerifyOListElem(agent_ya_, net, mroute, 2, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_ya_, net, mroute, 2, "10.1.1.6", agent_yc_);
        VerifyOListElem(agent_yb_, net, mroute, 1, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yc_, net, mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yc_, net, mroute, 2, "10.1.1.3", agent_xc_);
    }

    // Delete mcast route for all agents.
    BOOST_FOREACH(const char *net, networks) {
        agent_xa_->DeleteMcastRoute(net, mroute);
        agent_xb_->DeleteMcastRoute(net, mroute);
        agent_xc_->DeleteMcastRoute(net, mroute);
        agent_ya_->DeleteMcastRoute(net, mroute);
        agent_yb_->DeleteMcastRoute(net, mroute);
        agent_yc_->DeleteMcastRoute(net, mroute);
        task_util::WaitForIdle();
    }
};

static const char *config_tmpl2_new = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>%s</identifier>\
        <address>127.0.0.101</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>%s</identifier>\
        <address>127.0.0.102</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>erm-vpn</family>\
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

// Parameterize RouterId change for X and Y.

typedef std::tr1::tuple<bool, bool> RouterIdChangeParams;

class BgpXmppMcast2ServerParamTest :
    public BgpXmppMcast2ServerTest,
    public ::testing::WithParamInterface<RouterIdChangeParams> {

protected:
    void Reconfigure(const char *config_tmpl) {
        char config[8192];
        snprintf(config, sizeof(config), config_tmpl,
            tr1::get<0>(GetParam()) ? "192.168.0.111" : "192.168.0.101",
            bs_x_->session_manager()->GetPort(),
            tr1::get<1>(GetParam()) ? "192.168.0.112" : "192.168.0.102",
            bs_y_->session_manager()->GetPort());
        bs_x_->Configure(config);
        bs_y_->Configure(config);
        task_util::WaitForIdle();
    }
};

TEST_P(BgpXmppMcast2ServerParamTest, SingleAgent) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {

        // Verify number of routes on all agents.
        TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ(1, agent_ya_->McastRouteCount());

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

        // Verify the labels used by all agents.
        VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
        VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);

        uint32_t flap_count_xa_ = agent_xa_->flap_count();
        uint32_t flap_count_ya_ = agent_ya_->flap_count();

        // Update the RouterIDs of X and Y as appropriate.
        Reconfigure(config_tmpl2_new);

        if (idx == 0) {
            if (tr1::get<0>(GetParam())) {
                TASK_UTIL_EXPECT_NE(flap_count_xa_, agent_xa_->flap_count());
                TASK_UTIL_EXPECT_TRUE(agent_xa_->IsEstablished());
                agent_xa_->ClearInstances();
                TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
                agent_xa_->McastSubscribe("blue", 1);
            }

            if (tr1::get<1>(GetParam())) {
                TASK_UTIL_EXPECT_NE(flap_count_ya_, agent_ya_->flap_count());
                TASK_UTIL_EXPECT_TRUE(agent_ya_->IsEstablished());
                agent_ya_->ClearInstances();
                TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());
                agent_ya_->McastSubscribe("blue", 1);
            }

            agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
            agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
            TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
            TASK_UTIL_EXPECT_EQ(1, agent_ya_->McastRouteCount());
        }
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_xa_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());
};

TEST_P(BgpXmppMcast2ServerParamTest, MultipleAgent) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
    task_util::WaitForIdle();
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    agent_yc_->AddMcastRoute("blue", mroute, "10.1.1.6", "60000-69999");
    task_util::WaitForIdle();

    uint32_t flap_count_xa_ = agent_xa_->flap_count();
    uint32_t flap_count_xb_ = agent_xb_->flap_count();
    uint32_t flap_count_xc_ = agent_xc_->flap_count();
    uint32_t flap_count_ya_ = agent_ya_->flap_count();
    uint32_t flap_count_yb_ = agent_yb_->flap_count();
    uint32_t flap_count_yc_ = agent_yc_->flap_count();

    for (int idx = 0; idx < 3; ++idx) {

        // Verify number of routes on all agents.
        TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ(1, agent_ya_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ(1, agent_yb_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ(1, agent_yc_->McastRouteCount());

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.3", agent_xc_);
        VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xc_, "blue", mroute, 2, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xc_, "blue", mroute, 2, "10.1.1.6", agent_yc_);

        VerifyOListElem(agent_ya_, "blue", mroute, 2, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_ya_, "blue", mroute, 2, "10.1.1.6", agent_yc_);
        VerifyOListElem(agent_yb_, "blue", mroute, 1, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yc_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yc_, "blue", mroute, 2, "10.1.1.3", agent_xc_);

        // Verify the labels used by all agents.
        VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
        VerifyLabel(agent_xb_, "blue", mroute, 20000, 29999);
        VerifyLabel(agent_xc_, "blue", mroute, 30000, 39999);
        VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);
        VerifyLabel(agent_yb_, "blue", mroute, 50000, 59999);
        VerifyLabel(agent_yc_, "blue", mroute, 60000, 69999);

        // Update the RouterIDs of X and Y as appropriate.
        Reconfigure(config_tmpl2_new);

        if (idx == 0) {
            if (tr1::get<0>(GetParam())) {
                TASK_UTIL_EXPECT_NE(flap_count_xa_, agent_xa_->flap_count());
                TASK_UTIL_EXPECT_NE(flap_count_xb_, agent_xb_->flap_count());
                TASK_UTIL_EXPECT_NE(flap_count_xc_, agent_xc_->flap_count());
                TASK_UTIL_EXPECT_TRUE(agent_xa_->IsEstablished());
                TASK_UTIL_EXPECT_TRUE(agent_xb_->IsEstablished());
                TASK_UTIL_EXPECT_TRUE(agent_xc_->IsEstablished());
                agent_xa_->ClearInstances();
                agent_xb_->ClearInstances();
                agent_xc_->ClearInstances();
                TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
                TASK_UTIL_EXPECT_EQ(0, agent_xb_->McastRouteCount());
                TASK_UTIL_EXPECT_EQ(0, agent_xc_->McastRouteCount());
                agent_xa_->McastSubscribe("blue", 1);
                agent_xb_->McastSubscribe("blue", 1);
                agent_xc_->McastSubscribe("blue", 1);
            }

            if (tr1::get<1>(GetParam())) {
                TASK_UTIL_EXPECT_NE(flap_count_ya_, agent_ya_->flap_count());
                TASK_UTIL_EXPECT_NE(flap_count_yb_, agent_yb_->flap_count());
                TASK_UTIL_EXPECT_NE(flap_count_yc_, agent_yc_->flap_count());
                TASK_UTIL_EXPECT_TRUE(agent_ya_->IsEstablished());
                TASK_UTIL_EXPECT_TRUE(agent_yb_->IsEstablished());
                TASK_UTIL_EXPECT_TRUE(agent_yc_->IsEstablished());
                agent_ya_->ClearInstances();
                agent_yb_->ClearInstances();
                agent_yc_->ClearInstances();
                TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());
                TASK_UTIL_EXPECT_EQ(0, agent_yb_->McastRouteCount());
                TASK_UTIL_EXPECT_EQ(0, agent_yc_->McastRouteCount());
                agent_ya_->McastSubscribe("blue", 1);
                agent_yb_->McastSubscribe("blue", 1);
                agent_yc_->McastSubscribe("blue", 1);
            }

            agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
            agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
            agent_xc_->AddMcastRoute("blue", mroute, "10.1.1.3", "30000-39999");
            agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
            agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
            agent_yc_->AddMcastRoute("blue", mroute, "10.1.1.6", "60000-69999");
            TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
            TASK_UTIL_EXPECT_EQ(1, agent_xb_->McastRouteCount());
            TASK_UTIL_EXPECT_EQ(1, agent_xc_->McastRouteCount());
            TASK_UTIL_EXPECT_EQ(1, agent_ya_->McastRouteCount());
            TASK_UTIL_EXPECT_EQ(1, agent_yb_->McastRouteCount());
            TASK_UTIL_EXPECT_EQ(1, agent_yc_->McastRouteCount());
        }
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_xc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_yc_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

INSTANTIATE_TEST_CASE_P(Instance, BgpXmppMcast2ServerParamTest,
    ::testing::Combine(::testing::Bool(), ::testing::Bool()));

static const char *config_tmpl22_x = "\
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

static const char *config_tmpl22_y = "\
<config>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.102</identifier>\
        <address>127.0.0.102</address>\
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

static const char *config_tmpl22 = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.101</identifier>\
        <address>127.0.0.101</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.102</identifier>\
        <address>127.0.0.102</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
</config>\
";

static const char *config_tmpl22_bad = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.101</identifier>\
        <address>127.0.0.101</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>inet-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.102</identifier>\
        <address>127.0.0.102</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
</config>\
";

//
// Don't create the BGP sessions during SetUp.
//
class BgpXmppMcast2ServerTest2 : public BgpXmppMcast2ServerTestBase {
protected:
    virtual void SetUp() {
        BgpXmppMcast2ServerTestBase::SetUp();

        BgpXmppMcastTest::Configure(bs_x_, config_tmpl22_x);
        BgpXmppMcastTest::Configure(bs_y_, config_tmpl22_y);
        task_util::WaitForIdle();

        BgpXmppMcast2ServerTestBase::SessionUp();
        BgpXmppMcast2ServerTestBase::Subscribe("blue", 1);
    }

    virtual void TearDown() {
        BgpXmppMcast2ServerTestBase::SessionDown();
        BgpXmppMcast2ServerTestBase::TearDown();
    }
};

TEST_F(BgpXmppMcast2ServerTest2, BgpConnectLater_SingleAgent) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_ya_->McastRouteCount());

    // Now bring up the bgp session.
    Configure(config_tmpl22);

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_ya_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.4", agent_ya_);
    VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());
};

TEST_F(BgpXmppMcast2ServerTest2, BgpConnectLater_MultipleAgent) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    task_util::WaitForIdle();

    // Verify all OList elements on all agents.
    // There should be no connectivity between trees build by X and Y.
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
    VerifyOListElem(agent_yb_, "blue", mroute, 1, "10.1.1.4", agent_ya_);

    // Now bring up the bgp session.
    Configure(config_tmpl22);

    // Verify all OList elements on all agents.
    // There should be connectivity between trees build by X and Y.
    VerifyOListElem(agent_xb_, "blue", mroute, 2, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xb_, "blue", mroute, 2, "10.1.1.5", agent_yb_);
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
    VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_yb_->McastRouteCount());
};

TEST_F(BgpXmppMcast2ServerTest2, BgpSessionBounce_SingleAgent) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {

        // Bring up the bgp session.
        Configure(config_tmpl22);

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

        // Now bring down the session by having mismatched families.
        Configure(config_tmpl22_bad);

        // Verify number of routes on all agents.
        TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
        TASK_UTIL_EXPECT_EQ(1, agent_ya_->McastRouteCount());
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());
};

TEST_F(BgpXmppMcast2ServerTest2, BgpSessionBounce_MultipleAgent) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {

        // Bring up the bgp session.
        Configure(config_tmpl22);

        // Verify all OList elements on all agents.
        // There should be connectivity between trees build by X and Y.
        VerifyOListElem(agent_xb_, "blue", mroute, 2, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 2, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);

        // Now bring down the session by having mismatched families.
        Configure(config_tmpl22_bad);

        // Verify all OList elements on all agents.
        // There should be no connectivity between trees build by X and Y.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 1, "10.1.1.4", agent_ya_);
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_yb_->McastRouteCount());
};

static const char *config_tmpl23_bgp = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.101</identifier>\
        <address>127.0.0.101</address>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.102</identifier>\
        <address>127.0.0.102</address>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
</config>\
";

static const char *config_tmpl23_instance = "\
<config>\
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
// Don't create the instance during SetUp.
//
class BgpXmppMcast2ServerTest3 : public BgpXmppMcast2ServerTestBase {
protected:
    virtual void SetUp() {
        BgpXmppMcast2ServerTestBase::SetUp();

        Configure(config_tmpl23_bgp);
        task_util::WaitForIdle();

        BgpXmppMcast2ServerTestBase::SessionUp();
        BgpXmppMcast2ServerTestBase::Subscribe("blue", 1);
    }

    virtual void TearDown() {
        BgpXmppMcast2ServerTestBase::SessionDown();
        BgpXmppMcast2ServerTestBase::TearDown();
    }
};

TEST_F(BgpXmppMcast2ServerTest3, BgpConnectLater_SingleAgent) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());

    // Now create the instance.
    Configure(config_tmpl23_instance);

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(1, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_ya_->McastRouteCount());

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.4", agent_ya_);
    VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

    // Verify the labels used by all agents.
    VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
    VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());
};

TEST_F(BgpXmppMcast2ServerTest3, BgpConnectLater_MultipleAgent) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_yb_->McastRouteCount());

    // Now create the instance.
    Configure(config_tmpl23_instance);

    // Verify all OList elements on all agents.
    VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
    VerifyOListElem(agent_xb_, "blue", mroute, 2, "10.1.1.1", agent_xa_);
    VerifyOListElem(agent_xb_, "blue", mroute, 2, "10.1.1.5", agent_yb_);
    VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
    VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
    VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();

    // Verify number of routes on all agents.
    TASK_UTIL_EXPECT_EQ(0, agent_xa_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_xb_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_ya_->McastRouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_yb_->McastRouteCount());
};

static const char *config_tmpl3 = "\
<config>\
    <bgp-router name=\'X\'>\
        <identifier>192.168.0.101</identifier>\
        <address>127.0.0.101</address>\
        <autonomous-system>%d</autonomous-system>\
        <port>%d</port>\
        <session to=\'Y\'>\
            <address-families>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
        <session to=\'Z\'>\
            <address-families>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Y\'>\
        <identifier>192.168.0.102</identifier>\
        <address>127.0.0.102</address>\
        <autonomous-system>%d</autonomous-system>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
        <session to=\'Z\'>\
            <address-families>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
    </bgp-router>\
    <bgp-router name=\'Z\'>\
        <identifier>192.168.0.103</identifier>\
        <address>127.0.0.103</address>\
        <autonomous-system>%d</autonomous-system>\
        <port>%d</port>\
        <session to=\'X\'>\
            <address-families>\
                <family>erm-vpn</family>\
            </address-families>\
        </session>\
        <session to=\'Y\'>\
            <address-families>\
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

class BgpXmppMcast3ServerTestBase : public BgpXmppMcast2ServerTestBase {
protected:
    virtual void Configure(const char *config_tmpl, bool ebgp) {
        char config[8192];
        if (ebgp) {
            snprintf(config, sizeof(config), config_tmpl,
                64511, bs_x_->session_manager()->GetPort(),
                64512, bs_y_->session_manager()->GetPort(),
                64513, bs_z_->session_manager()->GetPort());
        } else {
            snprintf(config, sizeof(config), config_tmpl,
                64512, bs_x_->session_manager()->GetPort(),
                64512, bs_y_->session_manager()->GetPort(),
                64512, bs_z_->session_manager()->GetPort());
        }
        bs_x_->Configure(config);
        bs_y_->Configure(config);
        bs_z_->Configure(config);
    }

    virtual void SetUp() {
        bs_z_.reset(new BgpServerTest(&evm_, "Z"));
        xs_z_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);
        bs_z_->session_manager()->Initialize(0);
        xs_z_->Initialize(0, false);
        bcm_z_.reset(new BgpXmppChannelManager(xs_z_, bs_z_.get()));

        BgpXmppMcast2ServerTestBase::SetUp();
    }

    virtual void TearDown() {
        xs_z_->Shutdown();
        task_util::WaitForIdle();
        bs_z_->Shutdown();
        task_util::WaitForIdle();
        bcm_z_.reset();
        TcpServerManager::DeleteServer(xs_z_);
        xs_z_ = NULL;

        agent_za_->Delete();
        agent_zb_->Delete();
        agent_zc_->Delete();

        BgpXmppMcast2ServerTestBase::TearDown();
    }

    virtual void SessionUp() {
        BgpXmppMcast2ServerTestBase::SessionUp();

        agent_za_.reset(new test::NetworkAgentMock(
            &evm_, "agent-za", xs_z_->GetPort(), "127.0.0.7", "127.0.0.103"));
        TASK_UTIL_EXPECT_TRUE(agent_za_->IsEstablished());
        agent_zb_.reset(new test::NetworkAgentMock(
            &evm_, "agent-zb", xs_z_->GetPort(), "127.0.0.8", "127.0.0.103"));
        TASK_UTIL_EXPECT_TRUE(agent_zb_->IsEstablished());
        agent_zc_.reset(new test::NetworkAgentMock(
            &evm_, "agent-zc", xs_z_->GetPort(), "127.0.0.9", "127.0.0.103"));
        TASK_UTIL_EXPECT_TRUE(agent_zc_->IsEstablished());
    }

    virtual void SessionDown() {
        BgpXmppMcast2ServerTestBase::SessionDown();

        agent_za_->SessionDown();
        agent_zb_->SessionDown();
        agent_zc_->SessionDown();
        task_util::WaitForIdle();
    }

    virtual void Subscribe(const string net, int id) {
        BgpXmppMcast2ServerTestBase::Subscribe(net, id);

        agent_za_->McastSubscribe(net, id);
        agent_zb_->McastSubscribe(net, id);
        agent_zc_->McastSubscribe(net, id);
        task_util::WaitForIdle();
    }

    BgpServerTestPtr bs_z_;
    XmppServer *xs_z_;
    boost::scoped_ptr<BgpXmppChannelManager> bcm_z_;
    boost::shared_ptr<test::NetworkAgentMock> agent_za_;
    boost::shared_ptr<test::NetworkAgentMock> agent_zb_;
    boost::shared_ptr<test::NetworkAgentMock> agent_zc_;
};

// Parameterize iBGP vs eBGP.

class BgpXmppMcast3ServerParamTest :
    public BgpXmppMcast3ServerTestBase,
    public ::testing::WithParamInterface<bool> {
protected:
    virtual void SetUp() {
        BgpXmppMcast3ServerTestBase::SetUp();

        Configure(config_tmpl3, GetParam());
        task_util::WaitForIdle();

        BgpXmppMcast3ServerTestBase::SessionUp();
        BgpXmppMcast3ServerTestBase::Subscribe("blue", 1);
    }

    virtual void TearDown() {
        BgpXmppMcast3ServerTestBase::SessionDown();
        BgpXmppMcast3ServerTestBase::TearDown();
    }
};

//
// Tree builder flaps the route.
//
TEST_P(BgpXmppMcast3ServerParamTest, SingleAgentRouteFlapping1) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for agents xa, ya and za.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

        // Verify the labels used by all agents.
        VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
        VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);
        VerifyLabel(agent_za_, "blue", mroute, 70000, 79999);

        // Delete mcast route for agent xa.
        agent_xa_->DeleteMcastRoute("blue", mroute);
        task_util::WaitForIdle();

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 0);
        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.4", agent_ya_);

        // Verify the labels used by agent ya and za.
        VerifyLabel(agent_xa_, "blue", mroute);
        VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);
        VerifyLabel(agent_za_, "blue", mroute, 70000, 79999);

        // Add mcast route for agent xa.
        agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_za_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

//
// Non tree builder flaps the route.
//
TEST_P(BgpXmppMcast3ServerParamTest, SingleAgentRouteFlapping2) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for agents xa, ya and za.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

        // Verify the labels used by all agents.
        VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
        VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);
        VerifyLabel(agent_za_, "blue", mroute, 70000, 79999);

        // Delete mcast route for agent za.
        agent_za_->DeleteMcastRoute("blue", mroute);
        task_util::WaitForIdle();

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_za_, "blue", mroute, 0);
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.1", agent_xa_);

        // Verify the labels used by agent xa and ya.
        VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);
        VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);
        VerifyLabel(agent_za_, "blue", mroute);

        // Add mcast route for agent za.
        agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_za_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

//
// Forest node on tree builder flaps the route.
//
TEST_P(BgpXmppMcast3ServerParamTest, MultipleAgentRouteFlapping1) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
    agent_zb_->AddMcastRoute("blue", mroute, "10.1.1.8", "80000-89999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {
        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Delete mcast route for agent xb.
        agent_xb_->DeleteMcastRoute("blue", mroute);
        task_util::WaitForIdle();

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xb_, "blue", mroute, 0);
        VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xa_, "blue", mroute, 2, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);

        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);

        // Add mcast route for agent xb.
        agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_za_->DeleteMcastRoute("blue", mroute);
    agent_zb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

//
// Non-forest node on tree builder flaps the route.
//
TEST_P(BgpXmppMcast3ServerParamTest, MultipleAgentRouteFlapping2) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
    agent_zb_->AddMcastRoute("blue", mroute, "10.1.1.8", "80000-89999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {
        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Delete mcast route for agent xa.
        agent_xa_->DeleteMcastRoute("blue", mroute);
        task_util::WaitForIdle();

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 0);
        VerifyOListElem(agent_xb_, "blue", mroute, 2, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 2, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Add mcast route for agent xa.
        agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_za_->DeleteMcastRoute("blue", mroute);
    agent_zb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

//
// Forest node on non tree builder flaps the route.
//
TEST_P(BgpXmppMcast3ServerParamTest, MultipleAgentRouteFlapping3) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
    agent_zb_->AddMcastRoute("blue", mroute, "10.1.1.8", "80000-89999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {
        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Delete mcast route for agent yb.
        agent_yb_->DeleteMcastRoute("blue", mroute);
        task_util::WaitForIdle();

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 0);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Add mcast route for agent yb.
        agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_za_->DeleteMcastRoute("blue", mroute);
    agent_zb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

//
// Non-forest node on non tree builder flaps the route.
//
TEST_P(BgpXmppMcast3ServerParamTest, MultipleAgentRouteFlapping4) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
    agent_zb_->AddMcastRoute("blue", mroute, "10.1.1.8", "80000-89999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {
        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Delete mcast route for agent za.
        agent_za_->DeleteMcastRoute("blue", mroute);
        task_util::WaitForIdle();

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_za_, "blue", mroute, 0);
        VerifyOListElem(agent_zb_, "blue", mroute, 1, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Add mcast route for agent za.
        agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_za_->DeleteMcastRoute("blue", mroute);
    agent_zb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

//
// Forest node on tree builder changes nexthop.
//
TEST_P(BgpXmppMcast3ServerParamTest, MultipleAgentNexthopChange1) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
    agent_zb_->AddMcastRoute("blue", mroute, "10.1.1.8", "80000-89999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {
        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent xb.
        agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.102", "20000-29999");
        task_util::WaitForIdle();

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.102", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.102", agent_xb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);

        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.102", agent_xb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);

        // Change mcast route for agent xb.
        agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_za_->DeleteMcastRoute("blue", mroute);
    agent_zb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

//
// Non-forest node on tree builder changes nexthop.
//
TEST_P(BgpXmppMcast3ServerParamTest, MultipleAgentNexthopChange2) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
    agent_zb_->AddMcastRoute("blue", mroute, "10.1.1.8", "80000-89999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {
        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent xa.
        agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.101", "10000-19999");
        task_util::WaitForIdle();

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.101", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent xa.
        agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_za_->DeleteMcastRoute("blue", mroute);
    agent_zb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

//
// Forest node on non tree builder changes nexthop.
//
TEST_P(BgpXmppMcast3ServerParamTest, MultipleAgentNexthopChange3) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
    agent_zb_->AddMcastRoute("blue", mroute, "10.1.1.8", "80000-89999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {
        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent yb.
        agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.105", "50000-59999");
        task_util::WaitForIdle();

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.105", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.105", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent yb.
        agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_za_->DeleteMcastRoute("blue", mroute);
    agent_zb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

//
// Non-forest node on non tree builder changes nexthop.
//
TEST_P(BgpXmppMcast3ServerParamTest, MultipleAgentNexthopChange4) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
    agent_zb_->AddMcastRoute("blue", mroute, "10.1.1.8", "80000-89999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {
        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent xb.
        agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.104", "40000-49999");
        task_util::WaitForIdle();

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.104", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);

        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent xb.
        agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_za_->DeleteMcastRoute("blue", mroute);
    agent_zb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

//
// Forest node on tree builder changes label block.
//
TEST_P(BgpXmppMcast3ServerParamTest, MultipleAgentLabelBlockChange1) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
    agent_zb_->AddMcastRoute("blue", mroute, "10.1.1.8", "80000-89999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {

        // Verify label used by agent xb.
        VerifyLabel(agent_xb_, "blue", mroute, 20000, 29999);

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent xb.
        agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "120000-129999");
        task_util::WaitForIdle();

        // Verify label used by agent xb.
        VerifyLabel(agent_xb_, "blue", mroute, 120000, 129999);

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent xb.
        agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_za_->DeleteMcastRoute("blue", mroute);
    agent_zb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

//
// Non-forest node on tree builder changes label block.
//
TEST_P(BgpXmppMcast3ServerParamTest, MultipleAgentLabelBlockChange2) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
    agent_zb_->AddMcastRoute("blue", mroute, "10.1.1.8", "80000-89999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {

        // Verify label used by agent xa.
        VerifyLabel(agent_xa_, "blue", mroute, 10000, 19999);

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent xb.
        agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "110000-119999");
        task_util::WaitForIdle();

        // Verify label used by agent xa.
        VerifyLabel(agent_xa_, "blue", mroute, 110000, 119999);

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent xb.
        agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_za_->DeleteMcastRoute("blue", mroute);
    agent_zb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

//
// Forest node on non tree builder changes label block.
//
TEST_P(BgpXmppMcast3ServerParamTest, MultipleAgentLabelBlockChange3) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
    agent_zb_->AddMcastRoute("blue", mroute, "10.1.1.8", "80000-89999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {

        // Verify label used by agent yb.
        VerifyLabel(agent_yb_, "blue", mroute, 50000, 59999);

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent yb.
        agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "150000-159999");
        task_util::WaitForIdle();

        // Verify label used by agent yb.
        VerifyLabel(agent_yb_, "blue", mroute, 150000, 159999);

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent yb.
        agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_za_->DeleteMcastRoute("blue", mroute);
    agent_zb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

//
// Non-forest node on non tree builder changes label block.
//
TEST_P(BgpXmppMcast3ServerParamTest, MultipleAgentLabelBlockChange4) {
    const char *mroute = "225.0.0.1,0.0.0.0";

    // Add mcast route for all agents.
    agent_xa_->AddMcastRoute("blue", mroute, "10.1.1.1", "10000-19999");
    agent_xb_->AddMcastRoute("blue", mroute, "10.1.1.2", "20000-29999");
    agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
    agent_yb_->AddMcastRoute("blue", mroute, "10.1.1.5", "50000-59999");
    agent_za_->AddMcastRoute("blue", mroute, "10.1.1.7", "70000-79999");
    agent_zb_->AddMcastRoute("blue", mroute, "10.1.1.8", "80000-89999");
    task_util::WaitForIdle();

    for (int idx = 0; idx < 3; ++idx) {

        // Verify label used by agent ya.
        VerifyLabel(agent_ya_, "blue", mroute, 40000, 49999);

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent ya.
        agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "140000-149999");
        task_util::WaitForIdle();

        // Verify label used by agent ya.
        VerifyLabel(agent_ya_, "blue", mroute, 140000, 149999);

        // Verify all OList elements on all agents.
        VerifyOListElem(agent_xa_, "blue", mroute, 1, "10.1.1.2", agent_xb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.1", agent_xa_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_xb_, "blue", mroute, 3, "10.1.1.8", agent_zb_);

        VerifyOListElem(agent_ya_, "blue", mroute, 1, "10.1.1.5", agent_yb_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.4", agent_ya_);
        VerifyOListElem(agent_yb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        VerifyOListElem(agent_za_, "blue", mroute, 1, "10.1.1.8", agent_zb_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.7", agent_za_);
        VerifyOListElem(agent_zb_, "blue", mroute, 2, "10.1.1.2", agent_xb_);

        // Change mcast route for agent ya.
        agent_ya_->AddMcastRoute("blue", mroute, "10.1.1.4", "40000-49999");
        task_util::WaitForIdle();
    }

    // Delete mcast route for all agents.
    agent_xa_->DeleteMcastRoute("blue", mroute);
    agent_xb_->DeleteMcastRoute("blue", mroute);
    agent_ya_->DeleteMcastRoute("blue", mroute);
    agent_yb_->DeleteMcastRoute("blue", mroute);
    agent_za_->DeleteMcastRoute("blue", mroute);
    agent_zb_->DeleteMcastRoute("blue", mroute);
    task_util::WaitForIdle();
};

INSTANTIATE_TEST_CASE_P(Instance, BgpXmppMcast3ServerParamTest,
    ::testing::Bool());

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
