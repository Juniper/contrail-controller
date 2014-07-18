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
#include "io/tcp_session.h"
#include "xmpp/xmpp_proto.h"

namespace sc = boost::statechart;

namespace xmsm {
    struct Idle;
    struct Active;
    struct Connect;
    struct OpenSent;
    struct OpenConfirm;
    struct Established;
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

    XmppStateMachine(XmppConnection *connection, bool active);
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

    void TimerErrorHandler(std::string name, std::string error);

    // Feed session events into the state machine.
    void OnSessionEvent(TcpSession *session, TcpSession::Event event);

    // Receive Passive Open.
    bool PassiveOpen(XmppSession *session);

    // Receive incoming message
    void OnMessage(XmppSession *session, const XmppStanza::XmppMessage *msg);

    //void OnSessionError(Error error);

    // transfer the ownership of the session to the connection.
    void AssignSession();

    // Calculate Timer value for active to connect transition.
    int GetConnectTime() const;

    std::string StateName() const;
    std::string LastStateName() const;
    std::string LastStateChangeAt() const;
    xmsm::XmState StateType() const;

    // getters and setters
    XmppConnection *connection() { return connection_; }
    bool IsActiveChannel();
    bool logUVE();
    const char *ChannelType();
    void set_session(TcpSession *session);
    void clear_session();
    void DeleteSession(XmppSession *session);
    XmppSession *session() { return session_; }
    void set_state(xmsm::XmState state);

    void connect_attempts_inc() { attempts_++; }
    void connect_attempts_clear() { attempts_ = 0; }

    int hold_time() const { return hold_time_; }
    virtual int hold_time_msecs() const { return hold_time_ * 1000; }
    void set_hold_time(int hold_time) { hold_time_ = hold_time; }

    void unconsumed_event(const sc::event_base &event);

    void SendConnectionInfo(const std::string &event, 
                            const std::string &nextstate = "");

    void SendConnectionInfo(XmppConnectionInfo *info, const std::string &event, 
                            const std::string &nextstate = ""); 

    void set_last_event(const std::string &event);
    const std::string &last_event() const { return last_event_; }

    bool ConnectTimerCancelled() { return connect_timer_->cancelled(); }
    bool OpenTimerCancelled() { return open_timer_->cancelled(); }
    bool HoldTimerCancelled() { return hold_timer_->cancelled(); }
    void AssertOnHoldTimeout();

private:
    friend class XmppStateMachineTest;

    bool ConnectTimerExpired();
    bool OpenTimerExpired();
    bool HoldTimerExpired();
    bool Enqueue(const sc::event_base &ev);
    bool DequeueEvent(boost::intrusive_ptr<const sc::event_base> &event);

    WorkQueue<boost::intrusive_ptr<const sc::event_base> > work_queue_;
    XmppConnection *connection_;
    XmppSession *session_;
    TcpServer *server_;
    Timer *connect_timer_;
    Timer *open_timer_;
    Timer *hold_timer_;
    int hold_time_;
    int attempts_;
    bool deleted_;
    bool in_dequeue_;
    bool is_active_;
    xmsm::XmState state_;
    xmsm::XmState last_state_;
    uint64_t state_since_;
    std::string last_event_;
    uint64_t last_event_at_;

    DISALLOW_COPY_AND_ASSIGN(XmppStateMachine);
};

#endif
