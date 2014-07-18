/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_state_machine.h"

#include <typeinfo>
#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/statechart/custom_reaction.hpp>
#include <boost/statechart/event.hpp>
#include <boost/statechart/simple_state.hpp>
#include <boost/statechart/state.hpp>
#include <boost/statechart/state_machine.hpp>
#include <boost/statechart/transition.hpp>

#include "base/logging.h"
#include "base/task_annotations.h"
#include "io/event_manager.h"
#include "io/tcp_session.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include "sandesh/xmpp_peer_info_types.h"
#include "sandesh/xmpp_state_machine_sandesh_types.h"
#include "sandesh/xmpp_trace_sandesh_types.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_factory.h"
#include "xmpp/xmpp_log.h"
#include "xmpp/xmpp_server.h"
#include "xmpp/xmpp_session.h"


using namespace std;

namespace mpl = boost::mpl;
namespace sc = boost::statechart;

#define SM_LOG(_sm, _msg) do {  \
    XMPP_UTDEBUG(XmppStateMachineDebug, _sm->ChannelType(), _msg); \
} while (0);

namespace xmsm {
// events

struct EvStart : sc::event<EvStart> {
    static const char *Name() {
        return "EvStart";
    }
};

struct EvStop : sc::event<EvStop> {
    static const char *Name() {
        return "EvStop";
    }
};

struct EvAdminDown : sc::event<EvAdminDown> {
    static const char *Name() {
        return "EvAdminDown";
    }
};

struct EvConnectTimerExpired : sc::event<EvConnectTimerExpired> {
    static const char *Name() {
        return "EvConnectTimerExpired";
    }
};

struct EvOpenTimerExpired : sc::event<EvOpenTimerExpired> {
    static const char *Name() {
        return "EvOpenTimerExpired";
    }
};

struct EvHoldTimerExpired : sc::event<EvHoldTimerExpired> {
    static const char *Name() {
        return "EvHoldTimerExpired";
    }
};

struct EvTcpConnected : sc::event<EvTcpConnected> {
    EvTcpConnected(XmppSession *session) : session(session) { };
    static const char *Name() {
        return "EvTcpConnected";
    }
    XmppSession *session;
} ;

struct EvTcpConnectFail : sc::event<EvTcpConnectFail> {
    EvTcpConnectFail(XmppSession *session) : session(session) { };
    static const char *Name() {
        return "EvTcpConnectFail";
    }
    XmppSession *session;
} ;

struct EvTcpPassiveOpen : sc::event<EvTcpPassiveOpen> {
    EvTcpPassiveOpen(XmppSession *session) : session(session) { };
    static const char *Name() {
        return "EvTcpPassiveOpen";
    }
    XmppSession *session;
};

struct EvTcpClose : sc::event<EvTcpClose> {
    EvTcpClose(XmppSession *session) : session(session) { };
    static const char *Name() {
        return "EvTcpClose";
    }
    XmppSession *session;
};

struct EvTcpDeleteSession : sc::event<EvTcpDeleteSession> {
    explicit EvTcpDeleteSession(TcpSession *session) : session(session) { }
    static const char *Name() {
        return "EvTcpDeleteSession";
    }
    TcpSession *session;
};

struct EvXmppOpen : public sc::event<EvXmppOpen> {
    EvXmppOpen(XmppSession *session, const XmppStanza::XmppMessage *msg) : 
        session(session), 
        msg(static_cast<const XmppStanza::XmppStreamMessage *>(msg)) {
    }
    static const char *Name() {
        return "EvXmppOpen";
    }
    XmppSession *session;
    boost::shared_ptr<const XmppStanza::XmppStreamMessage> msg;
};

struct EvXmppKeepalive : sc::event<EvXmppKeepalive> {
    EvXmppKeepalive(XmppSession *session, const XmppStanza::XmppMessage *msg)
    : session(session), msg(msg) {
    }
    static const char *Name() {
        return "EvXmppKeepalive";
    }
    XmppSession *session;
    boost::shared_ptr<const XmppStanza::XmppMessage> msg;
};

struct EvXmppMessageReceive : sc::event<EvXmppMessageReceive> {
    EvXmppMessageReceive(XmppSession *session, 
                         const XmppStanza::XmppMessage *msg) 
    : session(session), msg(msg) {
    }
    static const char *Name() {
        return "EvXmppMessageReceive";
    }
    XmppSession *session;
    boost::shared_ptr<const XmppStanza::XmppMessage> msg;
};

struct EvXmppIqReceive : sc::event<EvXmppIqReceive> {
    EvXmppIqReceive(XmppSession *session, 
                    const XmppStanza::XmppMessage *msg) 
    : session(session), msg(msg) {
    }
    static const char *Name() {
        return "EvXmppIqReceive";
    }
    XmppSession *session;
    boost::shared_ptr<const XmppStanza::XmppMessage> msg;
};

struct EvXmppOpenReceive : sc::event<EvXmppOpenReceive> {
    EvXmppOpenReceive(XmppSession *session) : session(session) {
    }
    static const char *Name() {
        return "EvXmppOpenReceive";
    }
    XmppSession *session;
};

struct OpenSent;
struct OpenConfirm;
struct XmppStreamEstablished;

struct Idle : public sc::state<Idle, XmppStateMachine> {
    typedef sc::transition<EvStart, Active,
            XmppStateMachine, &XmppStateMachine::OnStart> reactions;
    Idle(my_context ctx) : my_base(ctx) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "(Idle)");
        state_machine->set_state(IDLE);
        state_machine->SendConnectionInfo("Start", "Active");
    }
};

