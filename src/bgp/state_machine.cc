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

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>

#include "base/logging.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session.h"
#include "bgp/bgp_session_manager.h"

using boost::system::error_code;
using namespace std;

namespace mpl = boost::mpl;
namespace sc = boost::statechart;

#define SM_LOG(level, _Msg)                                                  \
    do {                                                                     \
        StateMachine *_Xstate_machine = &context<StateMachine>();            \
        BgpPeer *_Xpeer = _Xstate_machine->peer();                           \
        std::ostringstream out;                                              \
        out << _Msg;                                                         \
        BGP_LOG_SERVER(_Xpeer, (BgpTable *) 0);                              \
        BGP_LOG(BgpStateMachineSessionMessage, level,                        \
                BGP_LOG_FLAG_SYSLOG, _Xpeer->ToString(), out.str());         \
    } while (false)

#define SESSION_LOG(session)                                                  \
do {                                                                          \
    std::ostringstream out;                                                   \
    out << "Enqueue event " << Name();                                        \
    BGP_LOG_SERVER((session) ? (session)->Peer() : (IPeer *)0, (BgpTable *)0);\
    BGP_LOG(BgpStateMachineSessionMessage, SandeshLevel::UT_DEBUG,            \
            BGP_LOG_FLAG_SYSLOG,                                              \
            (session) && (session)->Peer() ? (session)->Peer()->ToString():"",\
            out.str());                                                       \
} while (false)

namespace fsm {

// Events
struct EvStart : sc::event<EvStart> {
    EvStart() {
        BGP_LOG(BgpStateMachineMessage, SandeshLevel::SYS_DEBUG,
            BGP_LOG_FLAG_TRACE, Name());
    }
    static const char *Name() {
        return "EvStart";
    }
};

struct EvStop : sc::event<EvStop> {
    EvStop() {
        BGP_LOG(BgpStateMachineMessage, SandeshLevel::SYS_DEBUG,
                BGP_LOG_FLAG_TRACE, Name());
    }
    static const char *Name() {
        return "EvStop";
    }
};

struct EvIdleHoldTimerExpired : sc::event<EvIdleHoldTimerExpired> {
    EvIdleHoldTimerExpired(Timer *timer)  : timer_(timer){
        BGP_LOG(BgpStateMachineMessage, SandeshLevel::SYS_DEBUG,
                BGP_LOG_FLAG_TRACE, Name());
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
        BGP_LOG(BgpStateMachineMessage, SandeshLevel::SYS_DEBUG,
                BGP_LOG_FLAG_TRACE, Name());
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
        BGP_LOG(BgpStateMachineMessage, SandeshLevel::SYS_DEBUG,
                BGP_LOG_FLAG_TRACE, Name());
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
        BGP_LOG(BgpStateMachineMessage, SandeshLevel::SYS_DEBUG,
                BGP_LOG_FLAG_TRACE, Name());
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
        SESSION_LOG(session);
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
        SESSION_LOG(session);
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
        SESSION_LOG(session);
    }
    static const char *Name() {
        return "EvTcpPassiveOpen";
    }

    BgpSession *session;
};

struct EvTcpClose : sc::event<EvTcpClose> {
    EvTcpClose(BgpSession *session) : session(session) {
        SESSION_LOG(session);
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
        SESSION_LOG(session);
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
        SESSION_LOG(session);
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
        SESSION_LOG(session);
    }
    static const char *Name() {
        return "EvBgpOpen";
    }

    BgpSession *session;
    boost::shared_ptr<const BgpProto::OpenMessage> msg;
};

struct EvBgpOpenError : sc::event<EvBgpOpenError> {
    EvBgpOpenError(BgpSession *session, int subcode,
        const uint8_t *_data = NULL, size_t data_size = 0)
        : session(session), subcode(subcode) {
        SESSION_LOG(session);
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
        SESSION_LOG(session);
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
        BGP_LOG(BgpNotificationMessage, SandeshLevel::SYS_NOTICE,
                BGP_LOG_FLAG_ALL,
                session->Peer() ? session->Peer()->ToString() : "",
                BGP_PEER_DIR_IN, this->msg->data.size(),
                this->msg->error, this->msg->subcode,
                BgpProto::Notification::ToString(
                BgpProto::Notification::Code(this->msg->error),
                this->msg->subcode));
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
        SESSION_LOG(session);
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
        SESSION_LOG(session);
    }
    static const char *Name() {
        return "EvBgpUpdateError";
    }

