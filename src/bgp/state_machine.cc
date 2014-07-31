/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "state_machine.h"

#include <typeinfo>

#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/statechart/custom_reaction.hpp>
#include <boost/statechart/event.hpp>
#include <boost/statechart/simple_state.hpp>
#include <boost/statechart/state.hpp>
#include <boost/statechart/state_machine.hpp>
#include <boost/statechart/transition.hpp>
#include <sandesh/sandesh.h>
#include <tbb/atomic.h>

#include "base/logging.h"
#include "base/task_annotations.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session.h"
#include "bgp/bgp_session_manager.h"

using namespace std;

namespace mpl = boost::mpl;
namespace sc = boost::statechart;

const int StateMachine::kOpenTime = 15;                // seconds
const int StateMachine::kConnectInterval = 30;         // seconds
const int StateMachine::kHoldTime = 90;                // seconds
const int StateMachine::kOpenSentHoldTime = 240;       // seconds
const int StateMachine::kIdleHoldTime = 5000;          // milliseconds
const int StateMachine::kMaxIdleHoldTime = 100 * 1000; // milliseconds
const int StateMachine::kJitter = 10;                  // percentage

#define SM_LOG(level, _Msg)                                    \
    do {                                                       \
        std::ostringstream out;                                \
        out << _Msg;                                           \
        BGP_LOG_SERVER(peer_, (BgpTable *) 0);                 \
        BGP_LOG(BgpPeerStateMachine, level,                    \
                BGP_LOG_FLAG_SYSLOG, BGP_PEER_DIR_NA,          \
                peer_ ? peer_->ToUVEKey() : "",                \
                out.str());                                    \
    } while (false)

namespace fsm {

// Events for the state machine.  These are listed in roughly the same order
// as the RFC - Administrative, Timer, Tcp and Message.

struct EvStart : sc::event<EvStart> {
    EvStart() {
    }
    static const char *Name() {
        return "EvStart";
    }
};

struct EvStop : sc::event<EvStop> {
    EvStop(int subcode) : subcode(subcode) {
    }
    static const char *Name() {
        return "EvStop";
    }
    int subcode;
};

struct EvIdleHoldTimerExpired : sc::event<EvIdleHoldTimerExpired> {
    EvIdleHoldTimerExpired(Timer *timer)  : timer_(timer){
    }
    static const char *Name() {
        return "EvIdleHoldTimerExpired";
    }
    bool validate(StateMachine *state_machine) const {
        return !timer_->cancelled();
    }

    Timer *timer_;
};

struct EvConnectTimerExpired : sc::event<EvConnectTimerExpired> {
    EvConnectTimerExpired(Timer *timer) : timer_(timer) {
    }
    static const char *Name() {
        return "EvConnectTimerExpired";
    }
    bool validate(StateMachine *state_machine) const {
        if (timer_->cancelled()) {
            return false;
        } else if (state_machine->get_state() == StateMachine::ACTIVE) {
            return (state_machine->passive_session() == NULL);
        } else if (state_machine->get_state() == StateMachine::CONNECT) {
            return (!state_machine->active_session() ||
                    !state_machine->active_session()->IsEstablished());
        }
        return false;
    }

    Timer *timer_;
};

struct EvOpenTimerExpired : sc::event<EvOpenTimerExpired> {
    EvOpenTimerExpired(Timer *timer) : timer_(timer) {
    }
    static const char *Name() {
        return "EvOpenTimerExpired";
    }
    bool validate(StateMachine *state_machine) const {
        if (timer_->cancelled()) {
            return false;
        } else {
            return (state_machine->passive_session() != NULL);
        }
    }

    Timer *timer_;
};

struct EvHoldTimerExpired : sc::event<EvHoldTimerExpired> {
    EvHoldTimerExpired(Timer *timer) : timer_(timer) {
    }
    static const char *Name() {
        return "EvHoldTimerExpired";
    }
    bool validate(StateMachine *state_machine) const {
        if (timer_->cancelled()) {
            return false;
        } else if (state_machine->get_state() == StateMachine::OPENSENT) {
            return true;
        } else {
            return (state_machine->peer()->session() != NULL);
        }
    }

    Timer *timer_;
};

struct EvTcpConnected : sc::event<EvTcpConnected> {
    EvTcpConnected(BgpSession *session) : session(session) {
    }
    static const char *Name() {
        return "EvTcpConnected";
    }
    bool validate(StateMachine *state_machine) const {
        return (state_machine->active_session() == session);
    }

    BgpSession *session;
};

struct EvTcpConnectFail : sc::event<EvTcpConnectFail> {
    EvTcpConnectFail(BgpSession *session) : session(session) {
    }
    static const char *Name() {
        return "EvTcpConnectFail";
    }
    bool validate(StateMachine *state_machine) const {
        return (state_machine->active_session() == session);
    }

    BgpSession *session;
};

struct EvTcpPassiveOpen : sc::event<EvTcpPassiveOpen> {
    EvTcpPassiveOpen(BgpSession *session) : session(session) {
    }
    static const char *Name() {
        return "EvTcpPassiveOpen";
    }

    BgpSession *session;
};

struct EvTcpClose : sc::event<EvTcpClose> {
    EvTcpClose(BgpSession *session) : session(session) {
    }
    static const char *Name() {
        return "EvTcpClose";
    }
    bool validate(StateMachine *state_machine) const {
        return ((state_machine->peer()->session() == session) ||
                (state_machine->active_session() == session) ||
                (state_machine->passive_session() == session));
    }

    BgpSession *session;
};

// Used to defer the session delete after all events currently on the queue.
struct EvTcpDeleteSession : sc::event<EvTcpDeleteSession> {
    EvTcpDeleteSession(BgpSession *session) : session(session) {
    }
    static const char *Name() {
        return "EvTcpDeleteSession";
    }

    BgpSession *session;
};

struct EvBgpHeaderError : sc::event<EvBgpHeaderError> {
    EvBgpHeaderError(BgpSession *session, int subcode, const uint8_t *_data,
        size_t data_size)
        : session(session), subcode(subcode) {
        if (_data)
            data = std::string((const char *)_data, data_size);
    }
    static const char *Name() {
        return "EvBgpHeaderError";
    }

    BgpSession *session;
    int subcode;
    std::string data;
};

struct EvBgpOpen : sc::event<EvBgpOpen> {
    EvBgpOpen(BgpSession *session, const BgpProto::OpenMessage *msg)
        : session(session), msg(msg) {
        BGP_LOG_PEER(Message, session->Peer(), SandeshLevel::SYS_INFO,
            BGP_LOG_FLAG_SYSLOG, BGP_PEER_DIR_IN,
            "Open " << msg->ToString());
    }
    static const char *Name() {
        return "EvBgpOpen";
    }
    bool validate(StateMachine *state_machine) const {
        return ((state_machine->peer()->session() == session) ||
                (state_machine->active_session() == session) ||
                (state_machine->passive_session() == session));
    }

