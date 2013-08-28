/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_state_machine.h"

#include <boost/assign.hpp>
#include <boost/asio.hpp>

#include "base/logging.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"

#include "io/event_manager.h"
#include "io/test/event_manager_test.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "xmpp/xmpp_proto.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_client.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_session.h"

#include "testing/gunit.h"

using namespace std;
using namespace boost::assign;

namespace ip = boost::asio::ip;

#define XMPP_CONTROL_SERV   "bgp.contrail.com"

class XmppClientTest : public XmppClient {
public:
    XmppClientTest(EventManager *evm) :
        XmppClient(evm) {
    }

    ~XmppClientTest() {
    }

    void Connect(TcpSession *session, Endpoint remote) {
    }
};

class XmppStateMachineTest : public ::testing::Test {
protected:
    XmppStateMachineTest() {
        evm_.reset(new EventManager());
    }

    ~XmppStateMachineTest() { 
    }

    virtual void SetUp() {
        server_ = new XmppServer(evm_.get(), XMPP_CONTROL_SERV);
        server_->Initialize(0, false);
        LOG(DEBUG, "Created Xmpp server at port: " << server_->GetPort());

        client_ = new XmppClientTest(evm_.get());
        SetUpClientSideConnection();
    }

    virtual void TearDown() {
        LOG(DEBUG, "TearDown TearDown TearDown \n\n");
        server_->Shutdown();
        TcpServerManager::DeleteServer(server_);
        server_ = NULL;
        task_util::WaitForIdle();
 
        client_->Shutdown();
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(client_);
        client_ = NULL;
        task_util::WaitForIdle();
        evm_->Shutdown();
    }

    void SetUpClientSideConnection() {

        ip::tcp::endpoint remote_endpoint;
        remote_endpoint.port(server_->GetPort());

        XmppChannelConfig cfg(true);
        cfg.endpoint = remote_endpoint;

        connection_ = client_->CreateConnection(&cfg);
        sm_ = connection_->state_machine_.get();
    }

    void VerifyState(xmsm::XmState state) {
        LOG(DEBUG, "VerifyState " << state);
        task_util::WaitForIdle();
        TaskScheduler::GetInstance()->Stop();

        EXPECT_EQ(state, sm_->StateType());
        evm_->Poll();

        switch (state) {
        case xmsm::IDLE:
            //active & passive sessions
            EXPECT_TRUE(!ConnectTimerRunning());
            EXPECT_TRUE(!OpenTimerRunning());
            EXPECT_TRUE(!HoldTimerRunning());
            EXPECT_TRUE(sm_->session() == NULL);
            EXPECT_TRUE(connection_->session() == NULL);
            break;
        case xmsm::ACTIVE:
            EXPECT_TRUE(sm_->IsActiveChannel());
            EXPECT_TRUE(ConnectTimerRunning());
            EXPECT_TRUE(!OpenTimerRunning());
            EXPECT_TRUE(!HoldTimerRunning());
            EXPECT_TRUE(connection_->session() == NULL);
            //EXPECT_TRUE(sm_->session() == NULL);
            break;
        case xmsm::CONNECT:
            EXPECT_TRUE(sm_->IsActiveChannel());
            EXPECT_TRUE(ConnectTimerRunning());
            EXPECT_TRUE(!OpenTimerRunning());
            EXPECT_TRUE(!HoldTimerRunning());
            EXPECT_TRUE(sm_->connection() != NULL);
            EXPECT_TRUE(sm_->session() != NULL);
            break;
        case xmsm::OPENSENT:
            EXPECT_TRUE(sm_->IsActiveChannel());
            EXPECT_TRUE(!ConnectTimerRunning());
            EXPECT_TRUE(!OpenTimerRunning());
            EXPECT_TRUE(HoldTimerRunning());
            EXPECT_TRUE(sm_->connection() != NULL);
            EXPECT_TRUE(sm_->session() != NULL);
            break;
        case xmsm::ESTABLISHED:
            EXPECT_TRUE(!ConnectTimerRunning());
            EXPECT_TRUE(!OpenTimerRunning());
            EXPECT_TRUE(HoldTimerRunning());
            EXPECT_TRUE(sm_->session() != NULL);
            EXPECT_TRUE(connection_->session() != NULL);
            EXPECT_TRUE(sm_->GetConnectTime() == 0);
            break;
        default:
            ASSERT_TRUE(false);
            break;
        }

        TaskScheduler::GetInstance()->Start();
    }

