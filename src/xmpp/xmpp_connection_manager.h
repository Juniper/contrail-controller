/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef XMPP_XMPP_CONNECTION_MANAGER_H
#define XMPP_XMPP_CONNECTION_MANAGER_H

#include "base/queue_task.h"
#include "io/ssl_server.h"

class LifetimeActor;
class XmppSession;

//
// Common class to represent XmppClient and XmppServer
//
class XmppConnectionManager : public SslServer {
public:
    XmppConnectionManager(EventManager *evm,
        boost::asio::ssl::context::method m,
        bool ssl_enabled, bool ssl_handshake_delayed);

    void Shutdown();
    void EnqueueSession(XmppSession *session);
    size_t GetSessionQueueSize() const;
    virtual LifetimeActor *deleter() = 0;
    tbb::mutex &mutex() const { return mutex_; }

private:
    bool DequeueSession(TcpSessionPtr tcp_session);
    void WorkQueueExitCallback(bool done);

    WorkQueue<TcpSessionPtr> session_queue_;
    mutable tbb::mutex mutex_;

    DISALLOW_COPY_AND_ASSIGN(XmppConnectionManager);
};

#endif  // XMPP_XMPP_CONNECTION_MANAGER_H
