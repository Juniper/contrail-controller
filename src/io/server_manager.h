/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef IO_SERVER_MANAGER_H_
#define IO_SERVER_MANAGER_H_

#include <set>
#include <tbb/mutex.h>

//
// ServerManager is the place holder for all the TcpServer and UdpServer
// objects instantiated in the life time of a process
//
// TcpServer and UdpServer objects are help in ServerSet until all the cleanup
// is complete and only then should they be deleted via DeleteServer() API
//
// Since TcpServer and UdpServer bjects are also held by boost::asio routines,
// they are protected using intrusive pointers
//
// This is similar to how TcpSession objects are managed via TcpSessionPtr
//
template <typename ServerType, typename ServerPtrType>
class ServerManager {
public:
    //
    // Add a server object to the data base, by creating an intrusive reference
    //
    static void AddServer(ServerType *server) {
        tbb::mutex::scoped_lock lock(mutex_);
        server_ref_.insert(ServerPtrType(server));
    }
    //
    // Delete a server object from the data base, by removing the intrusive
    // reference. If any other objects has a reference to this server such as
    // boost::asio, the server object deletion is automatically deferred
    //
    static void DeleteServer(ServerType *server) {
        tbb::mutex::scoped_lock lock(mutex_);
        server_ref_.erase(ServerPtrType(server));
    }
    //
    // Return number of Servers.
    // Used in tests to ensure there are no leaks.
    //
    size_t GetServerCount() {
        return server_ref_.size();
    }

private:
    struct ServerPtrCmp {
        bool operator()(const ServerPtrType &lhs,
                        const ServerPtrType &rhs) const {
            return lhs.get() < rhs.get();
        }
    };
    typedef std::set<ServerPtrType, ServerPtrCmp> ServerSet;

    static tbb::mutex mutex_;
    static ServerSet server_ref_;
};

template <typename ServerType, typename ServerPtrType>
typename ServerManager<ServerType, ServerPtrType>::ServerSet
    ServerManager<ServerType, ServerPtrType>::server_ref_;

template <typename ServerType, typename ServerPtrType>
tbb::mutex ServerManager<ServerType, ServerPtrType>::mutex_;

#endif  // IO_SERVER_MANAGER_H_
