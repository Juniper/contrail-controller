/*
 *  Copyright (c) 2014 Codilime
 */

#include "analytics/tcp_output_stream.h"

#include <sstream>
#include <boost/bind.hpp>
#include <boost/statechart/state_machine.hpp>
#include <boost/statechart/simple_state.hpp>
#include <pugixml/pugixml.hpp>

#include "analytics/stream_manager.h"
#include "io/event_manager.h"
#include "base/logging.h"

namespace sc = boost::statechart;
using analytics::TCPOutputStream;

TCPOutputStream::TCPOutputStream(EventManager *evm,
  boost::asio::ip::tcp::endpoint endp)
: // base class constructor with task name as an arg
  OutputStreamHandler("TCPOutputStream"),
  state_machine_(this, evm),
  evm_(evm),
  endpoint_(endp),
  session_mgr_(evm, this),
  session_(static_cast<OutputStreamSession *>(session_mgr_.CreateSession())) { 
      state_machine_.initiate();
      state_machine_.process_event(EvConnect());
}

bool TCPOutputStream::ProcessMessage(const SandeshStreamData &msg) {
    printf("TCPOutputStream: ProcessMessage\n");

    std::stringstream ostr;
    msg.xml_doc.print(ostr, "",
        pugi::format_raw |
        pugi::format_no_declaration |
        pugi::format_no_escapes);
    return SendRaw((const u_int8_t *)(ostr.str().c_str()), ostr.str().size());
}

TCPOutputStream::OutputStreamSession::OutputStreamSession(
    TcpServer *mgr, Socket *socket, TCPOutputStream *owner)
: TcpSession(mgr, socket),
  owner_(owner) {
    set_observer(boost::bind(&OutputStreamSession::OnEvent, this, _1, _2));
}

void TCPOutputStream::OutputStreamSession::OnEvent(TcpSession *session,
                                                        Event event) {
    switch (event) {
        case CONNECT_COMPLETE:
            owner_->state_machine_.process_event(EvConnectCompleted());
            printf("TCPOutputStream: CONNECT_COMPLETE\n");
            break;
        case CONNECT_FAILED:
            owner_->state_machine_.process_event(EvConnectFailed());
            printf("TCPOutputStream: CONNECT_FAILED");
            break;
        case CLOSE:
            owner_->state_machine_.process_event(EvClosed());
            printf("TCPOutputStream: CLOSE");
            break;
        default:
            break;
    }
}

void TCPOutputStream::OutputStreamSession::WriteReady(
                const boost::system::error_code &ec) {
    // Dequeue one or more entries from WorkQueue, if it's not already
    // happening.
    if (!ec)
        owner_->output_workqueue_.MayBeStartRunner();
}

void TCPOutputStream::Connect() {
    session_mgr_.Connect(session_.get(), endpoint_);
}

bool TCPOutputStream::SendRaw(const u_int8_t *data, size_t len) {
    return session_->Send(data, len, NULL);
}

TCPOutputStream::ConnectionState::ConnectionState(
    TCPOutputStream *owner, EventManager *evm)
: owner(owner), evm(evm) {}

TCPOutputStream::SessionManager::SessionManager(EventManager *evm,
                                        TCPOutputStream *owner)
: TcpServer(evm),
  owner_(owner) {}

TcpSession *TCPOutputStream::SessionManager::AllocSession(Socket *socket) {
    return new OutputStreamSession(this, socket, owner_);
}

TCPOutputStream::Connecting::Connecting(my_context ctx)
: my_base(ctx) {
    context<ConnectionState>().owner->Connect();
}

TCPOutputStream::Reconnect::Reconnect(my_context ctx)
: my_base(ctx),
  reconnect_timer(*context<ConnectionState>().evm->io_service(), 
    "TCPOutputStreamReconnectTimer",
    context<ConnectionState>().owner->task_id_,
    1) {
    printf("TCPOutputStream: RECONNECT SCHEDULE TIMER");
    reconnect_timer.Start(100,
        boost::bind(&Reconnect::TimeoutCallback, this));
}

bool TCPOutputStream::Reconnect::TimeoutCallback() {
   printf("TCPOutputStream: RECONNECT TIMEOUT");
   reconnect_timer.Cancel();
   post_event(EvReconnectTimeout());
   return true;
}

TCPOutputStream::Connected::Connected(my_context ctx)
: my_base(ctx) {
    printf("TCPOutputStream: CONNECTED");
    ConnectionState &state_machine = context<ConnectionState>();
    state_machine.owner->output_workqueue_.MayBeStartRunner();
}
