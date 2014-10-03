/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/state_machine.h"

#include <boost/assign.hpp>
#include <boost/asio.hpp>
#include <boost/foreach.hpp>

#include "base/logging.h"
#include "base/task_annotations.h"
#include "base/test/task_test_util.h"
#include "net/bgp_af.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_config_parser.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_proto.h"
#include "control-node/control_node.h"
#include "io/event_manager.h"
#include "testing/gunit.h"

#include "bgp_message_test.h"
#include "bgp_server_test_util.h"

using namespace std;
using namespace boost::assign;
using namespace boost::posix_time;

class BgpSessionMock : public BgpSession {
public:
    enum State {
        CONNECT,
        ESTABLISHED,
        CLOSE,
    };

    enum Direction {
        ACTIVE,
        PASSIVE,
    };

    BgpSessionMock(BgpSessionManager *session, State state, Direction direction)
        : BgpSession(session, NULL), state_(state), direction_(direction) {
    }

    ~BgpSessionMock() {
        BGP_DEBUG_UT("Session delete");
    }

    virtual bool Send(const u_int8_t *data, size_t size, size_t *sent) {
        return true;
    }

    virtual bool Connected(Endpoint remote) {
        state_ = BgpSessionMock::ESTABLISHED;
        EventObserver obs = observer();
        if (obs != NULL) {
            obs(this, CONNECT_COMPLETE);
        }
        return true;
    }

    void Close() {
        state_ = BgpSessionMock::CLOSE;
        BgpSession::Close();
    }

    State state() { return state_; }
    Direction direction() { return direction_; }

private:
    State state_;
    Direction direction_;
};

class BgpSessionManagerMock : public BgpSessionManager {
public:
    BgpSessionManagerMock(EventManager *evm, BgpServer *server)
        : BgpSessionManager(evm, server), active_session_(NULL),
          passive_session_(NULL), duplicate_session_(NULL),
          create_session_fail_(false) {
    }

    const bool create_session_fail() const { return create_session_fail_; }
    void set_create_session_fail(bool flag) { create_session_fail_ = flag; }

    virtual TcpSession *CreateSession() {
        assert(!active_session_);

        // Simulate CreateSession() API failure, such as when the process runs
        // out of file descriptors.
        if (create_session_fail_) return NULL;

        active_session_ =
            new BgpSessionMock(this, BgpSessionMock::CONNECT,
                BgpSessionMock::ACTIVE);
        return active_session_;
    }

    BgpSessionMock *CreatePassiveSession() {
        assert(!passive_session_);
        passive_session_ =
            new BgpSessionMock(this, BgpSessionMock::ESTABLISHED,
                BgpSessionMock::PASSIVE);
        return passive_session_;
    }

    BgpSessionMock *CreateDuplicateSession() {
        assert(passive_session_);
        assert(!duplicate_session_);
        duplicate_session_ =
            new BgpSessionMock(this, BgpSessionMock::ESTABLISHED,
                BgpSessionMock::PASSIVE);
        return duplicate_session_;
    }

    virtual void Connect(TcpSession *session, Endpoint remote) {
    }

    virtual void DeleteSession(TcpSession *session) {
        if (active_session_ == session) {
            active_session_ = NULL;
        } else if (passive_session_ == session) {
            passive_session_ = NULL;
        } else if (duplicate_session_ == session) {
            duplicate_session_ = NULL;
        } else {
            assert(false);
        }
        delete static_cast<BgpSessionMock *>(session);
    }

    BgpSessionMock *active_session() { return active_session_; }
    BgpSessionMock *passive_session() { return passive_session_; }
    BgpSessionMock *duplicate_session() { return duplicate_session_; }

private:
    friend class StateMachineUnitTest;
    BgpSessionMock *active_session_, *passive_session_, *duplicate_session_;
    bool create_session_fail_;
};

class StateMachineUnitTest : public ::testing::Test {
protected:
    StateMachineUnitTest() : server_(&evm_, "A"),
        timer_(TimerManager::CreateTimer(*evm_.io_service(), "Dummy timer")) {
        session_mgr_ =
          static_cast<BgpSessionManagerMock *>(server_.session_manager());
        server_.Configure(GetConfig());
        task_util::WaitForIdle();

        string uuid = BgpConfigParser::session_uuid("A", "B", 1);
        TASK_UTIL_EXPECT_NE(server_.FindPeerByUuid(
                                BgpConfigManager::kMasterInstance, uuid),
                            (BgpPeer *) NULL);
        peer_ = server_.FindPeerByUuid(BgpConfigManager::kMasterInstance, uuid);
        sm_ = peer_->state_machine();
        sm_->set_idle_hold_time(1);

        boost::system::error_code ec;
        lower_id_ = Ip4Address::from_string("191.168.0.2", ec).to_ulong();
        higher_id_ = Ip4Address::from_string("193.168.0.0", ec).to_ulong();
    }

    ~StateMachineUnitTest() {
        task_util::WaitForIdle();
        server_.Shutdown();
        task_util::WaitForIdle();
        TimerManager::DeleteTimer(timer_);
        evm_.Shutdown();
    }

    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

    string GetConfig() {
        ostringstream out;
        out <<
        "<config>\
            <bgp-router name=\'A\'>\
                <identifier>192.168.0.1</identifier>\
                <address>127.0.0.1</address>\
                <autonomous-system>10458</autonomous-system>\
                <session to='B'/>\
            </bgp-router>\
            <bgp-router name=\'B\'>\
                <address>127.0.0.1</address>\
                <autonomous-system>10458</autonomous-system>\
            </bgp-router>\
        </config>";
        return out.str();
    }

    bool DummyTimerHandler() {
        BGP_DEBUG_UT("Dummy handler called");
        return false;
    }

    void RunToState(StateMachine::State state) {
        timer_->Start(15000,
            boost::bind(&StateMachineUnitTest::DummyTimerHandler, this));
        task_util::WaitForIdle();
        for (int count = 0; count < 100 && sm_->get_state() != state; count++) {
            evm_.RunOnce();
            task_util::WaitForIdle();
        }
        timer_->Cancel();
        VerifyState(state);
    }

    void GetToState(StateMachine::State state) {
        switch (state) {
        case StateMachine::IDLE: {
            EvAdminDown();
            VerifyState(state);
            break;
        }
        case StateMachine::ACTIVE: {
            GetToState(StateMachine::IDLE);
            EvAdminUp();
            task_util::WaitForIdle();
            EvIdleHoldTimerExpired();
            VerifyState(state);
            break;
        }
        case StateMachine::CONNECT: {
            GetToState(StateMachine::ACTIVE);
            EvConnectTimerExpired();
            VerifyState(state);
            break;
        }
        case StateMachine::OPENSENT: {
            GetToState(StateMachine::CONNECT);
            TcpSession::Endpoint endpoint;
            session_mgr_->active_session()->Connected(endpoint);
            VerifyState(state);
            break;
        }
        case StateMachine::OPENCONFIRM: {
            GetToState(StateMachine::OPENSENT);
            BgpProto::OpenMessage *open = new BgpProto::OpenMessage;
            BgpMessageTest::GenerateOpenMessage(open);
            peer_->ResetCapabilities();
            sm_->OnMessage(session_mgr_->active_session(), open);
            VerifyState(StateMachine::OPENCONFIRM);
            break;
        }
        case StateMachine::ESTABLISHED: {
            GetToState(StateMachine::OPENCONFIRM);
            BgpProto::Keepalive *keepalive = new BgpProto::Keepalive;
            sm_->OnMessage(session_mgr_->active_session(), keepalive);
            VerifyState(StateMachine::ESTABLISHED);
            break;
        }
        default: {
            ASSERT_TRUE(false);
            break;
        }
        }
    }