    BgpSession *session;
    int subcode;
    std::string data;
};

// states
struct Idle;
struct Active;
struct Connect;
struct OpenSent;
struct OpenConfirm;
struct Established;

template <class Ev, int code = 0>
struct TransitToIdle {
    typedef sc::transition<Ev, Idle, StateMachine,
            &StateMachine::OnIdle<Ev, code> > reaction;
};

template <>
struct TransitToIdle<EvBgpNotification, 0> {
    typedef sc::transition<EvBgpNotification, Idle, StateMachine,
            &StateMachine::OnIdleNotification> reaction;
};

template <class Ev>
struct IdleCease {
    typedef sc::transition<Ev, Idle, StateMachine,
            &StateMachine::OnIdle<Ev, BgpProto::Notification::Cease> > reaction;
};

template<class Ev>
struct IdleFsmError {
    typedef sc::transition<Ev, Idle, StateMachine,
            &StateMachine::OnIdle<Ev, BgpProto::Notification::FSMErr> > reaction;
};

template <class Ev, int code>
struct IdleError {
    typedef sc::transition<Ev, Idle, StateMachine,
            &StateMachine::OnIdleError<Ev, code> > reaction;
};

struct Idle : sc::state<Idle, StateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvStart>,
        sc::custom_reaction<EvStop>,
        sc::custom_reaction<EvTcpPassiveOpen>,
        sc::custom_reaction<EvIdleHoldTimerExpired>
    > reactions;

    Idle(my_context ctx) : my_base(ctx) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        BgpSession *session = peer->session();
        peer->clear_session();
        state_machine->set_state(StateMachine::IDLE);
        state_machine->set_active_session(NULL);
        state_machine->set_passive_session(NULL);
        state_machine->DeleteSession(session);
        state_machine->CancelOpenTimer();
        state_machine->CancelIdleHoldTimer();
    }

    ~Idle() {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->CancelIdleHoldTimer();
    }

    sc::result react(const EvStart &event) {
        StateMachine *state_machine = &context<StateMachine>();
        if (state_machine->idle_hold_time())
            state_machine->StartIdleHoldTimer();
        else
            return transit<Active>();
        return discard_event();
    }

    sc::result react(const EvStop &event) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->CancelIdleHoldTimer();
        state_machine->peer()->Close();
        return discard_event();
    }

    sc::result react(const EvIdleHoldTimerExpired &event) {
        return transit<Active>();
    }

    // Close the session and ignore event
    sc::result react(const EvTcpPassiveOpen &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpSession *session = event.session;
        state_machine->DeleteSession(session);
        return discard_event();
    }
};

struct Active : sc::state<Active, StateMachine> {
    typedef mpl::list<
        TransitToIdle<EvStop>::reaction,
        TransitToIdle<EvBgpNotification>::reaction,
        TransitToIdle<EvBgpKeepalive>::reaction,
        TransitToIdle<EvBgpUpdate>::reaction,
        IdleError<EvBgpOpenError, BgpProto::Notification::OpenMsgErr>::reaction,
        IdleError<EvBgpHeaderError, BgpProto::Notification::MsgHdrErr>::reaction,
        IdleError<EvBgpUpdateError, BgpProto::Notification::UpdateMsgErr>::reaction,
        sc::custom_reaction<EvConnectTimerExpired>,
        sc::custom_reaction<EvOpenTimerExpired>,
        sc::custom_reaction<EvTcpPassiveOpen>,
        sc::custom_reaction<EvTcpClose>,
        sc::custom_reaction<EvBgpOpen>
    > reactions;

