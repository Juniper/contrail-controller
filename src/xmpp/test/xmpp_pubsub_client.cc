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
#include "xmpp/xmpp_channel_mux.h"
#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_config.h"
#include "xmpp/xmpp_init.h"
#include "xmpp/xmpp_proto.h"
#include "xmpp/xmpp_server.h"

#include "testing/gunit.h"

using namespace boost::asio;
using namespace std;

#define PUBSUB_NODE_ADDR    "bgp-node.contrai.com"
#define SUB_ADDR            "agent@vnsw.contrailsystems.com"
#define XMPP_CONTROL_SERV   "bgp.contrail.com"
#define XMPP_SERVER_PORT    5288

class XmppBgpMockPeer : public XmppSamplePeer {
public:
    XmppBgpMockPeer(XmppChannelMux *channel) : 
        XmppSamplePeer(channel) {
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
        b_.reset(new XmppClient(evm_.get()));
        thread_.reset(new ServerThread(evm_.get()));

        thread_->Start();
    }

    virtual void TearDown() {
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
    auto_ptr<XmppClient> b_;
};

namespace {

TEST_F(XmppPubSubTest, Connection) {
    // create a pair of Xmpp channels in server A and client B.
    XmppConfigData *cfg_b = new XmppConfigData;
    LOG(DEBUG, "Create client");
    cfg_b->AddXmppChannelConfig(CreateXmppChannelCfg("127.0.0.1", 5288,
                         SUB_ADDR, XMPP_CONTROL_SERV, true));
    ConfigUpdate(b_.get(), cfg_b);

    LOG(DEBUG, "-- Executing --");

    // client channel
    XmppConnection *connection;
    TASK_UTIL_EXPECT_TRUE((connection = b_->FindConnection(XMPP_CONTROL_SERV)) != NULL);
    TASK_UTIL_EXPECT_EQ(xmsm::ESTABLISHED, connection->GetStateMcState());
    XmppBgpMockPeer *bgp_cchannel =
        (new XmppBgpMockPeer(connection->ChannelMux()));

    //send subscribe message from agent to bgp
    string data = FileRead("controller/src/xmpp/testdata/pubsub_sub.xml");
    uint8_t buf[4096];
    memcpy(buf, data.data(), data.size());
    bool ret = bgp_cchannel->SendUpdate(buf, data.size());
    EXPECT_TRUE(ret);
    LOG(DEBUG, "Sent bytes: " << data.size());
    TASK_UTIL_EXPECT_NE(0, bgp_cchannel->Count());

    //send publish  message from agent to bgp
    data = FileRead("controller/src/xmpp/testdata/pubsub_pub.xml");
    memcpy(buf, data.data(), data.size());
    ret = bgp_cchannel->SendUpdate(buf, data.size());
    EXPECT_TRUE(ret);
    LOG(DEBUG, "Sent bytes: " << data.size());
    TASK_UTIL_EXPECT_NE(1, bgp_cchannel->Count());

    delete bgp_cchannel;
    task_util::WaitForIdle();

    ConfigUpdate(b_.get(), new XmppConfigData());
    task_util::WaitForIdle();
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
    int result = RUN_ALL_TESTS();
    TearDown();
    return result;
}
