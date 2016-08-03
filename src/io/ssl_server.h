/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_IO_SSL_SERVER_H_
#define SRC_IO_SSL_SERVER_H_

#include <boost/asio/ssl.hpp>

#include "io/tcp_server.h"

class SslSession;

class SslServer : public TcpServer {
public:
    typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> SslSocket;

    explicit SslServer(EventManager *evm, boost::asio::ssl::context::method m,
                       bool ssl_enabled = true,
                       bool ssl_handshake_delayed = false);
    virtual ~SslServer();

protected:
    // given SSL socket, Create a session object.
    virtual SslSession *AllocSession(SslSocket *socket) = 0;

    // boost ssl context accessor to setup ssl context variables.
    boost::asio::ssl::context *context();

private:
    friend class SslSession;

    static void AcceptHandShakeHandler(TcpServerPtr server,
                                       TcpSessionPtr session,
                                       const boost::system::error_code& error);
    static void ConnectHandShakeHandler(TcpServerPtr server,
                                        TcpSessionPtr session,
                                        const boost::system::error_code& error);

    // suppress AllocSession method using tcp socket, not valid for
    // ssl server.
    TcpSession *AllocSession(Socket *socket) { return NULL; }

    TcpSession *AllocSession(bool server_session);

    // override accept complete handler to trigger handshake
    virtual void AcceptHandlerComplete(TcpSessionPtr session);

    // override connect complete handler to trigger handshake
    void ConnectHandlerComplete(TcpSessionPtr session);

    Socket *accept_socket() const;
    void set_accept_socket();

    boost::asio::ssl::context context_;
    std::auto_ptr<SslSocket> so_ssl_accept_;  // SSL socket used in async_accept
    bool ssl_enabled_;
    bool ssl_handshake_delayed_;
    DISALLOW_COPY_AND_ASSIGN(SslServer);
};

#endif  // SRC_IO_SSL_SERVER_H_