    Active(my_context ctx) : my_base(ctx) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->set_state(StateMachine::ACTIVE);
        if (state_machine->passive_session() == NULL)
            state_machine->StartConnectTimer(state_machine->GetConnectTime());
    }

    ~Active() {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->CancelConnectTimer();
    }

    sc::result react(const EvTcpPassiveOpen &event) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->set_passive_session(event.session);
        state_machine->CancelConnectTimer();
        state_machine->StartOpenTimer(StateMachine::kOpenTime);
        return discard_event();
    }

    sc::result react(const EvTcpClose &event) {
        StateMachine *state_machine = &context<StateMachine>();
        if (event.session == state_machine->passive_session()) {
            state_machine->set_passive_session(NULL);
            state_machine->CancelOpenTimer();
            state_machine->StartConnectTimer(state_machine->GetConnectTime());
        }
        return discard_event();
    }

    sc::result react(const EvConnectTimerExpired &event) {
        return transit<Connect>();
    }

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

        int local_holdtime = state_machine->GetDefaultHoldTime();
        state_machine->set_hold_time(min(event.msg->holdtime, local_holdtime));
        state_machine->AssignSession(false);
        peer->SendOpen(session);
        peer->SetCapabilities(event.msg.get());
        return transit<OpenConfirm>();
    }
};

struct Connect : sc::state<Connect, StateMachine> {
    typedef mpl::list<
        TransitToIdle<EvStop>::reaction,
        sc::custom_reaction<EvConnectTimerExpired>,
        sc::custom_reaction<EvOpenTimerExpired>,
        sc::custom_reaction<EvTcpConnected>,
        sc::custom_reaction<EvTcpConnectFail>,
        sc::custom_reaction<EvTcpPassiveOpen>,
        sc::custom_reaction<EvTcpClose>,
        sc::custom_reaction<EvBgpOpen>,
        IdleError<EvBgpOpenError, BgpProto::Notification::OpenMsgErr>::reaction,
        IdleError<EvBgpHeaderError, BgpProto::Notification::MsgHdrErr>::reaction,
        IdleError<EvBgpUpdateError, BgpProto::Notification::UpdateMsgErr>::reaction,
        TransitToIdle<EvBgpNotification>::reaction,
        TransitToIdle<EvBgpKeepalive>::reaction,
        TransitToIdle<EvBgpUpdate>::reaction
    > reactions;

    static const int kConnectTimeout = 60;  // seconds

    Connect(my_context ctx) : my_base(ctx) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->set_state(StateMachine::CONNECT);
        state_machine->connect_attempts_inc();
        state_machine->StartConnectTimer(state_machine->GetConnectTime());
        if (!StartSession(state_machine))
            return;
    }

    ~Connect() {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->CancelConnectTimer();
    }

    sc::result react(const EvConnectTimerExpired &event) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->set_active_session(NULL);
        return transit<Active>();
    }

    sc::result react(const EvOpenTimerExpired &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        peer->SendOpen(state_machine->passive_session());
        state_machine->set_active_session(NULL);
        return transit<OpenSent>();
    }

    sc::result react(const EvTcpConnected &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        BgpSession *session = state_machine->active_session();
        peer->SendOpen(session);
        return transit<OpenSent>();
    }

    sc::result react(const EvTcpConnectFail &event) {
        // delete session; restart connect timer.
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->set_active_session(NULL);
        return transit<Active>();
    }

    sc::result react(const EvTcpPassiveOpen &event) {
        // Connection collision is resolved by looking at the router ids
        // in the open message. The active connection may have been already
        // established (and the notification in flight).
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->set_passive_session(event.session);
        state_machine->StartOpenTimer(StateMachine::kOpenTime);
        return discard_event();
    }

    sc::result react(const EvTcpClose &event) {
        StateMachine *state_machine = &context<StateMachine>();
        if (event.session == state_machine->passive_session()) {
            state_machine->set_passive_session(NULL);
            state_machine->CancelOpenTimer();
            return discard_event();
        }
        assert(event.session == state_machine->active_session());
        state_machine->set_active_session(NULL);
        return transit<Active>();
    }

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

        int local_holdtime = state_machine->GetDefaultHoldTime();
        state_machine->set_hold_time(min(event.msg->holdtime, local_holdtime));
        state_machine->set_active_session(NULL);
        state_machine->AssignSession(false);
        peer->SendOpen(session);
        peer->SetCapabilities(event.msg.get());
        return transit<OpenConfirm>();
    }

    // Create an active connection request.
    bool StartSession(StateMachine *state_machine) {
        BgpPeer *peer = state_machine->peer();
        BgpSession *session = peer->CreateSession();
        if (session == NULL) {
            return false;
        }
        state_machine->set_active_session(session);
        session->set_observer(boost::bind(&StateMachine::OnSessionEvent,
                                          state_machine, _1, _2));
        peer->server()->session_manager()->Connect(
                    session, peer->peer_key().endpoint);
        return true;
    }
};