    void VerifyState(StateMachine::State state) {
        task_util::WaitForIdle();
        TaskScheduler::GetInstance()->Stop();

        BGP_DEBUG_UT("VerifyState " << state);
        TASK_UTIL_EXPECT_EQ(state, sm_->get_state());

        switch (state) {
        case StateMachine::IDLE:
            TASK_UTIL_EXPECT_TRUE(!ConnectTimerRunning());
            TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
            TASK_UTIL_EXPECT_TRUE(!HoldTimerRunning());
            if (peer_->IsAdminDown()) {
                TASK_UTIL_EXPECT_TRUE(!IdleHoldTimerRunning());
            } else {
                TASK_UTIL_EXPECT_TRUE(IdleHoldTimerRunning());
            }
            TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
            TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
            TASK_UTIL_EXPECT_TRUE(!peer_->KeepaliveTimerRunning());
            TASK_UTIL_EXPECT_TRUE(peer_->session() == NULL);
            break;

        case StateMachine::ACTIVE:
            TASK_UTIL_EXPECT_TRUE(ConnectTimerRunning() != OpenTimerRunning());
            TASK_UTIL_EXPECT_TRUE(!HoldTimerRunning());
            TASK_UTIL_EXPECT_TRUE(!IdleHoldTimerRunning());
            TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
            TASK_UTIL_EXPECT_TRUE((sm_->passive_session() != NULL) ==
                                  OpenTimerRunning());
            TASK_UTIL_EXPECT_TRUE(!peer_->KeepaliveTimerRunning());
            TASK_UTIL_EXPECT_TRUE(peer_->session() == NULL);
            break;

        case StateMachine::CONNECT:
            TASK_UTIL_EXPECT_TRUE(ConnectTimerRunning());
            TASK_UTIL_EXPECT_TRUE(!HoldTimerRunning());
            TASK_UTIL_EXPECT_TRUE(!IdleHoldTimerRunning());
            if (!session_mgr_->create_session_fail()) {
                TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
            } else {
                TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
            }
            TASK_UTIL_EXPECT_TRUE((sm_->passive_session() != NULL) ==
                                  OpenTimerRunning());
            TASK_UTIL_EXPECT_TRUE(!peer_->KeepaliveTimerRunning());
            TASK_UTIL_EXPECT_TRUE(peer_->session() == NULL);
            break;

        case StateMachine::OPENSENT:
            TASK_UTIL_EXPECT_TRUE(!ConnectTimerRunning());
            TASK_UTIL_EXPECT_TRUE(HoldTimerRunning());
            TASK_UTIL_EXPECT_TRUE(!IdleHoldTimerRunning());
            TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL ||
                                  sm_->passive_session() != NULL);
            TASK_UTIL_EXPECT_TRUE((sm_->passive_session() != NULL) ||
                                  !OpenTimerRunning());
            TASK_UTIL_EXPECT_TRUE(!peer_->KeepaliveTimerRunning());
            TASK_UTIL_EXPECT_TRUE(peer_->session() == NULL);
            break;

        case StateMachine::OPENCONFIRM:
            TASK_UTIL_EXPECT_TRUE(!ConnectTimerRunning());
            TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
            TASK_UTIL_EXPECT_TRUE(HoldTimerRunning());
            TASK_UTIL_EXPECT_TRUE(!IdleHoldTimerRunning());
            TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
            TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
            TASK_UTIL_EXPECT_TRUE(peer_->KeepaliveTimerRunning());
            TASK_UTIL_EXPECT_TRUE(peer_->session() != NULL);
            break;

        case StateMachine::ESTABLISHED:
            TASK_UTIL_EXPECT_TRUE(!ConnectTimerRunning());
            TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
            TASK_UTIL_EXPECT_TRUE(HoldTimerRunning());
            TASK_UTIL_EXPECT_TRUE(!IdleHoldTimerRunning());
            TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
            TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
            TASK_UTIL_EXPECT_TRUE(peer_->KeepaliveTimerRunning());
            TASK_UTIL_EXPECT_TRUE(peer_->session() != NULL);
            break;

        default:
            ASSERT_TRUE(false);
            break;
        }

        TaskScheduler::GetInstance()->Start();
    }

    void VerifyDirection(BgpSessionMock::Direction direction) {
        BgpSession *session = sm_->peer()->session();
        BgpSessionMock *mock_session = dynamic_cast<BgpSessionMock *>(session);
        ASSERT_TRUE(mock_session != NULL);
        TASK_UTIL_EXPECT_TRUE(mock_session->direction() == direction);
    }

    BgpSessionMock *GetSession(BgpSessionMock *session) {
        if (session) return session;
        session = session_mgr_->active_session();
        if (!session || session->state() != BgpSessionMock::ESTABLISHED) {
            if (!session_mgr_->passive_session())
                EvTcpPassiveOpen();
            session = session_mgr_->passive_session();
        }
        return session;
    }

    void EvStart() {
        sm_->reset_idle_hold_time();
        sm_->Initialize();
    }
    void EvStop() {
        sm_->Shutdown(BgpProto::Notification::AdminShutdown);
    }
    void EvAdminUp() {
        peer_->SetAdminState(false);
        sm_->set_idle_hold_time(1);
    }
    void EvAdminDown() {
        peer_->SetAdminState(true);
        sm_->set_idle_hold_time(1);
    }
    void EvConnectTimerExpired() {
        sm_->FireConnectTimer();
    }
    void EvIdleHoldTimerExpired() {
        sm_->FireIdleHoldTimer();
    }
    void EvOpenTimerExpired() {
        sm_->FireOpenTimer();
    }
    void EvTcpPassiveOpen() {
        ConcurrencyScope scope("bgp::Config");
        BgpSessionMock *session = session_mgr_->CreatePassiveSession();
        TcpSession::Endpoint endpoint;
        peer_->AcceptSession(session);
    }
    void EvTcpDuplicatePassiveOpen() {
        ConcurrencyScope scope("bgp::Config");
        BgpSessionMock *session = session_mgr_->CreateDuplicateSession();
        TcpSession::Endpoint endpoint;
        peer_->AcceptSession(session);
    }
    void EvOpenTimerExpiredPassive() {
        EvTcpPassiveOpen();
        task_util::WaitForIdle();
        sm_->FireOpenTimer();
        task_util::WaitForIdle();
    }
    void EvHoldTimerExpired() {
        sm_->FireHoldTimer();
    }
    void EvTcpConnected() {
        sm_->OnSessionEvent(session_mgr_->active_session(),
            TcpSession::CONNECT_COMPLETE);
    }
    void EvTcpConnectFail() {
        sm_->OnSessionEvent(session_mgr_->active_session(),
            TcpSession::CONNECT_FAILED);
    }
    void EvTcpClose(BgpSessionMock *session = NULL) {
        if (!session) session = session_mgr_->active_session();
        sm_->OnSessionEvent(session, TcpSession::CLOSE);
    }
    void EvBgpHeaderError(BgpSessionMock *session = NULL) {
        session = GetSession(session);
        ParseErrorContext context;
        context.error_code = BgpProto::Notification::MsgHdrErr;
        sm_->OnMessageError(session, &context);
    }
    void EvBgpOpen(BgpSessionMock *session = NULL) {
        session = GetSession(session);
        BgpProto::OpenMessage *open = new BgpProto::OpenMessage;
        BgpMessageTest::GenerateOpenMessage(open);
        peer_->ResetCapabilities();
        sm_->OnMessage(session, open);
    }
    void EvBgpOpenCustom(BgpSessionMock *session,
        uint32_t identifier = 1, int holdtime = 30) {
        BgpProto::OpenMessage *open = new BgpProto::OpenMessage;
        BgpMessageTest::GenerateOpenMessage(open);
        open->identifier = identifier;
        open->holdtime = holdtime;
        peer_->ResetCapabilities();
        sm_->OnMessage(session, open);
    }
    void EvBgpOpenError(BgpSessionMock *session = NULL) {
        session = GetSession(session);
        ParseErrorContext context;
        context.error_code = BgpProto::Notification::OpenMsgErr;
        sm_->OnMessageError(session, &context);
    }
    void EvBgpKeepalive(BgpSessionMock *session = NULL) {
        session = GetSession(session);
        BgpProto::Keepalive *keepalive = new BgpProto::Keepalive;
        sm_->OnMessage(session, keepalive);
    }
    void EvBgpNotification(BgpSessionMock *session = NULL) {
        session = GetSession(session);
        BgpProto::Notification *notification = new BgpProto::Notification;
        sm_->OnMessage(session, notification);
    }
    void EvBgpUpdate(BgpSessionMock *session = NULL) {
        session = GetSession(session);
        BgpProto::Update *update = new BgpProto::Update;
        BgpMessageTest::GenerateEmptyUpdateMessage(update);
        sm_->OnMessage(session, update);
    }
    void EvBgpUpdateError(BgpSessionMock *session = NULL) {
        session = GetSession(session);
        ParseErrorContext context;
        context.error_code = BgpProto::Notification::UpdateMsgErr;
        sm_->OnMessageError(session, &context);
    }

    bool ConnectTimerRunning() { return sm_->connect_timer_->running(); }
    bool OpenTimerRunning() { return sm_->open_timer_->running(); }
    bool HoldTimerRunning() { return sm_->hold_timer_->running(); }
    bool IdleHoldTimerRunning() { return sm_->idle_hold_timer_->running(); }

    EventManager evm_;
    BgpServerTest server_;
    BgpSessionManagerMock *session_mgr_;
    BgpPeer *peer_;
    StateMachine *sm_;
    Timer *timer_;
    uint32_t lower_id_, higher_id_;
};

typedef boost::function<void(void)> EvGen;
struct EvGenComp {
    bool operator()(const EvGen &lhs, const EvGen &rhs) const {
        return &lhs < &rhs;
    }
};

