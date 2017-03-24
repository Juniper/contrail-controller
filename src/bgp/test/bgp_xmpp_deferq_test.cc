/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <fstream>
#include <sstream>
#include <boost/assign/list_of.hpp>


using namespace boost::assign;


#include "base/task_annotations.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_membership.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/test/bgp_server_test_util.h"
#include "bgp/xmpp_message_builder.h"
#include "io/test/event_manager_test.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"

using namespace boost::asio;
using namespace std;

#define SUB_ADDR "agent@vnsw.contrailsystems.com"

class BgpXmppChannelMock;

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

    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel);

    int Count() {
        return count;
    }
    int count;
    BgpXmppChannelMock *channel_;
};

class BgpXmppChannelMock : public BgpXmppChannel {
public:
    BgpXmppChannelMock(XmppChannel *channel, BgpServer *server,
            BgpXmppChannelManager *manager) :
        BgpXmppChannel(channel, server, manager), count_(0), manager_(manager) {
            bgp_policy_ = RibExportPolicy(BgpProto::XMPP,
                                          RibExportPolicy::XMPP, -1, 0);
    }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        count_ ++;
        BgpXmppChannel::ReceiveUpdate(msg);
    }

    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }
    virtual ~BgpXmppChannelMock() {
        dynamic_cast<BgpXmppChannelManagerMock *>(manager_)->channel_ = NULL;
    }

private:
    size_t count_;
    BgpXmppChannelManager *manager_;
};

BgpXmppChannel *BgpXmppChannelManagerMock::CreateChannel(XmppChannel *channel) {
    channel_ = new BgpXmppChannelMock(channel, bgp_server_, this);
    return channel_;
}

static const char *config_template_with_instances = "\
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

static const char *config_template_without_instances = "\
<config>\
    <bgp-router name=\'A\'>\
        <identifier>192.168.0.1</identifier>\
        <address>127.0.0.1</address>\
        <port>%d</port>\
    </bgp-router>\
</config>\
";

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
        BgpMembershipManager *membership_mgr = a_->membership_mgr();
        int mm_instance_id;
        if (!membership_mgr->GetRegistrationInfo(channel->Peer(), table,
            &mm_instance_id)) {
            return false;
        }
        return (mm_instance_id == instance_id);
    }

    bool PeerRegisteredRibIn(BgpXmppChannel *channel, string instance_name) {
        RoutingInstanceMgr *instance_mgr = a_->routing_instance_mgr();
        RoutingInstance *rt_instance =
            instance_mgr->GetRoutingInstance(instance_name);
        if (rt_instance == NULL)
            return false;
        BgpTable *table = rt_instance->GetTable(Address::INET);
        if (table == NULL)
            return false;
        BgpMembershipManager *membership_mgr = a_->membership_mgr();
        return (membership_mgr->IsRibInRegistered(channel->Peer(), table));
    }

    bool PeerRegisteredRibOut(BgpXmppChannel *channel, string instance_name) {
        RoutingInstanceMgr *instance_mgr = a_->routing_instance_mgr();
        RoutingInstance *rt_instance =
            instance_mgr->GetRoutingInstance(instance_name);
        if (rt_instance == NULL)
            return false;
        BgpTable *table = rt_instance->GetTable(Address::INET);
        if (table == NULL)
            return false;
        BgpMembershipManager *membership_mgr = a_->membership_mgr();
        return (membership_mgr->IsRibOutRegistered(channel->Peer(), table));
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
        BgpMembershipManager *membership_mgr = a_->membership_mgr();
        return (!membership_mgr->IsRegistered(channel->Peer(), table));
    }

    bool PeerHasPendingInstanceMembershipRequests(BgpXmppChannel *channel) {
        return (!channel->instance_membership_request_map_.empty());
    }

    bool PeerHasPendingMembershipRequests(BgpXmppChannel *channel) {
        return (channel->table_membership_request_map_.size() != 0);
    }

    bool PeerCloseIsDeferred(BgpXmppChannel *channel) {
        return channel->defer_peer_close_;
    }

    size_t PeerDeferQSize(BgpXmppChannel *channel) {
        return channel->defer_q_.size();
    }

    void PausePeerRibMembershipManager() {
        a_->membership_mgr()->SetQueueDisable(true);
    }

    void ResumePeerRibMembershipManager() {
        a_->membership_mgr()->SetQueueDisable(false);
    }

    void PauseBgpXmppChannelManager() {
        bgp_channel_manager_->queue_.set_disable(true);
    }

    void ResumeBgpXmppChannelManager() {
        bgp_channel_manager_->queue_.set_disable(false);
    }

    int PeerInstanceSubscribe(BgpXmppChannel *channel) {
        return channel->channel_stats_.instance_subscribe;
    }

    int PeerInstanceUnsubscribe(BgpXmppChannel *channel) {
        return channel->channel_stats_.instance_unsubscribe;
    }

    int PeerTableSubscribeStart(BgpXmppChannel *channel) {
        return channel->channel_stats_.table_subscribe;
    }

    int PeerTableSubscribeComplete(BgpXmppChannel *channel) {
        return channel->channel_stats_.table_subscribe_complete;
    }

    int PeerTableUnsubscribeStart(BgpXmppChannel *channel) {
        return channel->channel_stats_.table_unsubscribe;
    }

    int PeerTableUnsubscribeComplete(BgpXmppChannel *channel) {
        return channel->channel_stats_.table_unsubscribe_complete;
    }

protected:
    BgpXmppUnitTest() : thread_(&evm_), xs_a_(NULL) { }

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
        TASK_UTIL_EXPECT_EQ(0, xs_a_->ConnectionCount());
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
        snprintf(config, sizeof(config), config_template_with_instances,
                 a_->session_manager()->GetPort());
        a_->Configure(config);
    }

    void ConfigureWithoutRoutingInstances() {
        char config[4096];
        snprintf(config, sizeof(config), config_template_without_instances,
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

    BgpXmppChannelMock *channel() { return bgp_channel_manager_->channel_; }

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

        TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
        TASK_UTIL_EXPECT_EQ(0, xs_a_->ConnectionCount());
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

static bool DummyWalkFunction(DBTablePartBase *root, DBEntryBase *entry) {
    return true;
}

static void DummyWalkDoneFunction(DBTableBase *table) {
}

int BgpXmppUnitTest::validate_done_;

namespace {

TEST_F(BgpXmppUnitTest, TableDeleteWithPendingWalk) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    // Pause deletion for blue inet table.
    BgpTable *blue_table = VerifyBgpTable("blue", Address::INET);
    PauseDelete(blue_table->deleter());

    // Unconfigure all instances.
    // The blue instance and blue inet table should exist in deleted state.
    UnconfigureRoutingInstances();
    task_util::WaitForIdle();
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    TASK_UTIL_EXPECT_TRUE(blue_table->IsDeleted());

    // Stop the scheduler and resume delete of the table.
    // This should create the bgp::Config Task to process lifetime manager
    // work queue.
    TaskScheduler::GetInstance()->Stop();
    blue_table->deleter()->ResumeDelete();

    // Start a bunch of table walks.
    DBTableWalker *walker = a_->database()->GetWalker();
    for (int idx = 0; idx < 128; ++idx) {
        walker->WalkTable(
            blue_table, NULL, DummyWalkFunction, DummyWalkDoneFunction);
    }

    // Start the scheduler.
    // Table should get deleted only after all the walks are done.
    TaskScheduler::GetInstance()->Start();

    // The blue instance should have been destroyed.
    VerifyNoRoutingInstance("blue");
}

TEST_F(BgpXmppUnitTest, Connection) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    // Wait upto 5 seconds
    BGP_DEBUG_UT("-- Executing --");
    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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

//
// Route ADD request in DBParition is handled after the peer unsubscribed
// from the VRF. This race condition is achieved by disabling the DBPartition
// while handling route add request and unsubscribe request
//
TEST_F(BgpXmppUnitTest, BasicDelayedInput) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Subscribe("blue", 1);
    task_util::WaitForIdle();
    agent_a_->AddRoute("blue", "10.1.1.2/32");
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 1);

    a_->database()->SetQueueDisable(true);
    agent_a_->AddRoute("blue", "10.1.1.1/32");
    task_util::WaitForIdle();

    agent_a_->Unsubscribe("blue", -1, false);
    task_util::WaitForIdle();

    // The unsubscribe request should have been processed by the membership
    // manager and a response returned.
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);

    a_->database()->SetQueueDisable(false);
    TASK_UTIL_EXPECT_TRUE(a_->database()->IsDBQueueEmpty());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);

    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

