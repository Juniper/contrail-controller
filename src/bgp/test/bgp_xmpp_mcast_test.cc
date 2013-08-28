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
#include "bgp/bgp_af.h"
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
#include "bgp/bgp_multicast.h"
#include "bgp/inetmcast/inetmcast_route.h"
#include "bgp/inetmcast/inetmcast_table.h"
#include "io/test/event_manager_test.h"
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"


#include "schema/bgp_l3vpn_unicast_types.h"

#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_state_machine.h"

#include "xml/xml_pugi.h"

#include "testing/gunit.h"

using namespace boost::asio;
using namespace std;
using namespace autogen;

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
    size_t mcast_count_;
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


class BgpXmppUnitTest : public ::testing::Test {
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
        LOG(DEBUG, "Created server at port: " << 
                a_->session_manager()->GetPort());
        xs_a_->Initialize(0, false);

        bgp_channel_manager_.reset(
            new BgpXmppChannelManagerMock(xs_a_, a_.get()));

        thread_.Start();
    }

    virtual void TearDown() {
        a_->Shutdown();
        task_util::WaitForIdle();
        xs_a_->Shutdown();
        task_util::WaitForIdle();
        bgp_channel_manager_.reset();
        TcpServerManager::DeleteServer(xs_a_);
        xs_a_ = NULL;
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
                 a_->session_manager()->GetPort());
        a_->Configure(config);
    }

    static void ValidateShowRouteResponse(Sandesh *sandesh, vector<size_t> &result) {
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

    EventManager evm_;
    ServerThread thread_;
    auto_ptr<BgpServerTest> a_;
    XmppServer *xs_a_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_b_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_c_;
    boost::scoped_ptr<BgpXmppChannelManagerMock> bgp_channel_manager_;

    static int validate_done_;
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
</config>\
";

#define WAIT_EQ(expected, actual) \
    TASK_UTIL_EXPECT_EQ(expected, actual)
#define WAIT_NE(expected, actual) \
    TASK_UTIL_EXPECT_NE(expected, actual)

namespace {

TEST_F(BgpXmppUnitTest, 3Agent_Multicast_SG_TEST) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client connected to XMPP server A 
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_a_->GetPort(), "127.0.0.1"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // create an XMPP client connected to XMPP server A 
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_a_->GetPort(), "127.0.0.2"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // create an XMPP client connected to XMPP server A 
    agent_c_.reset(
        new test::NetworkAgentMock(&evm_, "agent-c", xs_a_->GetPort(), "127.0.0.3"));
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    //Ensure 3 bgp xmpp channel creation
    TASK_UTIL_EXPECT_EQ(3, bgp_channel_manager_->Count());

    // Register to both unicast and multicast table
    agent_a_->Subscribe("blue", 1); 
    agent_b_->Subscribe("blue", 1); 
    agent_c_->Subscribe("blue", 1); 
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_[0]->Count());
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_[1]->Count());
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->channel_[2]->Count());

    // Multicast Route Entry Add
    stringstream mroute; 
    mroute << BgpAf::IPv4 << "/" << BgpAf::Mcast << "/" << "225.0.0.1,10.1.1.1";
    agent_a_->AddMcastRoute("blue", mroute.str(), "7.7.7.7", "10000-20000"); 
    task_util::WaitForIdle();
    WAIT_EQ(2, bgp_channel_manager_->channel_[0]->Count());

    // Multicast Route Entry Add
    agent_b_->AddMcastRoute("blue", mroute.str(), "8.8.8.8", "40000-60000"); 
    WAIT_EQ(2, bgp_channel_manager_->channel_[1]->Count());

    // Multicast Route Entry Add
    agent_c_->AddMcastRoute("blue", mroute.str(), "9.9.9.9", "60000-80000"); 
    WAIT_EQ(2, bgp_channel_manager_->channel_[2]->Count());
    task_util::WaitForIdle();

    //Verify route in the multicast table via sandesh
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    sandesh_context.xmpp_peer_manager = bgp_channel_manager_.get();
    Sandesh::set_client_context(&sandesh_context);
    std::vector<size_t> result = list_of(3);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    ShowRouteReq *show_req = new ShowRouteReq;
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    WAIT_EQ(1, validate_done_);

    //show route for a table
    result = list_of(3);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.inetmcast.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    WAIT_EQ(1, validate_done_);

    // Multicast Route Entry Delete 
    agent_a_->DeleteMcastRoute("blue", mroute.str()); 
    WAIT_EQ(3, bgp_channel_manager_->channel_[0]->Count());
    task_util::WaitForIdle();

    // Multicast Route Entry Delete 
    agent_b_->DeleteMcastRoute("blue", mroute.str()); 
    WAIT_EQ(3, bgp_channel_manager_->channel_[1]->Count());
    task_util::WaitForIdle();

    // Multicast Route Entry Delete 
    agent_c_->DeleteMcastRoute("blue", mroute.str()); 
    WAIT_EQ(3, bgp_channel_manager_->channel_[2]->Count());
    task_util::WaitForIdle();

    //TODO Verify route delete received at agent_a and agent_b

    //Verify route deleted in the mutlicast table via sandesh
    result.resize(0);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.inetmcast.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    WAIT_EQ(1, validate_done_);

    //trigger a TCP close event on the server
    agent_a_->SessionDown();
    agent_b_->SessionDown();
    agent_c_->SessionDown();
    WAIT_EQ(6, bgp_channel_manager_->Count());
    //wait for channel to be deleted in the context of config-task
    task_util::WaitForIdle();

};