    BgpSession *session;
    boost::shared_ptr<const BgpProto::OpenMessage> msg;
};

struct EvBgpOpenError : sc::event<EvBgpOpenError> {
    EvBgpOpenError(BgpSession *session, int subcode,
        const uint8_t *_data = NULL, size_t data_size = 0)
        : session(session), subcode(subcode) {
        if (subcode == BgpProto::Notification::UnsupportedVersion) {
            // For unsupported version, we need to send our version in the
            // data field.
            char version = 4;
            data.push_back(version);
        } else if (_data) {
            data = std::string((const char *)_data, data_size);
        }
    }
    static const char *Name() {
        return "EvBgpOpenError";
    }

    BgpSession *session;
    int subcode;
    std::string data;
};

struct EvBgpKeepalive : sc::event<EvBgpKeepalive> {
    EvBgpKeepalive(BgpSession *session) : session(session) {
        const StateMachine *state_machine = session->Peer()->state_machine();
        SandeshLevel::type log_level;
        if (state_machine->get_state() == StateMachine::ESTABLISHED) {
            log_level = Sandesh::LoggingUtLevel();
        } else {
            log_level = SandeshLevel::SYS_INFO;
        }
        BGP_LOG_PEER(Message, session->Peer(), log_level,
                     BGP_LOG_FLAG_SYSLOG, BGP_PEER_DIR_IN, "Keepalive");
    }
    static const char *Name() {
        return "EvBgpKeepalive";
    }
    bool validate(StateMachine *state_machine) const {
        return !session->IsClosed();
    }

    BgpSession *session;
};

struct EvBgpNotification : sc::event<EvBgpNotification> {
    EvBgpNotification(BgpSession *session, const BgpProto::Notification *msg)
        : session(session), msg(msg) {

        // Use SYS_DEBUG for connection collision, SYS_NOTICE for the rest.
        SandeshLevel::type log_level;
        if (msg->error == BgpProto::Notification::Cease &&
            msg->subcode == BgpProto::Notification::ConnectionCollision) {
            log_level = SandeshLevel::SYS_DEBUG;
        } else {
            log_level = SandeshLevel::SYS_NOTICE;
        }
        string peer_key =
            session->Peer() ? session->Peer()->ToUVEKey() : session->ToString();
        BGP_LOG(BgpPeerNotification, log_level, BGP_LOG_FLAG_ALL, peer_key,
                BGP_PEER_DIR_IN, msg->error, msg->subcode, msg->ToString());
    }
    static const char *Name() {
        return "EvBgpNotification";
    }
    bool validate(StateMachine *state_machine) const {
        return ((state_machine->peer()->session() == session) ||
                (state_machine->active_session() == session) ||
                (state_machine->passive_session() == session));
    }

    BgpSession *session;
    boost::shared_ptr<const BgpProto::Notification> msg;
};

struct EvBgpUpdate : sc::event<EvBgpUpdate> {
    EvBgpUpdate(BgpSession *session, const BgpProto::Update *msg)
        : session(session), msg(msg) {
        BGP_LOG_PEER(Message, session->Peer(), Sandesh::LoggingUtLevel(),
                     BGP_LOG_FLAG_SYSLOG, BGP_PEER_DIR_IN, "Update");
    }
    static const char *Name() {
        return "EvBgpUpdate";
    }

    BgpSession *session;
    boost::shared_ptr<const BgpProto::Update> msg;
};

struct EvBgpUpdateError : sc::event<EvBgpUpdateError> {
    EvBgpUpdateError(BgpSession *session, int subcode, std::string data)
        : session(session), subcode(subcode), data(data) {
    }
    static const char *Name() {
        return "EvBgpUpdateError";
    }

    BgpSession *session;
    int subcode;
    std::string data;
};

// States for the BGP state machine.
struct Idle;
struct Active;
struct Connect;
struct OpenSent;
struct OpenConfirm;
struct Established;

template <typename Ev, int code = 0>
struct TransitToIdle {
    typedef sc::transition<Ev, Idle, StateMachine,
        &StateMachine::OnIdle<Ev, code> > reaction;
};

template <>
struct TransitToIdle<EvBgpNotification, 0> {
    typedef sc::transition<EvBgpNotification, Idle, StateMachine,
        &StateMachine::OnIdleNotification> reaction;
};

template <typename Ev>
struct IdleCease {
    typedef sc::transition<Ev, Idle, StateMachine,
        &StateMachine::OnIdleCease<Ev> > reaction;
};

template <typename Ev>
struct IdleFsmError {
    typedef sc::transition<Ev, Idle, StateMachine,
        &StateMachine::OnIdle<Ev, BgpProto::Notification::FSMErr> > reaction;
};

template <typename Ev, int code>
struct IdleError {
    typedef sc::transition<Ev, Idle, StateMachine,
            &StateMachine::OnIdleError<Ev, code> > reaction;
};

//
// We start out in Idle and progress when we get EvStart. We also come back
// to Idle when there's any kind of error that we need to recover from.
//
struct Idle : sc::state<Idle, StateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvStart>,
        sc::custom_reaction<EvStop>,
        sc::custom_reaction<EvIdleHoldTimerExpired>,
        sc::custom_reaction<EvTcpPassiveOpen>
    > reactions;

    // Increment the flap count after setting the state.  This is friendly to
    // tests that first wait for the flap count to go up and then wait for the
    // state to reach ESTABLISHED again.  Incrementing the flap count before
    // setting the state could cause tests to break if they look at the old
    // state (which is still ESTABLISHED) and assume that it's the new state.
    // This could also be solved by using a mutex but it's not really needed.
    Idle(my_context ctx) : my_base(ctx) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        BgpSession *session = peer->session();
        peer->clear_session();
        state_machine->set_active_session(NULL);
        state_machine->set_passive_session(NULL);
        state_machine->DeleteSession(session);
        state_machine->CancelOpenTimer();
        state_machine->CancelIdleHoldTimer();
        bool flap = (state_machine->get_state() == StateMachine::ESTABLISHED);
        state_machine->set_state(StateMachine::IDLE);
        if (flap)
            peer->increment_flap_count();
    }

    ~Idle() {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->CancelIdleHoldTimer();
    }

    // Start idle hold timer if it's enabled, else go to Active right away.
    sc::result react(const EvStart &event) {
        StateMachine *state_machine = &context<StateMachine>();
        if (state_machine->idle_hold_time()) {
            state_machine->StartIdleHoldTimer();
        } else {
            return transit<Active>();
        }
        return discard_event();
    }

    // Stop idle hold timer and stay in Idle.
    sc::result react(const EvStop &event) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->CancelIdleHoldTimer();
        state_machine->peer()->Close();
        return discard_event();
    }

    // The idle hold timer expired, go to Active.
    sc::result react(const EvIdleHoldTimerExpired &event) {
        return transit<Active>();
    }

    // Delete the session and ignore event.
    sc::result react(const EvTcpPassiveOpen &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpSession *session = event.session;
        state_machine->DeleteSession(session);
        return discard_event();
    }
};