// The peer reaches OpenSent after sending an immediate OPEN on a active
// connection or a delayed OPEN on a passive connection. In the first case
// there may be both a passive and active session. In the later case there
// is only a passive connection active.
struct OpenSent : sc::state<OpenSent, StateMachine> {
    typedef mpl::list<
        IdleCease<EvStop>::reaction,
        sc::custom_reaction<EvTcpPassiveOpen>,
        sc::custom_reaction<EvTcpClose>,
        sc::custom_reaction<EvBgpOpen>,
        TransitToIdle<EvHoldTimerExpired, BgpProto::Notification::HoldTimerExp>::reaction,
        IdleError<EvBgpOpenError, BgpProto::Notification::OpenMsgErr>::reaction,
        IdleError<EvBgpHeaderError, BgpProto::Notification::MsgHdrErr>::reaction,
        IdleError<EvBgpUpdateError, BgpProto::Notification::UpdateMsgErr>::reaction,
        IdleFsmError<EvBgpKeepalive>::reaction,
        sc::custom_reaction<EvBgpNotification>,
        sc::custom_reaction<EvOpenTimerExpired>,
        IdleFsmError<EvBgpUpdate>::reaction
    > reactions;

    OpenSent(my_context ctx) : my_base(ctx) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->set_state(StateMachine::OPENSENT);
        state_machine->set_hold_time(StateMachine::kOpenSentHoldTime);
        state_machine->StartHoldTimer();
    }

    ~OpenSent() {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->CancelHoldTimer();
    }

    sc::result react(const EvBgpNotification &event) {
        StateMachine *state_machine = &context<StateMachine>();
        if (state_machine->ProcessNotificationEvent(event.session)) {
            if (state_machine->active_session()) {
                state_machine->CancelOpenTimer();
                return discard_event();
            } else if (state_machine->passive_session()) {
                if (state_machine->OpenTimerRunning()) {
                    return transit<Active>();
                } else {
                    return discard_event();
                }
            } else {
                return transit<Idle, StateMachine, EvBgpNotification>
                       (&StateMachine::OnIdle<EvBgpNotification, 0>, event);
            }
        }
        return discard_event();
    }

    sc::result react(const EvTcpPassiveOpen &event) {
        // Connection collision is resolved by looking at the router ids
        // in the open message. The active connection may have been already
        // established (and the notification in flight).
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->set_passive_session(event.session);
        state_machine->StartOpenTimer(StateMachine::kOpenTime);
        if (state_machine->active_session() == NULL) {
            return transit<Active>();
        } else {
            return discard_event();
        }
    }

    sc::result react(const EvOpenTimerExpired &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        peer->SendOpen(state_machine->passive_session());
        return discard_event();
    }

    sc::result react(const EvTcpClose &event) {
        StateMachine *state_machine = &context<StateMachine>();
        assert(event.session == state_machine->active_session() ||
                event.session == state_machine->passive_session());

        if (event.session == state_machine->active_session()) {
            state_machine->set_active_session(NULL);
            if (state_machine->passive_session() == NULL ||
                state_machine->OpenTimerRunning()) {
                return transit<Active>();
            }
        } else {
            state_machine->set_passive_session(NULL);
            state_machine->CancelOpenTimer();
            if (state_machine->active_session() == NULL) {
                return transit<Active>();
            }
        }
        return discard_event();
    }

    sc::result react(const EvBgpOpen &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        BgpSession *session = NULL;

        // Connection collision occurs when Open message is rxed with 
        // active session still open.
        // There are two paths to reach OpenSent from Connect state.
        // 1. As a response to OpenTimerExpired event. 
        //    In this case the active session is closed
        // 2. As a result of TcpConnect event(successful connect to Bgp Server)
        //    In this case the active session is left open
        if (state_machine->passive_session() &&
            state_machine->active_session()) {
            // Connection collision
            uint32_t local_bgp_id = peer->server()->bgp_identifier();
            if (event.msg->identifier > local_bgp_id) {
                // Passive connection wins, close the active session.
                peer->SendNotification(state_machine->active_session(),
                          BgpProto::Notification::Cease,
                          BgpProto::Notification::ConnectionCollision,
                          "Connection collision - closing active session");
                state_machine->set_active_session(NULL);

                session = state_machine->passive_session();

                if (state_machine->OpenTimerRunning()) {
                    state_machine->CancelOpenTimer();
                    peer->SendOpen(session);
                }

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

                session = state_machine->active_session();
                state_machine->CancelOpenTimer();

                if (event.session != session) {
                    return discard_event();
                } else {
                    state_machine->AssignSession(true);
                }
            }
        } else if (state_machine->passive_session()) {
            session = state_machine->passive_session();
            if (event.session != session) {
                return discard_event();
            }

            state_machine->AssignSession(false);
        } else if (state_machine->active_session()) {
            session = state_machine->active_session();
            if (event.session != session) {
                return discard_event();
            }

            state_machine->AssignSession(true);
        }

        int local_holdtime = state_machine->GetDefaultHoldTime();
        state_machine->set_hold_time(min(event.msg->holdtime, local_holdtime));
        peer->SetCapabilities(event.msg.get());
        return transit<OpenConfirm>();
    }
};

