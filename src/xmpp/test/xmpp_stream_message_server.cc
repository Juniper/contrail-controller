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

#define sXMPP_STREAM_RESP_BAD     "<?xml version='1.0'?><extra/><stream:stream from='dummyserver' to='dummycl' id='++123' version='1.0' xml:lang='en' xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams'/>"
#define sXMPP_STREAM_RESP_GOOD    "<?xml version='1.0'?><stream:stream from='dummyserver' to='dummycl' id='++123' version='1.0' xml:lang='en' xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams'/>"

class XmppMockServerConnection : public XmppServerConnection {
public:
    XmppMockServerConnection(XmppServer *server,
        const XmppChannelConfig *config, bool send_bad_open_resp,
        bool end_write_doc=false)
        : XmppServerConnection(server, config),
          send_bad_open_resp(send_bad_open_resp),
          send_write_doc(send_write_doc) {}

    void SendOpenConfirm(TcpSession *session) {
        if (!session) return;

        XmppProto::XmppStanza::XmppStreamMessage openstream;
        openstream.strmtype =
            XmppStanza::XmppStreamMessage::INIT_STREAM_HEADER_RESP;
        uint8_t data[256];

        //EncodeStream
        auto_ptr<XmlBase> resp_doc(XmppXmlImplFactory::Instance()->GetXmlImpl()); 
        if (send_bad_open_resp == true) { 
            if (resp_doc->LoadDoc(sXMPP_STREAM_RESP_BAD) == -1) { 
                return;
            }
        } else {
            if (resp_doc->LoadDoc(sXMPP_STREAM_RESP_GOOD) == -1) { 
                return;
            }
        }

        // XmppProto::SetTo and XmppProto::SetFrom
        string ns(sXMPP_STREAM_O);
        resp_doc->ReadNode(ns);
        resp_doc->ModifyAttribute("to" , GetComputeHostName());
        resp_doc->ModifyAttribute("from" , GetControllerHostName());

        uint8_t *buf = data;
        int len = 0;
        //Returns byte encoded in the doc
        if (send_write_doc) { 
            len = resp_doc->WriteDoc(buf);
        } else {
            len = resp_doc->WriteRawDoc(buf);
        }

        if (len > 0) {
            string openstr(buf, buf+len);
            boost::algorithm::ireplace_last(openstr, "/", " ");
            memcpy(buf, openstr.c_str(), len);
        }

	assert(len > 0);
	session->Send(data, len, NULL);

        return;
    }

    bool send_bad_open_resp;
    bool send_write_doc;
};


class XmppMockServer : public XmppServer {
public:
    XmppMockServer(EventManager *evm, const std::string &server_addr)
        : XmppServer(evm, server_addr),
          send_bad_open_resp(true),
          sconn_count(0) {
    }

    XmppServerConnection *CreateConnection(XmppSession *session) {

        ip::tcp::endpoint remote_endpoint;
        remote_endpoint.address(session->remote_endpoint().address());
        remote_endpoint.port(0);

        // Create a connection.
        XmppChannelConfig cfg(false);
        cfg.endpoint = remote_endpoint;
        cfg.FromAddr = this->ServerAddr();

        XmppServerConnection *sconnection;
        sconnection =
            new XmppMockServerConnection(this, &cfg, send_bad_open_resp);
        send_bad_open_resp = false;
        sconn_count++;
        return (sconnection);
    }

    bool send_bad_open_resp;
    int sconn_count;
};

class XmppSessionTest : public ::testing::Test {
public:
    static const int kMaxMessageSize = 4096;

protected:
    virtual void SetUp() {
        evm_.reset(new EventManager());
        a_ = new XmppMockServer(evm_.get(), XMPP_CONTROL_SERV);
        b_ = new XmppClient(evm_.get());
        thread_.reset(new ServerThread(evm_.get()));
        init_.reset(new XmppInit()); 

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

        init_->Reset(); 

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

    void SetupConnection() {
        LOG(DEBUG, "Create client");
        client_cfg = new XmppChannelConfig(true);
        CreateXmppChannelCfg(client_cfg, "127.0.0.1", a_->GetPort(), SUB_ADDR,
                             XMPP_CONTROL_SERV, true);
        init_->AddXmppChannelConfig(client_cfg); 
        init_->InitClient(b_);

        LOG(DEBUG, "-- Executing --");
    }

    void TearDownConnection() {
        cconnection_->ManagedDelete();
        task_util::WaitForIdle();
        cconnection_ = NULL;
    }

    auto_ptr<XmppInit> init_;
    XmppChannelConfig *client_cfg;
    XmppConnection *cconnection_;
    auto_ptr<EventManager> evm_;
    auto_ptr<ServerThread> thread_;
    XmppMockServer *a_;
    XmppClient *b_;
};


namespace {

TEST_F(XmppSessionTest, Connection) {
    SetupConnection();

    XmppConnection *connection;
    TASK_UTIL_EXPECT_TRUE(
        (connection = b_->FindConnection(XMPP_CONTROL_SERV)) != NULL);
    cconnection_ = connection;

    TASK_UTIL_EXPECT_TRUE(a_->sconn_count == 2);

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