//
// Route ADD request in DBParition is handled after the peer unsubscribed
// and resubscribed to the VRF. Expected result is route added before previous
// unsubscribe is ignored and doesn't cause route add to VRF after subsequent
// subscription.
// This race condition is achieved by disabling the DBPartition
// while handling route add request and unsubscribe and subscribe request
//
TEST_F(BgpXmppUnitTest, BasicDelayedInput_1) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Subscribe("blue", 1);
    task_util::WaitForIdle();
    agent_a_->AddRoute("blue", "10.1.1.2/32");
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 1);

    a_->database()->SetQueueDisable(true);
    agent_a_->AddRoute("blue", "10.1.1.1/32");
    task_util::WaitForIdle();

    agent_a_->Unsubscribe("blue", -1, false);
    task_util::WaitForIdle();

    // The unsubscribe request should have been processed by the membership
    // manager and a response returned.
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);

    // Subscribe again
    agent_a_->Subscribe("blue", 1);
    agent_a_->Subscribe("red", 2);
    task_util::WaitForIdle();

    // The subscribe request should have been processed by the membership
    // manager and a response returned.
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "red", 2));

    agent_a_->AddRoute("blue", "10.1.1.3/32");

    // Enable DB Partition
    a_->database()->SetQueueDisable(false);
    TASK_UTIL_EXPECT_TRUE(a_->database()->IsDBQueueEmpty());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 2);

    agent_a_->Unsubscribe("blue", -1, false);
    agent_a_->Unsubscribe("red", -1, false);
    task_util::WaitForIdle();

    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

//
// Route DELETE request in DBParition is handled after the peer unsubscribed
// and resubscribed to the VRF.
// Route add of same prefix after the re-subscribe is not impacted due to
// delayed route delete in DBPartition request queue.
// This race condition is achieved by disabling the DBPartition
// while handling route delete request and unsubscribe and subscribe request
//
TEST_F(BgpXmppUnitTest, BasicDelayedInput_2) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Subscribe("blue", 1);
    task_util::WaitForIdle();
    agent_a_->AddRoute("blue", "10.1.1.2/32");
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 1);
    agent_a_->AddRoute("blue", "10.1.1.1/32");
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_EQ(2, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 2);

    a_->database()->SetQueueDisable(true);
    agent_a_->DeleteRoute("blue", "10.1.1.1/32");
    task_util::WaitForIdle();

    agent_a_->Unsubscribe("blue", -1, false);
    task_util::WaitForIdle();

    // The unsubscribe request should have been processed by the membership
    // manager and a response returned.
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);

    // Subscribe again
    agent_a_->Subscribe("blue", 1);
    agent_a_->Subscribe("red", 2);
    task_util::WaitForIdle();

    // The subscribe request should have been processed by the membership
    // manager and a response returned.
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "red", 2));

    agent_a_->AddRoute("blue", "10.1.1.1/32");

    // Enable DB Partition
    a_->database()->SetQueueDisable(false);
    TASK_UTIL_EXPECT_TRUE(a_->database()->IsDBQueueEmpty());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(2, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 2);

    agent_a_->Unsubscribe("blue", -1, false);
    agent_a_->Unsubscribe("red", -1, false);
    task_util::WaitForIdle();

    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

//
// Verify the case where route add is processed when peer membership manager is
// half way through the unregister process.
//
TEST_F(BgpXmppUnitTest, BasicDelayedInput_3) {
    DBTableWalker::SetIterationToYield(1);
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Subscribe("blue", 1);
    task_util::WaitForIdle();

    // Add multiple routes to each DBTablePartition
    for (int idx = 0; idx < 64; idx++) {
        string prefix = string("10.1.") + integerToString(idx / 255) +
            "." + integerToString(idx % 255) + "/32";
        agent_a_->AddRoute("blue", prefix);
    }
    TASK_UTIL_EXPECT_EQ(64, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 64);

    // Disable DB input processing
    a_->database()->SetQueueDisable(true);

    agent_a_->AddRoute("blue", "10.0.1.1/32");
    task_util::WaitForIdle();
    DBTableBase *blue_tbl = a_->database()->FindTable("blue.inet.0");
    uint64_t walk_count =  blue_tbl->walk_request_count();

    // Send unsubscribe for the VRF with pending route add
    agent_a_->Unsubscribe("blue", -1, false);

    // Enable the DB input partition when DB walk request for Leave is
    // in progress
    TASK_UTIL_EXPECT_NE(blue_tbl->walk_request_count(), walk_count);

    // Enable DB Partition
    a_->database()->SetQueueDisable(false);

    // The unsubscribe request should have been processed by the membership
    // manager and a response returned.
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);
    DBTableWalker::SetIterationToYield(DBTableWalker::kIterationToYield);
}

