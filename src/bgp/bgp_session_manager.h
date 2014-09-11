/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_SESSION_MANAGER_H__
#define __BGP_SESSION_MANAGER_H__

#include <boost/asio/ip/tcp.hpp>
#include <boost/dynamic_bitset.hpp>

#include "base/queue_task.h"
#include "bgp/bgp_peer_key.h"
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
    virtual bool Initialize(short port);
    void Shutdown();
    void Terminate();
    bool IsQueueEmpty() const { return session_queue_.IsQueueEmpty(); }

    BgpServer *server() { return server_; }

protected:
    virtual TcpSession *AllocSession(Socket *socket);
    virtual bool AcceptSession(TcpSession *session);

private:
    friend class BgpServerUnitTest;

    BgpPeer *FindPeer(Endpoint remote);
    bool ProcessSession(BgpSession *session);
    void ProcessSessionDone(bool done);
    size_t GetQueueSize() const;
    void SetQueueDisable(bool disabled);

    BgpServer *server_;
    WorkQueue<BgpSession *> session_queue_;

    DISALLOW_COPY_AND_ASSIGN(BgpSessionManager);
};

#endif // __BGP_SESSION_MANAGER_H__
