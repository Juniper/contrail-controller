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
#define SUB_ADDR2 "agent@vnsw.contrailsystems.comagentagneagneagneagneagneagneagneagneagneagneagneagneagneagneagneagneagneagneagneagneagneagnettttttttttttttttttttttagenagenagenagenagenttttt"
#define sXMPP_STREAM_OPEN_BAD     "<?xml version='1.0'?><extra></extra>><stream:stream from='dummycl' to='dummyserver' version='1.0' xml:lang='en' xmlns:stream='http://etherx.jabber.org/streams'/>"
#define sXMPP_STREAM_OPEN_GOOD    "<?xml version='1.0'?><stream:stream from='dummycl' to='dummyserver' version='1.0' xml:lang='en' xmlns:stream='http://etherx.jabber.org/streams'/>"

class XmppMockClientConnection;
class XmppMockClientConnection : public XmppClientConnection {
public:
    XmppMockClientConnection(XmppClient *server, const XmppChannelConfig *config, bool send_write_doc=false,
                             bool out_of_bound=false)
        : XmppClientConnection(server, config),
                               open_count(0), send_bad_open(true), send_write_doc(send_write_doc),
                               out_of_bound(out_of_bound) {}
    virtual bool IsClient() const { return true; }

    bool SendOpen(XmppSession *session) {
        if (!session) return false;

        XmppProto::XmppStanza::XmppStreamMessage openstream;
        openstream.strmtype = XmppStanza::XmppStreamMessage::INIT_STREAM_HEADER;
        uint8_t data[2048];

        //EncodeStream
        auto_ptr<XmlBase> open_doc(XmppXmlImplFactory::Instance()->GetXmlImpl());
        if (out_of_bound == true) {
            if (open_doc->LoadDoc(sXMPP_STREAM_OPEN_GOOD) == -1) {
                return false;
            }
        } else if(send_bad_open == true) {
            if (open_doc->LoadDoc(sXMPP_STREAM_OPEN_BAD) == -1) {
                return false;
            }
            send_bad_open = false;
        } else {
            if (open_doc->LoadDoc(sXMPP_STREAM_OPEN_GOOD) == -1) {
                return false;
            }
        }

        // XmppProto::SetTo and XmppProto::SetFrom
        string ns(sXMPP_STREAM_O);
        open_doc->ReadNode(ns);
        open_doc->ModifyAttribute("to" , GetComputeHostName());
        if (out_of_bound) {
            open_doc->ModifyAttribute("from" , SUB_ADDR2);
        } else {
            open_doc->ModifyAttribute("from" , GetControllerHostName());
        }

        uint8_t *buf = data;
        int len = 0;
        //Returns byte encoded in the doc
        if (send_write_doc) {
            len = open_doc->WriteDoc(buf);
        } else {
            len = open_doc->WriteRawDoc(buf);
        }

        if (len > 0) {
            string openstr(buf, buf+len);
            boost::algorithm::ireplace_last(openstr, "/", " ");
            memcpy(buf, openstr.c_str(), len);
            LOG(DEBUG, "\n\n Sending open_doc:"  << openstr << "\n\n");
        }

        assert(len > 0);
        session->Send(data, len, NULL);
        open_count++;

        return true;
    }

    size_t open_count;
    bool send_bad_open;
    bool send_write_doc;
    bool out_of_bound;

};

class XmppStreamMessageTest : public ::testing::Test {
public:
    static const int kMaxMessageSize = 4096;

protected:


