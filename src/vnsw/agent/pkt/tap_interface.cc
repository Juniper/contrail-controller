/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
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
#include "tap_interface.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "pkt/pkt_types.h"
#include "pkt_init.h"

#define TUN_INTF_CLONE_DEV "/dev/net/tun"

#define TAP_TRACE(obj, ...)                                              \
do {                                                                     \
    Tap##obj::TraceMsg(PacketTraceBuf, __FILE__, __LINE__, __VA_ARGS__); \
} while (false)                                                          \

///////////////////////////////////////////////////////////////////////////////

// TapDescriptor to create and hold the native device descriptor
class TapInterface::TapDescriptor {
public:
    TapDescriptor(const std::string &name);
    virtual ~TapDescriptor();
    int fd() const { return fd_; }
    const unsigned char *MacAddress() const { return mac_; }

private:
    int fd_;
    unsigned char mac_[ETH_ALEN];
};

TapDescriptor::TapDescriptor(const std::string &name) {
    if (name == Agent::GetInstance()->pkt_interface_name()) {
        if ((fd_ = open(TUN_INTF_CLONE_DEV, O_RDWR)) < 0) {
            LOG(ERROR, "Packet Tap Error <" << errno << ": " << 
                strerror(errno) << "> opening tap-device");
            assert(0);
        }   

        struct ifreq ifr;
        memset(&ifr, 0, sizeof(ifr));
        ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
        strncpy(ifr.ifr_name, name.c_str(), IF_NAMESIZE);
        if (ioctl(fd_, TUNSETIFF, (void *)&ifr) < 0) {
            LOG(ERROR, "Packet Tap Error <" << errno << ": " << 
                strerror(errno) << "> creating " << name << "tap-device");
            assert(0);
        }

        // We dont want the fd to be inherited by child process such as
        // virsh etc... So, close tap fd on fork.
        if (fcntl(fd_, F_SETFD, FD_CLOEXEC) < 0) {
            LOG(ERROR, "Packet Tap Error <" << errno << ": " <<
                strerror(errno) << "> setting fcntl on " << name );
            assert(0);
        }

        if (ioctl(fd_, TUNSETPERSIST, 1) < 0) {
            LOG(ERROR, "Packet Tap Error <" << errno << ": " << 
                strerror(errno) << "> making tap interface persistent");
            assert(0);
        }

        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, name.c_str(), IF_NAMESIZE);
        if (ioctl(fd_, SIOCGIFHWADDR, (void *)&ifr) < 0) {
            LOG(ERROR, "Packet Tap Error <" << errno << ": " << 
                strerror(errno) << 
                "> retrieving MAC address of the tap interface");
            assert(0);
        }
        memcpy(mac_, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

        int raw_;
        if ((raw_ = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
            LOG(ERROR, "Packet Tap Error <" << errno << ": " << 
                strerror(errno) << "> creating socket");
            assert(0);
        }

        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, name.data(), IF_NAMESIZE);
        if (ioctl(raw_, SIOCGIFINDEX, (void *)&ifr) < 0) {
            LOG(ERROR, "Packet Tap Error <" << errno << ": " << 
                strerror(errno) << "> getting ifindex of the tap interface");
            assert(0);
        }

        struct sockaddr_ll sll;
        memset(&sll, 0, sizeof(struct sockaddr_ll));
        sll.sll_family = AF_PACKET;
        sll.sll_ifindex = ifr.ifr_ifindex;
        sll.sll_protocol = htons(ETH_P_ALL);
        if (bind(raw_, (struct sockaddr *)&sll, 
                 sizeof(struct sockaddr_ll)) < 0) {
            LOG(ERROR, "Packet Tap Error <" << errno << ": " << 
                strerror(errno) << "> binding the socket to the tap interface");
            assert(0);
        }

        memset(&ifr, 0, sizeof(ifr));
        strncpy(ifr.ifr_name, name.data(), IF_NAMESIZE);
        if (ioctl(raw_, SIOCGIFFLAGS, (void *)&ifr) < 0) {
            LOG(ERROR, "Packet Tap Error <" << errno << ": " << 
                strerror(errno) << "> getting socket flags");
            assert(0);
        }

        ifr.ifr_flags |= IFF_UP;
        if (ioctl(raw_, SIOCSIFFLAGS, (void *)&ifr) < 0) {
            LOG(ERROR, "Packet Tap Error <" << errno << ": " << 
                strerror(errno) << "> setting socket flags");
            assert(0);
        }

        close(raw_);
    } else {
        // Test mode; we use a socket to receive packets in the test mode
        if ((fd_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
            LOG(ERROR, "Packet Test Tap Error : Cannot open test UDP socket");
            assert(0);
        }
        struct sockaddr_in sin;
        memset((char *) &sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = 0;
        sin.sin_addr.s_addr = inet_addr("127.0.0.1");
        if (bind(fd_, (sockaddr *) &sin, sizeof(sin)) == -1) {
            LOG(ERROR, "Packet Test Tap Error : Cannot bind to test UDP socket");
            assert(0);
        }
    }
}

TapInterface::TapDescriptor::~TapDescriptor() {
    close(fd_);
}

///////////////////////////////////////////////////////////////////////////////

TapInterface::TapInterface(const std::string &name, 
                           boost::asio::io_service &io,
                           PktReadCallback cb) 
                         : read_buf_(NULL), pkt_handler_(cb),
                           tap_(new TapDescriptor(name)), input_(io) {
    boost::system::error_code ec;
    input_.assign(tap_->fd(), ec);
    assert(ec == 0);

    AsyncRead();
}

TapInterface::~TapInterface() { 
    if (read_buf_) {
        delete [] read_buf_;
    }
}

int TapInterface::TapFd() const { 
    return tap_->fd();
}

const unsigned char *TapInterface::MacAddress() const { 
    return tap_->MacAddress();
}

void TapInterface::AsyncWrite(uint8_t *buf, std::size_t len) {
    input_.async_write_some(boost::asio::buffer(buf, len), 
                            boost::bind(&TapInterface::WriteHandler, this,
                                 boost::asio::placeholders::error,
                                 boost::asio::placeholders::bytes_transferred,
                                 buf));
}

void TapInterface::WriteHandler(const boost::system::error_code &error,
                                std::size_t length, uint8_t *buf) {
    if (error)
        TAP_TRACE(Err, 
                  "Packet Tap Error <" + error.message() + "> sending packet");
    delete [] buf;
}

void TapInterface::ReadHandler(const boost::system::error_code &error,
                              std::size_t length) {
    if (!error) {
        pkt_handler_(read_buf_, length);
    } else  {
        TAP_TRACE(Err, 
                  "Packet Tap Error <" + error.message() + "> reading packet");
        if (error == boost::system::errc::operation_canceled) {
            return;
        }
    }

    AsyncRead();
}

void TapInterface::AsyncRead() {
    read_buf_ = new uint8_t[kMaxPacketSize];
    input_.async_read_some(
            boost::asio::buffer(read_buf_, kMaxPacketSize), 
            boost::bind(&TapInterface::ReadHandler, this,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
}

///////////////////////////////////////////////////////////////////////////////
