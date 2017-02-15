/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include "io/ssl_session.h"

#include <string>

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include "io/event_manager.h"
#include "io/io_log.h"
#include "io/io_utils.h"

using boost::asio::async_write;
using boost::asio::buffer;
using boost::asio::buffer_cast;
using boost::asio::mutable_buffer;
using boost::asio::mutable_buffers_1;
using boost::asio::null_buffers;
using boost::asio::placeholders::error;
using boost::asio::ssl::stream_base;
using boost::bind;
using boost::function;
using boost::system::error_code;
using std::size_t;
using std::srand;
using std::string;
using std::time;

class SslSession::SslReader : public Task {
public:
    typedef function<void(Buffer)> ReadHandler;

    SslReader(int task_id, SslSessionPtr session, ReadHandler read_fn,
              Buffer buffer)
       : Task(task_id, session->GetSessionInstance()),
         session_(session), read_fn_(read_fn), buffer_(buffer) {
    }
    virtual bool Run() {
        if (session_->IsEstablished()) {
            session_->ssl_last_read_len_ = BufferSize(buffer_);
            read_fn_(buffer_);
            if (session_->IsSslDisabled()) {
                session_->AsyncReadStart();
            } else if (!session_->IsSslHandShakeInProgress()) {
                session_->AsyncReadStart();
            }
        }
        return true;
    }
    string Description() const { return "SslSession::SslReader"; }

private:
    SslSessionPtr session_;
    ReadHandler read_fn_;
    Buffer buffer_;
};

SslSession::SslSession(SslServer *server, SslSocket *ssl_socket,
                       bool async_read_ready)
    : TcpSession(server, NULL, async_read_ready),
      ssl_socket_(ssl_socket),
      ssl_handshake_in_progress_(false),
      ssl_handshake_success_(false),
      ssl_enabled_(true),
      ssl_handshake_delayed_(false),
      ssl_last_read_len_(0) {

    if (server) {
        ssl_enabled_ = server->ssl_enabled_;
        ssl_handshake_delayed_ = server->ssl_handshake_delayed_;
    }
}

SslSession::~SslSession() {
}

Task* SslSession::CreateReaderTask(mutable_buffer buffer,
                                   size_t bytes_transferred) {
    Buffer rdbuf(buffer_cast<const uint8_t *>(buffer), bytes_transferred);
    SslReader *task = new SslReader(this->reader_task_id(),
        SslSessionPtr(this), bind(&SslSession::OnRead, this, _1), rdbuf);
    return (task);
}


TcpSession::Socket *SslSession::socket() const {
    if (ssl_socket_) {
        // return tcp socket
        return &ssl_socket_->next_layer();
    }
    return NULL;
}

// Register for data read notification from the tcp socket or from the ssl
// socket, as appropriate.
void SslSession::AsyncReadSome() {
    if (established()) {
        if (ssl_last_read_len_ == 0) {
            // we have drained the read buffer of the socket
            // register for a read notification from the tcp socket
            TcpSession::AsyncReadSome();
        } else {
            // trigger Async Read Handler for immediate read
            TriggerAsyncReadHandler();
        }
    }
}

// Tests with large data have consistently shown 16K as the maximum read data
// size even though when a lot more data was available in the underlying socket.
// Also, there is no available() api for the ssl socket.
size_t SslSession::GetReadBufferSize() const {
    return kDefaultBufferSize;
}

//
// Check if a socker error is hard and fatal. Only then should we close the
// socket. Soft errors like EINTR and EAGAIN should be ignored or properly
// handled with retries
//
bool SslSession::IsSocketErrorHard(const error_code &ec) {
    if (!ec)
        return false;
    if (ec.value() == boost::asio::error::try_again)
        return false;
    if (ec.value() == boost::asio::error::would_block)
        return false;
    if (ec.value() == boost::asio::error::in_progress)
        return false;
    if (ec.value() == boost::asio::error::interrupted)
        return false;
    if (ec.value() == boost::asio::error::network_down)
        return false;
    if (ec.value() == boost::asio::error::network_reset)
        return false;
    if (ec.value() == boost::asio::error::network_unreachable)
        return false;
    if (ec.value() == boost::asio::error::no_buffer_space)
        return false;
    if (ec.value() == ERR_PACK(ERR_LIB_SSL, 0, SSL_R_SHORT_READ))
        return false;

    return true;
}

size_t SslSession::ReadSome(mutable_buffer buffer, error_code *error) {
    // Read data from the tcp socket or from the ssl socket, as appropriate.
    assert(!ssl_handshake_in_progress_);
    if (!IsSslHandShakeSuccessLocked())
        return TcpSession::ReadSome(buffer, error);

    size_t rc = ssl_socket_->read_some(mutable_buffers_1(buffer), *error);
    if (IsSocketErrorHard(*error)) {
        // eof gets mapped to short read error 
        TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
                                  "Read failed due to error: "
                                  << error->category().name() << " "
                                  << error->message() << " " << error->value());
    }
    return rc;
}

size_t SslSession::WriteSome(const uint8_t *data, size_t len,
                             error_code *error) {
    if (IsSslHandShakeSuccessLocked()) {
        return ssl_socket_->write_some(buffer(data, len), *error);
    } else {
        return (TcpSession::WriteSome(data, len, error));
    }
}

void SslSession::AsyncWrite(const u_int8_t *data, size_t size) {
    if (IsSslHandShakeSuccessLocked()) {
        async_write(*ssl_socket_.get(), buffer(data, size),
                bind(&TcpSession::AsyncWriteHandler,
                TcpSessionPtr(this), error));
    } else {
        return (TcpSession::AsyncWrite(data, size));
    }
}

void SslSession::SslHandShakeCallback(SslHandShakeCallbackHandler cb,
                                      SslSessionPtr session,
                                      const error_code &error) {
    session->ssl_handshake_in_progress_ = false;
    if (!error) {
        session->SetSslHandShakeSuccess();
    } else {
        session->SetSslHandShakeFailure();
    }

    if (session->socket() != NULL && !(session->IsClosed())) {
        cb(session, error);
    }
}

void SslSession::TriggerSslHandShakeInternal(
        SslSessionPtr session, SslHandShakeCallbackHandler cb) {
    srand(time(0));
    error_code ec;
    session->ssl_handshake_in_progress_ = true;
    if (session->IsServerSession()) {
        session->ssl_socket_->async_handshake(stream_base::server,
            bind(&SslSession::SslHandShakeCallback, cb, session,
            error));
    } else {
        session->ssl_socket_->async_handshake(stream_base::client,
            bind(&SslSession::SslHandShakeCallback, cb, session,
                 error));
    }
}

void SslSession::TriggerSslHandShake(SslHandShakeCallbackHandler cb) {
    server()->event_manager()->io_service()->post(
        bind(&TriggerSslHandShakeInternal, SslSessionPtr(this), cb));
}