struct OpenConfirm : sc::state<OpenConfirm, StateMachine> {
    typedef mpl::list<
        IdleCease<EvStop>::reaction,
        IdleFsmError<EvBgpOpen>::reaction,
        TransitToIdle<EvHoldTimerExpired, BgpProto::Notification::HoldTimerExp>::reaction,
        sc::custom_reaction<EvBgpKeepalive>,
        sc::custom_reaction<EvTcpPassiveOpen>,
        TransitToIdle<EvTcpClose>::reaction,
        IdleError<EvBgpOpenError, BgpProto::Notification::OpenMsgErr>::reaction,
        IdleError<EvBgpHeaderError, BgpProto::Notification::MsgHdrErr>::reaction,
        IdleError<EvBgpUpdateError, BgpProto::Notification::UpdateMsgErr>::reaction,
        sc::custom_reaction<EvBgpNotification>,
        IdleFsmError<EvOpenTimerExpired>::reaction,
        IdleFsmError<EvBgpUpdate>::reaction
    > reactions;

    OpenConfirm(my_context ctx) : my_base(ctx) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        peer->SendKeepalive(false);
        peer->StartKeepaliveTimer();
        state_machine->set_state(StateMachine::OPENCONFIRM);
        state_machine->CancelOpenTimer();
        state_machine->StartHoldTimer();
    }

    ~OpenConfirm() {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->CancelHoldTimer();
    }

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

    sc::result react(const EvBgpNotification &event) {
        StateMachine *state_machine = &context<StateMachine>();
        if (state_machine->ProcessNotificationEvent(event.session)) {
            return transit<Idle, StateMachine, EvBgpNotification>
                       (&StateMachine::OnIdle<EvBgpNotification, 0>, event);
        }
        return discard_event();
    }

    sc::result react(const EvBgpKeepalive &event) {
        return transit<Established>();
    }
};

struct Established : sc::state<Established, StateMachine> {
    typedef mpl::list<
        IdleCease<EvStop>::reaction,
        TransitToIdle<EvHoldTimerExpired, BgpProto::Notification::HoldTimerExp>::reaction,
        sc::custom_reaction<EvBgpKeepalive>,
        TransitToIdle<EvBgpNotification>::reaction,
        sc::custom_reaction<EvBgpUpdate>,
        IdleFsmError<EvOpenTimerExpired>::reaction,
        IdleFsmError<EvBgpOpen>::reaction,
        IdleFsmError<EvBgpOpenError>::reaction,
        IdleError<EvBgpHeaderError, BgpProto::Notification::MsgHdrErr>::reaction,
        IdleError<EvBgpUpdateError, BgpProto::Notification::UpdateMsgErr>::reaction,
        TransitToIdle<EvTcpClose>::reaction,
        sc::custom_reaction<EvTcpPassiveOpen>
    > reactions;

