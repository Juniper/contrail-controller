/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "ifmap_state_machine.h"

#include <typeinfo>

#include <boost/asio/placeholders.hpp>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/statechart/custom_reaction.hpp>
#include <boost/statechart/event.hpp>
#include <boost/statechart/simple_state.hpp>
#include <boost/statechart/state.hpp>
#include <boost/statechart/state_machine.hpp>
#include <boost/statechart/transition.hpp>
#include <boost/mpl/list.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/task_annotations.h"
#include "base/timer_impl.h"

#include "ifmap_channel.h"
#include "ifmap/ifmap_log.h"
#include "ifmap_manager.h"
#include "ifmap/ifmap_log_types.h"

using boost::system::error_code;

const int IFMapStateMachine::kConnectWaitIntervalMs = 30000;
const int IFMapStateMachine::kResponseWaitIntervalMs = 5000;

namespace sc = boost::statechart;
namespace mpl = boost::mpl;

namespace ifsm {

// Events
struct EvStart : sc::event<EvStart> {
    EvStart() { }
    static const char * Name() {
        return "EvStart";
    }
};

struct EvWriteSuccess : sc::event<EvWriteSuccess> {
    EvWriteSuccess() { }
    static const char * Name() {
        return "EvWriteSuccess";
    }
};

struct EvWriteFailed : sc::event<EvWriteFailed> {
    EvWriteFailed() { }
    static const char * Name() {
        return "EvWriteFailed";
    }
};

struct EvReadSuccess : sc::event<EvReadSuccess> {
    EvReadSuccess() { }
    static const char * Name() {
        return "EvReadSuccess";
    }
};

struct EvReadFailed : sc::event<EvReadFailed> {
    EvReadFailed() { }
    static const char * Name() {
        return "EvReadFailed";
    }
};

struct EvConnectionCleaned : sc::event<EvConnectionCleaned> {
    EvConnectionCleaned() { }
    static const char * Name() {
        return "EvConnectionCleaned";
    }
};

struct EvResolveSuccess : sc::event<EvResolveSuccess> {
    EvResolveSuccess() { }
    static const char * Name() {
        return "EvResolveSuccess";
    }
};

struct EvResolveFailed : sc::event<EvResolveFailed> {
    EvResolveFailed() { }
    static const char * Name() {
        return "EvResolveFailed";
    }
};

struct EvConnectSuccess : sc::event<EvConnectSuccess> {
    EvConnectSuccess() { }
    static const char * Name() {
        return "EvConnectSuccess";
    }
};

struct EvConnectFailed : sc::event<EvConnectFailed> {
    EvConnectFailed() { }
    static const char * Name() {
        return "EvConnectFailed";
    }
};

struct EvConnectTimerExpired : sc::event<EvConnectTimerExpired> {
    EvConnectTimerExpired() { }
    static const char * Name() {
        return "EvConnectTimerExpired";
    }
};

struct EvResponseTimerExpired : sc::event<EvResponseTimerExpired> {
    EvResponseTimerExpired() { }
    static const char * Name() {
        return "EvResponseTimerExpired";
    }
};

struct EvHandshakeSuccess : sc::event<EvHandshakeSuccess> {
    EvHandshakeSuccess() { }
    static const char * Name() {
        return "EvHandshakeSuccess";
    }
};

struct EvHandshakeFailed : sc::event<EvHandshakeFailed> {
    EvHandshakeFailed() { }
    static const char * Name() {
        return "EvHandshakeFailed";
    }
};

struct EvProcResponseSuccess : sc::event<EvProcResponseSuccess> {
    EvProcResponseSuccess(boost::system::error_code error, size_t bytes) :
        error_(error), bytes_(bytes) {
    }
    static const char * Name() {
        return "EvProcResponseSuccess";
    }
    boost::system::error_code error_;
    size_t bytes_;
};

struct EvProcResponseFailed : sc::event<EvProcResponseFailed> {
    EvProcResponseFailed() { }
    static const char * Name() {
        return "EvProcResponseFailed";
    }
};

struct EvConnectionResetReq : sc::event<EvConnectionResetReq> {
    EvConnectionResetReq(std::string host, std::string port) :
        host_(host), port_(port) {
    }
    static const char * Name() {
        return "EvConnectionResetReq";
    }
    std::string host_;
    std::string port_;
};

// States
struct Idle;
struct ServerResolve;
struct SsrcConnect;
struct SsrcSslHandshake;
struct SendNewSession;
struct NewSessionResponseWait;
struct SendSubscribe;
struct SubscribeResponseWait;
struct ArcConnect;
struct ArcSslHandshake;
struct SendPoll;
struct PollResponseWait;
struct SsrcStart;
struct ConnectTimerWait;

// Initial state machine state
struct Idle : sc::simple_state<Idle, IFMapStateMachine> {
    typedef sc::transition<EvStart, ServerResolve, IFMapStateMachine,
                           &IFMapStateMachine::OnStart> reactions;
    Idle() {
        IFMAP_SM_DEBUG(IFMapString, "Entering Idle.");
    }
};

// This state is used only on connect retries. If we are in Idle, we directly
// go to ServerResolve
struct SsrcStart : sc::state<SsrcStart, IFMapStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvConnectionCleaned>,
        sc::custom_reaction<EvConnectionResetReq>
    > reactions;
    SsrcStart(my_context ctx) : my_base(ctx) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        // Since we are restarting the SM, log all transitions.
        sm->set_log_all_transitions(true);
        sm->set_state(IFMapStateMachine::SSRCSTART);
        sm->channel()->ReconnectPreparation();
    }
    sc::result react(const EvConnectionCleaned &event) {
        return transit<ConnectTimerWait>();
    }
    sc::result react(const EvConnectionResetReq &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->channel()->SetHostPort(event.host_, event.port_);
        return discard_event();
    }
};

