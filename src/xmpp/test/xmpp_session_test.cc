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
#include "xmpp/xmpp_session.h"
#include "xmpp/xmpp_state_machine.h"

#include "testing/gunit.h"

using namespace boost::asio;
using namespace std;

#define PUBSUB_NODE_ADDR "bgp-node.contrai.com"
#define SUB_ADDR "agent@vnsw.contrailsystems.com"
#define XMPP_CONTROL_SERV   "bgp.contrail.com"

class XmppMockConnection;
class XmppBgpMockPeer : public XmppSamplePeer {
public:
    XmppBgpMockPeer(XmppChannelMux *channel) : 
        XmppSamplePeer(channel) , count_(0) {
    }

    ~XmppBgpMockPeer() { }

    virtual void ReceiveUpdate(const XmppStanza::XmppMessage *msg) {
        count_++;
    }
    size_t Count() const { return count_; }
    void ResetCount() { count_ = 0; }

private:
    size_t count_;
};

class XmppMockConnection : public XmppClientConnection {
public:
    XmppMockConnection(XmppClient *server, const XmppChannelConfig *config)
        : XmppClientConnection(server, config), byte_count(0), msg_count(0) {}
    virtual void ReceiveMsg(XmppSession *session, const string &str) {
        byte_count += str.size();
        msg_count++;
        XmppConnection::ReceiveMsg(session, str);
    }
    virtual bool IsClient() const { return true; }
    void ResetStats() {
        byte_count = 0;
        msg_count = 0;
    }

    bool VerifyCumulativeStats(size_t byte, size_t msg = 1) {
        return (byte_count == byte && msg_count == msg);
    }

    bool VerifyPacketCount(XmppStanza::XmppMessageType type, 
                           uint64_t packets) const {
        const XmppSession *session = this->session();
        if (!session) return false;
        return (session->Stats(type).first == packets);
    }

    size_t byte_count;
    size_t msg_count;
};

class XmppSessionTest : public ::testing::Test {
public:
    static const int kMaxMessageSize = 4096;

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
        thread_->Start();
    }

    virtual void TearDown() {
        a_->Shutdown();
        task_util::WaitForIdle();
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
        task_util::WaitForIdle();
    }

    void CreateXmppChannelCfg(XmppChannelConfig *cfg, const char *address,
             int port, const string &from, const string &to, bool isClient) {
        cfg->endpoint.address(ip::address::from_string(address));
        cfg->endpoint.port(port);
        cfg->ToAddr = to;
        cfg->FromAddr = from;
        if (!isClient) cfg->NodeAddr = PUBSUB_NODE_ADDR;
        return;
    }

    void ConfigUpdate(XmppClient *client, const XmppConfigData *config) {
        client->ConfigUpdate(config);
    }

    void AddClientChannel(XmppMockConnection *channel);

    void SetupConnection() {
        XmppChannelConfig cfg(true);

        LOG(DEBUG, "Create client");
        CreateXmppChannelCfg(&cfg, "127.0.0.1", a_->GetPort(), SUB_ADDR,
                             XMPP_CONTROL_SERV, true);
        cconnection_ = new XmppMockConnection(b_, &cfg);
        cconnection_->Initialize();
        AddClientChannel(cconnection_);

        LOG(DEBUG, "-- Exectuting --");

        // server channels
        TASK_UTIL_EXPECT_TRUE(
                (sconnection_ = a_->FindConnection(SUB_ADDR)) != NULL);

        // Check for server, client connection is established. Wait upto 1 sec
        TASK_UTIL_EXPECT_TRUE(
                sconnection_->GetStateMcState() == xmsm::ESTABLISHED);
        TASK_UTIL_EXPECT_TRUE(
                cconnection_->GetStateMcState() == xmsm::ESTABLISHED); 
        cconnection_->ResetStats();
        bgp_server_peer_ = new XmppBgpMockPeer(sconnection_->ChannelMux());
    }

    void TearDownConnection() {
        delete bgp_server_peer_;
        cconnection_->ManagedDelete();
        task_util::WaitForIdle();
        cconnection_ = NULL;
        sconnection_ = NULL;
    }

    void SendAndVerify(const char *msg, size_t sendlen, 
                       size_t receivelen, size_t no_of_msg) {
        bgp_server_peer_->SendUpdate((const uint8_t *)msg, sendlen);
        LOG(DEBUG, "Sent bytes: " << sendlen);
        TASK_UTIL_EXPECT_TRUE(
                 cconnection_->VerifyCumulativeStats(receivelen, no_of_msg));
        LOG(DEBUG, "Received bytes: " << cconnection_->byte_count);
        cconnection_->ResetStats();
    }

    // Verify that we received n packets of given xmpp type
    void VerifyPacketTypeStats(XmppStanza::XmppMessageType type, 
                               uint64_t packets) {
        TASK_UTIL_EXPECT_TRUE(cconnection_->VerifyPacketCount(type, packets));
    }

    uint64_t PacketTypeStats(XmppStanza::XmppMessageType type) {
        const XmppSession *session = cconnection_->session();
        if (session) return session->Stats(type).first;
        return 0;
    }

    XmppBgpMockPeer *bgp_server_peer_;
    XmppConnection *sconnection_;
    XmppMockConnection *cconnection_;
    auto_ptr<EventManager> evm_;
    auto_ptr<ServerThread> thread_;
    XmppServer *a_;
    XmppClient *b_;
};