struct Active : public sc::state<Active, XmppStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvAdminDown>,
        sc::custom_reaction<EvConnectTimerExpired>,
        sc::custom_reaction<EvOpenTimerExpired>,
        sc::custom_reaction<EvTcpPassiveOpen>,
        sc::custom_reaction<EvTcpClose>,
        sc::custom_reaction<EvXmppOpen>,
        sc::custom_reaction<EvStop>
    > reactions;

    Active(my_context ctx) : my_base(ctx) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "(Xmpp Active State)");
        state_machine->set_state(ACTIVE);
        if (state_machine->IsActiveChannel() ) {
            SM_LOG(state_machine, "(Xmpp Start Connect Timer)");
            state_machine->StartConnectTimer(state_machine->GetConnectTime());
        }
    }
    ~Active() {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "Cancelling Connect timer ");
        state_machine->CancelConnectTimer();
        state_machine->CancelOpenTimer();
    }

   //event on client only
    sc::result react(const EvConnectTimerExpired &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (state_machine->ConnectTimerCancelled()) {
            SM_LOG(state_machine, "Discard EvConnectTimerExpired in (Active) State");
            return discard_event();
        } else {
            SM_LOG(state_machine, "EvConnectTimerExpired in (Active) State");
            return transit<Connect>();
        }
    }

    // event on server only
    sc::result react(const EvTcpPassiveOpen &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "EvTcpPassiveOpen in (Active) State");
        event.session->AsyncReadStart();
        state_machine->set_session(event.session);
        event.session->set_observer(
            boost::bind(&XmppStateMachine::OnSessionEvent, 
                        state_machine, _1, _2));
        state_machine->StartOpenTimer(XmppStateMachine::kOpenTime);
        XmppConnectionInfo info;
        info.set_local_port(event.session->local_port());
        info.set_remote_port(event.session->remote_port());
        state_machine->SendConnectionInfo(&info, event.Name());
        return discard_event();
    }

    // event on server only
    sc::result react(const EvTcpClose &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        SM_LOG(state_machine, "EvTcpClose in (Active) State");
        state_machine->CancelOpenTimer();
        state_machine->ResetSession();
        state_machine->SendConnectionInfo(event.Name(), "Idle");
        return transit<Idle>();
    }

    //event on server only
    sc::result react(const EvOpenTimerExpired &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (state_machine->OpenTimerCancelled()) {
            SM_LOG(state_machine, "Discard EvOpenTimerExpired in (Active) State");
            return discard_event();
        }
        SM_LOG(state_machine, "EvOpenTimerExpired in (Active) State");
        // At this point session on connection is not set, hence SendClose 
        // using session on the state_machine.
        TcpSession *session = state_machine->session();
        XmppConnection *connection = state_machine->connection();
        connection->SendClose(session);
        state_machine->ResetSession();
        state_machine->SendConnectionInfo(event.Name(), "Idle");
        return transit<Idle>();
    }

    //event on server only
    sc::result react(const EvXmppOpen &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        SM_LOG(state_machine, "EvXmppOpen in (Active) State");
        XmppConnection *connection = state_machine->connection();
        TcpSession *session = state_machine->session();
        state_machine->AssignSession();
        state_machine->CancelOpenTimer();
        state_machine->StartHoldTimer(connection->GetKeepAliveTimer() * 3);
        connection->SendOpenConfirm(session);
        connection->StartKeepAliveTimer();
        // Skipping openconfirm state till we have client authentication 
        // return transit<OpenConfirm>();
        XmppConnectionInfo info;
        info.set_identifier(event.msg->from);
        state_machine->SendConnectionInfo(&info, event.Name(), "Established");
        return transit<XmppStreamEstablished>();
    }

    sc::result react(const EvStop &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        XmppConnection *connection = state_machine->connection();
        SM_LOG(state_machine, "EvStop in (Active) State");
        state_machine->CancelOpenTimer();
        state_machine->CancelConnectTimer();
        connection->StopKeepAliveTimer();

        if (state_machine->IsActiveChannel() ) {
            state_machine->set_session(NULL);
            state_machine->SendConnectionInfo(event.Name(), "Active");
            return transit<Active>();
        } else {
            state_machine->ResetSession();
            state_machine->SendConnectionInfo(event.Name(), "Idle");
            return transit<Idle>();
        }
    }

    sc::result react(const EvAdminDown &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "Xmpp Admin Down. Transit to IDLE");
        XmppConnectionInfo info;
        info.set_close_reason("Administratively down");
        state_machine->connection()->set_close_reason("Administratively down");
        state_machine->SendConnectionInfo(&info, event.Name(), "Idle");
        return transit<Idle>();
    }

};