// This state is used only on connect retries.
struct ConnectTimerWait : sc::state<ConnectTimerWait, IFMapStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvConnectTimerExpired>,
        sc::custom_reaction<EvConnectionResetReq>
    > reactions;
    ConnectTimerWait(my_context ctx) : my_base(ctx) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->set_state(IFMapStateMachine::CONNECTTIMERWAIT);
        bool is_ssrc = true;
        sm->StartConnectTimer(sm->GetConnectTime(is_ssrc));
        sm->ssrc_connect_attempts_inc();
    }
    ~ConnectTimerWait() {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopConnectTimer();
    }
    sc::result react(const EvConnectTimerExpired &event) {
        return transit<ServerResolve>();
    }
    sc::result react(const EvConnectionResetReq &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopConnectTimer();
        sm->channel()->SetHostPort(event.host_, event.port_);
        return transit<SsrcStart>();
    }
};

struct ServerResolve : sc::state<ServerResolve, IFMapStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvResolveSuccess>,
        sc::custom_reaction<EvResolveFailed>,
        sc::custom_reaction<EvResponseTimerExpired>,
        sc::custom_reaction<EvConnectionResetReq>
    > reactions;
    ServerResolve(my_context ctx) : my_base(ctx) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->set_state(IFMapStateMachine::SERVERRESOLVE);
        sm->channel()->DoResolve();
        sm->StartResponseTimer();
    }
    sc::result react(const EvResolveSuccess &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        return transit<SsrcConnect>();
    }
    sc::result react(const EvResolveFailed &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        return transit<SsrcStart>();
    }
    sc::result react(const EvResponseTimerExpired &event) {
        return transit<SsrcStart>();
    }
    sc::result react(const EvConnectionResetReq &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        sm->channel()->SetHostPort(event.host_, event.port_);
        return transit<SsrcStart>();
    }
};

