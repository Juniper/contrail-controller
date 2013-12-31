/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <unistd.h>
#include <fstream>
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"

using namespace boost::assign;

#include <pugixml/pugixml.hpp>

#include "base/util.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_attr.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_proto.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/inet/inet_table.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/test/bgp_server_test_util.h"
#include "io/test/event_manager_test.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"


#include "schema/xmpp_unicast_types.h"

#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_state_machine.h"

#include "xml/xml_pugi.h"

#include "testing/gunit.h"

using namespace boost::asio;
using namespace std;

#define SUB_ADDR "agent@vnsw.contrailsystems.com" 

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
        BgpXmppChannelManager(x, b), count(0), channel_(NULL) { }

    virtual void XmppHandleChannelEvent(XmppChannel *channel,
                                        xmps::PeerState state) {
         count++;
         std::cout << "\n\n XmppHandleChannelEvent:" << state << "count:" << count;
         std::cout << "\n\n";

         BgpXmppChannelManager::XmppHandleChannelEvent(channel, state);
    }

    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel) {
        channel_ = new BgpXmppChannelMock(channel, bgp_server_, this);
        return channel_;
    }

    int Count() {
        return count;
    }
    int count;
    BgpXmppChannelMock *channel_;
};


class BgpXmppUnitTest : public ::testing::Test {
public:
    bool PeerRegistered(BgpXmppChannel *channel, std::string instance_name, 
                        int instance_id) {
        RoutingInstanceMgr *instance_mgr = a_->routing_instance_mgr();
        RoutingInstance *rt_instance =
            instance_mgr->GetRoutingInstance(instance_name);
        EXPECT_FALSE(rt_instance == NULL);
        BgpTable *table = rt_instance->GetTable(Address::INET);
        IPeerRib *rib = a_->membership_mgr()->IPeerRibFind(channel->Peer(), table);
        if (rib) {
            if (rib->instance_id() == instance_id) return true;
        }
        return false;
    }

    bool PeerNotRegistered(BgpXmppChannel *channel, std::string instance_name) {
        RoutingInstanceMgr *instance_mgr = a_->routing_instance_mgr();
        RoutingInstance *rt_instance =
            instance_mgr->GetRoutingInstance(instance_name);
        EXPECT_FALSE(rt_instance == NULL);
        BgpTable *table = rt_instance->GetTable(Address::INET);
        IPeerRib *rib = a_->membership_mgr()->IPeerRibFind(channel->Peer(), table);
        if (rib) {
            return false;
        }
        return true;
    }



protected:
    static const char *config_tmpl;

    BgpXmppUnitTest() : thread_(&evm_) { }

    string FileRead(const string &filename) {
        ifstream file(filename.c_str());
        string content((istreambuf_iterator<char>(file)),
                       istreambuf_iterator<char>());
        return content;
    }

    virtual void SetUp() {
        a_.reset(new BgpServerTest(&evm_, "A"));
        xs_a_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);

        a_->session_manager()->Initialize(0);
        BGP_DEBUG_UT("Created server at port: " << 
                a_->session_manager()->GetPort());
        xs_a_->Initialize(0, false);

        bgp_channel_manager_.reset(
            new BgpXmppChannelManagerMock(xs_a_, a_.get()));

