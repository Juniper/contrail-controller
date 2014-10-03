/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_state_machine.h"

#include <boost/assign.hpp>
#include <boost/asio.hpp>

#include "base/logging.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "control-node/control_node.h"

#include "io/event_manager.h"
#include "io/test/event_manager_test.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "xmpp/xmpp_proto.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_session.h"

#include "testing/gunit.h"

using namespace std;
using namespace boost::assign;

namespace ip = boost::asio::ip;

#define XMPP_CONTROL_SERV   "bgp.contrail.com"

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
        SetUpServerSideConnection(); //Accept
    }

    virtual void TearDown() {
        connection_->Shutdown();
        connection_->deleter()->ResumeDelete();
        task_util::WaitForIdle();
        server_->Shutdown();
        task_util::WaitForIdle();
        TcpServerManager::DeleteServer(server_);
        evm_->Shutdown();
    }

    void SetUpServerSideConnection() {
        ConcurrencyScope scope("bgp::Config");

        LOG(DEBUG, "Mock Server side connection created on accept");
        ip::tcp::endpoint remote_endpoint;
        remote_endpoint.port(0);

        XmppChannelConfig cfg(false);
        cfg.endpoint = remote_endpoint;
        cfg.FromAddr = server_->ServerAddr();

        connection_ = new XmppServerConnection(server_, &cfg);
        sm_ = connection_->state_machine_.get();
        server_->InsertConnection(connection_);
        connection_->deleter()->PauseDelete();
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
            // Cases like HoldTimerExpiry,TcpClose need to cleanup 
            // connection_ on server side.
            EXPECT_TRUE(connection_->session() == NULL);
            break;
        case xmsm::ACTIVE:
            if (sm_->IsActiveChannel()) {
                EXPECT_TRUE(ConnectTimerRunning() != OpenTimerRunning());
            } else {
                EXPECT_TRUE(!ConnectTimerRunning());
                //OpenTimer not running initially but
                //started after EvTcpPassiveOpen
            }
            EXPECT_TRUE(!HoldTimerRunning());
            EXPECT_TRUE(connection_->session() == NULL);
            break;
        case xmsm::OPENCONFIRM:
            EXPECT_TRUE(!ConnectTimerRunning());
            EXPECT_TRUE(!OpenTimerRunning());
            EXPECT_TRUE(HoldTimerRunning());
            EXPECT_TRUE(sm_->session() != NULL);
            EXPECT_TRUE(connection_->session() != NULL);
            break;
        case xmsm::ESTABLISHED:
            EXPECT_TRUE(!ConnectTimerRunning());
            EXPECT_TRUE(!OpenTimerRunning());
            EXPECT_TRUE(HoldTimerRunning());
            EXPECT_TRUE(sm_->session() != NULL);
            EXPECT_TRUE(connection_->session() != NULL);
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
    void EvStop() {
        sm_->Clear();
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
    void EvTcpPassiveOpen() {
        ip::tcp::endpoint remote_endpoint;
        remote_endpoint.port(0);
        session_ = static_cast<XmppSession *>(server_->CreateSession());
        connection_->AcceptSession(session_);
    }
    // Note EvTcpPassiveOpen() sets AsyncRead which will detect
    // other end of socket is not connected and closes the connection
    // Hence skip EvTcpPassiveOpen(), only set the session
    void EvTcpPassiveOpenFake() {
        ip::tcp::endpoint remote_endpoint;
        remote_endpoint.port(0);
        boost::system::error_code ec;
        session_ = static_cast<XmppSession *>(server_->CreateSession());
        session_->socket()->non_blocking(true, ec);
        // Do not generate PassiveOpen Event
        session_->SetConnection(connection_);
        sm_->set_session(session_);
    }

    void EvXmppOpen() {
        XmppStanza::XmppMessage *msg_; 
        msg_ = new XmppStanza::XmppMessage(XmppStanza::STREAM_HEADER);
        msg_->type = XmppStanza::STREAM_HEADER;
        msg_->from = "agent";
        msg_->to = "bgp.contrail.com";
        sm_->OnMessage(session_, msg_);     
    }

    void EvXmppKeepalive() {
        XmppStanza::XmppMessage *msg_; 
        msg_ = new XmppStanza::XmppMessage(XmppStanza::WHITESPACE_MESSAGE_STANZA);
        msg_->type =  XmppStanza::WHITESPACE_MESSAGE_STANZA;
        sm_->OnMessage(session_, msg_);
    }

    void EvTcpClose() {
        sm_->OnSessionEvent(session_,TcpSession::CLOSE); 
    }
   
    void EvXmppMessageReceive() {
        XmppStanza::XmppMessage *msg_; 
        msg_ = new XmppStanza::XmppMessage(XmppStanza::MESSAGE_STANZA);
        msg_->type =  XmppStanza::MESSAGE_STANZA;
        sm_->OnMessage(session_, msg_);
    }

    bool ConnectTimerRunning() { return(sm_->connect_timer_->running()); }
    bool OpenTimerRunning() { return(sm_->open_timer_->running()); }
    bool HoldTimerRunning() { return(sm_->hold_timer_->running()); }

    void FireConnectTimer() { sm_->connect_timer_->Fire(); }
    void FireOpenTimer() { sm_->open_timer_->Fire(); }
    void FireHoldTimer() { sm_->hold_timer_->Fire(); }


    auto_ptr<EventManager> evm_;

    XmppServer *server_;
    XmppStateMachine *sm_;
    XmppServerConnection *connection_;
    XmppSession *session_;
};

namespace {

//Verify Active state on EvStart
TEST_F(XmppStateMachineTest, Idle) {
    EvStop();
    VerifyState(xmsm::IDLE);

    EvStart();
    VerifyState(xmsm::ACTIVE);
}

// Test SendClose on OpenTimer expiry
// OldState : Active
// Event    : EvOpenTimerExpired
// NewState : Active with SendClose
TEST_F(XmppStateMachineTest, OpenTimerExpired) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpen();
    VerifyState(xmsm::ACTIVE);

    EvOpenTimerExpired();
    VerifyState(xmsm::IDLE);
}

// OldState :Active
// Event    :EvTcpClose
// NewState :Idle
TEST_F(XmppStateMachineTest, Active_EvTcpClose) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvTcpClose();
    VerifyState(xmsm::IDLE);
}

