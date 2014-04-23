/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __BGP_SESSION_MANAGER_H__
#define __BGP_SESSION_MANAGER_H__

#include <boost/asio/ip/tcp.hpp>
#include <boost/dynamic_bitset.hpp>

#include "io/tcp_server.h"
#include "bgp/bgp_peer_key.h"
#include "net/address.h"

class BgpServer;
class BgpPeer;
class TcpSession;

class BgpSessionManager : public TcpServer {
public:
    BgpSessionManager(EventManager *evm, BgpServer *);
    virtual ~BgpSessionManager();

    virtual TcpSession *CreateSession();

    virtual bool Initialize(short port);

    BgpServer *server() {
        return server_;
    }

protected:
    virtual TcpSession *AllocSession(Socket *socket);
    virtual bool AcceptSession(TcpSession *session);

private:
    friend class BgpSessionManagerTest;

    //back-pointer 
    BgpServer *server_;
    BgpPeer *FindPeer(boost::asio::ip::tcp::endpoint remote_endpoint);

    DISALLOW_COPY_AND_ASSIGN(BgpSessionManager);
};

#endif // __BGP_SESSION_MANAGER_H__