void XmppSessionTest::AddClientChannel(XmppMockConnection *connection) {
    b_->InsertConnection(connection);
}

namespace {

TEST_F(XmppSessionTest, Connection) {
    SetupConnection();

    // Read 6K size message from file.
    string data;
    data.reserve(kMaxMessageSize * 10);
    data = FileRead("controller/src/xmpp/testdata/iq.xml");
    // trim newline from the end.
    data.erase(data.find_last_not_of(" \n\r\t")+1);
    SendAndVerify(data.data(), data.size(), data.size(), 1);

    // Read large 24k iq message from file.
    data.reserve(kMaxMessageSize * 10);
    data = FileRead("controller/src/xmpp/testdata/iq-large.xml");
    data.erase(data.find_last_not_of(" \n\r\t")+1);
    SendAndVerify(data.data(), data.size(), data.size(), 1);

    TearDownConnection();
}

TEST_F(XmppSessionTest, MessageBoundary) {
    SetupConnection();

    string iq("<iq> blah blah </iq><");
    SendAndVerify(iq.data(), iq.size(), iq.size() - 1, 1);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 1);

    iq = "iq> Rest of the messsage </iq>";
    SendAndVerify(iq.data(), iq.size(), iq.size() + 1, 1);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 2);

    TearDownConnection();
}

// Test whitespace in the end and beginning of message
TEST_F(XmppSessionTest, MessageWithSpace) {
    SetupConnection();

    uint64_t ws_stats = PacketTypeStats(XmppStanza::WHITESPACE_MESSAGE_STANZA);
    string iq("<iq> blah blah </iq> ");
    SendAndVerify(iq.data(), iq.size(), iq.size(), 2);
    VerifyPacketTypeStats(XmppStanza::WHITESPACE_MESSAGE_STANZA, ws_stats+1);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 1);

    iq = "<iq> Rest of the messsage </iq>";
    SendAndVerify(iq.data(), iq.size(), iq.size(), 1);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 2);

    iq = "   <iq> Rest of the messsage </iq>"; // space in the beginning
    SendAndVerify(iq.data(), iq.size(), iq.size(), 2);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 3);
    VerifyPacketTypeStats(XmppStanza::WHITESPACE_MESSAGE_STANZA, ws_stats+2);

    iq = "\n<iq> Rest of the messsage \n </iq>"; // newline in the beginning
    SendAndVerify(iq.data(), iq.size(), iq.size(), 2);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 4);
    VerifyPacketTypeStats(XmppStanza::WHITESPACE_MESSAGE_STANZA, ws_stats+3);

    iq = "Ȁ<iq> Rest of the messsage \n </iq>"; // uni-code in the beginning
    SendAndVerify(iq.data(), iq.size(), iq.size(), 2);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 5);
    VerifyPacketTypeStats(XmppStanza::WHITESPACE_MESSAGE_STANZA, ws_stats+4);

    TearDownConnection();
}

// Test first 2 bytes of iq message in first read
TEST_F(XmppSessionTest, PartialIq1) {
    SetupConnection();

    uint64_t ws_stats = PacketTypeStats(XmppStanza::WHITESPACE_MESSAGE_STANZA);
    string iq("<iq> blah blah </iq> <i");
    // Expect to receive 1 iq and 1 whitespace
    SendAndVerify(iq.data(), iq.size(), iq.size() - 2, 2);
    VerifyPacketTypeStats(XmppStanza::WHITESPACE_MESSAGE_STANZA, ws_stats+1);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 1);

    iq = "q> Rest of the messsage </iq>";
    SendAndVerify(iq.data(), iq.size(), iq.size() + 2, 1);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 2);

    TearDownConnection();
}