// Attempt to create the SSRC channel via connect i.e. 3-way HS. On success,
// continue to SSL handshake. Otherwise, start over. 
struct SsrcConnect : sc::state<SsrcConnect, IFMapStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvConnectSuccess>,
        sc::custom_reaction<EvConnectFailed>,
        sc::custom_reaction<EvResponseTimerExpired>,
        sc::custom_reaction<EvConnectionResetReq>
    > reactions;
    SsrcConnect(my_context ctx) : my_base(ctx) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->set_state(IFMapStateMachine::SSRCCONNECT);
        bool is_ssrc = true;
        sm->channel()->DoConnect(is_ssrc);
        sm->StartResponseTimer();
    }
    sc::result react(const EvConnectSuccess &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        sm->ssrc_connect_attempts_clear();
        return transit<SsrcSslHandshake>();
    }
    sc::result react(const EvConnectFailed &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        return transit<SsrcStart>();
    }
    sc::result react(const EvResponseTimerExpired &event) {
        return transit<SsrcStart>();
    }
    sc::result react(const EvConnectionResetReq &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        sm->channel()->SetHostPort(event.host_, event.port_);
        return transit<SsrcStart>();
    }
};

// Perform the SSL handshake over the SSRC channel. On success, continue to
// NewSession. Otherwise, start over.
struct SsrcSslHandshake :
                        sc::state<SsrcSslHandshake, IFMapStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvHandshakeSuccess>,
        sc::custom_reaction<EvHandshakeFailed>,
        sc::custom_reaction<EvResponseTimerExpired>,
        sc::custom_reaction<EvConnectionResetReq>
    > reactions;
    SsrcSslHandshake(my_context ctx) : my_base(ctx) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->set_state(IFMapStateMachine::SSRCSSLHANDSHAKE);
        bool is_ssrc = true;
        sm->channel()->DoSslHandshake(is_ssrc);
        sm->StartResponseTimer();
    }
    sc::result react(const EvHandshakeSuccess &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        return transit<SendNewSession>();
    }
    sc::result react(const EvHandshakeFailed &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        return transit<SsrcStart>();
    }
    sc::result react(const EvResponseTimerExpired &event) {
        return transit<SsrcStart>();
    }
    sc::result react(const EvConnectionResetReq &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        sm->channel()->SetHostPort(event.host_, event.port_);
        return transit<SsrcStart>();
    }
};

// Send the newSession request. If the write is successful, continue to
// NewSessionResponseWait and wait for a response. Otherwise, start over.
struct SendNewSession : sc::state<SendNewSession, IFMapStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvWriteSuccess>,
        sc::custom_reaction<EvWriteFailed>,
        sc::custom_reaction<EvConnectionResetReq>
    > reactions;
    SendNewSession(my_context ctx) : my_base(ctx) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->set_state(IFMapStateMachine::SENDNEWSESSION);
        sm->channel()->SendNewSessionRequest();
    }
    sc::result react(const EvWriteSuccess &event) {
        return transit<NewSessionResponseWait>();
    }
    sc::result react(const EvWriteFailed &event) {
        return transit<SsrcStart>();
    }
    sc::result react(const EvConnectionResetReq &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->channel()->SetHostPort(event.host_, event.port_);
        return transit<SsrcStart>();
    }
};

// Wait for a response from the server to the newSession request. On
// successfully receiving a response, attempt to extract relevant information.
// If we fail, start over. Otherwise, continue to SendSubscribe.
// Also, if the read fails, start over.
struct NewSessionResponseWait :
    sc::state<NewSessionResponseWait, IFMapStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvReadSuccess>,
        sc::custom_reaction<EvReadFailed>,
        sc::custom_reaction<EvResponseTimerExpired>,
        sc::custom_reaction<EvProcResponseSuccess>,
        sc::custom_reaction<EvProcResponseFailed>,
        sc::custom_reaction<EvConnectionResetReq>
    > reactions;
    NewSessionResponseWait(my_context ctx) : my_base(ctx) {
        // ask channel to wait for newSession response
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->set_state(IFMapStateMachine::NEWSESSIONRESPONSEWAIT);
        sm->channel()->NewSessionResponseWait();
        sm->StartResponseTimer();
    }
    sc::result react(const EvReadSuccess &event) {
        // response to 'newSession' has been read successfully
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        int failure = sm->channel()->ExtractPubSessionId();
        if (failure) {
            return transit<SsrcStart>();
        } else {
            return transit<SendSubscribe>();
        }
    }
    sc::result react(const EvReadFailed &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        return transit<SsrcStart>();
    }
    sc::result react(const EvResponseTimerExpired &event) {
        return transit<SsrcStart>();
    }
    sc::result react(const EvProcResponseSuccess &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->channel()->ProcResponse(event.error_, event.bytes_);
        return discard_event();
    }
    sc::result react(const EvProcResponseFailed &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        return transit<SsrcStart>();
    }
    sc::result react(const EvConnectionResetReq &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        sm->channel()->SetHostPort(event.host_, event.port_);
        return transit<SsrcStart>();
    }
};

