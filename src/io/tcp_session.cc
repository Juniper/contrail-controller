/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "io/tcp_session.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/scoped_array.hpp>
#include <boost/asio/detail/socket_option.hpp>

#include "base/logging.h"
#include "base/task.h"
#include "base/util.h"
#include "io/event_manager.h"
#include "io/tcp_server.h"
#include "io/tcp_message_write.h"
#include "io/io_utils.h"
#include "io/io_log.h"

using namespace boost::asio;
using namespace boost::system;
using namespace std;

int TcpSession::reader_task_id_ = -1;

class TcpSession::Reader : public Task {
public:
    typedef boost::function<void(Buffer)> ReadHandler;

    Reader(TcpSessionPtr session, ReadHandler read_fn, Buffer buffer) 
        : Task(session->reader_task_id(), session->GetSessionInstance()), 
          session_(session), read_fn_(read_fn), buffer_(buffer) {
    }
    virtual bool Run() {
        if (session_->IsEstablished()) {
            read_fn_(buffer_);
            session_->AsyncReadStart();
        }
        return true;
    }
private:
    TcpSessionPtr session_;
    ReadHandler read_fn_;
    Buffer buffer_;
};

TcpSession::TcpSession(
    TcpServer *server, Socket *socket, bool async_read_ready)
    : server_(server),
      socket_(socket),
      read_on_connect_(async_read_ready),
      buffer_size_(kDefaultBufferSize),
      established_(false),
      closed_(false),
      direction_(ACTIVE),
      writer_(new TcpMessageWriter(socket, this)) {
    refcount_ = 0;
    writer_->RegisterNotification(
                 boost::bind(&TcpSession::WriteReadyInternal, this, _1));
    if (reader_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        reader_task_id_ = scheduler->GetTaskId("io::ReaderTask");
    }
    name_ = "-";
}

TcpSession::~TcpSession() {
    assert(!established_);
    for (BufferQueue::iterator iter = buffer_queue_.begin();
         iter != buffer_queue_.end(); ++iter) {
        DeleteBuffer(*iter);
    }
    buffer_queue_.clear();
}

mutable_buffer TcpSession::AllocateBuffer() {
    u_int8_t *data = new u_int8_t[buffer_size_];
    mutable_buffer buffer = mutable_buffer(data, buffer_size_);
    {
        tbb::mutex::scoped_lock lock(mutex_);
        buffer_queue_.push_back(buffer);
    }
    return buffer;
}

void TcpSession::DeleteBuffer(mutable_buffer buffer) {
    uint8_t *data = buffer_cast<uint8_t *>(buffer);
    delete[] data;
}

static int BufferCmp(const mutable_buffer &lhs, const const_buffer &rhs) {
    const uint8_t *lp = buffer_cast<uint8_t *>(lhs);
    const uint8_t *rp = buffer_cast<const uint8_t *>(rhs);
    if (lp < rp) {
        return -1;
    }
    if (lp > rp) {
        return 1;
    }
    return 0;
}

void TcpSession::ReleaseBuffer(Buffer buffer) {
    tbb::mutex::scoped_lock lock(mutex_);
    ReleaseBufferLocked(buffer);
}

void TcpSession::ReleaseBufferLocked(Buffer buffer) {
    for (BufferQueue::iterator iter = buffer_queue_.begin();
         iter != buffer_queue_.end(); ++iter) {
        if (BufferCmp(*iter, buffer) == 0) {
            DeleteBuffer(*iter);
            buffer_queue_.erase(iter);
            return;
        }
    }
    assert(false);
}

