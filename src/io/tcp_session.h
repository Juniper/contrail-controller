/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __TCP_SESSION_H__
#define __TCP_SESSION_H__

#include <list>
#include <deque>

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>

#include <tbb/mutex.h>
#include <tbb/task.h>
#ifndef _LIBCPP_VERSION
#include <tbb/compat/condition_variable>
#endif
#include "base/util.h"
#include "io/tcp_server.h"

class EventManager;
class TcpServer;
class TcpSession;
class TcpMessageWriter;

// TcpSession
//
// Concurrency: the session is created by the event manager thread, which
// also invokes the AsyncHandlers. ReleaseBuffer and Send will typically be
// invoked by a different thread.
class TcpSession {
public:
    static const int kDefaultBufferSize = 4 * 1024;

    enum Event {
        EVENT_NONE,
        ACCEPT,
        CONNECT_COMPLETE,
        CONNECT_FAILED,
        CLOSE
    };
    enum Direction {
        ACTIVE,
        PASSIVE
    };
    typedef boost::asio::ip::tcp::socket Socket;
    typedef boost::asio::ip::tcp::endpoint Endpoint;
    typedef boost::function<void(TcpSession *, Event)> EventObserver;
    typedef boost::asio::const_buffer Buffer;

    // TcpSession constructor takes ownership of socket.
    TcpSession(TcpServer *server, Socket *socket,
               bool async_read_ready = true);
    // Performs a non-blocking send operation.
    virtual bool Send(const u_int8_t *data, size_t size, size_t *sent);

    // Called by TcpServer to trigger async read.
    virtual bool Connected(Endpoint remote);

    void ConnectFailed();

    void Close();

    virtual std::string ToString() const { return name_; }

    void SetBufferSize(int buffer_size);

    // Getters and setters
    Socket *socket() { return socket_.get(); }
    TcpServer *server() { return server_.get(); }
    int32_t local_port() const;
    int32_t remote_port() const;

    // Concurrency: changing the observer guarantees mutual exclusion with
    // the observer invocation. e.g. if the caller sets the observer to NULL
    // it is guaranteed that the observer will not get invoked after this
    // method returns.
    void set_observer(EventObserver observer);

    // Buffers must be freed in arrival order.
    virtual void ReleaseBuffer(Buffer buffer);

    // This function returns the instance to run SessionTask.
    // Returning Task::kTaskInstanceAny would allow multiple session tasks to 
    // run in parallel.
    // Derived class may override implementation if it expects the all the 
    // Tasks of the session to run in specific instance
    // Note: Two tasks of same task ID and task instance can't run 
    //       at in parallel
    // E.g. BgpSession is created per BgpPeer and to ensure that 
    //      there is one SessionTask per peer, PeerIndex is returned 
    //      from this function
    virtual int GetSessionInstance() const;

    static const uint8_t *BufferData(const Buffer &buffer) {
        return boost::asio::buffer_cast<const uint8_t *>(buffer);
    }
    static size_t BufferSize(const Buffer &buffer) {
        return boost::asio::buffer_size(buffer);
    }

    bool IsEstablished() const {
        tbb::mutex::scoped_lock lock(mutex_);
        return established_;
    }

    bool IsClosed() const {
        tbb::mutex::scoped_lock lock(mutex_);
        return closed_;
    }

    Endpoint remote_endpoint() const {
        return remote_;
    }

    Endpoint local_endpoint() const;

    virtual boost::system::error_code SetSocketOptions();
    static bool IsSocketErrorHard(const boost::system::error_code &ec);
    void set_read_on_connect(bool read) { read_on_connect_ = read; }
    void SessionEstablished(Endpoint remote, Direction direction);

    void AsyncReadStart();

    const io::SocketStats &GetSocketStats() const { return stats_; }
    void GetRxSocketStats(SocketIOStats &socket_stats) const;
    void GetTxSocketStats(SocketIOStats &socket_stats) const;

protected:
    virtual ~TcpSession();

    // Read handler. Called from a TBB task.
    virtual void OnRead(Buffer buffer) = 0;
    // Callback after socket is ready for write.
    virtual void WriteReady(const boost::system::error_code &error);