//State valid only for client side, connection in Active state
struct Connect : public sc::state<Connect, XmppStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvAdminDown>,
        sc::custom_reaction<EvConnectTimerExpired>,
        sc::custom_reaction<EvTcpConnected>,
        sc::custom_reaction<EvTcpConnectFail>,
        sc::custom_reaction<EvTcpClose>,
        sc::custom_reaction<EvStop>
    > reactions;

    static const int kConnectTimeout = 60;  // seconds

    Connect(my_context ctx) : my_base(ctx) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "(Xmpp Connect)");
        StartSession(state_machine);
        state_machine->connect_attempts_inc();
        SM_LOG(state_machine, "Xmpp Connect: Start Connect timer");
        state_machine->set_state(CONNECT);
        state_machine->StartConnectTimer(state_machine->GetConnectTime());
        XmppSession *session = state_machine->session();
        if (session != NULL) {
            XmppConnectionInfo info;
            info.set_local_port(session->local_port());
            info.set_remote_port(session->remote_port());
            state_machine->SendConnectionInfo(&info, "Connect Event");
       }
    }
    ~Connect() {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "Cancelling Connect timer ");
        state_machine->CancelConnectTimer();
    }

    sc::result react(const EvConnectTimerExpired &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (state_machine->ConnectTimerCancelled()) {
            SM_LOG(state_machine, "Discard EvConnectTimerExpired in (Connect) State");
            return discard_event();
        }
        SM_LOG(state_machine, "Xmpp Connect: Connect timer expired");
        CloseSession(state_machine);
        XmppConnectionInfo info;
        info.set_close_reason("Connect timer expired");
        state_machine->connection()->set_close_reason("Connect timer expired");
        state_machine->SendConnectionInfo(&info, event.Name(), "Active");
        return transit<Active>();
    }

    sc::result react(const EvTcpConnected &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        XmppConnection *connection = state_machine->connection();
        state_machine->CancelConnectTimer();
        TcpSession *session = state_machine->session();
        connection->SendOpen(session);
        state_machine->StartHoldTimer(connection->GetKeepAliveTimer() * 3);
        SM_LOG(state_machine, "Xmpp Connected: Cancelling Connect timer");
        XmppConnectionInfo info;
        info.set_local_port(session->local_port());
        info.set_remote_port(session->remote_port());
        state_machine->SendConnectionInfo(&info, event.Name(), "OpenSent");
        return transit<OpenSent>();
    }

    sc::result react(const EvTcpConnectFail &event) {
        // delete session; restart connect timer.
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        state_machine->set_session(NULL);
        SM_LOG(state_machine,"Xmpp Connect fail: Cancelling Connect timer");
        state_machine->CancelConnectTimer();
        state_machine->SendConnectionInfo(event.Name(), "Active");
        return transit<Active>();
    }

    sc::result react(const EvTcpClose &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        // close the tcp sessions.
        CloseSession(state_machine);
        SM_LOG(state_machine,"Xmpp Tcp Close in Active State");
        state_machine->CancelConnectTimer();
        state_machine->SendConnectionInfo(event.Name(), "Active");
        return transit<Active>();
    }

    sc::result react(const EvStop &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "EvStop in (Connect) State");
        CloseSession(state_machine);
        state_machine->CancelConnectTimer();
        XmppConnectionInfo info;
        info.set_close_reason("EvStop received");
        state_machine->connection()->set_close_reason("EvStop received");
        state_machine->SendConnectionInfo(&info, event.Name(), "Active");
        return transit<Active>();
    }

    sc::result react(const EvAdminDown &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        CloseSession(state_machine);
        SM_LOG(state_machine, "Xmpp Admin Down. Transit to IDLE");
        XmppConnectionInfo info;
        info.set_close_reason("Administratively down");
        state_machine->connection()->set_close_reason("Administratively down");
        state_machine->SendConnectionInfo(&info, event.Name(), "Idle");
        return transit<Idle>();
    }

    // Create an active connection request.
    void StartSession(XmppStateMachine *state_machine) {
        XmppConnection *connection = state_machine->connection();
        XmppSession *session = connection->CreateSession();
        state_machine->set_session(session);
        session->set_observer(boost::bind(&XmppStateMachine::OnSessionEvent,
                                          state_machine, _1, _2));
        boost::system::error_code err;
        session->socket()->bind(connection->local_endpoint(), err);
        if (err) {
            LOG(WARN, "Bind failure for local address " <<
                connection->local_endpoint() << " : " << err.message());
            assert(false);
        }
        connection->server()->Connect(session, connection->endpoint());
    }

    void CloseSession(XmppStateMachine *state_machine) {
        state_machine->set_session(NULL);
    }
};

