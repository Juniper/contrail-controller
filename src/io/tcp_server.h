/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __TCPSERVER_H__
#define __TCPSERVER_H__

#include <map>
#include <set>
#include <boost/asio/ip/tcp.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <tbb/mutex.h>
#include <tbb/compat/condition_variable>

#include "base/util.h"
#include "io/server_manager.h"

class EventManager;
class TcpSession;
class TcpServerSocketStats;

class TcpServer {
public:
    typedef boost::asio::ip::tcp::endpoint Endpoint;
    typedef boost::asio::ip::tcp::socket Socket;

    explicit TcpServer(EventManager *evm);
    virtual ~TcpServer();

    // Bind a listening socket and register it with the event manager.
    virtual bool Initialize(short port);

    const std::string ToString() const { return name_; }
    void SetAcceptor();
    void ResetAcceptor();

    // shutdown the listening socket.
    void Shutdown();

    // close all existing sessions and delete them.
    void ClearSessions();

    // Helper function that allocates a socket and calls the virtual method
    // AllocSession. The session object is owned by the TcpServer and must
    // be deallocated via DeleteSession.
    virtual TcpSession *CreateSession();

    // Delete a session object.
    virtual void DeleteSession(TcpSession *session);

    virtual void Connect(TcpSession *session, Endpoint remote);

    virtual bool DisableSandeshLogMessages() { return false; }

    int GetPort() const;

    struct SocketStats {
        SocketStats() {
            read_calls = 0;
            read_bytes = 0;
            write_calls = 0;
            write_bytes = 0;
            write_blocked = 0;
            write_blocked_duration_usecs = 0;
        }

        void GetRxStats(TcpServerSocketStats &socket_stats) const;
        void GetTxStats(TcpServerSocketStats &socket_stats) const;

        tbb::atomic<uint64_t> read_calls;
        tbb::atomic<uint64_t> read_bytes;
        tbb::atomic<uint64_t> write_calls;
        tbb::atomic<uint64_t> write_bytes;
        tbb::atomic<uint64_t> write_blocked;
        tbb::atomic<uint64_t> write_blocked_duration_usecs;
    };
    const SocketStats &GetSocketStats() const { return stats_; }

    //
    // Return the number of tcp sessions in the map
    //
    size_t GetSessionCount() const {
        return session_ref_.size();
    }

    EventManager *event_manager() { return evm_; }

    // Returns true if any of the sessions on this server has read available
    // data.
    bool HasSessionReadAvailable() const;
    bool HasSessions() const;

    TcpSession *GetSession(Endpoint remote);

    // wait until the server has deleted all sessions.
    void WaitForEmpty();

    void GetRxSocketStats(TcpServerSocketStats &socket_stats) const;
    void GetTxSocketStats(TcpServerSocketStats &socket_stats) const;

protected:
    // Create a session object.
    virtual TcpSession *AllocSession(Socket *socket) = 0;

    //
    // Passively accepted a new session. Returns true if the session is
    // accepted, false otherwise.
    //
    // If the session is not accepted, tcp_server.cc deletes the newly
    // created session.
    //
    virtual bool AcceptSession(TcpSession *session);

    // For testing - will typically be used by derived class.
    void set_socket_open_failure(bool flag) { socket_open_failure_ = flag; }
    bool socket_open_failure() const { return socket_open_failure_; }

    Endpoint LocalEndpoint() const;

private:
    friend class TcpSession;
    friend class TcpMessageWriter;
    friend class BgpServerUnitTest;
    friend void intrusive_ptr_add_ref(TcpServer *server);
    friend void intrusive_ptr_release(TcpServer *server);

    typedef boost::intrusive_ptr<TcpServer> TcpServerPtr;
    typedef boost::intrusive_ptr<TcpSession> TcpSessionPtr;
    struct TcpSessionPtrCmp {
        bool operator()(const TcpSessionPtr &lhs,
                        const TcpSessionPtr &rhs) const {
            return lhs.get() < rhs.get();
        }
    };
    typedef std::set<TcpSessionPtr, TcpSessionPtrCmp> SessionSet;
    typedef std::multimap<Endpoint, TcpSession *> SessionMap;

    void InsertSessionToMap(Endpoint remote, TcpSession *session);
    bool RemoveSessionFromMap(Endpoint remote, TcpSession *session);

    // Called by the asio service.
    void AcceptHandlerInternal(TcpServerPtr server,
             const boost::system::error_code &error);

    void ConnectHandler(TcpServerPtr server, TcpSessionPtr session,
                        const boost::system::error_code &error);

    // Trigger the async accept operation.
    void AsyncAccept();

    void OnSessionClose(TcpSession *session);
    void SetName(Endpoint local_endpoint);

    SocketStats stats_;
    EventManager *evm_;
    // mutex protects the session maps
    mutable tbb::mutex mutex_;
    tbb::interface5::condition_variable cond_var_;
    SessionSet session_ref_;
    SessionMap session_map_;
    std::auto_ptr<Socket> so_accept_;      // socket used in async_accept
    boost::scoped_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    tbb::atomic<int> refcount_;
    std::string name_;
    bool socket_open_failure_;

    DISALLOW_COPY_AND_ASSIGN(TcpServer);
};

typedef boost::intrusive_ptr<TcpServer> TcpServerPtr;

inline void intrusive_ptr_add_ref(TcpServer *server) {
    server->refcount_.fetch_and_increment();
}

inline void intrusive_ptr_release(TcpServer *server) {
    int prev = server->refcount_.fetch_and_decrement();
    if (prev == 1) {
        delete server;
    }
}

class TcpServerManager {
public:
    static void AddServer(TcpServer *server);
    static void DeleteServer(TcpServer *server);
    static size_t GetServerCount();

private:
    static ServerManager<TcpServer, TcpServerPtr> impl_;
};

#endif // __TCPSERVER_H__