    virtual int reader_task_id() const {
        return reader_task_id_;
    }

    EventObserver observer() { return observer_; }
    boost::system::error_code SetSocketKeepaliveOptions(int keepalive_time,
            int keepalive_intvl, int keepalive_probes);

private:
    class Reader;
    friend class TcpServer;
    friend class TcpMessageWriter;
    friend void intrusive_ptr_add_ref(TcpSession *session);
    friend void intrusive_ptr_release(TcpSession *session);
    typedef boost::intrusive_ptr<TcpSession> TcpSessionPtr;
    typedef std::list<boost::asio::mutable_buffer> BufferQueue;

    static void AsyncReadHandler(TcpSessionPtr session,
                                 boost::asio::mutable_buffer buffer,
                                 const boost::system::error_code &error,
                                 size_t size);
    static void AsyncWriteHandler(TcpSessionPtr session,
                                  const boost::system::error_code &error);

    void ReleaseBufferLocked(Buffer buffer);
    void CloseInternal(bool call_observer, bool notify_server = true);
    void SetEstablished(Endpoint remote, Direction dir);

    tbb::mutex &mutex() { return mutex_; }

    bool IsClosedLocked() const {
        return closed_;
    }
    void SetName();

    boost::asio::mutable_buffer AllocateBuffer();
    void DeleteBuffer(boost::asio::mutable_buffer buffer);
    void WriteReadyInternal(const boost::system::error_code &);

    static int reader_task_id_;

    TcpServerPtr server_;
    boost::scoped_ptr<Socket> socket_;
    bool read_on_connect_;
    int buffer_size_;

    // Protects session state and buffer queue.
    mutable tbb::mutex mutex_;

    /**************** protected by mutex_ ****************/
    bool established_;          // In TCP ESTABLISHED state.
    bool closed_;               // Close has been called.
    Endpoint remote_;           // Remote end-point
    Direction direction_;       // direction (active, passive)
    BufferQueue buffer_queue_;
    /**************** end protected by mutex_ ****************/

    // Protects observer manipulation and invocation. When this lock is
    // held the session mutex should not be held and vice-versa.
    tbb::mutex obs_mutex_;
    EventObserver observer_;

    io::SocketStats stats_;
    boost::scoped_ptr<TcpMessageWriter> writer_;

    tbb::atomic<int> refcount_;
    std::string name_;

    DISALLOW_COPY_AND_ASSIGN(TcpSession);
};

inline void intrusive_ptr_add_ref(TcpSession *session) {
    session->refcount_.fetch_and_increment();
}

inline void intrusive_ptr_release(TcpSession *session) {
    int prev = session->refcount_.fetch_and_decrement();
    if (prev == 1) {
        delete session;
    }
}

// TcpMessageReader
//
// Provides base implementation of OnRead() for TcpSession assuming
// fixed message header length
//
class TcpMessageReader {
public:
    typedef boost::asio::const_buffer Buffer;
    typedef boost::function<void(const u_int8_t *, size_t)> ReceiveCallback;

    TcpMessageReader(TcpSession *session, ReceiveCallback callback);
    virtual ~TcpMessageReader();
    virtual void OnRead(Buffer buffer);

protected:
    virtual int MsgLength(Buffer buffer, int offset) = 0;
    virtual const int GetHeaderLenSize() = 0;
    virtual const int GetMaxMessageSize() = 0;

private:
    typedef std::deque<Buffer> BufferQueue;

    // Copy the queue into one contiguous buffer.
    uint8_t *BufferConcat(uint8_t *data, Buffer buffer, int msglength);

    int QueueByteLength() const;

    Buffer PullUp(uint8_t *data, Buffer buffer, size_t size) const;

    int AllocBufferSize(int length);

    TcpSession *session_;
    ReceiveCallback callback_;
    BufferQueue queue_;
    int offset_;
    int remain_;

    DISALLOW_COPY_AND_ASSIGN(TcpMessageReader);
};

#endif // __TCP_SESSION_H__