        thread_.Start();
    }

    virtual void TearDown() {
        agent_a_->SessionDown();
        task_util::WaitForIdle();
        xs_a_->Shutdown();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(0, xs_a_->ConnectionsCount());
        agent_a_->Delete();
        bgp_channel_manager_.reset();
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(xs_a_);
        task_util::WaitForIdle();
        a_->Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }

    void Configure() {
        char config[4096];
        snprintf(config, sizeof(config), config_tmpl,
                 a_->session_manager()->GetPort());
        a_->Configure(config);
    }

    XmppChannelConfig *CreateXmppChannelCfg(const char *address, int port,
                                            const string &from, 
                                            const string &to,
                                            bool isClient) {
        XmppChannelConfig *cfg = new XmppChannelConfig(isClient);
        boost::system::error_code ec;
        cfg->endpoint.address(ip::address::from_string(address, ec));
        cfg->endpoint.port(port);
        cfg->ToAddr = to;
        cfg->FromAddr = from;
        if (!isClient) cfg->NodeAddr = test::XmppDocumentMock::kControlNodeJID;
        return cfg;
    }

    static void ValidateShowRouteResponse(Sandesh *sandesh, vector<int> &result) {
        ShowRouteResp *resp = dynamic_cast<ShowRouteResp *>(sandesh);
        EXPECT_NE((ShowRouteResp *)NULL, resp);

        EXPECT_EQ(result.size(), resp->get_tables().size());

        cout << "*******************************************************"<<endl;
        for (size_t i = 0; i < resp->get_tables().size(); i++) {
            EXPECT_EQ(result[i], resp->get_tables()[i].routes.size());
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

    EventManager evm_;
    ServerThread thread_;
    auto_ptr<BgpServerTest> a_;
    XmppServer *xs_a_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> bgp_channel_manager_;

    static int validate_done_;
};

class BgpXmppSerializeMembershipReqTest : public BgpXmppUnitTest {
    virtual void SetUp() {
        a_.reset(new BgpServerTest(&evm_, "A"));
        xs_a_ = new XmppServer(&evm_, test::XmppDocumentMock::kControlNodeJID);

        a_->session_manager()->Initialize(0);
        BGP_DEBUG_UT("Created server at port: " << 
                     a_->session_manager()->GetPort());
        xs_a_->Initialize(0, false);

        bgp_channel_manager_.reset(new BgpXmppChannelManagerMock(xs_a_, 
                                                                 a_.get()));

        thread_.Start();

        Configure();
        task_util::WaitForIdle();

        // create an XMPP client in server A
        agent_a_.reset(new test::NetworkAgentMock(&evm_, 
                                                  SUB_ADDR, xs_a_->GetPort()));

        TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
        TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    }

    virtual void TearDown() {
        agent_a_->SessionDown();
        task_util::WaitForIdle();
        xs_a_->Shutdown();
        task_util::WaitForIdle();
        TASK_UTIL_EXPECT_EQ(0, xs_a_->ConnectionsCount());
        agent_a_->Delete();
        bgp_channel_manager_.reset();
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(xs_a_);
        task_util::WaitForIdle();
        a_->Shutdown();
        task_util::WaitForIdle();
        evm_.Shutdown();
        thread_.Join();
        task_util::WaitForIdle();
    }
};

int BgpXmppUnitTest::validate_done_;

const char *BgpXmppUnitTest::config_tmpl = "\
<config>\
    <bgp-router name=\'A\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
    </bgp-router>\
    <routing-instance name='blue'>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
    <routing-instance name='red'>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
</config>\
";

namespace {

TEST_F(BgpXmppUnitTest, Connection) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    // Wait upto 5 seconds
    BGP_DEBUG_UT("-- Executing --");
    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Subscribe(BgpConfigManager::kMasterInstance, -1); 
    agent_a_->Subscribe("blue", 1); 
    agent_a_->Subscribe("red", 2); 
    agent_a_->AddRoute("blue","10.1.1.1/32");

    TASK_UTIL_EXPECT_EQ(2, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 2);

    //show route
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    sandesh_context.xmpp_peer_manager = bgp_channel_manager_.get();
    Sandesh::set_client_context(&sandesh_context);
    std::vector<int> result2 = list_of(1)(1)(1)(1);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                               result2));
    ShowRouteReq *show_req = new ShowRouteReq;
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(1, validate_done_);
    
    //trigger a TCP close event on the server
    agent_a_->SessionDown();
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->Count());

    //wait for channel to be deleted in the context of config-task
    task_util::WaitForIdle();

}

TEST_F(BgpXmppUnitTest, ConnectionTearWithPendingReg) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Subscribe(BgpConfigManager::kMasterInstance, -1); 

    //trigger a TCP close event on the server with two subscribe request
    agent_a_->Subscribe("red", 2); 
    agent_a_->Subscribe("blue", 1); 
    agent_a_->AddRoute("blue","10.1.1.1/32");
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}


TEST_F(BgpXmppUnitTest, ConnectionTearWithPendingUnreg) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Subscribe(BgpConfigManager::kMasterInstance, -1); 
    agent_a_->Subscribe("red", 2); 
    agent_a_->Subscribe("blue", 1); 
    agent_a_->AddRoute("blue","10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(2, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 2);

    //trigger a TCP close event on the server with two unsubscribe request
    agent_a_->Unsubscribe("red", -1, false); 
    agent_a_->Unsubscribe("blue", -1, false); 
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterWithoutRoutingInstance) {

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Subscribe("red", 2); 
    agent_a_->Subscribe("blue", 1); 
    agent_a_->AddRoute("blue","10.1.1.1/32");
    agent_a_->Subscribe(BgpConfigManager::kMasterInstance, -1); 

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(bgp_channel_manager_->channel_, 
                                     BgpConfigManager::kMasterInstance, -1));

    task_util::WaitForIdle();

    Configure();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 2);

    //trigger a TCP close event on the server with two unsubscribe request
    agent_a_->Unsubscribe("red", -1, false); 
    agent_a_->Unsubscribe("blue", -1, false); 
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegAddDelAddRouteWithoutRoutingInstance) {

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Subscribe("red", 2); 
    agent_a_->Subscribe("blue", 1); 
    agent_a_->AddRoute("blue","10.1.1.1/32");
    agent_a_->Subscribe(BgpConfigManager::kMasterInstance, -1); 

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(bgp_channel_manager_->channel_, 
                                     BgpConfigManager::kMasterInstance, -1));

    agent_a_->DeleteRoute("blue","10.1.1.1/32");
    agent_a_->AddRoute("blue","30.1.1.1/32");
    task_util::WaitForIdle();

    Configure();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 2);

    //trigger a TCP close event on the server with two unsubscribe request
    agent_a_->Unsubscribe("red", -1, false); 
    agent_a_->Unsubscribe("blue", -1, false); 
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}