void TcpSession::AsyncReadStart() {
    mutable_buffer buffer = AllocateBuffer();
    tbb::mutex::scoped_lock lock(mutex_);
    if (!established_) {
        ReleaseBufferLocked(buffer);
        return;
    }
    socket_->async_read_some(mutable_buffers_1(buffer),
        boost::bind(&TcpSession::AsyncReadHandler, TcpSessionPtr(this), buffer,
                    boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
}

TcpSession::Endpoint TcpSession::local_endpoint() const {
    tbb::mutex::scoped_lock lock(mutex_);
    if (!established_) {
        return Endpoint();
    }
    boost::system::error_code err;
    Endpoint local = socket_->local_endpoint(err);
    if (err) {
        return Endpoint();
    }
    return local;
}

void TcpSession::set_observer(EventObserver observer) {
    tbb::mutex::scoped_lock lock(obs_mutex_);
    observer_ = observer;
}

void TcpSession::SetName() {
    std::ostringstream out;
    boost::system::error_code err;
    Endpoint local;

    local = socket_->local_endpoint(err);
    out << local.address().to_string() << ":" << local.port() << "::";
    out << remote_.address().to_string() << ":" << remote_.port();

    name_ = out.str();
}

void TcpSession::SessionEstablished(Endpoint remote,
                                    Direction direction) {
    established_ = true;
    remote_ = remote;
    direction_ = direction;
    SetName();
}

bool TcpSession::Connected(Endpoint remote) {
    assert(refcount_);
    {
        tbb::mutex::scoped_lock lock(mutex_);
        if (closed_) {
            return false;
        }
        SessionEstablished(remote, TcpSession::ACTIVE);
    }

    TCP_SESSION_LOG_DEBUG(this, TCP_DIR_IN,
                          "Active session connection complete");

    {
        tbb::mutex::scoped_lock obs_lock(obs_mutex_);
        if (observer_) {
            observer_(this, CONNECT_COMPLETE);
        }
    }

    if (read_on_connect_) {
        AsyncReadStart();
    }
    return true;
}

void TcpSession::ConnectFailed() {
    tbb::mutex::scoped_lock obs_lock(obs_mutex_);
    if (observer_) {
        observer_(this, CONNECT_FAILED);
    }
}

// Requires: lock must not be held
void TcpSession::CloseInternal(bool call_observer, bool notify_server) {
    tbb::mutex::scoped_lock lock(mutex_);

    if (socket_.get() != NULL && !closed_) {
        boost::system::error_code err;
        socket_->close(err);
    }
    closed_ = true;
    if (!established_) {
        return;
    }
    established_ = false;

    // Take a reference through intrusive pointer to protect session from
    // possibly getting deleted from another thread.
    TcpSessionPtr session = TcpSessionPtr(this);
    lock.release();

    if (call_observer) {
        tbb::mutex::scoped_lock obs_lock(obs_mutex_);
        if (observer_) {
            observer_(this, CLOSE);
        }
    }

    if (notify_server) {
        server_->OnSessionClose(this);
    }
}

void TcpSession::Close() {
    CloseInternal(false);
}

// virtual method overriden in derrived classes.
void TcpSession::WriteReady(const boost::system::error_code &error) {
}

void TcpSession::WriteReadyInternal(const boost::system::error_code &error) {
    if (IsSocketErrorHard(error)) {
        TCP_SESSION_LOG_INFO(this, TCP_DIR_OUT, "Write failed due to error: "
                             << error.value()
                             << " category: " << error.category().name()
                             << " message: " << error.message());
        CloseInternal(true);
        return;
    }
    WriteReady(error);
}

void TcpSession::AsyncWriteHandler(TcpSessionPtr session,
                                   const boost::system::error_code &error) {
    if (session->IsSocketErrorHard(error)) {
        TCP_SESSION_LOG_INFO(session, TCP_DIR_OUT, "Write failed due to error: "
                             << error.message());
        session->CloseInternal(true);
        return;
    }
}

bool TcpSession::Send(const u_int8_t *data, size_t size, size_t *sent) {
    bool ret = true;
    tbb::mutex::scoped_lock lock(mutex_);

    // Reset sent, if provided.
    if (sent) *sent = 0;

    //
    // If the session closed in the mean while, bail out
    //
    if (!established_) return false;

    if (socket_->non_blocking()) {
        boost::system::error_code error;
        int len = writer_->Send(data, size, error);
        lock.release();
        if (len < 0) {
            TCP_SESSION_LOG_INFO(this, TCP_DIR_OUT,
                "Write failed due to error: " << error.category().name() << " "
                                              << error.message());
            CloseInternal(true);
            return false;
        }
        if (len < 0 || (size_t)len != size) ret = false;
        if (sent) *sent = (len > 0) ? len : 0;
    } else {
        boost::asio::async_write(
            *socket_.get(), buffer(data, size),
            boost::bind(&TcpSession::AsyncWriteHandler, TcpSessionPtr(this),
                        boost::asio::placeholders::error));
        if (sent) *sent = size;
    }
    return ret;
}

void TcpSession::AsyncReadHandler(
    TcpSessionPtr session, mutable_buffer buffer,
    const boost::system::error_code &error, size_t bytes_transferred) {

    tbb::mutex::scoped_lock lock(session->mutex_);
    if (session->closed_) {
        session->ReleaseBufferLocked(buffer);
        return;
    }

    if (IsSocketErrorHard(error)) {
        session->ReleaseBufferLocked(buffer);
        TCP_SESSION_LOG_UT_DEBUG(session, TCP_DIR_IN,
                                 "Read failed due to error " << error.value()
                                     << " : " << error.message());
        lock.release();
        session->CloseInternal(true);
        return;
    }

    // Update read statistics.
    session->stats_.read_calls++;
    session->stats_.read_bytes += bytes_transferred;
    session->server_->stats_.read_calls++;
    session->server_->stats_.read_bytes += bytes_transferred;

    Buffer rdbuf(buffer_cast<const uint8_t *>(buffer), bytes_transferred);
    Reader *task = new Reader(
        session, boost::bind(&TcpSession::OnRead, session.get(), _1), rdbuf);
    // Starting a new task for the session
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

int TcpSession::GetSessionInstance() const {
    return Task::kTaskInstanceAny;
}


int32_t TcpSession::local_port() const {
    if (socket_.get() == NULL) {
        return -1;
    }
    boost::system::error_code error;
    Endpoint local = socket_->local_endpoint(error);
    if (IsSocketErrorHard(error)) {
        return -1;
    }
    return local.port();
}

int32_t TcpSession::remote_port() const {
    if (socket_.get() == NULL) {
        return -1;
    }
    boost::system::error_code error;
    Endpoint remote = socket_->remote_endpoint(error);
    if (IsSocketErrorHard(error)) {
        return -1;
    }
    return remote.port();
}

TcpMessageReader::TcpMessageReader(TcpSession *session, 
                                   ReceiveCallback callback)
    : session_(session), callback_(callback), offset_(0), remain_(-1) {
}

TcpMessageReader::~TcpMessageReader() {
}

// Returns a buffer allocation size that is larger than the message.
int TcpMessageReader::AllocBufferSize(int length) {
    const int kMaxMessageSize = GetMaxMessageSize();
    if (length == -1) {
        return kMaxMessageSize;
    }
    int bufsize = 1 << 8;
    for (; bufsize < kMaxMessageSize && bufsize < length;
         bufsize <<= 1);
    return bufsize;
}

uint8_t *TcpMessageReader::BufferConcat(uint8_t *data, Buffer buffer,
                                        int msglength) {
    uint8_t *dst = data;

    while (!queue_.empty()) {
        Buffer head = queue_.front();
        const uint8_t *cp = TcpSession::BufferData(head) + offset_;
        int bytes = TcpSession::BufferSize(head) - offset_;
        assert((dst - data) + bytes < msglength);
        memcpy(dst, cp, bytes);
        dst += bytes;
        queue_.pop_front();
        session_->ReleaseBuffer(head);
        offset_ = 0;
        remain_ = -1;
    }

    int count = msglength - (dst - data);
    assert((dst - data) + count <= msglength);
    memcpy(dst, TcpSession::BufferData(buffer), count);
    offset_ = count;

    return data;
}

int TcpMessageReader::QueueByteLength() const {
    int total = 0;
    for (BufferQueue::const_iterator iter = queue_.begin();
         iter != queue_.end(); ++iter) {
        if (total == 0) {
            total = TcpSession::BufferSize(*iter) - offset_;
        } else {
            total += TcpSession::BufferSize(*iter);
        }
    }
    return total;
}

TcpMessageReader::Buffer TcpMessageReader::PullUp(
               uint8_t *data, Buffer buffer, size_t size) const {
    size_t offset = 0;

    for (BufferQueue::const_iterator iter = queue_.begin();
         iter != queue_.end(); ++iter) {
        const uint8_t *cp;
        int avail;
        if (offset == 0) {
            cp = TcpSession::BufferData(*iter) + offset_;
            avail = TcpSession::BufferSize(*iter) - offset_;
        } else {
            cp = TcpSession::BufferData(*iter);
            avail = TcpSession::BufferSize(*iter);
        }
        int remain = size - offset;
        avail = min(avail, remain);
        assert(offset + avail <= size);
        memcpy(data + offset, cp, avail);
        offset += avail;
    }

    int avail = TcpSession::BufferSize(buffer);
    int remain = size - offset;
    avail = min(avail, remain);
    assert(offset + avail <= size);
    memcpy(data + offset, TcpSession::BufferData(buffer), avail);
    offset += avail;

    if (offset < size) {
        return Buffer();
    }
    return Buffer(data, size);
}

// Read the socket stream and send messages to the peer object.
void TcpMessageReader::OnRead(Buffer buffer) {
    const int kHeaderLenSize = GetHeaderLenSize();
    const int kMaxMessageSize = GetMaxMessageSize();
    size_t size = TcpSession::BufferSize(buffer);
    TCP_SESSION_LOG_UT_DEBUG(session_, TCP_DIR_IN, "Read " << size << " bytes");

    if (!queue_.empty()) {
        int msglength = MsgLength(queue_.front(), offset_);
        if (msglength < 0) {
            int queuelen = QueueByteLength();
            if (queuelen + (int) size < kHeaderLenSize) {
                queue_.push_back(buffer);
                return;
            }
            boost::scoped_array<uint8_t> data(new uint8_t[kHeaderLenSize]);
            Buffer header = PullUp(data.get(), buffer, kHeaderLenSize);
            assert(TcpSession::BufferSize(header) == (size_t) kHeaderLenSize);

            msglength = MsgLength(header, 0);
            remain_ = msglength - queuelen;
        }
        if (msglength > kMaxMessageSize) {
            // TODO: post an error.
            return;
        }

        assert(remain_ > 0);
        if (size < (size_t) remain_) {
            queue_.push_back(buffer);
            remain_ -= size;
            return;
        }

        // concat the buffers into a contiguous message.
        boost::scoped_array<uint8_t>
            data(new uint8_t[AllocBufferSize(msglength)]);
        BufferConcat(data.get(), buffer, msglength);
        assert(remain_ == -1);
        // Receive the message
        callback_(data.get(), msglength);
    }

    int avail = size - offset_;
    while (avail > 0) {
        int msglength = MsgLength(buffer, offset_);
        if (msglength < 0) {
            break;
        }
        if (msglength > kMaxMessageSize) {
            // TODO: post an error.
            return;
        }
        if (msglength > avail) {
            remain_ = msglength - avail;
            break;
        }
        // Receive the message
        callback_(TcpSession::BufferData(buffer) + offset_, msglength);
        offset_ += msglength;
        avail -= msglength;
    }

    if (avail > 0) {
        queue_.push_back(buffer);
    } else {
        session_->ReleaseBuffer(buffer);
        offset_ = 0;
        assert(remain_ == -1);
    }
}

//
// Check if a socker error is hard and fatal. Only then should we close the
// socket. Soft errors like EINTR and EAGAIN should be ignored or properly
// handled with retries
//
bool TcpSession::IsSocketErrorHard(const boost::system::error_code &ec) {

    if (!ec) return false;

    if (ec == error::try_again) return false;
    if (ec == error::would_block) return false;
    if (ec == error::in_progress) return false;
    if (ec == error::interrupted) return false;
    if (ec == error::network_down) return false;
    if (ec == error::network_reset) return false;
    if (ec == error::network_unreachable) return false;
    if (ec == error::no_buffer_space) return false;

    return true;
}

boost::system::error_code TcpSession::SetSocketKeepaliveOptions(int keepalive_time,
        int keepalive_intvl, int keepalive_probes) {
    boost::system::error_code ec;
    socket_base::keep_alive keep_alive_option(true);
    socket_->set_option(keep_alive_option, ec);
    if (ec) {
        TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
                "keep_alive set error: " << ec);
        return ec;
    }
#ifdef TCP_KEEPIDLE
    typedef boost::asio::detail::socket_option::integer< IPPROTO_TCP, TCP_KEEPIDLE > keepalive_idle_time;
    keepalive_idle_time keepalive_idle_time_option(keepalive_time);
    socket_->set_option(keepalive_idle_time_option, ec);
    if (ec) {
        TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
                "keepalive_idle_time: " << keepalive_time << " set error: " << ec);
        return ec;
    }
#endif
#ifdef TCP_KEEPALIVE
    typedef boost::asio::detail::socket_option::integer< IPPROTO_TCP, TCP_KEEPALIVE > keepalive_idle_time;
    keepalive_idle_time keepalive_idle_time_option(keepalive_time);
    socket_->set_option(keepalive_idle_time_option, ec);
    if (ec) {
        TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
                "keepalive_idle_time: " << keepalive_time << " set error: " << ec);
        return ec;
    }
#endif
#ifdef TCP_KEEPINTVL
    typedef boost::asio::detail::socket_option::integer< IPPROTO_TCP, TCP_KEEPINTVL > keepalive_interval;
    keepalive_interval keepalive_interval_option(keepalive_intvl);
    socket_->set_option(keepalive_interval_option, ec);
    if (ec) {
        TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
                "keepalive_interval: " << keepalive_intvl << " set error: " << ec);
        return ec;
    }