    void EvStart() {
        sm_->Initialize();
    }
    void EvConnectTimerExpired() {
        boost::system::error_code error;
        FireConnectTimer();
    }
    void EvOpenTimerExpired() {
        boost::system::error_code error;
        FireOpenTimer();
    }
    void EvHoldTimerExpired() {
        FireHoldTimer();
    }

    void EvTcpConnected() {
        session_ = sm_->session();
        sm_->OnSessionEvent(session_,TcpSession::CONNECT_COMPLETE); 
    }

    void EvXmppOpen() {
        XmppStanza::XmppMessage *msg_; 
        msg_ = new XmppStanza::XmppMessage(XmppStanza::STREAM_HEADER);
        msg_->type = XmppStanza::STREAM_HEADER;
        msg_->from = "bgp.contrail.com";
        msg_->to = "agent";
        sm_->OnMessage(session_, msg_);     
    }

    void EvXmppKeepalive() {
        XmppStanza::XmppMessage *msg_; 
        msg_ = new XmppStanza::XmppMessage(XmppStanza::WHITESPACE_MESSAGE_STANZA);
        msg_->type =  XmppStanza::WHITESPACE_MESSAGE_STANZA;
        sm_->OnMessage(session_, msg_);
    }

    void EvXmppMessageReceive() {
        XmppStanza::XmppMessage *msg_; 
        msg_ = new XmppStanza::XmppMessage(XmppStanza::MESSAGE_STANZA);
        msg_->type =  XmppStanza::MESSAGE_STANZA;
        sm_->OnMessage(session_, msg_);
    }

    void EvTcpConnectFailed() {
        session_ = sm_->session();
        sm_->OnSessionEvent(session_,TcpSession::CONNECT_FAILED); 
    }
    void EvTcpClose() {
        session_ = sm_->session();
        sm_->OnSessionEvent(session_,TcpSession::CLOSE); 
    }

    bool ConnectTimerRunning() { return(sm_->connect_timer_->running()); }
    bool OpenTimerRunning() { return(sm_->open_timer_->running()); }
    bool HoldTimerRunning() { return(sm_->hold_timer_->running()); }

    void FireConnectTimer() { sm_->connect_timer_->Fire(); }
    void FireOpenTimer() { sm_->open_timer_->Fire(); }
    void FireHoldTimer() { sm_->hold_timer_->Fire(); }


    auto_ptr<EventManager> evm_;

    XmppServer *server_;
    XmppClientTest *client_;

    XmppStateMachine *sm_;
    XmppConnection *connection_;
    //auto_ptr<XmppConnection> connection_;
    XmppSession *session_;
};

namespace {

// OldState : Idle
// Event    : EvStart
// NewState : Active 
TEST_F(XmppStateMachineTest, Idle_EvStart) {

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);
}

// OldState : Active
// Event    : EvTcpConnectTimerExpired
// NewState : Connect 
TEST_F(XmppStateMachineTest, Active_EvTcpConnectTimerExpired) {

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);
}

// OldState : Connect 
// Event    : EvConnectTimerExpired
// NewState : Active 
TEST_F(XmppStateMachineTest, Connect_EvTcpConnectTimerExpired) {

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvConnectTimerExpired();
    VerifyState(xmsm::ACTIVE);
}

// OldState : Connect 
// Event    : EvConnectFail
// NewState : Active 
TEST_F(XmppStateMachineTest, Connect_EvTcpConnectFailed) {

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvTcpConnectFailed();
    VerifyState(xmsm::ACTIVE);
}