// The client reaches OpenSent after sending an immediate OPEN on a active
// connection. Server should not come in this state.
struct OpenSent : public sc::state<OpenSent, XmppStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvAdminDown>,
        sc::custom_reaction<EvTcpClose>,
        sc::custom_reaction<EvXmppOpen>,
        sc::custom_reaction<EvHoldTimerExpired>,
        sc::custom_reaction<EvStop>
    > reactions;

    OpenSent(my_context ctx) : my_base(ctx) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "(Xmpp OpenSent)");
        state_machine->set_state(OPENSENT);
    }
    ~OpenSent() {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "Cancelling Hold timer ");
        state_machine->CancelHoldTimer();
    }

    sc::result react(const EvTcpClose &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        SM_LOG(state_machine, "EvTcpClose in (OpenSent) State");
        state_machine->CancelHoldTimer();
        if (event.session) { 
            state_machine->set_session(NULL);
            state_machine->SendConnectionInfo(event.Name(), "Active");
            return transit<Active>();
        } 
        return discard_event();
    }
    sc::result react(const EvXmppOpen &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "EvXmppOpen (OpenSent) State");
        XmppConnection *connection = state_machine->connection();
        if (event.session == state_machine->session()) {
            state_machine->AssignSession();
            connection->SendKeepAlive();
            connection->StartKeepAliveTimer();
            state_machine->StartHoldTimer(connection->GetKeepAliveTimer() * 3);
            XmppConnectionInfo info;
            info.set_identifier(event.msg->from);
            state_machine->SendConnectionInfo(&info, event.Name(), "Established");
            return transit<XmppStreamEstablished>();
        }
        return discard_event();
    }

    sc::result react(const EvHoldTimerExpired &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (state_machine->HoldTimerCancelled()) {
            SM_LOG(state_machine, "Discard EvHoldTimerExpired in (OpenSent) State");
            return discard_event();
        }
        SM_LOG(state_machine, 
                "EvHoldTimerExpired in (OpenSent) State. Transit to IDLE");
        CloseSession(state_machine);
        XmppConnectionInfo info;
        info.set_close_reason("Hold timer expired");
        state_machine->connection()->set_close_reason("Hold timer expired");
        state_machine->SendConnectionInfo(&info, event.Name(), "Active");
        return transit<Active>();
    }

    sc::result react(const EvStop &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "EvStop in (OpenSent) State");
        state_machine->CancelHoldTimer();
        CloseSession(state_machine);
        XmppConnectionInfo info;
        info.set_close_reason("EvStop received");
        state_machine->connection()->set_close_reason("EvStop received");
        state_machine->SendConnectionInfo(&info, event.Name(), "Active");
        return transit<Active>();
    }

    sc::result react(const EvAdminDown &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        CloseSession(state_machine);
        SM_LOG(state_machine, "Xmpp Admin Down. Transit to IDLE");
        XmppConnectionInfo info;
        info.set_close_reason("Administratively down");
        state_machine->connection()->set_close_reason("Administratively down");
        state_machine->SendConnectionInfo(&info, event.Name(), "Idle");
        return transit<Idle>();
    }

    void CloseSession(XmppStateMachine *state_machine) {
        state_machine->set_session(NULL);
    }
};

