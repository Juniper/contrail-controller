/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if.h>
#include <linux/if_ether.h>
#include <linux/if_tun.h>
#include <linux/if_packet.h>

#include "base/logging.h"
#include "cmn/agent_cmn.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "pkt/pkt_types.h"
#include "pkt/pkt_init.h"
#include "pkt0_interface.h"

#define TUN_INTF_CLONE_DEV "/dev/net/tun"

#define TAP_TRACE(obj, ...)                                              \
do {                                                                     \
    Tap##obj::TraceMsg(PacketTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
} while (false)                                                          \

///////////////////////////////////////////////////////////////////////////////

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

void Pkt0Interface::InitControlInterface() {
    pkt_handler()->agent()->set_pkt_interface_name(name_);

    if ((tap_fd_ = open(TUN_INTF_CLONE_DEV, O_RDWR)) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> opening tap-device");
        assert(0);
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    strncpy(ifr.ifr_name, name_.c_str(), IF_NAMESIZE);
    if (ioctl(tap_fd_, TUNSETIFF, (void *)&ifr) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> creating " << name_ << "tap-device");
        assert(0);
    }

    // We dont want the fd to be inherited by child process such as
    // virsh etc... So, close tap fd on fork.
    if (fcntl(tap_fd_, F_SETFD, FD_CLOEXEC) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> setting fcntl on " << name_ );
        assert(0);
    }

    if (ioctl(tap_fd_, TUNSETPERSIST, 0) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> making tap interface non-persistent");
        assert(0);
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name_.c_str(), IF_NAMESIZE);
    if (ioctl(tap_fd_, SIOCGIFHWADDR, (void *)&ifr) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " << strerror(errno) <<
            "> retrieving MAC address of the tap interface");
        assert(0);
    }
    memcpy(mac_address_, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

    int raw;
    if ((raw = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> creating socket");
        assert(0);
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name_.data(), IF_NAMESIZE);
    if (ioctl(raw, SIOCGIFINDEX, (void *)&ifr) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> getting ifindex of the tap interface");
        assert(0);
    }

    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(struct sockaddr_ll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = ifr.ifr_ifindex;
    sll.sll_protocol = htons(ETH_P_ALL);
    if (bind(raw, (struct sockaddr *)&sll,
             sizeof(struct sockaddr_ll)) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> binding the socket to the tap interface");
        assert(0);
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, name_.data(), IF_NAMESIZE);
    if (ioctl(raw, SIOCGIFFLAGS, (void *)&ifr) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> getting socket flags");
        assert(0);
    }

    ifr.ifr_flags |= IFF_UP;
    if (ioctl(raw, SIOCSIFFLAGS, (void *)&ifr) < 0) {
        LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
            strerror(errno) << "> setting socket flags");
        assert(0);
    }
    close(raw);

    boost::system::error_code ec;
    input_.assign(tap_fd_, ec);
    assert(ec == 0);

    VrouterControlInterface::InitControlInterface();
    AsyncRead();
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
