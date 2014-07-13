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

#include "base/task_annotations.h"
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
         LOG(DEBUG, "XmppHandleChannelEvent: " << state << " count: " << count);
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
        if (rt_instance == NULL)
            return false;
        BgpTable *table = rt_instance->GetTable(Address::INET);
        if (table == NULL)
            return false;
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
        if (rt_instance == NULL)
            return true;
        BgpTable *table = rt_instance->GetTable(Address::INET);
        if (table == NULL)
            return true;
        IPeerRib *rib = a_->membership_mgr()->IPeerRibFind(channel->Peer(), table);
        if (rib) {
            return false;
        }
        return true;
    }

    bool PeerHasPendingInstanceMembershipRequests(BgpXmppChannel *channel) {
        return (!channel->vrf_membership_request_map_.empty());
    }

    bool PeerHasPendingMembershipRequests(BgpXmppChannel *channel) {
        return (channel->routingtable_membership_request_map_.size() != 0);
    }

    bool PeerCloseIsDeferred(BgpXmppChannel *channel) {
        return channel->defer_peer_close_;
    }

    size_t PeerDeferQSize(BgpXmppChannel *channel) {
        return channel->defer_q_.size();
    }

    void PausePeerRibMembershipManager() {
        a_->membership_mgr()->event_queue_->set_disable(true);
    }

    void ResumePeerRibMembershipManager() {
        a_->membership_mgr()->event_queue_->set_disable(false);
    }

    void PauseBgpXmppChannelManager() {
        bgp_channel_manager_->queue_.set_disable(true);
    }

    void ResumeBgpXmppChannelManager() {
        bgp_channel_manager_->queue_.set_disable(false);
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
        task_util::WaitForIdle();
        ConcurrencyScope scope("bgp::Config");
        TASK_UTIL_EXPECT_TRUE(a_->IsReadyForDeletion());
        task_util::WaitForIdle();
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

    void UnconfigureRoutingInstances() {
        const char *config = "\
<delete>\
    <routing-instance name='blue'>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
    <routing-instance name='red'>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
</delete>\
";
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

    RoutingInstance *VerifyRoutingInstance(const string &instance) {
        TASK_UTIL_EXPECT_TRUE(
            a_->routing_instance_mgr()->GetRoutingInstance(instance) != NULL);
        return a_->routing_instance_mgr()->GetRoutingInstance(instance);
    }

    void VerifyNoRoutingInstance(const string &instance) {
        TASK_UTIL_EXPECT_TRUE(
            a_->routing_instance_mgr()->GetRoutingInstance(instance) == NULL);
    }

    BgpTable *VerifyBgpTable(const string &instance, Address::Family family) {
        RoutingInstance *rt_instance = VerifyRoutingInstance(instance);
        TASK_UTIL_EXPECT_TRUE(rt_instance->GetTable(family) != NULL);
        return rt_instance->GetTable(family);
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

static void PauseDelete(LifetimeActor *actor) {
    TaskScheduler::GetInstance()->Stop();
    actor->PauseDelete();
    TaskScheduler::GetInstance()->Start();
}

static void ResumeDelete(LifetimeActor *actor) {
    TaskScheduler::GetInstance()->Stop();
    actor->ResumeDelete();
    TaskScheduler::GetInstance()->Start();
}

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
        ConcurrencyScope scope("bgp::Config");
        TASK_UTIL_EXPECT_TRUE(a_->IsReadyForDeletion());
        task_util::WaitForIdle();
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

TEST_F(BgpXmppUnitTest, RegisterWithDeletedRoutingInstance) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());

    // Unconfigure all instances.
    // The red instance should get destroyed while the blue instance should
    // still exist in deleted state. All tables in the blue instance should
    // get destroyed.
    UnconfigureRoutingInstances();
    task_util::WaitForIdle();
    VerifyNoRoutingInstance("red");
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    TASK_UTIL_EXPECT_EQ(0, blue->GetTables().size());

    // Subscribe and add route to deleted blue instance. Make sure that the
    // messages have been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue","10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());

    // Resume deletion of blue instance and make sure it's gone.
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());

    // Configure instances and verify that the route has been added.
    Configure();
    VerifyRoutingInstance("red");
    VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") != NULL);
    TASK_UTIL_EXPECT_TRUE(
        PeerRegistered(bgp_channel_manager_->channel_, "blue", 1));

    // Clean up.
    agent_a_->Unsubscribe("blue", -1, false);
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, UnregisterWithDeletedRoutingInstance) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Subscribe and add route to deleted blue instance. Make sure that the
    // messages have been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue","10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerRegistered(bgp_channel_manager_->channel_, "blue", 1));

    // Verify that the route has been added.
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") != NULL);

    // Pause deletion for blue instance.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());

    // Unconfigure all instances.
    // The red instance should get destroyed while the blue instance should
    // still exist in deleted state. All tables in the blue instance should
    // not get destroyed since the inet table has a route from the agent.
    UnconfigureRoutingInstances();
    task_util::WaitForIdle();
    VerifyNoRoutingInstance("red");
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    TASK_UTIL_EXPECT_NE(0, blue->GetTables().size());

    // Unsubscribe from the blue instance and make sure that the unsubscribe
    // message has been processed on the bgp server.
    agent_a_->Unsubscribe("blue");
    TASK_UTIL_EXPECT_EQ(3, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Resume deletion of blue instance and make sure it's gone.
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");

    // Route shouldn't exist anymore.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") == NULL);

    // Clean up.
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterUnregisterWithDeletedRoutingInstance) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());

    // Unconfigure all instances.
    // The red instance should get destroyed while the blue instance should
    // still exist in deleted state. All tables in the blue instance should
    // get destroyed.
    UnconfigureRoutingInstances();
    task_util::WaitForIdle();
    VerifyNoRoutingInstance("red");
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    TASK_UTIL_EXPECT_EQ(0, blue->GetTables().size());

    // Subscribe and add route to deleted blue instance. Make sure that the
    // messages have been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue","10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());

    // Unsubscribe from the blue instance and make sure that the unsubscribe
    // message has been processed on the bgp server.
    agent_a_->Unsubscribe("blue");
    TASK_UTIL_EXPECT_EQ(3, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Resume deletion of blue instance and make sure it's gone.
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") == NULL);

    // Clean up.
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterAddDelAddRouteWithDeletedRoutingInstance) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());

    // Unconfigure all instances.
    // The red instance should get destroyed while the blue instance should
    // still exist in deleted state. All tables in the blue instance should
    // get destroyed.
    UnconfigureRoutingInstances();
    task_util::WaitForIdle();
    VerifyNoRoutingInstance("red");
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    TASK_UTIL_EXPECT_EQ(0, blue->GetTables().size());

    // Subscribe and add route to deleted blue instance. Make sure that the
    // messages have been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue","10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Delete the route and make sure message is processed on bgp server.
    agent_a_->DeleteRoute("blue", "10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(3, bgp_channel_manager_->channel_->Count());

    // Add the route again and make sure message is processed on bgp server.
    agent_a_->AddRoute("blue", "10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(4, bgp_channel_manager_->channel_->Count());

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());

    // Resume deletion of blue instance and make sure it's gone.
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());

    // Configure instances and verify that the route has been added.
    Configure();
    VerifyRoutingInstance("red");
    VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") != NULL);
    TASK_UTIL_EXPECT_TRUE(
        PeerRegistered(bgp_channel_manager_->channel_, "blue", 1));

    // Clean up.
    agent_a_->Unsubscribe("blue", -1, false);
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterWithDeletedBgpTable1) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance and the inet table.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());
    BgpTable *blue_table = VerifyBgpTable("blue", Address::INET);
    PauseDelete(blue_table->deleter());

    // Unconfigure all instances.
    // The blue instance should still exist in deleted state.
    // The INET tables in blue instance should still exist in deleted state.
    UnconfigureRoutingInstances();
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    TASK_UTIL_EXPECT_EQ(1, blue->GetTables().size());
    blue_table = VerifyBgpTable("blue", Address::INET);
    TASK_UTIL_EXPECT_TRUE(blue_table->IsDeleted());

    // Subscribe to deleted blue instance. Make sure that the message has
    // been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Resume deletion of blue inet table and blue instance and make sure
    // they are gone.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");

    // Clean up.
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterWithDeletedBgpTable2) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance and the inet table.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());
    BgpTable *blue_table = VerifyBgpTable("blue", Address::INET);
    PauseDelete(blue_table->deleter());

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Subscribe to the blue instance. Make sure that the message has
    // been processed on the bgp server. The subscription request will
    // get enqueued in the membership manager, but won't be processed.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Unconfigure all instances.
    // The blue instance should still exist in deleted state.
    // The INET tables in blue instance should still exist in deleted state.
    UnconfigureRoutingInstances();
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    blue_table = VerifyBgpTable("blue", Address::INET);
    TASK_UTIL_EXPECT_TRUE(blue_table->IsDeleted());

    // Resume the peer membership manager.
    ResumePeerRibMembershipManager();

    // The subscribe request should have been processed by the membership
    // manager and a response returned.  The membership manager will have
    // no subscription state since the table was amrked deleted when the
    // subscribe was processed by it.
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Resume deletion of blue inet table and blue instance and make sure
    // they are gone.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");

    // Clean up.
    agent_a_->Unsubscribe("blue", -1, false);
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, UnregisterWithDeletedBgpTable1) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Subscribe to blue instance. Make sure that the message has
    // been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_->Count());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerRegistered(bgp_channel_manager_->channel_, "blue", 1));

    // Pause deletion for blue instance and the inet table.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());
    BgpTable *blue_table = VerifyBgpTable("blue", Address::INET);
    PauseDelete(blue_table->deleter());

    // Unconfigure all instances.
    // The blue instance should still exist in deleted state.
    // The INET tables in blue instance should still exist in deleted state.
    UnconfigureRoutingInstances();
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    blue_table = VerifyBgpTable("blue", Address::INET);
    TASK_UTIL_EXPECT_TRUE(blue_table->IsDeleted());

    // Unsubscribe to blue instance. Make sure that the message has
    // been processed on the bgp server.
    agent_a_->Unsubscribe("blue");
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->channel_->Count());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Resume deletion of blue inet table and blue instance and make sure
    // they are gone.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");

    // Clean up.
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, UnregisterWithDeletedBgpTable2) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance and the inet table.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());
    BgpTable *blue_table = VerifyBgpTable("blue", Address::INET);
    PauseDelete(blue_table->deleter());

    // Subscribe to the blue instance. Make sure that the message has
    // been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_->Count());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerRegistered(bgp_channel_manager_->channel_, "blue", 1));

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Unsubscribe to the blue instance. Make sure that the message has
    // been processed on the bgp server. The unsubscribe request will
    // get enqueued in the membership manager, but won't be processed.
    agent_a_->Unsubscribe("blue");
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerRegistered(bgp_channel_manager_->channel_, "blue", 1));

    // Unconfigure all instances.
    // The blue instance should still exist in deleted state.
    // The INET tables in blue instance should still exist in deleted state.
    UnconfigureRoutingInstances();
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    blue_table = VerifyBgpTable("blue", Address::INET);
    TASK_UTIL_EXPECT_TRUE(blue_table->IsDeleted());

    // Resume the peer membership manager.
    ResumePeerRibMembershipManager();

    // The unsubscribe request should have been processed by the membership
    // manager and a response returned.
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Resume deletion of blue inet table and blue instance and make sure
    // they are gone.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");

    // Clean up.
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterUnregisterWithDeletedBgpTable1) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance and the inet table.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());
    BgpTable *blue_table = VerifyBgpTable("blue", Address::INET);
    PauseDelete(blue_table->deleter());

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Subscribe to the blue instance. Make sure that the message has been
    // processed on the bgp server. The subscription request will get
    // enqueued in the membership manager, but won't be processed.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Unconfigure all instances.
    // The blue instance should still exist in deleted state.
    // The INET tables in blue instance should still exist in deleted state.
    UnconfigureRoutingInstances();
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    blue_table = VerifyBgpTable("blue", Address::INET);
    TASK_UTIL_EXPECT_TRUE(blue_table->IsDeleted());

    // Resume the peer membership manager.
    ResumePeerRibMembershipManager();

    // The subscribe request should have been processed by the membership
    // manager and a response returned.  The membership manager will have
    // no subscription state since the table was marked deleted when the
    // subscribe was processed by it.
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Unsubscribe for the blue instance.
    agent_a_->Unsubscribe("blue", -1, false);
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));

    // Resume deletion of blue inet table and blue instance.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterUnregisterWithDeletedBgpTable2) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance and the inet table.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());
    BgpTable *blue_table = VerifyBgpTable("blue", Address::INET);
    PauseDelete(blue_table->deleter());

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Subscribe to the blue instance and add a route. Make sure that messages
    // have been processed on the bgp server. The subscription request will get
    // enqueued in the membership manager, but won't be processed.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Unconfigure all instances.
    // The blue instance should still exist in deleted state.
    // The INET tables in blue instance should still exist in deleted state.
    UnconfigureRoutingInstances();
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    blue_table = VerifyBgpTable("blue", Address::INET);
    TASK_UTIL_EXPECT_TRUE(blue_table->IsDeleted());

    // Resume the peer membership manager.
    ResumePeerRibMembershipManager();

    // The subscribe request should have been processed by the membership
    // manager and a response returned.  The membership manager will have
    // no subscription state since the table was marked deleted when the
    // subscribe was processed by it.
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Unsubscribe for the blue instance. The request will be queued to
    // the membership manager but won't be processed yet.
    agent_a_->Unsubscribe("blue", -1, false);
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));

    // Resume deletion of blue inet table and blue instance.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");

    // Resume the peer membership manager.
    ResumePeerRibMembershipManager();
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
}

