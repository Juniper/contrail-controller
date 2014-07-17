/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <unistd.h>
#include <fstream>
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>
using namespace boost::assign;

#include <pugixml/pugixml.hpp>

#include "base/util.h"
#include "base/test/task_test_util.h"

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"

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
#include "control-node/control_node.h"
#include "control-node/test/network_agent_mock.h"
#include "io/test/event_manager_test.h"

#include "schema/xmpp_unicast_types.h"

#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_state_machine.h"

#include "xml/xml_pugi.h"

#include "testing/gunit.h"

using namespace boost::asio;
using namespace std;
using namespace autogen;

#define SUB_ADDR "agent@vnsw.contrailsystems.com" 
#define XMPP_CONTROL_SERV   "bgp.contrail.com" 
#define PUBSUB_NODE_ADDR "bgp-node.contrail.com"

class XmppChannelMuxMock : public XmppChannelMux {
public:
    XmppChannelMuxMock(XmppConnection *conn) : XmppChannelMux(conn), count_(0) {
    }

    virtual bool Send(const uint8_t *msg, size_t msgsize, xmps::PeerId id,
        SendReadyCb cb) {
        bool ret;

        // Simulate write blocked after the first message is sent.
        ret = XmppChannelMux::Send(msg, msgsize, id, cb);
        assert(ret);
        if (++count_ == 1) {
            XmppChannelMux::RegisterWriteReady(id, cb);
            return false;
        }

        return true;
    }

private:
    int count_;
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

    XmppChannel *xmpp_channel() { return channel_; }
    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }
    virtual ~BgpXmppChannelMock() { }

private:
    size_t count_;
};

class XmppVnswBgpMockChannel : public BgpXmppChannel {
public:
    XmppVnswBgpMockChannel(XmppChannel *channel, BgpServer *bgp_server,
                           BgpXmppChannelManager *channel_manager) 
        : BgpXmppChannel(channel, bgp_server, channel_manager), count_(0) { }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        count_++;
    }
    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }

private:
    size_t count_;
};

class BgpXmppChannelManagerMock : public BgpXmppChannelManager {
public:
    BgpXmppChannelManagerMock(XmppServer *x, BgpServer *b) :
        BgpXmppChannelManager(x, b), count_(0), channel_(NULL), 
        xmpp_mux_ch_(NULL) { }

    virtual ~BgpXmppChannelManagerMock() {
    }

    virtual void XmppHandleChannelEvent(XmppChannel *channel,
                                        xmps::PeerState state) {
         if (xmpp_mux_ch_ == NULL) {
             XmppChannelMux *mux = static_cast<XmppChannelMux *>(channel);
             xmpp_mux_ch_ = new XmppChannelMuxMock(mux->connection());

             //
             // Update the channel stored in bgp_xmpp_channel to this new mock
             // channel. Old gets deleted via auto_ptr
             //
             mux->connection()->SetChannelMux(xmpp_mux_ch_);
         }

         //
         // Register Mock XmppChannelMux with bgp
         //
         XmppChannel *ch = static_cast<XmppChannel *>(xmpp_mux_ch_);
         BgpXmppChannelManager::XmppHandleChannelEvent(ch, state);
    }

    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel) {
        channel_ = new BgpXmppChannelMock(channel, bgp_server_, this);
        return channel_;
    }

    void XmppVisit(BgpXmppChannel *channel) {
        count_++;
    }
    int Count() {
        count_ = 0;
        VisitChannels(boost::bind(&BgpXmppChannelManagerMock::XmppVisit,
                      this, _1));
        return count_;
    }

    int count_;
    BgpXmppChannelMock *channel_;
    XmppChannelMuxMock *xmpp_mux_ch_;
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
        xs_a_ = new XmppServer(&evm_, XMPP_CONTROL_SERV);

        a_->session_manager()->Initialize(0);
        BGP_DEBUG_UT("Created server at port: " << 
                a_->session_manager()->GetPort());
        xs_a_->Initialize(0, false);

        bgp_channel_manager_ = new BgpXmppChannelManagerMock(xs_a_, a_.get());

        thread_.Start();
    }


    virtual void TearDown() {
        agent_a_->Delete();
        task_util::WaitForIdle();
        xs_a_->Shutdown();
        task_util::WaitForIdle();
        a_->Shutdown();
        task_util::WaitForIdle();
        delete bgp_channel_manager_;
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(xs_a_);
        xs_a_ = NULL;
        evm_.Shutdown();
        task_util::WaitForIdle();
        thread_.Join();
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
        if (!isClient) cfg->NodeAddr = PUBSUB_NODE_ADDR;
        return cfg;
    }

    EventManager evm_;
    ServerThread thread_;
    auto_ptr<BgpServerTest> a_;
    XmppServer *xs_a_;
    boost::scoped_ptr<test::NetworkAgentMock> agent_a_;
    BgpXmppChannelManagerMock *bgp_channel_manager_;
    XmppVnswBgpMockChannel *xmpp_cchannel_;

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
    <routing-instance name='red'>\
        <vrf-target>target:1:1</vrf-target>\
    </routing-instance>\
