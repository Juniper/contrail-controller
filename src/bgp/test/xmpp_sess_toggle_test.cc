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

#include <pugixml/pugixml.hpp>

#include "base/util.h"
#include "base/test/task_test_util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_proto.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/test/bgp_server_test_util.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"
#include "control-node/test/network_agent_mock.h"

#include "schema/xmpp_unicast_types.h"

#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_state_machine.h"

#include "xml/xml_pugi.h"

#include "testing/gunit.h"

using namespace boost::asio;
using namespace boost::assign;
using namespace std;
using namespace autogen;

#define SUB_ADDR "agent@vnsw.contrailsystems.com"  //agent host-name

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
        BgpXmppChannelManager(x, b), channel_(NULL) { }

    virtual void XmppHandleChannelEvent(XmppChannel *channel, 
                                        xmps::PeerState state) {

         BgpXmppChannelManager::XmppHandleChannelEvent(channel, state);
    }

    virtual BgpXmppChannel *CreateChannel(XmppChannel *channel) {
        channel_ = new BgpXmppChannelMock(channel, bgp_server_, this);
        return channel_;
    }

    BgpXmppChannelMock *channel_;
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
        BGP_DEBUG_UT("Created server at port: " << 
                a_->session_manager()->GetPort());
        xs_a_->Initialize(0, false);

        thread_.Start();

        BgpXmppConnectionEnvSetUp();
    }

    virtual void TearDown() {
        xs_a_->Shutdown();
        task_util::WaitForIdle();

        //
        // Delete the channel manager
        //
        delete bgp_channel_manager_;
        bgp_channel_manager_ = NULL;
        task_util::WaitForIdle();

        TcpServerManager::DeleteServer(xs_a_);
        xs_a_ = NULL;

        a_->Shutdown();
        task_util::WaitForIdle();
        agent_a_->Delete();
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

    void BgpXmppConnectionEnvSetUp() {
        //bring-up bgp-server
        Configure();
        task_util::WaitForIdle();

        // Verify XmppChannel object creation on client connect. 
        bgp_channel_manager_ = new BgpXmppChannelManagerMock(xs_a_, a_.get());

        // create an XMPP client in server A
        agent_a_.reset(
            new test::NetworkAgentMock(&evm_, SUB_ADDR, xs_a_->GetPort()));

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
    BgpXmppChannelManagerMock *bgp_channel_manager_;

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
</config>\
";

namespace {

TEST_F(BgpXmppUnitTest, TestSessionUpDown) {

    XmppConnection *sconnection;

    TASK_UTIL_EXPECT_TRUE((sconnection = xs_a_->FindConnection(SUB_ADDR)) !=
                              NULL);
    TASK_UTIL_EXPECT_EQ(xmsm::ESTABLISHED, sconnection->GetStateMcState());
    EXPECT_TRUE(sconnection->session() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->FindChannel(SUB_ADDR) != NULL);

    TcpServer *ts = static_cast<TcpServer *>(xs_a_);
    EXPECT_EQ(1, ts->GetSessionCount());

    //bring-down client session
    agent_a_->SessionDown();
    TASK_UTIL_EXPECT_FALSE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_EQ(0, ts->GetSessionCount());

    if (!xs_a_->IsPeerCloseGraceful()) {
        // Ensure XmppConnection is removed both from connection_map_
        // and deleted_connection_set_
        TASK_UTIL_EXPECT_TRUE(xs_a_->ConnectionCount() == 0);
        EXPECT_TRUE(bgp_channel_manager_->FindChannel(SUB_ADDR) == NULL);
    } else {
        TASK_UTIL_EXPECT_TRUE((sconnection = xs_a_->FindConnection(SUB_ADDR)) !=
                NULL);
        EXPECT_TRUE(bgp_channel_manager_->FindChannel(SUB_ADDR) != NULL);
    }

    //bring-up client session
    agent_a_->SessionUp();
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());

    TASK_UTIL_EXPECT_TRUE(
        (sconnection = xs_a_->FindConnection(SUB_ADDR)) != NULL);
    TASK_UTIL_EXPECT_EQ(xmsm::ESTABLISHED, sconnection->GetStateMcState());
    EXPECT_TRUE(sconnection->session() != NULL);
    TASK_UTIL_EXPECT_TRUE(agent_a_->IsEstablished());
    TASK_UTIL_EXPECT_TRUE(bgp_channel_manager_->FindChannel(SUB_ADDR) != NULL);
}
}

class TestEnvironment : public ::testing::Environment {
    virtual ~TestEnvironment() { }
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