TEST_F(StateMachineUnitTest, Matrix) {
    boost::asio::io_service::work work(*evm_.io_service());
    typedef map<EvGen, StateMachine::State, EvGenComp> Transitions;

#define TRANSITION(F, E) \
    ((EvGen) boost::bind(&StateMachineUnitTest_Matrix_Test::F, this), E)
#define TRANSITION2(F, E) \
    ((EvGen) boost::bind(&StateMachineUnitTest_Matrix_Test::F, this, \
            (BgpSessionMock *) NULL), E)

    Transitions idle = map_list_of
            TRANSITION(EvTcpPassiveOpen, StateMachine::IDLE)
            TRANSITION(EvAdminUp, StateMachine::ACTIVE);

    Transitions active = map_list_of
            TRANSITION(EvStop, StateMachine::IDLE)
            TRANSITION(EvAdminDown, StateMachine::IDLE)
            TRANSITION(EvHoldTimerExpired, StateMachine::ACTIVE)
            TRANSITION2(EvBgpNotification, StateMachine::IDLE)
            TRANSITION2(EvBgpKeepalive, StateMachine::IDLE)
            TRANSITION2(EvBgpUpdate, StateMachine::IDLE)
            TRANSITION2(EvBgpOpenError, StateMachine::IDLE)
            TRANSITION2(EvBgpHeaderError, StateMachine::IDLE)
            TRANSITION2(EvBgpUpdateError, StateMachine::IDLE)
            TRANSITION(EvConnectTimerExpired, StateMachine::CONNECT)
            TRANSITION(EvOpenTimerExpired, StateMachine::ACTIVE)
            TRANSITION(EvOpenTimerExpiredPassive, StateMachine::OPENSENT)
            TRANSITION(EvTcpPassiveOpen, StateMachine::ACTIVE)
            TRANSITION2(EvTcpClose, StateMachine::ACTIVE)
            TRANSITION2(EvBgpOpen, StateMachine::OPENCONFIRM);

    Transitions connect = map_list_of
            TRANSITION(EvStop, StateMachine::IDLE)
            TRANSITION(EvAdminDown, StateMachine::IDLE)
            TRANSITION(EvConnectTimerExpired, StateMachine::ACTIVE)
            TRANSITION(EvOpenTimerExpiredPassive, StateMachine::OPENSENT)
            TRANSITION(EvTcpConnected, StateMachine::OPENSENT)
            TRANSITION(EvTcpConnectFail, StateMachine::ACTIVE)
            TRANSITION(EvTcpPassiveOpen, StateMachine::CONNECT)
            TRANSITION2(EvTcpClose, StateMachine::ACTIVE)
            TRANSITION2(EvBgpOpen, StateMachine::OPENCONFIRM)
            TRANSITION2(EvBgpHeaderError, StateMachine::IDLE)
            TRANSITION2(EvBgpOpenError, StateMachine::IDLE)
            TRANSITION2(EvBgpUpdateError, StateMachine::IDLE)
            TRANSITION2(EvBgpNotification, StateMachine::IDLE)
            TRANSITION(EvHoldTimerExpired, StateMachine::CONNECT)
            TRANSITION2(EvBgpKeepalive, StateMachine::IDLE)
            TRANSITION2(EvBgpUpdate, StateMachine::IDLE);

    Transitions opensent = map_list_of
            TRANSITION(EvStop, StateMachine::IDLE)
            TRANSITION(EvAdminDown, StateMachine::IDLE)
            TRANSITION(EvTcpPassiveOpen, StateMachine::OPENSENT)
            TRANSITION2(EvTcpClose, StateMachine::ACTIVE)
            TRANSITION2(EvBgpOpen, StateMachine::OPENCONFIRM)
            TRANSITION2(EvBgpHeaderError, StateMachine::IDLE)
            TRANSITION2(EvBgpOpenError, StateMachine::IDLE)
            TRANSITION2(EvBgpUpdateError, StateMachine::IDLE)
            TRANSITION(EvHoldTimerExpired, StateMachine::IDLE)
            TRANSITION2(EvBgpKeepalive, StateMachine::IDLE)
            TRANSITION2(EvBgpNotification, StateMachine::IDLE)
            TRANSITION(EvConnectTimerExpired, StateMachine::OPENSENT)
            TRANSITION(EvOpenTimerExpiredPassive, StateMachine::OPENSENT)
            TRANSITION2(EvBgpUpdate, StateMachine::IDLE);

    Transitions openconfirm = map_list_of
            TRANSITION(EvStop, StateMachine::IDLE)
            TRANSITION(EvAdminDown, StateMachine::IDLE)
            TRANSITION(EvTcpPassiveOpen, StateMachine::OPENCONFIRM)
            TRANSITION2(EvBgpOpen, StateMachine::IDLE)
            TRANSITION(EvHoldTimerExpired, StateMachine::IDLE)
            TRANSITION2(EvBgpKeepalive, StateMachine::ESTABLISHED)
            TRANSITION2(EvTcpClose, StateMachine::IDLE)
            TRANSITION2(EvBgpHeaderError, StateMachine::IDLE)
            TRANSITION2(EvBgpOpenError, StateMachine::IDLE)
            TRANSITION2(EvBgpUpdateError, StateMachine::IDLE)
            TRANSITION2(EvBgpNotification, StateMachine::IDLE)
            TRANSITION(EvConnectTimerExpired, StateMachine::OPENCONFIRM)
            TRANSITION(EvOpenTimerExpiredPassive, StateMachine::OPENCONFIRM)
            TRANSITION2(EvBgpUpdate, StateMachine::IDLE);

    Transitions established = map_list_of
            TRANSITION(EvStop, StateMachine::IDLE)
            TRANSITION(EvAdminDown, StateMachine::IDLE)
            TRANSITION2(EvBgpOpen, StateMachine::IDLE)
            TRANSITION(EvHoldTimerExpired, StateMachine::IDLE)
            TRANSITION2(EvTcpClose, StateMachine::IDLE)
            TRANSITION2(EvBgpKeepalive, StateMachine::ESTABLISHED)
            TRANSITION2(EvBgpHeaderError, StateMachine::IDLE)
            TRANSITION2(EvBgpOpenError, StateMachine::IDLE)
            TRANSITION2(EvBgpUpdateError, StateMachine::IDLE)
            TRANSITION2(EvBgpNotification, StateMachine::IDLE)
            TRANSITION(EvConnectTimerExpired, StateMachine::ESTABLISHED)
            TRANSITION(EvOpenTimerExpired, StateMachine::ESTABLISHED)
            TRANSITION(EvTcpPassiveOpen, StateMachine::ESTABLISHED)
            TRANSITION2(EvBgpUpdate, StateMachine::ESTABLISHED);

    Transitions matrix[] =
        { idle, active, connect, opensent, openconfirm, established };

    for (int k = StateMachine::IDLE; k <= StateMachine::ESTABLISHED; k++) {
        StateMachine::State i = static_cast<StateMachine::State> (k);
        int count = 0;
        for (Transitions::iterator j = matrix[i].begin();
                j != matrix[i].end(); j++) {
            BGP_DEBUG_UT("Starting test " << count++ << " in state " << i
                    << " expecting " << j->second << " *********************");
            GetToState(i);
            j->first();
            RunToState(j->second);

            // If the final state is IDLE, and the peer is not AdminDown,
            // we expect to go to ACTIVE state after the idle hold timer
            // expires.
            if (j->second == StateMachine::IDLE) {
                if (sm_->peer()->IsAdminDown()) {
                    TASK_UTIL_EXPECT_TRUE(!IdleHoldTimerRunning());
                } else {
                    RunToState(StateMachine::ACTIVE);
                }
            }
            sm_->set_idle_hold_time(1);
        }
    }
}

//
// Simplest sequence of events to go to Established.
//
TEST_F(StateMachineUnitTest, Basic) {
    GetToState(StateMachine::ACTIVE);
    EvConnectTimerExpired();
    VerifyState(StateMachine::CONNECT);
    BgpSessionMock *session = session_mgr_->active_session();
    TcpSession::Endpoint endpoint;
    session->Connected(endpoint);
    VerifyState(StateMachine::OPENSENT);
    EvBgpOpen();
    VerifyState(StateMachine::OPENCONFIRM);
    EvBgpKeepalive();
    VerifyState(StateMachine::ESTABLISHED);
}

//
// Simulate EvTcpConnectFail and EvTcpClose.
//
TEST_F(StateMachineUnitTest, ConnectionErrors) {
    boost::asio::io_service::work work(*evm_.io_service());
    GetToState(StateMachine::CONNECT);
    EvTcpConnectFail();
    VerifyState(StateMachine::ACTIVE);
    EvConnectTimerExpired();
    VerifyState(StateMachine::CONNECT);
    BgpSessionMock *session = session_mgr_->active_session();
    TcpSession::Endpoint endpoint;
    session->Connected(endpoint);
    VerifyState(StateMachine::OPENSENT);
    EvTcpClose();
    VerifyState(StateMachine::ACTIVE);
}

//
// Simulate CreateSession failure and verify that we cycle through the
// Connect and Active states till CreateSession eventually succeeds.
//
TEST_F(StateMachineUnitTest, CreateSessionFail) {

    // Mark CreateSession() to fail.
    session_mgr_->set_create_session_fail(true);
    GetToState(StateMachine::ACTIVE);

    // Feed a few EvConnectTimerExpired and go through Active/Connect.
    for (int i = 0; i < 4; i++) {
        EvConnectTimerExpired();
        VerifyState(StateMachine::CONNECT);
        EvConnectTimerExpired();
        VerifyState(StateMachine::ACTIVE);
    }

    // Restore CreateSession() to succeed.
    session_mgr_->set_create_session_fail(false);

    // Make sure we can progress beyond Connect.
    EvConnectTimerExpired();
    VerifyState(StateMachine::CONNECT);
    BgpSessionMock *session = session_mgr_->active_session();
    TcpSession::Endpoint endpoint;
    session->Connected(endpoint);
    VerifyState(StateMachine::OPENSENT);
}

//
// First get to Active state and then inject EvConnectTimerExpired and
// EvTcpConnectFail multiple times to cycle through Connect and Active
// states.
//
// Verify that the connect time backs off as expected.
//
TEST_F(StateMachineUnitTest, ConnectTimerBackoff) {
    GetToState(StateMachine::ACTIVE);
    for (int idx = 0; idx < 10; idx++) {
        int connect_time = sm_->GetConnectTime();
        if (idx == 0) {
            TASK_UTIL_EXPECT_EQ(0, connect_time);
        } else if (idx <= 5) {
            TASK_UTIL_EXPECT_EQ(1 << (idx -1), connect_time);
        } else {
            TASK_UTIL_EXPECT_EQ(StateMachine::kConnectInterval, connect_time);
        }
        EvConnectTimerExpired();
        VerifyState(StateMachine::CONNECT);
        EvTcpConnectFail();
        VerifyState(StateMachine::ACTIVE);
    }
    EvConnectTimerExpired();
    VerifyState(StateMachine::CONNECT);
    BgpSessionMock *session = session_mgr_->active_session();
    TcpSession::Endpoint endpoint;
    session->Connected(endpoint);
    VerifyState(StateMachine::OPENSENT);
}