TEST_F(BgpXmppUnitTest, 3Agent_Multicast_G_TEST) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client connected to XMPP server A 
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_a_->GetPort(), "127.0.0.4"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // create an XMPP client connected to XMPP server A 
    agent_b_.reset(
        new test::NetworkAgentMock(&evm_, "agent-b", xs_a_->GetPort(), "127.0.0.5"));
    TASK_UTIL_EXPECT_TRUE(agent_b_->IsEstablished());

    // create an XMPP client connected to XMPP server A 
    agent_c_.reset(
        new test::NetworkAgentMock(&evm_, "agent-c", xs_a_->GetPort(), "127.0.0.6"));
    TASK_UTIL_EXPECT_TRUE(agent_c_->IsEstablished());

    //Ensure 3 bgp xmpp channel creation
    TASK_UTIL_EXPECT_EQ(3, bgp_channel_manager_->Count());

    // Register to both unicast and multicast table
    agent_a_->Subscribe("blue", 1); 
    agent_b_->Subscribe("blue", 1); 
    agent_c_->Subscribe("blue", 1); 
    TASK_UTIL_EXPECT_EQ(1,bgp_channel_manager_->channel_[0]->Count());
    TASK_UTIL_EXPECT_EQ(1,bgp_channel_manager_->channel_[1]->Count());
    TASK_UTIL_EXPECT_EQ(1,bgp_channel_manager_->channel_[2]->Count());

    // Multicast Route Entry Add
    stringstream mroute;
    mroute << BgpAf::IPv4 << "/" << BgpAf::Mcast << "/" << "225.0.0.1,10.1.1.1";
    agent_a_->AddMcastRoute("blue", mroute.str(), "7.7.7.7", "10000-20000"); 
    WAIT_EQ(2, bgp_channel_manager_->channel_[0]->Count());

    // Multicast Route Entry Add
    agent_b_->AddMcastRoute("blue", mroute.str(), "8.8.8.8", "40000-60000"); 
    WAIT_EQ(2, bgp_channel_manager_->channel_[1]->Count());

    // Multicast Route Entry Add
    agent_c_->AddMcastRoute("blue", mroute.str(), "9.9.9.9", "60000-80000"); 
    WAIT_EQ(2, bgp_channel_manager_->channel_[2]->Count());
    task_util::WaitForIdle();

    //Verify route in the multicast table via sandesh
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    sandesh_context.xmpp_peer_manager = bgp_channel_manager_.get();
    Sandesh::set_client_context(&sandesh_context);
    std::vector<size_t> result = list_of(3);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    ShowRouteReq *show_req = new ShowRouteReq;
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    WAIT_EQ(1, validate_done_);

    //show route for a table
    result = list_of(3);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.inetmcast.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    WAIT_EQ(1, validate_done_);

    // Multicast Route Entry Delete 
    agent_a_->DeleteMcastRoute("blue", mroute.str()); 
    WAIT_EQ(3, bgp_channel_manager_->channel_[0]->Count());
    task_util::WaitForIdle();

    // Multicast Route Entry Delete 
    agent_b_->DeleteMcastRoute("blue", mroute.str()); 
    WAIT_EQ(3, bgp_channel_manager_->channel_[1]->Count());
    task_util::WaitForIdle();

    // Multicast Route Entry Delete 
    agent_c_->DeleteMcastRoute("blue", mroute.str()); 
    WAIT_EQ(3, bgp_channel_manager_->channel_[2]->Count());
    task_util::WaitForIdle();

    //TODO Verify route delete received at agent_a and agent_b

    //Verify route deleted in the mutlicast table via sandesh
    result.resize(0);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.inetmcast.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    WAIT_EQ(1, validate_done_);

    //trigger a TCP close event on the server
    agent_a_->SessionDown();
    agent_b_->SessionDown();
    agent_c_->SessionDown();
    WAIT_EQ(6, bgp_channel_manager_->Count());
    //wait for channel to be deleted in the context of config-task
    task_util::WaitForIdle();

};

