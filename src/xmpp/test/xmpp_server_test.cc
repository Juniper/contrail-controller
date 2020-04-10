/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_server.h"
#include "xmpp/test/xmpp_sample_peer.h"
#include <boost/bind.hpp>
#include "base/task.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"

#include "base/util.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "xmpp/xmpp_channel_mux.h"
#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_config.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_state_machine.h"

#include "testing/gunit.h"

using namespace boost::asio;
using namespace std;

#define PUBSUB_NODE_ADDR "bgp-node.contrai.com"
#define SUB_ADDR "agent@vnsw.contrailsystems.com"
#define XMPP_CONTROL_SERV   "bgp.contrail.com"

class XmppBgpMockPeer : public XmppSamplePeer {
public:
    XmppBgpMockPeer(XmppChannel *channel) :
        XmppSamplePeer(channel) , count_(0) {
    }
    virtual ~XmppBgpMockPeer() {
        LOG(DEBUG, "Deleting XmppBgpMockPeer");
    }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        count_ ++;
    }

    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }

private:
    size_t count_;
};

class XmppPeerManagerMock;

class XmppServerTest : public ::testing::Test {
public:
    void XmppConnectionEventCb(XmppChannel *ch, xmps::PeerState state) {
        LOG(DEBUG, "Called XmppConnectionEventCb Event: " << state);
        if (state == xmps::READY) {
            peer_ = new XmppBgpMockPeer(ch);
        } else if (state == xmps::NOT_READY) {
            if (peer_) delete peer_;
            peer_ = NULL;
        }
    }

protected:
    virtual void SetUp() {
        evm_.reset(new EventManager());
        a_ = new XmppServer(evm_.get());
        b_ = new XmppClient(evm_.get());
        peer_ = NULL;
        thread_.reset(new ServerThread(evm_.get()));

        a_->Initialize(0, false);
        LOG(DEBUG, "Created server at port: " << a_->GetPort());
        thread_->Start();
    }

    virtual void TearDown() {
        LOG(DEBUG, "TearDown");

        // Shutdown the client first, before terminating the server.
        b_->Shutdown();
        task_util::WaitForIdle();

        LOG(DEBUG, "Shutdown XMPP server " << a_->GetPort());
        a_->Shutdown();
        task_util::WaitForIdle();
        ASSERT_TRUE(a_->GetPort() == -1);

        TcpServerManager::DeleteServer(a_);
        TcpServerManager::DeleteServer(b_);
        a_ = NULL;
        b_ = NULL;

        evm_->Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
    }

    XmppChannelConfig *CreateXmppChannelCfg(const char *address, int port,
                                            const string &from,
                                            const string &to, bool isClient) {
        XmppChannelConfig *cfg = new XmppChannelConfig(isClient);

        cfg->endpoint.address(ip::address::from_string(address));
        cfg->endpoint.port(port);
        cfg->ToAddr = to;
        cfg->FromAddr = from;
        if (!isClient) cfg->NodeAddr = PUBSUB_NODE_ADDR;
        return cfg;
    }

    void ConfigUpdate(XmppClient *client, const XmppConfigData *config) {
        client->ConfigUpdate(config);
    }

    void TestBasicConnection(const string &local_name,
                             const string &remote_name, bool fail);

    auto_ptr<EventManager> evm_;
    auto_ptr<ServerThread> thread_;
    XmppServer *a_;
    XmppClient *b_;
    boost::scoped_ptr<XmppPeerManagerMock> xmpp_peer_manager_;
    XmppBgpMockPeer *peer_;
};

class XmppPeerManagerMock : public XmppPeerManager {
public:
    XmppPeerManagerMock(XmppServer *x, void *b, XmppServerTest *ptr) :
        XmppPeerManager(x, b), ptr_(ptr), count(0) { }
    virtual void XmppHandleConnectionEvent(XmppChannel *channel,
            xmps::PeerState state) {
        ptr_->XmppConnectionEventCb(channel, state);
        XmppPeerManager::XmppHandleConnectionEvent(channel, state);
    }
    void XmppVisit(XmppSamplePeer *peer) {
        count++;
    }
    XmppServerTest *ptr_;
    int count;
};