//
// In Active state, we wait for the connect timer timer to expire before we
// move to Connect and start the active session. If we get a passive session
// we accept it and wait for our delayed open timer to expire.
//
struct Active : sc::state<Active, StateMachine> {
    typedef mpl::list<
        IdleCease<EvStop>::reaction,
        sc::custom_reaction<EvConnectTimerExpired>,
        sc::custom_reaction<EvOpenTimerExpired>,
        sc::custom_reaction<EvTcpPassiveOpen>,
        sc::custom_reaction<EvTcpClose>,
        sc::custom_reaction<EvBgpOpen>,
        TransitToIdle<EvBgpNotification>::reaction,
        IdleFsmError<EvBgpKeepalive>::reaction,
        IdleFsmError<EvBgpUpdate>::reaction,
        IdleError<EvBgpHeaderError, BgpProto::Notification::MsgHdrErr>::reaction,
        IdleError<EvBgpOpenError, BgpProto::Notification::OpenMsgErr>::reaction,
        IdleError<EvBgpUpdateError, BgpProto::Notification::UpdateMsgErr>::reaction
    > reactions;

    // Start the connect timer if we don't have a passive session. There may
    // a passive session if we got here from Connect or OpenSent.
    Active(my_context ctx) : my_base(ctx) {
        StateMachine *state_machine = &context<StateMachine>();
        if (state_machine->passive_session() == NULL)
            state_machine->StartConnectTimer(state_machine->GetConnectTime());
        state_machine->set_state(StateMachine::ACTIVE);
    }

    // Stop the connect timer.  If we are going to Connect state, the timer
    // will be started again from the constructor for that state.
    ~Active() {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->CancelConnectTimer();
    }

    // The connect timer expired, go to Connect.
    sc::result react(const EvConnectTimerExpired &event) {
        return transit<Connect>();
    }

    // Send an OPEN message on the passive session and go to OpenSent.
    sc::result react(const EvOpenTimerExpired &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpSession *session = state_machine->passive_session();
        if (session) {
            BgpPeer *peer = state_machine->peer();
            peer->SendOpen(session);
            return transit<OpenSent>();
        }
        return discard_event();
    }

    // Cancel the connect timer since we now have a passive session. Note
    // that we get rid of any existing passive session if we get another
    // one. Also start the open timer in order to implement a delayed open
    // on the passive session.
    sc::result react(const EvTcpPassiveOpen &event) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->set_passive_session(event.session);
        state_machine->CancelConnectTimer();
        state_machine->StartOpenTimer(StateMachine::kOpenTime);
        return discard_event();
    }

    // Start the connect timer since we don't have a passive session anymore.
    sc::result react(const EvTcpClose &event) {
        StateMachine *state_machine = &context<StateMachine>();
        if (event.session == state_machine->passive_session()) {
            state_machine->set_passive_session(NULL);
            state_machine->CancelOpenTimer();
            state_machine->StartConnectTimer(state_machine->GetConnectTime());
        }
        return discard_event();
    }

    // We received an OPEN message on the passive session. Send OPEN message
    // and go to OpenConfirm.
    sc::result react(const EvBgpOpen &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        BgpSession *session = state_machine->passive_session();

        // If EvTcpPassiveOpen was received in IDLE state and the following
        // open message happens to be processed when we are in Active state,
        // we may not have the passive session for open message. Ignore the
        // event in that case.
        if (!session)
            return discard_event();

        // Ignore the OPEN if it was received on a stale passive session.
        // This can happen if we got another passive session between the
        // original passive session and the OPEN message on that session.
        if (session != event.session)
            return discard_event();

        // Send OPEN and go to OpenConfirm.
        int local_holdtime = state_machine->GetConfiguredHoldTime();
        state_machine->set_hold_time(min(event.msg->holdtime, local_holdtime));
        state_machine->AssignSession(false);
        peer->SendOpen(session);
        peer->SetCapabilities(event.msg.get());
        return transit<OpenConfirm>();
    }
};

//
// In Connect state, we wait for the active session to come up. We also accept
// a passive session if we get one and start a delayed open timer.
//
struct Connect : sc::state<Connect, StateMachine> {
    typedef mpl::list<
        IdleCease<EvStop>::reaction,
        sc::custom_reaction<EvConnectTimerExpired>,
        sc::custom_reaction<EvOpenTimerExpired>,
        sc::custom_reaction<EvTcpConnected>,
        sc::custom_reaction<EvTcpConnectFail>,
        sc::custom_reaction<EvTcpPassiveOpen>,
        sc::custom_reaction<EvTcpClose>,
        sc::custom_reaction<EvBgpOpen>,
        TransitToIdle<EvBgpNotification>::reaction,
        IdleFsmError<EvBgpKeepalive>::reaction,
        IdleFsmError<EvBgpUpdate>::reaction,
        IdleError<EvBgpHeaderError, BgpProto::Notification::MsgHdrErr>::reaction,
        IdleError<EvBgpOpenError, BgpProto::Notification::OpenMsgErr>::reaction,
        IdleError<EvBgpUpdateError, BgpProto::Notification::UpdateMsgErr>::reaction
    > reactions;

    Connect(my_context ctx) : my_base(ctx) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->connect_attempts_inc();
        state_machine->StartConnectTimer(state_machine->GetConnectTime());
        state_machine->StartSession();
        state_machine->set_state(StateMachine::CONNECT);
    }

    ~Connect() {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->CancelConnectTimer();
    }

    // Get rid of the active session and go back to Active.
    sc::result react(const EvConnectTimerExpired &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        state_machine->set_active_session(NULL);
        peer->inc_connect_timer_expired();
        return transit<Active>();
    }

    // The open timer for the passive session expired.  Since the active
    // session has not yet come up we get rid of it and decide to use the
    // passive session.  Send an OPEN on the passive session and move to
    // OpenSent.
    sc::result react(const EvOpenTimerExpired &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        peer->SendOpen(state_machine->passive_session());
        state_machine->set_active_session(NULL);
        return transit<OpenSent>();
    }

    // The active session is up.  Send an OPEN right away and go to OpenSent.
    // Note that we may also have the open timer running if we have a passive
    // session.  Things will eventually get resolved in the OpenSent state.
    sc::result react(const EvTcpConnected &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        BgpSession *session = state_machine->active_session();
        peer->SendOpen(session);
        return transit<OpenSent>();
    }

    // Delete the active session and go to Active.  Note that we may still
    // have a passive session.
    sc::result react(const EvTcpConnectFail &event) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->set_active_session(NULL);
        return transit<Active>();
    }

    // Start the open timer in order to implement a delayed open on passive
    // session.  Note that we get rid of any existing passive session if we
    // had one.
    sc::result react(const EvTcpPassiveOpen &event) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->set_passive_session(event.session);
        state_machine->StartOpenTimer(StateMachine::kOpenTime);
        return discard_event();
    }

    // Either the active or passive session got closed.
    sc::result react(const EvTcpClose &event) {
        StateMachine *state_machine = &context<StateMachine>();
        if (event.session == state_machine->passive_session()) {

            // Get rid of the passive session and cancel the open timer.
            // Stay in Connect and wait for the active session to come up.
            state_machine->set_passive_session(NULL);
            state_machine->CancelOpenTimer();
            return discard_event();

        } else {

            // Get rid of the active session and go to Active.  Note that we
            // may still have a passive session at this point.
            assert(event.session == state_machine->active_session());
            state_machine->set_active_session(NULL);
            return transit<Active>();
        }
    }

    // We received an OPEN message on the passive session. Send OPEN message
    // and go to OpenConfirm.
    sc::result react(const EvBgpOpen &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        BgpSession *session = state_machine->passive_session();

        // If EvTcpPassiveOpen was received in IDLE state and the following
        // open message happens to be processed when we are in Connect state,
        // we may not have the passive session for open message. Ignore the
        // event in that case.
        if (!session)
            return discard_event();

        // Ignore the OPEN if it was received on a stale passive session.
        // This can happen if we got another passive session between the
        // original passive session and the OPEN message on that session.
        if (session != event.session)
            return discard_event();

        // Send OPEN and go to OpenConfirm.  Since we've decided to use the
        // passive session, we get rid of the active one.
        int local_holdtime = state_machine->GetConfiguredHoldTime();
        state_machine->set_hold_time(min(event.msg->holdtime, local_holdtime));
        state_machine->set_active_session(NULL);
        state_machine->AssignSession(false);
        peer->SendOpen(session);
        peer->SetCapabilities(event.msg.get());
        return transit<OpenConfirm>();
    }
};