class StateMachineIdleTest : public StateMachineUnitTest {
protected:
    virtual void SetUp() {
        GetToState(StateMachine::IDLE);
    }

    virtual void TearDown() {
    }
};

// Old State: Idle
// Event:     EvTcpPassiveOpen
// New State: Idle
TEST_F(StateMachineIdleTest, TcpPassiveOpen) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::IDLE);
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);

    // Also verify that we can proceed after this event
    GetToState(StateMachine::ACTIVE);
}

// Old State: Idle
// Event:     EvAdminUp + EvAdminDown + EvIdleHoldTimerExpired
// New State: Idle
// Intent:    IdleHoldTimer expires right after an AdminDown is enqueued.
//            We shouldn't proceed beyond the Idle state if this happens.
// Other:     Need to make sure that IdleHoldTimer is running when we call
//            EvIdleHoldTimerExpired, otherwise no event will be enqueued.
TEST_F(StateMachineIdleTest, AdminUpThenAdminDownThenIdleHoldTimerExpired) {
    TaskScheduler::GetInstance()->Stop();
    EvAdminUp();
    sm_->set_idle_hold_time(StateMachine::kMaxIdleHoldTime);
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::IDLE);
    TaskScheduler::GetInstance()->Stop();
    EvAdminDown();
    EvIdleHoldTimerExpired();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::IDLE);
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: Idle
// Event:     EvTcpPassiveOpen + EvBgpOpen
// New State: Idle
TEST_F(StateMachineIdleTest, TcpPassiveOpenThenBgpOpen) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    BgpSessionMock *session = session_mgr_->passive_session();
    EvBgpOpenCustom(session, lower_id_);
    TaskScheduler::GetInstance()->Start();

    VerifyState(StateMachine::IDLE);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->active_session() == NULL);

    // Also verify that we can proceed after this event
    GetToState(StateMachine::ACTIVE);
}

// Old State: Idle
// Event:     EvTcpPassiveOpen + EvStart + EvBgpOpen
// New State: Active
// Messages:  None
// Other:     Open message is ignored since there's no passive session
//            when we process it. The passive session gets closed since
//            we are in IDLE state when we process it.
TEST_F(StateMachineIdleTest, TcpPassiveOpenThenStartThenBgpOpen) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvStart();
    BgpSessionMock *session = session_mgr_->passive_session();
    EvBgpOpenCustom(session, lower_id_);
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
}

// Old State: Idle
// Event:     EvTcpPassiveOpen + EvStart + EvConnectTimerExpired + EvBgpOpen
// New State: Connect
// Messages:  None
// Other:     Open message is ignored since there's no passive session
//            when we process it. The passive session gets closed since
//            we are in IDLE state when we process it.
TEST_F(StateMachineIdleTest, TcpPassiveOpenThenConnectTimerExpiredThenBgpOpen) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvStart();
    EvConnectTimerExpired();
    BgpSessionMock *session = session_mgr_->passive_session();
    EvBgpOpenCustom(session, lower_id_);
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::CONNECT);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
}

class StateMachineActiveTest : public StateMachineUnitTest {
protected:
    virtual void SetUp() {
        GetToState(StateMachine::ACTIVE);
    }

    virtual void TearDown() {
    }
};

// Old State: Active
// Event:     EvTcpPassiveOpen.
// New State: Active
// Timers:    Open timer should be running since we have a passive session.
//            Connect timer must have been cancelled on seeing passive session.
// Messages:  None.
TEST_F(StateMachineActiveTest, TcpPassiveOpen) {
    EvTcpPassiveOpen();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(!ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: Active
// Event:     EvTcpPassiveOpen + EvTcpPassiveOpen.
// New State: Active
// Timers:    Open timer should be running since we have a passive session.
//            Connect timer must have been cancelled on seeing passive session.
// Messages:  None.
// Other:     Original passive session should get closed and deleted, while
//            the duplicate passive session becomes the new passive session.
TEST_F(StateMachineActiveTest, DuplicateTcpPassiveOpen) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvTcpDuplicatePassiveOpen();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(!ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->duplicate_session() != NULL);
}

// Old State: Active
// Event:     EvTcpPassiveOpen + EvTcpPassiveOpen + BgpOpen
// New State: Active
// Timers:    Open timer should be running since we have a passive session.
//            Connect timer must have been cancelled on seeing passive session.
// Messages:  None.
// Intent:    BgpOpen should be ignored since it's received on stale session.
// Other:     Original passive session should get closed and deleted, while
//            the duplicate passive session becomes the new passive session.
TEST_F(StateMachineActiveTest, DuplicateTcpPassiveOpenThenStaleBgpOpen) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvTcpDuplicatePassiveOpen();
    EvBgpOpen(session_mgr_->passive_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(!ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->duplicate_session() != NULL);
}

// Old State: Active
// Event:     EvTcpClose on the passive session.
// New State: Active
// Timers:    Open timer should not be running since passive session got closed.
//            Connect timer should have been started again.
// Messages:  None.
TEST_F(StateMachineActiveTest, TcpPassiveClose) {
    EvTcpPassiveOpen();
    EvTcpClose(session_mgr_->passive_session());
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: Active
// Event:     EvOpenTimerExpired
// New State: OpenSent
// Timers:    Open timer must not be running since it just expired.
// Messages:  Open message sent on passive session.
TEST_F(StateMachineActiveTest, OpenTimerExpiredPassive) {
    EvOpenTimerExpiredPassive();
    RunToState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: Active
// Event:     EvTcpPassiveOpen + EvConnectTimerExpired
// New State: Active
// Timers:    Open timer should be running since we have a passive session.
//            Connect timer must not be running since it just expired.
// Messages:  None.
//
// Note:      The Connect timer has expired before it can be cancelled while
//            processing EvTcpPassiveOpen.
TEST_F(StateMachineActiveTest, TcpPassiveOpenThenConnectTimerExpired) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvConnectTimerExpired();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(!ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Parameterize id (higher/lower)

class StateMachineActiveParamTest1 :
    public StateMachineActiveTest,
    public ::testing::WithParamInterface<bool> {
protected:
    virtual void SetUp() {
        id_ = GetParam() ? lower_id_ : higher_id_;
        StateMachineActiveTest::SetUp();
    }

    virtual void TearDown() {
        StateMachineActiveTest::TearDown();
    }

    int id_;
};

// Old State: Active
// Event:     EvTcpPassiveOpen + EvBgpOpen (from higher or lower id)
// New State: OpenConfirm
// Messages:  Open message sent on passive session.
TEST_P(StateMachineActiveParamTest1, TcpPassiveOpenThenBgpOpen) {
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), id_);
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::PASSIVE);
}

INSTANTIATE_TEST_CASE_P(One, StateMachineActiveParamTest1, ::testing::Bool());

// Parameterize id (higher/lower) and holdtime (higher/lower)

typedef std::tr1::tuple<bool, bool> ActiveTestParams2;

class StateMachineActiveParamTest2 :
    public StateMachineActiveTest,
    public ::testing::WithParamInterface<ActiveTestParams2> {
protected:
    virtual void SetUp() {
        id_ = tr1::get<0>(GetParam()) ? lower_id_ : higher_id_;
        if (tr1::get<1>(GetParam())) {
            holdtime_ = StateMachine::kHoldTime - 1;
        } else {
            holdtime_ = StateMachine::kHoldTime + 1;
        }
        StateMachineActiveTest::SetUp();
    }

    virtual void TearDown() {
        StateMachineActiveTest::TearDown();
    }

    uint32_t id_;
    int holdtime_;
};

// Old State: Active
// Event:     EvTcpPassiveOpen + EvBgpOpen (with higher or lower holdtime)
// New State: OpenConfirm
// Messages:  Open message sent on passive session.
TEST_P(StateMachineActiveParamTest2, BgpHoldtimeNegotiation) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), id_, holdtime_);
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::PASSIVE);
    TASK_UTIL_EXPECT_EQ(min(holdtime_, StateMachine::kHoldTime),
        sm_->hold_time());
}

INSTANTIATE_TEST_CASE_P(One, StateMachineActiveParamTest2,
    ::testing::Combine(::testing::Bool(), ::testing::Bool()));

class StateMachineConnectTest : public StateMachineUnitTest {
protected:
    virtual void SetUp() {
        GetToState(StateMachine::CONNECT);
    }

    virtual void TearDown() {
    }
};

// Old State: Connect
// Event:     EvTcpConnected
// New State: OpenSent
// Timers:    Open timer must not be running since there's no passive session.
// Messages:  Open message sent on active session.
TEST_F(StateMachineConnectTest, TcpConnected) {
    EvTcpConnected();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: Connect
// Event:     EvTcpPassiveOpen
// New State: Connect
// Timers:    Open timer must not running since there's a passive session.
// Messages:  None.
TEST_F(StateMachineConnectTest, TcpPassiveOpen) {
    EvTcpPassiveOpen();
    VerifyState(StateMachine::CONNECT);
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: Connect
// Event:     EvTcpPassiveOpen + EvTcpPassiveOpen.
// New State: Connect
// Timers:    Open timer should be running since we have a passive session.
// Messages:  None.
// Other:     Original passive session should get closed and deleted, while
//            the duplicate passive session becomes the new passive session.
TEST_F(StateMachineConnectTest, DuplicateTcpPassiveOpen) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvTcpDuplicatePassiveOpen();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::CONNECT);
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->duplicate_session() != NULL);
}