// Test message across multiple reads
TEST_F(XmppSessionTest, PartialIq2) {
    SetupConnection();

    uint64_t ws_stats = PacketTypeStats(XmppStanza::WHITESPACE_MESSAGE_STANZA);
    string iq("<iq> blah blah </iq> <i");
    // Expect to receive 1 iq and 1 whitespace
    SendAndVerify(iq.data(), iq.size(), iq.size() - 2, 2);
    VerifyPacketTypeStats(XmppStanza::WHITESPACE_MESSAGE_STANZA, ws_stats+1);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 1);

    iq = "q> Rest of the messsage is here ";
    size_t prev = iq.size() + 2;
    SendAndVerify(iq.data(), iq.size(), 0, 0);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 1);
    usleep(2000);

    iq = "more messsage is here </iq>";
    SendAndVerify(iq.data(), iq.size(), iq.size() + prev, 1);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 2);

    TearDownConnection();
}

// Test with garbage between 2 messages
TEST_F(XmppSessionTest, Garbage1) {
    SetupConnection();

    uint64_t ws_stats = PacketTypeStats(XmppStanza::WHITESPACE_MESSAGE_STANZA);
    string iq_ok("<iq> blah blah </iq>"); 
    string garb("   abc<i"); // 3 space and garbage
    string iq = iq_ok + garb;
    // Expect to receive 1 iq and 1 whitespace
    SendAndVerify(iq.data(), iq.size(), iq_ok.size() + 3, 2);
    VerifyPacketTypeStats(XmppStanza::WHITESPACE_MESSAGE_STANZA, ws_stats+1);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 1);

    iq = "q> Rest of the messsage </iq>";
    SendAndVerify(iq.data(), iq.size(), iq.size() + 5, 1);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 2);

    ws_stats = PacketTypeStats(XmppStanza::WHITESPACE_MESSAGE_STANZA);
    garb = "   abc   <i";
    iq = iq_ok + garb;
    SendAndVerify(iq.data(), iq.size(), iq_ok.size() + 3, 2);
    VerifyPacketTypeStats(XmppStanza::WHITESPACE_MESSAGE_STANZA, ws_stats+1);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 3);

    iq = "q> Rest of the messsage </iq>";
    SendAndVerify(iq.data(), iq.size(), iq.size() + 8, 1);
    VerifyPacketTypeStats(XmppStanza::IQ_STANZA, 4);
    // whitespace after garbage is ignored
    VerifyPacketTypeStats(XmppStanza::WHITESPACE_MESSAGE_STANZA, ws_stats+1);

    TearDownConnection();
}

TEST_F(XmppSessionTest, SendClose) {

    SetupConnection();
    // In Established state ensure SendClose is not processed
    sconnection_->SendClose(sconnection_->session());

    ASSERT_TRUE(sconnection_->GetStateMcState() == xmsm::ESTABLISHED); 
    ASSERT_TRUE(cconnection_->GetStateMcState() == xmsm::ESTABLISHED); 
    
    TearDownConnection();
}

#if 0
//Test keepaliveTimer and HoldTimer
TEST_F(XmppSessionTest, KeepAlive) {
    SetupConnection();

    sconnection_->SetKeepAliveTimer(1);
    cconnection_->SetKeepAliveTimer(1);

    //Wait for atleast 3 times keepAliveTimer to ensure HoldTimer has
    //not fired.
    sleep(4);

    //ping-pong of KeepAlive shud keep session up.
    ASSERT_TRUE(sconnection_->GetStateMcState() == xmsm::ESTABLISHED); 
    ASSERT_TRUE(cconnection_->GetStateMcState() == xmsm::ESTABLISHED); 

    sconnection_->StopKeepAliveTimer();
    cconnection_->StopKeepAliveTimer();

    //Wait for atleast 3 times keepAliveTimer for HoldTimer to fire.
    sleep(4);

    //No keepalives, hence server side state-machine set to IDLE.
    ASSERT_TRUE(sconnection_->GetStateMcState() == xmsm::IDLE); 

    TearDownConnection();

}
#endif
}

#include "control-node/control_node.h"
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
