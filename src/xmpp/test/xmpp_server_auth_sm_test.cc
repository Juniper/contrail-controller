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

class XmppMockServerConnection : public XmppServerConnection {
public:
    XmppMockServerConnection(XmppServer *server, const XmppChannelConfig *config)
        : XmppServerConnection(server, config), ssl_cb_count_(0) {
    }

    void SslHandShakeResponseCb(SslSessionPtr session,
        const boost::system::error_code& error) {
        ssl_cb_count_++;
    }

    size_t GetSslcbCount() {
        return ssl_cb_count_;
    }

private:
    size_t ssl_cb_count_;
};


class XmppStateMachineTest : public ::testing::Test {
protected:
    XmppStateMachineTest() {
        evm_.reset(new EventManager());
    }

    ~XmppStateMachineTest() {
    }

    virtual void SetUp() {
        XmppChannelConfig server_cfg(false); // isClient_ = false
        server_cfg.auth_enabled = true;
        server_cfg.path_to_server_cert =
            "controller/src/xmpp/testdata/server-build02.pem";
        server_cfg.path_to_server_priv_key =
            "controller/src/xmpp/testdata/server-build02.key";

        server_ = new XmppServer(evm_.get(), XMPP_CONTROL_SERV, &server_cfg);
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

        XmppChannelConfig cfg(false); // isClient_ = false
        cfg.endpoint = remote_endpoint;
        cfg.FromAddr = server_->ServerAddr();
        cfg.auth_enabled = true;

        connection_ = new XmppMockServerConnection(server_, &cfg);
        sm_ = connection_->state_machine_.get();
        /* override the default ssl handshake cb */
        SslHandShakeCallbackHandler cb = boost::bind(
            &XmppMockServerConnection::SslHandShakeResponseCb, connection_, _1, _2);
        sm_->SetHandShakeCbHandler(cb);
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

    void VerifyOpenConfirmState(xmsm::XmOpenConfirmState state) {
        LOG(DEBUG, "VerifyOpenConfirmState " << state);
        task_util::WaitForIdle();
        TaskScheduler::GetInstance()->Stop();

        EXPECT_EQ(state, sm_->get_openconfirm_state());
        evm_->Poll();

        switch (state) {
        case xmsm::OPENCONFIRM_INIT:
            EXPECT_TRUE(HoldTimerRunning());
            break;
        case xmsm::OPENCONFIRM_FEATURE_NEGOTIATION:
            EXPECT_TRUE(HoldTimerRunning());
            break;
        case xmsm::OPENCONFIRM_FEATURE_SUCCESS:
            EXPECT_TRUE(HoldTimerRunning());
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
        XmppStanza::XmppStreamMessage *msg_;
        msg_ = new XmppStanza::XmppStreamMessage();
        msg_->strmtype = XmppStanza::XmppStreamMessage::INIT_STREAM_HEADER;
        msg_->from = "agent";
        msg_->to = "bgp.contrail.com";
        sm_->OnMessage(session_, msg_);
    }

    void EvStartTls() {
        XmppStanza::XmppStreamMessage *msg_;
        msg_ = new XmppStanza::XmppStreamMessage();
        msg_->strmtype = XmppStanza::XmppStreamMessage::FEATURE_TLS;
        msg_->strmtlstype = XmppStanza::XmppStreamMessage::TLS_START;
        msg_->from = "agent";
        msg_->to = "bgp.contrail.com";
        sm_->OnMessage(session_, msg_);
    }

    void EvTlsHandShakeSuccess() {
        sm_->OnEvent(session_, xmsm::EvTLSHANDSHAKE_SUCCESS);
    }

    void EvTlsHandShakeFailure() {
        sm_->OnEvent(session_, xmsm::EvTLSHANDSHAKE_FAILURE);
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

    void EvXmppMessageStanza() {
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
    XmppMockServerConnection *connection_;
    XmppSession *session_;
};

namespace {

// Old State : Active
// Event     : EvXmppOpen
// New State : OpenConfirm_Init
TEST_F(XmppStateMachineTest, Active__EvXmppOpen) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);
}

// Old State : OpenConfirm
// Event     : EvHoldTimerExpired
// New State : Idle
TEST_F(XmppStateMachineTest, OpenConfirm__EvHoldTimerExpired) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvHoldTimerExpired();
    VerifyState(xmsm::IDLE);
}

// Old State : OpenConfirm
// Event     : EvXmppKeepalive
// New State : OpenConfirm
TEST_F(XmppStateMachineTest, OpenConfirm__EvXmppKeepalive) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvXmppKeepalive();
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);
    VerifyState(xmsm::OPENCONFIRM);
}