TEST_F(BgpXmppUnitTest, RegisterWithoutRoutingInstance) {
    ConfigureWithoutRoutingInstances();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Subscribe("red", 2);
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue","10.1.1.1/32");
    agent_a_->Subscribe(BgpConfigManager::kMasterInstance, -1);

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);
    TASK_UTIL_EXPECT_TRUE(
        PeerRegistered(channel(), BgpConfigManager::kMasterInstance, -1));

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
    ConfigureWithoutRoutingInstances();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Subscribe("red", 2);
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue","10.1.1.1/32");
    agent_a_->Subscribe(BgpConfigManager::kMasterInstance, -1);

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);
    TASK_UTIL_EXPECT_TRUE(
        PeerRegistered(channel(), BgpConfigManager::kMasterInstance, -1));

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
    ConfigureWithoutRoutingInstances();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    agent_a_->Subscribe(BgpConfigManager::kMasterInstance, -1);
    agent_a_->Subscribe("red", 2);
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue","10.3.1.1/32");

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);
    TASK_UTIL_EXPECT_TRUE(
        PeerRegistered(channel(), BgpConfigManager::kMasterInstance, -1));

    // unsubscribe request
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Unsubscribe("blue", -1, false);
    agent_a_->Unsubscribe(BgpConfigManager::kMasterInstance, -1, false);

    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);
    TASK_UTIL_EXPECT_TRUE(
        PeerNotRegistered(channel(), BgpConfigManager::kMasterInstance));

    Configure();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 0);

    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterRibInWithoutRoutingInstance) {
    ConfigureWithoutRoutingInstances();
    task_util::WaitForIdle();

    // Create an xmpp client in server A.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Subscribe agent A to blue before creating the blue instance.
    // Verify that agent A is not registered to blue table.
    agent_a_->Subscribe("blue", 1, true, true);
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Configure and create the blue instance.
    // Verify that agent is registered to blue table for RibIn but not RibOut.
    Configure();
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibIn(channel(), "blue"));
    TASK_UTIL_EXPECT_FALSE(PeerRegisteredRibOut(channel(), "blue"));

    // Unsubscribe agent A from the blue instance.
    agent_a_->Unsubscribe("blue");
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Verify instance subscribe/unsubscribe counts.
    TASK_UTIL_EXPECT_EQ(1, PeerInstanceSubscribe(channel()));
    TASK_UTIL_EXPECT_EQ(1, PeerInstanceUnsubscribe(channel()));

    // Verify table subscribe/unsubscribe counts.
    TASK_UTIL_EXPECT_EQ(4, PeerTableSubscribeStart(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableUnsubscribeStart(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableUnsubscribeComplete(channel()));

    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterWithDeletedRoutingInstance) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());

    // Resume deletion of blue instance and make sure it's gone.
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());

    // Configure instances and verify that the route has been added.
    Configure();
    VerifyRoutingInstance("red");
    VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") != NULL);
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));

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

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Subscribe and add route to deleted blue instance. Make sure that the
    // messages have been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue","10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));

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
    agent_a_->Unsubscribe("blue", -1, true, false);
    TASK_UTIL_EXPECT_EQ(3, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

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

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());

    // Unsubscribe from the blue instance and make sure that the unsubscribe
    // message has been processed on the bgp server.
    agent_a_->Unsubscribe("blue", -1, true, false);
    TASK_UTIL_EXPECT_EQ(3, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Resume deletion of blue instance and make sure it's gone.
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") == NULL);

    // Clean up.
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterRibInWithDeletedRoutingInstance) {
    Configure();
    task_util::WaitForIdle();

    // Create an xmpp client in server A.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());

    // Unconfigure all instances.
    // The blue instance should still exist in deleted state. All tables in
    // the blue instance should get destroyed.
    UnconfigureRoutingInstances();
    task_util::WaitForIdle();
    VerifyNoRoutingInstance("red");
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    TASK_UTIL_EXPECT_EQ(0, blue->GetTables().size());

    // Subscribe agent A to blue instance.
    // Verify that agent A is not registered to blue table.
    agent_a_->Subscribe("blue", 1, true, true);
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Configure and blue instance again.
    // Verify that agent A is not registered to blue table since the previous
    // incarnation of the instance has not yet been deleted.
    Configure();
    task_util::WaitForIdle();

    // Resume deletion of blue instance and make sure it gets recreated.
    ResumeDelete(blue->deleter());
    task_util::WaitForIdle();
    VerifyRoutingInstance("blue");

    // Verify that agent is registered to blue table for RibIn but not RibOut.
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibIn(channel(), "blue"));
    TASK_UTIL_EXPECT_FALSE(PeerRegisteredRibOut(channel(), "blue"));

    // Unsubscribe agent A from the blue instance.
    agent_a_->Unsubscribe("blue");
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Verify instance subscribe/unsubscribe counts.
    TASK_UTIL_EXPECT_EQ(1, PeerInstanceSubscribe(channel()));
    TASK_UTIL_EXPECT_EQ(1, PeerInstanceUnsubscribe(channel()));

    // Verify table subscribe/unsubscribe counts.
    TASK_UTIL_EXPECT_EQ(4, PeerTableSubscribeStart(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableUnsubscribeStart(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableUnsubscribeComplete(channel()));

    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterRibInUnregisterWithDeletedRoutingInstance) {
    Configure();
    task_util::WaitForIdle();

    // Create an xmpp client in server A.
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());

    // Unconfigure all instances.
    // The blue instance should still exist in deleted state. All tables in
    // the blue instance should get destroyed.
    UnconfigureRoutingInstances();
    task_util::WaitForIdle();
    VerifyNoRoutingInstance("red");
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());
    TASK_UTIL_EXPECT_EQ(0, blue->GetTables().size());

    // Subscribe agent A to blue instance.
    // Verify that agent A is not registered to blue table.
    agent_a_->Subscribe("blue", 1, true, true);
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Unsubscribe agent A from the blue instance.
    agent_a_->Unsubscribe("blue");
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Resume deletion of blue instance and make sure it's gone.
    ResumeDelete(blue->deleter());
    task_util::WaitForIdle();
    VerifyNoRoutingInstance("blue");

    // Verify instance subscribe/unsubscribe counts.
    TASK_UTIL_EXPECT_EQ(1, PeerInstanceSubscribe(channel()));
    TASK_UTIL_EXPECT_EQ(1, PeerInstanceUnsubscribe(channel()));

    // Verify table subscribe/unsubscribe counts.
    TASK_UTIL_EXPECT_EQ(0, PeerTableSubscribeStart(channel()));
    TASK_UTIL_EXPECT_EQ(0, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(0, PeerTableUnsubscribeStart(channel()));
    TASK_UTIL_EXPECT_EQ(0, PeerTableUnsubscribeComplete(channel()));

    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterAddDelAddRouteWithDeletedRoutingInstance) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Delete the route and make sure message is processed on bgp server.
    agent_a_->DeleteRoute("blue", "10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(3, channel()->Count());

    // Add the route again and make sure message is processed on bgp server.
    agent_a_->AddRoute("blue", "10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(4, channel()->Count());

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());

    // Resume deletion of blue instance and make sure it's gone.
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());

    // Configure instances and verify that the route has been added.
    Configure();
    VerifyRoutingInstance("red");
    VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") != NULL);
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));

    // Clean up.
    agent_a_->Unsubscribe("blue", -1, false);
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, DuplicateRegisterWithoutRoutingInstance) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Subscribe to non-existent green instance.
    agent_a_->Subscribe("green", 3);
    TASK_UTIL_EXPECT_FALSE(PeerRegistered(channel(), "green", 3));

    // Send a duplicate subscribe for the green instance.
    // This should trigger a Close from the server.
    agent_a_->Subscribe("green", 3);

    // Make sure session on agent flapped and instances are intact.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);
    VerifyRoutingInstance("blue");
    VerifyRoutingInstance("red");
}

TEST_F(BgpXmppUnitTest, DuplicateRegisterWithNonDeletedRoutingInstance) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Subscribe and add route to blue instance. Make sure that the messages
    // have been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue","10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") != NULL);

    // Send a duplicate subscribe for the blue instance.
    // This should trigger a Close from the server.
    agent_a_->Subscribe("blue", 1);

    // Make sure session on agent flapped and instances are intact.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);
    VerifyRoutingInstance("blue");
    VerifyRoutingInstance("red");
}

TEST_F(BgpXmppUnitTest, DuplicateRegisterWithDeletedRoutingInstance1) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Subscribe and add route to blue instance. Make sure that the messages
    // have been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue","10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(
        PeerRegistered(channel(), "blue", 1));
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") != NULL);

    // Unconfigure all instances.
    // The red instance should get destroyed while the blue instance should
    // still exist in deleted state.
    UnconfigureRoutingInstances();
    task_util::WaitForIdle();
    VerifyNoRoutingInstance("red");
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());

    // Send a duplicate subscribe for the blue instance.
    // This should trigger a Close from the server.
    agent_a_->Subscribe("blue", 1);

    // Make sure session on agent flapped and the blue instance is gone.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);
    VerifyNoRoutingInstance("blue");
}

TEST_F(BgpXmppUnitTest, DuplicateRegisterWithDeletedRoutingInstance2) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Pause deletion for blue instance.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());

    // Subscribe and add route to blue instance. Make sure that the messages
    // have been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue","10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") != NULL);

    // Unconfigure all instances.
    // The red instance should get destroyed while the blue instance should
    // still exist in deleted state.
    UnconfigureRoutingInstances();
    task_util::WaitForIdle();
    VerifyNoRoutingInstance("red");
    VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());

    // Send unsubscribe for the blue instance.
    agent_a_->Unsubscribe("blue", -1, true, false);
    TASK_UTIL_EXPECT_FALSE(PeerRegistered(channel(), "blue", 1));

    // Send back to back subscribe for the blue instance.
    // This should trigger a Close from the server.
    agent_a_->Subscribe("blue", 1);
    agent_a_->Subscribe("blue", 1);

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Resume deletion of blue instance and make sure it's gone.
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");
}

TEST_F(BgpXmppUnitTest, SpuriousUnregisterWithoutRoutingInstance) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Unsubscribe to non-existent green instance.
    agent_a_->Unsubscribe("green", -1);
    TASK_UTIL_EXPECT_FALSE(PeerRegistered(channel(), "green", 3));

    // Make sure session on agent flapped and instances are intact.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);
    VerifyRoutingInstance("blue");
    VerifyRoutingInstance("red");
}

TEST_F(BgpXmppUnitTest, SpuriousUnregisterWithNonDeletedRoutingInstance1) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Subscribe and add route to blue instance.
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue","10.1.1.1/32");
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") != NULL);

    // Send back to back unsubscribe for the blue instance.
    // This should trigger a Close from the server.
    agent_a_->Unsubscribe("blue", -1);
    agent_a_->Unsubscribe("blue", -1);

    // Make sure session on agent flapped and instances are intact.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);
    VerifyRoutingInstance("blue");
    VerifyRoutingInstance("red");
}

