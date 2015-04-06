/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __src_io_ssl_session_h__
#define __src_io_ssl_session_h__

#include "io/tcp_session.h"
#include "io/ssl_server.h"

class SslSession;
typedef boost::intrusive_ptr<SslSession> SslSessionPtr;
typedef boost::function<void(SslSessionPtr,
    const boost::system::error_code& error)> SslHandShakeCallbackHandler;

class SslSession : public TcpSession {
public:
    typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> SslSocket;

    // SslSession constructor takes ownership of socket.
    SslSession(SslServer *server, SslSocket *socket,
               bool async_read_ready = true);

    virtual Socket *socket() const;

    // Override to trigger handshake
    virtual bool Connected(Endpoint remote);

    // Override to trigger handshake
    virtual void Accepted();

    // Trigger delayed SslHandShake
    void TriggerSslHandShake(SslHandShakeCallbackHandler);

    // Additional states to determine the trigger of SSL handshake
    bool IsSslDisabled() {
        return (!ssl_enabled_);
    }

    bool IsSslHandShakeDelayed() {
        return ssl_handshake_delayed_;
    }

    bool IsSslHandShakeSuccess() {
        tbb::mutex::scoped_lock lock(mutex_);
        return ssl_handshake_success_;
    }

    bool IsSslHandShakeSuccessLocked() {
        return ssl_handshake_success_;
    }

    bool IsSslHandShakeInProgress() {
        tbb::mutex::scoped_lock lock(mutex_);
        return ssl_handshake_in_progress_;
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

    void AsyncReadSome(boost::asio::mutable_buffer buffer);
    std::size_t WriteSome(const uint8_t *data, std::size_t len,
                          boost::system::error_code &error);
    void AsyncWrite(const u_int8_t *data, std::size_t size);

    virtual Task* CreateReaderTask(boost::asio::mutable_buffer, size_t);

    static void SslHandShakeCallback(SslHandShakeCallbackHandler cb,
        SslSessionPtr, const boost::system::error_code &error);

    void SetSslHandShakeSuccess() {
        tbb::mutex::scoped_lock lock(mutex_);
        ssl_handshake_success_ = true;
    }

    void SetSslHandShakeFailure() {
        tbb::mutex::scoped_lock lock(mutex_);
        ssl_handshake_success_ = false;
    }

    boost::scoped_ptr<SslSocket> ssl_socket_;

    /**************** protected by mutex_ *************************/
    bool ssl_handshake_in_progress_;  // ssl handshake ongoing
    bool ssl_handshake_success_;      // ssl handshake success
    /**************** end protected by mutex_ *********************/

    /**************** config knobs ********************************/
    bool ssl_enabled_;               // default true
    bool ssl_handshake_delayed_;     // default false
    /**************************************************************/

    DISALLOW_COPY_AND_ASSIGN(SslSession);
};

#endif  // __src_io_ssl_session_h__
