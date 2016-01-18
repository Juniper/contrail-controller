/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/filesystem.hpp>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>

#include "base/logging.h"
#include "cmn/agent_cmn.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "pkt/pkt_types.h"
#include "pkt/pkt_init.h"
#include "pkt0_interface.h"

using namespace boost::asio;
#define TAP_TRACE(obj, ...)                                              \
do {                                                                     \
    Tap##obj::TraceMsg(PacketTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
} while (false)                                                          \

///////////////////////////////////////////////////////////////////////////////
const string Pkt0Socket::kSocketDir = "/var/run/vrouter";
const string Pkt0Socket::kAgentSocketPath = Pkt0Socket::kSocketDir
                                            + "/agent_pkt0";
const string Pkt0Socket::kVrouterSocketPath = Pkt0Socket::kSocketDir
                                            + "/dpdk_pkt0";

Pkt0Interface::Pkt0Interface(const std::string &name,
                             boost::asio::io_service *io) :
    name_(name), tap_fd_(-1), input_(*io), read_buff_(NULL), pkt_handler_(NULL) {
    memset(mac_address_, 0, sizeof(mac_address_));
}

Pkt0Interface::~Pkt0Interface() {
    if (read_buff_) {
        delete [] read_buff_;
    }
}

void Pkt0Interface::IoShutdownControlInterface() {
    boost::system::error_code ec;
    input_.close(ec);
    tap_fd_ = -1;
}

void Pkt0Interface::ShutdownControlInterface() {
}

void Pkt0Interface::WriteHandler(const boost::system::error_code &error,
                                 std::size_t length, PacketBufferPtr pkt,
                                 uint8_t *buff) {
    if (error)
        TAP_TRACE(Err,
                  "Packet Tap Error <" + error.message() + "> sending packet");
    delete [] buff;
}


