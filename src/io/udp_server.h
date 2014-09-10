/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef IO_UDP_SERVER_H_
#define IO_UDP_SERVER_H_

#include <string>
#include <vector>
#include <boost/asio.hpp>
#include <boost/intrusive_ptr.hpp>
#include "io/event_manager.h"
#include "io/server_manager.h"

class UdpServer {
 public:
    enum ServerState {
        OK = 42,
        Uninitialized,
        SocketOpenFailed,
        SocketBindFailed,
    };
    static const int kDefaultBufferSize = 4 * 1024;

    explicit UdpServer(EventManager *evm, int buffer_size = kDefaultBufferSize);
    explicit UdpServer(boost::asio::io_service *io_service,
                       int buffer_size = kDefaultBufferSize);
    virtual ~UdpServer();

    virtual bool Initialize(unsigned short port);
    virtual bool Initialize(const std::string &ipaddress, unsigned short port);
    virtual bool Initialize(boost::asio::ip::udp::endpoint local_endpoint);
    virtual void Shutdown();

    // tx-rx
    void StartSend(boost::asio::ip::udp::endpoint ep, std::size_t bytes_to_send,
            boost::asio::mutable_buffer buffer);
    virtual void HandleReceive(
            boost::asio::const_buffer &recv_buffer,
            boost::asio::ip::udp::endpoint remote_endpoint,
            std::size_t bytes_transferred,
            const boost::system::error_code& error);
    void StartReceive();
    virtual void HandleSend(boost::asio::const_buffer send_buffer,
            boost::asio::ip::udp::endpoint remote_endpoint,
            std::size_t bytes_transferred,
            const boost::system::error_code& error);
    // state
    ServerState GetServerState() { return state_; }
    boost::asio::ip::udp::endpoint GetLocalEndpoint(
        boost::system::error_code *error);
    std::string GetLocalEndpointAddress();
    int GetLocalEndpointPort();
    // buffers
    boost::asio::mutable_buffer AllocateBuffer();
    boost::asio::mutable_buffer AllocateBuffer(std::size_t s);
    void DeallocateBuffer(boost::asio::const_buffer &buffer);

 protected:
    EventManager *event_manager() { return evm_; }
    virtual bool DisableSandeshLogMessages() { return false; }
    virtual std::string ToString() { return name_; }

 private:
    friend void intrusive_ptr_add_ref(UdpServer *server);
    friend void intrusive_ptr_release(UdpServer *server);
    virtual void SetName(boost::asio::ip::udp::endpoint ep);
    void HandleReceiveInternal(
            boost::asio::const_buffer recv_buffer,
            std::size_t bytes_transferred,
            const boost::system::error_code& error);

    boost::asio::ip::udp::socket socket_;
    int buffer_size_;
    ServerState state_;
    EventManager *evm_;
    std::string name_;
    boost::asio::ip::udp::endpoint remote_endpoint_;
    tbb::mutex mutex_;
    std::vector<u_int8_t *> pbuf_;
    tbb::atomic<int> refcount_;

    DISALLOW_COPY_AND_ASSIGN(UdpServer);
};

typedef boost::intrusive_ptr<UdpServer> UdpServerPtr;

inline void intrusive_ptr_add_ref(UdpServer *server) {
    server->refcount_.fetch_and_increment();
}

inline void intrusive_ptr_release(UdpServer *server) {
    int prev = server->refcount_.fetch_and_decrement();
    if (prev == 1) {
        delete server;
    }
}

class UdpServerManager {
public:
    static void AddServer(UdpServer *server);
    static void DeleteServer(UdpServer *server);

private:
    static ServerManager<UdpServer, UdpServerPtr> impl_;
};

#endif  // IO_UDP_SERVER_H_