// Old State: Connect
// Event:     EvTcpPassiveOpen + EvTcpPassiveOpen + BgpOpen
// New State: Connect
// Timers:    Open timer should be running since we have a passive session.
// Messages:  None.
// Intent:    BgpOpen should be ignored since it's received on stale session.
// Other:     Original passive session should get closed and deleted, while
//            the duplicate passive session becomes the new passive session.
TEST_F(StateMachineConnectTest, DuplicateTcpPassiveOpenThenStaleBgpOpen) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvTcpDuplicatePassiveOpen();
    EvBgpOpen(session_mgr_->passive_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::CONNECT);
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->duplicate_session() != NULL);
}

// Old State: Connect
// Event:     EvConnectTimerExpired
// New State: Active
// Timers:    Connect timer must be running since we want an active session.
// Messages:  None.
TEST_F(StateMachineConnectTest, ConnectTimerExpired1) {
    EvConnectTimerExpired();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: Connect
// Event:     EvTcpPassiveOpen + EvConnectTimerExpired
// New State: Active
// Timers:    Open timer must be running since we have a passive session.
//            Connect timer must not be started since there's a passive session.
// Messages:  None.
//
// Note:      The Connect timer has expired before it can be cancelled while
//            processing EvTcpPassiveOpen.
TEST_F(StateMachineConnectTest, ConnectTimerExpired2) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvConnectTimerExpired();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(!ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: Connect
// Event:     EvTcpConnectFail
// New State: Active
// Timers:    Connect timer must be running since we want an active session.
// Messages:  None.
TEST_F(StateMachineConnectTest, TcpConnectFail) {
    EvTcpConnectFail();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: Connect
// Event:     EvTcpClose
// New State: Active
// Timers:    Connect timer must be running since we want an active session.
// Messages:  None.
TEST_F(StateMachineConnectTest, TcpClose) {
    EvTcpConnectFail();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: Connect
// Event:     EvTcpClose
// New State: Active
// Timers:    Connect timer must not be running since we have passive session.
// Messages:  None.
TEST_F(StateMachineConnectTest, TcpPassiveOpenThenActiveTcpClose) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvTcpClose();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(!ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: Connect
// Event:     EvTcpPassiveOpen + EvTcpConnectFail
// New State: Active
// Timers:    Open timer must be running since we have a passive session.
//            Connect timer must not be started since there's a passive session.
// Messages:  None.
TEST_F(StateMachineConnectTest, TcpPassiveOpenThenTcpConnectFail) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvTcpConnectFail();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(!ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: Connect
// Event:     EvTcpPassiveOpen + EvTcpClose
// New State: Connect
// Timers:    Open timer must not be running since there's no passive session.
//            Connect timer must be running since we want an active session.
// Messages:  None.
TEST_F(StateMachineConnectTest, TcpPassiveOpenThenTcpPassiveClose) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvTcpClose(session_mgr_->passive_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::CONNECT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: Connect
// Event:     EvTcpPassiveOpen + EvOpenTimerExpired
// New State: OpenSent
// Timers:    Open timer must not be running since it just expired.
// Messages:  Open message sent on passive session.
TEST_F(StateMachineConnectTest, TcpPassiveOpenThenOpenTimerExpired) {
    EvOpenTimerExpiredPassive();
    RunToState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: Connect
// Event:     EvTcpConnected + EvConnectTimerExpired
// New State: OpenSent
// Messages:  Open message sent on active session.
// Other:     The Connect timer has expired before it can be cancelled while
//            processing EvTcpConnected.
TEST_F(StateMachineConnectTest, TcpConnectedThenConnectTimerExpired) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpConnected();
    EvConnectTimerExpired();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
}

// Old State: Connect
// Event:     EvConnectTimerExpired + EvTcpConnected
// New State: Active
// Messages:  None.
// Other:     The Connect timer expired before we see EvTcpConnected, thus
//            the session would already have been deleted when we process
//            EvTcpConnected.
TEST_F(StateMachineConnectTest, ConnectTimerExpiredThenTcpConnected) {
    TaskScheduler::GetInstance()->Stop();
    EvConnectTimerExpired();
    EvTcpConnected();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->active_session() == NULL);
}

// Old State: Connect
// Event:     EvConnectTimerExpired + EvTcpConnectFail
// New State: Active
// Messages:  None.
// Other:     The Connect timer expired before we see EvTcpConnectFail, thus
//            the session would already have been deleted when we process
//            EvTcpConnectFail.
TEST_F(StateMachineConnectTest, ConnectTimerExpiredThenTcpConnectFail) {
    TaskScheduler::GetInstance()->Stop();
    EvConnectTimerExpired();
    EvTcpConnectFail();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->active_session() == NULL);
}

// Parameterize id (higher/lower)

class StateMachineConnectParamTest1 :
    public StateMachineConnectTest,
    public ::testing::WithParamInterface<bool> {
protected:
    virtual void SetUp() {
        id_ = GetParam() ? lower_id_ : higher_id_;
        StateMachineConnectTest::SetUp();
    }

    virtual void TearDown() {
        StateMachineConnectTest::TearDown();
    }

    int id_;
};

// Old State: Connect
// Event:     EvTcpPassiveOpen + EvBgpOpen (from higher or lower id)
// New State: OpenConfirm
// Messages:  Open message sent on passive session.
TEST_P(StateMachineConnectParamTest1, TcpPassiveOpenThenBgpOpen) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), id_);
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::PASSIVE);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->active_session() == NULL);
}

// Old State: Connect
// Event:     EvTcpPassiveOpen + EvBgpOpen (from higher or lower id) +
//            EvTcpConnected
// New State: OpenConfirm
// Messages:  Open message sent on passive session.
// Other:     The active session would have been closed before we process
//            EvTcpConnected.
TEST_P(StateMachineConnectParamTest1, BgpPassiveOpenThenTcpConnected) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), id_);
    EvTcpConnected();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::PASSIVE);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->active_session() == NULL);
}

// Old State: Connect
// Event:     EvTcpPassiveOpen + EvBgpOpen (from higher or lower id) +
//            EvTcpConnectFail
// New State: OpenConfirm
// Messages:  Open message sent on passive session.
// Other:     The active session would have been closed before we process
//            EvTcpConnectFail.
TEST_P(StateMachineConnectParamTest1, BgpPassiveOpenThenTcpConnectFail) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), id_);
    EvTcpConnectFail();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::PASSIVE);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->active_session() == NULL);
}

// Old State: Connect
// Event:     EvTcpPassiveOpen + EvBgpOpen (from higher or lower id) +
//            EvConnectTimerExpired
// New State: OpenConfirm
// Messages:  Open message sent on passive session.
// Other:     The active session would have been closed before we process
//            EvConnectTimerExpired.
TEST_P(StateMachineConnectParamTest1, BgpPassiveOpenThenConnectTimerExpired) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), id_);
    EvConnectTimerExpired();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::PASSIVE);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->active_session() == NULL);
}

INSTANTIATE_TEST_CASE_P(One, StateMachineConnectParamTest1, ::testing::Bool());

// Parameterize id (higher/lower) and holdtime (higher/lower)

typedef std::tr1::tuple<bool, bool> ConnectTestParams2;

class StateMachineConnectParamTest2 :
    public StateMachineConnectTest,
    public ::testing::WithParamInterface<ConnectTestParams2> {
protected:
    virtual void SetUp() {
        id_ = tr1::get<0>(GetParam()) ? lower_id_ : higher_id_;
        if (tr1::get<1>(GetParam())) {
            holdtime_ = StateMachine::kHoldTime - 1;
        } else {
            holdtime_ = StateMachine::kHoldTime + 1;
        }
        StateMachineConnectTest::SetUp();
    }

    virtual void TearDown() {
        StateMachineConnectTest::TearDown();
    }

    uint32_t id_;
    int holdtime_;
};

// Old State: Connect
// Event:     EvTcpPassiveOpen + EvBgpOpen (with higher or lower holdtime)
// New State: OpenConfirm
// Messages:  Open message sent on passive session.
TEST_P(StateMachineConnectParamTest2, BgpHoldtimeNegotiation) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), id_, holdtime_);
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::PASSIVE);
    TASK_UTIL_EXPECT_EQ(min(holdtime_, StateMachine::kHoldTime),
        sm_->hold_time());
}

INSTANTIATE_TEST_CASE_P(One, StateMachineConnectParamTest2,
    ::testing::Combine(::testing::Bool(), ::testing::Bool()));

class StateMachineOpenSentTest : public StateMachineUnitTest {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

// Old State: OpenSent
// Event:     EvTcpPassiveOpen
// New State: OpenSent
// Timers:    Open timer must be running since there's a passive session.
// Messages:  None.
TEST_F(StateMachineOpenSentTest, TcpPassiveOpen) {
    GetToState(StateMachine::OPENSENT);
    EvTcpPassiveOpen();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvTcpPassiveOpen.
// New State: OpenSent
// Timers:    Open timer should be running since we have a passive session.
// Messages:  None.
// Other:     Original passive session should get closed and deleted, while
//            the duplicate passive session becomes the new passive session.
TEST_F(StateMachineOpenSentTest, DuplicateTcpPassiveOpen1) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvTcpDuplicatePassiveOpen();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->duplicate_session() != NULL);
}