//
// In the OpenSent state, we wait for the other end to send an OPEN message.
// The state machine reaches OpenSent after sending an immediate OPEN message
// on the active connection or a delayed OPEN on a passive connection. In the
// former case there may be both a passive and active session. In the latter,
// there is only a passive connection.
//
struct OpenSent : sc::state<OpenSent, StateMachine> {
    typedef mpl::list<
        IdleCease<EvStop>::reaction,
        sc::custom_reaction<EvOpenTimerExpired>,
        TransitToIdle<EvHoldTimerExpired, BgpProto::Notification::HoldTimerExp>::reaction,
        sc::custom_reaction<EvTcpPassiveOpen>,
        sc::custom_reaction<EvTcpClose>,
        sc::custom_reaction<EvBgpOpen>,
        sc::custom_reaction<EvBgpNotification>,
        IdleFsmError<EvBgpKeepalive>::reaction,
        IdleFsmError<EvBgpUpdate>::reaction,
        IdleError<EvBgpHeaderError, BgpProto::Notification::MsgHdrErr>::reaction,
        IdleError<EvBgpOpenError, BgpProto::Notification::OpenMsgErr>::reaction,
        IdleError<EvBgpUpdateError, BgpProto::Notification::UpdateMsgErr>::reaction
    > reactions;

    // Start the hold timer to ensure that we don't get stuck in OpenSent if
    // the other end never sends an OPEN message.
    OpenSent(my_context ctx) : my_base(ctx) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->set_hold_time(StateMachine::kOpenSentHoldTime);
        state_machine->StartHoldTimer();
        state_machine->set_state(StateMachine::OPENSENT);
    }

    // Cancel the hold timer.  If we go to OpenConfirm, the timer will get
    // started again from the constructor for that state.
    ~OpenSent() {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->CancelHoldTimer();
    }

    // Send an OPEN message on the passive session.  This means that we must
    // have got to OpenSent because we sent an OPEN on the active session.
    // Stay in OpenSent and wait for the other end to send an OPEN message.
    sc::result react(const EvOpenTimerExpired &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        peer->SendOpen(state_machine->passive_session());
        return discard_event();
    }

    // Update the passive session and start the open timer. Note that any
    // existing passive session will get deleted.
    sc::result react(const EvTcpPassiveOpen &event) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->set_passive_session(event.session);
        state_machine->StartOpenTimer(StateMachine::kOpenTime);

        // If we don't have an active session, we need to go back to Active
        // since we haven't sent an OPEN message on the new passive session.
        // If we have active session, it means that we sent an OPEN message
        // on it already, so we can stay in OpenSent.
        if (!state_machine->active_session()) {
            return transit<Active>();
        } else {
            return discard_event();
        }
    }

    // Either the passive or the active session closed.
    sc::result react(const EvTcpClose &event) {
        StateMachine *state_machine = &context<StateMachine>();
        if (event.session == state_machine->active_session()) {

            // Since the active session was closed, we go back to Active if
            // don't have a passive session or if we haven't yet sent an OPEN
            // on the passive session.
            state_machine->set_active_session(NULL);
            if (state_machine->passive_session() == NULL ||
                state_machine->OpenTimerRunning()) {
                return transit<Active>();
            }

        } else {

            // Since the passive session was closed, we cancel the open timer.
            // We need to go back to Active if don't have a active session.
            state_machine->set_passive_session(NULL);
            state_machine->CancelOpenTimer();
            if (state_machine->active_session() == NULL)
                return transit<Active>();
        }

        return discard_event();
    }

    // This one is pretty involved.
    sc::result react(const EvBgpOpen &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        BgpSession *session = NULL;

        if (state_machine->passive_session() &&
            state_machine->active_session()) {

            // Need to resolve connection collision.
            uint32_t local_bgp_id = peer->server()->bgp_identifier();
            if (event.msg->identifier > local_bgp_id) {

                // Passive connection wins, close the active session.
                peer->SendNotification(state_machine->active_session(),
                    BgpProto::Notification::Cease,
                    BgpProto::Notification::ConnectionCollision,
                    "Connection collision - closing active session");
                state_machine->set_active_session(NULL);

                // If we haven't already sent an OPEN message on the passive
                // session, cancel the open timer and send the OPEN message.
                session = state_machine->passive_session();
                if (state_machine->OpenTimerRunning()) {
                    state_machine->CancelOpenTimer();
                    peer->SendOpen(session);
                }

                // If the OPEN was not received on the passive session, stay
                // in OpenSent and wait for the other end to send the OPEN on
                // on the passive session.
                // If the OPEN was received on the passive session, we assign
                // the passive session to the peer and fall through to go to
                // OpenConfirm.
                if (event.session != session) {
                    return discard_event();
                } else {
                    state_machine->AssignSession(false);
                }

            } else {

                // Active connection wins, close the passive session.
                peer->SendNotification(state_machine->passive_session(),
                    BgpProto::Notification::Cease,
                    BgpProto::Notification::ConnectionCollision,
                    "Connection collision - closing passive session");
                state_machine->set_passive_session(NULL);
                state_machine->CancelOpenTimer();

                // If the OPEN was not received on the active session, stay
                // in OpenSent and wait for the other end to send the OPEN on
                // on the active session.
                // If the OPEN was received on the active session, we assign
                // the active session to the peer and fall through to go to
                // OpenConfirm.
                session = state_machine->active_session();
                if (event.session != session) {
                    return discard_event();
                } else {
                    state_machine->AssignSession(true);
                }
            }

        } else if (state_machine->passive_session()) {

            // If the OPEN was not received on the passive session, stay
            // in OpenSent and wait for the other end to send the OPEN on
            // on the passive session.
            // If the OPEN was received on the passive session, we assign
            // the passive session to the peer and fall through to go to
            // OpenConfirm.
            session = state_machine->passive_session();
            if (event.session != session) {
                return discard_event();
            } else {
                state_machine->AssignSession(false);
            }

        } else if (state_machine->active_session()) {

            // If the OPEN was not received on the active session, stay
            // in OpenSent and wait for the other end to send the OPEN on
            // on the active session.
            // If the OPEN was received on the active session, we assign
            // the active session to the peer and fall through to go to
            // OpenConfirm.
            session = state_machine->active_session();
            if (event.session != session) {
                return discard_event();
            } else {
                state_machine->AssignSession(true);
            }
        }

        int local_holdtime = state_machine->GetConfiguredHoldTime();
        state_machine->set_hold_time(min(event.msg->holdtime, local_holdtime));
        peer->SetCapabilities(event.msg.get());
        return transit<OpenConfirm>();
    }

    // Notification received on one of the sessions for this state machine.
    sc::result react(const EvBgpNotification &event) {
        StateMachine *state_machine = &context<StateMachine>();

        // Ignore if the NOTIFICATION came in on a stale session.
        if (!state_machine->ProcessNotificationEvent(event.session))
            return discard_event();

        // The call to ProcessNotificationEvent above would have closed
        // the session on the which the message was received.
        if (state_machine->active_session()) {

            // Since we still have an active session, the passive session
            // has been closed, so we cancel the open timer.  We stay in
            // OpenSent since we still have an active session on which we
            // have already sent an OPEN message.
            state_machine->CancelOpenTimer();
            return discard_event();

        } else if (state_machine->passive_session()) {

            // Since we still have the passive session, the active session
            // has been closed.  If the open timer is still running, we go
            // back to Active because we don't have an active session now.
            // If the open timer has already expired, we stay in OpenSent
            // since we have sent an OPEN on the passive session.
            if (state_machine->OpenTimerRunning()) {
                return transit<Active>();
            } else {
                return discard_event();
            }

        } else {

            // We have neither an active or passive session.  Go to Idle.
            return transit<Idle, StateMachine, EvBgpNotification>(
                &StateMachine::OnIdle<EvBgpNotification, 0>, event);
        }
    }
};