void XmppServerTest::TestBasicConnection (const string &local_name,
                                          const string &remote_name,
                                          bool fail) {
    xmpp_peer_manager_.reset(new XmppPeerManagerMock(a_, NULL, this));

    // create a pair of Xmpp channel in server A and client B.
    XmppConfigData *cfg_b = new XmppConfigData;
    LOG(DEBUG, "Create client");
    cfg_b->AddXmppChannelConfig(CreateXmppChannelCfg("127.0.0.1",
                                a_->GetPort(), local_name, remote_name, true));
    ConfigUpdate(b_, cfg_b);

    LOG(DEBUG, "-- Exectuting --");
    task_util::WaitForIdle();

    if (!fail)
        TASK_UTIL_EXPECT_NE(static_cast<XmppBgpMockPeer *>(NULL), peer_);
    xmpp_peer_manager_->VisitPeers(
            boost::bind(&XmppPeerManagerMock::XmppVisit,
                xmpp_peer_manager_.get(), _1));
    TASK_UTIL_EXPECT_EQ(fail ? 0 : 1, xmpp_peer_manager_->count);
    // reset the count for VisitPeers
    xmpp_peer_manager_->count = 0;


    // Wait for connection on server.
    if (fail) {
        TASK_UTIL_EXPECT_EQ(static_cast<XmppConnection *>(NULL),
                            a_->FindConnection(local_name));
    } else {
        // server channels
        TASK_UTIL_EXPECT_NE(static_cast<XmppConnection *>(NULL),
                            a_->FindConnection(local_name));
        XmppConnection *sconnection = a_->FindConnection(local_name);
        // Check for server, client connection is established. Wait upto 1 sec
        TASK_UTIL_EXPECT_EQ(xmsm::ESTABLISHED, sconnection->GetStateMcState());
    }

    // client channels
    TASK_UTIL_EXPECT_NE(static_cast<XmppConnection *>(NULL),
                        b_->FindConnection(remote_name));
    XmppConnection *cconnection = b_->FindConnection(remote_name);
    if (fail) {
        TASK_UTIL_EXPECT_NE(xmsm::ESTABLISHED, cconnection->GetStateMcState());
    } else {
        TASK_UTIL_EXPECT_EQ(xmsm::ESTABLISHED, cconnection->GetStateMcState());
    }

    // Tear down client and check if server peer count has gone to 0.
    ConfigUpdate(b_, new XmppConfigData());
    TASK_UTIL_EXPECT_EQ(0U, xmpp_peer_manager_->peer_mux_map().size());

    // Config update will close the peer from client side
    // which will generate server side close event and
    // callback XmppConnectionEventCb will be called.
    TASK_UTIL_EXPECT_EQ(static_cast<XmppBgpMockPeer *>(NULL), peer_);
}

namespace {

TEST_F(XmppServerTest, Connection) {
    TestBasicConnection(SUB_ADDR, XMPP_CONTROL_SERV, false);
}

TEST_F(XmppServerTest, LongLocalName) {
    string local_name;
    for (int i = 0; i < 10; i++)
        local_name += SUB_ADDR;
    string remote_name;
    TestBasicConnection(local_name, XMPP_CONTROL_SERV, false);
}

TEST_F(XmppServerTest, LongRemoteName) {
    string remote_name;
    for (int i = 0; i < 10; i++)
        remote_name += XMPP_CONTROL_SERV;
    TestBasicConnection(SUB_ADDR, remote_name, false);
}

TEST_F(XmppServerTest, LongBothName) {
    string local_name;
    for (int i = 0; i < 10; i++)
        local_name += SUB_ADDR;
    string remote_name;
    for (int i = 0; i < 10; i++)
        remote_name += XMPP_CONTROL_SERV;
    TestBasicConnection(local_name, remote_name, false);
}

TEST_F(XmppServerTest, VeryLongLocalName) {
    string local_name;
    for (int i = 0; i < 100; i++)
        local_name += SUB_ADDR;
    TestBasicConnection(local_name, XMPP_CONTROL_SERV, true);
}

TEST_F(XmppServerTest, VeryLongRemoteName) {
    string remote_name;
    for (int i = 0; i < 100; i++)
        remote_name += XMPP_CONTROL_SERV;
    TestBasicConnection(SUB_ADDR, remote_name, true);
}

TEST_F(XmppServerTest, VeryLongBothName) {
    string local_name;
    for (int i = 0; i < 100; i++)
        local_name += SUB_ADDR;
    string remote_name;
    for (int i = 0; i < 100; i++)
        remote_name += XMPP_CONTROL_SERV;
    TestBasicConnection(local_name, remote_name, true);
}

}

static void SetUp() {
    LoggingInit();
    ControlNode::SetDefaultSchedulingPolicy();
}

static void TearDown() {
    task_util::WaitForIdle();
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Terminate();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    SetUp();
    Sandesh::SetLocalLogging(true);
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
