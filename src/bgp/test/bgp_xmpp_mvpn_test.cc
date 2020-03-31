/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>

#include "bgp/bgp_factory.h"
#include "bgp/bgp_mvpn.h"
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

    static void ValidateShowManagerMvpnDetailResponse(Sandesh *sandesh,
        vector<string> &result) {
        ShowMvpnManagerDetailResp *resp =
            dynamic_cast<ShowMvpnManagerDetailResp *>(sandesh);
        EXPECT_NE((ShowMvpnManagerDetailResp *)NULL, resp);

        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_neighbors().size());
        cout << "*******************************************************"<<endl;
        for (size_t i = 0; i < resp->get_neighbors().size(); ++i) {
            cout << resp->get_neighbors()[i].log() << endl;
            bool found = false;
            BOOST_FOREACH(const string &rd, result) {
                if (resp->get_neighbors()[i].get_rd() == rd) {
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found);
        }
        cout << "*******************************************************"<<endl;
        validate_done_ = 1;
    }

    static void ValidateShowProjectManagerMvpnDetailResponse(Sandesh *sandesh,
        vector<string> &result) {
        ShowMvpnProjectManagerDetailResp *resp =
            dynamic_cast<ShowMvpnProjectManagerDetailResp *>(sandesh);
        EXPECT_NE((ShowMvpnProjectManagerDetailResp *)NULL, resp);

        TASK_UTIL_EXPECT_EQ(result.size(), resp->get_states().size());
        cout << "*******************************************************"<<endl;
        for (size_t i = 0; i < resp->get_states().size(); ++i) {
            cout << resp->get_states()[i].log() << endl;
            bool found = false;
            BOOST_FOREACH(const string &group, result) {
                if (resp->get_states()[i].get_group() == group) {
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found);
        }
        cout << "*******************************************************"<<endl;
        validate_done_ = 1;
    }

    void AddMvpnRoute(BgpTable *table, const string &prefix_str,
                      const string &target, bool add_leaf_req = false) {
        MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
        DBRequest add_req;
        add_req.key.reset(new MvpnTable::RequestKey(prefix, NULL));

        BgpAttrSpec attr_spec;
        ExtCommunitySpec *commspec(new ExtCommunitySpec());
        RouteTarget tgt = RouteTarget::FromString(target);
        commspec->communities.push_back(tgt.GetExtCommunityValue());
        attr_spec.push_back(commspec);

    if (add_leaf_req) {
            PmsiTunnelSpec *pmsi_spec(new PmsiTunnelSpec());
            pmsi_spec->tunnel_flags = PmsiTunnelSpec::LeafInfoRequired;
            attr_spec.push_back(pmsi_spec);
    }

        BgpAttrPtr attr = bs_x_->attr_db()->Locate(attr_spec);
        STLDeleteValues(&attr_spec);
        add_req.data.reset(new MvpnTable::RequestData(attr, 0, 20));
        add_req.oper = DBRequest::DB_ENTRY_ADD_CHANGE;
        table->Enqueue(&add_req);
        task_util::WaitForIdle();
    }

    void DeleteMvpnRoute(BgpTable *table, const string &prefix_str) {
        DBRequest delete_req;
        MvpnPrefix prefix(MvpnPrefix::FromString(prefix_str));
        delete_req.key.reset(new MvpnTable::RequestKey(prefix, NULL));
        delete_req.oper = DBRequest::DB_ENTRY_DELETE;
        table->Enqueue(&delete_req);
        task_util::WaitForIdle();
    }

    virtual void SetUp() {
        bs_x_.reset(new BgpServerTest(&evm_, "X"));
        bs_x_->set_mvpn_ipv4_enable(true);
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
        agent_xa_->McastSubscribe(BgpConfigManager::kFabricInstance, 1000);
        agent_xb_->McastSubscribe(BgpConfigManager::kFabricInstance, 1000);
        agent_xc_->McastSubscribe(BgpConfigManager::kFabricInstance, 1000);
        task_util::WaitForIdle();
    }

    virtual void Configure(const char *config_tmpl) {
        char config[8192];
        snprintf(config, sizeof(config), config_tmpl,
            bs_x_->session_manager()->GetPort());
        bs_x_->Configure(config);
    }

    BgpTable *GetVrfTable(BgpServerTestPtr server, const string &name,
                          Address::Family family = Address::MVPN) {
        RoutingInstanceMgr *rim = server->routing_instance_mgr();
        TASK_UTIL_EXPECT_TRUE(rim->GetRoutingInstance(name) != NULL);
        RoutingInstance *rtinstance = rim->GetRoutingInstance(name);
        TASK_UTIL_EXPECT_TRUE(rtinstance->GetTable(family) != NULL);
        BgpTable *table = rtinstance->GetTable(family);
        return table;
    }

    const BgpTable *GetVrfTable(BgpServerTestPtr server,
                                const string &name,
                                Address::Family family = Address::MVPN) const {
        RoutingInstanceMgr *rim = server->routing_instance_mgr();
        TASK_UTIL_EXPECT_TRUE(rim->GetRoutingInstance(name) != NULL);
        const RoutingInstance *rtinstance = rim->GetRoutingInstance(name);
        TASK_UTIL_EXPECT_TRUE(rtinstance->GetTable(family) != NULL);
        const BgpTable *table = rtinstance->GetTable(family);
        return table;
    }

    size_t GetVrfTableSize(BgpServerTestPtr server, const string &name,
                           Address::Family family = Address::MVPN) const {
        return GetVrfTable(server, name, family)->Size();
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
    <virtual-network name='default-domain:default-project:ip-fabric'>\
        <network-id>1000</network-id>\
    </virtual-network>\
    <routing-instance name='default-domain:default-project:ip-fabric:ip-fabric'>\
        <virtual-network>blue</virtual-network>\
        <vrf-target>target:1:100</vrf-target>\
    </routing-instance>\
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
    agent_xa_->AddType7MvpnRoute("blue", "225.0.0,90.1.1.1");
    TASK_UTIL_EXPECT_EQ(1, GetVrfTableSize(bs_x_, "blue"));
}

TEST_F(BgpXmppMvpnErrorTest, BadSourceAddress) {
    agent_xa_->AddType7MvpnRoute("blue", "225.0.0.1,90.1.1");
    TASK_UTIL_EXPECT_EQ(1, GetVrfTableSize(bs_x_, "blue"));
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

TEST_F(BgpXmppMvpnSubscriptionTest, PendingSubscribeType5) {
    const char *mroute = "225.0.0.1,20.1.1.10";

    // Register agent a to the multicast table and add a mvpn route of type 5
    // without waiting for the subscription to be processed.
    agent_xa_->MvpnSubscribe("blue", 1);
    agent_xa_->MvpnSubscribe(BgpConfigManager::kFabricInstance, 1000);
    agent_xa_->AddType5MvpnRoute("blue", mroute, "20.1.1.11");

    // Verify that the route gets added
    TASK_UTIL_EXPECT_EQ(2, GetVrfTableSize(bs_x_, "blue"));

    // Add the route again, there should still be only 1 route
    agent_xa_->AddType5MvpnRoute("blue", mroute, "20.1.1.11");
    TASK_UTIL_EXPECT_EQ(2, GetVrfTableSize(bs_x_, "blue"));

    // Add another route, there should be 2 routes
    const char *mroute2 = "225.0.0.1,20.1.1.20";
    agent_xa_->AddType5MvpnRoute("blue", mroute2, "20.1.1.12");
    TASK_UTIL_EXPECT_EQ(3, GetVrfTableSize(bs_x_, "blue"));

    // Delete one mvpn route, there should still be a route
    agent_xa_->DeleteMvpnRoute("blue", mroute2, MvpnPrefix::SourceActiveADRoute);
    TASK_UTIL_EXPECT_EQ(2, GetVrfTableSize(bs_x_, "blue"));

    // Delete second route, it should get deleted
    agent_xa_->DeleteMvpnRoute("blue", mroute, MvpnPrefix::SourceActiveADRoute);
    TASK_UTIL_EXPECT_EQ(1, GetVrfTableSize(bs_x_, "blue"));
}

TEST_F(BgpXmppMvpnSubscriptionTest, PendingSubscribeType7) {
    const char *mroute = "225.0.0.1,20.1.1.10";

    // Register agent a to the multicast table and add a mvpn route of type 7
    // without waiting for the subscription to be processed.
    agent_xa_->MvpnSubscribe("blue", 1);
    agent_xa_->MvpnSubscribe(BgpConfigManager::kFabricInstance, 1000);
    agent_xa_->AddType5MvpnRoute("blue", mroute, "20.1.1.11");

    // Verify that the route gets added
    TASK_UTIL_EXPECT_EQ(2, GetVrfTableSize(bs_x_, "blue"));

    // Add the route again, there should still be only 1 route
    agent_xb_->MvpnSubscribe("blue", 1);
    agent_xb_->MvpnSubscribe(BgpConfigManager::kFabricInstance, 1000);
    agent_xb_->AddType5MvpnRoute("blue", mroute, "20.1.1.12");
    agent_xa_->AddType5MvpnRoute("blue", mroute, "20.1.1.11");
    TASK_UTIL_EXPECT_EQ(2, GetVrfTableSize(bs_x_, "blue"));

    // Delete mvpn route from one agent, there should still be a route
    agent_xa_->DeleteMvpnRoute("blue", mroute, MvpnPrefix::SourceActiveADRoute);
    TASK_UTIL_EXPECT_EQ(2, GetVrfTableSize(bs_x_, "blue"));

    // Delete route from second agent, it should get deleted
    agent_xb_->DeleteMvpnRoute("blue", mroute, MvpnPrefix::SourceActiveADRoute);
    // Delete route from second agent, it should get deleted
    TASK_UTIL_EXPECT_EQ(1, GetVrfTableSize(bs_x_, "blue"));
}

TEST_F(BgpXmppMvpnSubscriptionTest, PendingUnsubscribe) {
    const char *mroute = "225.0.0.1,10.1.1.10";

    // Register agent a to the multicast table and add a mvpn route
    // without waiting for the subscription to be processed. Then go
    // ahead and unsubscribe right away.
    agent_xa_->MvpnSubscribe("blue", 1);
    agent_xa_->MvpnSubscribe(BgpConfigManager::kFabricInstance, 1000);
    agent_xa_->AddType7MvpnRoute("blue", mroute);
    agent_xa_->MvpnUnsubscribe("blue");
    agent_xa_->MvpnUnsubscribe(BgpConfigManager::kFabricInstance);

    // Verify number of routes.
    TASK_UTIL_EXPECT_EQ(1, GetVrfTableSize(bs_x_, "blue"));
}

TEST_F(BgpXmppMvpnSubscriptionTest, SubsequentSubscribeUnsubscribe) {
    const char *mroute = "225.0.0.1,10.1.1.10";

    // Register agent b to the multicast table and add a mvpn route
    // after waiting for the subscription to be processed.
    agent_xb_->MvpnSubscribe("blue", 1);
    agent_xb_->MvpnSubscribe(BgpConfigManager::kFabricInstance, 1000);
    task_util::WaitForIdle();
    agent_xb_->AddType7MvpnRoute("blue", mroute);
    MvpnTable *blue_table_ = static_cast<MvpnTable *>(
        bs_x_->database()->FindTable("blue.mvpn.0"));

    // Register agent a to the multicast table and add a mvpn route
    // without waiting for the subscription to be processed. Then go
    // ahead and unsubscribe right away. Then subscribe again with a
    // different id and add the route again.
    agent_xa_->MvpnSubscribe("blue", 1);
    agent_xa_->MvpnSubscribe(BgpConfigManager::kFabricInstance, 1000);
    agent_xa_->AddType7MvpnRoute("blue", mroute);
    agent_xa_->MvpnUnsubscribe("blue");
    agent_xa_->MvpnUnsubscribe(BgpConfigManager::kFabricInstance);
    agent_xa_->MvpnSubscribe("blue", 2);
    agent_xa_->MvpnSubscribe(BgpConfigManager::kFabricInstance, 1000);
    agent_xa_->AddType7MvpnRoute("blue", mroute);

    // Verify number of routes in blue table.
    TASK_UTIL_EXPECT_EQ(2, GetVrfTableSize(bs_x_, "blue"));

    // Verify that agent a mvpn route was added.
    const char *route = "7-0:0,0,10.1.1.10,225.0.0.1";
    MvpnPrefix prefix(MvpnPrefix::FromString(route));
    MvpnTable::RequestKey key(prefix, NULL);
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
        agent_xa_->AddType7MvpnRoute("blue", mroute);
        agent_xb_->AddType7MvpnRoute("blue", mroute);
        task_util::WaitForIdle();
    }

    // Verify that all routes are added once.
    TASK_UTIL_EXPECT_EQ(sizeof(mroute_list)/sizeof(mroute_list[0]) + 1,
                        GetVrfTableSize(bs_x_, "blue"));

    // Delete mvpn route for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->DeleteMvpnRoute("blue", mroute);
        agent_xb_->DeleteMvpnRoute("blue", mroute);
        task_util::WaitForIdle();
    }
};