//
// In OpenConfirm, we wait for the other end to send a KEEPALIVE.
//
struct OpenConfirm : sc::state<OpenConfirm, StateMachine> {
    typedef mpl::list<
        IdleCease<EvStop>::reaction,
        IdleFsmError<EvOpenTimerExpired>::reaction,
        TransitToIdle<EvHoldTimerExpired, BgpProto::Notification::HoldTimerExp>::reaction,
        sc::custom_reaction<EvTcpPassiveOpen>,
        TransitToIdle<EvTcpClose>::reaction,
        IdleFsmError<EvBgpOpen>::reaction,
        sc::custom_reaction<EvBgpNotification>,
        sc::custom_reaction<EvBgpKeepalive>,
        IdleFsmError<EvBgpUpdate>::reaction,
        IdleError<EvBgpHeaderError, BgpProto::Notification::MsgHdrErr>::reaction,
        IdleError<EvBgpOpenError, BgpProto::Notification::OpenMsgErr>::reaction,
        IdleError<EvBgpUpdateError, BgpProto::Notification::UpdateMsgErr>::reaction
    > reactions;

    // Send a KEEPALIVE and start the keepalive timer on the peer. Also start
    // the hold timer based on the negotiated hold time value.
    OpenConfirm(my_context ctx) : my_base(ctx) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        peer->SendKeepalive(false);
        peer->StartKeepaliveTimer();
        state_machine->CancelOpenTimer();
        state_machine->StartHoldTimer();
        state_machine->set_state(StateMachine::OPENCONFIRM);
    }

    // Cancel the hold timer.  If we go to Established, the timer will get
    // started again from the constructor for that state.
    ~OpenConfirm() {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->CancelHoldTimer();
    }

    // Send a notification, delete the new session and stay in OpenConfirm.
    sc::result react(const EvTcpPassiveOpen &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        BgpSession *session = event.session;
        peer->SendNotification(session,
            BgpProto::Notification::Cease,
            BgpProto::Notification::ConnectionRejected,
            "Connection rejected - unexpected passive session");
        state_machine->DeleteSession(session);
        return discard_event();
    }

    // Ignore the notification if it's for a stale session, else go to Idle.
    sc::result react(const EvBgpNotification &event) {
        StateMachine *state_machine = &context<StateMachine>();
        if (!state_machine->ProcessNotificationEvent(event.session))
            return discard_event();

        return transit<Idle, StateMachine, EvBgpNotification>(
            &StateMachine::OnIdle<EvBgpNotification, 0>, event);
    }

    // Go to Established.  The hold timer will be started in the constructor
    // for that state.
    sc::result react(const EvBgpKeepalive &event) {
        return transit<Established>();
    }
};

//
// Established is the final state for an operation peer.
//
struct Established : sc::state<Established, StateMachine> {
    typedef mpl::list<
        IdleCease<EvStop>::reaction,
        IdleFsmError<EvOpenTimerExpired>::reaction,
        TransitToIdle<EvHoldTimerExpired, BgpProto::Notification::HoldTimerExp>::reaction,
        sc::custom_reaction<EvTcpPassiveOpen>,
        TransitToIdle<EvTcpClose>::reaction,
        IdleFsmError<EvBgpOpen>::reaction,
        TransitToIdle<EvBgpNotification>::reaction,
        sc::custom_reaction<EvBgpKeepalive>,
        sc::custom_reaction<EvBgpUpdate>,
        IdleError<EvBgpHeaderError, BgpProto::Notification::MsgHdrErr>::reaction,
        IdleFsmError<EvBgpOpenError>::reaction,
        IdleError<EvBgpUpdateError, BgpProto::Notification::UpdateMsgErr>::reaction
    > reactions;

    Established(my_context ctx) : my_base(ctx) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        peer->server()->IncUpPeerCount();
        state_machine->connect_attempts_clear();
        state_machine->StartHoldTimer();
        state_machine->set_state(StateMachine::ESTABLISHED);
        peer->RegisterAllTables();
    }

    ~Established() {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        peer->server()->DecUpPeerCount();
        state_machine->CancelHoldTimer();
    }

    // A new TCP session request should cause the previous BGP session
    // to be closed, rather than ignoring the event.
    sc::result react(const EvTcpPassiveOpen &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpSession *session = event.session;
        state_machine->DeleteSession(session);
        return discard_event();
    }

    // Restart the hold timer.
    sc::result react(const EvBgpKeepalive &event) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->StartHoldTimer();
        return discard_event();
    }

    // Restart the hold timer and process the update.
    sc::result react(const EvBgpUpdate &event) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->StartHoldTimer();
        state_machine->peer()->ProcessUpdate(event.msg.get());
        return discard_event();
    }
};

}  // namespace fsm

StateMachine::StateMachine(BgpPeer *peer)
      : work_queue_(TaskScheduler::GetInstance()->GetTaskId("bgp::StateMachine"),
                    peer->GetIndex(),
                    boost::bind(&StateMachine::DequeueEvent, this, _1)),
        peer_(peer),
        active_session_(NULL),
        passive_session_(NULL),
        connect_timer_(TimerManager::CreateTimer(*peer->server()->ioservice(), 
            "Connect timer",
            TaskScheduler::GetInstance()->GetTaskId("bgp::StateMachine"),
            peer->GetIndex())),
        open_timer_(TimerManager::CreateTimer(*peer->server()->ioservice(), 
            "Open timer",
            TaskScheduler::GetInstance()->GetTaskId("bgp::StateMachine"),
            peer->GetIndex())),
        hold_timer_(TimerManager::CreateTimer(*peer->server()->ioservice(), 
            "Hold timer",
            TaskScheduler::GetInstance()->GetTaskId("bgp::StateMachine"),
            peer->GetIndex())),
        idle_hold_timer_(TimerManager::CreateTimer(*peer->server()->ioservice(), 
            "Idle hold timer",
            TaskScheduler::GetInstance()->GetTaskId("bgp::StateMachine"),
            peer->GetIndex())),
        hold_time_(GetConfiguredHoldTime()),
        idle_hold_time_(0),
        attempts_(0),
        state_(IDLE) {
    initiate();
}