// OldState : Connect 
// Event    : EvTcpConnected
// NewState : OpenSent 
TEST_F(XmppStateMachineTest, Connect_EvTcpConnected) {

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvTcpConnected();
    VerifyState(xmsm::OPENSENT);
}

// OldState : Connect 
// Event    : EvTcpClose
// NewState : Active 
TEST_F(XmppStateMachineTest, Connect_EvTcpClose) {

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvTcpClose();
    VerifyState(xmsm::ACTIVE);
}

// OldState : OpenSent 
// Event    : EvXmppOpen
// NewState : Established 
TEST_F(XmppStateMachineTest, OpenSent_EvXmppOpen) {

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvTcpConnected();
    VerifyState(xmsm::OPENSENT);

    EvXmppOpen();
    VerifyState(xmsm::ESTABLISHED);
}


// OldState : OpenSent 
// Event    : EvTcpClose
// NewState : Active 
TEST_F(XmppStateMachineTest, OpenSent_EvTcpClose) {

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvTcpConnected();
    VerifyState(xmsm::OPENSENT);

    EvTcpClose();
    VerifyState(xmsm::ACTIVE);
}

// OldState : OpenSent 
// Event    : EvHoldTimerExpired
// NewState : Active 
TEST_F(XmppStateMachineTest, OpenSent_EvHoldTimerExpired) {

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvTcpConnected();
    VerifyState(xmsm::OPENSENT);

    EvHoldTimerExpired();
    VerifyState(xmsm::ACTIVE);
}

// OldState : Established 
// Event    : EvXmppKeepAlive
// NewState : Established 
TEST_F(XmppStateMachineTest, Established_EvXmppKeepAlive) {

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvTcpConnected();
    VerifyState(xmsm::OPENSENT);

    EvXmppOpen();
    VerifyState(xmsm::ESTABLISHED);

    EvXmppKeepalive();
    VerifyState(xmsm::ESTABLISHED);
}

// OldState : Established 
// Event    : EvXmppMessageReceive
// NewState : Established 
TEST_F(XmppStateMachineTest, Established_EvXmppMessageReceive) {

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvTcpConnected();
    VerifyState(xmsm::OPENSENT);

    EvXmppOpen();
    VerifyState(xmsm::ESTABLISHED);

    EvXmppMessageReceive();
    VerifyState(xmsm::ESTABLISHED);
}

// OldState : Established 
// Event    : EvHoldTimerExpired 
// NewState : Active 
TEST_F(XmppStateMachineTest, Established_EvHoldTimerExpired) {

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvTcpConnected();
    VerifyState(xmsm::OPENSENT);

    EvXmppOpen();
    VerifyState(xmsm::ESTABLISHED);

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    EvHoldTimerExpired();
    VerifyState(xmsm::ACTIVE);
}

// OldState : Established 
// Event    : EvHoldTimerExpired 
// NewState : Active 
// Event    : EvTcpConnectFailed
// NewState : Active
// Event    : EvTcpConnect
// NewState : OpenSent
TEST_F(XmppStateMachineTest, Established_EvHoldTimerExpired_connectfail) {

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvTcpConnected();
    VerifyState(xmsm::OPENSENT);

    EvXmppOpen();
    VerifyState(xmsm::ESTABLISHED);

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    EvHoldTimerExpired();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvTcpConnectFailed();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvTcpConnected();
    VerifyState(xmsm::OPENSENT);
}

// OldState : Connect 
// Event    : EvTcpConnectFailed
// NewState : Active 
// Event    : EvTcpConnected
// NewState : OpenSent 
TEST_F(XmppStateMachineTest, Connect_EvTcpConnectFailed_EvTcpConnected) {

    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    sm_->connect_attempts_inc(); // set attempts as we do not
                                 // want connection timer to expire
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvTcpConnectFailed();
    VerifyState(xmsm::ACTIVE);

    EvConnectTimerExpired();
    VerifyState(xmsm::CONNECT);

    EvTcpConnected();
    VerifyState(xmsm::OPENSENT);
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