struct OpenConfirm : public sc::state<OpenConfirm, XmppStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvAdminDown>,
        sc::custom_reaction<EvXmppKeepalive>,
        sc::custom_reaction<EvTcpClose>,
        sc::custom_reaction<EvHoldTimerExpired>,
        sc::custom_reaction<EvStop>
    > reactions;

    OpenConfirm(my_context ctx) : my_base(ctx) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "(Xmpp OpenConfirm)");
        state_machine->set_state(OPENCONFIRM);
    }

    sc::result react(const EvXmppKeepalive &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        XmppConnection *connection = state_machine->connection();
        SM_LOG(state_machine, "EvXmppKeepalive in (OpenConfirm) State");
        state_machine->StartHoldTimer(connection->GetKeepAliveTimer() * 3);
        state_machine->SendConnectionInfo(event.Name(), "Established");
        return transit<XmppStreamEstablished>();
    }

    sc::result react(const EvTcpClose &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "EvTcpClose in (OpenConfirm) State");
        state_machine->CancelHoldTimer();
        if (state_machine->IsActiveChannel() ) {
            CloseSession(state_machine);
            state_machine->SendConnectionInfo(event.Name(), "Active");
            return transit<Active>();
        } else {
            state_machine->ResetSession();
            state_machine->SendConnectionInfo(event.Name(), "Idle");
            return transit<Idle>();
        }
    }

    sc::result react(const EvHoldTimerExpired &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (state_machine->HoldTimerCancelled()) {
            SM_LOG(state_machine, "Discard EvHoldTimerExpired in (OpenConfirm) State");
            return discard_event();
        }
        SM_LOG(state_machine, 
               "EvHoldTimerExpired in (OpenConfirm) State.");
        XmppConnectionInfo info;
        info.set_close_reason("Hold timer expired");
        state_machine->connection()->set_close_reason("Hold timer expired");
        if (state_machine->IsActiveChannel() ) {
            CloseSession(state_machine);
            state_machine->SendConnectionInfo(&info, event.Name(), "Active");
            return transit<Active>();
        } else {
            state_machine->ResetSession();
            state_machine->SendConnectionInfo(&info, event.Name(), "Idle");
            return transit<Idle>();
        }
    }

    sc::result react(const EvStop &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "EvStop in (OpenConfirm) State");
        state_machine->CancelHoldTimer();
        if (state_machine->IsActiveChannel() ) {
            CloseSession(state_machine);
            state_machine->SendConnectionInfo(event.Name(), "Active");
            return transit<Active>();
        } else {
            state_machine->ResetSession();
            state_machine->SendConnectionInfo(event.Name(), "Idle");
            return transit<Idle>();
        }
    }

    sc::result react(const EvAdminDown &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        CloseSession(state_machine);
        SM_LOG(state_machine, "Xmpp Admin Down. Transit to IDLE");
        XmppConnectionInfo info;
        info.set_close_reason("Administratively down");
        state_machine->connection()->set_close_reason("Administratively down");
        state_machine->SendConnectionInfo(&info, event.Name(), "Idle");
        return transit<Idle>();
    }

    void CloseSession(XmppStateMachine *state_machine) {
        XmppConnection *connection = state_machine->connection();
        if (connection != NULL) connection->StopKeepAliveTimer();
        state_machine->set_session(NULL);
    }


};

struct XmppStreamEstablished : 
        public sc::state<XmppStreamEstablished, XmppStateMachine> {
    typedef mpl::list<
        sc::custom_reaction<EvAdminDown>,
        sc::custom_reaction<EvTcpClose>,
        sc::custom_reaction<EvXmppKeepalive>,
        sc::custom_reaction<EvXmppMessageReceive>,
        sc::custom_reaction<EvXmppIqReceive>,
        sc::custom_reaction<EvHoldTimerExpired>,
        sc::custom_reaction<EvStop>
    > reactions;

    XmppStreamEstablished(my_context ctx) : my_base(ctx) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "(XMPP Established)");
        state_machine->connect_attempts_clear();
        XmppConnection *connection = state_machine->connection();
        state_machine->StartHoldTimer(connection->GetKeepAliveTimer() * 3);
        state_machine->set_state(ESTABLISHED);
        connection->ChannelMux()->HandleStateEvent(xmsm::ESTABLISHED);
    }
    ~XmppStreamEstablished() {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "Cancelling Hold timer ");
        state_machine->CancelHoldTimer();
    }

    sc::result react(const EvTcpClose &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        SM_LOG(state_machine, "EvTcpClose in (Established) State");
        state_machine->SendConnectionInfo(event.Name());
        state_machine->ResetSession();
        if (state_machine->IsActiveChannel()) return transit<Active>(); else return transit<Idle>();
    }

    sc::result react(const EvXmppKeepalive &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        XmppConnection *connection = state_machine->connection();
        state_machine->StartHoldTimer(connection->GetKeepAliveTimer() * 3);
        return discard_event();
    }

    sc::result react(const EvXmppMessageReceive &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        XmppConnection *connection = state_machine->connection();
        SM_LOG(state_machine, "EvXmppMessageReceive in (Established) State");
        state_machine->StartHoldTimer(connection->GetKeepAliveTimer() * 3);
        state_machine->connection()->ProcessXmppChatMessage(
            static_cast<const XmppStanza::XmppChatMessage *>(event.msg.get()));
        return discard_event();
    }

    sc::result react(const EvXmppIqReceive &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        XmppConnection *connection = state_machine->connection();
        SM_LOG(state_machine, "EvXmppIqMessageReceive in (Established) State");
        state_machine->StartHoldTimer(connection->GetKeepAliveTimer() * 3);
        state_machine->connection()->ProcessXmppIqMessage(
                static_cast<const XmppStanza::XmppMessage *>(event.msg.get())
                );
        return discard_event();
    }

    sc::result react(const EvHoldTimerExpired &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (state_machine->HoldTimerCancelled()) {
            SM_LOG(state_machine, "Discard EvHoldTimerExpired in (Established) State");
            return discard_event();
        }
        XMPP_NOTICE(XmppStateMachineDebug, state_machine->ChannelType(),
                "EvHoldTimerExpired in (Established) State. Transit to IDLE");
        state_machine->SendConnectionInfo(event.Name());
        state_machine->AssertOnHoldTimeout();
        state_machine->ResetSession();
        if (state_machine->IsActiveChannel()) {
            return transit<Active>(); 
        } else {
            return transit<Idle>();
        }
    }

    sc::result react(const EvStop &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        state_machine->SendConnectionInfo(event.Name());
        state_machine->ResetSession();
        if (state_machine->IsActiveChannel()) {
            return transit<Active>(); 
        } else {
            return transit<Idle>();
        }
    }

    sc::result react(const EvAdminDown &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        state_machine->ResetSession();
        SM_LOG(state_machine, "Xmpp Admin Down. Transit to IDLE");
        XmppConnectionInfo info;
        info.set_close_reason("Administratively down");
        state_machine->connection()->set_close_reason("Administratively down");
        state_machine->SendConnectionInfo(&info, event.Name(), "Idle");
        return transit<Idle>();
    }
};

} // namespace xmsm