TEST_F(BgpXmppUnitTest, SpuriousUnregisterWithNonDeletedRoutingInstance2) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Send spurious unsubscribe for the blue instance.
    // This should trigger a Close from the server.
    agent_a_->Unsubscribe("blue", -1);

    // Make sure session on agent flapped and instances are intact.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);
    VerifyRoutingInstance("blue");
    VerifyRoutingInstance("red");
}

TEST_F(BgpXmppUnitTest, SpuriousUnregisterWithDeletedRoutingInstance1) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Pause deletion for blue instance and the inet table.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());
    BgpTable *blue_table = VerifyBgpTable("blue", Address::INET);
    PauseDelete(blue_table->deleter());

    // Subscribe and add route to blue instance.
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue","10.1.1.1/32");
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") != NULL);

    // Unconfigure all instances.
    // The red instance should get destroyed while the blue instance should
    // still exist in deleted state.
    UnconfigureRoutingInstances();
    task_util::WaitForIdle();
    VerifyNoRoutingInstance("red");
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());

    // Send back to back unsubscribe for the blue instance.
    // This should trigger a Close from the server.
    agent_a_->Unsubscribe("blue", -1);
    agent_a_->Unsubscribe("blue", -1);

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Resume deletion of blue inet table and blue instance and make sure
    // they are gone.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");
}

TEST_F(BgpXmppUnitTest, SpuriousUnregisterWithDeletedRoutingInstance2) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Pause deletion for blue instance and the inet table.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());
    BgpTable *blue_table = VerifyBgpTable("blue", Address::INET);
    PauseDelete(blue_table->deleter());

    // Unconfigure all instances.
    // The red instance should get destroyed while the blue instance should
    // still exist in deleted state.
    UnconfigureRoutingInstances();
    task_util::WaitForIdle();
    VerifyNoRoutingInstance("red");
    blue = VerifyRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(blue->deleted());

    // Send spurious unsubscribe for the blue instance.
    // This should trigger a Close from the server.
    agent_a_->Unsubscribe("blue", -1);

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Resume deletion of blue inet table and blue instance and make sure
    // they are gone.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");
}

TEST_F(BgpXmppUnitTest, RegisterWithDeletedBgpTable1) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
    TASK_UTIL_EXPECT_EQ(1, channel()->Count());
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

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

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
    TASK_UTIL_EXPECT_EQ(1, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

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
    // subscription state even though the table was marked deleted when the
    // subscribe was processed by it.
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibIn(channel(), "blue"));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibOut(channel(), "blue"));

    // Unsubscribe from blue instance.
    agent_a_->Unsubscribe("blue", -1, false);

    // Resume deletion of blue inet table and blue instance and make sure
    // they are gone.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");

    // Clean up.
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, UnregisterWithDeletedBgpTable1) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Subscribe to blue instance. Make sure that the message has
    // been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, channel()->Count());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));

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
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

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

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause deletion for blue instance and the inet table.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());
    BgpTable *blue_table = VerifyBgpTable("blue", Address::INET);
    PauseDelete(blue_table->deleter());

    // Subscribe to the blue instance. Make sure that the message has
    // been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, channel()->Count());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibOut(channel(), "blue"));

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Unsubscribe to the blue instance. Make sure that the message has
    // been processed on the bgp server. The unsubscribe request will
    // get enqueued in the membership manager, but won't be processed.
    agent_a_->Unsubscribe("blue");
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_FALSE(PeerRegisteredRibIn(channel(), "blue"));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibOut(channel(), "blue"));

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
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

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

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
    TASK_UTIL_EXPECT_EQ(1, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

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
    // subscription state even though the table was marked deleted when the
    // subscribe was processed by it.
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));

    // Unsubscribe for the blue instance.
    agent_a_->Unsubscribe("blue", -1, false);
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));

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

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
    TASK_UTIL_EXPECT_EQ(1, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

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
    // subscription state even though the table was marked deleted when the
    // subscribe was processed by it.
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Unsubscribe for the blue instance. The request will be queued to
    // the membership manager but won't be processed yet.
    agent_a_->Unsubscribe("blue", -1, false);
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));

    // Resume the peer membership manager.
    ResumePeerRibMembershipManager();
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));

    // Resume deletion of blue inet table and blue instance.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");
}

TEST_F(BgpXmppUnitTest, RegisterUnregisterWithDeletedBgpTable3) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
    TASK_UTIL_EXPECT_EQ(1, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

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
    // subscription state even though the table was marked deleted when the
    // subscribe was processed by it.
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibIn(channel(), "blue"));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibOut(channel(), "blue"));

    // Unsubscribe for the old incarnation.
    agent_a_->Unsubscribe("blue", -1, false);

    // Resume deletion of blue inet table and blue instance.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");

    // Clean up.
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterUnregisterWithDeletedBgpTable4) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
    TASK_UTIL_EXPECT_EQ(1, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

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
    // subscription state even though the table was marked deleted when the
    // subscribe was processed by it.
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));

    // Resume deletion of blue inet table and blue instance.
    // Verify that the blue table and instance are still present since
    // the membership manager has a reference to the table.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    VerifyRoutingInstance("blue");
    VerifyBgpTable("blue", Address::INET);

    // Unsubscribe for the blue instance and verify that the instance is
    // gone.
    agent_a_->Unsubscribe("blue", -1, false);
    VerifyNoRoutingInstance("blue");
}

TEST_F(BgpXmppUnitTest, RegisterAddDelAddRouteWithDeletedBgpTable) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
    TASK_UTIL_EXPECT_EQ(1, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Add, delete and add the same route.
    agent_a_->AddRoute("blue","10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    agent_a_->DeleteRoute("blue", "10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(3, channel()->Count());
    agent_a_->AddRoute("blue", "10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(4, channel()->Count());

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
    // manager and a response returned.  The membership manager will have
    // subscription state even though the table was marked deleted when the
    // subscribe was processed by it.
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));
    task_util::WaitForIdle();

    // Route shouldn't have been added.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());

    // Unsubscribe.
    agent_a_->Unsubscribe("blue", -1, false);

    // Resume deletion of blue inet table and blue instance and make sure
    // they are gone.
    ResumeDelete(blue_table->deleter());
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");

    // Clean up.
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterUnregisterWithDeletedBgpTableThenRegisterAgain1) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

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
    TASK_UTIL_EXPECT_EQ(4, PeerTableSubscribeComplete(channel()));

    // The subscribe request should have been processed by the membership
    // manager and a response returned.  The membership manager will have
    // subscription state even though the table was marked deleted when the
    // subscribe was processed by it.
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));

    // Unsubscribe for the old incarnation.
    agent_a_->Unsubscribe("blue", -1, false, false);
    TASK_UTIL_EXPECT_EQ(3, channel()->Count());
    TASK_UTIL_EXPECT_NE(0, PeerTableUnsubscribeComplete(channel()));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));

    // Subscribe for the new incarnation and add a route.  The subscribe
    // should get deferred till the routing instance is created again.
    // The route add should also get deferred.
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue", "10.1.1.2/32");
    TASK_UTIL_EXPECT_EQ(5, channel()->Count());
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingInstanceMembershipRequests(channel()));

    // Configure the routing instances again.  The peer should be not be
    // registered to the blue instance since the old instance and table
    // have not yet been deleted.
    Configure();
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingInstanceMembershipRequests(channel()));

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
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingInstanceMembershipRequests(channel()));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") == NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.2/32") != NULL);

    // Clean up.
    agent_a_->Unsubscribe("blue", -1, false);
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, RegisterUnregisterWithDeletedBgpTableThenRegisterAgain2) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

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
    // subscription state even though the table was marked deleted when the
    // subscribe was processed by it.
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));

    // Pause the peer membership manager again.
    PausePeerRibMembershipManager();

    // Unsubscribe for the old incarnation.  The unsubscribe request will get
    // enqueued in the membership manager, but won't be processed.
    agent_a_->Unsubscribe("blue", -1, false, false);
    TASK_UTIL_EXPECT_EQ(3, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibOut(channel(), "blue"));

    // Subscribe for the new incarnation and add a route.  The subscribe
    // should get deferred till the routing instance is created again.
    // The route add should also get deferred.
    agent_a_->Subscribe("blue", 1);
    agent_a_->AddRoute("blue", "10.1.1.2/32");
    TASK_UTIL_EXPECT_EQ(5, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingInstanceMembershipRequests(channel()));

    // Resume the peer membership manager.
    // The pending unsubscribes for the table(s) should get processed but the
    // pending instance membership should still be there because the instance
    // and table have not yet been deleted.
    ResumePeerRibMembershipManager();
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingInstanceMembershipRequests(channel()));

    // Configure the routing instances again.  The peer should be not be
    // registered to the blue instance since the old instance and table
    // have not yet been deleted.
    Configure();
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingInstanceMembershipRequests(channel()));

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
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingInstanceMembershipRequests(channel()));
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

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Subscribe to the blue instance. Make sure that the message has
    // been processed on the bgp server. The subscription request will
    // get enqueued in the membership manager, but won't be processed.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Bring the session down.  The close should get deferred since there
    // are pending membership requests.
    agent_a_->SessionDown();
    TASK_UTIL_EXPECT_TRUE(PeerCloseIsDeferred(channel()));

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

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Subscribe to the blue instance. Make sure that the message has
    // been processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, channel()->Count());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "blue", 1));

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Unsubscribe to the blue instance. Make sure that the message has
    // been processed on the bgp server. The unsubscribe request will
    // get enqueued in the membership manager, but won't be processed.
    agent_a_->Unsubscribe("blue");
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_FALSE(PeerRegisteredRibIn(channel(), "blue"));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibOut(channel(), "blue"));

    // Bring the session down.  The close should get deferred since there
    // are pending membership requests.
    agent_a_->SessionDown();
    TASK_UTIL_EXPECT_TRUE(PeerCloseIsDeferred(channel()));

    // Resume the peer membership manager.  This should cause the deferred
    // peer close to resume and finish.
    ResumePeerRibMembershipManager();
}

