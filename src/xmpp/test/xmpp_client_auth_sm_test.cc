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

class XmppMockClientConnection : public XmppClientConnection {
public:
    XmppMockClientConnection(XmppClient *server, const XmppChannelConfig *config)
        : XmppClientConnection(server, config) {
    }

    void SslHandShakeResponseCb(SslSessionPtr session,
        const boost::system::error_code& error) {
    }
};


class XmppClientTest : public XmppClient {
public:
    XmppClientTest(EventManager *evm, const XmppChannelConfig *cfg) :
        XmppClient(evm, cfg) {
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
        XmppChannelConfig server_cfg(false); // isClient_ = false
        server_cfg.auth_enabled = true;
        server_cfg.path_to_server_cert = "controller/src/xmpp/testdata/server-build02.pem";
        server_cfg.path_to_server_priv_key = "controller/src/xmpp/testdata/server-build02.key";

        server_ = new XmppServer(evm_.get(), XMPP_CONTROL_SERV, &server_cfg);
        server_->Initialize(0, false);
        LOG(DEBUG, "Created Xmpp server at port: " << server_->GetPort());

        SetUpClientSideConnection();
    }

    virtual void TearDown() {
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

        XmppChannelConfig cfg(true); // isClient_ = true
        cfg.endpoint = remote_endpoint;
        cfg.auth_enabled = true;
        cfg.path_to_server_cert = "controller/src/xmpp/testdata/server-build02.pem";
        cfg.path_to_server_priv_key = "controller/src/xmpp/testdata/server-build02.key";

        /* Create XmppClient */
        client_ = new XmppClientTest(evm_.get(), &cfg);
        connection_ = new XmppMockClientConnection(client_, &cfg);
        client_->InsertConnection(connection_);
        sm_ = connection_->state_machine_.get();
        /* set ssl handshake cb */
        SslHandShakeCallbackHandler cb = boost::bind(
            &XmppMockClientConnection::SslHandShakeResponseCb, connection_, _1, _2);
        sm_->SetHandShakeCbHandler(cb);
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
        case xmsm::OPENCONFIRM:
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
            EXPECT_TRUE(sm_->get_connect_attempts() != 0);
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

    void EvTcpConnected() {
        session_ = sm_->session();
        sm_->OnSessionEvent(session_,TcpSession::CONNECT_COMPLETE);
    }

    void EvXmppOpen() {
        XmppStanza::XmppStreamMessage *msg_;
        msg_ = new XmppStanza::XmppStreamMessage();
        msg_->strmtype = XmppStanza::XmppStreamMessage::INIT_STREAM_HEADER;
        msg_->from = "bgp.contrail.com";
        msg_->to = "agent";
        sm_->OnMessage(session_, msg_);
    }

    void EvStreamFeatureRequest() {
        XmppStanza::XmppStreamMessage *msg_;
        msg_ = new XmppStanza::XmppStreamMessage();
        msg_->strmtype = XmppStanza::XmppStreamMessage::FEATURE_TLS;
        msg_->strmtlstype = XmppStanza::XmppStreamMessage::TLS_FEATURE_REQUEST;
        msg_->from = "bgp.contrail.com";
        msg_->to = "agent";
        sm_->OnMessage(session_, msg_);
    }

    void EvTlsProceed() {
        XmppStanza::XmppStreamMessage *msg_;
        msg_ = new XmppStanza::XmppStreamMessage();
        msg_->strmtype = XmppStanza::XmppStreamMessage::FEATURE_TLS;
        msg_->strmtlstype = XmppStanza::XmppStreamMessage::TLS_PROCEED;
        msg_->from = "bgp.contrail.com";
        msg_->to = "agent";
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

    void EvXmppMessageStanza() {
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
    XmppMockClientConnection *connection_;
    XmppSession *session_;
};

namespace {

// OldState : OpenSent
// Event    : EvXmppOpen
// NewState : OpenConfirm
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
    VerifyState(xmsm::OPENCONFIRM);
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

// OldState : OpenSent
// Event    : EvStop
// NewState : Active
TEST_F(XmppStateMachineTest, OpenSent_EvStop) {

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

    EvStop();
    VerifyState(xmsm::ACTIVE);
}


// OldState : OpenConfirm (OPENCONFIRM_INIT)
// Event    : EvXmppKeepAlive
// NewState : OpenConfirm
TEST_F(XmppStateMachineTest, OpenConfirm_Init__EvXmppKeepAlive) {

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
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvXmppKeepalive(); //To be discarded
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);
}


// OldState : OpenConfirm (OPENCONFIRM_INIT)
// Event    : EvHoldTimerExpired
// NewState : Active
TEST_F(XmppStateMachineTest, OpenConfirm_Init__HoldTimerExpired) {

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
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvHoldTimerExpired();
    VerifyState(xmsm::ACTIVE);
}


// OldState : OpenConfirm(OPENCONFIRM_INIT)
// Event    : EvStreamFeatureRequest
// NewState : OpenConfirm(OPENCONFIRM_FEATURE_NEGOTIATION)
TEST_F(XmppStateMachineTest, OpenConfirm_Init__EvStreamFeatureRequest) {

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
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStreamFeatureRequest();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);
}

// OldState : OpenConfirm(OPENCONFIRM_FEATURE_NEGOTIATION)
// Event    : EvHoldTimerExpired
// NewState : Active
TEST_F(XmppStateMachineTest, OpenConfirm_FeatureNegotiation__EvHoldTimerExpired) {

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
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStreamFeatureRequest();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvHoldTimerExpired();
    VerifyState(xmsm::ACTIVE);
}


// OldState : OpenConfirm(OPENCONFIRM_FEATURE_NEGOTIATION)
// Event    : EvTlsProceed
// NewState : OpenConfirm(OPENCONFIRM_FEATURE_NEGOTIATION)
TEST_F(XmppStateMachineTest, OpenConfirm_EvTlsProceed) {

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
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStreamFeatureRequest();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsProceed();
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);
}


// OldState : OpenConfirm(OPENCONFIRM_FEATURE_NEGOTIATION)
// Event    : EvHoldTimerExpired while TLS negotiation in progress
// NewState : Active
TEST_F(XmppStateMachineTest, OpenConfirm_TlsProceed__EvHoldTimerExpired) {

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
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStreamFeatureRequest();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsProceed();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvHoldTimerExpired();
    VerifyState(xmsm::ACTIVE);
}

// OldState : OpenConfirm(OPENCONFIRM_FEATURE_NEGOTIATION)
// Event    : EvTcpClose while TLS negotiation in progress
// NewState : Active
TEST_F(XmppStateMachineTest, OpenConfirm_TlsProceed__EvTcpClose) {

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
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStreamFeatureRequest();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsProceed();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTcpClose();
    VerifyState(xmsm::ACTIVE);
}


// OldState : OpenConfirm(OPENCONFIRM_FEATURE_NEGOTIATION)
// Event    : EvStop while TLS negotiation in progress
// NewState : Active
TEST_F(XmppStateMachineTest, OpenConfirm_TlsProceed__EvStop) {

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
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStreamFeatureRequest();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsProceed();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvStop();
    VerifyState(xmsm::ACTIVE);
}


// OldState : OpenConfirm(OPENCONFIRM_FEATURE_NEGOTIATION)
// Event    : EvTlsHandShakeSuccess
// NewState : OpenConfirm(OPENCONFIRM_FEATURE_SUCCESS)
TEST_F(XmppStateMachineTest, OpenConfirm_EvTlsHandShakeSuccess) {

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
    VerifyState(xmsm::OPENCONFIRM);

    EvStreamFeatureRequest();
    VerifyState(xmsm::OPENCONFIRM);

    EvTlsProceed();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsHandShakeSuccess();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);
}