// Old State: OpenSent (via passive session)
// Event:     EvTcpPassiveOpen
// New State: Active
// Timers:    Open timer should be running since we have a passive session.
//            Connect timer must not be started since there's a passive session.
// Messages:  None.
// Other:     Original passive session should get closed and deleted, while
//            the duplicate passive session becomes the new passive session.
TEST_F(StateMachineOpenSentTest, DuplicateTcpPassiveOpen2) {
    GetToState(StateMachine::ACTIVE);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpDuplicatePassiveOpen();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(!ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->duplicate_session() != NULL);
}

// Old State: OpenSent (via active and passive session)
// Event:     EvTcpPassiveOpen
// New State: OpenSent
// Timers:    Open timer should be running since we have a passive session.
//            Connect timer must not be started since there's a passive session.
// Messages:  None.
// Other:     Original passive session should get closed and deleted, while
//            the duplicate passive session becomes the new passive session.
TEST_F(StateMachineOpenSentTest, DuplicateTcpPassiveOpen3) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpDuplicatePassiveOpen();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->duplicate_session() != NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvOpenTimerExpired
// New State: OpenSent
// Timers:    Open timer must not be running since it just expired.
// Messages:  Open message sent on passive session.
TEST_F(StateMachineOpenSentTest, TcpPassiveOpenThenOpenTimerExpired) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpNotification (passive)
// New State: OpenSent
// Timers:    Open timer must not be running since there's no passive session.
// Messages:  None.
TEST_F(StateMachineOpenSentTest, Notification1a) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpNotification(session_mgr_->passive_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent (via active and passive sessions)
// Event:     EvBgpNotification (passive)
// New State: OpenSent
// Timers:    Open timer must not be running since there's no passive session.
// Messages:  Open message sent on passive session.
TEST_F(StateMachineOpenSentTest, Notification1b) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    EvBgpNotification(session_mgr_->passive_session());
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpNotification (active)
// New State: Active
// Timers:    Open timer must be running since there's a passive session.
// Messages:  None.
TEST_F(StateMachineOpenSentTest, Notification2a) {
    GetToState(StateMachine::OPENSENT);
    EvTcpPassiveOpen();
    EvBgpNotification(session_mgr_->active_session());
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: OpenSent (via active and passive sessions)
// Event:     EvBgpNotification (active)
// New State: OpenSent
// Timers:    Open timer must not be running since it already expired.
// Messages:  None.
TEST_F(StateMachineOpenSentTest, Notification2b) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    EvBgpNotification(session_mgr_->active_session());
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: OpenSent
// Event:     EvBgpNotification (on active session)
// New State: Idle
// Messages:  None.
TEST_F(StateMachineOpenSentTest, Notification3a) {
    GetToState(StateMachine::OPENSENT);
    EvBgpNotification(session_mgr_->active_session());
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenSent (via passive session)
// Event:     EvBgpNotification (active)
// New State: Idle
// Messages:  None.
TEST_F(StateMachineOpenSentTest, Notification3b) {
    GetToState(StateMachine::ACTIVE);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    EvBgpNotification(session_mgr_->active_session());
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenSent
// Event:     EvTcpClose (on active session)
// New State: Active
// Timers:    Connect timer must be running since there's no passive session.
// Messages:  None.
TEST_F(StateMachineOpenSentTest, TcpActiveClose1) {
    GetToState(StateMachine::OPENSENT);
    EvTcpClose(session_mgr_->active_session());
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvTcpClose (active)
// New State: Active
// Timers:    Connect timer must not be running since there's a passive session.
//            Open timer must be running since there's a passive session.
// Messages:  None.
TEST_F(StateMachineOpenSentTest, TcpActiveClose2) {
    GetToState(StateMachine::OPENSENT);
    EvTcpPassiveOpen();
    EvTcpClose(session_mgr_->active_session());
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(!ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: OpenSent (via active and passive sessions)
// Event:     EvTcpClose (active)
// New State: OpenSent
// Timers:    Open timer must not be running since it already expired.
// Messages:  Open message sent on passive session.
TEST_F(StateMachineOpenSentTest, TcpActiveClose3) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    EvTcpClose(session_mgr_->active_session());
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvTcpClose (passive)
// New State: OpenSent
// Timers:    Open timer must not be running since there's no passive session.
// Messages:  None.
TEST_F(StateMachineOpenSentTest, TcpPassiveClose1) {
    GetToState(StateMachine::OPENSENT);
    EvTcpPassiveOpen();
    EvTcpClose(session_mgr_->passive_session());
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent (via passive session)
// Event:     EvTcpClose (passive)
// New State: Active
// Timers:    Open timer must not be running since there's no passive session.
//            Connect timer must be running since there's no passive session.
// Messages:  None.
TEST_F(StateMachineOpenSentTest, TcpPassiveClose2) {
    GetToState(StateMachine::ACTIVE);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    EvTcpClose(session_mgr_->passive_session());
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent (via active and passive sessions)
// Event:     EvTcpClose (passive)
// New State: OpenSent
// Timers:    Open timer must not be running since there's no passive session.
// Messages:  None.
TEST_F(StateMachineOpenSentTest, TcpPassiveClose3) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    EvTcpClose(session_mgr_->passive_session());
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpOpen (active with higher id)
// New State: OpenSent
// Timers:    Open timer must not be running since it was cancelled.
// Messages:  Open message sent on passive session.
TEST_F(StateMachineOpenSentTest, Collision1a) {
    GetToState(StateMachine::OPENSENT);
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->active_session(), higher_id_);
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: OpenSent (via active and passive sessions)
// Event:     EvBgpOpen (active with higher id)
// New State: OpenSent
// Timers:    Open timer must not be running since it already expired.
// Messages:  None.
TEST_F(StateMachineOpenSentTest, Collision1b) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    EvBgpOpenCustom(session_mgr_->active_session(), higher_id_);
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpOpen (passive with higher id)
// New State: OpenConfirm
// Messages:  Open message sent on passive session.
//            Keepalive message sent on passive session.
TEST_F(StateMachineOpenSentTest, Collision1c) {
    GetToState(StateMachine::OPENSENT);
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), higher_id_);
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::PASSIVE);
}

// Old State: OpenSent (via active and passive sessions)
// Event:     EvBgpOpen (passive with higher id)
// New State: OpenConfirm
// Messages:  Keepalive message sent on passive session.
TEST_F(StateMachineOpenSentTest, Collision1d) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    EvBgpOpenCustom(session_mgr_->passive_session(), higher_id_);
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::PASSIVE);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpOpen (passive with lower id)
// New State: OpenSent
// Timers:    Open timer must not be running since there's no passive session.
// Messages:  None.
TEST_F(StateMachineOpenSentTest, Collision2a) {
    GetToState(StateMachine::OPENSENT);
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), lower_id_);
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent (via active and passive sessions)
// Event:     EvBgpOpen (passive with lower id)
// New State: OpenSent
// Timers:    Open timer must not be running since it already expired.
// Messages:  None.
TEST_F(StateMachineOpenSentTest, Collision2b) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    EvBgpOpenCustom(session_mgr_->passive_session(), lower_id_);
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpOpen (active with lower id)
// New State: OpenConfirm
// Messages:  Keepalive message sent on active session.
TEST_F(StateMachineOpenSentTest, Collision2c) {
    GetToState(StateMachine::OPENSENT);
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->active_session(), lower_id_);
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::ACTIVE);
}

// Old State: OpenSent (via active and passive sessions)
// Event:     EvBgpOpen (active with lower id)
// New State: OpenConfirm
// Messages:  Keepalive message sent on active session.
TEST_F(StateMachineOpenSentTest, Collision2d) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    EvBgpOpenCustom(session_mgr_->active_session(), lower_id_);
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::ACTIVE);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpOpen (passive with duplicate id)
// New State: Idle
// Messages:  Notification message sent on passive session.
TEST_F(StateMachineOpenSentTest, Collision3a) {
    GetToState(StateMachine::OPENSENT);
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(),
        server_.bgp_identifier());
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenSent (via active and passive sessions)
// Event:     EvBgpOpen (passive with duplicate id)
// New State: Idle
// Messages:  Notification message sent on passive session.
TEST_F(StateMachineOpenSentTest, Collision3b) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    EvBgpOpenCustom(session_mgr_->passive_session(),
        server_.bgp_identifier());
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpOpen (active with duplicate id)
// New State: Idle
// Messages:  Notification message sent on active session.
TEST_F(StateMachineOpenSentTest, Collision3c) {
    GetToState(StateMachine::OPENSENT);
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->active_session(),
        server_.bgp_identifier());
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenSent (via active and passive sessions)
// Event:     EvBgpOpen (active with duplicate id)
// New State: Idle
// Messages:  Notification message sent on active session.
TEST_F(StateMachineOpenSentTest, Collision3d) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    EvBgpOpenCustom(session_mgr_->active_session(),
        server_.bgp_identifier());
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenSent
// Event:     EvBgpOpenError (active) + EvBgpKeepalive (active)
// New State: Idle
// Messages:  Notification message sent on active session.
TEST_F(StateMachineOpenSentTest, BgpOpenErrorThenKeepalive) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvBgpOpenError(session_mgr_->active_session());
    EvBgpKeepalive(session_mgr_->active_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpOpen (passive with lower id) +
//            EvBgpKeepalive (passive)
// New State: OpenSent
// Timers:    Open timer must not be running since there's no passive session.
// Messages:  Notification message sent on passive session.
TEST_F(StateMachineOpenSentTest, CollisionThenKeepalive) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), lower_id_);
    EvBgpKeepalive(session_mgr_->passive_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + 2 * EvBgpOpen (passive with lower id)
// New State: OpenSent
// Timers:    Open timer must not be running since there's no passive session.
// Messages:  Notification message sent on passive session.
TEST_F(StateMachineOpenSentTest, CollisionThenBgpOpen) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), lower_id_);
    EvBgpOpenCustom(session_mgr_->passive_session(), lower_id_);
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpOpen (passive with lower id) +
//            EvBgpNotification (passive)
// New State: OpenSent
// Timers:    Open timer must not be running since there's no passive session.
// Messages:  Notification message sent on passive session.
TEST_F(StateMachineOpenSentTest, CollisionThenNotification1) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), lower_id_);
    EvBgpNotification(session_mgr_->passive_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpOpen (passive with lower id) +
//            EvBgpNotification (active)
// New State: Active
// Timers:    Open timer must not be running since there's no passive session.
//            Connect timer must be running since there's no passive session.
// Messages:  Notification message sent on passive session.
TEST_F(StateMachineOpenSentTest, CollisionThenNotification2) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), lower_id_);
    EvBgpNotification(session_mgr_->active_session());
    TaskScheduler::GetInstance()->Start();
    RunToState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(ConnectTimerRunning());
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpOpen (passive with lower id) +
//            EvTcpClose (passive)
// New State: OpenSent
// Timers:    Open timer must not be running since there's no passive session.
// Messages:  Notification message sent on passive session.
TEST_F(StateMachineOpenSentTest, CollisionThenClose1a) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), lower_id_);
    EvTcpClose(session_mgr_->passive_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpOpen (active with lower id) +
//            EvTcpClose (passive)
// New State: OpenConfirm
// Messages:  Notification message sent on passive session.
//            Keepalive message sent on active session.
TEST_F(StateMachineOpenSentTest, CollisionThenClose1b) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->active_session(), lower_id_);
    EvTcpClose(session_mgr_->passive_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::ACTIVE);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpOpen (passive with higher id) +