TEST_F(BgpXmppUnitTest, CreateRoutingInstanceWithPeerCloseInProgress1) {
    ConfigureWithoutRoutingInstances();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Nothing has been configured yet, so there should be no instances.
    VerifyNoRoutingInstance("red");
    VerifyNoRoutingInstance("blue");

    // Subscribe to non-existent blue instance. Make sure that the message is
    // processed on the bgp server.
    agent_a_->Subscribe("blue", 1);
    TASK_UTIL_EXPECT_EQ(1, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingInstanceMembershipRequests(channel()));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));

    // Pause the channel manager work queue.
    PauseBgpXmppChannelManager();

    // Bring the session down.  The peer close should not get deferred since
    // there are no pending membership requests in the channel.  However, the
    // channel cleanup won't finish because the channel manager work queue has
    // been paused.
    agent_a_->SessionDown();
    TASK_UTIL_EXPECT_TRUE(channel()->peer_deleted());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(PeerCloseIsDeferred(channel()));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingInstanceMembershipRequests(channel()));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));

    // Configure instances and verify that the instances are created.
    Configure();
    VerifyRoutingInstance("red");
    VerifyRoutingInstance("blue");

    // The channel should not be registered to the blue instance since the
    // instance was created after the session went down.  It shouldn't have
    // any pending membership requests either.
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingInstanceMembershipRequests(channel()));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));

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

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
    TASK_UTIL_EXPECT_EQ(1, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingInstanceMembershipRequests(channel()));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));

    // Resume deletion of blue instance and make sure it's gone.
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Pause the channel manager work queue.
    PauseBgpXmppChannelManager();

    // Bring the session down.  The peer close should not get deferred since
    // there are no pending membership requests in the channel.  However, the
    // channel cleanup won't finish because the channel manager work queue has
    // been paused.
    agent_a_->SessionDown();
    TASK_UTIL_EXPECT_TRUE(channel()->peer_deleted());
    task_util::WaitForIdle();
    TASK_UTIL_EXPECT_FALSE(PeerCloseIsDeferred(channel()));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingInstanceMembershipRequests(channel()));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));

    // Configure instances and verify that the instances are created.
    Configure();
    VerifyRoutingInstance("red");
    VerifyRoutingInstance("blue");

    // The channel should not be registered to the blue instance since the
    // instance was created after the session went down.  It shouldn't have
    // any pending membership requests either.
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingInstanceMembershipRequests(channel()));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));

    // Resume the channel manager work queue. This should cause the deferred
    // peer close to resume and finish.
    ResumeBgpXmppChannelManager();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, AddDeleteInetRouteWithoutRegister1) {
    ConfigureWithoutRoutingInstances();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Add a route without registering.
    agent_a_->AddRoute("blue", "10.1.1.1/32");

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Wait for the session to get established again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    old_flap_count = agent_a_->flap_count();

    // Delete a route without registering.
    agent_a_->DeleteRoute("blue", "10.1.1.1/32");

    // Make sure session on agent flapped again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    agent_a_->SessionDown();
}

TEST_F(BgpXmppUnitTest, AddDeleteInetRouteWithoutRegister2) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Add a route without registering.
    agent_a_->AddRoute("blue", "10.1.1.1/32");

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Wait for the session to get established again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    old_flap_count = agent_a_->flap_count();

    // Delete a route without registering.
    agent_a_->DeleteRoute("blue", "10.1.1.1/32");

    // Make sure session on agent flapped again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    agent_a_->SessionDown();
}

TEST_F(BgpXmppUnitTest, AddDeleteInetRouteWithoutRegister3) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Subscribe and unsubscribe to the blue instance. The requests will get
    // enqueued in the membership manager, but won't be processed.
    agent_a_->Subscribe("blue", 1);
    agent_a_->Unsubscribe("blue", 1);
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Add a route with pending unsubscribe.
    agent_a_->AddRoute("blue", "10.1.1.1/32");

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Wait for the session to get established again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    old_flap_count = agent_a_->flap_count();

    // Delete a route without registering.
    agent_a_->DeleteRoute("blue", "10.1.1.1/32");

    // Make sure session on agent flapped again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Resume the peer membership manager and bring down the session.
    ResumePeerRibMembershipManager();
    agent_a_->SessionDown();
}