TEST_F(BgpXmppUnitTest, RegisterUnregisterWithDeletedBgpTable3) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance and the inet table.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());
    BgpTable *blue_table = VerifyBgpTable("blue", Address::INET);
    PauseDelete(blue_table->deleter());

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Subscribe to the blue instance. Make sure that the message has been
    // processed on the bgp server. The subscription request will get
    // enqueued in the membership manager, but won't be processed.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Unconfigure all instances.
    // The blue instance should still exist in deleted state.
    // The INET tables in blue instance should still exist in deleted state.
    UnconfigureRoutingInstances();
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    blue_table = VerifyBgpTable("blue", Address::INET);
    TASK_UTIL_EXPECT_TRUE(blue_table->IsDeleted());

    // Resume the peer membership manager.
    ResumePeerRibMembershipManager();

    // The subscribe request should have been processed by the membership
    // manager and a response returned.  The membership manager will have
    // no subscription state since the table was marked deleted when the
    // subscribe was processed by it.
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Resume deletion of blue inet table and blue instance.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");

    // Unsubscribe for the old incarnation.
    agent_a_->Unsubscribe("blue", -1, false);
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
}

TEST_F(BgpXmppUnitTest, RegisterAddDelAddRouteWithDeletedBgpTable) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance and the inet table.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());
    BgpTable *blue_table = VerifyBgpTable("blue", Address::INET);
    PauseDelete(blue_table->deleter());

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Subscribe to the blue instance. Make sure that the message has
    // been processed on the bgp server. The subscription request will
    // get enqueued in the membership manager, but won't be processed.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Add, delete and add the same route.
    agent_a_->AddRoute("blue","10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->channel_->Count());
    agent_a_->DeleteRoute("blue", "10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(3, bgp_channel_manager_->channel_->Count());
    agent_a_->AddRoute("blue", "10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(4, bgp_channel_manager_->channel_->Count());

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());

    // Unconfigure all instances.
    // The blue instance should still exist in deleted state.
    // The INET tables in blue instance should still exist in deleted state.
    UnconfigureRoutingInstances();
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    blue_table = VerifyBgpTable("blue", Address::INET);
    TASK_UTIL_EXPECT_TRUE(blue_table->IsDeleted());

    // Resume the peer membership manager.
    ResumePeerRibMembershipManager();

    // The subscribe request should have been processed by the membership
    // manager and a response returned.  Since the table was deleted, the
    // membership manager will have no subscription state.
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));
    task_util::WaitForIdle();

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());

    // Resume deletion of blue inet table and blue instance and make sure
    // they are gone.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");

    // Clean up.
    agent_a_->Unsubscribe("blue", -1, false);
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterUnregisterWithDeletedBgpTableThenRegisterAgain) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance and the inet table.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());
    BgpTable *blue_table = VerifyBgpTable("blue", Address::INET);
    PauseDelete(blue_table->deleter());

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Subscribe to the blue instance and add a route. Make sure that messages
    // have been processed on the bgp server. The subscription request will get
    // enqueued in the membership manager, but won't be processed.
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue", "10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Unconfigure all instances.
    // The blue instance should still exist in deleted state.
    // The INET tables in blue instance should still exist in deleted state.
    UnconfigureRoutingInstances();
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    blue_table = VerifyBgpTable("blue", Address::INET);
    TASK_UTIL_EXPECT_TRUE(blue_table->IsDeleted());

    // Resume the peer membership manager.
    ResumePeerRibMembershipManager();

    // The subscribe request should have been processed by the membership
    // manager and a response returned.  The membership manager will have
    // no subscription state since the table was marked deleted when the
    // subscribe was processed by it.
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Unsubscribe for the old incarnation.
    agent_a_->Unsubscribe("blue", -1, false);
    TASK_UTIL_EXPECT_EQ(3, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));

    // Subscribe for the new incarnation and add a route.  The subscribe
    // should get deferred till the routing instance is created again.
    // The route add should also get deferred.
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue", "10.1.1.2/32");
    TASK_UTIL_EXPECT_EQ(5, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));

    // Configure the routing instances again.  The peer should be not be
    // registered to the blue instance since the old instance and table
    // have not yet been deleted.
    Configure();
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Resume deletion of blue inet table and blue instance.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    task_util::WaitForIdle();

    // Make sure that the instance and table got created again - they should
    // not be marked deleted.
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_FALSE(blue->deleted());
    blue_table = VerifyBgpTable("blue", Address::INET);
    TASK_UTIL_EXPECT_FALSE(blue_table->IsDeleted());

    // Peer should have been registered to the blue instance and the agent
    // should have the route to 10.1.1.2/32, but not to 10.1.1.1/32.
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerRegistered(bgp_channel_manager_->channel_, "blue", 1));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") == NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.2/32") != NULL);

    // Clean up.
    agent_a_->Unsubscribe("blue", -1, false);
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, DeferCloseWithPendingRegister) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Subscribe to the blue instance. Make sure that the message has
    // been processed on the bgp server. The subscription request will
    // get enqueued in the membership manager, but won't be processed.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Bring the session down.  The close should get deferred since there
    // are pending membership requests.
    agent_a_->SessionDown();
    TASK_UTIL_EXPECT_TRUE(
        PeerCloseIsDeferred(bgp_channel_manager_->channel_));

    // Resume the peer membership manager.  This should cause the deferred
    // peer close to resume and finish.
    ResumePeerRibMembershipManager();
}