TEST_F(BgpXmppMvpnMultiAgentTest, ValidateShowRoute) {
    const char *mroute_list[] = {
        "225.0.0.1,90.1.1.1",
    };

    // Add mvpn routes for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->AddType7MvpnRoute("blue", mroute);
        agent_xb_->AddType7MvpnRoute("blue", mroute);
        task_util::WaitForIdle();
    }

    // Verify that all routes are added once.
    TASK_UTIL_EXPECT_EQ(sizeof(mroute_list)/sizeof(mroute_list[0]) + 1,
                        GetVrfTableSize(bs_x_, "blue"));
    TASK_UTIL_EXPECT_EQ(4, GetVrfTableSize(bs_x_,
                        BgpConfigManager::kFabricInstance, Address::ERMVPN));

    // Verify routes via sandesh.
    BgpSandeshContext sandesh_context;
    RegisterSandeshShowXmppExtensions(&sandesh_context);
    sandesh_context.bgp_server = bs_x_.get();
    sandesh_context.xmpp_peer_manager = bcm_x_.get();
    Sandesh::set_client_context(&sandesh_context);

    // First get all tables.
    // blue.mvpn.0, bgp.ermvpn.0, bgp.mvpn.0, bgp.rtarget.0,
    // default-domain:default-project:ip-fabric:ip-fabric.ermvpn.0,
    // default-domain:default-project:ip-fabric:ip-fabric.mvpn.0
    std::vector<size_t> result = {2, 2, 2, 6, 4, 1};
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteResponse, _1, result));
    ShowRouteReq *show_req = new ShowRouteReq;
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);

    // Now get blue.mvpn.0.
    result = list_of(2).convert_to_container<vector<size_t> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteResponse, _1, result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.mvpn.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);

    // Now get bgp.mvpn.0.
    result = list_of(2).convert_to_container<vector<size_t> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteResponse, _1, result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("bgp.mvpn.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);

    // Delete mvpn route for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->DeleteMvpnRoute("blue", mroute);
        agent_xb_->DeleteMvpnRoute("blue", mroute);
        task_util::WaitForIdle();
    }

    // Verify that all routes are deleted.
    TASK_UTIL_EXPECT_EQ(1, GetVrfTableSize(bs_x_, "blue"));

    // Get blue.mvpn.0 again.
    result = list_of(1).convert_to_container<vector<size_t> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteResponse, _1, result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.mvpn.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);

    // Get bgp.mvpn.0 again.
    result = list_of(2).convert_to_container<vector<size_t> >();
    Sandesh::set_response_callback(
        boost::bind(ValidateShowRouteResponse, _1, result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("bgp.mvpn.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);
};

TEST_F(BgpXmppMvpnMultiAgentTest, ValidateShowMvpnManagerDetail) {
    // Inject Type-1 AD Route directly into the bgp.mvpn.0 table.
    string prefix = "1-10.1.1.1:65535,9.8.7.6";
    BgpTable *master = GetVrfTable(bs_x_, BgpConfigManager::kMasterInstance);
    BgpTable *blue = GetVrfTable(bs_x_, "blue");
    AddMvpnRoute(master, prefix, "target:1:1");

    // Verify that all routes are added once.
    TASK_UTIL_EXPECT_EQ(2, blue->Size());

    // Verify multicast manager detail via sandesh.
    BgpSandeshContext sandesh_context;
    RegisterSandeshShowXmppExtensions(&sandesh_context);
    sandesh_context.bgp_server = bs_x_.get();
    sandesh_context.xmpp_peer_manager = bcm_x_.get();
    Sandesh::set_client_context(&sandesh_context);
    vector<string> result = {"10.1.1.1:65535"};
    Sandesh::set_response_callback(
        boost::bind(ValidateShowManagerMvpnDetailResponse, _1, result));
    ShowMvpnManagerDetailReq *show_req = new ShowMvpnManagerDetailReq;
    validate_done_ = 0;
    show_req->set_name("blue.mvpn.0");
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);

    DeleteMvpnRoute(master, prefix);
    TASK_UTIL_EXPECT_EQ(1, blue->Size());
    TASK_UTIL_EXPECT_EQ(2, master->Size());

    // Get blue.mvpn.0 again.
    result.resize(0);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowManagerMvpnDetailResponse, _1, result));
    show_req = new ShowMvpnManagerDetailReq;
    validate_done_ = 0;
    show_req->set_name("blue.mvpn.0");
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);
};

