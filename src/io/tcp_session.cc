/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "io/tcp_session.h"

#include <algorithm>
#include <string>

#include <boost/asio.hpp>
#include <boost/asio/detail/socket_option.hpp>
#include <boost/bind.hpp>
#include <boost/scoped_array.hpp>

#include "base/logging.h"
#include "io/event_manager.h"
#include "io/io_log.h"
#include "io/io_utils.h"
#include "io/tcp_message_write.h"
#include "io/tcp_server.h"

using boost::asio::async_write;
using boost::asio::buffer;
using boost::asio::buffer_cast;
using boost::asio::detail::socket_option::integer;
using boost::asio::const_buffer;
using boost::asio::mutable_buffer;
using boost::asio::mutable_buffers_1;
using boost::asio::null_buffers;
using boost::asio::socket_base;
using boost::bind;
using boost::function;
using boost::scoped_array;
using boost::system::error_code;
using std::min;
using std::ostringstream;
using std::string;

using boost::asio::error::eof;
using boost::asio::error::try_again;
using boost::asio::error::would_block;
using boost::asio::error::in_progress;
using boost::asio::error::interrupted;
using boost::asio::error::network_down;
using boost::asio::error::network_reset;
using boost::asio::error::network_unreachable;
using boost::asio::error::no_buffer_space;
using boost::asio::placeholders::error;

int TcpSession::reader_task_id_ = -1;

class TcpSession::Reader : public Task {
public:
    typedef function<void(Buffer)> ReadHandler;