TEST_F(BgpXmppUnitTest, DeferCloseWithPendingUnregister) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Subscribe to the blue instance. Make sure that the message has
    // been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_->Count());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerRegistered(bgp_channel_manager_->channel_, "blue", 1));

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Unsubscribe to the blue instance. Make sure that the message has
    // been processed on the bgp server. The unsubscribe request will
    // get enqueued in the membership manager, but won't be processed.
    agent_a_->Unsubscribe("blue");
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_TRUE(
        PeerRegistered(bgp_channel_manager_->channel_, "blue", 1));

    // Bring the session down.  The close should get deferred since there
    // are pending membership requests.
    agent_a_->SessionDown();
    TASK_UTIL_EXPECT_TRUE(
        PeerCloseIsDeferred(bgp_channel_manager_->channel_));

    // Resume the peer membership manager.  This should cause the deferred
    // peer close to resume and finish.
    ResumePeerRibMembershipManager();
}

TEST_F(BgpXmppUnitTest, CreateRoutingInstanceWithPeerCloseInProgress1) {
    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Nothing has been configured yet, so there should be no instances.
    VerifyNoRoutingInstance("red");
    VerifyNoRoutingInstance("blue");

    // Subscribe to non-existent blue instance. Make sure that the message is
    // processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));
    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingInstanceMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));

    // Pause the channel manager work queue.
    PauseBgpXmppChannelManager();

    // Bring the session down.  The peer close should not get deferred since
    // there are no pending membership requests in the channel.  However, the
    // channel cleanup won't finish because the channel manager work queue has
    // been paused.
    agent_a_->SessionDown();
    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_->peer_deleted());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(
        PeerCloseIsDeferred(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingInstanceMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));

    // Configure instances and verify that the instances are created.
    Configure();
    VerifyRoutingInstance("red");
    VerifyRoutingInstance("blue");

    // The channel should not be registered to the blue instance since the
    // instance was created after the session went down.  It shouldn't have
    // any pending membership requests either.
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingInstanceMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));

    // Resume the channel manager work queue. This should cause the deferred
    // peer close to resume and finish.
    ResumeBgpXmppChannelManager();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, CreateRoutingInstanceWithPeerCloseInProgress2) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_ != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());

    // Unconfigure all instances.
    // The red instance should get destroyed while the blue instance should
    // still exist in deleted state. All tables in the blue instance should
    // get destroyed.
    UnconfigureRoutingInstances();
    task_util::WaitForIdle();
    VerifyNoRoutingInstance("red");
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    TASK_UTIL_EXPECT_EQ(0, blue->GetTables().size());

    // Subscribe to deleted blue instance. Make sure that the message has is
    // processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));
    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingInstanceMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));

    // Resume deletion of blue instance and make sure it's gone.
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));

    // Pause the channel manager work queue.
    PauseBgpXmppChannelManager();

    // Bring the session down.  The peer close should not get deferred since
    // there are no pending membership requests in the channel.  However, the
    // channel cleanup won't finish because the channel manager work queue has
    // been paused.
    agent_a_->SessionDown();
    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->channel_->peer_deleted());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(
        PeerCloseIsDeferred(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingInstanceMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));

    // Configure instances and verify that the instances are created.
    Configure();
    VerifyRoutingInstance("red");
    VerifyRoutingInstance("blue");

    // The channel should not be registered to the blue instance since the
    // instance was created after the session went down.  It shouldn't have
    // any pending membership requests either.
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingInstanceMembershipRequests(bgp_channel_manager_->channel_));
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));

    // Resume the channel manager work queue. This should cause the deferred
    // peer close to resume and finish.
    ResumeBgpXmppChannelManager();
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