TEST_F(BgpXmppMvpnMultiAgentTest, ValidateShowMvpnProjectManagerDetail) {
    BgpTable *master = GetVrfTable(bs_x_, BgpConfigManager::kMasterInstance);
    BgpTable *blue = GetVrfTable(bs_x_, "blue");

    const char *mroute_list[] = {
        "225.0.0.1,9.8.7.6",
    };

    // Add mvpn routes for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->AddType7MvpnRoute("blue", mroute);
        agent_xb_->AddType7MvpnRoute("blue", mroute);
        task_util::WaitForIdle();
    }

    string prefix = "3-10.1.1.1:65535,9.8.7.6,225.0.0.1,192.168.1.1";
    AddMvpnRoute(master, prefix, "target:1:1", true);

    // Verify that all routes are added once.
    TASK_UTIL_EXPECT_EQ(sizeof(mroute_list)/sizeof(mroute_list[0]) + 2,
                        GetVrfTableSize(bs_x_, "blue"));

    // Verify that all routes are added once.
    TASK_UTIL_EXPECT_EQ(3, master->Size());
    TASK_UTIL_EXPECT_EQ(3, blue->Size());

    // Verify multicast manager detail via sandesh.
    BgpSandeshContext sandesh_context;
    RegisterSandeshShowXmppExtensions(&sandesh_context);
    sandesh_context.bgp_server = bs_x_.get();
    sandesh_context.xmpp_peer_manager = bcm_x_.get();
    Sandesh::set_client_context(&sandesh_context);
    vector<string> result = {"225.0.0.1"};
    Sandesh::set_response_callback(
        boost::bind(ValidateShowProjectManagerMvpnDetailResponse, _1, result));
    ShowMvpnProjectManagerDetailReq *show_req =
        new ShowMvpnProjectManagerDetailReq;
    validate_done_ = 0;
    show_req->set_name(string(BgpConfigManager::kFabricInstance) + ".ermvpn.0");
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);

    // Add mvpn routes for all agents.
    BOOST_FOREACH(const char *mroute, mroute_list) {
        agent_xa_->DeleteMvpnRoute("blue", mroute);
        agent_xb_->DeleteMvpnRoute("blue", mroute);
        task_util::WaitForIdle();
    }
    DeleteMvpnRoute(master, prefix);

    // Verify that only type-1 ad route remains.
    TASK_UTIL_EXPECT_EQ(1, blue->Size());
    TASK_UTIL_EXPECT_EQ(2, master->Size());

    result.resize(0);
    Sandesh::set_response_callback(
        boost::bind(ValidateShowManagerMvpnDetailResponse, _1, result));
    ShowMvpnManagerDetailReq *show_pm_req = new ShowMvpnManagerDetailReq;
    validate_done_ = 0;
    show_pm_req->set_name(
        string(BgpConfigManager::kFabricInstance) + ".ermvpn.0");
    show_pm_req->HandleRequest();
    show_pm_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);
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