TEST_F(BgpXmppUnitTest, DeleteInetRouteFromDeletedInstance) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    agent_a_->SubscribeAll("blue", 1);
    task_util::WaitForIdle();
    agent_a_->AddRoute("blue", "10.1.1.2/32");
    agent_a_->AddInet6Route("blue", "::ffff:1/128");
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());

    // Pause deletion for blue instance.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());

    UnconfigureRoutingInstances();
    task_util::WaitForIdle();

    agent_a_->DeleteRoute("blue", "10.1.1.2/32");
    agent_a_->DeleteInet6Route("blue", "::ffff:1/128");
    task_util::WaitForIdle();

    // Make sure session on agent did not flap.
    TASK_UTIL_EXPECT_EQ(old_flap_count, agent_a_->flap_count());

    // Resume deletion of the instance but instance should not get deleted
    // yet because table has not been unsubscribed yet.
    ResumeDelete(blue->deleter());
    VerifyRoutingInstance("blue");

    // Unsubscribe from the table and then expect the instance to get deleted.
    agent_a_->UnsubscribeAll("blue", -1);
    VerifyNoRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Route should have been deleted now.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, DeleteInetRouteAndUnsubscribeFromDeletedInstance) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    agent_a_->SubscribeAll("blue", 1);
    task_util::WaitForIdle();
    agent_a_->AddRoute("blue", "10.1.1.2/32");
    agent_a_->AddInet6Route("blue", "::ffff:1/128");
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());

    // Pause deletion for blue instance.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());

    UnconfigureRoutingInstances();
    task_util::WaitForIdle();

    // Delete route and unsubscribe from the table together, while the instance
    // is still under deletion.
    agent_a_->DeleteRoute("blue", "10.1.1.2/32");
    agent_a_->DeleteInet6Route("blue", "::ffff:1/128");
    agent_a_->UnsubscribeAll("blue", -1);
    task_util::WaitForIdle();

    // Make sure session on agent did not flap.
    TASK_UTIL_EXPECT_EQ(old_flap_count, agent_a_->flap_count());

    // Resume deletion of the instance and ensure that instance is deleted.
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Route should have been added now.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, UnsubscribeFromDeletedInstance) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    agent_a_->SubscribeAll("blue", 1);
    task_util::WaitForIdle();
    agent_a_->AddRoute("blue", "10.1.1.2/32");
    agent_a_->AddInet6Route("blue", "::ffff:1/128");
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_EQ(1, agent_a_->Inet6RouteCount());

    // Pause deletion for blue instance.
    RoutingInstance *blue = VerifyRoutingInstance("blue");
    PauseDelete(blue->deleter());

    UnconfigureRoutingInstances();
    task_util::WaitForIdle();

    // Unsubscribe from the instance, without explicitly deleting the routes.
    // Routes shall get deleted from BgpXmppChannel as part of resulting
    // table walk from membership manager.
    agent_a_->UnsubscribeAll("blue", -1, true, false);
    task_util::WaitForIdle();

    // Make sure session on agent did not flap.
    TASK_UTIL_EXPECT_EQ(old_flap_count, agent_a_->flap_count());

    // Resume deletion of the instance and ensure that instance is deleted.
    ResumeDelete(blue->deleter());
    VerifyNoRoutingInstance("blue");
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Route should have been added now.
    TASK_UTIL_EXPECT_EQ(0, agent_a_->RouteCount());
    TASK_UTIL_EXPECT_EQ(0, agent_a_->Inet6RouteCount());
    agent_a_->SessionDown();
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, AddDeleteInet6RouteWithoutRegister1) {
    ConfigureWithoutRoutingInstances();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Add a route without registering.
    agent_a_->AddInet6Route("blue", "::ffff:1/128");

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Wait for the session to get established again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    old_flap_count = agent_a_->flap_count();

    // Delete a route without registering.
    agent_a_->DeleteInet6Route("blue", "::ffff:1/128");

    // Make sure session on agent flapped again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    agent_a_->SessionDown();
}

TEST_F(BgpXmppUnitTest, AddDeleteInet6RouteWithoutRegister2) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Add a route without registering.
    agent_a_->AddInet6Route("blue", "::ffff:1/128");

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Wait for the session to get established again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    old_flap_count = agent_a_->flap_count();

    // Delete a route without registering.
    agent_a_->DeleteInet6Route("blue", "::ffff:1/128");

    // Make sure session on agent flapped again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    agent_a_->SessionDown();
}

TEST_F(BgpXmppUnitTest, AddDeleteInet6RouteWithoutRegister3) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Subscribe and unsubscribe to the blue instance. The requests will get
    // enqueued in the membership manager, but won't be processed.
    agent_a_->Subscribe("blue", 1);
    agent_a_->Unsubscribe("blue", 1);
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Add a route with pending unsubscribe.
    agent_a_->AddInet6Route("blue", "::ffff:1/128");

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Wait for the session to get established again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    old_flap_count = agent_a_->flap_count();

    // Delete a route without registering.
    agent_a_->DeleteInet6Route("blue", "::ffff:1/128");

    // Make sure session on agent flapped again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Resume the peer membership manager and bring down the session.
    ResumePeerRibMembershipManager();
    agent_a_->SessionDown();
}

TEST_F(BgpXmppUnitTest, AddDeleteMcastRouteWithoutRegister1) {
    ConfigureWithoutRoutingInstances();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Add a route without registering.
    agent_a_->AddMcastRoute("blue", "225.0.0.1,90.1.1.1", "10.1.1.1", "10-20");

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Wait for the session to get established again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    old_flap_count = agent_a_->flap_count();

    // Delete a route without registering.
    agent_a_->DeleteMcastRoute("blue", "225.0.0.1,90.1.1.1");

    // Make sure session on agent flapped again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    agent_a_->SessionDown();
}

TEST_F(BgpXmppUnitTest, AddDeleteMcastRouteWithoutRegister2) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Add a route without registering.
    agent_a_->AddMcastRoute("blue", "225.0.0.1,90.1.1.1", "10.1.1.1", "10-20");

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Wait for the session to get established again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    old_flap_count = agent_a_->flap_count();

    // Delete a route without registering.
    agent_a_->DeleteMcastRoute("blue", "225.0.0.1,90.1.1.1");

    // Make sure session on agent flapped again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    agent_a_->SessionDown();
}

TEST_F(BgpXmppUnitTest, AddDeleteMcastRouteWithoutRegister3) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Subscribe and unsubscribe to the blue instance. The requests will get
    // enqueued in the membership manager, but won't be processed.
    agent_a_->Subscribe("blue", 1);
    agent_a_->Unsubscribe("blue", 1);
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Add a route with pending unsubscribe.
    agent_a_->AddMcastRoute("blue", "225.0.0.1,90.1.1.1", "10.1.1.1", "10-20");

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Wait for the session to get established again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    old_flap_count = agent_a_->flap_count();

    // Delete a route without registering.
    agent_a_->DeleteMcastRoute("blue", "225.0.0.1,90.1.1.1");

    // Make sure session on agent flapped again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Resume the peer membership manager and bring down the session.
    ResumePeerRibMembershipManager();
    agent_a_->SessionDown();
}

TEST_F(BgpXmppUnitTest, AddDeleteEnetRouteWithoutRegister1) {
    ConfigureWithoutRoutingInstances();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Add a route without registering.
    agent_a_->AddEnetRoute("blue", "aa:0:0:0:0:01,10.1.1.1/32", "192.168.1.1");

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Wait for the session to get established again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    old_flap_count = agent_a_->flap_count();

    // Delete a route without registering.
    agent_a_->DeleteEnetRoute("blue", "aa:0:0:0:0:01,10.1.1.1/32");

    // Make sure session on agent flapped again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    agent_a_->SessionDown();
}

TEST_F(BgpXmppUnitTest, AddDeleteEnetRouteWithoutRegister2) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Add a route without registering.
    agent_a_->AddEnetRoute("blue", "aa:0:0:0:0:01,10.1.1.1/32", "192.168.1.1");

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Wait for the session to get established again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    old_flap_count = agent_a_->flap_count();

    // Delete a route without registering.
    agent_a_->DeleteEnetRoute("blue", "aa:0:0:0:0:01,10.1.1.1/32");

    // Make sure session on agent flapped again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    agent_a_->SessionDown();
}

TEST_F(BgpXmppUnitTest, AddDeleteEnetRouteWithoutRegister3) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    uint32_t old_flap_count = agent_a_->flap_count();

    // Pause the peer membership manager.
    PausePeerRibMembershipManager();

    // Subscribe and unsubscribe to the blue instance. The requests will get
    // enqueued in the membership manager, but won't be processed.
    agent_a_->Subscribe("blue", 1);
    agent_a_->Unsubscribe("blue", 1);
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));

    // Add a route with pending unsubscribe.
    agent_a_->AddEnetRoute("blue", "aa:0:0:0:0:01,10.1.1.1/32", "192.168.1.1");

    // Make sure session on agent flapped.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Wait for the session to get established again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    old_flap_count = agent_a_->flap_count();

    // Delete a route without registering.
    agent_a_->DeleteEnetRoute("blue", "aa:0:0:0:0:01,10.1.1.1/32");

    // Make sure session on agent flapped again.
    TASK_UTIL_EXPECT_TRUE(agent_a_->flap_count() > old_flap_count);

    // Resume the peer membership manager and bring down the session.
    ResumePeerRibMembershipManager();
    agent_a_->SessionDown();
}

TEST_F(BgpXmppSerializeMembershipReqTest, SerializedMembershipReq1) {
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    agent_a_->Subscribe("red", 1, false);
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false);
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red"));
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

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(),
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

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "red", 2));
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

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red"));
    BGP_VERIFY_ROUTE_COUNT(
        a_->routing_instance_mgr()->GetRoutingInstance("red")->GetTable(
                           Address::INET), 0);
}

TEST_F(BgpXmppSerializeMembershipReqTest, SerializedMembershipReq5) {
    agent_a_->Subscribe("red", 1, false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(),
                                         "red", 1));

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 3, false);
    agent_a_->AddRoute("red","10.1.1.1/32");
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "red", 3));
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
    ASSERT_TRUE(agent_a_->RouteCount() == 1);
}