TEST_F(BgpXmppSerializeMembershipReqTest, FlushDeferQForVrfAndTable1) {
    PausePeerRibMembershipManager();

    agent_a_->Subscribe("blue", 1, false);
    agent_a_->Subscribe("red", 2, false);
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "red"));

    agent_a_->AddRoute("red", "10.1.1.1/32");
    agent_a_->AddRoute("red", "10.1.1.2/32");
    agent_a_->AddRoute("blue", "10.1.1.1/32");
    agent_a_->AddRoute("blue", "10.1.1.2/32");
    TASK_UTIL_EXPECT_EQ(6, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_EQ(4, PeerDeferQSize(bgp_channel_manager_->channel_));

    agent_a_->Unsubscribe("blue", -1, false);
    TASK_UTIL_EXPECT_EQ(7, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_EQ(2, PeerDeferQSize(bgp_channel_manager_->channel_));

    agent_a_->Unsubscribe("red", -1, false);
    TASK_UTIL_EXPECT_EQ(8, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_EQ(0, PeerDeferQSize(bgp_channel_manager_->channel_));

    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    ResumePeerRibMembershipManager();
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
}

TEST_F(BgpXmppSerializeMembershipReqTest, FlushDeferQForVrfAndTable2) {
    PausePeerRibMembershipManager();

    agent_a_->Subscribe("blue", 1, false);
    agent_a_->Subscribe("red", 2, false);
    TASK_UTIL_EXPECT_EQ(2, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "blue"));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "red"));

    agent_a_->AddRoute("red", "10.1.1.1/32");
    agent_a_->AddRoute("red", "10.1.1.2/32");
    agent_a_->AddRoute("blue", "10.1.1.1/32");
    agent_a_->AddRoute("blue", "10.1.1.2/32");
    TASK_UTIL_EXPECT_EQ(6, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_EQ(4, PeerDeferQSize(bgp_channel_manager_->channel_));

    agent_a_->Unsubscribe("red", -1, false);
    TASK_UTIL_EXPECT_EQ(7, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_EQ(2, PeerDeferQSize(bgp_channel_manager_->channel_));

    agent_a_->Unsubscribe("blue", -1, false);
    TASK_UTIL_EXPECT_EQ(8, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_EQ(0, PeerDeferQSize(bgp_channel_manager_->channel_));

    TASK_UTIL_EXPECT_TRUE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
    ResumePeerRibMembershipManager();
    TASK_UTIL_EXPECT_FALSE(
        PeerHasPendingMembershipRequests(bgp_channel_manager_->channel_));
}

TEST_F(BgpXmppSerializeMembershipReqTest, FlushDeferQForVrf1) {
    agent_a_->Subscribe("red1", 1, false);
    agent_a_->Subscribe("red2", 2, false);
    agent_a_->Subscribe("red3", 3, false);
    TASK_UTIL_EXPECT_EQ(3, bgp_channel_manager_->channel_->Count());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "red1"));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "red2"));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "red3"));

    agent_a_->AddRoute("red2", "10.1.1.1/32");
    agent_a_->AddRoute("red2", "10.1.1.2/32");
    agent_a_->AddRoute("red3", "10.1.1.1/32");
    agent_a_->AddRoute("red3", "10.1.1.2/32");
    agent_a_->AddRoute("red1", "10.1.1.1/32");
    agent_a_->AddRoute("red1", "10.1.1.2/32");
    TASK_UTIL_EXPECT_EQ(9, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_EQ(6, PeerDeferQSize(bgp_channel_manager_->channel_));

    agent_a_->Unsubscribe("red1", -1, false);
    TASK_UTIL_EXPECT_EQ(10, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_EQ(4, PeerDeferQSize(bgp_channel_manager_->channel_));

    agent_a_->Unsubscribe("red2", -1, false);
    TASK_UTIL_EXPECT_EQ(11, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_EQ(2, PeerDeferQSize(bgp_channel_manager_->channel_));

    agent_a_->Unsubscribe("red3", -1, false);
    TASK_UTIL_EXPECT_EQ(12, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_EQ(0, PeerDeferQSize(bgp_channel_manager_->channel_));
}

TEST_F(BgpXmppSerializeMembershipReqTest, FlushDeferQForVrf2) {
    agent_a_->Subscribe("red1", 1, false);
    agent_a_->Subscribe("red2", 2, false);
    agent_a_->Subscribe("red3", 3, false);
    TASK_UTIL_EXPECT_EQ(3, bgp_channel_manager_->channel_->Count());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "red1"));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "red2"));
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(bgp_channel_manager_->channel_, "red3"));

    agent_a_->AddRoute("red2", "10.1.1.1/32");
    agent_a_->AddRoute("red2", "10.1.1.2/32");
    agent_a_->AddRoute("red3", "10.1.1.1/32");
    agent_a_->AddRoute("red3", "10.1.1.2/32");
    agent_a_->AddRoute("red1", "10.1.1.1/32");
    agent_a_->AddRoute("red1", "10.1.1.2/32");
    TASK_UTIL_EXPECT_EQ(9, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_EQ(6, PeerDeferQSize(bgp_channel_manager_->channel_));

    agent_a_->Unsubscribe("red2", -1, false);
    TASK_UTIL_EXPECT_EQ(10, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_EQ(4, PeerDeferQSize(bgp_channel_manager_->channel_));

    agent_a_->Unsubscribe("red1", -1, false);
    TASK_UTIL_EXPECT_EQ(11, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_EQ(2, PeerDeferQSize(bgp_channel_manager_->channel_));

    agent_a_->Unsubscribe("red3", -1, false);
    TASK_UTIL_EXPECT_EQ(12, bgp_channel_manager_->channel_->Count());
    TASK_UTIL_EXPECT_EQ(0, PeerDeferQSize(bgp_channel_manager_->channel_));
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
