/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "io/tcp_message_write.h"

#include "base/util.h"
#include "base/logging.h"
#include "io/tcp_session.h"
#include "io/io_log.h"

using namespace boost::asio;
using namespace boost::system;
using tbb::mutex;

TcpMessageWriter::TcpMessageWriter(Socket *socket, TcpSession *session) :
    socket_(socket), offset_(0), session_(session) {
}

TcpMessageWriter::~TcpMessageWriter() {
    for (BufferQueue::iterator iter = buffer_queue_.begin();
         iter != buffer_queue_.end(); ++iter) {
        DeleteBuffer(*iter);
    }
    buffer_queue_.clear();
}

int TcpMessageWriter::Send(const uint8_t *data, size_t len, error_code &ec) {
    int wrote = 0;

    // Update socket write call statistics.
    session_->stats_.write_calls++;
    session_->stats_.write_bytes += len;

    session_->server_->stats_.write_calls++;
    session_->server_->stats_.write_bytes += len;

    if (buffer_queue_.empty()) {
        wrote = socket_->write_some(boost::asio::buffer(data, len), ec);
        if (TcpSession::IsSocketErrorHard(ec)) return -1;
        assert(wrote >= 0);

        if ((size_t)wrote != len) {
            TCP_SESSION_LOG_UT_DEBUG(session_, TCP_DIR_OUT,
                "Encountered partial send of " << wrote << " bytes when "
                "sending " << len << " bytes, Error: " << ec);
            BufferAppend(data + wrote, len - wrote);
            DeferWrite();
        }
    } else {
        TCP_SESSION_LOG_UT_DEBUG(session_, TCP_DIR_OUT,
            "Write not ready. Enqueue buffer (len = " << len << ") and return");
        BufferAppend(data, len);
    }
    return wrote;
}

void TcpMessageWriter::DeferWrite() {

    // Update socket write block count.
    session_->stats_.write_blocked++;
    session_->server_->stats_.write_blocked++;
    socket_->async_write_some(
        boost::asio::null_buffers(), 
        boost::bind(&TcpMessageWriter::HandleWriteReady, this,
                    TcpSessionPtr(session_),
                    placeholders::error, UTCTimestampUsec()));
    return;
}

// Socket is ready for write. Flush any pending data and notify 
// clients aboout it.
void TcpMessageWriter::HandleWriteReady(TcpSessionPtr session_ptr,
                                        const error_code &error,
                                        uint64_t block_start_time) {
    mutex::scoped_lock lock(session_->mutex());

    // Update socket write block time.
    uint64_t blocked_usecs = UTCTimestampUsec() - block_start_time;
    session_->stats_.write_blocked_duration_usecs += blocked_usecs;
    session_->server_->stats_.write_blocked_duration_usecs += blocked_usecs;

    if (TcpSession::IsSocketErrorHard(error)) {
        goto done;
    }

    //
    // Ignore if connection is already closed.
    //
    if (session_->IsClosedLocked()) return;

    while (!buffer_queue_.empty()) {
        boost::asio::mutable_buffer head = buffer_queue_.front();
        const uint8_t *data = buffer_cast<const uint8_t *>(head) + offset_;
        int remaining = buffer_size(head) - offset_;
        error_code ec;
        int wrote = socket_->write_some(buffer(data, remaining), ec);
        if (TcpSession::IsSocketErrorHard(ec)) {
            lock.release();
            if (!cb_.empty()) cb_(ec);
            return;
        }
        assert(wrote >= 0);
        if (wrote != remaining) {
            offset_ += wrote;
            DeferWrite();
            return;
        } else {
            offset_ = 0;
            DeleteBuffer(head);
            buffer_queue_.pop_front();
        }
    }
    buffer_queue_.clear();

done:
    lock.release();
    // The session object is implicitly accessed in by cb_. This is
    // safe because this function currently holds a refcount on the session
    // via TcpSessionPtr.
    if (!cb_.empty()) {
        cb_(error);
    }
    return;
}

void TcpMessageWriter::BufferAppend(const uint8_t *src, int bytes) {
    u_int8_t *data = new u_int8_t[bytes];
    memcpy(data, src, bytes);
    mutable_buffer buffer = mutable_buffer(data, bytes);
    buffer_queue_.push_back(buffer);
}

void TcpMessageWriter::DeleteBuffer(mutable_buffer buffer) {
    const uint8_t *data = buffer_cast<const uint8_t *>(buffer);
    delete[] data;
    return;
}

void TcpMessageWriter::RegisterNotification(SendReadyCb cb) {
    cb_ = cb;
}
