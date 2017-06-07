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
#include "io/ssl_session.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "sandesh/common/vns_types.h"
#include "sandesh/common/vns_constants.h"
#include "sandesh/xmpp_client_server_sandesh_types.h"
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

struct EvXmppMessage : sc::event<EvXmppMessage> {
    explicit EvXmppMessage(XmppSession *session,
        const XmppStanza::XmppMessage *msg) : session(session), msg(msg) { }
    static const char *Name() {
        return "EvXmppMessage";
    }
    XmppSession *session;
    const XmppStanza::XmppMessage *msg;
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

struct EvStreamFeatureRequest : sc::event<EvStreamFeatureRequest> {
    EvStreamFeatureRequest(XmppSession *session,
                           const XmppStanza::XmppMessage *msg) :
        session(session),
        msg(static_cast<const XmppStanza::XmppStreamMessage *>(msg)) {
    }
    static const char *Name() {
        return "EvStreamFeatureRequest";
    }
    XmppSession *session;
    boost::shared_ptr<const XmppStanza::XmppMessage> msg;
};

struct EvStartTls : sc::event<EvStartTls> {
    EvStartTls(XmppSession *session,
               const XmppStanza::XmppMessage *msg) :
        session(session),
        msg(static_cast<const XmppStanza::XmppStreamMessage *>(msg)) {
    }
    static const char *Name() {
        return "EvStartTls";
    }
    XmppSession *session;
    boost::shared_ptr<const XmppStanza::XmppMessage> msg;
};

struct EvTlsProceed : sc::event<EvTlsProceed> {
    EvTlsProceed(XmppSession *session,
                 const XmppStanza::XmppMessage *msg) :
        session(session),
        msg(static_cast<const XmppStanza::XmppStreamMessage *>(msg)) {
    }
    static const char *Name() {
        return "EvTlsProceed";
    }
    XmppSession *session;
    boost::shared_ptr<const XmppStanza::XmppMessage> msg;
};

struct EvTlsHandShakeSuccess : sc::event<EvTlsHandShakeSuccess> {
    explicit EvTlsHandShakeSuccess(XmppSession *session) :
        session(session) { }
    static const char *Name() {
        return "EvTlsHandShakeSuccess";
    }
    XmppSession *session;
};

struct EvTlsHandShakeFailure : sc::event<EvTlsHandShakeFailure> {
    explicit EvTlsHandShakeFailure(XmppSession *session) :
        session(session) { }
    static const char *Name() {
        return "EvTlsHandShakeFailure";
    }
    XmppSession *session;
};

struct EvXmppKeepalive : sc::event<EvXmppKeepalive> {
    EvXmppKeepalive(XmppSession *session,
                    const XmppStanza::XmppMessage *msg) :
        session(session), msg(msg) {
    }
    static const char *Name() {
        return "EvXmppKeepalive";
    }
    XmppSession *session;
    boost::shared_ptr<const XmppStanza::XmppMessage> msg;
};

struct EvXmppMessageStanza : sc::event<EvXmppMessageStanza> {
    EvXmppMessageStanza(XmppSession *session,
                         const XmppStanza::XmppMessage *msg) :
    session(session), msg(msg) {
    }
    static const char *Name() {
        return "EvXmppMessageStanza";
    }
    XmppSession *session;
    boost::shared_ptr<const XmppStanza::XmppMessage> msg;
};

struct EvXmppIqStanza : sc::event<EvXmppIqStanza> {
    EvXmppIqStanza(XmppSession *session,
                    const XmppStanza::XmppMessage *msg)
    : session(session), msg(msg) {
    }
    static const char *Name() {
        return "EvXmppIqStanza";
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

struct Idle : public sc::state<Idle, XmppStateMachine> {
    typedef sc::transition<EvStart, Active,
            XmppStateMachine, &XmppStateMachine::OnStart> reactions;
    Idle(my_context ctx) : my_base(ctx) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "(Idle)");
        bool flap = (state_machine->get_state() == ESTABLISHED);
        state_machine->set_state(IDLE);
        state_machine->SendConnectionInfo("Start", "Active");
        if (flap) {
            state_machine->connection()->increment_flap_count();
        }
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
        state_machine->keepalive_count_clear();
        bool flap = (state_machine->get_state() == ESTABLISHED);
        state_machine->set_state(ACTIVE);
        if (flap) {
            state_machine->connection()->increment_flap_count();
        }
        if (state_machine->IsActiveChannel() ) {
            if (state_machine->get_connect_attempts() >=
                XmppStateMachine::kMaxAttempts) {
                XmppConnection *connection = state_machine->connection();
                if (connection) {
                    state_machine->SendConnectionInfo(
                        "Connect failed after retries");
                    // Notify clients if any action to be taken
                    connection->ChannelMux()->HandleStateEvent(xmsm::ACTIVE);
                }
            }
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
            state_machine->SendConnectionInfo(event.Name(), "Connect");
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
        XmppSession *session = state_machine->session();
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
        state_machine->AssignSession();

        XmppConnection *connection = state_machine->connection();
        if (connection->IsDeleted()) {
            state_machine->ResetSession();
            return discard_event();
        }

        XmppSession *session = state_machine->session();
        state_machine->ResurrectOldConnection(connection, session);
        state_machine->CancelOpenTimer();
        if (!connection->SendOpenConfirm(session)) {
            connection->SendClose(session);
            state_machine->ResetSession();
            state_machine->SendConnectionInfo("Send Open Confirm Failed",
                                              "Idle");
            return transit<Idle>();
        } else {
            XmppConnectionInfo info;
            info.set_identifier(event.msg->from);
            if (state_machine->IsAuthEnabled()) {
                state_machine->SendConnectionInfo(&info, event.Name(),
                                                  "Open Confirm");
                return transit<OpenConfirm>();
            } else {
                connection->StartKeepAliveTimer();
                state_machine->SendConnectionInfo(&info, event.Name(),
                                                  "Established");
                return transit<XmppStreamEstablished>();
            }
        }
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
        SM_LOG(state_machine, "Xmpp Connected: Cancelling Connect timer");
        XmppSession *session = state_machine->session();
        XmppConnectionInfo info;
        info.set_local_port(session->local_port());
        info.set_remote_port(session->remote_port());
        if (connection->SendOpen(session)) {
            state_machine->StartHoldTimer();
            state_machine->SendConnectionInfo(&info, event.Name(), "OpenSent");
            return transit<OpenSent>();
        } else {
            SM_LOG(state_machine, "SendOpen failed in (Connect) State");
            CloseSession(state_machine);
            info.set_close_reason("SendOpen failed");
            state_machine->connection()->set_close_reason("Send Open failed");
            state_machine->SendConnectionInfo(&info, "Send Open failed", "Active");
            return transit<Active>();
        }
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
            XmppConnectionInfo info;
            info.set_identifier(event.msg->from);
            if (state_machine->IsAuthEnabled()) {
                state_machine->SendConnectionInfo(&info, event.Name(),
                                                  "Open Confirm");
                return transit<OpenConfirm>();
            } else {
                connection->SendKeepAlive();
                connection->StartKeepAliveTimer();
                state_machine->StartHoldTimer();
                state_machine->SendConnectionInfo(&info, event.Name(),
                                                  "Established");
                return transit<XmppStreamEstablished>();
            }
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
        sc::custom_reaction<EvTcpClose>,
        sc::custom_reaction<EvHoldTimerExpired>,
        sc::custom_reaction<EvStreamFeatureRequest>, //received by client
        sc::custom_reaction<EvStartTls>,             //received by server
        sc::custom_reaction<EvTlsProceed>,           //received by client
        sc::custom_reaction<EvTlsHandShakeSuccess>,
        sc::custom_reaction<EvTlsHandShakeFailure>,
        sc::custom_reaction<EvXmppOpen>,             //received by server
        sc::custom_reaction<EvStop>
    > reactions;

    OpenConfirm(my_context ctx) : my_base(ctx) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "(Xmpp OpenConfirm)");
        state_machine->StartHoldTimer();
        XmppConnectionInfo info;
        if (!state_machine->IsActiveChannel()) { //server
            if (state_machine->IsAuthEnabled()) {
                XmppConnection *connection = state_machine->connection();
                XmppSession *session = state_machine->session();
                if (!connection->SendStreamFeatureRequest(session)) {
                    connection->SendClose(session);
                    SM_LOG(state_machine,
                        "Xmpp Send Stream Feature Request Failed, IDLE");
                    state_machine->ResetSession();
                    info.set_close_reason("Send Stream Feature Request Failed");
                    state_machine->connection()->set_close_reason(
                        "Send Stream Feature Request Failed");
                    state_machine->SendConnectionInfo(&info,
                        "Send Stream Feature Request failed", "Idle");
                    // cannot transition state as this is the constructor
                    // of new state
                }
            }
        }
        state_machine->set_state(OPENCONFIRM);
        state_machine->set_openconfirm_state(OPENCONFIRM_INIT);
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

    // received by the client
    sc::result react(const EvStreamFeatureRequest &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "EvStreamFeatureRequest in (OpenConfirm) State");
        XmppConnection *connection = state_machine->connection();
        XmppSession *session = state_machine->session();
        // TODO, we need to have a supported stream feature list
        // and compare against the requested stream feature list
        // which will enable us to send start of various features
        if (!connection->SendStartTls(session)) {
            connection->SendClose(session);
            state_machine->ResetSession();
            XmppConnectionInfo info;
            info.set_close_reason("Send Start Tls Failed");
            state_machine->SendConnectionInfo(&info, event.Name(), "Active");
            return transit<Active>();
        } else {
            state_machine->StartHoldTimer();
            state_machine->SendConnectionInfo(event.Name(),
                "Sent Start Tls, OpenConfirm Feature Negotiation");
            state_machine->set_openconfirm_state(
                       OPENCONFIRM_FEATURE_NEGOTIATION);
            return discard_event();
        }
    }