// OldState :Active
// Event    :EvStop
// NewState :Idle
TEST_F(XmppStateMachineTest, Active_EvStop) {
    VerifyState(xmsm::ACTIVE);

    EvStop();
    VerifyState(xmsm::IDLE);
}

// OldState :Active
// Event    :EvStop
// NewState :Idle
TEST_F(XmppStateMachineTest, Active_EvPassive_EvStop) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvStop();
    VerifyState(xmsm::IDLE);
}


// Old State : OpenConfirm 
// Event     : EvTcpClose
// New State : Idle
TEST_F(XmppStateMachineTest, DISABLED_OpenConfirm_EvTcpClose) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);

    EvTcpClose();
    VerifyState(xmsm::IDLE);
}

// Old State : OpenConfirm 
// Event     : EvHoldTimerExpired
// New State : Idle
TEST_F(XmppStateMachineTest, DISABLED_OpenConfirm_EvHoldTimerExpired) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);

    EvHoldTimerExpired();
    VerifyState(xmsm::IDLE);
}

//EvXmppOpen & EvXmppKeepalive
// Old State : OpenConfirm 
// Event     : EvXmppKeepalive
// New State : Established
TEST_F(XmppStateMachineTest, EvXmppOpen) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::ESTABLISHED);

    // Old State : Established 
    // Event     : EvXmppKeepalive
    // New State : Established
    EvXmppKeepalive();
    VerifyState(xmsm::ESTABLISHED);

    //Close the connection as we verify by
    //maintaing a ref-count on TcpSession
    EvTcpClose();
    VerifyState(xmsm::IDLE);
}

//Test HoldTimer Expiry
// OldState :Established
// Event    :EvHoldTimerExpired
// NewState :Idle
TEST_F(XmppStateMachineTest, Established_EvHoldTimerExpired) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();

    VerifyState(xmsm::ESTABLISHED);

    EvHoldTimerExpired();
    VerifyState(xmsm::IDLE);
}

// Old State : Established 
// Event     : EvTcpClose
// New State : Idle
TEST_F(XmppStateMachineTest, Established_EvTcpClose) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();

    VerifyState(xmsm::ESTABLISHED);

    EvTcpClose();
    VerifyState(xmsm::IDLE);
}

// Old State : Established 
// Event     : EvStop
// New State : Idle
TEST_F(XmppStateMachineTest, Established_EvStop) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();

    VerifyState(xmsm::ESTABLISHED);

    EvStop();
    VerifyState(xmsm::IDLE);
}


// Old State : Established 
// Event     : EvXmppMessageReceive
// New State : Established 
TEST_F(XmppStateMachineTest, EvXmppMessageReceive) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::ESTABLISHED);

    EvXmppMessageReceive();
    VerifyState(xmsm::ESTABLISHED);

    EvTcpClose();
    VerifyState(xmsm::IDLE);
}
}

int main(int argc, char **argv) {
    LoggingInit();
    ::testing::InitGoogleTest(&argc, argv);
    ControlNode::SetDefaultSchedulingPolicy();
    Sandesh::SetLocalLogging(true);
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