    Established(my_context ctx) : my_base(ctx) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        peer->RegisterAllTables();
        peer->server()->IncUpPeerCount();
        state_machine->set_state(StateMachine::ESTABLISHED);
        state_machine->connect_attempts_clear();
        state_machine->StartHoldTimer();
    }

    ~Established() {
        StateMachine *state_machine = &context<StateMachine>();
        BgpPeer *peer = state_machine->peer();
        peer->server()->DecUpPeerCount();
        state_machine->CancelHoldTimer();
    }

    sc::result react(const EvBgpKeepalive &event) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->StartHoldTimer();
        return discard_event();
    }

    sc::result react(const EvBgpUpdate &event) {
        StateMachine *state_machine = &context<StateMachine>();
        state_machine->StartHoldTimer();
        state_machine->peer()->ProcessUpdate(event.msg.get());
        return discard_event();
    }

    // A new TCP session request should cause the previous BGP session
    // to be closed, rather than ignoring the event.
    sc::result react(const EvTcpPassiveOpen &event) {
        StateMachine *state_machine = &context<StateMachine>();
        BgpSession *session = event.session;
        state_machine->DeleteSession(session);
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
        hold_time_(GetDefaultHoldTime()),
        idle_hold_time_(0),
        attempts_(0),
        deleted_(false),
        state_(IDLE) {
    initiate();
}

void StateMachine::DeleteAllTimers() {
    TimerManager::DeleteTimer(connect_timer_);
    TimerManager::DeleteTimer(open_timer_);
    TimerManager::DeleteTimer(hold_timer_);
    TimerManager::DeleteTimer(idle_hold_timer_);
}

StateMachine::~StateMachine() {
    deleted_ = true;
    work_queue_.Shutdown();

    terminate();

    //
    // Delete timer after state machine is terminated so that there is no
    // possible reference to the timers being deleted any more
    //
    DeleteAllTimers();
}

void StateMachine::Initialize() {
    Enqueue(fsm::EvStart());
}

void StateMachine::Shutdown(void) {
    Enqueue(fsm::EvStop());
}

void StateMachine::SetAdminState(bool down) {
    if (down) {
        Enqueue(fsm::EvStop());
    } else {
        reset_idle_hold_time();
        peer_->reset_flap_count();
        // On fresh restart of state machine, all previous state should be reset
        reset_last_info();
        Enqueue(fsm::EvStart());
    }
}

void StateMachine::StartConnectTimer(int seconds) {
    connect_timer_->Cancel();

    // Add some jitter upto +-kJitter percentage to reduce connection collisions.
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

// 
// Test Only API : Start
//
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


//
// Test Only API : END
//

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

void StateMachine::DeleteSession(BgpSession *session) {
    if (!session)
        return;
    session->set_observer(NULL);
    session->Close();
    Enqueue(fsm::EvTcpDeleteSession(session));
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

template <class Ev, int code>
void StateMachine::OnIdle(const Ev &event) {
    // Release all resources
    SendNotificationAndClose(peer()->session(), code);
    // Collect before changing state

    bool flap = (state_ == ESTABLISHED);
    if (flap) peer()->increment_flap_count();
    set_state(IDLE);
}

void StateMachine::OnIdleNotification(const fsm::EvBgpNotification &event) {
    // Release all resources
    SendNotificationAndClose(peer()->session(), 0);
    // Collect before changing state
    bool flap = (state_ == ESTABLISHED);
    if (flap) peer()->increment_flap_count();
    set_state(IDLE);
    set_last_notification_in(event.msg->error, event.msg->subcode, event.Name());
}


//The template below must only be called for EvBgpHeaderError, EvBgpOpenError
//or EvBgpUpdateError
template <class Ev, int code>
void StateMachine::OnIdleError(const Ev &event) {
    SendNotificationAndClose(event.session,
                             code, event.subcode, event.data);
    // Collect before changing state
    bool flap = (state_ == ESTABLISHED);
    if (flap) peer()->increment_flap_count();
    set_state(IDLE);
}

void StateMachine::TimerErrorHanlder(std::string name, std::string error) {
}

bool StateMachine::ConnectTimerExpired() {
    if (!deleted_) {

        BGP_LOG_STR(BgpStateMachineMessage, SandeshLevel::SYS_DEBUG,
            BGP_LOG_FLAG_TRACE, "Bgp Peer " << peer_->ToString() <<
            " : Triggered EvConnectTimerExpired in state " << StateName());
        Enqueue(fsm::EvConnectTimerExpired(connect_timer_));
    }

    return false;
}

bool StateMachine::OpenTimerExpired() {
    Enqueue(fsm::EvOpenTimerExpired(open_timer_));
    return false;
}

bool StateMachine::HoldTimerExpired() {
    error_code error;

    // Reset hold timer if there is data already present in the socket.
    if (peer() && peer()->session() && peer()->session()->socket() &&
            peer()->session()->socket()->available(error) > 0) {
        return true;
    }
    Enqueue(fsm::EvHoldTimerExpired(hold_timer_));
    return false;
}


bool StateMachine::IdleHoldTimerExpired() {
    Enqueue(fsm::EvIdleHoldTimerExpired(idle_hold_timer_));
    return false;
}

// Concurrency: ASIO thread.
void StateMachine::OnSessionEvent(
        TcpSession *session, TcpSession::Event event) {
    BgpSession *bgp_session = static_cast<BgpSession *>(session);
    switch (event) {
    case TcpSession::CONNECT_COMPLETE:
        Enqueue(fsm::EvTcpConnected(bgp_session));
        break;
    case TcpSession::CONNECT_FAILED:
        Enqueue(fsm::EvTcpConnectFail(bgp_session));
        break;
    case TcpSession::CLOSE:
        Enqueue(fsm::EvTcpClose(bgp_session));
        break;
    default:
        break;
    }
}

bool StateMachine::PassiveOpen(BgpSession *session) {
    session->set_observer(boost::bind(&StateMachine::OnSessionEvent,
                this, _1, _2));
    return Enqueue(fsm::EvTcpPassiveOpen(session));
}

void StateMachine::OnMessage(BgpSession *session, BgpProto::BgpMessage *msg) {
    switch (msg->type) {
    case BgpProto::OPEN: {
        BgpProto::OpenMessage *open_msg =
                static_cast<BgpProto::OpenMessage *>(msg);
        BgpPeer *peer = session->Peer();
        peer->inc_rx_open();
        if (int subcode = open_msg->Validate(peer)) {
            Enqueue(fsm::EvBgpOpenError(session, subcode));
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
        if (peer) peer->inc_rx_notification();
        Enqueue(fsm::EvBgpNotification(session,
                static_cast<BgpProto::Notification *>(msg)));
        msg = NULL;
        break;
    }
    case BgpProto::UPDATE: {
        BgpProto::Update *update =
                static_cast<BgpProto::Update *>(msg);
        BgpPeer *peer = NULL;
        if (session) 
            peer = session->Peer();

        if (peer) peer->inc_rx_update();
        std::string data;
        int subcode;
        if (peer && (subcode = update->Validate(peer, data))) {
            Enqueue(fsm::EvBgpUpdateError(session, subcode, data));
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
    if (msg) delete msg;
}

void StateMachine::OnSessionError(BgpSession *session,
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
    default: break;
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
ostream &operator<< (ostream &out, const StateMachine::State &state) {
    out << state_names[state];
    return out;
}

void StateMachine::AssignSession(bool active) {
    if (active) {
        peer_->set_session(active_session_);
        active_session_ = NULL;
    } else {
        peer_->set_session(passive_session_);
        passive_session_ = NULL;
    }
}

const int StateMachine::kConnectInterval;

int StateMachine::GetConnectTime() const {
    int backoff = min(attempts_, 6);
    return std::min(backoff ? 1 << (backoff - 1) : 0, kConnectInterval);
}

bool StateMachine::DequeueEvent(StateMachine::EventContainer ec) {
    const fsm::EvTcpDeleteSession *deferred_delete =
            dynamic_cast<const fsm::EvTcpDeleteSession *>(ec.event.get());
    if (deferred_delete != NULL) {
        peer_->server()->session_manager()->DeleteSession(
            deferred_delete->session);
        return true;
    }
    if (deleted_) return true;

    set_last_event(TYPE_NAME(*ec.event));
    if (ec.validate.empty() || ec.validate(this)) {
        SandeshLevel::type log_level = SandeshLevel::SYS_DEBUG;

        //
        // Reduce log level for periodic keep alive messages.
        //
        if (get_state() == ESTABLISHED &&
                TYPE_NAME(*ec.event) == "fsm::EvBgpKeepalive") {
            log_level = SandeshLevel::UT_DEBUG;
        }
        SM_LOG(log_level, "Processing " << TYPE_NAME(*ec.event) << " in state "
                                        << StateName());
        process_event(*ec.event);
    } else {
        SM_LOG(SandeshLevel::SYS_INFO,
               "Discarding " << TYPE_NAME(*ec.event) << " in state "
                                                     << StateName());
    }
    ec.event.reset();
    return true;
}

void StateMachine::unconsumed_event(const sc::event_base &event) {
}

// This class determines whether a given class has a method called 'validate'
template<typename Ev>
struct HasValidate
{
    template<typename T, bool (T::*)(StateMachine *) const> struct SFINAE {};
    template<typename T> static char Test(SFINAE<T, &T::validate>*);
    template<typename T> static int Test(...);
    static const bool Has = sizeof(Test<Ev>(0)) == sizeof(char);
};

template <typename Ev, bool has_validate>
struct ValidateFn {
    EvValidate operator()(const Ev *event) { return NULL; }
};

template <typename Ev>
struct ValidateFn<Ev, true> {
    EvValidate operator()(const Ev *event) {
        return boost::bind(&Ev::validate, event, _1);
    }
};

template <typename Ev>
bool StateMachine::Enqueue(const Ev &event) {
    if (deleted_) return false;

    EventContainer ec;
    ec.event = event.intrusive_from_this();
    ec.validate = ValidateFn<Ev, HasValidate<Ev>::Has>()(static_cast<const Ev *>(ec.event.get()));
    work_queue_.Enqueue(ec);

    return true;
}

void StateMachine::SetDataCollectionKey(BgpPeerInfo *peer_info) const {
    peer_->SetDataCollectionKey(peer_info);
}

const std::string StateMachine::last_notification_in_error() const {
    return(BgpProto::Notification::ToString(
           static_cast<BgpProto::Notification::Code>(last_notification_in_.first),
           last_notification_in_.second));
}

const std::string StateMachine::last_notification_out_error() const {
    return(BgpProto::Notification::ToString(
           static_cast<BgpProto::Notification::Code>(last_notification_out_.first),
           last_notification_out_.second));
}

//
// Get the default hold time in seconds
//
const int StateMachine::GetDefaultHoldTime() {
    static bool init_ = false;
    static int time_ = kHoldTime;

    if (!init_) {

        // XXX For testing only - Configure through environment variable
        char *time_str = getenv("BGP_KEEPALIVE_SECONDS");
        if (time_str) {
            time_ = strtoul(time_str, NULL, 0) * 3;
        }
        init_ = true;
    }
    return time_;
}

void StateMachine::set_last_event(const std::string &event) { 
    last_event_ = event; 
    last_event_at_ = UTCTimestampUsec(); 
    // ignore logging keepalive event
    if (event == "fsm::EvBgpKeepalive") {
	    return;
    }
    BgpPeerInfoData peer_info;
    peer_info.set_name(peer()->ToUVEKey());
    PeerEventInfo event_info;
    event_info.set_last_event(last_event_);
    event_info.set_last_event_at(last_event_at_);
    peer_info.set_event_info(event_info);

    BGPPeerInfo::Send(peer_info);
}

void StateMachine::set_last_notification_out(int code, int subcode, const string &reason) {
    last_notification_out_ = std::make_pair(code, subcode);
    last_notification_out_at_ = UTCTimestampUsec();
    last_notification_out_error_ = reason;
    BgpPeerInfoData peer_info;
    peer_info.set_name(peer()->ToUVEKey());
    peer_info.set_notification_out_at(last_notification_out_at_);
    peer_info.set_notification_out(BgpProto::Notification::ToString(
            static_cast<BgpProto::Notification::Code>(code), subcode));
    BGPPeerInfo::Send(peer_info);
}

void StateMachine::set_last_notification_in(int code, int subcode, const string &reason) {
    last_notification_in_ = std::make_pair(code, subcode);
    last_notification_in_at_ = UTCTimestampUsec();
    last_notification_in_error_ = reason;
    BgpPeerInfoData peer_info;
    peer_info.set_name(peer()->ToUVEKey());
    peer_info.set_notification_in_at(last_notification_in_at_);
    peer_info.set_notification_in(BgpProto::Notification::ToString(
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
    hold_time_ = GetDefaultHoldTime(); 
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