TEST_F(BgpXmppSerializeMembershipReqTest, SerializedMembershipReq6) {
    agent_a_->Subscribe("red", 1, false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "red", 1));

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 3, false);
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false);
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red"));
    BGP_VERIFY_ROUTE_COUNT(
        a_->routing_instance_mgr()->GetRoutingInstance("red")->GetTable(
                           Address::INET), 0);
}

TEST_F(BgpXmppSerializeMembershipReqTest, SerializedMembershipReq7) {
    agent_a_->Subscribe("red", 1, false);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "red", 1));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibIn(channel(), "red"));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibOut(channel(), "red"));

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 3, false, true);
    agent_a_->AddRoute("red", "10.1.1.1/32");
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "red", 3));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibIn(channel(), "red"));
    TASK_UTIL_EXPECT_FALSE(PeerRegisteredRibOut(channel(), "red"));

    BGP_VERIFY_ROUTE_COUNT(
        a_->routing_instance_mgr()->GetRoutingInstance("red")->GetTable(
                           Address::INET), 1);
}

TEST_F(BgpXmppSerializeMembershipReqTest, SerializedMembershipReq8) {
    agent_a_->Subscribe("red", 1, false, true);
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "red", 1));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibIn(channel(), "red"));
    TASK_UTIL_EXPECT_FALSE(PeerRegisteredRibOut(channel(), "red"));

    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Stop();
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 3, false);
    agent_a_->AddRoute("red", "10.1.1.1/32");
    scheduler->Start();
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "red", 3));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibIn(channel(), "red"));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibOut(channel(), "red"));

    BGP_VERIFY_ROUTE_COUNT(
        a_->routing_instance_mgr()->GetRoutingInstance("red")->GetTable(
                           Address::INET), 1);
}

TEST_F(BgpXmppSerializeMembershipReqTest, MembershipRequestStateMachine1) {
    PausePeerRibMembershipManager();
    agent_a_->Subscribe("red", 1, false);
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false);
    TASK_UTIL_EXPECT_EQ(1, PeerInstanceSubscribe(channel()));
    TASK_UTIL_EXPECT_EQ(1, PeerInstanceUnsubscribe(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    ResumePeerRibMembershipManager();

    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableUnsubscribeComplete(channel()));

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red"));
    BgpTable *red_table = VerifyBgpTable("red", Address::INET);
    BGP_VERIFY_ROUTE_COUNT(red_table, 0);
}

TEST_F(BgpXmppSerializeMembershipReqTest, MembershipRequestStateMachine2) {
    PausePeerRibMembershipManager();
    agent_a_->Subscribe("red", 1, false);
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 2, false);
    TASK_UTIL_EXPECT_EQ(2, PeerInstanceSubscribe(channel()));
    TASK_UTIL_EXPECT_EQ(1, PeerInstanceUnsubscribe(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    ResumePeerRibMembershipManager();

    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(0, PeerTableUnsubscribeComplete(channel()));

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "red", 2));
    BgpTable *red_table = VerifyBgpTable("red", Address::INET);
    BGP_VERIFY_ROUTE_COUNT(red_table, 0);
}

TEST_F(BgpXmppSerializeMembershipReqTest, MembershipRequestStateMachine3) {
    PausePeerRibMembershipManager();
    agent_a_->Subscribe("red", 1, false);
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 2, false);
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 2, false);
    agent_a_->AddRoute("red","10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(3, PeerInstanceSubscribe(channel()));
    TASK_UTIL_EXPECT_EQ(2, PeerInstanceUnsubscribe(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    ResumePeerRibMembershipManager();

    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(0, PeerTableUnsubscribeComplete(channel()));

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "red", 2));
    BgpTable *red_table = VerifyBgpTable("red", Address::INET);
    BGP_VERIFY_ROUTE_COUNT(red_table, 1);
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
}

TEST_F(BgpXmppSerializeMembershipReqTest, MembershipRequestStateMachine4) {
    PausePeerRibMembershipManager();
    agent_a_->Subscribe("red", 1, false);
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 2, false);
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 3, false);
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false);
    TASK_UTIL_EXPECT_EQ(3, PeerInstanceSubscribe(channel()));
    TASK_UTIL_EXPECT_EQ(3, PeerInstanceUnsubscribe(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    ResumePeerRibMembershipManager();

    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableUnsubscribeComplete(channel()));

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red"));
    BgpTable *red_table = VerifyBgpTable("red", Address::INET);
    BGP_VERIFY_ROUTE_COUNT(red_table, 0);
}

TEST_F(BgpXmppSerializeMembershipReqTest, MembershipRequestStateMachine5) {
    agent_a_->Subscribe("red", 1, false);
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "red", 1));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));

    TASK_UTIL_EXPECT_EQ(4, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(0, PeerTableUnsubscribeComplete(channel()));

    PausePeerRibMembershipManager();
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 3, false);
    agent_a_->AddRoute("red","10.1.1.1/32");
    TASK_UTIL_EXPECT_EQ(2, PeerInstanceSubscribe(channel()));
    TASK_UTIL_EXPECT_EQ(1, PeerInstanceUnsubscribe(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    ResumePeerRibMembershipManager();

    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_EQ(8, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableUnsubscribeComplete(channel()));

    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "red", 3));
    BgpTable *red_table = VerifyBgpTable("red", Address::INET);
    BGP_VERIFY_ROUTE_COUNT(red_table, 1);
    TASK_UTIL_EXPECT_EQ(1, agent_a_->RouteCount());
}

TEST_F(BgpXmppSerializeMembershipReqTest, MembershipRequestStateMachine6) {
    agent_a_->Subscribe("red", 1, false);
    TASK_UTIL_EXPECT_TRUE(PeerRegistered(channel(), "red", 1));
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));

    TASK_UTIL_EXPECT_EQ(4, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(0, PeerTableUnsubscribeComplete(channel()));

    PausePeerRibMembershipManager();
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 3, false);
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false);
    TASK_UTIL_EXPECT_EQ(2, PeerInstanceSubscribe(channel()));
    TASK_UTIL_EXPECT_EQ(2, PeerInstanceUnsubscribe(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    ResumePeerRibMembershipManager();

    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableUnsubscribeComplete(channel()));

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red"));
    BgpTable *red_table = VerifyBgpTable("red", Address::INET);
    BGP_VERIFY_ROUTE_COUNT(red_table, 0);
}

TEST_F(BgpXmppSerializeMembershipReqTest, MembershipRequestStateMachine7) {
    PausePeerRibMembershipManager();
    agent_a_->Subscribe("red", 1, false);
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 2, false, true);
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false);
    TASK_UTIL_EXPECT_EQ(2, PeerInstanceSubscribe(channel()));
    TASK_UTIL_EXPECT_EQ(2, PeerInstanceUnsubscribe(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    ResumePeerRibMembershipManager();

    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableUnsubscribeComplete(channel()));

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red"));
    BgpTable *red_table = VerifyBgpTable("red", Address::INET);
    BGP_VERIFY_ROUTE_COUNT(red_table, 0);
}

TEST_F(BgpXmppSerializeMembershipReqTest, MembershipRequestStateMachine8) {
    PausePeerRibMembershipManager();
    agent_a_->Subscribe("red", 1, false, true);
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 2, false);
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false);
    TASK_UTIL_EXPECT_EQ(2, PeerInstanceSubscribe(channel()));
    TASK_UTIL_EXPECT_EQ(2, PeerInstanceUnsubscribe(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    ResumePeerRibMembershipManager();

    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableUnsubscribeComplete(channel()));

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red"));
    BgpTable *red_table = VerifyBgpTable("red", Address::INET);
    BGP_VERIFY_ROUTE_COUNT(red_table, 0);
}

