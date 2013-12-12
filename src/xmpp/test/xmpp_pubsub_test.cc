/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/test/xmpp_sample_peer.h"
#include <fstream>
#include <sstream>

#include "base/util.h"
#include "base/test/task_test_util.h"

#include "control-node/control_node.h"
#include "io/test/event_manager_test.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "xmpp/xmpp_channel_mux.h"
#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_config.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_proto.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_state_machine.h"

#include "testing/gunit.h"

using namespace boost::asio;
using namespace std;

#define PUBSUB_NODE_ADDR "bgp-node.contrai.com"
#define SUB_ADDR "agent@vnsw.contrailsystems.com"
#define XMPP_CONTROL_SERV   "bgp.contrail.com"

class XmppBgpMockPeer : public XmppSamplePeer {
public:
    XmppBgpMockPeer(XmppChannelMux *channel) : 
        XmppSamplePeer(channel) , count_(0) {
    }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *) {
        count_ ++;
    }

    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }

private:
    size_t count_;
};

class XmppPubSubTest : public ::testing::Test {
protected:
    string FileRead(const string &filename) {
        string content;
        fstream file(filename.c_str(), fstream::in);
        if (!file) {
            LOG(DEBUG, "File not found : " << filename);
            return content;
        }
        while (!file.eof()) {
            char piece[256];
            file.read(piece, sizeof(piece));
            content.append(piece, file.gcount());
        }
        file.close();
        return content;
    }

    virtual void SetUp() {
        evm_.reset(new EventManager());
        a_ = new XmppServer(evm_.get(), XMPP_CONTROL_SERV);
        b_ = new XmppClient(evm_.get());
        thread_.reset(new ServerThread(evm_.get()));

        a_->Initialize(0, false);
        LOG(DEBUG, "Created server at port: " << a_->GetPort());
        LOG(DEBUG, "Created client at port: " << b_->GetPort());
        thread_->Start();
    }

    virtual void TearDown() {
        a_->Shutdown();
        b_->Shutdown();
        task_util::WaitForIdle();

        TcpServerManager::DeleteServer(a_);
        a_ = NULL;
        TcpServerManager::DeleteServer(b_);
        b_ = NULL;

        evm_->Shutdown();
        if (thread_.get() != NULL) {
            thread_->Join();
        }
    }

    XmppChannelConfig *CreateXmppChannelCfg(const char *address, int port,
                                      const string &from, const string &to,
                                      bool isClient) {
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

    auto_ptr<EventManager> evm_;
    auto_ptr<ServerThread> thread_;
    XmppServer *a_;
    XmppClient *b_;
};

namespace {

TEST_F(XmppPubSubTest, Connection) {
    // create a pair of Xmpp channels in server A and client B.
    XmppConfigData *cfg_b = new XmppConfigData;
    LOG(DEBUG, "Create client");
    cfg_b->AddXmppChannelConfig(CreateXmppChannelCfg("127.0.0.1", a_->GetPort(),
                                SUB_ADDR, XMPP_CONTROL_SERV, true));
    ConfigUpdate(b_, cfg_b);

    LOG(DEBUG, "-- Exectuting --");

    // server channels
    XmppConnection *sconnection;
    // Wait for connection on server. 
    TASK_UTIL_EXPECT_TRUE((sconnection = a_->FindConnection(SUB_ADDR)) != NULL);
    // Check for server, client connection is established. Wait upto 1 sec
    TASK_UTIL_EXPECT_TRUE(sconnection->GetStateMcState() == xmsm::ESTABLISHED);
    XmppBgpMockPeer *bgp_schannel = 
            new XmppBgpMockPeer(sconnection->ChannelMux());

    // client channel
    XmppConnection *cconnection = b_->FindConnection(XMPP_CONTROL_SERV);
    ASSERT_FALSE(cconnection == NULL);
    TASK_UTIL_EXPECT_TRUE(cconnection->GetStateMcState() == xmsm::ESTABLISHED);
    XmppBgpMockPeer *bgp_cchannel(
            new XmppBgpMockPeer(cconnection->ChannelMux()));

    //send subscribe message from agent to bgp
    string data = FileRead("controller/src/xmpp/testdata/pubsub_sub.xml");
    uint8_t buf[4096];
    memcpy(buf, data.data(), data.size());
    bool ret = bgp_cchannel->SendUpdate(buf, data.size());
    EXPECT_TRUE(ret);
    TASK_UTIL_EXPECT_TRUE(bgp_schannel->Count() != 0);

    //send publish  message from agent to bgp
    data = FileRead("controller/src/xmpp/testdata/pubsub_pub.xml");
    memcpy(buf, data.data(), data.size());
    ret = bgp_cchannel->SendUpdate(buf, data.size());
    EXPECT_TRUE(ret);
    LOG(DEBUG, "Sent bytes: " << data.size());
    TASK_UTIL_EXPECT_TRUE(bgp_schannel->Count() != 1);

    delete bgp_schannel;
    delete bgp_cchannel;

    task_util::WaitForIdle();

    //cleanup client
    ConfigUpdate(b_, new XmppConfigData());
    task_util::WaitForIdle();
}

}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    Sandesh::SetLocalLogging(true);
    Sandesh::SetLoggingLevel(SandeshLevel::UT_DEBUG);
    return RUN_ALL_TESTS();
}