// Send the subscribe request. If the write is successful, continue to
// SubscribeResponseWait and wait for a response. Otherwise, start over.
struct SendSubscribe : sc::state<SendSubscribe, IFMapStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvWriteSuccess>,
        sc::custom_reaction<EvWriteFailed>,
        sc::custom_reaction<EvConnectionResetReq>
    > reactions;
    SendSubscribe(my_context ctx) : my_base(ctx) {
        // ask channel to send 'subscribe'
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->set_state(IFMapStateMachine::SENDSUBSCRIBE);
        sm->channel()->SendSubscribe();
    }
    sc::result react(const EvWriteSuccess &event) {
        return transit<SubscribeResponseWait>();
    }
    sc::result react(const EvWriteFailed &event) {
        return transit<SsrcStart>();
    }
    sc::result react(const EvConnectionResetReq &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->channel()->SetHostPort(event.host_, event.port_);
        return transit<SsrcStart>();
    }
};

// Wait for a response from the server to the subscribe request. On
// successfully receiving a response, check if the subscribe succeeded.
// If it failed, start over. Otherwise, continue to ArcConnect.
// Also, if the read fails, start over.
struct SubscribeResponseWait :
    sc::state<SubscribeResponseWait, IFMapStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvReadSuccess>,
        sc::custom_reaction<EvReadFailed>,
        sc::custom_reaction<EvResponseTimerExpired>,
        sc::custom_reaction<EvProcResponseSuccess>,
        sc::custom_reaction<EvProcResponseFailed>,
        sc::custom_reaction<EvConnectionResetReq>
    > reactions;
    SubscribeResponseWait(my_context ctx) : my_base(ctx) {
        // ask channel to wait for newSession response
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->set_state(IFMapStateMachine::SUBSCRIBERESPONSEWAIT);
        sm->channel()->SubscribeResponseWait();
        sm->StartResponseTimer();
    }
    sc::result react(const EvReadSuccess &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        int failure = sm->channel()->ReadSubscribeResponseStr();
        if (failure) {
            return transit<SsrcStart>();
        } else {
            return transit<ArcConnect>();
        }
    }
    sc::result react(const EvReadFailed &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        return transit<SsrcStart>();
    }
    sc::result react(const EvResponseTimerExpired &event) {
        return transit<SsrcStart>();
    }
    sc::result react(const EvProcResponseSuccess &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->channel()->ProcResponse(event.error_, event.bytes_);
        return discard_event();
    }
    sc::result react(const EvProcResponseFailed &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        return transit<SsrcStart>();
    }
    sc::result react(const EvConnectionResetReq &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        sm->channel()->SetHostPort(event.host_, event.port_);
        return transit<SsrcStart>();
    }
};

// Attempt to create the ARC channel via connect i.e. 3-way HS. On success,
// continue to SSL handshake. Otherwise, start over. 
struct ArcConnect : sc::state<ArcConnect, IFMapStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvConnectSuccess>,
        sc::custom_reaction<EvConnectFailed>,
        sc::custom_reaction<EvResponseTimerExpired>,
        sc::custom_reaction<EvConnectionResetReq>
    > reactions;
    ArcConnect(my_context ctx) : my_base(ctx) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->set_state(IFMapStateMachine::ARCCONNECT);
        bool is_ssrc = false;
        sm->channel()->DoConnect(is_ssrc);
        sm->StartResponseTimer();
    }
    sc::result react(const EvConnectSuccess &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        sm->arc_connect_attempts_clear();
        return transit<ArcSslHandshake>();
    }
    sc::result react(const EvConnectFailed &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        return transit<SsrcStart>();
    }
    sc::result react(const EvResponseTimerExpired &event) {
        return transit<SsrcStart>();
    }
    sc::result react(const EvConnectionResetReq &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        sm->channel()->SetHostPort(event.host_, event.port_);
        return transit<SsrcStart>();
    }
};