void StateMachine::DeleteAllTimers() {
    TimerManager::DeleteTimer(connect_timer_);
    TimerManager::DeleteTimer(open_timer_);
    TimerManager::DeleteTimer(hold_timer_);
    TimerManager::DeleteTimer(idle_hold_timer_);
}

//
// Delete timers after state machine is terminated so that there is no
// possible reference to the timers being deleted any more
//
StateMachine::~StateMachine() {
    work_queue_.Shutdown();
    terminate();
    DeleteAllTimers();
}

void StateMachine::Initialize() {
    Enqueue(fsm::EvStart());
}

void StateMachine::Shutdown(int subcode) {
    if (peer_->IsDeleted()) {
        work_queue_.SetExitCallback(
            boost::bind(&StateMachine::DequeueEventDone, this, _1));
    }
    Enqueue(fsm::EvStop(subcode));
}

void StateMachine::SetAdminState(bool down) {
    if (down) {
        Enqueue(fsm::EvStop(BgpProto::Notification::AdminShutdown));
    } else {
        reset_idle_hold_time();
        peer_->reset_flap_count();
        // On fresh restart of state machine, all previous state should be reset
        reset_last_info();
        Enqueue(fsm::EvStart());
    }
}

bool StateMachine::IsQueueEmpty() const {
    return work_queue_.IsQueueEmpty();
}

template <typename Ev, int code>
void StateMachine::OnIdle(const Ev &event) {
    // Release all resources.
    SendNotificationAndClose(peer_->session(), code);
}

template <typename Ev>
void StateMachine::OnIdleCease(const Ev &event) {
    // Release all resources.
    SendNotificationAndClose(
        peer_->session(), BgpProto::Notification::Cease, event.subcode);
}

//
// The template below must only be called for EvBgpHeaderError, EvBgpOpenError
// or EvBgpUpdateError.
//
template <typename Ev, int code>
void StateMachine::OnIdleError(const Ev &event) {
    // Release all resources.
    SendNotificationAndClose(event.session, code, event.subcode, event.data);
}

void StateMachine::OnIdleNotification(const fsm::EvBgpNotification &event) {
    // Release all resources.
    SendNotificationAndClose(peer()->session(), 0);
    set_last_notification_in(event.msg->error, event.msg->subcode,
        event.Name());
}

int StateMachine::GetConnectTime() const {
    int backoff = min(attempts_, 6);
    return std::min(backoff ? 1 << (backoff - 1) : 0, kConnectInterval);
}