    Reader(TcpSessionPtr session, ReadHandler read_fn, Buffer buffer)
        : Task(session->reader_task_id(), session->GetSessionInstance()),
          session_(session), read_fn_(read_fn), buffer_(buffer) {
    }
    virtual bool Run() {
        if (session_->IsEstablished()) {
            read_fn_(buffer_);
            if (session_->IsReaderDeferred()) {
                // Update socket read block count.
                session_->stats_.read_block_start_time = UTCTimestampUsec();
                session_->stats_.read_blocked++;
                session_->server_->stats_.read_blocked++;
            } else {
                session_->AsyncReadStart();
            }
        }
        return true;
    }
    string Description() const { return "TcpSession::Reader"; }

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
      established_(false),
      closed_(false),
      direction_(ACTIVE),
      writer_(new TcpMessageWriter(this)),
      name_("-") {
    refcount_ = 0;
    if (reader_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        reader_task_id_ = scheduler->GetTaskId("io::ReaderTask");
    }
    if (server_) {
        io_strand_.reset(new Strand(*server->event_manager()->io_service()));
    }
    defer_reader_ = false;
}

TcpSession::~TcpSession() {
    assert(!established_);
    for (BufferQueue::iterator iter = buffer_queue_.begin();
         iter != buffer_queue_.end(); ++iter) {
        DeleteBuffer(*iter);
    }
    buffer_queue_.clear();
}

mutable_buffer TcpSession::AllocateBuffer(size_t buffer_size) {
    u_int8_t *data = new u_int8_t[buffer_size];
    mutable_buffer buffer = mutable_buffer(data, buffer_size);
    buffer_queue_.push_back(buffer);
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

void TcpSession::AsyncReadStartInternal(TcpSessionPtr session) {
    // Update socket read block time.
    if (stats_.read_block_start_time) {
        uint64_t blocked_usecs = UTCTimestampUsec() -
            stats_.read_block_start_time;
        stats_.read_block_start_time = 0;
        stats_.read_blocked_duration_usecs += blocked_usecs;
        server_->stats_.read_blocked_duration_usecs += blocked_usecs;
    }

    tbb::mutex::scoped_lock lock(mutex_);
    AsyncReadSome();
}

void TcpSession::AsyncReadStart() {
    if (io_strand_) {
        io_strand_->post(bind(&TcpSession::AsyncReadStartInternal, this,
                         TcpSessionPtr(this)));
    }
}

void TcpSession::SetDeferReader(bool defer_reader) {
    if (defer_reader_ != defer_reader) {
        defer_reader_ = defer_reader;
        // Call AsyncReadStart if reader was previously deferred
        if (!defer_reader_) {
            AsyncReadStart();
        }
    }
}

void TcpSession::DeferWriter() {
    // Update socket write block count.
    stats_.write_blocked++;
    server_->stats_.write_blocked++;
    socket()->async_write_some(null_buffers(),
                               bind(&TcpSession::WriteReadyInternal,
                                    TcpSessionPtr(this),
                                    error, UTCTimestampUsec()));
}

void TcpSession::AsyncReadSome() {
    if (established_) {
        socket()->async_read_some(null_buffers(),
            bind(&TcpSession::AsyncReadHandler, TcpSessionPtr(this)));
    }
}

size_t TcpSession::WriteSome(const uint8_t *data, size_t len,
                             error_code *error) {
    return socket()->write_some(buffer(data, len), *error);
}

void TcpSession::AsyncWrite(const u_int8_t *data, size_t size) {
    async_write(*socket(), buffer(data, size),
        bind(&TcpSession::AsyncWriteHandler, TcpSessionPtr(this),
             error));
}

TcpSession::Endpoint TcpSession::local_endpoint() const {
    tbb::mutex::scoped_lock lock(mutex_);
    if (!established_)
        return Endpoint();

    error_code error;
    Endpoint local = socket()->local_endpoint(error);
    if (error) {
        return Endpoint();
    }
    return local;
}

void TcpSession::set_observer(EventObserver observer) {
    tbb::mutex::scoped_lock lock(obs_mutex_);
    observer_ = observer;
}

void TcpSession::SetName() {
    ostringstream out;
    error_code error;
    Endpoint local;

    local = socket()->local_endpoint(error);
    out << local.address().to_string() << ":" << local.port() << "::";
    out << remote_.address().to_string() << ":" << remote_.port();

    name_ = out.str();
}

void TcpSession::SessionEstablished(Endpoint remote,
                                    Direction direction) {
    established_ = true;
    remote_ = remote;
    remote_addr_str_ = remote.address().to_string();
    direction_ = direction;
    SetName();
}

void TcpSession::Accepted() {
    TCP_SESSION_LOG_DEBUG(this, TCP_DIR_OUT,
                          "Passive session Accept complete");
    {
        tbb::mutex::scoped_lock obs_lock(obs_mutex_);
        if (observer_) {
            observer_(this, ACCEPT);
        }
    }

    if (read_on_connect_) {
        AsyncReadStart();
    }
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
    SetSocketOptions();

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
void TcpSession::CloseInternal(const error_code &ec,
                               bool call_observer, bool notify_server) {
    tbb::mutex::scoped_lock lock(mutex_);

    if (socket() != NULL && !closed_) {
        error_code error;
        socket()->close(error);
    }
    closed_ = true;
    if (!established_) {
        return;
    }
    established_ = false;

    // copy the ec to close reason
    close_reason_ = ec;

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

void TcpSession::TriggerAsyncReadHandler() {
    if (io_strand_) {
        io_strand_->post(bind(&TcpSession::AsyncReadHandler,
                              TcpSessionPtr(this)));
    }
}

void TcpSession::Close() {
    error_code ec;
    CloseInternal(ec, false);
}

// virtual method overriden in derrived classes.
void TcpSession::WriteReady(const error_code &error) {
}

void TcpSession::WriteReadyInternal(TcpSessionPtr session,
                                    const error_code &error,
                                    uint64_t block_start_time) {
    error_code ec = error;
    tbb::mutex::scoped_lock lock(session->mutex_);

    // Update socket write block time.
    uint64_t blocked_usecs = UTCTimestampUsec() - block_start_time;
    session->stats_.write_blocked_duration_usecs += blocked_usecs;
    session->server_->stats_.write_blocked_duration_usecs += blocked_usecs;

    if (session->IsSocketErrorHard(ec)) {
        goto session_error;
    }

    //
    // Ignore if connection is already closed.
    //
    if (session->IsClosedLocked()) return;

    session->writer_->HandleWriteReady(&ec);
    if (session->IsSocketErrorHard(ec)) {
        goto session_error;
    }

    lock.release();
    session->WriteReady(ec);
    return;

session_error:
    lock.release();
    TCP_SESSION_LOG_ERROR(session.get(), TCP_DIR_OUT,
                          "Write failed due to error: " << ec.value()
                          << " category: " << ec.category().name()
                          << " message: " << ec.message());
    session->CloseInternal(ec, true);
}

void TcpSession::AsyncWriteHandler(TcpSessionPtr session,
                                   const error_code &error) {
    if (session->IsSocketErrorHard(error)) {
        TCP_SESSION_LOG_ERROR(session, TCP_DIR_OUT,
                              "Write failed due to error: " << error.message());
        session->CloseInternal(error, true);
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

    if (socket()->non_blocking()) {
        error_code error;
        int len = writer_->Send(data, size, &error);
        lock.release();
        if (len < 0) {
            TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
                                  "Write failed due to error: "
                                  << error.category().name() << " "
                                  << error.message());
            CloseInternal(error, true);
            return false;
        }
        if ((size_t) len != size)
            ret = false;
        if (sent) *sent = (len > 0) ? len : 0;
    } else {
        AsyncWrite(data, size);
        if (sent) *sent = size;
    }
    return ret;
}

Task* TcpSession::CreateReaderTask(mutable_buffer buffer,
                                  size_t bytes_transferred) {
    Buffer rdbuf(buffer_cast<const uint8_t *>(buffer), bytes_transferred);
    Reader *task = new Reader(TcpSessionPtr(this),
                              bind(&TcpSession::OnRead, this, _1), rdbuf);
    return (task);
}

size_t TcpSession::ReadSome(mutable_buffer buffer, error_code *error) {
    return socket()->read_some(mutable_buffers_1(buffer), *error);
}

// Tests with large data have shown large amounts of data being read in one
// read_some() call, if available. Hence, allocate memory for all the bytes
// available in the socket, but no less t han kDefaultBufferSize.
size_t TcpSession::GetReadBufferSize() const {
    error_code error;
    size_t size = socket_->available(error);
    if (size < kDefaultBufferSize)
        size = kDefaultBufferSize;
    return size;
}

void TcpSession::AsyncReadHandler(TcpSessionPtr session) {
    tbb::mutex::scoped_lock lock(session->mutex_);
    if (session->closed_) {
        return;
    }

    mutable_buffer buffer =
        session->AllocateBuffer(session->GetReadBufferSize());

    error_code error;
    size_t bytes_transferred = session->ReadSome(buffer, &error);
    if (IsSocketErrorHard(error)) {
        session->ReleaseBufferLocked(buffer);
        // eof is returned when the peer closed the socket, no need to log error
        if ((error != eof) && (strncmp(error.category().name(), "asio.ssl", 8) != 0)) {
            TCP_SESSION_LOG_ERROR(session, TCP_DIR_IN,
                    "Read failed due to error " << error.value()
                    << " : " << error.message());
        }

        lock.release();
        session->CloseInternal(error, true);
        return;
    }

    // Update read statistics.
    session->stats_.read_calls++;
    session->stats_.read_bytes += bytes_transferred;
    session->server_->stats_.read_calls++;
    session->server_->stats_.read_bytes += bytes_transferred;

    Task *task = session->CreateReaderTask(buffer, bytes_transferred);
    // Starting a new task for the session
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

int TcpSession::GetSessionInstance() const {
    return Task::kTaskInstanceAny;
}


int32_t TcpSession::local_port() const {
    if (socket() == NULL) {
        return -1;
    }
    error_code error;
    Endpoint local = socket()->local_endpoint(error);
    if (IsSocketErrorHard(error)) {
        return -1;
    }
    return local.port();
}

int32_t TcpSession::remote_port() const {
    if (socket() == NULL) {
        return -1;
    }
    error_code error;
    Endpoint remote = socket()->remote_endpoint(error);
    if (IsSocketErrorHard(error)) {
        return -1;
    }
    return remote.port();
}

int TcpSession::SetMd5SocketOption(uint32_t peer_ip,
                                   const string &md5_password) {
    return server()->SetMd5SocketOption(socket_->native_handle(), peer_ip,
                                        md5_password);
}

int TcpSession::ClearMd5SocketOption(uint32_t peer_ip) {
    return server()->SetMd5SocketOption(socket_->native_handle(), peer_ip, "");
}

int TcpSession::SetDscpSocketOption(uint8_t value) {
    return server()->SetDscpSocketOption(socket()->native_handle(), value);
}

uint8_t TcpSession::GetDscpValue() const {
    return server_->GetDscpValue(socket()->native_handle());
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
    for (; bufsize < kMaxMessageSize && bufsize < length; bufsize <<= 1) {
    }
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
    size_t size = TcpSession::BufferSize(buffer);
    TCP_SESSION_LOG_UT_DEBUG(session_, TCP_DIR_IN, "Read " << size << " bytes");

    if (!queue_.empty()) {
        int msglength = MsgLength(queue_.front(), offset_);
        if (msglength < 0) {
            int queuelen = QueueByteLength();
            if (queuelen + static_cast<int>(size) < kHeaderLenSize) {
                queue_.push_back(buffer);
                return;
            }
            scoped_array<uint8_t> data(new uint8_t[kHeaderLenSize]);
            Buffer header = PullUp(data.get(), buffer, kHeaderLenSize);
            assert(TcpSession::BufferSize(header) == (size_t) kHeaderLenSize);

            msglength = MsgLength(header, 0);
            remain_ = msglength - queuelen;
        }

        assert(remain_ > 0);
        if (size < (size_t) remain_) {
            queue_.push_back(buffer);
            remain_ -= size;
            return;
        }

        // concat the buffers into a contiguous message.
        scoped_array<uint8_t> data(new uint8_t[AllocBufferSize(msglength)]);
        BufferConcat(data.get(), buffer, msglength);
        assert(remain_ == -1);
        // Receive the message
        bool success = callback_(data.get(), msglength);
        if (!success)
            return;
    }

    int avail = size - offset_;
    while (avail > 0) {
        int msglength = MsgLength(buffer, offset_);
        if (msglength < 0) {
            break;
        }
        if (msglength > avail) {
            remain_ = msglength - avail;
            break;
        }
        // Receive the message
        bool success =
            callback_(TcpSession::BufferData(buffer) + offset_, msglength);
        offset_ += msglength;
        avail -= msglength;
        if (!success)
            return;
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
bool TcpSession::IsSocketErrorHard(const error_code &ec) {
    if (!ec)
        return false;
    if (ec == try_again)
        return false;
    if (ec == would_block)
        return false;
    if (ec == in_progress)
        return false;
    if (ec == interrupted)
        return false;
    if (ec == network_down)
        return false;
    if (ec == network_reset)
        return false;
    if (ec == network_unreachable)
        return false;
    if (ec == no_buffer_space)
        return false;

    return true;
}

error_code TcpSession::SetSocketKeepaliveOptions(int keepalive_time,
        int keepalive_intvl, int keepalive_probes, int tcp_user_timeout_val) {
    error_code ec;
    socket_base::keep_alive keep_alive_option(true);
    socket()->set_option(keep_alive_option, ec);
    if (ec) {
        TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
                "keep_alive set error: " << ec);
        return ec;
    }
#ifdef TCP_KEEPIDLE
    typedef integer< IPPROTO_TCP, TCP_KEEPIDLE > keepalive_idle_time;
    keepalive_idle_time keepalive_idle_time_option(keepalive_time);
    socket()->set_option(keepalive_idle_time_option, ec);
    if (ec) {
        TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
            "keepalive_idle_time: " << keepalive_time << " set error: " << ec);
        return ec;
    }
#endif
#ifdef TCP_KEEPALIVE
    typedef integer< IPPROTO_TCP, TCP_KEEPALIVE > keepalive_idle_time;
    keepalive_idle_time keepalive_idle_time_option(keepalive_time);
    socket()->set_option(keepalive_idle_time_option, ec);
    if (ec) {
        TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
            "keepalive_idle_time: " << keepalive_time << " set error: " << ec);
        return ec;
    }
#endif
#ifdef TCP_KEEPINTVL
    typedef integer< IPPROTO_TCP, TCP_KEEPINTVL > keepalive_interval;
    keepalive_interval keepalive_interval_option(keepalive_intvl);
    socket()->set_option(keepalive_interval_option, ec);
    if (ec) {
        TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
            "keepalive_interval: " << keepalive_intvl << " set error: " << ec);
        return ec;
    }
#endif
#ifdef TCP_KEEPCNT
    typedef integer< IPPROTO_TCP, TCP_KEEPCNT > keepalive_count;
    keepalive_count keepalive_count_option(keepalive_probes);
    socket()->set_option(keepalive_count_option, ec);
    if (ec) {
        TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
            "keepalive_probes: " << keepalive_probes << " set error: " << ec);
        return ec;
    }
#endif
#ifdef TCP_USER_TIMEOUT
    typedef integer< IPPROTO_TCP, TCP_USER_TIMEOUT > tcp_user_timeout;
    tcp_user_timeout tcp_user_timeout_option(tcp_user_timeout_val);
    socket()->set_option(tcp_user_timeout_option, ec);
    if (ec) {
        TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
            "tcp_user_timeout: " << tcp_user_timeout_val << " set error: "
            << ec);
        return ec;
    }
#endif

    return ec;
}

error_code TcpSession::SetSocketOptions() {
    error_code ec;

    //
    // Make socket write non-blocking
    //
    socket()->non_blocking(true, ec);
    if (ec) {
        TCP_SESSION_LOG_ERROR(this, TCP_DIR_NA,
                              "Cannot set socket non blocking: " << ec);
        return ec;
    }

    char *buffer_size_str = getenv("TCP_SESSION_SOCKET_BUFFER_SIZE");
    if (!buffer_size_str) return ec;

    uint64_t sz = strtoul(buffer_size_str, NULL, 0);
    if (sz) {
        //
        // Set socket send and receive buffer size
        //
        // Currently used only under test environments to trigger partial
        // sends more deterministically
        //
        socket_base::send_buffer_size send_buffer_size_option(sz);
        socket()->set_option(send_buffer_size_option, ec);
        if (ec) {
            TCP_SESSION_LOG_ERROR(this, TCP_DIR_OUT,
                                  "send_buffer_size set error: " << ec);
            return ec;
        }

        socket_base::receive_buffer_size receive_buffer_size_option(sz);
        socket()->set_option(receive_buffer_size_option, ec);
        if (ec) {
            TCP_SESSION_LOG_ERROR(this, TCP_DIR_IN,
                                  "receive_buffer_size set error: " << ec);
            return ec;
        }
    }

    return ec;
}

void TcpSession::GetRxSocketStats(SocketIOStats *socket_stats) const {
    stats_.GetRxStats(socket_stats);
}

void TcpSession::GetTxSocketStats(SocketIOStats *socket_stats) const {
    stats_.GetTxStats(socket_stats);
}