// Perform the SSL handshake over the ARC channel. On success, continue to
// SendPoll. Otherwise, start over.
struct ArcSslHandshake : sc::state<ArcSslHandshake, IFMapStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvHandshakeSuccess>,
        sc::custom_reaction<EvHandshakeFailed>,
        sc::custom_reaction<EvResponseTimerExpired>,
        sc::custom_reaction<EvConnectionResetReq>
    > reactions;
    ArcSslHandshake(my_context ctx) : my_base(ctx) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->set_state(IFMapStateMachine::ARCSSLHANDSHAKE);
        bool is_ssrc = false;
        sm->channel()->DoSslHandshake(is_ssrc);
        sm->StartResponseTimer();
    }
    sc::result react(const EvHandshakeSuccess &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        return transit<SendPoll>();
    }
    sc::result react(const EvHandshakeFailed &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        return transit<SsrcStart>();
    }
    sc::result react(const EvResponseTimerExpired &event) {
        return transit<SsrcStart>();
    }
    sc::result react(const EvConnectionResetReq &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->StopResponseTimer();
        sm->channel()->SetHostPort(event.host_, event.port_);
        return transit<SsrcStart>();
    }
};

// Send the poll request. If the write is successful, continue to
// PollResponseWait and wait for a response. Otherwise, start over.
struct SendPoll : sc::state<SendPoll, IFMapStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvWriteSuccess>,
        sc::custom_reaction<EvWriteFailed>,
        sc::custom_reaction<EvConnectionResetReq>
    > reactions;
    SendPoll(my_context ctx) : my_base(ctx) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->set_state(IFMapStateMachine::SENDPOLL);
        sm->channel()->SendPollRequest();
    }
    sc::result react(const EvWriteSuccess &event) {
        return transit<PollResponseWait>();
    }
    sc::result react(const EvWriteFailed &event) {
        return transit<SsrcStart>();
    }
    sc::result react(const EvConnectionResetReq &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->channel()->SetHostPort(event.host_, event.port_);
        return transit<SsrcStart>();
    }
};

// Wait for a response from the server to the poll request. On
// successfully receiving a response, check if we received a kosher response.
// If not, start over. If we did, give the received poll response to the
// parser.  Also, if the read fails, start over.
struct PollResponseWait :
    sc::state<PollResponseWait, IFMapStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvReadSuccess>,
        sc::custom_reaction<EvReadFailed>,
        sc::custom_reaction<EvProcResponseSuccess>,
        sc::custom_reaction<EvProcResponseFailed>,
        sc::custom_reaction<EvConnectionResetReq>
    > reactions;
    PollResponseWait(my_context ctx) : my_base(ctx) {
        // ask channel to wait for newSession response
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->set_state(IFMapStateMachine::POLLRESPONSEWAIT);
        sm->channel()->PollResponseWait();
    }
    sc::result react(const EvReadSuccess &event) {
        // the header of the response to 'poll' has been read successfully
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        int failure = sm->channel()->ReadPollResponse();
        if (failure) {
            return transit<SsrcStart>();
        } else {
            return transit<SendPoll>();
        }
    }
    sc::result react(const EvReadFailed &event) {
        return transit<SsrcStart>();
    }
    sc::result react(const EvProcResponseSuccess &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->channel()->ProcResponse(event.error_, event.bytes_);
        return discard_event();
    }
    sc::result react(const EvProcResponseFailed &event) {
        return transit<SsrcStart>();
    }
    sc::result react(const EvConnectionResetReq &event) {
        IFMapStateMachine *sm = &context<IFMapStateMachine>();
        sm->channel()->SetHostPort(event.host_, event.port_);
        return transit<SsrcStart>();
    }
};

}  // namespace ifsm

