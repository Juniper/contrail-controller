/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <base/logging.h>
#include <io/udp_server.h>
#include <io/io_log.h>
#include <io/io_utils.h>

using boost::asio::buffer_cast;
using boost::asio::mutable_buffer;
using boost::asio::mutable_buffers_1;
using boost::asio::const_buffer;
using boost::asio::ip::udp;

int UdpServer::reader_task_id_ = -1;

class UdpServer::Reader : public Task {
public:
    Reader(UdpServerPtr server, const udp::endpoint &remote_endpoint,
        const_buffer &buffer)
        : Task(server->reader_task_id(),
               server->reader_task_instance(remote_endpoint)),
        server_(server),
        remote_endpoint_(remote_endpoint),
        buffer_(buffer) {
    }

    virtual bool Run() {
        if (server_->GetServerState() == OK) {
            server_->OnRead(buffer_, remote_endpoint_);
        }
        return true;
    }

private:
    UdpServerPtr server_;
    udp::endpoint remote_endpoint_;
    const_buffer buffer_;
};

UdpServer::UdpServer(boost::asio::io_service *io_service, int buffer_size):
    socket_(*io_service),
    buffer_size_(buffer_size),
    state_(Uninitialized),
    evm_(NULL) {
    if (reader_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        reader_task_id_ = scheduler->GetTaskId("io::udp::ReaderTask");
    }
    refcount_ = 0;
    UdpServerManager::AddServer(this);
}

UdpServer::UdpServer(EventManager *evm, int buffer_size):
    socket_(*(evm->io_service())),
    buffer_size_(buffer_size),
    state_(Uninitialized),
    evm_(evm) {
    if (reader_task_id_ == -1) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        reader_task_id_ = scheduler->GetTaskId("io::udp::ReaderTask");
    }
    refcount_ = 0;
    UdpServerManager::AddServer(this);
}

int UdpServer::reader_task_instance(const udp::endpoint &rep) const {
    return Task::kTaskInstanceAny;
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
    {
        tbb::mutex::scoped_lock lock(mutex_);
        while (!pbuf_.empty()) {
            delete[] pbuf_.back();
            pbuf_.pop_back();
        }
    }
    if (socket_.is_open()) {
        boost::system::error_code ec;
        socket_.close(ec);
        if (ec) {
            UDP_SERVER_LOG_ERROR(this, UDP_DIR_NA,
                "ERROR closing UDP socket: " << ec);
        }
    }
    state_ = Uninitialized;
}