#endif
#ifdef TCP_KEEPCNT
    typedef boost::asio::detail::socket_option::integer< IPPROTO_TCP, TCP_KEEPCNT > keepalive_count;
    keepalive_count keepalive_count_option(keepalive_probes);
    socket_->set_option(keepalive_count_option, ec);
    if (ec) {
        TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
                "keepalive_probes: " << keepalive_probes << " set error: " << ec);
        return ec;
    }
#endif
    return ec;
}

boost::system::error_code TcpSession::SetSocketOptions() {
    boost::system::error_code ec;

    //
    // Make socket write non-blocking
    //
    socket_->non_blocking(true, ec);
    if (ec) {
        TCP_SESSION_LOG_ERROR(this, TCP_DIR_NA,
                              "Cannot set socket non blocking: " << ec);
        return ec;
    }

    char *buffer_size_str = getenv("TCP_SESSION_SOCKET_BUFFER_SIZE");
    if (!buffer_size_str) return ec;

    unsigned long int sz = strtoul(buffer_size_str, NULL, 0);
    if (sz) {

        //
        // Set socket send and receive buffer size
        //
        // Currently used only under test environments to trigger partial
        // sends more deterministically
        //
        socket_base::send_buffer_size send_buffer_size_option(sz);
        socket_->set_option(send_buffer_size_option, ec);
        if (ec) {
            TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
                                  "send_buffer_size set error: " << ec);
            return ec;
        }

        socket_base::receive_buffer_size receive_buffer_size_option(sz);
        socket_->set_option(receive_buffer_size_option, ec);
        if (ec) {
            TCP_SESSION_LOG_ERROR(this, TCP_DIR_IN,
                                  "receive_buffer_size set error: " << ec);
            return ec;
        }
    }

    return ec;
}

void TcpSession::GetRxSocketStats(SocketIOStats &socket_stats) const {
    stats_.GetRxStats(socket_stats);
}

void TcpSession::GetTxSocketStats(SocketIOStats &socket_stats) const {
    stats_.GetTxStats(socket_stats);
}

void TcpSession::SetBufferSize(int buffer_size) {
    buffer_size_ = buffer_size;
}