IFMapStateMachine::IFMapStateMachine(IFMapManager *manager)
    : manager_(manager),
      connect_timer_(new TimerImpl(*manager->io_service())),
      ssrc_connect_attempts_(0), arc_connect_attempts_(0),
      response_timer_(new TimerImpl(*manager->io_service())),
      response_timer_expired_count_(0),
      work_queue_(TaskScheduler::GetInstance()->GetTaskId(
                  "ifmap::StateMachine"),
                  0, boost::bind(&IFMapStateMachine::DequeueEvent, this, _1)),
      channel_(NULL), state_(IDLE), last_state_(IDLE), last_state_change_at_(0),
      last_event_at_(0), max_connect_wait_interval_ms_(kConnectWaitIntervalMs),
      max_response_wait_interval_ms_(kResponseWaitIntervalMs),
      log_all_transitions_(true) {
}

void IFMapStateMachine::Initialize() {
    initiate();
    EnqueueEvent(ifsm::EvStart());
}

// Return the time (in milliseconds) to wait for connect to succeed
int IFMapStateMachine::GetConnectTime(bool is_ssrc) const {
    int backoff, backoff_ms;
    if (is_ssrc) {
        backoff = std::min(ssrc_connect_attempts_, 6);
        backoff_ms = (1 << backoff) * 1000;
        return std::min(backoff_ms, max_connect_wait_interval_ms_);
    } else {
        backoff = std::min(arc_connect_attempts_, 6);
        backoff_ms = (1 << backoff) * 1000;
        return std::min(backoff_ms, max_connect_wait_interval_ms_);
    }
}

void IFMapStateMachine::StartConnectTimer(int milliseconds) {
    // TODO assert if timer is still running. How?
    error_code ec;
    connect_timer_->expires_from_now(milliseconds, ec);
    connect_timer_->async_wait(
        boost::bind(&IFMapStateMachine::ConnectTimerExpired, this,
                    boost::asio::placeholders::error, milliseconds));
}

void IFMapStateMachine::StopConnectTimer() {
    error_code ec;
    connect_timer_->cancel(ec);
}

// current state: ConnectTimerWait
void IFMapStateMachine::ConnectTimerExpired(
        const boost::system::error_code& error, int milliseconds) {
    // If the timer was cancelled, there is nothing to do.
    if (error == boost::asio::error::operation_aborted) {
        return;
    }
    IFMAP_SM_DEBUG(IFMapSmExpiredTimerMessage, milliseconds/1000,
                   "second connect timer expired.");
    EnqueueEvent(ifsm::EvConnectTimerExpired());
}

void IFMapStateMachine::StartResponseTimer() {
    error_code ec;
    response_timer_->expires_from_now(max_response_wait_interval_ms_, ec);
    response_timer_->async_wait(
        boost::bind(&IFMapStateMachine::ResponseTimerExpired, this,
                    boost::asio::placeholders::error));
}

void IFMapStateMachine::StopResponseTimer() {
    error_code ec;
    response_timer_->cancel(ec);
}

// current state: SsrcConnect or SsrcSslHandshake or NewSessionResponseWait
// or SubscribeResponseWait or ArcConnect or ArcSslHandshake
void IFMapStateMachine::ResponseTimerExpired(
        const boost::system::error_code& error) {
    // If the timer was cancelled, there is nothing to do.
    if (error == boost::asio::error::operation_aborted) {
        return;
    }
    IFMAP_SM_DEBUG(IFMapSmExpiredTimerMessage,
        max_response_wait_interval_ms_/1000, "second response timer expired.");
    response_timer_expired_count_inc();
    EnqueueEvent(ifsm::EvResponseTimerExpired());
}

void IFMapStateMachine::Start() {
    Initialize();
}

// current state: SsrcStart
void IFMapStateMachine::ProcConnectionCleaned() {
    EnqueueEvent(ifsm::EvConnectionCleaned());
}