//            EvTcpClose (active)
// New State: OpenConfirm
// Messages:  Keepalive message sent on passive session.
TEST_F(StateMachineOpenSentTest, CollisionThenClose2a) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), higher_id_);
    EvTcpClose(session_mgr_->active_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::PASSIVE);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpOpen (active with higher id) +
//            EvTcpClose (active)
// New State: OpenSent
// Timers:    Open timer must not be running since it must be cancelled.
// Messages:  Open message sent on passive session.
TEST_F(StateMachineOpenSentTest, CollisionThenClose2b) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->active_session(), higher_id_);
    EvTcpClose(session_mgr_->active_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpNotification (active) +
//            EvTcpClose (active)
// New State: Active
// Timers:    Open timer must be running since there's a passive session.
TEST_F(StateMachineOpenSentTest, ActiveNotificationThenClose) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpNotification(session_mgr_->active_session());
    EvTcpClose(session_mgr_->active_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() != NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpNotification (passive) +
//            EvTcpClose (passive)
// New State: OpenSent
// Timers:    Open timer must not be running since there's no passive session.
TEST_F(StateMachineOpenSentTest, PassiveNotificationThenClose) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpNotification(session_mgr_->passive_session());
    EvTcpClose(session_mgr_->passive_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpOpen (passive with lower id) +
//            EvOpenTimerExpired
// New State: OpenSent
// Timers:    Open timer must not be running since there's no passive session.
TEST_F(StateMachineOpenSentTest, CollisionThenOpenTimerExpired) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), lower_id_);
    EvOpenTimerExpired();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvBgpNotification (passive) +
//            EvOpenTimerExpired
// New State: OpenSent
// Timers:    Open timer must not be running since there's no passive session.
TEST_F(StateMachineOpenSentTest, NotificationThenOpenTimerExpired) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvBgpNotification(session_mgr_->passive_session());
    EvOpenTimerExpired();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent
// Event:     EvTcpPassiveOpen + EvTcpClose (passive) + EvOpenTimerExpired
// New State: OpenSent
// Timers:    Open timer must not be running since there's no passive session.
TEST_F(StateMachineOpenSentTest, TcpCloseThenOpenTimerExpired) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    EvTcpClose(session_mgr_->passive_session());
    EvOpenTimerExpired();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENSENT);
    TASK_UTIL_EXPECT_TRUE(!OpenTimerRunning());
    TASK_UTIL_EXPECT_TRUE(sm_->active_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(sm_->passive_session() == NULL);
}

// Old State: OpenSent
// Event:     EvTcpClose + EvAdminDown
// New State: Idle
// Timers:    IdleHold timer must not be running as it should get cancelled
//            when processing EvStop.
TEST_F(StateMachineOpenSentTest, TcpCloseThenAdminDown) {
    GetToState(StateMachine::OPENSENT);
    sm_->set_idle_hold_time(StateMachine::kMaxIdleHoldTime);
    EvTcpClose(session_mgr_->active_session());
    task_util::WaitForIdle();
    EvAdminDown();
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenSent
// Event:     EvHoldTimerExpired + EvBgpOpen (via active session)
// New State: Idle
// Timers:    IdleHold timer must be running.
// Messages:  Notification message sent on active session.
TEST_F(StateMachineOpenSentTest, HoldTimerExpiredThenBgpOpen1) {
    GetToState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvHoldTimerExpired();
    EvBgpOpen();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenSent (via passive session)
// Event:     EvHoldTimerExpired + EvBgpOpen (via passive session)
// New State: Idle
// Timers:    IdleHold timer must be running.
// Messages:  Notification message sent on passive session.
TEST_F(StateMachineOpenSentTest, HoldTimerExpiredThenBgpOpen2) {
    GetToState(StateMachine::ACTIVE);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    TaskScheduler::GetInstance()->Stop();
    EvHoldTimerExpired();
    EvBgpOpen(session_mgr_->passive_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenSent (via active and passive sessions)
// Event:     EvHoldTimerExpired + EvBgpOpen (via active session)
// New State: Idle
// Timers:    IdleHold timer must be running.
// Messages:  Notification message sent on active session.
TEST_F(StateMachineOpenSentTest, HoldTimerExpiredThenBgpOpen3a) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    TaskScheduler::GetInstance()->Stop();
    EvHoldTimerExpired();
    EvBgpOpen(session_mgr_->active_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenSent (via active and passive sessions)
// Event:     EvHoldTimerExpired + EvBgpOpen (via passive session)
// New State: Idle
// Timers:    IdleHold timer must be running.
// Messages:  Notification message sent on active session.
TEST_F(StateMachineOpenSentTest, HoldTimerExpiredThenBgpOpen3b) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    TaskScheduler::GetInstance()->Stop();
    EvHoldTimerExpired();
    EvBgpOpen(session_mgr_->passive_session());
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::IDLE);
}

// Parameterize id (higher/lower)

class StateMachineOpenSentParamTest1 :
    public StateMachineOpenSentTest,
    public ::testing::WithParamInterface<bool> {
protected:
    virtual void SetUp() {
        id_ = GetParam() ? lower_id_ : higher_id_;
        StateMachineOpenSentTest::SetUp();
    }

    virtual void TearDown() {
        StateMachineOpenSentTest::TearDown();
    }

    int id_;
};

// Old State: OpenSent
// Event:     EvBgpOpen (active with higher or lower id)
// New State: OpenConfirm
// Messages:  Keepalive message sent on active session.
TEST_P(StateMachineOpenSentParamTest1, BgpOpenActive) {
    GetToState(StateMachine::OPENSENT);
    EvBgpOpenCustom(session_mgr_->active_session(), id_);
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::ACTIVE);
}

// Old State: OpenSent (via passive session)
// Event:     EvBgpOpen (passive with higher or lower id)
// New State: OpenConfirm
// Messages:  Keepalive message sent on passive session.
TEST_P(StateMachineOpenSentParamTest1, BgpOpenPassive) {
    GetToState(StateMachine::ACTIVE);
    EvOpenTimerExpiredPassive();
    EvBgpOpenCustom(session_mgr_->passive_session(), id_);
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::PASSIVE);
}

INSTANTIATE_TEST_CASE_P(One, StateMachineOpenSentParamTest1, ::testing::Bool());

// Parameterize holdtime (higher/lower)

class StateMachineOpenSentParamTest2 :
    public StateMachineOpenSentTest,
    public ::testing::WithParamInterface<bool> {
protected:
    virtual void SetUp() {
        if (GetParam()) {
            holdtime_ = StateMachine::kHoldTime - 1;
        } else {
            holdtime_ = StateMachine::kHoldTime + 1;
        }
        StateMachineOpenSentTest::SetUp();
    }

    virtual void TearDown() {
        StateMachineOpenSentTest::TearDown();
    }

    int holdtime_;
};

// Old State: OpenSent (via active and passive session)
// Event:     EvBgpOpen (via active session)
// New State: OpenConfirm
TEST_P(StateMachineOpenSentParamTest2, BgpHoldtimeNegotiation1) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    EvBgpOpenCustom(session_mgr_->active_session(), lower_id_, holdtime_);
    VerifyState(StateMachine::OPENCONFIRM);
    TASK_UTIL_EXPECT_EQ(min(holdtime_, StateMachine::kHoldTime),
        sm_->hold_time());
}

// Old State: OpenSent (via active and passive session)
// Event:     EvBgpOpen (via active session)
// New State: OpenConfirm
TEST_P(StateMachineOpenSentParamTest2, BgpHoldtimeNegotiation2) {
    GetToState(StateMachine::OPENSENT);
    EvOpenTimerExpiredPassive();
    EvBgpOpenCustom(session_mgr_->passive_session(), higher_id_, holdtime_);
    VerifyState(StateMachine::OPENCONFIRM);
    TASK_UTIL_EXPECT_EQ(min(holdtime_, StateMachine::kHoldTime),
        sm_->hold_time());
}