TEST_F(BgpXmppUnitTest, Multicast_PendingSubscribe_Test) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client connected to XMPP server A 
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_a_->GetPort(), "127.0.0.4"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    //Ensure 1 bgp xmpp channel creation
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->Count());

    // Register to both unicast and multicast table
    agent_a_->Subscribe("blue", 1); 
    // Multicast Route Entry Add
    stringstream mroute;
    mroute << BgpAf::IPv4 << "/" << BgpAf::Mcast << "/" << "225.0.0.1";
    agent_a_->AddMcastRoute("blue", mroute.str(), "7.7.7.7", "10000-20000"); 
    WAIT_EQ(2, bgp_channel_manager_->channel_[0]->Count());
    task_util::WaitForIdle();

    //Verify route in the multicast table via sandesh
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    sandesh_context.xmpp_peer_manager = bgp_channel_manager_.get();
    Sandesh::set_client_context(&sandesh_context);
    std::vector<size_t> result = list_of(1);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    ShowRouteReq *show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.inetmcast.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    WAIT_EQ(1, validate_done_);

    //Verify route in the inetmcast table 
    InetMcastTable *blue_table_ = static_cast<InetMcastTable *>(
            a_->database()->FindTable("blue.inetmcast.0"));
    ostringstream route;
    route << "127.0.0.4:1:225.0.0.1,0.0.0.0";
    InetMcastPrefix prefix(InetMcastPrefix::FromString(route.str()));
    InetMcastTable::RequestKey key(prefix, NULL);
    TASK_UTIL_EXPECT_TRUE(
            dynamic_cast<InetMcastRoute *>(blue_table_->Find(&key)) != NULL);

    // Multicast Route Entry Delete 
    agent_a_->DeleteMcastRoute("blue", mroute.str()); 
    WAIT_EQ(3, bgp_channel_manager_->channel_[0]->Count());
    task_util::WaitForIdle();

    //Verify route deleted in the mutlicast table via sandesh
    result.resize(0);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));
    show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.inetmcast.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    WAIT_EQ(1, validate_done_);

    //trigger a TCP close event on the server
    agent_a_->SessionDown();
    WAIT_EQ(2, bgp_channel_manager_->Count());
    //wait for channel to be deleted in the context of config-task
    task_util::WaitForIdle();
}