bool UdpServer::Initialize(const std::string &ipaddress, unsigned short port) {
    boost::system::error_code error;
    boost::asio::ip::address ip = boost::asio::ip::address::from_string(
                ipaddress, error);
    if (!error) {
        udp::endpoint local_endpoint = udp::endpoint(ip, port);
        return Initialize(local_endpoint);
    } else {
        UDP_SERVER_LOG_ERROR(this, UDP_DIR_NA, "IP address conversion: "
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
        UDP_SERVER_LOG_ERROR(this, UDP_DIR_NA,
            "Initialize UDP server in WRONG state: " << state_);
        return false;
    }
    boost::system::error_code error;
    socket_.open(udp::v4(), error);
    if (error) {
        UDP_SERVER_LOG_ERROR(this, UDP_DIR_NA, "UDP socket open FAILED: " <<
            error.message());
        state_ = SocketOpenFailed;
        return false;
    }
    socket_.bind(local_endpoint, error);
    if (error) {
        boost::system::error_code ec;
        UDP_SERVER_LOG_ERROR(this, UDP_DIR_NA, "UDP socket bind FAILED: "
            << error.message() << ":" << socket_.local_endpoint(ec));
        state_ = SocketBindFailed;
        socket_.close(ec);
        return false;
    }
    SetName(local_endpoint);
    state_ = OK;
    return true;
}

mutable_buffer UdpServer::AllocateBuffer(std::size_t s) {
    u_int8_t *p = new u_int8_t[s];
    {
        tbb::mutex::scoped_lock lock(mutex_);
        pbuf_.push_back(p);
    }
    return mutable_buffer(p, s);
}

mutable_buffer UdpServer::AllocateBuffer() {
    return AllocateBuffer(buffer_size_);
}

void UdpServer::DeallocateBuffer(const_buffer &buffer) {
    const u_int8_t *p = buffer_cast<const uint8_t *>(buffer);
    {
        tbb::mutex::scoped_lock lock(mutex_);
        std::vector<u_int8_t *>::iterator f = std::find(pbuf_.begin(),
            pbuf_.end(), p);
        if (f != pbuf_.end())
            pbuf_.erase(f);
    }
    delete[] p;
}

void UdpServer::StartSend(udp::endpoint ep, std::size_t bytes_to_send,
    const_buffer buffer) {
    if (state_ == OK) {
        socket_.async_send_to(boost::asio::buffer(buffer), ep,
            boost::bind(&UdpServer::HandleSendInternal, UdpServerPtr(this),
                buffer, ep,
                boost::asio::placeholders::bytes_transferred,
                boost::asio::placeholders::error));
    } else {
        stats_.write_errors++;
        UDP_SERVER_LOG_ERROR(this, UDP_DIR_NA,
            "StartSend UDP server in WRONG state: " << state_);
        DeallocateBuffer(buffer);
    }
}

void UdpServer::HandleSendInternal(const_buffer send_buffer,
    udp::endpoint remote_endpoint, std::size_t bytes_transferred,
    const boost::system::error_code& error) {
    if (state_ != OK) {
        stats_.write_errors++;
        UDP_SERVER_LOG_ERROR(this, UDP_DIR_OUT,
            "Send UDP server in WRONG state: " << state_);
        return;
    }
    if (error) {
        stats_.write_errors++;
        UDP_SERVER_LOG_ERROR(this, UDP_DIR_OUT,
            "Send to " << remote_endpoint << " FAILED due to error: " <<
            error.value() << " : " << error.message());
        DeallocateBuffer(send_buffer);
        return;
    }
    // Update write statistics.
    stats_.write_calls++;
    stats_.write_bytes += bytes_transferred;
    // Call the handler
    HandleSend(send_buffer, remote_endpoint, bytes_transferred, error);
}

void UdpServer::StartReceive() {
    if (state_ == OK) {
        mutable_buffer b(AllocateBuffer());
        const_buffer buffer(buffer_cast<const uint8_t*>(b),
                            buffer_size(b));
        socket_.async_receive_from(mutable_buffers_1(b),
            remote_endpoint_, boost::bind(&UdpServer::HandleReceiveInternal,
            UdpServerPtr(this), buffer,
            boost::asio::placeholders::bytes_transferred,
            boost::asio::placeholders::error));
    } else {
        stats_.read_errors++;
        UDP_SERVER_LOG_ERROR(this, UDP_DIR_NA,
            "StartReceive UDP server in WRONG state: " << state_);
    }
}

void UdpServer::HandleReceiveInternal(const_buffer recv_buffer,
    std::size_t bytes_transferred, const boost::system::error_code& error) {
    if (state_ != OK) {
        stats_.read_errors++;
        UDP_SERVER_LOG_ERROR(this, UDP_DIR_IN,
            "Receive UDP server in WRONG state: " << state_);
        return;
    }
    if (error) {
        stats_.read_errors++;
        UDP_SERVER_LOG_ERROR(this, UDP_DIR_IN,
            "Read FAILED due to error: " << error.value() << " : " <<
            error.message());
        DeallocateBuffer(recv_buffer);
        StartReceive();
        return;
    }
    // Update read statistics.
    stats_.read_calls++;
    stats_.read_bytes += bytes_transferred;
    // Call the handler
    HandleReceive(recv_buffer, remote_endpoint_, bytes_transferred, error);
    StartReceive();
}

void UdpServer::HandleReceive(const_buffer &recv_buffer,
    udp::endpoint remote_endpoint, std::size_t bytes_transferred,
    const boost::system::error_code& error) {
    const_buffer rdbuf(buffer_cast<const uint8_t *>(recv_buffer),
                       bytes_transferred);
    Reader *task = new Reader(UdpServerPtr(this), remote_endpoint,
                              rdbuf);
    // Starting a new task for the session
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    scheduler->Enqueue(task);
}

void UdpServer::OnRead(const_buffer &recv_buffer,
    const udp::endpoint &remote_endpoint) {
    UDP_SERVER_LOG_ERROR(this, UDP_DIR_IN, "Receive UDP: " <<
        "Default implementation of OnRead does NOT process received message");
    DeallocateBuffer(recv_buffer);
}

void UdpServer::HandleSend(boost::asio::const_buffer send_buffer,
    udp::endpoint remote_endpoint, std::size_t bytes_transferred,
    const boost::system::error_code& error) {
    DeallocateBuffer(send_buffer);
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

void UdpServer::GetRxSocketStats(SocketIOStats &socket_stats) const {
    stats_.GetRxStats(socket_stats);
}

void UdpServer::GetTxSocketStats(SocketIOStats &socket_stats) const {
    stats_.GetTxStats(socket_stats);
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