TEST_F(BgpXmppSerializeMembershipReqTest, MembershipRequestStateMachine9) {
    PausePeerRibMembershipManager();
    agent_a_->Subscribe("red", 1, false);
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 2, false, true);
    TASK_UTIL_EXPECT_EQ(2, PeerInstanceSubscribe(channel()));
    TASK_UTIL_EXPECT_EQ(1, PeerInstanceUnsubscribe(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    ResumePeerRibMembershipManager();

    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_EQ(8, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableUnsubscribeComplete(channel()));

    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibIn(channel(), "red"));
    TASK_UTIL_EXPECT_FALSE(PeerRegisteredRibOut(channel(), "red"));
    BgpTable *red_table = VerifyBgpTable("red", Address::INET);
    BGP_VERIFY_ROUTE_COUNT(red_table, 0);
}

TEST_F(BgpXmppSerializeMembershipReqTest, MembershipRequestStateMachine10) {
    PausePeerRibMembershipManager();
    agent_a_->Subscribe("red", 1, false, true);
    agent_a_->AddRoute("red","10.1.1.1/32");
    agent_a_->Unsubscribe("red", -1, false);
    agent_a_->Subscribe("red", 2, false);
    TASK_UTIL_EXPECT_EQ(2, PeerInstanceSubscribe(channel()));
    TASK_UTIL_EXPECT_EQ(1, PeerInstanceUnsubscribe(channel()));
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    ResumePeerRibMembershipManager();

    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
    TASK_UTIL_EXPECT_EQ(8, PeerTableSubscribeComplete(channel()));
    TASK_UTIL_EXPECT_EQ(4, PeerTableUnsubscribeComplete(channel()));

    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibIn(channel(), "red"));
    TASK_UTIL_EXPECT_TRUE(PeerRegisteredRibOut(channel(), "red"));
    BgpTable *red_table = VerifyBgpTable("red", Address::INET);
    BGP_VERIFY_ROUTE_COUNT(red_table, 0);
}

TEST_F(BgpXmppSerializeMembershipReqTest, FlushDeferQForVrfAndTable1) {
    PausePeerRibMembershipManager();

    agent_a_->Subscribe("blue", 1, false);
    agent_a_->Subscribe("red", 2, false);
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red"));

    agent_a_->AddRoute("red", "10.1.1.1/32");
    agent_a_->AddRoute("red", "10.1.1.2/32");
    agent_a_->AddRoute("blue", "10.1.1.1/32");
    agent_a_->AddRoute("blue", "10.1.1.2/32");
    TASK_UTIL_EXPECT_EQ(6, channel()->Count());
    TASK_UTIL_EXPECT_EQ(4, PeerDeferQSize(channel()));

    agent_a_->Unsubscribe("blue", -1, false, false);
    TASK_UTIL_EXPECT_EQ(7, channel()->Count());
    TASK_UTIL_EXPECT_EQ(2, PeerDeferQSize(channel()));

    agent_a_->Unsubscribe("red", -1, false, false);
    TASK_UTIL_EXPECT_EQ(8, channel()->Count());
    TASK_UTIL_EXPECT_EQ(0, PeerDeferQSize(channel()));

    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    ResumePeerRibMembershipManager();
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
}

TEST_F(BgpXmppSerializeMembershipReqTest, FlushDeferQForVrfAndTable2) {
    PausePeerRibMembershipManager();

    agent_a_->Subscribe("blue", 1, false);
    agent_a_->Subscribe("red", 2, false);
    TASK_UTIL_EXPECT_EQ(2, channel()->Count());
    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "blue"));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red"));

    agent_a_->AddRoute("red", "10.1.1.1/32");
    agent_a_->AddRoute("red", "10.1.1.2/32");
    agent_a_->AddRoute("blue", "10.1.1.1/32");
    agent_a_->AddRoute("blue", "10.1.1.2/32");
    TASK_UTIL_EXPECT_EQ(6, channel()->Count());
    TASK_UTIL_EXPECT_EQ(4, PeerDeferQSize(channel()));

    agent_a_->Unsubscribe("red", -1, false, false);
    TASK_UTIL_EXPECT_EQ(7, channel()->Count());
    TASK_UTIL_EXPECT_EQ(2, PeerDeferQSize(channel()));

    agent_a_->Unsubscribe("blue", -1, false, false);
    TASK_UTIL_EXPECT_EQ(8, channel()->Count());
    TASK_UTIL_EXPECT_EQ(0, PeerDeferQSize(channel()));

    TASK_UTIL_EXPECT_TRUE(PeerHasPendingMembershipRequests(channel()));
    ResumePeerRibMembershipManager();
    TASK_UTIL_EXPECT_FALSE(PeerHasPendingMembershipRequests(channel()));
}

TEST_F(BgpXmppSerializeMembershipReqTest, FlushDeferQForVrf1) {
    agent_a_->Subscribe("red1", 1, false);
    agent_a_->Subscribe("red2", 2, false);
    agent_a_->Subscribe("red3", 3, false);
    TASK_UTIL_EXPECT_EQ(3, channel()->Count());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red1"));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red2"));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red3"));

    agent_a_->AddRoute("red2", "10.1.1.1/32");
    agent_a_->AddRoute("red2", "10.1.1.2/32");
    agent_a_->AddRoute("red3", "10.1.1.1/32");
    agent_a_->AddRoute("red3", "10.1.1.2/32");
    agent_a_->AddRoute("red1", "10.1.1.1/32");
    agent_a_->AddRoute("red1", "10.1.1.2/32");
    TASK_UTIL_EXPECT_EQ(9, channel()->Count());
    TASK_UTIL_EXPECT_EQ(6, PeerDeferQSize(channel()));

    agent_a_->Unsubscribe("red1", -1, false, false);
    TASK_UTIL_EXPECT_EQ(10, channel()->Count());
    TASK_UTIL_EXPECT_EQ(4, PeerDeferQSize(channel()));

    agent_a_->Unsubscribe("red2", -1, false, false);
    TASK_UTIL_EXPECT_EQ(11, channel()->Count());
    TASK_UTIL_EXPECT_EQ(2, PeerDeferQSize(channel()));

    agent_a_->Unsubscribe("red3", -1, false, false);
    TASK_UTIL_EXPECT_EQ(12, channel()->Count());
    TASK_UTIL_EXPECT_EQ(0, PeerDeferQSize(channel()));
}

TEST_F(BgpXmppSerializeMembershipReqTest, FlushDeferQForVrf2) {
    agent_a_->Subscribe("red1", 1, false);
    agent_a_->Subscribe("red2", 2, false);
    agent_a_->Subscribe("red3", 3, false);
    TASK_UTIL_EXPECT_EQ(3, channel()->Count());
    task_util::WaitForIdle();

    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red1"));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red2"));
    TASK_UTIL_EXPECT_TRUE(PeerNotRegistered(channel(), "red3"));

    agent_a_->AddRoute("red2", "10.1.1.1/32");
    agent_a_->AddRoute("red2", "10.1.1.2/32");
    agent_a_->AddRoute("red3", "10.1.1.1/32");
    agent_a_->AddRoute("red3", "10.1.1.2/32");
    agent_a_->AddRoute("red1", "10.1.1.1/32");
    agent_a_->AddRoute("red1", "10.1.1.2/32");
    TASK_UTIL_EXPECT_EQ(9, channel()->Count());
    TASK_UTIL_EXPECT_EQ(6, PeerDeferQSize(channel()));

    agent_a_->Unsubscribe("red2", -1, false, false);
    TASK_UTIL_EXPECT_EQ(10, channel()->Count());
    TASK_UTIL_EXPECT_EQ(4, PeerDeferQSize(channel()));

    agent_a_->Unsubscribe("red1", -1, false, false);
    TASK_UTIL_EXPECT_EQ(11, channel()->Count());
    TASK_UTIL_EXPECT_EQ(2, PeerDeferQSize(channel()));

    agent_a_->Unsubscribe("red3", -1, false, false);
    TASK_UTIL_EXPECT_EQ(12, channel()->Count());
    TASK_UTIL_EXPECT_EQ(0, PeerDeferQSize(channel()));
}

TEST_F(BgpXmppUnitTest, BgpXmppBadAddress) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    TASK_UTIL_EXPECT_TRUE(channel() != NULL);
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
    BgpServer::Initialize();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpServerTest::GlobalSetUp();
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