TEST_F(BgpXmppUnitTest, Multicast_PendingUnSubscribe_Test) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client connected to XMPP server A 
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_a_->GetPort(), "127.0.0.4"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    //Ensure 1 bgp xmpp channel creation
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->Count());

    // Register to both unicast and multicast table
    agent_a_->Subscribe("blue", 1); 
    // Multicast Route Entry Add
    stringstream mroute;
    mroute << BgpAf::IPv4 << "/" << BgpAf::Mcast << "/" << "225.0.0.1";
    agent_a_->AddMcastRoute("blue", mroute.str(), "7.7.7.7", "10000-20000"); 
    agent_a_->Unsubscribe("blue"); 
    WAIT_EQ(3, bgp_channel_manager_->channel_[0]->Count());

    //Verify no routes in the inetmcast table 
    InetMcastTable *blue_table_ = static_cast<InetMcastTable *>(
            a_->database()->FindTable("blue.inetmcast.0"));
    ostringstream route;
    route << "127.0.0.4:1:225.0.0.1,0.0.0.0";
    InetMcastPrefix prefix(InetMcastPrefix::FromString(route.str()));
    InetMcastTable::RequestKey key(prefix, NULL);
    TASK_UTIL_EXPECT_TRUE(
            dynamic_cast<InetMcastRoute *>(blue_table_->Find(&key)) == NULL);
    task_util::WaitForIdle();

    //Verify route in the multicast table via sandesh
    BgpSandeshContext sandesh_context;
    sandesh_context.bgp_server = a_.get();
    sandesh_context.xmpp_peer_manager = bgp_channel_manager_.get();
    Sandesh::set_client_context(&sandesh_context);
    std::vector<size_t> result;
    result.resize(0);
    Sandesh::set_response_callback(boost::bind(ValidateShowRouteResponse, _1,
                                   result));

    ShowRouteReq *show_req = new ShowRouteReq;
    show_req->set_routing_table("blue.inetmcast.0");
    validate_done_ = 0;
    show_req->HandleRequest();
    show_req->Release();
    task_util::WaitForIdle();
    WAIT_EQ(1, validate_done_);

    //trigger a TCP close event on the server
    agent_a_->SessionDown();
    WAIT_EQ(2, bgp_channel_manager_->Count());
    //wait for channel to be deleted in the context of config-task
    task_util::WaitForIdle();

}

TEST_F(BgpXmppUnitTest, Multicast_SubsequentSubUnsub_Test) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client connected to XMPP server A 
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_a_->GetPort(), "127.0.0.4"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    //Ensure 1 bgp xmpp channel creation
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->Count());

    // Register to both unicast and multicast table
    agent_a_->Subscribe("blue", 1); 
    // Multicast Route Entry Add
    stringstream mroute;
    mroute << BgpAf::IPv4 << "/" << BgpAf::Mcast << "/" << "225.0.0.1";
    agent_a_->AddMcastRoute("blue", mroute.str(), "7.7.7.7", "10000-20000"); 
    WAIT_EQ(2, bgp_channel_manager_->channel_[0]->Count());
    agent_a_->Unsubscribe("blue"); 
    agent_a_->Subscribe("blue", 2); 
    // Multicast Route Entry Add
    agent_a_->AddMcastRoute("blue", mroute.str(), "7.7.7.7", "10000-20000"); 
    WAIT_EQ(5, bgp_channel_manager_->channel_[0]->Count());

    //Verify route added with instance_id = 2
    InetMcastTable *blue_table_ = static_cast<InetMcastTable *>(
            a_->database()->FindTable("blue.inetmcast.0"));
    ostringstream route;
    route << "127.0.0.4:2:225.0.0.1,0.0.0.0";
    InetMcastPrefix prefix(InetMcastPrefix::FromString(route.str()));
    InetMcastTable::RequestKey key(prefix, NULL);
    TASK_UTIL_EXPECT_TRUE(
            dynamic_cast<InetMcastRoute *>(blue_table_->Find(&key)) != NULL);

    //trigger a TCP close event on the server
    agent_a_->SessionDown();
    WAIT_EQ(2, bgp_channel_manager_->Count());
    //wait for channel to be deleted in the context of config-task
    task_util::WaitForIdle();

};

