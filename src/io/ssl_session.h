/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __src_io_ssl_session_h__
#define __src_io_ssl_session_h__

#include "io/tcp_session.h"
#include "io/ssl_server.h"

class SslSession : public TcpSession {
public:
    typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> SslSocket;

    // SslSession constructor takes ownership of socket.
    SslSession(SslServer *server, SslSocket *socket,
               bool async_read_ready = true, bool ssl_handshake_delayed = false);

    virtual Socket *socket() const;

    // Override to trigger handshake
    virtual bool Connected(Endpoint remote);

    // Override to trigger handshake
    virtual void Accepted();

    // Additional states to determine the trigger of SSL handshake
    bool IsSslHandShakeDelayed() {
        return ssl_handshake_delayed_;
    }

protected:
    virtual ~SslSession();

private:
    static void AcceptHandShakeHandler(TcpSessionPtr session,
                                       const boost::system::error_code& error);
    static void ConnectHandShakeHandler(TcpSessionPtr session, Endpoint remote,
                                        const boost::system::error_code& error);

    void AsyncReadSome(boost::asio::mutable_buffer buffer);
    std::size_t WriteSome(const uint8_t *data, std::size_t len,
                          boost::system::error_code &error);
    void AsyncWrite(const u_int8_t *data, std::size_t size);

    boost::scoped_ptr<SslSocket> ssl_socket_;
    bool ssl_handshake_delayed_; 

    DISALLOW_COPY_AND_ASSIGN(SslSession);
};

#endif  // __src_io_ssl_session_h__
