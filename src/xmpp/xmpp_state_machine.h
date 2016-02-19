/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __XMPP_STATE_MC__
#define __XMPP_STATE_MC__

#include <boost/asio.hpp>
#include <boost/statechart/state_machine.hpp>
#include <tbb/mutex.h>

#include "base/queue_task.h"
#include "base/timer.h"
#include "io/ssl_session.h"
#include "xmpp/xmpp_proto.h"

namespace sc = boost::statechart;

namespace xmsm {
    struct Idle;
    struct Active;
    struct Connect;
    struct OpenSent;
    struct OpenConfirm;
    struct XmppStreamEstablished;
}

namespace xmsm {
    struct EvStart;
    struct Idle;

typedef enum {
    IDLE        = 0,
    ACTIVE      = 1,
    CONNECT     = 2,
    OPENSENT    = 3,
    OPENCONFIRM = 4,
    ESTABLISHED = 5
} XmState;

typedef enum {
    OPENCONFIRM_INIT                         = 0,
    OPENCONFIRM_FEATURE_NEGOTIATION          = 1,
    OPENCONFIRM_FEATURE_SUCCESS              = 2
} XmOpenConfirmState;


typedef enum {
    EvTLSHANDSHAKE_FAILURE = 0,
    EvTLSHANDSHAKE_SUCCESS = 1
} SslHandShakeResponse;

}

class XmppConnection;
class XmppSession;
class TcpSession;
class XmppConnectionInfo;

class XmppStateMachine :
        public sc::state_machine<XmppStateMachine, xmsm::Idle> {
public:
    static const int kOpenTime = 15;         // seconds
    static const int kConnectInterval = 30;  // seconds
    static const int kHoldTime = 90;         // seconds
    static const int kMaxAttempts = 4;
    static const int kJitter = 10;           // percentage

    XmppStateMachine(XmppConnection *connection, bool active, bool auth_enabled = false);
    ~XmppStateMachine();

    void Initialize();
    void Clear();
    void SetAdminState(bool down);

    // State transitions
    void OnStart(const xmsm::EvStart &event);

    virtual void StartConnectTimer(int seconds);
    void CancelConnectTimer();
    virtual void StartOpenTimer(int seconds);
    void CancelOpenTimer();

    int GetConfiguredHoldTime() const;
    virtual void StartHoldTimer();
    void CancelHoldTimer();
    void ResetSession();

    bool IsAuthEnabled() { return auth_enabled_; }

    void TimerErrorHandler(std::string name, std::string error);

    // Feed session events into the state machine.
    virtual void OnSessionEvent(TcpSession *session, TcpSession::Event event);

    // Receive Passive Open.
    bool PassiveOpen(XmppSession *session);

    // Receive incoming message
    void OnMessage(XmppSession *session, const XmppStanza::XmppMessage *msg);

    // Receive incoming ssl events
    //void OnEvent(XmppSession *session, xmsm::SslHandShakeResponse);
    void OnEvent(SslSession *session, xmsm::SslHandShakeResponse);

    //void OnSessionError(Error error);

    // transfer the ownership of the session to the connection.
    void AssignSession();

    // Calculate Timer value for active to connect transition.
    int GetConnectTime() const;

    void SetHandShakeCbHandler(SslHandShakeCallbackHandler cb) {
        handshake_cb_ = cb;
    }

    SslHandShakeCallbackHandler HandShakeCbHandler() { return handshake_cb_; }

    std::string StateName() const;
    std::string LastStateName() const;
    std::string LastStateChangeAt() const;
    xmsm::XmState StateType() const;
    xmsm::XmOpenConfirmState OpenConfirmStateType() const;

    // getters and setters
    XmppConnection *connection() { return connection_; }
    void set_connection(const XmppConnection *connection) {
        connection_ = const_cast<XmppConnection *>(connection);
    }
    void SwapXmppConnection(XmppStateMachine *other) {
        XmppConnection *tmp = connection_;
        connection_ = other->connection_;
        other->connection_ = tmp;
    }
    bool IsActiveChannel();
    bool logUVE();
    const char *ChannelType();
    void set_session(TcpSession *session);
    void clear_session();
    void DeleteSession(XmppSession *session);
    XmppSession *session() { return session_; }
    void set_state(xmsm::XmState state);
    xmsm::XmState get_state() { return state_; }
    void set_openconfirm_state(xmsm::XmOpenConfirmState state);
    xmsm::XmOpenConfirmState get_openconfirm_state() {
        return openconfirm_state_;
    }

    void connect_attempts_inc() { attempts_++; }
    void connect_attempts_clear() { attempts_ = 0; }
    int get_connect_attempts() const { return attempts_; }

    void keepalive_count_inc() { keepalive_count_++; }
    void keepalive_count_clear() { keepalive_count_ = 0; }
    int get_keepalive_count() const { return keepalive_count_; }

    int hold_time() const { return hold_time_; }
    virtual int hold_time_msecs() const { return hold_time_ * 1000; }
    void set_hold_time(int hold_time) { hold_time_ = hold_time; }

    void unconsumed_event(const sc::event_base &event);

    void SendConnectionInfo(const std::string &event, 
                            const std::string &nextstate = "");

    void SendConnectionInfo(XmppConnectionInfo *info, const std::string &event, 
                            const std::string &nextstate = ""); 
    void ResurrectOldConnection(XmppConnection *connection,
                                XmppSession *session);

    void set_last_event(const std::string &event);
    const std::string &last_event() const { return last_event_; }

    bool ConnectTimerCancelled() { return connect_timer_->cancelled(); }
    bool OpenTimerCancelled() { return open_timer_->cancelled(); }
    bool HoldTimerCancelled() { return hold_timer_->cancelled(); }
    void AssertOnHoldTimeout();
    bool HoldTimerExpired();

private:
    friend class XmppStateMachineTest;

    bool ConnectTimerExpired();
    bool OpenTimerExpired();
    bool Enqueue(const sc::event_base &ev);
    bool DequeueEvent(boost::intrusive_ptr<const sc::event_base> &event);
    bool ProcessStreamHeaderMessage(XmppSession *session,
                                    const XmppStanza::XmppMessage *msg);

    WorkQueue<boost::intrusive_ptr<const sc::event_base> > work_queue_;
    XmppConnection *connection_;
    XmppSession *session_;
    TcpServer *server_;
    Timer *connect_timer_;
    Timer *open_timer_;
    Timer *hold_timer_;
    int hold_time_;
    uint32_t attempts_;
    uint32_t keepalive_count_;
    bool deleted_;
    bool in_dequeue_;
    bool is_active_;
    bool auth_enabled_;
    xmsm::XmState state_;
    xmsm::XmState last_state_;
    xmsm::XmOpenConfirmState openconfirm_state_;
    uint64_t state_since_;
    std::string last_event_;
    uint64_t last_event_at_;
    SslHandShakeCallbackHandler handshake_cb_;

    DISALLOW_COPY_AND_ASSIGN(XmppStateMachine);
};

#endif