void XmppStateMachine::AssertOnHoldTimeout() {
    static bool init_ = false;
    static bool assert_ = false;

    if (!init_) {
        char *str = getenv("XMPP_ASSERT_ON_HOLD_TIMEOUT");
        if (str && strtoul(str, NULL, 0) != 0) assert_ = true;
        init_ = true;
    }

    if (!assert_) return;

    if (connection()) {
        connection()->LogMsg("HOLD TIMER EXPIRED: ");
    } else {
        LOG4CPLUS_DEBUG(log4cplus::Logger::getRoot(), "HOLD TIMER EXPIRED: ");
    }

    assert(!assert_);
}

void XmppStateMachine::ResetSession() {
    XmppConnection *connection = this->connection();
    set_session(NULL);
    CancelHoldTimer();

    if (!connection) return;

    // Stop keepalives, transition to IDLE and notify registerd entities.
    connection->increment_flap_count();
    connection->StopKeepAliveTimer();
    connection->ChannelMux()->HandleStateEvent(xmsm::IDLE);
    if (IsActiveChannel()) return;

    // Retain the connection if graceful restart is supported.
    XmppServer *server = dynamic_cast<XmppServer *>(connection->server());
    if (server->IsPeerCloseGraceful()) return;

    // Delete the connection.
    connection->ManagedDelete();
}

