/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __src_io_ssl_server_h__
#define __src_io_ssl_server_h__

#include <boost/asio/ssl.hpp>

#include "io/tcp_server.h"

class SslSession;

class SslServer : public TcpServer {
public:
    typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> SslSocket;

    explicit SslServer(EventManager *evm, boost::asio::ssl::context::method m);
    virtual ~SslServer();

protected:
    // given SSL socket, Create a session object.
    virtual SslSession *AllocSession(SslSocket *socket) = 0;

    // boost ssl context accessor to setup ssl context variables.
    boost::asio::ssl::context *context();

private:
    // suppress AllocSession method using tcp socket, not valid for
    // ssl server.
    TcpSession *AllocSession(Socket *socket) { return NULL; }

    TcpSession *AllocSession(bool server_session);

    Socket *accept_socket() const;
    void set_accept_socket();

    boost::asio::ssl::context context_;
    std::auto_ptr<SslSocket> so_ssl_accept_;       // SSL socket used in async_accept
    DISALLOW_COPY_AND_ASSIGN(SslServer);
};

#endif  //__src_io_ssl_server_h__
