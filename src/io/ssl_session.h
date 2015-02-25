/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __src_io_ssl_session_h__
#define __src_io_ssl_session_h__

#include "io/tcp_session.h"
#include "io/ssl_server.h"

class SslSession;
typedef boost::intrusive_ptr<SslSession> SslSessionPtr;

class SslSession : public TcpSession {
public:
    typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> SslSocket;

    // SslSession constructor takes ownership of socket.
    SslSession(SslServer *server, SslSocket *socket,
               bool async_read_ready = true, bool ssl_disabled = false,
               bool ssl_handshake_delayed = false);

    virtual Socket *socket() const;

    // Override to trigger handshake
    virtual bool Connected(Endpoint remote);

    // Override to trigger handshake
    virtual void Accepted();

    // Trigger delayed SslHandShake
    typedef boost::function<void(SslSessionPtr, 
        const boost::system::error_code& error)> SslHandShakeCallbackHandler;
    void TriggerServerSslHandShake(SslHandShakeCallbackHandler);
    void TriggerClientSslHandShake(SslHandShakeCallbackHandler);

    // Additional states to determine the trigger of SSL handshake
    bool IsSslDisabled() {
        return ssl_disabled_;
    }

    bool IsSslHandShakeDelayed() {
        return ssl_handshake_delayed_;
    }

    bool IsSslHandShakeSuccess() {
        return ssl_handshake_success_;
    }

    void SetSslHandShakeStatus(bool success) {
        tbb::mutex::scoped_lock lock(mutex_); 
        ssl_handshake_success_ = success;
    }

protected:
    virtual ~SslSession();

private:
    class SslReader;
    friend class Sslserver;
    static void AcceptHandShakeHandler(TcpSessionPtr session,
                                       const boost::system::error_code& error);
    static void ConnectHandShakeHandler(TcpSessionPtr session, Endpoint remote,
                                        const boost::system::error_code& error);

    static void AsyncReadHandler(SslSessionPtr session,
                                 boost::asio::mutable_buffer buffer,
                                 const boost::system::error_code &error,
                                 size_t size);

    void AsyncReadSome(boost::asio::mutable_buffer buffer);
    std::size_t WriteSome(const uint8_t *data, std::size_t len,
                          boost::system::error_code &error);
    void AsyncWrite(const u_int8_t *data, std::size_t size);

    virtual void OnRead(Buffer buffer) = 0;

    SslServerPtr server_;
    boost::scoped_ptr<SslSocket> ssl_socket_;

    /**************** protected by mutex_ *************************/ 
    bool ssl_handshake_success_;      // ssl handshake success
    /**************** end protected by mutex_ *********************/

    /**************** config knobs ********************************/
    bool ssl_disabled_;              // default false
    bool ssl_handshake_delayed_;     // default false
    /**************************************************************/ 

    DISALLOW_COPY_AND_ASSIGN(SslSession);
};

#endif  // __src_io_ssl_session_h__