// Old State : OpenConfirm
// Event     : EvTcpClose
// New State : Idle
TEST_F(XmppStateMachineTest, OpenConfirm__EvTcpClose) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    //Close the connection as we verify by
    //maintaing a ref-count on TcpSession
    EvTcpClose();
    VerifyState(xmsm::IDLE);
}

// Old State : OpenConfirm
// Event     : EvStartTls
// New State : OpenConfirm
TEST_F(XmppStateMachineTest, OpenConfirm_Init__EvStartTls) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStartTls();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);
    EXPECT_TRUE(connection_->GetSslcbCount() == 1);
}

// Old State : OpenConfirm
// Event     : EvTlsHandShakeFailure
// New State : Idle
TEST_F(XmppStateMachineTest, OpenConfirm_Init__EvHandShakeFailure) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStartTls();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EXPECT_TRUE(connection_->GetSslcbCount() == 1);
    EvTlsHandShakeFailure();
    VerifyState(xmsm::IDLE);
}

// Old State : OpenConfirm
// Event     : EvTlsHandShakeSuccess
// New State : OpenConfirm
TEST_F(XmppStateMachineTest, OpenConfirm_Init__EvHandShakeSuccess) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStartTls();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EXPECT_TRUE(connection_->GetSslcbCount() == 1);
    EvTlsHandShakeSuccess();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);
}

// Old State : OpenConfirm
// Event     : EvXmppOpen
// New State : Established
TEST_F(XmppStateMachineTest, OpenConfirm__Feature_Success__EvXmpOpen) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStartTls();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EXPECT_TRUE(connection_->GetSslcbCount() == 1);
    EvTlsHandShakeSuccess();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);

    EvXmppOpen();
    VerifyState(xmsm::ESTABLISHED);
}

// Old State : OpenConfirm
// Event     : EvXmppKeepalive
// New State : OpenConfirm
TEST_F(XmppStateMachineTest, OpenConfirm_Feature_Success__EvXmppKeepalive) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStartTls();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EXPECT_TRUE(connection_->GetSslcbCount() == 1);
    EvTlsHandShakeSuccess();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);

    EvXmppKeepalive();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);
}

// Old State : OpenConfirm
// Event     : EvHoldTimerExpired
// New State : Idle
TEST_F(XmppStateMachineTest, OpenConfirm_Feature_Success__EvHoldTimerExpired) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStartTls();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EXPECT_TRUE(connection_->GetSslcbCount() == 1);
    EvTlsHandShakeSuccess();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);

    EvHoldTimerExpired();
    VerifyState(xmsm::IDLE);
}


// Old State : Established
// Event     : EvTcpClose
// New State : Idle
TEST_F(XmppStateMachineTest, Established__EvTcpClose) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStartTls();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EXPECT_TRUE(connection_->GetSslcbCount() == 1);
    EvTlsHandShakeSuccess();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);

    EvXmppOpen();
    VerifyState(xmsm::ESTABLISHED);

    EvTcpClose();
    VerifyState(xmsm::IDLE);
}


// Old State : Established
// Event     : EvStop
// New State : Idle
TEST_F(XmppStateMachineTest, Established__EvStop) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStartTls();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EXPECT_TRUE(connection_->GetSslcbCount() == 1);
    EvTlsHandShakeSuccess();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);

    EvXmppOpen();
    VerifyState(xmsm::ESTABLISHED);

    EvStop();
    VerifyState(xmsm::IDLE);
}


// Old State : Established
// Event     : EvXmppMessageStanza
// New State : Established
TEST_F(XmppStateMachineTest, Established__EvXmppMessageReceive) {
    VerifyState(xmsm::ACTIVE);

    EvTcpPassiveOpenFake();
    VerifyState(xmsm::ACTIVE);

    EvXmppOpen();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStartTls();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EXPECT_TRUE(connection_->GetSslcbCount() == 1);
    EvTlsHandShakeSuccess();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);

    EvXmppOpen();
    VerifyState(xmsm::ESTABLISHED);

    EvXmppMessageStanza();
    VerifyState(xmsm::ESTABLISHED);
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
