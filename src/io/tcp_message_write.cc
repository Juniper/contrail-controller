/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "io/tcp_message_write.h"

#include "base/util.h"
#include "base/logging.h"
#include "io/tcp_session.h"
#include "io/io_log.h"

using boost::asio::buffer;
using boost::asio::buffer_cast;
using boost::asio::mutable_buffer;
using boost::system::error_code;
using tbb::mutex;
using std::min;

const int TcpMessageWriter::kDefaultWriteBufferSize;
const int TcpMessageWriter::kMaxPendingBufferSize;
const int TcpMessageWriter::kMinPendingBufferSize;

TcpMessageWriter::TcpMessageWriter(TcpSession *session) :
    offset_(0), last_write_(0), session_(session) {
}

TcpMessageWriter::~TcpMessageWriter() {
    for (BufferQueue::iterator iter = buffer_queue_.begin();
         iter != buffer_queue_.end(); ++iter) {
        DeleteBuffer(*iter);
    }
    buffer_queue_.clear();
}

int TcpMessageWriter::Send(const uint8_t *data, size_t len, error_code *ec) {
    int wrote = 0;

    // Update socket write call statistics.
    session_->stats_.write_calls++;
    session_->stats_.write_bytes += len;

    session_->server_->stats_.write_calls++;
    session_->server_->stats_.write_bytes += len;

    if (buffer_queue_.empty()) {
        wrote = session_->WriteSome(data, len, ec);
        if (TcpSession::IsSocketErrorHard(*ec)) return -1;
        assert(wrote >= 0);

        if ((size_t)wrote != len) {
            TCP_SESSION_LOG_UT_DEBUG(session_, TCP_DIR_OUT,
                "Encountered partial send of " << wrote << " bytes when "
                "sending " << len << " bytes, Error: " << ec);
            BufferAppend(data + wrote, len - wrote);
            session_->DeferWriter();
        }
    } else {
        TCP_SESSION_LOG_UT_DEBUG(session_, TCP_DIR_OUT,
            "Write not ready. Enqueue buffer (len = " << len << ") and return");
        BufferAppend(data, len);
    }
    return wrote;
}

int TcpMessageWriter::AsyncSend(const uint8_t *data, size_t len, error_code *ec) {

    int write = len;

    if (buffer_queue_.empty()) {
        BufferAppend(data, len);
        if (session_->io_strand_) {
            session_->io_strand_->post(bind(&TcpSession::AsyncWriteInternal,
                                       session_, TcpSessionPtr(session_)));
        }
    } else {
        BufferAppend(data, len);
    }

    if ((buffer_queue_.size() - offset_) > TcpMessageWriter::kMaxPendingBufferSize) {
        /* throttle the sender */
        session_->stats_.write_blocked++;
        session_->server_->stats_.write_blocked++;
        session_->stats_.write_block_start_time = UTCTimestampUsec();
        write = 0;
    }

    return write;
}

void TcpMessageWriter::TriggerAsyncWrite() {

    /* Return if there is an async write in progress */
    assert(last_write_ == 0);
    assert(!buffer_queue_.empty());

    boost::asio::mutable_buffer head = buffer_queue_.front();
    const uint8_t *data = buffer_cast<const uint8_t *>(head) + offset_;
    int remaining = buffer_size(head) - offset_;
    int write  = min(TcpMessageWriter::kDefaultWriteBufferSize, remaining);
    last_write_ = write;

    // Update socket write call statistics.
    session_->stats_.write_calls++;
    session_->server_->stats_.write_calls++;

    session_->AsyncWrite(data, write);
}

bool TcpMessageWriter::FlushBuffer(size_t wrote, bool *send_ready) {

    assert(last_write_ == wrote);
    assert(!buffer_queue_.empty());

    bool more_write = true;
    last_write_ = 0;

    boost::asio::mutable_buffer head = buffer_queue_.front();
    if ((offset_ + wrote) == buffer_size(head)) {
        offset_ = 0;
        DeleteBuffer(head);
        buffer_queue_.pop_front();
    } else {
        offset_ += wrote;
    }

    if ((buffer_queue_.size() - offset_)  <
         TcpMessageWriter::kMinPendingBufferSize) {
        uint64_t blocked_usecs =  UTCTimestampUsec() -
                session_->stats_.write_block_start_time;
        session_->stats_.write_blocked_duration_usecs += blocked_usecs;
        session_->server_->stats_.write_blocked_duration_usecs += blocked_usecs;
        *send_ready = true;
    }

    if (buffer_queue_.empty()) {
        buffer_queue_.clear();
        more_write = false;
    }

    return more_write;
}


// Socket is ready for write. Flush any pending data
void TcpMessageWriter::HandleWriteReady(error_code *error) {
    while (!buffer_queue_.empty()) {
        boost::asio::mutable_buffer head = buffer_queue_.front();
        const uint8_t *data = buffer_cast<const uint8_t *>(head) + offset_;
        int remaining = buffer_size(head) - offset_;
        int wrote = session_->WriteSome(data, remaining, error);
        if (TcpSession::IsSocketErrorHard(*error)) {
            return;
        }
        assert(wrote >= 0);
        if (wrote != remaining) {
            offset_ += wrote;
            session_->DeferWriter();
            return;
        } else {
            offset_ = 0;
            DeleteBuffer(head);
            buffer_queue_.pop_front();
        }
    }
    buffer_queue_.clear();
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