void StateMachine::StartConnectTimer(int seconds) {
    connect_timer_->Cancel();

    // Add up to +/- kJitter percentage to reduce connection collisions.
    int ms = seconds ? seconds * 1000 : 50;
    ms = (ms * (100 - kJitter))/ 100;
    ms += (ms *(rand() % (kJitter*2)))/100;
    connect_timer_->Start(ms,
        boost::bind(&StateMachine::ConnectTimerExpired, this),
        boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
}

void StateMachine::CancelConnectTimer() {
    connect_timer_->Cancel();
}

bool StateMachine::ConnectTimerRunning() {
    return connect_timer_->running();
}

void StateMachine::StartOpenTimer(int seconds) {
    open_timer_->Cancel();
    open_timer_->Start(seconds * 1000,
        boost::bind(&StateMachine::OpenTimerExpired, this),
        boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
}

void StateMachine::CancelOpenTimer() {
    open_timer_->Cancel();
}

bool StateMachine::OpenTimerRunning() {
    return open_timer_->running();
}

void StateMachine::StartIdleHoldTimer() {
    if (idle_hold_time_ <= 0)
        return;

    idle_hold_timer_->Cancel();
    idle_hold_timer_->Start(idle_hold_time_,
        boost::bind(&StateMachine::IdleHoldTimerExpired, this),
        boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
}

void StateMachine::CancelIdleHoldTimer() {
    idle_hold_timer_->Cancel();
}

bool StateMachine::IdleHoldTimerRunning() {
    return idle_hold_timer_->running();
}

void StateMachine::StartHoldTimer() {
    if (hold_time_ <= 0)
        return;

    hold_timer_->Cancel();
    hold_timer_->Start(hold_time_ * 1000,
        boost::bind(&StateMachine::HoldTimerExpired, this),
        boost::bind(&StateMachine::TimerErrorHanlder, this, _1, _2));
}

void StateMachine::CancelHoldTimer() {
    hold_timer_->Cancel();
}

bool StateMachine::HoldTimerRunning() {
    return hold_timer_->running();
}

// Test Only APIs : Start

void StateMachine::FireConnectTimer() {
    connect_timer_->Fire();
}

void StateMachine::FireOpenTimer() {
    open_timer_->Fire();
}

void StateMachine::FireHoldTimer() {
    hold_timer_->Fire();
}

void StateMachine::FireIdleHoldTimer() {
    idle_hold_timer_->Fire();
}

// Test Only APIs : END

//
// Create an active session.
//
void StateMachine::StartSession() {
    BgpSession *session = peer_->CreateSession();
    if (!session)
        return;
    set_active_session(session);
    session->set_observer(
        boost::bind(&StateMachine::OnSessionEvent, this, _1, _2));
    peer_->server()->session_manager()->Connect(session,
        peer_->peer_key().endpoint);
}

//
// Post a pseudo event to delete the underlying TcpSession.
//
// This ensures that any references to the TcpSession from pending events on
// the state machine queue are still valid. Since we remove the TCP observer
// before posting the delete event, we are guaranteed that we won't receive
// any more events on the TcpSession.
//
void StateMachine::DeleteSession(BgpSession *session) {
    if (!session)
        return;
    session->set_observer(NULL);
    session->Close();
    Enqueue(fsm::EvTcpDeleteSession(session));
}

//
// Transfer the ownership of the session from state machine to the peer.
// This is called after we have resolved any connection collision issues
// and decided that we want to reach ESTABLISHED state via the session.
//
void StateMachine::AssignSession(bool active) {
    if (active) {
        peer_->set_session(active_session_);
        active_session_ = NULL;
    } else {
        peer_->set_session(passive_session_);
        passive_session_ = NULL;
    }
}

void StateMachine::set_active_session(BgpSession *session) {
    DeleteSession(active_session_);
    active_session_ = session;
}

void StateMachine::set_passive_session(BgpSession *session) {
    DeleteSession(passive_session_);
    passive_session_ = session;
}

BgpSession *StateMachine::active_session() {
    return active_session_;
}

BgpSession *StateMachine::passive_session() {
    return passive_session_;
}

void StateMachine::SendNotificationAndClose(BgpSession *session, int code,
        int subcode, const std::string &data) {
    // Prefer the passive session if available since it's operational.
    if (!session)
        session = passive_session_;
    if (!session)
        session = active_session_;
    if (session && code != 0)
        peer_->SendNotification(session, code, subcode, data);

    set_idle_hold_time(idle_hold_time() ? idle_hold_time() : kIdleHoldTime);
    reset_hold_time();
    peer_->Close();
}

// Process notification message.
//
// Typically we close the session. However, during connection collisions, we
// could receive notifications on sessions that are not currently assigned to
// the peer. In such cases, we discard the event and let the state machine
// continue in the other session which is currently assigned to the peer.
bool StateMachine::ProcessNotificationEvent(BgpSession *session) {

    // If this is a notification event that does not belong to the session,
    // ignore. If either session is not present, continue normal processing
    // of the notification.
    if (session && peer_->session() && peer_->session() != session) {
        return false;
    }

    // TransitToIdle<EvBgpNotification>::reaction,
    if (active_session() == session) {
        set_active_session(NULL);
    } else {
        set_passive_session(NULL);
    }

    return true;
}

bool StateMachine::ConnectTimerExpired() {
    Enqueue(fsm::EvConnectTimerExpired(connect_timer_));
    return false;
}

bool StateMachine::OpenTimerExpired() {
    Enqueue(fsm::EvOpenTimerExpired(open_timer_));
    return false;
}

bool StateMachine::HoldTimerExpired() {
    boost::system::error_code error;

    // Reset hold timer if there is data already present in the socket.
    if (peer() && peer()->session() && peer()->session()->socket() &&
            peer()->session()->socket()->available(error) > 0) {
        return true;
    }
    Enqueue(fsm::EvHoldTimerExpired(hold_timer_));
    peer_->inc_hold_timer_expired();
    return false;
}

bool StateMachine::IdleHoldTimerExpired() {
    Enqueue(fsm::EvIdleHoldTimerExpired(idle_hold_timer_));
    return false;
}

//
// Concurrency: ASIO thread.
// Feed TCP session events into the state machine.
//
void StateMachine::OnSessionEvent(
        TcpSession *session, TcpSession::Event event) {
    BgpSession *bgp_session = static_cast<BgpSession *>(session);
    switch (event) {
    case TcpSession::CONNECT_COMPLETE:
        Enqueue(fsm::EvTcpConnected(bgp_session));
        break;
    case TcpSession::CONNECT_FAILED:
        Enqueue(fsm::EvTcpConnectFail(bgp_session));
        peer_->inc_connect_error();
        break;
    case TcpSession::CLOSE:
        Enqueue(fsm::EvTcpClose(bgp_session));
        break;
    default:
        break;
    }
}

//
// Receive TCP Passive Open.
// Set the observer and start async read on the session. Note that we disable
// read on connect when we accept passive sessions.
//
bool StateMachine::PassiveOpen(BgpSession *session) {
    CHECK_CONCURRENCY("bgp::Config");
    Enqueue(fsm::EvTcpPassiveOpen(session));
    session->set_observer(boost::bind(&StateMachine::OnSessionEvent,
        this, _1, _2));
    session->AsyncReadStart();
    return true;
}

//
// Handle incoming message on the session.
//
void StateMachine::OnMessage(BgpSession *session, BgpProto::BgpMessage *msg) {
    switch (msg->type) {
    case BgpProto::OPEN: {
        BgpProto::OpenMessage *open_msg =
            static_cast<BgpProto::OpenMessage *>(msg);
        BgpPeer *peer = session->Peer();
        peer->inc_rx_open();
        if (int subcode = open_msg->Validate(peer)) {
            Enqueue(fsm::EvBgpOpenError(session, subcode));
            peer->inc_open_error();
        } else {
            Enqueue(fsm::EvBgpOpen(session, open_msg));
            msg = NULL;
        }
        break;
    }
    case BgpProto::KEEPALIVE: {
        BgpPeer *peer = session->Peer();
        Enqueue(fsm::EvBgpKeepalive(session));
        if (peer) peer->inc_rx_keepalive();
        break;
    }
    case BgpProto::NOTIFICATION: {
        BgpPeer *peer = session->Peer();
        if (peer)
            peer->inc_rx_notification();
        Enqueue(fsm::EvBgpNotification(session,
                static_cast<BgpProto::Notification *>(msg)));
        msg = NULL;
        break;
    }
    case BgpProto::UPDATE: {
        BgpProto::Update *update = static_cast<BgpProto::Update *>(msg);
        BgpPeer *peer = NULL;
        if (session) 
            peer = session->Peer();
        if (peer)
            peer->inc_rx_update();

        std::string data;
        int subcode;
        if (peer && (subcode = update->Validate(peer, data))) {
            Enqueue(fsm::EvBgpUpdateError(session, subcode, data));
            peer->inc_update_error();
        } else {
            Enqueue(fsm::EvBgpUpdate(session, update));
            msg = NULL;
        }
        break;
    }
    default:
        SM_LOG(SandeshLevel::SYS_NOTICE, "Unknown message type " << msg->type);
        break;
    }

    delete msg;
}

//
// Handle errors in incoming message on the session.
//
void StateMachine::OnMessageError(BgpSession *session,
        const ParseErrorContext *context) {
    switch (context->error_code) {
    case BgpProto::Notification::MsgHdrErr: {
        Enqueue(fsm::EvBgpHeaderError(session, context->error_subcode,
                context->data, context->data_size));
        break;
    }
    case BgpProto::Notification::OpenMsgErr:
        Enqueue(fsm::EvBgpOpenError(session, context->error_subcode,
                context->data, context->data_size));
        break;
    case BgpProto::Notification::UpdateMsgErr:
        Enqueue(fsm::EvBgpUpdateError(session, context->error_subcode,
                std::string((const char *)context->data, context->data_size)));
        break;
    default:
        break;
    }
}

static const std::string state_names[] = {
    "Idle",
    "Active",
    "Connect",
    "OpenSent",
    "OpenConfirm",
    "Established"
};

const string &StateMachine::StateName() const {
    return state_names[state_];
}

const string &StateMachine::LastStateName() const {
    return state_names[last_state_];
}

const std::string StateMachine::last_state_change_at() const {
    return integerToString(UTCUsecToPTime(last_state_change_at_));
}

ostream &operator<<(ostream &out, const StateMachine::State &state) {
    out << state_names[state];
    return out;
}

// This class determines whether a given class has a method called 'validate'.
template <typename Ev>
struct HasValidate {
    template <typename T, bool (T::*)(StateMachine *) const> struct SFINAE {};
    template <typename T> static char Test(SFINAE<T, &T::validate>*);
    template <typename T> static int Test(...);
    static const bool Has = sizeof(Test<Ev>(0)) == sizeof(char);
};

template <typename Ev, bool has_validate>
struct ValidateFn {
    EvValidate operator()(const Ev *event) {
        return NULL;
    }
};

template <typename Ev>
struct ValidateFn<Ev, true> {
    EvValidate operator()(const Ev *event) {
        return boost::bind(&Ev::validate, event, _1);
    }
};

template <typename Ev>
bool StateMachine::Enqueue(const Ev &event) {
    LogEvent(TYPE_NAME(event), "Enqueue");
    EventContainer ec;
    ec.event = event.intrusive_from_this();
    ec.validate = ValidateFn<Ev, HasValidate<Ev>::Has>()(
        static_cast<const Ev *>(ec.event.get()));
    work_queue_.Enqueue(ec);

    return true;
}

void StateMachine::LogEvent(string event_name, string msg,
                            SandeshLevel::type log_level) {

    // Reduce log level for keepalive and update messages.
    if (get_state() == ESTABLISHED &&
        (event_name == "fsm::EvBgpKeepalive" ||
             event_name == "fsm::EvBgpUpdate")) {
        log_level = Sandesh::LoggingUtLevel();
    }
    SM_LOG(log_level, msg << " " << event_name << " in state " << StateName());
}

bool StateMachine::DequeueEvent(StateMachine::EventContainer ec) {
    const fsm::EvTcpDeleteSession *deferred_delete =
        dynamic_cast<const fsm::EvTcpDeleteSession *>(ec.event.get());
    if (deferred_delete != NULL) {
        LogEvent(TYPE_NAME(*ec.event), "Dequeue");
        peer_->server()->session_manager()->DeleteSession(
            deferred_delete->session);
        return true;
    }

    set_last_event(TYPE_NAME(*ec.event));
    if (ec.validate.empty() || ec.validate(this)) {
        LogEvent(TYPE_NAME(*ec.event), "Dequeue");
        process_event(*ec.event);
    } else {
        LogEvent(TYPE_NAME(*ec.event), "Discard", SandeshLevel::SYS_INFO);
    }
    ec.event.reset();

    return true;
}

void StateMachine::DequeueEventDone(bool done) {
    peer_->RetryDelete();
}

void StateMachine::SetDataCollectionKey(BgpPeerInfo *peer_info) const {
    peer_->SetDataCollectionKey(peer_info);
}

const std::string StateMachine::last_notification_in_error() const {
    return (BgpProto::Notification::toString(
        static_cast<BgpProto::Notification::Code>(last_notification_in_.first),
        last_notification_in_.second));
}

const std::string StateMachine::last_notification_out_error() const {
    return (BgpProto::Notification::toString(
        static_cast<BgpProto::Notification::Code>(last_notification_out_.first),
        last_notification_out_.second));
}

//
// Return the configured hold time in seconds.
//
int StateMachine::GetConfiguredHoldTime() const {
    static tbb::atomic<bool> env_checked = tbb::atomic<bool>();
    static tbb::atomic<int> env_hold_time = tbb::atomic<int>();

    // For testing only - configure through environment variable.
    if (!env_checked) {
        char *keepalive_time_str = getenv("BGP_KEEPALIVE_SECONDS");
        if (keepalive_time_str) {
            env_hold_time = strtoul(keepalive_time_str, NULL, 0) * 3;
            env_checked = true;
            return env_hold_time;
        } else {
            env_checked = true;
        }
    } else if (env_hold_time) {
        return env_hold_time;
    }

    // Use the configured hold-time if available.
    if (peer_ && peer_->server()->hold_time())
        return peer_->server()->hold_time();

    // Use hard coded default.
    return kHoldTime;
}

void StateMachine::set_last_event(const std::string &event) { 
    last_event_ = event; 
    last_event_at_ = UTCTimestampUsec(); 

    // Don't log keepalive events.
    if (event == "fsm::EvBgpKeepalive")
	    return;

    BgpPeerInfoData peer_info;
    peer_info.set_name(peer()->ToUVEKey());
    PeerEventInfo event_info;
    event_info.set_last_event(last_event_);
    event_info.set_last_event_at(last_event_at_);
    peer_info.set_event_info(event_info);
    BGPPeerInfo::Send(peer_info);
}

void StateMachine::set_last_notification_out(int code, int subcode,
    const string &reason) {
    last_notification_out_ = std::make_pair(code, subcode);
    last_notification_out_at_ = UTCTimestampUsec();
    last_notification_out_error_ = reason;

    BgpPeerInfoData peer_info;
    peer_info.set_name(peer()->ToUVEKey());
    peer_info.set_notification_out_at(last_notification_out_at_);
    peer_info.set_notification_out(BgpProto::Notification::toString(
        static_cast<BgpProto::Notification::Code>(code), subcode));
    BGPPeerInfo::Send(peer_info);
}

void StateMachine::set_last_notification_in(int code, int subcode,
    const string &reason) {
    last_notification_in_ = std::make_pair(code, subcode);
    last_notification_in_at_ = UTCTimestampUsec();
    last_notification_in_error_ = reason;

    BgpPeerInfoData peer_info;
    peer_info.set_name(peer()->ToUVEKey());
    peer_info.set_notification_in_at(last_notification_in_at_);
    peer_info.set_notification_in(BgpProto::Notification::toString(
        static_cast<BgpProto::Notification::Code>(code), subcode));
    BGPPeerInfo::Send(peer_info);
}

void StateMachine::set_state(State state) { 
    last_state_ = state_; state_ = state; 
    last_state_change_at_ = UTCTimestampUsec();

    BgpPeerInfoData peer_info;
    peer_info.set_name(peer()->ToUVEKey());
    PeerStateInfo state_info;
    state_info.set_state(StateName());
    state_info.set_last_state(LastStateName());
    state_info.set_last_state_at(last_state_change_at_);
    peer_info.set_state_info(state_info);
    BGPPeerInfo::Send(peer_info);
}

void StateMachine::set_hold_time(int hold_time) { 
    hold_time_ = hold_time; 

    BgpPeerInfoData peer_info;
    peer_info.set_name(peer()->ToUVEKey());
    peer_info.set_hold_time(hold_time_);
    BGPPeerInfo::Send(peer_info);
}

void StateMachine::reset_hold_time() { 
    hold_time_ = GetConfiguredHoldTime();

    BgpPeerInfoData peer_info;
    peer_info.set_name(peer()->ToUVEKey());
    peer_info.set_hold_time(hold_time_);
    BGPPeerInfo::Send(peer_info);
}

void StateMachine::reset_last_info() {
    last_notification_in_ = std::make_pair(0,0);
    last_notification_in_at_ = 0;
    last_notification_in_error_ = std::string();
    last_notification_out_ = std::make_pair(0,0);
    last_notification_out_at_ = 0;
    last_notification_out_error_ = std::string();
    last_state_ = IDLE;
    last_event_ = "";
    last_state_change_at_ = 0;
    last_event_at_ = 0;

    BgpPeerInfoData peer_info;
    peer_info.set_name(peer()->ToUVEKey());
    PeerStateInfo state_info;
    state_info.set_state(StateName());
    state_info.set_last_state(LastStateName());
    state_info.set_last_state_at(last_state_change_at_);
    peer_info.set_state_info(state_info);

    PeerEventInfo event_info;
    event_info.set_last_event(last_event_);
    event_info.set_last_event_at(last_event_at_);
    peer_info.set_event_info(event_info);
    BGPPeerInfo::Send(peer_info);
}