// current state: ServerResolve
void IFMapStateMachine::ProcResolveResponse(
        const boost::system::error_code& error) {
    CHECK_CONCURRENCY_MAIN_THR();
    if (error) {
        if (ProcErrorAndIgnore(error)) {
            return;
        }
        EnqueueEvent(ifsm::EvResolveFailed());
    } else {
        EnqueueEvent(ifsm::EvResolveSuccess());
    }
}

// current state: SsrcConnect or ArcConnect
void IFMapStateMachine::ProcConnectResponse(
        const boost::system::error_code& error) {
    CHECK_CONCURRENCY_MAIN_THR();
    if (error) {
        if (ProcErrorAndIgnore(error)) {
            return;
        }
        EnqueueEvent(ifsm::EvConnectFailed());
    } else {
        EnqueueEvent(ifsm::EvConnectSuccess());
    }
}

// current state: SsrcSslHandshake or ArcSslHandshake
void IFMapStateMachine::ProcHandshakeResponse(
        const boost::system::error_code& error) {
    CHECK_CONCURRENCY_MAIN_THR();
    if (error) {
        if (ProcErrorAndIgnore(error)) {
            return;
        }
        EnqueueEvent(ifsm::EvHandshakeFailed());
    } else {
        EnqueueEvent(ifsm::EvHandshakeSuccess());
    }
}

// current state: SendNewSession
void IFMapStateMachine::ProcNewSessionWrite(
        const boost::system::error_code& error, size_t bytes_transferred) {
    CHECK_CONCURRENCY_MAIN_THR();
    if (error) {
        if (ProcErrorAndIgnore(error)) {
            return;
        }
        EnqueueEvent(ifsm::EvWriteFailed());
    } else {
        EnqueueEvent(ifsm::EvWriteSuccess());
    }
}

// current state: NewSessionResponseWait
// Can run in the context of both, "ifmap::StateMachine" or main-thread
void IFMapStateMachine::ProcNewSessionResponse(
        const boost::system::error_code& error, size_t bytes_transferred) {
    if (error) {
        if (ProcErrorAndIgnore(error)) {
            return;
        }
        EnqueueEvent(ifsm::EvReadFailed());
    } else {
        EnqueueEvent(ifsm::EvReadSuccess());
    }
}

// current state: SendSubscribe
void IFMapStateMachine::ProcSubscribeWrite(
        const boost::system::error_code& error, size_t bytes_transferred) {
    CHECK_CONCURRENCY_MAIN_THR();
    if (error) {
        if (ProcErrorAndIgnore(error)) {
            return;
        }
        EnqueueEvent(ifsm::EvWriteFailed());
    } else {
        EnqueueEvent(ifsm::EvWriteSuccess());
    }
}

// current state: SubscribeResponseWait
// Can run in the context of both, "ifmap::StateMachine" or main-thread
void IFMapStateMachine::ProcSubscribeResponse(
        const boost::system::error_code& error, size_t bytes_transferred) {
    if (error) {
        if (ProcErrorAndIgnore(error)) {
            return;
        }
        EnqueueEvent(ifsm::EvReadFailed());
    } else {
        EnqueueEvent(ifsm::EvReadSuccess());
    }
}

// current state: SendPoll
void IFMapStateMachine::ProcPollWrite(
        const boost::system::error_code& error, size_t bytes_transferred) {
    CHECK_CONCURRENCY_MAIN_THR();
    if (error) {
        if (ProcErrorAndIgnore(error)) {
            return;
        }
        EnqueueEvent(ifsm::EvWriteFailed());
    } else {
        channel_->increment_sent_msg_cnt();
        EnqueueEvent(ifsm::EvWriteSuccess());
    }
}

// current state: PollResponseWait
// Can run in the context of both, "ifmap::StateMachine" or main-thread
void IFMapStateMachine::ProcPollResponseRead(
        const boost::system::error_code& error, size_t bytes_transferred) {
    if (error) {
       if (ProcErrorAndIgnore(error)) {
            return;
        }
        EnqueueEvent(ifsm::EvReadFailed());
    } else {
        EnqueueEvent(ifsm::EvReadSuccess());
    }
}