void Pkt0Interface::AsyncRead() {
    read_buff_ = new uint8_t[kMaxPacketSize];
    input_.async_read_some(
            boost::asio::buffer(read_buff_, kMaxPacketSize),
            boost::bind(&Pkt0Interface::ReadHandler, this,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
}

void Pkt0Interface::ReadHandler(const boost::system::error_code &error,
                              std::size_t length) {
    if (error) {
        TAP_TRACE(Err,
                  "Packet Tap Error <" + error.message() + "> reading packet");
        if (error == boost::system::errc::operation_canceled) {
            return;
        }

        delete [] read_buff_;
        read_buff_ = NULL;
    }

    if (!error) {
        Agent *agent = pkt_handler()->agent();
        PacketBufferPtr pkt(agent->pkt()->packet_buffer_manager()->Allocate
            (PktHandler::RX_PACKET, read_buff_, kMaxPacketSize, 0, length,
             0));
        VrouterControlInterface::Process(pkt);
    }

    AsyncRead();
}

int Pkt0Interface::Send(uint8_t *buff, uint16_t buff_len,
                        const PacketBufferPtr &pkt) {
    std::vector<boost::asio::const_buffer> buff_list;
    buff_list.push_back(boost::asio::buffer(buff, buff_len));
    buff_list.push_back(boost::asio::buffer(pkt->data(), pkt->data_len()));

    input_.async_write_some(buff_list,
                            boost::bind(&Pkt0Interface::WriteHandler, this,
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::bytes_transferred,
                                        pkt, buff));
    return (buff_len + pkt->data_len());
}

Pkt0RawInterface::Pkt0RawInterface(const std::string &name,
                                   boost::asio::io_service *io) :
    Pkt0Interface(name, io) {
}

Pkt0RawInterface::~Pkt0RawInterface() {
    if (read_buff_) {
        delete [] read_buff_;
    }
}

Pkt0Socket::Pkt0Socket(const std::string &name,
    boost::asio::io_service *io):
    connected_(false), socket_(*io), timer_(NULL),
    read_buff_(NULL), pkt_handler_(NULL), name_(name){
}

Pkt0Socket::~Pkt0Socket() {
    if (read_buff_) {
        delete [] read_buff_;
    }
}

void Pkt0Socket::CreateUnixSocket() {
    boost::filesystem::create_directory(kSocketDir);
    boost::filesystem::remove(kAgentSocketPath);

    boost::system::error_code ec;
    socket_.open();
    local::datagram_protocol::endpoint ep(kAgentSocketPath);
    socket_.bind(ep, ec);
    if (ec) {
        LOG(DEBUG, "Error binding to the socket " << kAgentSocketPath
                << ": " << ec.message());
    }
    assert(ec == 0);
}

void Pkt0Socket::InitControlInterface() {
    CreateUnixSocket();
    VrouterControlInterface::InitControlInterface();
    AsyncRead();
    StartConnectTimer();
}

void Pkt0Socket::IoShutdownControlInterface() {
    if (read_buff_) {
        delete [] read_buff_;
    }

    boost::system::error_code ec;
    socket_.close(ec);
}

void Pkt0Socket::ShutdownControlInterface() {
}

void Pkt0Socket::AsyncRead() {
    read_buff_ = new uint8_t[kMaxPacketSize];
    socket_.async_receive(
            boost::asio::buffer(read_buff_, kMaxPacketSize), 
            boost::bind(&Pkt0Socket::ReadHandler, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));
}

void Pkt0Socket::StartConnectTimer() {
    Agent *agent = pkt_handler()->agent();
    timer_.reset(TimerManager::CreateTimer(
                 *(agent->event_manager()->io_service()),
                 "UnixSocketConnectTimer"));
    timer_->Start(kConnectTimeout,
                  boost::bind(&Pkt0Socket::OnTimeout, this));
}

bool Pkt0Socket::OnTimeout() {
    local::datagram_protocol::endpoint ep(kVrouterSocketPath);
    boost::system::error_code ec;
    socket_.connect(ep, ec);
    if (ec != 0) {
        LOG(DEBUG, "Error connecting to socket " << kVrouterSocketPath
                << ": " << ec.message());
        return true;
    }
    connected_ = true;
    return false;
}

int Pkt0Socket::Send(uint8_t *buff, uint16_t buff_len,
                      const PacketBufferPtr &pkt) {
    if (connected_ == false) {
        //queue the data?
        return (pkt->data_len());
    }

    std::vector<boost::asio::const_buffer> buff_list;
    buff_list.push_back(boost::asio::buffer(buff, buff_len));
    buff_list.push_back(boost::asio::buffer(pkt->data(), pkt->data_len()));

    socket_.async_send(buff_list,
                       boost::bind(&Pkt0Socket::WriteHandler, this,
                       boost::asio::placeholders::error,
                       boost::asio::placeholders::bytes_transferred,
                       pkt, buff));
    return (buff_len + pkt->data_len());
}

void Pkt0Socket::ReadHandler(const boost::system::error_code &error,
                             std::size_t length) {
    if (error) {
        TAP_TRACE(Err,
                  "Packet Error <" + error.message() + "> reading packet");
        if (error == boost::system::errc::operation_canceled) {
            return;
        }

        delete [] read_buff_;
        read_buff_ = NULL;
    }

    if (!error) {
        Agent *agent = pkt_handler()->agent();
        PacketBufferPtr pkt(agent->pkt()->packet_buffer_manager()->Allocate
            (PktHandler::RX_PACKET, read_buff_, kMaxPacketSize, 0, length,
             0));
        VrouterControlInterface::Process(pkt);
    }

    AsyncRead();
}

void Pkt0Socket::WriteHandler(const boost::system::error_code &error,
                              std::size_t length, PacketBufferPtr pkt,
                              uint8_t *buff) {
    if (error)
        TAP_TRACE(Err,
                  "Packet Error <" + error.message() + "> sending packet");
    delete [] buff;
}