XmppStateMachine::XmppStateMachine(XmppConnection *connection, bool active)
    : work_queue_(TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"),
                  connection->GetIndex(),
                  boost::bind(&XmppStateMachine::DequeueEvent, this, _1)),
      connection_(connection), session_(NULL),
      connect_timer_(TimerManager::CreateTimer(*connection->server()->event_manager()->io_service(), "Connect timer",
             TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"), 0)),
      open_timer_(TimerManager::CreateTimer(*connection->server()->event_manager()->io_service(), "Open timer",
             TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"), 0)),
      hold_timer_(TimerManager::CreateTimer(*connection->server()->event_manager()->io_service(), "Hold timer",
             TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"), 0)),
      attempts_(0),
      deleted_(false),
      in_dequeue_(false),
      is_active_(active),
      state_(xmsm::IDLE) {
}

XmppStateMachine::~XmppStateMachine() {
    TcpSession *sess = session();

    assert(!deleted_);
    deleted_ = true;

    work_queue_.Shutdown();
    //
    // If there is a session assigned to this state machine, reset the observer
    // so that tcp does not have a reference to 'this' which is going away
    //
    if (sess) {
        set_session(NULL);
    }

    //
    // Explicitly call the state destructor before the state machine itself.
    // This is needed because some of the destructors access the state machine
    // context.
    //
    terminate();

    //
    // Delete timer after state machine is terminated so that there is no
    // possible reference to the timers being deleted any more
    //
    TimerManager::DeleteTimer(connect_timer_);
    TimerManager::DeleteTimer(open_timer_);
    TimerManager::DeleteTimer(hold_timer_);
}

void XmppStateMachine::Initialize() {
    initiate();
    Enqueue(xmsm::EvStart());
}

// Note this api does not enqueue the deletion of TCP session
void XmppStateMachine::clear_session() {
    if (session_ != NULL) {
        session_->set_observer(NULL);
        session_->SetConnection(NULL);
        session_->Close();
        connection_->set_session(NULL);
        session_ = NULL;
    }
}

void XmppStateMachine::DeleteSession(XmppSession *session) {
    if (session != NULL) {
        session->set_observer(NULL);
        session->SetConnection(NULL);
        session->Close();
        Enqueue(xmsm::EvTcpDeleteSession(session));
    }
}

void XmppStateMachine::set_session(TcpSession *session) {
    if (session_ != NULL) {
        connection_->set_session(NULL);
        DeleteSession(session_);
    }
    session_ = static_cast<XmppSession *>(session);
}

void XmppStateMachine::TimerErrorHandler(std::string name, std::string error) {
}

void XmppStateMachine::StartConnectTimer(int seconds) {
    CancelConnectTimer();
    connect_timer_->Start(seconds * 1000,
        boost::bind(&XmppStateMachine::ConnectTimerExpired, this),
        boost::bind(&XmppStateMachine::TimerErrorHandler, this, _1, _2));
}

void XmppStateMachine::CancelConnectTimer() {
    connect_timer_->Cancel();
}

void XmppStateMachine::StartOpenTimer(int seconds) {
    CancelOpenTimer();
    open_timer_->Start(seconds * 1000,
        boost::bind(&XmppStateMachine::OpenTimerExpired, this),
        boost::bind(&XmppStateMachine::TimerErrorHandler, this, _1, _2));
}

void XmppStateMachine::CancelOpenTimer() {
    open_timer_->Cancel();
}

void XmppStateMachine::StartHoldTimer(int seconds) {

    // To reset timer value, we need to cancel before start
    CancelHoldTimer();
    if (!seconds) return;

    hold_timer_->Start(seconds * 1000,
        boost::bind(&XmppStateMachine::HoldTimerExpired, this),
        boost::bind(&XmppStateMachine::TimerErrorHandler, this, _1, _2));
}

void XmppStateMachine::CancelHoldTimer() {
    hold_timer_->Cancel();
}

void XmppStateMachine::OnStart(const xmsm::EvStart &event) {
}

bool XmppStateMachine::ConnectTimerExpired() {
    XMPP_UTDEBUG(XmppStateMachineTimerExpire, this->ChannelType(), 
                "Connect", StateName());
    Enqueue(xmsm::EvConnectTimerExpired());
    return false;
}

bool XmppStateMachine::OpenTimerExpired() {
    XMPP_UTDEBUG(XmppStateMachineTimerExpire, 
                 this->ChannelType(), "Open", StateName());
    Enqueue(xmsm::EvOpenTimerExpired());
    return false;
}


bool XmppStateMachine::HoldTimerExpired() {
    boost::system::error_code error;

    // Reset hold timer if there is data already present in the socket.
    if (session() && session()->socket() &&
            session()->socket()->available(error) > 0) {
        return true;
    }
    XMPP_UTDEBUG(XmppStateMachineTimerExpire, this->ChannelType(),
                 "Hold", StateName());
    Enqueue(xmsm::EvHoldTimerExpired());
    return false;
}

void XmppStateMachine::OnSessionEvent(
        TcpSession *session, TcpSession::Event event) {
    switch (event) {
    case TcpSession::CONNECT_COMPLETE:
        XMPP_NOTICE(XmppEventLog, this->ChannelType(), 
                    "Event: Tcp Connected ",
                    this->connection()->endpoint().address().to_string());
        Enqueue(xmsm::EvTcpConnected(static_cast<XmppSession *>(session)));
        break;
    case TcpSession::CONNECT_FAILED:
        XMPP_NOTICE(XmppEventLog, this->ChannelType(), 
                    "Event: Tcp Connect Fail ",
                    this->connection()->endpoint().address().to_string());
        Enqueue(xmsm::EvTcpConnectFail(static_cast<XmppSession *>(session)));
        break;
    case TcpSession::CLOSE:
        XMPP_NOTICE(XmppEventLog, this->ChannelType(),
                    "Event: Tcp Connection Closed ",
                    this->connection()->endpoint().address().to_string());
        Enqueue(xmsm::EvTcpClose(static_cast<XmppSession *>(session)));
        break;
    default:
        XMPP_WARNING(XmppUnknownEvent, this->ChannelType(), event);
        break;
    }
}

bool XmppStateMachine::PassiveOpen(XmppSession *session) {
    string state = "PassiveOpen in state: " + StateName();
    XMPP_NOTICE(XmppEventLog, this->ChannelType(), state, 
                session->Connection()->endpoint().address().to_string());
    return Enqueue(xmsm::EvTcpPassiveOpen(session));
}

void XmppStateMachine::OnMessage(XmppSession *session,
                                 const XmppStanza::XmppMessage *msg) {
    bool enqueued = false;

    switch (msg->type) {
        case XmppStanza::STREAM_HEADER:
            session->Connection()->SetTo(msg->from);
            enqueued = Enqueue(xmsm::EvXmppOpen(session, msg));
            break;
        case XmppStanza::WHITESPACE_MESSAGE_STANZA:
            enqueued = Enqueue(xmsm::EvXmppKeepalive(session, msg));
            break;
        case XmppStanza::IQ_STANZA:
            enqueued = Enqueue(xmsm::EvXmppIqReceive(session, msg));
            break;
        case XmppStanza::MESSAGE_STANZA:
            enqueued = Enqueue(xmsm::EvXmppIqReceive(session, msg));
            break;
        default:
            if (msg->IsValidType(msg->type)) {
            } else {
                XMPP_NOTICE(XmppStateMachineUnsupportedMessage, 
                            ChannelType(), (int)msg->type);
            }
            break;
    }

    if (!enqueued) {
        delete msg;
    }
}

void XmppStateMachine::Clear() {
    Enqueue(xmsm::EvStop());
}

void XmppStateMachine::SetAdminState(bool down) {
    assert(IsActiveChannel());
    if (down) {
        Enqueue(xmsm::EvAdminDown());
    } else {
        Enqueue(xmsm::EvStart());
    }
}

void XmppStateMachine::set_state(xmsm::XmState state) { 
    last_state_ = state_; 
    state_ = state;
    state_since_ = UTCTimestampUsec();

    if (!logUVE()) return;

    XmppPeerInfoData peer_info;
    peer_info.set_name(connection()->ToUVEKey());
    PeerStateInfo state_info;
    state_info.set_state(StateName());
    state_info.set_last_state(LastStateName());
    state_info.set_last_state_at(state_since_);
    peer_info.set_state_info(state_info);
    XMPPPeerInfo::Send(peer_info);
}

static const char *state_names[] = { 
    "Idle",
    "Active",
    "Connect",
    "OpenSent",
    "OpenConfirm",
    "Established" };

string XmppStateMachine::StateName() const {
    return state_names[state_];
}

string XmppStateMachine::LastStateName() const {
    return state_names[last_state_];
}

string XmppStateMachine::LastStateChangeAt() const {
    return integerToString(UTCUsecToPTime(state_since_));
}

xmsm::XmState XmppStateMachine::StateType() const {
    return state_;
}

void XmppStateMachine::AssignSession() {
    connection_->set_session(static_cast<XmppSession *>(session_));
}

const int XmppStateMachine::kConnectInterval;

int XmppStateMachine::GetConnectTime() const {
    int backoff = min(attempts_, 6);
    return std::min(backoff ? 1 << (backoff - 1) : 0, kConnectInterval);
}

bool XmppStateMachine::IsActiveChannel() {
    return is_active_;
}

bool XmppStateMachine::logUVE() { 
    return connection()->logUVE(); 
}

const char *XmppStateMachine::ChannelType() {
    return (IsActiveChannel() ? " Mode Client: " : " Mode Server: " );
}

bool XmppStateMachine::DequeueEvent(
        boost::intrusive_ptr<const sc::event_base>  &event) {
    const xmsm::EvTcpDeleteSession *deferred_delete =
            dynamic_cast<const xmsm::EvTcpDeleteSession *>(event.get());
    if (deferred_delete) {
        TcpSession *session = deferred_delete->session;
        session->server()->DeleteSession(session);
        return true;
    }
    if (deleted_) {
        event.reset();
        return true;
    }

    set_last_event(TYPE_NAME(*event));
    in_dequeue_ = true;
    XMPP_UTDEBUG(XmppStateMachineDequeueEvent, TYPE_NAME(*event), 
                 StateName(), this->connection()->endpoint().address().to_string());
    process_event(*event);
    event.reset();
    in_dequeue_ = false;
    return true;
}

void XmppStateMachine::unconsumed_event(const sc::event_base &event) {
    XMPP_LOG(XmppUnconsumedEvent, ChannelType(), 
            TYPE_NAME(event), StateName());
}

void XmppStateMachine::set_last_event(const std::string &event) { 
    last_event_ = event; 
    last_event_at_ = UTCTimestampUsec();

    if (!logUVE()) return;

    // ignore logging keepalive event
    if (event == "xmsm::EvXmppKeepalive") {
	    return;
    }

    XmppPeerInfoData peer_info;
    peer_info.set_name(connection()->ToUVEKey());
    PeerEventInfo event_info;
    event_info.set_last_event(last_event_);
    event_info.set_last_event_at(last_event_at_);
    peer_info.set_event_info(event_info);

    XMPPPeerInfo::Send(peer_info);
}

//
// Enqueue an event to xmpp state machine.
//
// Retrun false if the event is not enqueued
//
bool XmppStateMachine::Enqueue(const sc::event_base &event) {
    if (!deleted_) {
        work_queue_.Enqueue(event.intrusive_from_this());
        return true;
    }

    return false;
}

// Object trace routines.
void XmppStateMachine::SendConnectionInfo(const string &event, 
         const string &nextstate) {
    XmppConnectionInfo info;
    SendConnectionInfo(&info, event, nextstate);
    return;
}

void XmppStateMachine::SendConnectionInfo(XmppConnectionInfo *info, 
        const string &event, const string &nextstate) {

    info->set_ip_address(this->connection()->endpoint().address().to_string());
    info->set_state(StateName());
    info->set_event(event); 
    if (!nextstate.empty()) {
         info->set_next_state(nextstate);
    }
    XMPP_CONNECTION_LOG_MSG(*info);
    return;
}