INSTANTIATE_TEST_CASE_P(One, StateMachineOpenSentParamTest2, ::testing::Bool());

// Parameterize id (higher/lower) and holdtime (higher/lower)

typedef std::tr1::tuple<bool, bool> OpenSentTestParams3;

class StateMachineOpenSentParamTest3 :
    public StateMachineOpenSentTest,
    public ::testing::WithParamInterface<OpenSentTestParams3> {
protected:
    virtual void SetUp() {
        id_ = tr1::get<0>(GetParam()) ? lower_id_ : higher_id_;
        if (tr1::get<1>(GetParam())) {
            holdtime_ = StateMachine::kHoldTime - 1;
        } else {
            holdtime_ = StateMachine::kHoldTime + 1;
        }
        StateMachineOpenSentTest::SetUp();
    }

    virtual void TearDown() {
        StateMachineOpenSentTest::TearDown();
    }

    uint32_t id_;
    int holdtime_;
};

// Old State: OpenSent (via active session)
// Event:     EvBgpOpen (higher/lower id and higher/lower holdtime)
// New State: OpenConfirm
TEST_P(StateMachineOpenSentParamTest3, BgpHoldtimeNegotiation1) {
    GetToState(StateMachine::OPENSENT);
    EvBgpOpenCustom(session_mgr_->active_session(), id_, holdtime_);
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::ACTIVE);
    TASK_UTIL_EXPECT_EQ(min(holdtime_, StateMachine::kHoldTime),
        sm_->hold_time());
}

// Old State: OpenSent (via passive session)
// Event:     EvBgpOpen (higher/lower id and higher/lower holdtime)
// New State: OpenConfirm
TEST_P(StateMachineOpenSentParamTest3, BgpHoldtimeNegotiation2) {
    GetToState(StateMachine::ACTIVE);
    EvOpenTimerExpiredPassive();
    VerifyState(StateMachine::OPENSENT);
    EvBgpOpenCustom(session_mgr_->passive_session(), id_, holdtime_);
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::PASSIVE);
    TASK_UTIL_EXPECT_EQ(min(holdtime_, StateMachine::kHoldTime),
        sm_->hold_time());
}

INSTANTIATE_TEST_CASE_P(One, StateMachineOpenSentParamTest3,
    ::testing::Combine(::testing::Bool(), ::testing::Bool()));

class StateMachineOpenConfirmTest : public StateMachineUnitTest {
protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

// Old State: OpenConfirm (via active session)
// Event:     EvBgpNotification (active)
// New State: Idle
TEST_F(StateMachineOpenConfirmTest, Notification) {
    GetToState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::ACTIVE);
    EvBgpNotification(session_mgr_->active_session());
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenConfirm (via active session)
// Event:     EvTcpPassiveOpen
// New State: OpenConfirm
// Messages:  Notification message sent on passive session.
TEST_F(StateMachineOpenConfirmTest, TcpPassiveOpen) {
    GetToState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::ACTIVE);
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
}

// Old State: OpenConfirm (via active session)
// Event:     EvBgpKeepalive (active)
// New State: Established
TEST_F(StateMachineOpenConfirmTest, BgpKeepalive) {
    GetToState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::ACTIVE);
    EvBgpKeepalive(session_mgr_->active_session());
    VerifyState(StateMachine::ESTABLISHED);
    VerifyDirection(BgpSessionMock::ACTIVE);
}

// Old State: OpenConfirm (via active session)
// Event:     EvBgpNotification (active) + EvHoldTimerExpired
// New State: Idle
TEST_F(StateMachineOpenConfirmTest, NotificationThenHoldTimerExpired) {
    GetToState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::ACTIVE);
    TaskScheduler::GetInstance()->Stop();
    EvBgpNotification(session_mgr_->active_session());
    EvHoldTimerExpired();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenConfirm (via active session)
// Event:     EvTcpClose (active) + EvHoldTimerExpired
// New State: Idle
TEST_F(StateMachineOpenConfirmTest, TcpCloseThenHoldTimerExpired) {
    GetToState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::ACTIVE);
    TaskScheduler::GetInstance()->Stop();
    EvTcpClose(session_mgr_->active_session());
    EvHoldTimerExpired();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenConfirm
// Event:     EvTcpClose + EvAdminDown
// New State: Idle
// Timers:    IdleHold timer must not be running as it should get cancelled
//            when processing EvStop.
TEST_F(StateMachineOpenConfirmTest, TcpCloseThenAdminDown) {
    GetToState(StateMachine::OPENCONFIRM);
    sm_->set_idle_hold_time(StateMachine::kMaxIdleHoldTime);
    EvTcpClose(session_mgr_->active_session());
    task_util::WaitForIdle();
    EvAdminDown();
    VerifyState(StateMachine::IDLE);
}

// Parameterize id (higher/lower)

class StateMachineOpenConfirmParamTest1 :
    public StateMachineOpenConfirmTest,
    public ::testing::WithParamInterface<bool> {
protected:
    virtual void SetUp() {
        id_ = GetParam() ? lower_id_ : higher_id_;
        StateMachineOpenConfirmTest::SetUp();
    }

    virtual void TearDown() {
        StateMachineOpenConfirmTest::TearDown();
    }

    int id_;
};

// Old State: OpenConfirm (via passive session)
// Event:     EvBgpNotification (passive)
// New State: Idle
TEST_P(StateMachineOpenConfirmParamTest1, Notification) {
    GetToState(StateMachine::ACTIVE);
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), id_);
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::PASSIVE);
    EvBgpNotification(session_mgr_->passive_session());
    VerifyState(StateMachine::IDLE);
}

// Old State: OpenConfirm (via passive session)
// Event:     EvTcpPassiveOpen (duplicate)
// New State: OpenConfirm
TEST_P(StateMachineOpenConfirmParamTest1, TcpPassiveOpen) {
    GetToState(StateMachine::ACTIVE);
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), id_);
    VerifyState(StateMachine::OPENCONFIRM);
    TaskScheduler::GetInstance()->Stop();
    EvTcpDuplicatePassiveOpen();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::OPENCONFIRM);
    VerifyDirection(BgpSessionMock::PASSIVE);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() != NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->duplicate_session() == NULL);
}

// Old State: OpenConfirm (via passive session)
// Event:     EvBgpKeepalive (passive)
// New State: Established
TEST_P(StateMachineOpenConfirmParamTest1, BgpKeepalive) {
    GetToState(StateMachine::ACTIVE);
    EvTcpPassiveOpen();
    EvBgpOpenCustom(session_mgr_->passive_session(), id_);
    VerifyState(StateMachine::OPENCONFIRM);
    EvBgpKeepalive(session_mgr_->passive_session());
    VerifyState(StateMachine::ESTABLISHED);
    VerifyDirection(BgpSessionMock::PASSIVE);
}

INSTANTIATE_TEST_CASE_P(One, StateMachineOpenConfirmParamTest1,
    ::testing::Bool());


class StateMachineEstablishedTest : public StateMachineUnitTest {
protected:
    virtual void SetUp() {
        GetToState(StateMachine::ESTABLISHED);
        VerifyDirection(BgpSessionMock::ACTIVE);
    }

    virtual void TearDown() {
    }
};

// Old State: Established
// Event:     EvTcpPassiveOpen
// New State: Established
TEST_F(StateMachineEstablishedTest, TcpPassiveOpen) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    TaskScheduler::GetInstance()->Start();
    VerifyState(StateMachine::ESTABLISHED);
    VerifyDirection(BgpSessionMock::ACTIVE);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->active_session() != NULL);
}

// Old State: Established
// Event:     EvTcpPassiveOpen + EvBgpOpen (on passive session)
// New State: Established
TEST_F(StateMachineEstablishedTest, TcpPassiveOpenThenBgpOpen) {
    TaskScheduler::GetInstance()->Stop();
    EvTcpPassiveOpen();
    BgpSessionMock *session = session_mgr_->passive_session();
    EvBgpOpenCustom(session, lower_id_);
    TaskScheduler::GetInstance()->Start();
    task_util::WaitForIdle();
    VerifyState(StateMachine::ESTABLISHED);
}

// Old State: Established
// Event:     EvBgpOpen (on active session)
// New State: Idle
TEST_F(StateMachineEstablishedTest, BgpOpen) {
    TaskScheduler::GetInstance()->Stop();
    EvBgpOpen(session_mgr_->active_session());
    TaskScheduler::GetInstance()->Start();
    task_util::WaitForIdle();
    VerifyState(StateMachine::IDLE);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->passive_session() == NULL);
    TASK_UTIL_EXPECT_TRUE(session_mgr_->active_session() == NULL);
}

// Old State: Established
// Event:     EvTcpClose + EvAdminDown
// New State: Idle
// Timers:    IdleHold timer must not be running as it should get cancelled
//            when processing EvStop.
TEST_F(StateMachineEstablishedTest, TcpCloseThenAdminDown) {
    sm_->set_idle_hold_time(StateMachine::kMaxIdleHoldTime);
    EvTcpClose(session_mgr_->active_session());
    task_util::WaitForIdle();
    EvAdminDown();
    VerifyState(StateMachine::IDLE);
}

int main(int argc, char **argv) {
    bgp_log_test::init();
    ControlNode::SetDefaultSchedulingPolicy();
    BgpObjectFactory::Register<BgpSessionManager>(
        boost::factory<BgpSessionManagerMock *>());
    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    TaskScheduler::GetInstance()->Terminate();
    return result;
}