// OldState : OpenConfirm(OPENCONFIRM_FEATURE_SUCESS)
// Event    : EvHoldTimerExpired
// NewState : Active
TEST_F(XmppStateMachineTest, OpenConfirm_FeatureSucess__EvHoldTimerExpired) {

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
    VerifyState(xmsm::OPENCONFIRM);

    EvStreamFeatureRequest();
    VerifyState(xmsm::OPENCONFIRM);

    EvTlsProceed();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsHandShakeSuccess();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);

    EvHoldTimerExpired();
    VerifyState(xmsm::ACTIVE);
}


// OldState : OpenConfirm(OPENCONFIRM_FEATURE_NEGOTIATION)
// Event    : EvTlsHandShakeFailure
// NewState : Active
TEST_F(XmppStateMachineTest, OpenConfirm_EvTlsHandShakeFailure) {

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
    VerifyState(xmsm::OPENCONFIRM);

    EvStreamFeatureRequest();
    VerifyState(xmsm::OPENCONFIRM);

    EvTlsProceed();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsHandShakeFailure();
    VerifyState(xmsm::ACTIVE);
}


// OldState : OpenConfirm(OPENCONFIRM_FEATURE_SUCESS)
// Event    : EvXmppOpen
// NewState : Established
TEST_F(XmppStateMachineTest, OpenConfirm_EvXmppOpen) {

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
    VerifyState(xmsm::OPENCONFIRM);

    EvStreamFeatureRequest();
    VerifyState(xmsm::OPENCONFIRM);

    EvTlsProceed();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsHandShakeSuccess();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);

    EvXmppOpen();
    VerifyState(xmsm::ESTABLISHED);
}

// OldState : Established
// Event    : EvXmppKeepalive
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
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStreamFeatureRequest();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsProceed();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsHandShakeSuccess();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);

    EvXmppOpen();
    VerifyState(xmsm::ESTABLISHED);

    EvXmppKeepalive();
    VerifyState(xmsm::ESTABLISHED);
}


// OldState : Established
// Event    : EvXmppMessageStanza
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
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStreamFeatureRequest();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsProceed();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsHandShakeSuccess();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);

    EvXmppOpen();
    VerifyState(xmsm::ESTABLISHED);

    EvXmppMessageStanza();
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
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStreamFeatureRequest();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsProceed();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsHandShakeSuccess();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);

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
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_INIT);

    EvStreamFeatureRequest();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsProceed();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_NEGOTIATION);

    EvTlsHandShakeSuccess();
    VerifyState(xmsm::OPENCONFIRM);
    VerifyOpenConfirmState(xmsm::OPENCONFIRM_FEATURE_SUCCESS);

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
