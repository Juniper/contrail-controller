/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_SESSION_MANAGER_H_
#define SRC_BGP_BGP_SESSION_MANAGER_H_

#include <boost/asio/ip/tcp.hpp>
#include <boost/dynamic_bitset.hpp>

#include "base/queue_task.h"
#include "io/tcp_server.h"
#include "net/address.h"

class BgpPeer;
class BgpServer;
class BgpSession;
class TcpSession;

class BgpSessionManager : public TcpServer {
public:
    BgpSessionManager(EventManager *evm, BgpServer *server);
    virtual ~BgpSessionManager();

    virtual TcpSession *CreateSession();
    virtual bool Initialize(unsigned short port);
    void Shutdown();
    void Terminate();
    bool MayDelete() const;

    void EnqueueWriteReady(BgpSession *session);

    BgpServer *server() { return server_; }

protected:
    virtual TcpSession *AllocSession(Socket *socket);
    virtual bool AcceptSession(TcpSession *session);

private:
    friend class BgpServerUnitTest;

    BgpPeer *FindPeer(Endpoint remote);
    bool ProcessSession(BgpSession *session);
    bool ProcessWriteReady(TcpSessionPtr tcp_session);
    void WorkQueueExitCallback(bool done);
    size_t GetSessionQueueSize() const;
    void SetSessionQueueDisable(bool disabled);

    BgpServer *server_;
    WorkQueue<BgpSession *> session_queue_;
    WorkQueue<TcpSessionPtr> write_ready_queue_;

    DISALLOW_COPY_AND_ASSIGN(BgpSessionManager);
};

#endif  // SRC_BGP_BGP_SESSION_MANAGER_H_