// current state:
// NewSessionResponseWait, SubscribeResponseWait, PollResponseWait
void IFMapStateMachine::ProcResponse(
        const boost::system::error_code& error, size_t bytes_transferred) {
    CHECK_CONCURRENCY_MAIN_THR();
    if (error) {
        if (ProcErrorAndIgnore(error)) {
            return;
        }
        EnqueueEvent(ifsm::EvProcResponseFailed());
    } else {
        EnqueueEvent(ifsm::EvProcResponseSuccess(error, bytes_transferred));
    }
}

// Returns true if the error can be ignored. False otherwise.
bool IFMapStateMachine::ProcErrorAndIgnore(
        const boost::system::error_code& error) {
    IFMAP_SM_WARN(IFMapPeerConnError, error.message(), StateName(last_state_),
                  StateName(state_), last_event());

    bool done = false;
    if (error == boost::asio::error::operation_aborted) {
        done = true;
    } else if (error == boost::asio::error::timed_out) {
        channel_->IncrementTimedout();
    }
    return done;
}

void IFMapStateMachine::ResetConnectionReqEnqueue(const std::string &host,
                                                  const std::string &port) {
    EnqueueEvent(ifsm::EvConnectionResetReq(host, port));
}

void IFMapStateMachine::OnStart(const ifsm::EvStart &event) {
}

void IFMapStateMachine::EnqueueEvent(const sc::event_base &event) {
    work_queue_.Enqueue(event.intrusive_from_this());
}

bool IFMapStateMachine::DequeueEvent(
        boost::intrusive_ptr<const sc::event_base>& event) {
    set_last_event(TYPE_NAME(*event));
    process_event(*event);
    event.reset();
    return true;
}

void IFMapStateMachine::LogSmTransition() {
    switch (state_) {
    case SENDPOLL:
    case POLLRESPONSEWAIT:
        // Log these states only once for each connection attempt. This helps
        // since we will keep circling between them once the connection has
        // been established and we dont need to print them for every new VM 
        // that comes up.
        if (log_all_transitions()) {
            IFMAP_SM_DEBUG(IFMapSmTransitionMessage, StateName(last_state_),
                           "===>", StateName(state_));
            if (state_ == POLLRESPONSEWAIT) {
                set_log_all_transitions(false);
            }
        }
        break;
    default:
        IFMAP_SM_DEBUG(IFMapSmTransitionMessage, StateName(last_state_), "===>",
                    StateName(state_));
        break;
    }
}

void IFMapStateMachine::set_state(State state) {
    last_state_ = state_; state_ = state;
    LogSmTransition();
    last_state_change_at_ = UTCTimestampUsec();
}

// This must match exactly with IFMapStateMachine::State
static const std::string state_names[] = {
    "Idle",
    "ServerResolve",
    "SsrcConnect",
    "SsrcSslHandshake",
    "SendNewSession",
    "NewSessionResponseWait",
    "SendSubscribe",
    "SubscribeResponseWait",
    "ArcConnect",
    "ArcSslHandshake",
    "SendPoll",
    "PollResponseWait",
    "SsrcStart",
    "ConnectTimerWait",
};

const std::string &IFMapStateMachine::StateName() const {
    return state_names[state_];
}

const std::string &IFMapStateMachine::StateName(State state) const {
    return state_names[state];
}

const std::string &IFMapStateMachine::LastStateName() const {
    return state_names[last_state_];
}

const std::string IFMapStateMachine::last_state_change_at() const {
    return duration_usecs_to_string(UTCTimestampUsec() - last_state_change_at_);
}

void IFMapStateMachine::set_last_event(const std::string &event) { 
    last_event_ = event;
    last_event_at_ = UTCTimestampUsec(); 
}

const std::string IFMapStateMachine::last_event_at() const {
    return duration_usecs_to_string(UTCTimestampUsec() - last_event_at_);
}
