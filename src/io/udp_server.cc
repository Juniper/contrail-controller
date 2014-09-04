/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <base/logging.h>
#include <io/udp_server.h>
#include <io/io_log.h>

using boost::asio::buffer_cast;
using boost::asio::mutable_buffer;
using boost::asio::mutable_buffers_1;
using boost::asio::ip::udp;

UdpServer::UdpServer(boost::asio::io_service *io_service, int buffer_size):
    socket_(*io_service),
    buffer_size_(buffer_size),
    state_(Uninitialized),
    evm_(NULL) {
    refcount_ = 0;
    UdpServerManager::AddServer(this);
}

UdpServer::UdpServer(EventManager *evm, int buffer_size):
    socket_(*(evm->io_service())),
    buffer_size_(buffer_size),
    state_(Uninitialized),
    evm_(evm) {
    refcount_ = 0;
    UdpServerManager::AddServer(this);
}

void UdpServer::SetName(udp::endpoint ep) {
    std::ostringstream s;
    boost::system::error_code ec;
    s << "Udpsocket@" << ep;
    name_ = s.str();
}

UdpServer::~UdpServer() {
    assert(state_ == Uninitialized || state_ == SocketOpenFailed ||
           state_ == SocketBindFailed);
    assert(pbuf_.empty());
}

void UdpServer::Shutdown() {
    while (!pbuf_.empty()) {
        delete[] pbuf_.back();
        pbuf_.pop_back();
    }
    if (state_ == OK && socket_.is_open()) {
        boost::system::error_code ec;
        socket_.shutdown(udp::socket::shutdown_both, ec);
        if (ec) {
            TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA,
                "Error shutdown udp socket " << ec);
        }
        socket_.close(ec);
        if (ec) {
            TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA,
                "Error closing udp socket " << ec);
        }
        state_ = Uninitialized;
    }
}

bool UdpServer::Initialize(const std::string &ipaddress, unsigned short port) {
    boost::system::error_code error;
    boost::asio::ip::address ip = boost::asio::ip::address::from_string(
                ipaddress, error);
    if (!error) {
        udp::endpoint local_endpoint = udp::endpoint(ip, port);
        return Initialize(local_endpoint);
    } else {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA, "IP address conversion: "
            << ipaddress << ": " << error);
        return false;
    }
}

bool UdpServer::Initialize(unsigned short port) {
    udp::endpoint local_endpoint = udp::endpoint(udp::v4(), port);
    return Initialize(local_endpoint);
}

bool UdpServer::Initialize(udp::endpoint local_endpoint) {
    if (GetServerState() != Uninitialized) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA, "udp socket in wrong state "
            << state_);
        return false;
    }
    boost::system::error_code error;
    socket_.open(udp::v4(), error);
    if (error) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA, "udp socket open: " <<
            error.message());
        state_ = SocketOpenFailed;
        return false;
    }
    socket_.bind(local_endpoint, error);
    if (error) {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA, "udp socket bind: "
            << error.message() << ":" << socket_.local_endpoint(error));
        state_ = SocketBindFailed;
        return false;
    }
    SetName(local_endpoint);
    state_ = OK;
    return true;
}

udp::endpoint UdpServer::GetLocalEndPoint() {
    boost::system::error_code ec;
    return socket_.local_endpoint(ec);
}

mutable_buffer UdpServer::AllocateBuffer(std::size_t s) {
    u_int8_t *p = new u_int8_t[s];
    pbuf_.push_back(p);
    return mutable_buffer(p, s);
}

mutable_buffer UdpServer::AllocateBuffer() {
    return AllocateBuffer(buffer_size_);
}

void UdpServer::DeallocateBuffer(boost::asio::const_buffer &buffer) {
    const u_int8_t *p = buffer_cast<const uint8_t *>(buffer);
    std::vector<u_int8_t *>::iterator f = std::find(pbuf_.begin(),
        pbuf_.end(), p);
    if (f != pbuf_.end())
        pbuf_.erase(f);
    delete[] p;
}

void UdpServer::StartSend(udp::endpoint ep, std::size_t bytes_to_send,
    mutable_buffer buffer) {
    if (state_ == OK) {
        boost::asio::const_buffer b(buffer_cast<const uint8_t*>(buffer),
                                        buffer_size(buffer));
        socket_.async_send_to(mutable_buffers_1(buffer), ep,
            boost::bind(&UdpServer::HandleSend, UdpServerPtr(this), b, ep,
              boost::asio::placeholders::bytes_transferred,
              boost::asio::placeholders::error));
    } else {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA,
            "error udp socket StartSend: state==" << state_);
    }
}

void UdpServer::StartReceive() {
    if (state_ == OK) {
        mutable_buffer b = AllocateBuffer();
        boost::asio::const_buffer buffer(buffer_cast<const uint8_t*>(b),
                                        buffer_size(b));
        socket_.async_receive_from(mutable_buffers_1(b),
            remote_endpoint_, boost::bind(&UdpServer::HandleReceiveInternal,
            UdpServerPtr(this), buffer,
            boost::asio::placeholders::bytes_transferred,
            boost::asio::placeholders::error));
    } else {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA,
            "error udp socket StartReceive: state==" << state_);
    }
}

void UdpServer::HandleReceiveInternal(boost::asio::const_buffer recv_buffer,
    std::size_t bytes_transferred, const boost::system::error_code& error) {
    HandleReceive(recv_buffer, remote_endpoint_, bytes_transferred, error);
    if (!error.value())
        StartReceive();
}

void UdpServer::HandleReceive(boost::asio::const_buffer &recv_buffer,
    udp::endpoint remote_endpoint, std::size_t bytes_transferred,
    const boost::system::error_code& error) {
    if (!error || error == boost::asio::error::message_size) {
        // handle error == boost::asio::error::message_size
        // call on read (recv_buffer, *remote_endpoint, bytes_transferred)
        // StartReceive();
    } else {
        TCP_SERVER_LOG_ERROR(this, TCP_DIR_NA,
            "error reading udp socket " << error);
    }
    DeallocateBuffer(recv_buffer);
}

void UdpServer::HandleSend(boost::asio::const_buffer send_buffer,
    udp::endpoint remote_endpoint, std::size_t bytes_transferred,
    const boost::system::error_code& error) {
    // check error
    // DeallocateBuffer (send_buffer);
    // DeallocateEndPoint (remote_endpoint);
}

udp::endpoint UdpServer::GetLocalEndpoint(boost::system::error_code *error) {
    return socket_.local_endpoint(*error);
}

std::string UdpServer::GetLocalEndpointAddress() {
    boost::system::error_code error;
    udp::endpoint ep = GetLocalEndpoint(&error);
    if (error.value())
        return "";
    return ep.address().to_string();
}

int UdpServer::GetLocalEndpointPort() {
    boost::system::error_code error;
    udp::endpoint ep = GetLocalEndpoint(&error);
    if (error.value())
        return -1;
    return ep.port();
}

//
// UdpServerManager class routines
//
ServerManager<UdpServer, UdpServerPtr> UdpServerManager::impl_;

void UdpServerManager::AddServer(UdpServer *server) {
    impl_.AddServer(server);
}

void UdpServerManager::DeleteServer(UdpServer *server) {
    impl_.DeleteServer(server);
}
