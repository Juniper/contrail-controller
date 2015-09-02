/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "xmpp/xmpp_connection_manager.h"

#include <boost/bind.hpp>

#include "base/lifetime.h"
#include "base/task_annotations.h"
#include "xmpp/xmpp_session.h"

//
// Constructor for XmppConnectionManager.
//
XmppConnectionManager::XmppConnectionManager(EventManager *evm,
    boost::asio::ssl::context::method m,
    bool ssl_enabled, bool ssl_handshake_delayed)
    : SslServer(evm, m, ssl_enabled, ssl_handshake_delayed),
      session_queue_(TaskScheduler::GetInstance()->GetTaskId("bgp::Config"),
          0, boost::bind(&XmppConnectionManager::DequeueSession, this, _1)) {
}

//
// Shutdown the XmppConnectionManager.
//
// Register an exit callback to the session WorkQueue so that we can retry
// deletion when the WorkQueue becomes empty.
//
void XmppConnectionManager::Shutdown() {
    TcpServer::Shutdown();
    session_queue_.SetExitCallback(
        boost::bind(&XmppConnectionManager::WorkQueueExitCallback, this, _1));
    session_queue_.Shutdown();
}

//
// Concurrency: called in the context of io thread.
//
// Add a XmppSession to the queue of write ready sessions.
// Take a reference to make sure that XmppSession doesn't get deleted before
// it's processed.
//
void XmppConnectionManager::EnqueueSession(XmppSession *session) {
    if (deleter()->IsDeleted())
        return;
    session_queue_.Enqueue(TcpSessionPtr(session));
}

//
// Concurrency: called in the context of bgp::Config task.
//
// Handler for XmppSessions that are dequeued from the session WorkQueue.
//
// The Xmpp[Client|Server] doesn't get destroyed if the WorkQueue is non-empty.
//
bool XmppConnectionManager::DequeueSession(TcpSessionPtr tcp_session) {
    CHECK_CONCURRENCY("bgp::Config");
    XmppSession *session = static_cast<XmppSession *>(tcp_session.get());
    session->ProcessWriteReady();
    return true;
}

//
// Exit callback for the session WorkQueue.
//
void XmppConnectionManager::WorkQueueExitCallback(bool done) {
    CHECK_CONCURRENCY("bgp::Config");
    if (!deleter()->IsDeleted())
        return;
    deleter()->RetryDelete();
}

//
// Return size of WorkQueue of write ready XmppSessions.
//
size_t XmppConnectionManager::GetSessionQueueSize() const {
    CHECK_CONCURRENCY("bgp::Config");
    return session_queue_.Length();
}