    // received by client
    sc::result react(const EvTlsProceed &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "EvTlsProceed in (OpenConfirm) State");
        XmppConnection *connection = state_machine->connection();
        XmppSession *session = state_machine->session();
        state_machine->StartHoldTimer();
        XmppConnectionInfo info;
        info.set_identifier(connection->GetTo());
        // Trigger Ssl Handshake on client side
        session->TriggerSslHandShake(state_machine->HandShakeCbHandler());
        state_machine->SendConnectionInfo( &info, event.Name(),
            "Trigger Client Ssl Handshake");
        return discard_event();
    }

    //received by server
    sc::result react(const EvStartTls &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        SM_LOG(state_machine, "EvStartTls in (OpenConfirm) State");
        XmppConnection *connection = state_machine->connection();
        XmppSession *session = state_machine->session();
        XmppConnectionInfo info;
        info.set_identifier(connection->GetTo());
        if (!connection->SendProceedTls(session)) {
            connection->SendClose(session);
            state_machine->ResetSession();
            info.set_close_reason("Send Proceed Tls Failed");
            state_machine->SendConnectionInfo(&info, event.Name(), "Idle");
            return transit<Idle>();
        } else {
            state_machine->StartHoldTimer();
            // Trigger Ssl Handshake on server side
            session->TriggerSslHandShake(state_machine->HandShakeCbHandler());
            state_machine->SendConnectionInfo(&info, event.Name(),
                "Trigger Server Ssl Handshake");
            return discard_event();
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

    sc::result react(const EvTlsHandShakeSuccess &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        SM_LOG(state_machine, "EvTlsHandShakeSuccess in (OpenConfirm) State");
        XmppConnection *connection = state_machine->connection();
        XmppSession *session = state_machine->session();
        session->AsyncReadStart();
        state_machine->set_openconfirm_state(OPENCONFIRM_FEATURE_SUCCESS);
        if (state_machine->IsActiveChannel()) { //client
            if (!connection->SendOpen(session)) {
                connection->SendClose(session);
                state_machine->ResetSession();
                XmppConnectionInfo info;
                info.set_close_reason("Open send failed in OpenConfirm State");
                state_machine->SendConnectionInfo(event.Name(), "Active");
                return transit<Active>();
            }
        }
        // both server and client
        state_machine->StartHoldTimer();
        state_machine->SendConnectionInfo(event.Name(),
            "OpenConfirm Feature Negotiation Success");
        return discard_event();
    }

    sc::result react(const EvTlsHandShakeFailure &event) {
       XmppStateMachine *state_machine = &context<XmppStateMachine>();
       if (event.session != state_machine->session()) {
           return discard_event();
       }
       SM_LOG(state_machine, "EvTlsHandShakeFailure in (TLSHandShake) State");
       // Do not send stream close as error occured at TLS layer
       state_machine->ResetSession();
       if (state_machine->IsActiveChannel()) { // client
           state_machine->SendConnectionInfo(event.Name(), "Active");
           return transit<Active>();
       } else {
           state_machine->SendConnectionInfo(event.Name(), "Idle");
           return transit<Idle>();
       }
    }

    //event on server and client
    sc::result react(const EvXmppOpen &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        SM_LOG(state_machine, "EvXmppOpen in (OpenConfirm) State");
        XmppConnectionInfo info;
        info.set_identifier(event.msg->from);
        XmppConnection *connection = state_machine->connection();
        XmppSession *session = state_machine->session();
        if (connection->IsActiveChannel()) { //client
            connection->SendKeepAlive();
            connection->StartKeepAliveTimer();
            state_machine->StartHoldTimer();
            state_machine->SendConnectionInfo(&info, event.Name(),
                                              "Established");
            return transit<XmppStreamEstablished>();
        } else { //server
            if (!connection->SendOpenConfirm(session)) {
                connection->SendClose(session);
                SM_LOG(state_machine,
                    "Xmpp Send Open Confirm Failed, IDLE");
                state_machine->ResetSession();
                info.set_close_reason("Send Open Confirm Failed");
                state_machine->connection()->set_close_reason(
                    "Send Open Confirm Failed");
                state_machine->SendConnectionInfo(&info, event.Name(), "Idle");
                return transit<Idle>();
            } else {
                connection->StartKeepAliveTimer();
                state_machine->StartHoldTimer();
                state_machine->SendConnectionInfo(&info, event.Name(),
                                                  "Established");
                return transit<XmppStreamEstablished>();
            }
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
        sc::custom_reaction<EvXmppMessageStanza>,
        sc::custom_reaction<EvXmppIqStanza>,
        sc::custom_reaction<EvHoldTimerExpired>,
        sc::custom_reaction<EvStop>
    > reactions;

    XmppStreamEstablished(my_context ctx) : my_base(ctx) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        SM_LOG(state_machine, "(XMPP Established)");
        XmppConnection *connection = state_machine->connection();
        state_machine->StartHoldTimer();
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
        state_machine->ResetSession();
        if (state_machine->IsActiveChannel()) {
            state_machine->SendConnectionInfo(event.Name(), "Active");
            return transit<Active>();
        } else {
            state_machine->SendConnectionInfo(event.Name(), "Idle");
            return transit<Idle>();
        }
    }

    sc::result react(const EvXmppKeepalive &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        state_machine->keepalive_count_inc();
        if (state_machine->get_keepalive_count() == 3) {
            state_machine->connect_attempts_clear();
        }
        state_machine->StartHoldTimer();
        return discard_event();
    }

    sc::result react(const EvXmppMessageStanza &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        SM_LOG(state_machine, "EvXmppMessageStanza in (Established) State");
        state_machine->StartHoldTimer();
        state_machine->connection()->ProcessXmppChatMessage(
            static_cast<const XmppStanza::XmppChatMessage *>(event.msg.get()));
        return discard_event();
    }

    sc::result react(const EvXmppIqStanza &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (event.session != state_machine->session()) {
            return discard_event();
        }
        SM_LOG(state_machine, "EvXmppIqStanza in (Established) State");
        state_machine->StartHoldTimer();
        state_machine->connection()->ProcessXmppIqMessage(
            static_cast<const XmppStanza::XmppMessage *>(event.msg.get()));
        return discard_event();
    }

    sc::result react(const EvHoldTimerExpired &event) {
        XmppStateMachine *state_machine = &context<XmppStateMachine>();
        if (state_machine->HoldTimerCancelled()) {
            SM_LOG(state_machine, "Discard EvHoldTimerExpired in (Established) State");
            return discard_event();
        }
        XMPP_NOTICE(XmppStateMachineNotice, state_machine->ChannelType(),
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
        SM_LOG(state_machine, "EvStop in (Established) State");
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

    if (!connection)
        return;

    // Stop keepalives, transition to IDLE and notify registered entities.
    connection->StopKeepAliveTimer();
    connection->ChannelMux()->HandleStateEvent(xmsm::IDLE);
    if (IsActiveChannel())
        return;

    // Retain the connection if graceful restart is supported.
    XmppServer *server = dynamic_cast<XmppServer *>(connection->server());
    if (!server->IsPeerCloseGraceful())
        connection->ManagedDelete();
}

XmppStateMachine::XmppStateMachine(XmppConnection *connection, bool active,
    bool auth_enabled)
    : work_queue_(TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"),
          connection->GetTaskInstance(),
          boost::bind(&XmppStateMachine::DequeueEvent, this, _1)),
      connection_(connection),
      session_(NULL),
      server_(connection->server()),
      connect_timer_(
          TimerManager::CreateTimer(*server_->event_manager()->io_service(),
          "Connect timer",
          TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"),
          connection->GetTaskInstance())),
      open_timer_(
          TimerManager::CreateTimer(*server_->event_manager()->io_service(),
          "Open timer",
          TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"),
          connection->GetTaskInstance())),
      hold_timer_(
          TimerManager::CreateTimer(*server_->event_manager()->io_service(),
          "Hold timer",
          TaskScheduler::GetInstance()->GetTaskId("xmpp::StateMachine"),
          connection->GetTaskInstance())),
      hold_time_(GetConfiguredHoldTime()),
      attempts_(0),
      keepalive_count_(0),
      deleted_(false),
      in_dequeue_(false),
      is_active_(active),
      auth_enabled_(auth_enabled),
      state_(xmsm::IDLE),
      last_state_(xmsm::IDLE),
      openconfirm_state_(xmsm::OPENCONFIRM_INIT) {
      handshake_cb_ = boost::bind(
          &XmppConnection::ProcessSslHandShakeResponse, connection, _1, _2);
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
        session_->ClearConnection();
        session_->Close();
        connection_->clear_session();
        session_ = NULL;
    }
}

void XmppStateMachine::DeleteSession(XmppSession *session) {
    if (session != NULL) {
        session->set_observer(NULL);
        session->ClearConnection();
        session->Close();
        Enqueue(xmsm::EvTcpDeleteSession(session));
    }
}

void XmppStateMachine::set_session(TcpSession *session) {
    if (session_ != NULL) {
        connection_->clear_session();
        DeleteSession(session_);
    }
    session_ = static_cast<XmppSession *>(session);
}

void XmppStateMachine::TimerErrorHandler(std::string name, std::string error) {
}

void XmppStateMachine::StartConnectTimer(int seconds) {
    CancelConnectTimer();

    // Add up to +/- kJitter percentage to reduce connection collisions.
    int ms = ((seconds)? seconds * 1000 : 50);
    ms = (ms * (100 - kJitter)) / 100;
    ms += (ms * (rand() % (kJitter * 2))) / 100;
    connect_timer_->Start(ms,
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

int XmppStateMachine::GetConfiguredHoldTime() const {
    static tbb::atomic<bool> env_checked = tbb::atomic<bool>();
    static tbb::atomic<int> env_hold_time = tbb::atomic<int>();

    // For testing only - configure through environment variable.
    if (!env_checked) {
        char *keepalive_time_str = getenv("XMPP_KEEPALIVE_SECONDS");
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

    // Use hard coded default.
    return kHoldTime;
}

void XmppStateMachine::StartHoldTimer() {
    CancelHoldTimer();

    if (hold_time_ <= 0)
        return;

    hold_timer_->Start(hold_time_ * 1000,
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
    XMPP_NOTICE(XmppEventLog, this->ChannelType(),
                 "Event: OpenTimer Expired ",
                 connection()->endpoint().address().to_string(),
                 connection()->GetTo());
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
    XMPP_NOTICE(XmppEventLog, this->ChannelType(),
                 "Event: HoldTimer Expired ",
                 connection()->endpoint().address().to_string(),
                 connection()->GetTo());
    Enqueue(xmsm::EvHoldTimerExpired());
    return false;
}

void XmppStateMachine::OnSessionEvent(
        TcpSession *session, TcpSession::Event event) {
    switch (event) {
    case TcpSession::ACCEPT:
        break;
    case TcpSession::CONNECT_COMPLETE:
        XMPP_NOTICE(XmppEventLog, this->ChannelType(),
                    "Event: Tcp Connected ",
                    connection()->endpoint().address().to_string(),
                    connection()->GetTo());
        Enqueue(xmsm::EvTcpConnected(static_cast<XmppSession *>(session)));
        break;
    case TcpSession::CONNECT_FAILED:
        XMPP_NOTICE(XmppEventLog, this->ChannelType(),
                    "Event: Tcp Connect Fail ",
                    connection()->endpoint().address().to_string(),
                    connection()->GetTo());
        Enqueue(xmsm::EvTcpConnectFail(static_cast<XmppSession *>(session)));
        connection_->inc_connect_error();
        break;
    case TcpSession::CLOSE:
        XMPP_NOTICE(XmppEventLog, this->ChannelType(),
                    "Event: Tcp Connection Closed ",
                    connection()->endpoint().address().to_string(),
                    connection()->GetTo());
        Enqueue(xmsm::EvTcpClose(static_cast<XmppSession *>(session)));
        connection_->inc_session_close();
        break;
    default:
        XMPP_WARNING(XmppUnknownEvent, this->ChannelType(), event);
        break;
    }
}

bool XmppStateMachine::PassiveOpen(XmppSession *session) {
    string state = "PassiveOpen in state: " + StateName();
    XMPP_NOTICE(XmppEventLog, this->ChannelType(), state,
                session->Connection()->endpoint().address().to_string(), "");
    return Enqueue(xmsm::EvTcpPassiveOpen(session));
}

// Process XmppStream header message received over a session. Close the stream
// if an old session is still present and undergoing graceful restart.
//
// Return true if msg is enqueued for further processing, false otherwise.
void XmppStateMachine::ProcessStreamHeaderMessage(XmppSession *session,
        const XmppStanza::XmppMessage *msg) {
    XmppConnectionManager *connection_manager =
        dynamic_cast<XmppConnectionManager *>(connection_->server());
    tbb::mutex::scoped_lock lock(connection_manager->mutex());

    // Update "To" information which can be used to map an older session
    session->Connection()->SetTo(msg->from);

    XmppServer *xmpp_server = dynamic_cast<XmppServer *>(server_);
    XmppConnectionEndpoint *endpoint = NULL;

    // Look for an endpoint which may already exist
    if (xmpp_server)
        endpoint = xmpp_server->FindConnectionEndpoint(connection_->ToString());

    // If older endpoint is present and is still associated with XmppConnection,
    // check if older connection is under graceful-restart.
    if (endpoint && endpoint->connection()) {
        if (connection_ != endpoint->connection()) {
            // Close new connection and retain old connection if the endpoint
            // IP addresses do not match.
            boost::asio::ip::address addr =
                endpoint->connection()->endpoint().address();
            if (connection_->endpoint().address() != addr) {
                XMPP_WARNING(XmppDeleteConnection,
                    "Drop new xmpp connection " + session->ToString() +
                    " as another connection with same name " + msg->from +
                    " but with different IP address " + addr.to_string() +
                    " already exists");
                ProcessEvent(xmsm::EvTcpClose(session));
                delete msg;
                return;
            }

            XmppChannelMux *channel = endpoint->connection()->ChannelMux();

            // If GR is not supported, then close all new connections until old
            // one is completely deleted. Even if GR is supported, new
            // connection cannot be accepted until old one is fully cleaned up.
            bool ready = channel->GetPeerState() == xmps::READY;
            if (!xmpp_server->IsPeerCloseGraceful() || ready ||
                    channel->ReceiverCount()) {

                // Bring down old session if it is still in ESTABLISHED state.
                // This is the scenario in which old session's TCP did not learn
                // the session down event, possibly due to compute cold reboot.
                // In that case, trigger closure (and possibly GR) process for
                // the old session.
                if (ready) {
                    XmppStateMachine *sm =
                        endpoint->connection()->state_machine();
                    XMPP_NOTICE(XmppDeleteConnection,
                               "Delete old xmpp connection " +
                               sm->session()->ToString() +
                               " as a new connection as been initiated");
                    sm->Enqueue(xmsm::EvTcpClose(sm->session()));
                }

                XMPP_NOTICE(XmppDeleteConnection,
                            "Drop new xmpp connection " + session->ToString() +
                            " as current connection is still not deleted");
                ProcessEvent(xmsm::EvTcpClose(session));
                delete msg;
                return;
            }
        }
    }

    // In all other cases, process the OpenMessage like it is normally done.
    ProcessEvent(xmsm::EvXmppOpen(session, msg));
}

void XmppStateMachine::OnMessage(XmppSession *session,
                                 const XmppStanza::XmppMessage *msg) {
    if (!Enqueue(xmsm::EvXmppMessage(session, msg)))
        delete msg;
}

void XmppStateMachine::ProcessMessage(XmppSession *session,
                                      const XmppStanza::XmppMessage *msg) {
    // Bail if session is already reset and disassociated from the connection.
    if (!session->Connection()) {
        delete msg;
        return;
    }

    const XmppStanza::XmppStreamMessage *stream_msg =
        static_cast<const XmppStanza::XmppStreamMessage *>(msg);

    switch (msg->type) {
        case XmppStanza::STREAM_HEADER:
            if (stream_msg->strmtype ==
                XmppStanza::XmppStreamMessage::FEATURE_TLS) {

                switch (stream_msg->strmtlstype) {
                    case (XmppStanza::XmppStreamMessage::TLS_FEATURE_REQUEST):
                        ProcessEvent(xmsm::EvStreamFeatureRequest(session,
                                                                  msg));
                        break;
                    case (XmppStanza::XmppStreamMessage::TLS_START):
                        ProcessEvent(xmsm::EvStartTls(session, msg));
                        break;
                    case (XmppStanza::XmppStreamMessage::TLS_PROCEED):
                        ProcessEvent(xmsm::EvTlsProceed(session, msg));
                        break;
                    default:
                        break;
                }

            } else if (stream_msg->strmtype ==
                    XmppStanza::XmppStreamMessage::INIT_STREAM_HEADER ||
                stream_msg->strmtype ==
                    XmppStanza::XmppStreamMessage::INIT_STREAM_HEADER_RESP)
                ProcessStreamHeaderMessage(session, msg);
            break;
        case XmppStanza::WHITESPACE_MESSAGE_STANZA:
            ProcessEvent(xmsm::EvXmppKeepalive(session, msg));
            break;
        case XmppStanza::IQ_STANZA:
            ProcessEvent(xmsm::EvXmppIqStanza(session, msg));
            break;
        case XmppStanza::MESSAGE_STANZA:
            ProcessEvent(xmsm::EvXmppMessageStanza(session, msg));
            break;
        default:
            if (!msg->IsValidType(msg->type)) {
                XMPP_NOTICE(XmppStateMachineUnsupportedMessage,
                            ChannelType(), (int)msg->type);
            }
            delete msg;
            break;
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

void  XmppStateMachine::OnEvent(SslSession *session,
                                xmsm::SslHandShakeResponse resp) {
    XmppSession *sess = static_cast<XmppSession *>(session);
    switch(resp) {
       case xmsm::EvTLSHANDSHAKE_FAILURE:
           Enqueue(xmsm::EvTlsHandShakeFailure(sess));
           break;
       case xmsm::EvTLSHANDSHAKE_SUCCESS:
           Enqueue(xmsm::EvTlsHandShakeSuccess(sess));
           break;
       default:
           break;
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


void XmppStateMachine::set_openconfirm_state(xmsm::XmOpenConfirmState state) {
    openconfirm_state_ = state;
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

xmsm::XmOpenConfirmState XmppStateMachine::OpenConfirmStateType() const {
    return openconfirm_state_;
}

void XmppStateMachine::AssignSession() {
    connection_->set_session(static_cast<XmppSession *>(session_));
}

const int XmppStateMachine::kConnectInterval;

int XmppStateMachine::GetConnectTime() const {
    int backoff = attempts_ > 6 ? 6 : attempts_;
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
        boost::intrusive_ptr<const sc::event_base> &event) {
    // Process message event and enqueue additional events as necessary.
    const xmsm::EvXmppMessage *ev_xmpp_message =
            dynamic_cast<const xmsm::EvXmppMessage *>(event.get());
    if (ev_xmpp_message) {
        ProcessMessage(ev_xmpp_message->session, ev_xmpp_message->msg);
    } else {
        ProcessEvent(*event);
        event.reset();
    }
    return true;
}

void XmppStateMachine::ProcessEvent(const sc::event_base &event) {
    const xmsm::EvTcpDeleteSession *deferred_delete =
            dynamic_cast<const xmsm::EvTcpDeleteSession *>(&event);
    if (deferred_delete) {
        TcpSession *session = deferred_delete->session;
        session->server()->DeleteSession(session);
        return;
    }
    if (deleted_) {
        return;
    }

    set_last_event(TYPE_NAME(event));
    in_dequeue_ = true;
    XMPP_UTDEBUG(XmppStateMachineDequeueEvent, ChannelType(), TYPE_NAME(event),
                 StateName(),
                 this->connection()->endpoint().address().to_string(),
                 this->connection()->GetTo());
    process_event(event);
    in_dequeue_ = false;
}

void XmppStateMachine::unconsumed_event(const sc::event_base &event) {
    XMPP_LOG(XmppUnconsumedEvent, ChannelType(),
            TYPE_NAME(event), StateName());
}

void XmppStateMachine::set_last_event(const std::string &event) {
    last_event_ = event;
    last_event_at_ = UTCTimestampUsec();

    if (!logUVE()) return;

    // Skip iq and message events.
    if (event == "xmsm::EvXmppIqStanza" ||
        event == "xmsm::EvXmppMessageStanza") {
        return;
    }

    // Skip keepalive events after we've reached established state.
    if (state_ == xmsm::ESTABLISHED && event == "xmsm::EvXmppKeepalive") {
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
// Return false if the event is not enqueued.
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

// Resurrect an old xmpp connection if present (when under GracefulRestart)
//
// During Graceful Restart (or otherwise), new connections are rejected in
// ProcessStreamHeaderMessage() itself until old one's cleanup process is
// complete and the system is ready to start a new session.
//
// Hence in here, when called upon receipt of OpenMessage, we can try to reuse
// old XmppConnection if present and there by complete any pending GR process
//
// We do so by reusing XmppConnection, XmppChannel, etc. from the old connection
// and only use the XmppSession and XmppStateMachine from the new session
//
// New connection is instead associated with the old state machine and session,
// and their deletion is triggered
void XmppStateMachine::ResurrectOldConnection(XmppConnection *new_connection,
                                              XmppSession *new_session) {

    // Look for an endpoint (which is a persistent data structure) across
    // xmpp session flips
    bool created;
    XmppConnectionEndpoint *connection_endpoint =
        static_cast<XmppServer *>(
            new_connection->server())->LocateConnectionEndpoint(
                static_cast<XmppServerConnection *>(new_connection), created);

    // If this is a new endpoint, then there is no older connection to manage.
    if (created)
        return;

    // GR Helper must be enabled when we are trying to resurrect connection.
    XmppServer *server = dynamic_cast<XmppServer *>(new_connection->server());
    assert(server->IsPeerCloseGraceful());

    XMPP_DEBUG(XmppCreateConnection, "Resurrect xmpp connection " +
               new_session->ToString());

    // Retrieve old XmppConnection and XmppStateMachine (to reuse)
    XmppConnection *old_xmpp_connection = connection_endpoint->connection();
    assert(old_xmpp_connection);

    XmppStateMachine *old_state_machine = old_xmpp_connection->state_machine();
    assert(old_state_machine);

    // Swap Old and New connections and state machines linkages
    new_connection->SwapXmppStateMachine(old_xmpp_connection);
    this->SwapXmppConnection(old_state_machine);

    // Update XmppConnection in the old session.
    XmppSession *old_xmpp_session = old_state_machine->session();
    if (old_xmpp_session)
        old_xmpp_session->SetConnection(new_connection);
    new_session->SetConnection(old_xmpp_connection);

    // Set new session with the old connection as it would be the current active
    // connection from now on.
    old_xmpp_connection->set_session(new_session);

    // Trigger deletion of the new connection which now is associated wth the
    // the old_state_machine
    new_connection->Shutdown();
}