</config>\
";

#define WAIT_EQ(expected, actual) \
    TASK_UTIL_EXPECT_EQ(expected, actual)
#define WAIT_NE(expected, actual) \
    TASK_UTIL_EXPECT_NE(expected, actual)

namespace {

TEST_F(BgpXmppUnitTest, WriteReadyTest) {

    Configure();
    task_util::WaitForIdle();

    // create an XMPP client in server A
    agent_a_.reset(
        new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

    // Wait upto 1 sec
    WAIT_EQ(1, bgp_channel_manager_->Count());
    BGP_DEBUG_UT("-- Executing --");
    WAIT_NE(static_cast<BgpXmppChannelMock *>(NULL),
            bgp_channel_manager_->channel_);
    WAIT_EQ(xmps::READY,
        bgp_channel_manager_->channel_->xmpp_channel()->GetPeerState());

    agent_a_->Subscribe(BgpConfigManager::kMasterInstance, -1);
    WAIT_EQ(1, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received subscribe message for default at Server \n ");

    agent_a_->Subscribe("blue", 1);
    WAIT_EQ(2, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received subscribe message for blue at Server \n ");

    agent_a_->Subscribe("red", 2);
    WAIT_EQ(3, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received subscribe message for red at Server \n ");

    agent_a_->AddRoute("blue","10.1.1.1/32");
    WAIT_EQ(4, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received route for blue at Server \n ");

    agent_a_->AddRoute("red","20.1.1.1/32");
    WAIT_EQ(5, bgp_channel_manager_->channel_->Count());
    BGP_DEBUG_UT("Received route for red at Server \n ");

    // send blocked, no route should be reflected back after the first one.
    // check a few times to make sure that no more routes are reflected
    for (int idx = 0; idx < 10; ++idx) {
        WAIT_EQ(1, agent_a_->RouteCount());
        usleep(10000);
        task_util::WaitForIdle();
    }

    // simulate write unblocked
    XmppConnection *sconnection = xs_a_->FindConnection(SUB_ADDR);
    const boost::system::error_code ec;
    sconnection->ChannelMux()->WriteReady(ec);

    // simulated send block after first send, hence subsequent routes
    // reflected back to the Xmpp vnsw client only now, through WriteReady()
    WAIT_EQ(4, agent_a_->RouteCount());

    // route reflected back + leaked
    TASK_UTIL_ASSERT_TRUE(agent_a_->RouteLookup("blue", "10.1.1.1/32") != NULL);
    TASK_UTIL_ASSERT_TRUE(agent_a_->RouteLookup("blue", "20.1.1.1/32") != NULL);
    TASK_UTIL_ASSERT_TRUE(agent_a_->RouteLookup("red", "10.1.1.1/32") != NULL);
    TASK_UTIL_ASSERT_TRUE(agent_a_->RouteLookup("red", "20.1.1.1/32") != NULL);

    //trigger a TCP close event on the server
    agent_a_->SessionDown();
    usleep(50);
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