TEST_F(BgpXmppUnitTest, RegUnregWithoutRoutingInstance) {

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Subscribe(BgpConfigManager::kMasterInstance, -1); 
    agent_a_->Subscribe("red", 2); 
    agent_a_->Subscribe("blue", 1); 
    agent_a_->AddRoute("blue","10.3.1.1/32");

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(bgp_channel_manager_->channel_, 
                                     BgpConfigManager::kMasterInstance, -1));

    // unsubscribe request
    agent_a_->Unsubscribe("red", -1, false); 
    agent_a_->Unsubscribe("blue", -1, false); 
    agent_a_->Unsubscribe(BgpConfigManager::kMasterInstance, -1, false); 

    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(bgp_channel_manager_->channel_, 
                                     BgpConfigManager::kMasterInstance));

    Configure();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);

    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppSerializeMembershipReqTest, SerializedMembershipReq0) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    agent_a_->Subscribe("red", 1, false); 
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Subscribe("red", 2, false); 
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(bgp_channel_manager_->channel_, 
                                            "red", 2));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 1);
}

TEST_F(BgpXmppSerializeMembershipReqTest, SerializedMembershipReq1) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    agent_a_->Subscribe("red", 1, false); 
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false); 
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(bgp_channel_manager_->channel_, 
                                            "red"));
    BGP_VERIFY_ROUTE_COUNT(
        a_->routing_instance_mgr()->GetRoutingInstance("red")->GetTable(
                           Address::INET), 0);
}

TEST_F(BgpXmppSerializeMembershipReqTest, SerializedMembershipReq2) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    agent_a_->Subscribe("red", 1, false); 
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false); 
    agent_a_->Subscribe("red", 2, false); 
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(bgp_channel_manager_->channel_, 
                                         "red", 2));
    BGP_VERIFY_ROUTE_COUNT(
        a_->routing_instance_mgr()->GetRoutingInstance("red")->GetTable(
                           Address::INET), 0);
}

TEST_F(BgpXmppSerializeMembershipReqTest, SerializedMembershipReq3) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    agent_a_->Subscribe("red", 1, false); 
    agent_a_->Unsubscribe("red", -1, false); 
    agent_a_->Subscribe("red", 2, false); 
    agent_a_->Unsubscribe("red", -1, false); 
    agent_a_->Subscribe("red", 2, false); 
    agent_a_->AddRoute("red","10.1.1.1/32");
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(bgp_channel_manager_->channel_, 
                                         "red", 2));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 1);
}

TEST_F(BgpXmppSerializeMembershipReqTest, SerializedMembershipReq4) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    agent_a_->Subscribe("red", 1, false); 
    agent_a_->Unsubscribe("red", -1, false); 
    agent_a_->Subscribe("red", 2, false); 
    agent_a_->Unsubscribe("red", -1, false); 
    agent_a_->Subscribe("red", 3, false); 
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false); 
    scheduler->Start();

    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(bgp_channel_manager_->channel_, 
                                         "red"));
    BGP_VERIFY_ROUTE_COUNT(
        a_->routing_instance_mgr()->GetRoutingInstance("red")->GetTable(
                           Address::INET), 0);
}

TEST_F(BgpXmppSerializeMembershipReqTest, SerializedMembershipReq5) {
    agent_a_->Subscribe("red", 1, false); 
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(bgp_channel_manager_->channel_, 
                                         "red", 1));

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    agent_a_->Unsubscribe("red", -1, false); 
    agent_a_->Subscribe("red", 3, false); 
    agent_a_->AddRoute("red","10.1.1.1/32");
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(bgp_channel_manager_->channel_, 
                                         "red", 3));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 1);
}

TEST_F(BgpXmppSerializeMembershipReqTest, SerializedMembershipReq6) {
    agent_a_->Subscribe("red", 1, false); 
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(bgp_channel_manager_->channel_, 
                                         "red", 1));

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    agent_a_->Unsubscribe("red", -1, false); 
    agent_a_->Subscribe("red", 3, false); 
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false); 
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(bgp_channel_manager_->channel_, 
                                         "red"));
    BGP_VERIFY_ROUTE_COUNT(
        a_->routing_instance_mgr()->GetRoutingInstance("red")->GetTable(
                           Address::INET), 0);
}

TEST_F(BgpXmppUnitTest, BgpXmppBadAddress) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Subscribe("red", 1, false); 
    agent_a_->AddRoute("red","10.1.1.1./32");
    agent_a_->AddRoute("red","10.1.1.1/32", "70.2.");
    BGP_VERIFY_ROUTE_COUNT(
        a_->routing_instance_mgr()->GetRoutingInstance("red")->GetTable(
                           Address::INET), 0);

    agent_a_->SessionDown();
    task_util::WaitForIdle();
}
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
