/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_IO_SSL_SESSION_H_
#define SRC_IO_SSL_SESSION_H_

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

    void SetSslHandShakeInProgress(bool state) {
       tbb::mutex::scoped_lock lock(mutex_);
       ssl_handshake_in_progress_ = state;
    }

    static bool IsSocketErrorHard(const boost::system::error_code &ec);
protected:
    virtual ~SslSession();

private:
    class SslReader;
    friend class SslServer;

    // SslSession do actual ssl socket read for data in this context with
    // session mutex held, to avoid concurrent read and write operations
    // on same socket.
    size_t ReadSome(boost::asio::mutable_buffer buffer,
                    boost::system::error_code *error);
    std::size_t WriteSome(const uint8_t *data, std::size_t len,
                          boost::system::error_code *error);
    void AsyncWrite(const u_int8_t *data, std::size_t size);

    static void TriggerSslHandShakeInternal(SslSessionPtr ptr,
                                            SslHandShakeCallbackHandler cb);

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
    virtual size_t GetReadBufferSize() const;
    virtual void AsyncReadSome();

    boost::scoped_ptr<SslSocket> ssl_socket_;

    /**************** protected by mutex_ *************************/
    bool ssl_handshake_in_progress_;  // ssl handshake ongoing
    bool ssl_handshake_success_;      // ssl handshake success
    /**************** end protected by mutex_ *********************/

    /**************** config knobs ********************************/
    bool ssl_enabled_;               // default true
    bool ssl_handshake_delayed_;     // default false
    /**************************************************************/

    size_t ssl_last_read_len_;       // data len of the last read done

    DISALLOW_COPY_AND_ASSIGN(SslSession);
};

#endif  // SRC_IO_SSL_SESSION_H_