TEST_F(BgpXmppUnitTest, Multicast_MultipleRoutes_Test) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client connected to XMPP server A 
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_a_->GetPort(), "127.0.0.4"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    //Ensure bgp xmpp channel creation
    TASK_UTIL_EXPECT_EQ(1, bgp_channel_manager_->Count());

    // Register to both unicast and multicast table
    agent_a_->Subscribe("blue", 1); 
    WAIT_EQ(1, bgp_channel_manager_->channel_[0]->Count());
    // Multicast Route Entry Add
    stringstream mroute;
    mroute << BgpAf::IPv4 << "/" << BgpAf::Mcast << "/" << "225.0.0.1";
    agent_a_->AddMcastRoute("blue", mroute.str(), "7.7.7.7", "10000-20000"); 
    WAIT_EQ(2, bgp_channel_manager_->channel_[0]->Count());
    stringstream mroute2;
    mroute2 << BgpAf::IPv4 << "/" << BgpAf::Mcast << "/" << "225.0.0.2";
    agent_a_->AddMcastRoute("blue", mroute2.str(), "7.7.7.7", "10000-20000"); 
    WAIT_EQ(3, bgp_channel_manager_->channel_[0]->Count());
    task_util::WaitForIdle();

    //Verify route added with instance_id = 2
    InetMcastTable *blue_table_ = static_cast<InetMcastTable *>(
            a_->database()->FindTable("blue.inetmcast.0"));
    TASK_UTIL_EXPECT_EQ(2, blue_table_->Size());

    //trigger a TCP close event on the server
    agent_a_->SessionDown();
    WAIT_EQ(2, bgp_channel_manager_->Count());
    //wait for channel to be deleted in the context of config-task
    task_util::WaitForIdle();

    EXPECT_TRUE(blue_table_->Size() == 0);
};

TEST_F(BgpXmppUnitTest, XmppBadAddress) {
    Configure();
    task_util::WaitForIdle();

    // create an XMPP client connected to XMPP server A 
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, "agent-a", xs_a_->GetPort(), "127.0.0.4"));
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    // Register to both unicast and multicast table
    agent_a_->Subscribe("blue", 1); 
    WAIT_EQ(1, bgp_channel_manager_->channel_[0]->Count());
    // Multicast Route Entry Add with unsupported af
    agent_a_->AddMcastRoute("blue", "2/8/225.0.0.1", "7.7.7.7", "10000-20000"); 
    WAIT_EQ(2, bgp_channel_manager_->channel_[0]->Count());

    //Verify route added with instance_id = 2
    InetMcastTable *blue_table_ = static_cast<InetMcastTable *>(
            a_->database()->FindTable("blue.inetmcast.0"));
    EXPECT_TRUE(blue_table_->Size() == 0);

    // Multicast Route Entry Add with invalid nh 
    agent_a_->AddMcastRoute("blue", "1/8/225.0.0.1", "7.7", "10000-20000"); 
    WAIT_EQ(3, bgp_channel_manager_->channel_[0]->Count());
    EXPECT_TRUE(blue_table_->Size() == 0);

    //trigger a TCP close event on the server
    agent_a_->SessionDown();
    WAIT_EQ(2, bgp_channel_manager_->Count());
    //wait for channel to be deleted in the context of config-task
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