    virtual void SetUp() {
        evm_.reset(new EventManager());
        a_ = new XmppServer(evm_.get(), XMPP_CONTROL_SERV);
        b_ = new XmppClient(evm_.get());
        thread_.reset(new ServerThread(evm_.get()));
        init_.reset(new XmppInit());

        init_->InitServer(a_, 0, false);

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

        init_->Reset(true);
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

    void AddClientChannel(XmppMockClientConnection *connection) {
        b_->InsertConnection(connection);
    }

    void SetupConnection() {
        XmppChannelConfig cfg(true);

        LOG(DEBUG, "Create client");
        CreateXmppChannelCfg(&cfg, "127.0.0.1", a_->GetPort(), SUB_ADDR,
                             XMPP_CONTROL_SERV, true);
        cconnection_ = new XmppMockClientConnection(b_, &cfg);
        cconnection_->Initialize();
        AddClientChannel(cconnection_);

        LOG(DEBUG, "-- Exectuting --");
    }

    void SetupWriteDocConnection() {
        XmppChannelConfig cfg(true);

        LOG(DEBUG, "Create client");
        CreateXmppChannelCfg(&cfg, "127.0.0.1", a_->GetPort(), SUB_ADDR,
                             XMPP_CONTROL_SERV, true);
        cconnection_ = new XmppMockClientConnection(b_, &cfg, true);
        cconnection_->Initialize();
        AddClientChannel(cconnection_);

        LOG(DEBUG, "-- Exectuting --");
    }

    void SetupOutofBoundConnection() {
        XmppChannelConfig cfg(true);

        LOG(DEBUG, "Create client");
        CreateXmppChannelCfg(&cfg, "127.0.0.1", a_->GetPort(), SUB_ADDR,
                             XMPP_CONTROL_SERV, true);
        cconnection_ = new XmppMockClientConnection(b_, &cfg, true, true);
        cconnection_->Initialize();
        AddClientChannel(cconnection_);

        LOG(DEBUG, "-- Exectuting --");
    }

    void TearDownConnection() {

        cconnection_->ManagedDelete();
        task_util::WaitForIdle();

        cconnection_ = NULL;
    }

    auto_ptr<XmppInit> init_;
    XmppMockClientConnection *cconnection_;
    auto_ptr<EventManager> evm_;
    auto_ptr<ServerThread> thread_;
    XmppServer *a_;
    XmppClient *b_;
};

namespace {

TEST_F(XmppStreamMessageTest, Connection) {
    SetupConnection();

    // server connection
    XmppConnection *sconnection_bad;
    TASK_UTIL_EXPECT_TRUE(
        (sconnection_bad = a_->FindConnection(SUB_ADDR)) != NULL);

    TASK_UTIL_EXPECT_TRUE(cconnection_->open_count == 2);

    // server connection
    XmppConnection *sconnection_good;
    TASK_UTIL_EXPECT_TRUE(
        (sconnection_good = a_->FindConnection(SUB_ADDR)) != NULL);

    // Check for server, client connection is established. Wait upto 1 sec
    TASK_UTIL_EXPECT_TRUE(
        sconnection_good->GetStateMcState() == xmsm::ESTABLISHED);

    TASK_UTIL_EXPECT_TRUE(
        cconnection_->GetStateMcState() == xmsm::ESTABLISHED);

    TearDownConnection();
}

TEST_F(XmppStreamMessageTest, WriteDoc_Connection) {
    SetupWriteDocConnection();

    // server connection
    XmppConnection *sconnection;
    TASK_UTIL_EXPECT_TRUE(
        (sconnection = a_->FindConnection(SUB_ADDR)) != NULL);

    // Check for server, client connection is established. Wait upto 1 sec
    TASK_UTIL_EXPECT_TRUE(
        sconnection->GetStateMcState() == xmsm::ESTABLISHED);

    TASK_UTIL_EXPECT_TRUE(
        cconnection_->GetStateMcState() == xmsm::ESTABLISHED);

    TearDownConnection();
}

TEST_F(XmppStreamMessageTest, CheckMemoryOutofBound_ServerReceiveOpen) {
    // XmppMockClientConnection overloaded SendOpen will send
    // out of bound message, check the out of bound at server side.
    //
    // Allocate a 2k buffer and send the open from client to server.
    // Server side while encoding the OpenConfirm, detects buffer overflow
    // and sends a TCP close to the client
    // At this point, client is in Connect state and receives a Close
    // and moves to Active state.
    SetupOutofBoundConnection();

    // server connection
    XmppConnection *sconnection;
    TASK_UTIL_EXPECT_TRUE(
            (sconnection = a_->FindConnection(SUB_ADDR2)) == NULL);

    // This ensures client state-machine transitioned to OpenSent state
    TASK_UTIL_EXPECT_TRUE(
             cconnection_->open_count > 1);
    // client state-machine transitions to Active state on receiving a
    // a close from server
    TASK_UTIL_EXPECT_TRUE(
             cconnection_->GetStateMcState() == xmsm::ACTIVE);

    TearDownConnection();
}

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
